#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "solo/VoiceAnalysis.h"
#include "solo/VocalProposalAdapter.h"

#include <cmath>

using namespace solo;

SoloAIStudioProcessor::SoloAIStudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter (outputGain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Volumen",
                                                              juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    {
        const juce::ScopedLock sl (stateLock);
        applyRealtimeSettings();
        publishAll();
    }
    startTimerHz (10);
}

SoloAIStudioProcessor::~SoloAIStudioProcessor()
{
    stopTimer();
}

uint64_t SoloAIStudioProcessor::freshSeed()
{
    return (uint64_t) (juce::Random::getSystemRandom().nextInt64() & 0x7FFFFFFFFFFFll) + 1;
}

// ======================================================================== audio
void SoloAIStudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool rateChanged = std::abs (currentSampleRate - sampleRate) > 0.5;
    currentSampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    audio.prepare (currentSampleRate, juce::jmax (1, samplesPerBlock));
    if (rateChanged)
    {
        // La IR se remuestrea a la frecuencia del motor: hay que volver a prepararla.
        const juce::ScopedLock sl (stateLock);
        irPath.clear();
        publishAll();
    }
}

void SoloAIStudioProcessor::releaseResources() {}

bool SoloAIStudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet().isDisabled() || layouts.inputBuses.isEmpty();
}

void SoloAIStudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    midi.clear(); // la entrada MIDI no se usa; la salida son las notas del solo

    TransportState host;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            host.playing = pos->getIsPlaying();
            const auto bpm = pos->getBpm();
            const auto ppq = pos->getPpqPosition();
            if (bpm) host.bpm = *bpm;
            if (ppq) host.ppqPosition = *ppq;
            host.valid = bpm.hasValue() && ppq.hasValue() && *bpm > 0.0;
            if (const auto ts = pos->getTimeInSamples()) { host.timeInSamplesValid = true; host.timeInSamples = *ts; }
            if (auto ts = pos->getTimeSignature()) { host.tsNumerator = ts->numerator; host.tsDenominator = ts->denominator; }
        }
    }
    hostValid.store (host.valid);
    hostPlaying.store (host.playing);
    hostBpm.store (host.bpm);
    hostTsNum.store (host.tsNumerator);
    hostTsDen.store (host.tsDenominator);
    hostHasSampleTime.store (host.timeInSamplesValid);
    lastBlockSize.store (n);

    audio.setOutputGain (outputGain->get());
    float* left = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0) : nullptr;
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    if (left == nullptr) return;
    // El motor escribe el bloque completo (sustituye el contenido). En mono suma L+R con su propia ganancia.
    const auto r = audio.process (left, right, n, host);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    // Registro de transporte (sin reservas: anillo fijo).
    {
        const uint32_t i = transportLogPos.fetch_add (1) % (uint32_t) kTransportLog;
        auto& rec = transportLog[i];
        rec.hostMs = juce::Time::getMillisecondCounterHiRes();
        rec.bpm = host.bpm; rec.ppq = host.ppqPosition; rec.deviationPpq = r.deviationPpq;
        rec.timeInSamples = host.timeInSamplesValid ? host.timeInSamples : -1;
        rec.numSamples = n; rec.events = r.eventCount;
        rec.valid = host.valid; rec.playing = host.playing; rec.resynced = r.resynced; rec.effectivePlaying = r.transportPlaying;
        if (r.resynced) resyncsSeen.fetch_add (1);
    }

    if (midiOutEnabled.load())
    {
        // Solo notas (on/off). La expresión (bends, vibrato) va en el MIDI EXPORTADO como pitch bend;
        // en la salida MIDI en directo no se envía todavía (ver docs/MIDI.md).
        const auto* ev = audio.events();
        for (int i = 0; i < r.eventCount; ++i)
        {
            const auto& e = ev[i];
            const int off = juce::jlimit (0, juce::jmax (0, n - 1), e.sampleOffset);
            if (e.type == EventType::NoteOn)
                midi.addEvent (juce::MidiMessage::noteOn (1, e.pitch, (juce::uint8) e.velocity), off);
            else
                midi.addEvent (juce::MidiMessage::noteOff (1, e.pitch), off);
        }
    }
}

// ======================================================================== estado
void SoloAIStudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    const juce::ScopedLock sl (stateLock);
    project.state.settings.outputGain = outputGain->get();
    const auto text = serializeState (project.state);
    destData.replaceAll (text.data(), text.size());
}

void SoloAIStudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (data == nullptr || sizeInBytes <= 0) return;
    const std::string text ((const char*) data, (size_t) sizeInBytes);
    SoloState loaded;
    std::string error;
    if (! deserializeState (text, loaded, error))
    {
        // Estado ilegible: se mantiene el actual (no se aplica nada a medias).
        setStatus (utf8 ("No se pudo restaurar el estado: ") + utf8 (error));
        return;
    }
    const int fromVersion = loaded.loadedFromVersion;
    int variations = 0;
    {
        const juce::ScopedLock sl (stateLock);
        project.state = std::move (loaded);
        project.undo.clear();
        variations = project.state.history.size();
        applyRealtimeSettings();
        publishAll();
    }
    outputGain->setValueNotifyingHost (outputGain->convertTo0to1 ((float) getSettings().outputGain));
    auto msg = utf8 ("Sesión restaurada (") + juce::String (variations) + utf8 (" variaciones)");
    if (fromVersion < SoloState::kVersion) msg += utf8 (" · migrada desde la versión ") + juce::String (fromVersion);
    setStatus (msg);
}

// ======================================================================== publicación al hilo de audio
void SoloAIStudioProcessor::applyRealtimeSettings()
{
    const auto& s = project.state.settings;
    audio.setInstrument (s.instrument);
    audio.setSyncToHost (s.syncToHost);
    audio.setUseHostTempo (s.useHostTempo);
    audio.setManualBpm (s.manualBpm);
}

void SoloAIStudioProcessor::publishAll()
{
    // Llamar con stateLock tomado (un solo productor por RealtimeSwap).
    const auto& st = project.state;
    // Solo se republica si cambian las notas, el inicio o el loop: republicar corta la nota que suena.
    if (st.history.empty()) audio.publishSequenceIfChanged (nullptr, 0, false);
    else
    {
        const auto& seq = st.history.current();
        const int64_t offset = (int64_t) (st.settings.startBar - 1) * seq.timeSignature.ticksPerBar();
        audio.publishSequenceIfChanged (&seq, offset, st.settings.loop);
    }
    const FxSettings fx = st.settings.fxEnabled ? st.fx : FxSettings {};
    audio.publishFx (fx);
    audio.publishAutomation (st.settings.fxEnabled ? st.automation : AutomationSet {});
    tailSeconds.store (juce::jlimit (0.5, 12.0, EffectsChain::tailSeconds (fx, effectiveBpm()) + 0.5));
    // v3: rig (motor, fuente, expresión), banco SFZ e IR
    ensureRigResources();
    audio.publishRig (project.rigSettings());
    audio.publishSampleBank (bank);
    audio.publishImpulse (impulse);
    const int lat = audio.latencySamples();
    if (lat != publishedLatency) { publishedLatency = lat; setLatencySamples (lat); }
}

void SoloAIStudioProcessor::ensureRigResources()
{
    const auto& st = project.state;
    // Banco de samples: solo se (re)carga si cambia la ruta. Un fallo deja el motor de samples en silencio y se informa.
    const std::string wantBank = st.settings.guitarEngine == (int) GuitarEngineKind::Samples ? st.settings.sfzPath : std::string();
    if (wantBank != bankPath)
    {
        bankPath = wantBank;
        bank.reset();
        bankError.clear(); bankSummary.clear();
        if (! wantBank.empty())
        {
            auto b = std::make_shared<SampleBank>();
            std::string err;
            if (loadSfz (wantBank, *b, err)) { bank = b; bankSummary = b->summary(); bankError = err; }
            else bankError = err.empty() ? "No se pudo cargar el banco" : err;
        }
    }
    // Impulso de pantalla del usuario (WAV mono/estéreo): se remuestrea a la frecuencia del motor.
    const std::string wantIr = st.fx.ampModel == 1 ? st.fx.cabIrPath : std::string();
    if (wantIr != irPath)
    {
        irPath = wantIr;
        impulse.reset();
        irError.clear(); irSummary.clear();
        if (! wantIr.empty())
        {
            MonoAudio a;
            std::string err;
            if (! readWavFile (wantIr, a, err)) irError = err;
            else
            {
                auto ir = std::make_shared<CabImpulse>();
                if (prepareCabImpulse (a.samples, a.sampleRate, currentSampleRate, *ir, err))
                {
                    ir->name = juce::File (wantIr).getFileName().toStdString();
                    impulse = ir;
                    irSummary = ir->name + " (" + std::to_string (ir->taps.size()) + " muestras a " + std::to_string ((int) currentSampleRate) + " Hz)";
                    irError = err;
                }
                else irError = err;
            }
        }
    }
}

