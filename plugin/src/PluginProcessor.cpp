#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    currentSampleRate = sampleRate > 0 ? sampleRate : 48000.0;
    audio.prepare (currentSampleRate, juce::jmax (1, samplesPerBlock));
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
            if (auto ts = pos->getTimeSignature()) { host.tsNumerator = ts->numerator; host.tsDenominator = ts->denominator; }
        }
    }
    hostValid.store (host.valid);
    hostPlaying.store (host.playing);
    hostBpm.store (host.bpm);
    hostTsNum.store (host.tsNumerator);
    hostTsDen.store (host.tsDenominator);

    audio.setOutputGain (outputGain->get());
    float* left = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0) : nullptr;
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    if (left == nullptr) return;
    // El motor escribe el bloque completo (sustituye el contenido). En mono suma L+R con su propia ganancia.
    const auto r = audio.process (left, right, n, host);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

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
    if (st.history.empty()) audio.publishSequence (nullptr);
    else
    {
        const auto& seq = st.history.current();
        const int64_t offset = (int64_t) (st.settings.startBar - 1) * seq.timeSignature.ticksPerBar();
        audio.publishSequence (makePlaybackData (seq, offset, st.settings.loop));
    }
    const FxSettings fx = st.settings.fxEnabled ? st.fx : FxSettings {};
    audio.publishFx (fx);
    audio.publishAutomation (st.settings.fxEnabled ? st.automation : AutomationSet {});
    tailSeconds.store (juce::jlimit (0.5, 12.0, EffectsChain::tailSeconds (fx, effectiveBpm()) + 0.5));
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
    return { hostValid.load(), hostPlaying.load(), hostBpm.load(), hostTsNum.load(), hostTsDen.load() };
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
