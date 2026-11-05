#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

using namespace solo;

SoloAIStudioProcessor::SoloAIStudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    addParameter (outputGain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Volumen",
                                                              juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f));
    eventScratch.resize (4096);
    engine.setInstrument (state.settings.instrument);
    startTimerHz (10);
}

SoloAIStudioProcessor::~SoloAIStudioProcessor()
{
    stopTimer();
}

// ======================================================================== audio
void SoloAIStudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0 ? sampleRate : 44100.0;
    engine.prepare (currentSampleRate);
    player.reset();
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
    buffer.clear();
    midi.clear(); // la entrada MIDI no se usa en esta versión; la salida son las notas del solo

    // ---- transporte del host
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

    const double bpm = TransportResolver::effectiveBpm (host, useHostTempo.load(), manualBpm.load());

    if (previewStartRequest.exchange (false)) { previewClock.start(); player.reset(); previewRunning.store (true); }
    if (previewStopRequest.exchange (false)) { previewClock.stop(); previewRunning.store (false); }

    const PlaybackData* data = playback.acquire();
    TransportState t;
    double offsetPpq = data ? (double) data->startOffsetTicks / kPPQ : 0.0;
    if (previewClock.isRunning())
    {
        t = previewClock.next (bpm, n, currentSampleRate);
        t.ppqPosition += offsetPpq; // el reloj interno empieza en el inicio del solo
        if (data && ! data->loop && (t.ppqPosition - offsetPpq) * kPPQ > (double) data->lengthTicks + kPPQ)
        {
            previewClock.stop();
            previewRunning.store (false);
        }
    }
    else if (syncToHost.load())
    {
        t = host; // posiciones del host => tempo del host
    }

    const int count = player.process (t, currentSampleRate, n, data, eventScratch.data(), (int) eventScratch.size());

    // Posición para la interfaz
    if (data && t.valid && t.playing && data->lengthTicks > 0)
    {
        const double local = t.ppqPosition * kPPQ - (double) data->startOffsetTicks;
        if (local < 0 || (! data->loop && local > (double) data->lengthTicks)) playhead.store (-1);
        else playhead.store ((int64_t) std::fmod (local, (double) data->lengthTicks));
    }
    else playhead.store (-1);

    engine.setOutputGain (outputGain->get());
    float* left = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0) : nullptr;
    float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    if (left != nullptr)
    {
        if (right == left)
        {
            // Mono: renderizar en estéreo temporal sería una reserva; sumamos a medias en el mismo canal.
            engine.process (left, left, n, eventScratch.data(), count);
            buffer.applyGain (0.5f);
        }
        else engine.process (left, right, n, eventScratch.data(), count);
    }

    if (midiOutEnabled.load())
    {
        for (int i = 0; i < count; ++i)
        {
            const auto& e = eventScratch[(size_t) i];
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
    state.settings.outputGain = outputGain->get();
    const auto text = serializeState (state);
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
        // Estado ilegible: se mantiene el estado actual (no se aplica nada a medias).
        setStatus (utf8 ("No se pudo restaurar el estado: ") + utf8 (error));
        return;
    }
    {
        const juce::ScopedLock sl (stateLock);
        state = std::move (loaded);
        engine.setInstrument (state.settings.instrument);
        syncToHost.store (state.settings.syncToHost);
        useHostTempo.store (state.settings.useHostTempo);
        manualBpm.store (state.settings.manualBpm);
        publishPlayback();
    }
    outputGain->setValueNotifyingHost (outputGain->convertTo0to1 ((float) state.settings.outputGain));
    setStatus (utf8 ("Sesión restaurada (") + juce::String (state.history.size()) + utf8 (" variaciones)"));
}

// ======================================================================== API del editor
SoloSettings SoloAIStudioProcessor::getSettings() const
{
    const juce::ScopedLock sl (stateLock);
    return state.settings;
}

void SoloAIStudioProcessor::setSettings (const SoloSettings& s)
{
    const juce::ScopedLock sl (stateLock);
    const bool placementChanged = s.startBar != state.settings.startBar || s.loop != state.settings.loop;
    state.settings = s;
    engine.setInstrument (s.instrument);
    syncToHost.store (s.syncToHost);
    useHostTempo.store (s.useHostTempo);
    manualBpm.store (s.manualBpm);
    if (placementChanged) publishPlayback();
}

bool SoloAIStudioProcessor::hasSequence() const
{
    const juce::ScopedLock sl (stateLock);
    return ! state.history.empty();
}

Sequence SoloAIStudioProcessor::getCurrentSequence() const
{
    const juce::ScopedLock sl (stateLock);
    return state.history.empty() ? Sequence {} : state.history.current();
}

void SoloAIStudioProcessor::replaceCurrentSequence (const Sequence& s)
{
    const juce::ScopedLock sl (stateLock);
    if (state.history.empty()) return;
    auto& cur = state.history.current();
    cur = s;
    cur.normalise();
    publishPlayback();
}

int SoloAIStudioProcessor::variationCount() const
{
    const juce::ScopedLock sl (stateLock);
    return state.history.size();
}

int SoloAIStudioProcessor::variationIndex() const
{
    const juce::ScopedLock sl (stateLock);
    return state.history.currentIndex();
}

double SoloAIStudioProcessor::effectiveBpm() const
{
    TransportState h;
    h.valid = hostValid.load();
    h.bpm = hostBpm.load();
    return TransportResolver::effectiveBpm (h, useHostTempo.load(), manualBpm.load());
}