// ---------------------------------------------------------------- V4: beat y Melody Explorer
bool SoloAIStudioProcessor::importBeat (const juce::File& wav, juce::String& message)
{
    MonoAudio in;
    std::string err;
    if (! readWavFile (wav.getFullPathName().toStdString(), in, err)) { message = utf8 ("No se pudo leer el WAV: ") + utf8 (err); return false; }
    // Análisis offline en el hilo de mensajes (medido: ~0.7 s por 2 minutos de audio). Nunca en processBlock.
    const auto r = perform ([&] (ProjectController& p) {
        auto res = p.importBeat (in, wav.getFullPathName().toStdString());
        if (res.ok && ! p.state.beat.harmony.segments.empty())
        {
            const auto a = p.applyBeatToSettings(); // tonalidad, acordes, compases y BPM del beat pasan al compositor
            if (a.ok) res.message += "\n" + a.message;
        }
        return res;
    });
    message = utf8 (r.message);
    setStatus (message);
    return r.ok;
}

bool SoloAIStudioProcessor::proposeRhythmFromFile (const juce::File& wav, juce::String& message)
{
    MonoAudio in; std::string err;
    if (! readWavFile (wav.getFullPathName().toStdString(), in, err)) { message = utf8 ("No se pudo leer el WAV: ") + utf8 (err); return false; }
    const auto va = analyseVoice (in);
    const auto r = perform ([&] (ProjectController& p) { return p.proposeRhythm (va, {}); });
    message = utf8 (r.message);
    return r.ok;
}

bool SoloAIStudioProcessor::proposeMelodyFromFile (const juce::File& wav, juce::String& message)
{
    MonoAudio in; std::string err;
    if (! readWavFile (wav.getFullPathName().toStdString(), in, err)) { message = utf8 ("No se pudo leer el WAV: ") + utf8 (err); return false; }
    const auto va = analyseVoice (in);
    VocalProposalOptions o;
    o.seed = freshSeed();
    const auto r = perform ([&] (ProjectController& p) { return p.proposeFromVoice (va, o); });
    message = utf8 (r.message);
    return r.ok;
}

bool SoloAIStudioProcessor::correctRegionFromFile (const juce::File& wav, int fromBar, int toBar, juce::String& message)
{
    MonoAudio in; std::string err;
    if (! readWavFile (wav.getFullPathName().toStdString(), in, err)) { message = utf8 ("No se pudo leer el WAV: ") + utf8 (err); return false; }
    const auto va = analyseVoice (in);
    const auto r = perform ([&] (ProjectController& p) { return p.correctRegionWithVoice (va, fromBar, toBar); });
    message = utf8 (r.message);
    return r.ok;
}

SoloAIStudioProcessor::BeatView SoloAIStudioProcessor::getBeatView() const
{
    BeatView v;
    const juce::ScopedLock sl (stateLock);
    const auto& b = project.state.beat;
    v.melodyActive = project.state.melody.active;
    if (v.melodyActive)
    {
        v.identity = utf8 (project.state.melody.identity.description);
        v.diversity = project.state.melody.diversity;
        if (project.hasSequence()) v.currentStrategy = project.state.melody.strategyOf (project.state.history.currentItem().id);
    }
    if (! b.loaded()) return v;
    v.loaded = true;
    v.percussive = b.analysis.percussiveOnly;
    v.path = utf8 (b.path);
    v.summary = utf8 (b.analysis.summary());
    v.bpm = b.harmony.bpm; v.bpmConfidence = b.harmony.bpmConfidence; v.bpmConfirmed = b.harmony.bpmProvenance == Provenance::Confirmed;
    v.key = utf8 (b.harmony.key.toString()); v.keyConfidence = b.harmony.keyConfidence; v.keyConfirmed = b.harmony.keyProvenance == Provenance::Confirmed;
    v.chordConfidence = b.analysis.chordConfidence;
    v.bars = std::max (b.harmony.bars, b.analysis.bars);
    v.sectionFrom = b.sectionFromBar; v.sectionTo = b.sectionToBar;
    if (! b.harmony.segments.empty())
        for (int bar = 0; bar < b.harmony.bars; ++bar) { v.chordPerBar.push_back (utf8 (b.harmony.chordAtBar (bar).toString (b.harmony.key.prefersFlats()))); v.chordConfPerBar.push_back (b.harmony.chordConfidenceAtBar (bar)); }
    v.energyPerBar.assign ((size_t) v.bars, 1);
    for (const auto& e : b.analysis.energyRegions)
        for (int bar = e.startBar; bar < e.endBar && bar < v.bars; ++bar) if (bar >= 0) v.energyPerBar[(size_t) bar] = (int) e.level;
    for (const auto& w : b.analysis.warnings) v.warnings.push_back (utf8 (w));
    return v;
}

