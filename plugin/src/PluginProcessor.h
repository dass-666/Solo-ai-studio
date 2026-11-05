#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "solo/AudioRenderer.h"
#include "solo/MidiFile.h"
#include "solo/Playback.h"
#include "solo/StateManager.h"

#include <atomic>

// Convierte texto UTF-8 (literales del código) a juce::String.
inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
inline juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

class SoloAIStudioProcessor : public juce::AudioProcessor,
                              public juce::ChangeBroadcaster,
                              private juce::Timer
{
public:
    SoloAIStudioProcessor();
    ~SoloAIStudioProcessor() override;

    // ---------------------------------------------------------------- AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Solo AI Studio"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---------------------------------------------------------------- API del editor (hilo de mensajes)
    solo::SoloSettings getSettings() const;
    void setSettings (const solo::SoloSettings& s); // no regenera
    bool hasSequence() const;
    solo::Sequence getCurrentSequence() const;
    void replaceCurrentSequence (const solo::Sequence& s); // ediciones del piano roll
    int variationCount() const;
    int variationIndex() const;

    // Devuelven false y un mensaje si no se pudo generar.
    bool generateNew (juce::String& message);
    bool regenerateBars (int fromBar, int toBar, juce::String& message);
    void previousVariation();
    void nextVariation();

    bool exportMidi (const juce::File& file, juce::String& message) const;
    bool exportWav (const juce::File& file, juce::String& message) const;
    juce::File writeTempMidiForDrag() const;

    void startPreview();
    void stopPreview();
    bool isPreviewing() const { return previewRunning.load(); }

    // Información del host para la interfaz.
    struct HostInfo { bool valid; bool playing; double bpm; int tsNum; int tsDen; };
    HostInfo getHostInfo() const;
    double effectiveBpm() const;
    // Posición de reproducción en ticks del solo (-1 si no suena).
    int64_t playheadTick() const { return playhead.load(); }
    juce::String getLastStatus() const;

    juce::AudioParameterFloat* outputGain = nullptr;

private:
    void timerCallback() override;
    void publishPlayback();
    void setStatus (const juce::String& s);

    // Estado de la sesión (solo hilo de mensajes; el lock protege get/setStateInformation).
    mutable juce::CriticalSection stateLock;
    solo::SoloState state;
    juce::String lastStatus;

    // Tiempo real
    solo::RealtimeSwap<solo::PlaybackData> playback;
    solo::InstrumentEngine engine;
    solo::SoloPlayer player;
    solo::ManualClock previewClock;
    std::vector<solo::TimedEvent> eventScratch;
    std::atomic<bool> previewStartRequest { false }, previewStopRequest { false }, previewRunning { false };
    std::atomic<bool> syncToHost { true }, useHostTempo { true }, midiOutEnabled { true };
    std::atomic<double> manualBpm { 120.0 };
    std::atomic<bool> hostValid { false }, hostPlaying { false };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int> hostTsNum { 4 }, hostTsDen { 4 };
    std::atomic<int64_t> playhead { -1 };
    std::atomic<int64_t> soloLengthTicks { 0 };
    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoloAIStudioProcessor)
};