bool SoloAIStudioProcessor::generateNew (juce::String& message)
{
    const juce::ScopedLock sl (stateLock);
    ComposerParams p;
    std::string err;
    if (! state.settings.toComposerParams (effectiveBpm(), p, err)) { message = utf8 (err); return false; }
    // Semilla nueva en cada "Generar" para obtener alternativas; queda guardada con la variación
    // y puede fijarse en modo Pro para reproducir exactamente un resultado.
    p.seed = state.settings.seed;
    auto seq = SoloComposer::compose (p);
    if (seq.notes.empty()) { message = utf8 ("No se generaron notas"); return false; }
    state.history.push (seq, p.seed);
    publishPlayback();
    const auto warnings = p.progression.validate (p.key);
    message = utf8 ("Variación ") + juce::String (state.history.currentIndex() + 1) + utf8 (" generada (semilla ")
              + juce::String ((juce::int64) p.seed) + ", " + juce::String ((int) seq.notes.size()) + " notas)";
    if (! warnings.empty()) message += utf8 (" · Aviso: ") + utf8 (warnings.front());
    return true;
}

bool SoloAIStudioProcessor::regenerateBars (int fromBar, int toBar, juce::String& message)
{
    const juce::ScopedLock sl (stateLock);
    if (state.history.empty()) { message = utf8 ("Primero genera un solo"); return false; }
    ComposerParams p;
    std::string err;
    if (! state.settings.toComposerParams (effectiveBpm(), p, err)) { message = utf8 (err); return false; }
    const auto current = state.history.current();
    const uint64_t newSeed = juce::Random::getSystemRandom().nextInt64() & 0x7FFFFFFFFFFFll;
    auto seq = SoloComposer::regenerateBars (current, p, fromBar, toBar, newSeed);
    // La regeneración parcial crea una variación nueva: la anterior se conserva.
    state.history.push (seq, state.history.currentSeed());
    publishPlayback();
    message = utf8 ("Compases ") + juce::String (fromBar + 1) + "-" + juce::String (toBar + 1)
              + utf8 (" regenerados (nueva variación)");
    return true;
}

void SoloAIStudioProcessor::previousVariation()
{
    const juce::ScopedLock sl (stateLock);
    if (! state.history.canGoBack()) return;
    state.history.back();
    publishPlayback();
}

void SoloAIStudioProcessor::nextVariation()
{
    const juce::ScopedLock sl (stateLock);
    if (! state.history.canGoForward()) return;
    state.history.forward();
    publishPlayback();
}

void SoloAIStudioProcessor::publishPlayback()
{
    // Llamar con stateLock tomado (serializa a los productores de RealtimeSwap).
    if (state.history.empty())
    {
        playback.publish (nullptr);
        soloLengthTicks.store (0);
        return;
    }
    const auto& seq = state.history.current();
    const int64_t offset = (int64_t) (state.settings.startBar - 1) * seq.timeSignature.ticksPerBar();
    auto data = makePlaybackData (seq, offset, state.settings.loop);
    soloLengthTicks.store (data->lengthTicks);
    playback.publish (std::move (data));
}

static Key keyOrDefault (const SoloSettings& s)
{
    Key k;
    Key::parse (s.keyText, k);
    return k;
}

bool SoloAIStudioProcessor::exportMidi (const juce::File& file, juce::String& message) const
{
    Sequence seq;
    MidiExportOptions o;
    {
        const juce::ScopedLock sl (stateLock);
        if (state.history.empty()) { message = utf8 ("No hay solo que exportar"); return false; }
        seq = state.history.current();
        o.format = state.settings.midiFormat;
        o.key = keyOrDefault (state.settings);
        o.gmProgram = profileFor (state.settings.instrument).gmProgram;
        o.trackName = std::string ("Solo AI Studio - ") + instrumentName (state.settings.instrument);
    }
    seq.bpm = effectiveBpm();
    const auto bytes = writeMidiFile (seq, o);
    if (! file.replaceWithData (bytes.data(), bytes.size()))
    {
        message = utf8 ("No se pudo escribir ") + file.getFullPathName();
        return false;
    }
    message = utf8 ("MIDI exportado: ") + file.getFileName() + " (" + juce::String ((int) seq.notes.size()) + " notas, "
              + juce::String (seq.bpm, 2) + " BPM)";
    return true;
}

bool SoloAIStudioProcessor::exportWav (const juce::File& file, juce::String& message) const
{
    Sequence seq;
    RenderOptions ro;
    {
        const juce::ScopedLock sl (stateLock);
        if (state.history.empty()) { message = utf8 ("No hay solo que exportar"); return false; }
        seq = state.history.current();
        ro.sampleRate = state.settings.wavSampleRate;
        ro.instrument = state.settings.instrument;
    }
    ro.gain = outputGain->get();
    seq.bpm = effectiveBpm();
    const auto r = renderSequence (seq, ro);
    const auto bytes = encodeWav24 (r.left, r.right, (int) ro.sampleRate);
    if (! file.replaceWithData (bytes.data(), bytes.size()))
    {
        message = utf8 ("No se pudo escribir ") + file.getFullPathName();
        return false;
    }
    message = utf8 ("WAV exportado: ") + file.getFileName() + " (" + juce::String (ro.sampleRate / 1000.0, 1) + " kHz, 24 bits, pico "
              + juce::String (juce::Decibels::gainToDecibels (r.peak), 1) + " dBFS)";
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

void SoloAIStudioProcessor::startPreview() { previewStopRequest.store (false); previewStartRequest.store (true); }
void SoloAIStudioProcessor::stopPreview() { previewStartRequest.store (false); previewStopRequest.store (true); }

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
    playback.collectGarbage();
}

juce::AudioProcessorEditor* SoloAIStudioProcessor::createEditor()
{
    return new SoloAIStudioEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SoloAIStudioProcessor();
}