SoloAIStudioProcessor::RigResources SoloAIStudioProcessor::getRigResources() const
{
    const juce::ScopedLock sl (stateLock);
    RigResources r;
    r.bankSummary = utf8 (bankSummary); r.bankError = utf8 (bankError); r.irSummary = utf8 (irSummary); r.irError = utf8 (irError);
    r.latencySamples = publishedLatency;
    return r;
}

// ======================================================================== API del editor
OpResult SoloAIStudioProcessor::perform (const std::function<OpResult (ProjectController&)>& op)
{
    OpResult r;
    {
        const juce::ScopedLock sl (stateLock);
        project.bpm = effectiveBpm();
        r = op (project);
        // Republicar siempre: deshacer puede cambiar notas y efectos a la vez, y publicar es barato.
        applyRealtimeSettings();
        publishAll();
    }
    setStatus (utf8 (r.ok ? "" : "No se pudo: ") + utf8 (r.message));
    return r;
}

void SoloAIStudioProcessor::read (const std::function<void (const ProjectController&)>& f) const
{
    const juce::ScopedLock sl (stateLock);
    f (project);
}

SoloSettings SoloAIStudioProcessor::getSettings() const
{
    const juce::ScopedLock sl (stateLock);
    return project.state.settings;
}

void SoloAIStudioProcessor::setSettings (const SoloSettings& s)
{
    const juce::ScopedLock sl (stateLock);
    project.state.settings = s;
    applyRealtimeSettings();
    publishAll();
}

bool SoloAIStudioProcessor::hasSequence() const
{
    const juce::ScopedLock sl (stateLock);
    return project.hasSequence();
}

Sequence SoloAIStudioProcessor::getCurrentSequence() const
{
    const juce::ScopedLock sl (stateLock);
    return project.hasSequence() ? *project.current() : Sequence {};
}

void SoloAIStudioProcessor::replaceCurrentSequence (const Sequence& s)
{
    perform ([&] (ProjectController& p) { return p.replaceSequence (s, "Editar notas"); });
}

int SoloAIStudioProcessor::variationCount() const
{
    const juce::ScopedLock sl (stateLock);
    return project.state.history.size();
}

int SoloAIStudioProcessor::variationIndex() const
{
    const juce::ScopedLock sl (stateLock);
    return project.state.history.currentIndex();
}

bool SoloAIStudioProcessor::canUndo() const
{
    const juce::ScopedLock sl (stateLock);
    return project.undo.canUndo();
}

bool SoloAIStudioProcessor::canRedo() const
{
    const juce::ScopedLock sl (stateLock);
    return project.undo.canRedo();
}

double SoloAIStudioProcessor::effectiveBpm() const
{
    TransportState h;
    h.valid = hostValid.load();
    h.bpm = hostBpm.load();
    const auto s = [this] { const juce::ScopedLock sl (stateLock); return std::make_pair (project.state.settings.useHostTempo, project.state.settings.manualBpm); }();
    return TransportResolver::effectiveBpm (h, s.first, s.second);
}

bool SoloAIStudioProcessor::generateNew (juce::String& message)
{
    // La semilla la decide el editor (fija en modo Pro o nueva en cada "Generar") a través de settings.seed.
    const auto seed = getSettings().seed;
    const auto r = perform ([&] (ProjectController& p) { return p.generate (seed); });
    message = utf8 (r.message);
    return r.ok;
}

bool SoloAIStudioProcessor::regenerateBars (int fromBar, int toBar, juce::String& message)
{
    const auto seed = freshSeed();
    const auto r = perform ([&] (ProjectController& p) { return p.regenerateBars (fromBar, toBar, seed); });
    message = utf8 (r.message);
    return r.ok;
}

void SoloAIStudioProcessor::previousVariation()
{
    perform ([] (ProjectController& p) {
        const int i = p.state.history.currentIndex();
        return i > 0 ? p.selectVariation (i - 1) : OpResult::fail ("Ya es la primera variación");
    });
}

