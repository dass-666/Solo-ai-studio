#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "solo/AudioRenderer.h"
#include "solo/Commands.h"
#include "solo/MidiFile.h"
#include "solo/Project.h"
#include "solo/SoloEngine.h"
#include "solo/StateManager.h"

#include <atomic>
#include <functional>

// Convierte texto UTF-8 (literales del código) a juce::String.
inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
inline juce::String utf8 (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

// El procesador es una capa fina:
//   - el proyecto (notas, variaciones, efectos, deshacer) vive en solo::ProjectController (núcleo, sin JUCE);
//   - el audio en tiempo real lo hace solo::SoloEngine, el MISMO motor que la exportación WAV.
// Toda edición pasa por perform(), que toma el lock, ejecuta la operación y vuelve a publicar al hilo de audio.
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
    double getTailLengthSeconds() const override { return tailSeconds.load(); }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---------------------------------------------------------------- API del editor (hilo de mensajes)
    // Ejecuta una operación del proyecto (con deshacer cuando corresponde), republica y notifica.
    solo::OpResult perform (const std::function<solo::OpResult (solo::ProjectController&)>& op);
    // Lectura consistente del proyecto.
    void read (const std::function<void (const solo::ProjectController&)>& f) const;

    solo::SoloSettings getSettings() const;
    void setSettings (const solo::SoloSettings& s); // no regenera ni entra en deshacer
    bool hasSequence() const;
    solo::Sequence getCurrentSequence() const;
    void replaceCurrentSequence (const solo::Sequence& s); // ediciones del piano roll (deshacibles)
    int variationCount() const;
    int variationIndex() const;
    bool canUndo() const;
    bool canRedo() const;

    bool generateNew (juce::String& message);
    bool regenerateBars (int fromBar, int toBar, juce::String& message);
    void previousVariation();
    void nextVariation();

    // Voz: lee un WAV, lo analiza y lo convierte en solo (modo indicado). Devuelve el resumen del análisis.
    bool importVoice (const juce::File& wav, const solo::HumToSoloOptions& o, juce::String& message);
    // Dirección por texto: interpretar, previsualizar sobre una copia y ejecutar.
    solo::ParsedCommand parseCommandText (const juce::String& text) const;
    solo::CommandPreview previewParsed (const solo::ParsedCommand& c) const;
    solo::CommandResult executeParsed (const solo::ParsedCommand& c);

    bool exportMidi (const juce::File& file, juce::String& message) const;
    bool exportWav (const juce::File& file, juce::String& message) const;
    juce::File writeTempMidiForDrag() const;

    void startPreview();
    void stopPreview();
    bool isPreviewing() const { return audio.isPreviewing(); }

    struct HostInfo { bool valid; bool playing; double bpm; int tsNum; int tsDen; };
    HostInfo getHostInfo() const;
    double effectiveBpm() const;
    int64_t playheadTick() const { return audio.playheadTick(); }
    juce::String getLastStatus() const;

    juce::AudioParameterFloat* outputGain = nullptr;

private:
    void timerCallback() override;
    void publishAll();   // llamar con stateLock tomado
    void applyRealtimeSettings();
    void setStatus (const juce::String& s);
  public:
    static uint64_t freshSeed();
  private:

    mutable juce::CriticalSection stateLock;
    solo::ProjectController project;
    juce::String lastStatus;

    solo::SoloEngine audio;
    std::atomic<bool> midiOutEnabled { true };
    std::atomic<bool> hostValid { false }, hostPlaying { false };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int> hostTsNum { 4 }, hostTsDen { 4 };
    std::atomic<double> tailSeconds { 0.5 };
    double currentSampleRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SoloAIStudioProcessor)
};