void SoloAIStudioProcessor::nextVariation()
{
    perform ([] (ProjectController& p) {
        const int i = p.state.history.currentIndex();
        return i + 1 < p.state.history.size() ? p.selectVariation (i + 1) : OpResult::fail ("Ya es la última variación");
    });
}

bool SoloAIStudioProcessor::importVoice (const juce::File& wav, const HumToSoloOptions& o, juce::String& message)
{
    MonoAudio in;
    std::string err;
    if (! readWavFile (wav.getFullPathName().toStdString(), in, err)) { message = utf8 ("No se pudo leer el WAV: ") + utf8 (err); return false; }
    // Análisis en el hilo de mensajes: unas décimas de segundo para tomas cortas (medido en pruebas con voces sintéticas).
    const auto va = analyseVoice (in);
    const auto r = perform ([&] (ProjectController& p) { return p.applyHum (va, o); });
    int pitched = 0;
    for (const auto& n : va.notes) pitched += n.pitched ? 1 : 0;
    message = utf8 (r.message) + utf8 (" · voz: ") + juce::String ((int) va.notes.size()) + utf8 (" notas detectadas (")
              + juce::String (pitched) + utf8 (" con altura), confianza ") + juce::String (va.meanConfidence, 2);
    if (! va.pitchReliable) message += utf8 (" · altura poco fiable: se usó el ritmo");
    setStatus (message);
    return r.ok;
}

ParsedCommand SoloAIStudioProcessor::parseCommandText (const juce::String& text) const
{
    RecognizedText t;
    t.text = text.toStdString();
    return parseCommand (t);
}

CommandPreview SoloAIStudioProcessor::previewParsed (const ParsedCommand& c) const
{
    const juce::ScopedLock sl (stateLock);
    return previewCommand (project, c, 12345);
}

CommandResult SoloAIStudioProcessor::executeParsed (const ParsedCommand& c)
{
    CommandResult r;
    const auto seed = freshSeed();
    {
        const juce::ScopedLock sl (stateLock);
        project.bpm = effectiveBpm();
        r = executeCommand (project, c, seed);
        applyRealtimeSettings();
        publishAll();
    }
    if (r.requestPlay) startPreview();
    if (r.requestStop) stopPreview();
    juce::String msg;
    for (const auto& x : r.results) msg << (x.ok ? utf8 ("✓ ") : utf8 ("✗ ")) << utf8 (x.message) << "  ";
    setStatus (msg.trim());
    return r;
}

bool SoloAIStudioProcessor::exportMidi (const juce::File& file, juce::String& message) const
{
    Sequence seq;
    MidiExportOptions o;
    {
        const juce::ScopedLock sl (stateLock);
        if (! project.hasSequence()) { message = utf8 ("No hay solo que exportar"); return false; }
        const auto& s = project.state.settings;
        seq = *project.current();
        o.format = s.midiFormat;
        Key::parse (s.keyText, o.key);
        o.gmProgram = profileFor (s.instrument).gmProgram;
        o.trackName = std::string ("Solo AI Studio - ") + instrumentName (s.instrument);
        o.pitchBend = s.midiPitchBend;
        // v3: con los motores nuevos las curvas exportadas son los mismos gestos que suenan (escalados por Expresión).
        if (s.guitarEngine != (int) GuitarEngineKind::LegacyV2 && s.instrument == InstrumentId::Guitar) o.expressionPercent = project.state.macros.expression;
    }
    seq.bpm = effectiveBpm();
    const auto bytes = writeMidiFile (seq, o);
    if (! file.replaceWithData (bytes.data(), bytes.size()))
    {
        message = utf8 ("No se pudo escribir ") + file.getFullPathName();
        return false;
    }
    message = utf8 ("MIDI exportado: ") + file.getFileName() + " (" + juce::String ((int) seq.notes.size()) + " notas, "
              + juce::String (seq.bpm, 2) + " BPM" + (o.pitchBend ? utf8 (", con pitch bend") : juce::String()) + ")";
    return true;
}

bool SoloAIStudioProcessor::exportWav (const juce::File& file, juce::String& message) const
{
    Sequence seq;
    EngineRenderOptions ro;
    {
        const juce::ScopedLock sl (stateLock);
        if (! project.hasSequence()) { message = utf8 ("No hay solo que exportar"); return false; }
        const auto& st = project.state;
        seq = *project.current();
        ro.sampleRate = st.settings.wavSampleRate;
        ro.instrument = st.settings.instrument;
        if (st.settings.fxEnabled) { ro.fx = st.fx; ro.automation = st.automation; }
        ro.rig = project.rigSettings();
        ro.bank = bank;
        ro.impulse = impulse;
    }
    ro.gain = outputGain->get();
    seq.bpm = effectiveBpm();
    // Mismo motor que la escucha en directo: efectos, automatización y cola de delay/reverb incluidos.
    const auto r = renderWithEngine (seq, ro);
    const auto bytes = encodeWav24 (r.left, r.right, (int) ro.sampleRate);
    if (! file.replaceWithData (bytes.data(), bytes.size()))
    {
        message = utf8 ("No se pudo escribir ") + file.getFullPathName();
        return false;
    }
    message = utf8 ("WAV exportado: ") + file.getFileName() + " (" + juce::String (ro.sampleRate / 1000.0, 1) + " kHz, 24 bits, pico "
              + juce::String (juce::Decibels::gainToDecibels (r.peak), 1) + " dBFS, cola " + juce::String (r.tailSeconds, 1) + " s)";
    return true;
}

juce::File SoloAIStudioProcessor::writeTempMidiForDrag() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("SoloAIStudio");
    dir.createDirectory();
    const auto f = dir.getChildFile ("Solo AI Studio " + juce::String (juce::Time::getCurrentTime().toMilliseconds() % 100000) + ".mid");
    juce::String msg;
    return exportMidi (f, msg) ? f : juce::File();
}

void SoloAIStudioProcessor::startPreview() { audio.requestPreviewStart(); }
void SoloAIStudioProcessor::stopPreview() { audio.requestPreviewStop(); }

SoloAIStudioProcessor::HostInfo SoloAIStudioProcessor::getHostInfo() const
{
    return { hostValid.load(), hostPlaying.load(), hostBpm.load(), hostTsNum.load(), hostTsDen.load(), resyncsSeen.load(),
             hostHasSampleTime.load() };
}

juce::String SoloAIStudioProcessor::transportDiagnosticText() const
{
    // Copia del anillo (lectura sin bloqueo: algún registro puede estar a medio escribir; es un diagnóstico).
    const uint32_t end = transportLogPos.load();
    const uint32_t count = juce::jmin ((uint32_t) kTransportLog, end);
    juce::String t;
    t << "Solo AI Studio - diagnostico de transporte" << juce::newLine;
    t << "frecuencia " << currentSampleRate << " Hz, ultimo bloque " << lastBlockSize.load() << " muestras, "
      << "posicion en muestras del host: " << (hostHasSampleTime.load() ? "si" : "no")
      << ", resincronizaciones totales: " << (int) resyncsSeen.load() << juce::newLine;
    t << "Columnas: ms | valido | play(host) | play(efectivo) | bpm | ppq | muestras | n | eventos | resync | desviacion(ppq)" << juce::newLine;
    const double t0 = count > 0 ? transportLog[(end - count) % (uint32_t) kTransportLog].hostMs : 0.0;
    for (uint32_t k = 0; k < count; ++k)
    {
        const auto& r = transportLog[(end - count + k) % (uint32_t) kTransportLog];
        t << juce::String (r.hostMs - t0, 2) << " | " << (r.valid ? 1 : 0) << " | " << (r.playing ? 1 : 0) << " | "
          << (r.effectivePlaying ? 1 : 0) << " | " << juce::String (r.bpm, 3) << " | " << juce::String (r.ppq, 6) << " | "
          << juce::String (r.timeInSamples) << " | " << r.numSamples << " | " << r.events << " | " << (r.resynced ? 1 : 0)
          << " | " << juce::String (r.deviationPpq, 6) << juce::newLine;
    }
    return t;
}

juce::String SoloAIStudioProcessor::getLastStatus() const
{
    const juce::ScopedLock sl (stateLock);
    return lastStatus;
}

void SoloAIStudioProcessor::setStatus (const juce::String& s)
{
    {
        const juce::ScopedLock sl (stateLock);
        lastStatus = s;
    }
    sendChangeMessage();
}

void SoloAIStudioProcessor::timerCallback()
{
    audio.collectGarbage();
}

juce::AudioProcessorEditor* SoloAIStudioProcessor::createEditor()
{
    return new SoloAIStudioEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SoloAIStudioProcessor();
}
