// Host de prueba mínimo: carga el VST3 compilado (como lo haría un DAW) y verifica:
//  1. Descripción: nombre, instrumento, salida estéreo, salida MIDI.
//  2. Restauración de estado (setStateInformation) con una secuencia conocida.
//  3. Reproducción sincronizada con un transporte simulado (146 BPM) a varios tamaños de bloque y sample rates:
//     audio no silencioso, sin NaN, sin clipping, eventos MIDI de salida = notas, sin notas colgadas al parar.
//  4. getStateInformation devuelve el mismo estado (persistencia del proyecto).
//  5. (opcional, --editor) crea y destruye el editor.
//
// Uso: SoloHostTest [ruta/al/Solo AI Studio.vst3] [--editor]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include "solo/SoloComposer.h"
#include "solo/StateManager.h"
#include <cstdio>
#include <map>

namespace
{
int failures = 0;
void check (bool ok, const char* what) { std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", what); if (! ok) ++failures; }
void check (bool ok, const juce::String& what)
{
    std::printf ("[%s] %s\n", ok ? " OK " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

struct FakePlayHead : juce::AudioPlayHead
{
    double bpm = 146.0, ppq = 0.0;
    bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setBpm (bpm);
        p.setPpqPosition (ppq);
        p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

// Estado con una secuencia conocida (formato StateManager v1): 4 notas en 2 compases.
const char* kState =
    "SOLOAISTUDIO-STATE 1\n"
    "key=Am\nprogression=Am F C G\nts=4\nbars=2\ninstrument=2\nstyle=1\nenergy=0.6\ndensity=0.5\ncomplexity=0.4\n"
    "seed=77\nmanualBpm=120\nuseHostTempo=1\nsyncToHost=1\nstartBar=1\nloop=0\noutputGain=1\nproMode=0\n"
    "wavSampleRate=48000\nmidiFormat=1\n"
    "variations=1 0\n"
    "var 77 2 4 146 4\n"
    "n 0 900 69 100 0\nn 960 900 72 90 0\nn 1920 1800 76 110 0\nn 3840 3000 74 80 0\n"
    "end\n";
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    juce::String path = SOLO_VST3_PATH;
    bool testEditor = false;
    juce::File snapshot;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--editor") testEditor = true;
        else if (a == "--snapshot" && i + 1 < argc) { testEditor = true; snapshot = juce::File (juce::String (argv[++i])); }
        else path = a;
    }
    const juce::File bundle (path);
    check (bundle.exists(), "Existe el bundle VST3: " + bundle.getFullPathName());

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> descs;
    format.findAllTypesForFile (descs, bundle.getFullPathName());
    check (descs.size() == 1, "El escaneo VST3 encuentra 1 plugin (encontrados: " + juce::String (descs.size()) + ")");
    if (descs.isEmpty()) return 1;
    const auto& d = *descs[0];
    std::printf ("     nombre=%s fabricante=%s categoria=%s uid=%s\n", d.name.toRawUTF8(), d.manufacturerName.toRawUTF8(),
                 d.category.toRawUTF8(), d.createIdentifierString().toRawUTF8());
    check (d.name == "Solo AI Studio", "Nombre del plugin");
    check (d.isInstrument, "Se declara como instrumento");
    std::printf ("     canales de salida declarados en el escaneo: %d\n", d.numOutputChannels);

    std::map<double, bool> rates { { 44100.0, true }, { 48000.0, true }, { 96000.0, true } };
    for (const auto& [sr, unused] : rates)
        for (int block : { 64, 256, 512, 1024 })
        {
            juce::String err;
            auto inst = format.createInstanceFromDescription (d, sr, block, err);
            if (! inst) { check (false, "Instanciar: " + err); return 1; }
            inst->enableAllBuses();
            inst->setPlayConfigDetails (0, 2, sr, block);
            inst->prepareToPlay (sr, block);
            // Un DAW guarda el estado VST3 tal cual lo devuelve el plugin (IComponent::getState).
            // El host de JUCE lo envuelve en XML; aquí construimos ese envoltorio con nuestro estado.
            {
                juce::XmlElement xml ("VST3PluginState");
                juce::MemoryBlock raw (kState, std::strlen (kState));
                xml.createNewChildElement ("IComponent")->addTextElement (raw.toBase64Encoding());
                juce::MemoryBlock wrapped;
                juce::AudioProcessor::copyXmlToBinary (xml, wrapped);
                inst->setStateInformation (wrapped.getData(), (int) wrapped.getSize());
            }

            FakePlayHead ph;
            inst->setPlayHead (&ph);
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            int ons = 0, offs = 0, active = 0, maxActive = 0;
            float peak = 0.0f;
            bool finite = true;
            int firstOnSample = -1;
            const double seconds = 2 * 4 * 60.0 / ph.bpm + 0.5;
            const int blocks = (int) (seconds * sr / block);
            for (int b = 0; b < blocks; ++b)
            {
                ph.ppq = (double) b * block * ph.bpm / 60.0 / sr;
                buf.clear();
                midi.clear();
                inst->processBlock (buf, midi);
                for (const auto meta : midi)
                {
                    const auto m = meta.getMessage();
                    if (m.isNoteOn()) { ++ons; ++active; if (firstOnSample < 0) firstOnSample = b * block + meta.samplePosition; }
                    else if (m.isNoteOff()) { ++offs; --active; }
                    maxActive = juce::jmax (maxActive, active);
                }
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < block; ++i)
                    {
                        const float x = buf.getSample (c, i);
                        finite &= std::isfinite (x);
                        peak = juce::jmax (peak, std::abs (x));
                    }
            }
            const juce::String tag = juce::String ("sr=") + juce::String ((int) sr) + " bloque=" + juce::String (block) + ": ";
            check (ons == 4 && offs == 4, tag + "MIDI out 4 note-on / 4 note-off (" + juce::String (ons) + "/" + juce::String (offs) + ")");
            check (maxActive <= 1 && active == 0, tag + "sin notas colgadas");
            check (firstOnSample == 0, tag + "primera nota en la muestra 0");
            check (finite && peak > 0.02f && peak <= 1.0f, tag + "audio valido (pico " + juce::String (peak, 3) + ")");

            // Salto a mitad del solo (inicio de la nota larga) y parada => la nota debe cerrarse.
            int stopOns = 0, stopOffs = 0;
            auto countMidi = [&] { for (const auto meta : midi) { stopOns += meta.getMessage().isNoteOn(); stopOffs += meta.getMessage().isNoteOff(); } };
            ph.ppq = 4.0;
            ph.playing = true;
            buf.clear(); midi.clear(); inst->processBlock (buf, midi); countMidi();
            ph.playing = false;
            buf.clear(); midi.clear(); inst->processBlock (buf, midi); countMidi();
            check (stopOns == 1 && stopOffs == 1, tag + "parar el transporte cierra la nota sonando");

            juce::MemoryBlock mb;
            inst->getStateInformation (mb);
            juce::String restored;
            if (auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
                if (auto* comp = xml->getChildByName ("IComponent"))
                {
                    juce::MemoryBlock raw;
                    raw.fromBase64Encoding (comp->getAllSubText());
                    restored = raw.toString();
                }
            check (restored.contains ("var 77 2 4 146 4") && restored.contains ("n 1920 1800 76 110 0"),
                   tag + "getStateInformation conserva la secuencia");

            if (testEditor && sr == 48000.0 && block == 512)
            {
                std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorAndMakeActive());
                check (ed != nullptr && ed->getWidth() >= 1100, "Editor creado (" + juce::String (ed ? ed->getWidth() : 0) + " px)");
                check (inst->getTotalNumOutputChannels() == 2, "Instancia con salida estereo");
                if (ed != nullptr && snapshot != juce::File())
                {
                    // Capturas con un proyecto real generado por el núcleo (Am F C G, 146 BPM, 8 compases):
                    // modo fácil y cada pestaña Pro. Nombre: <ruta>-facil.png, <ruta>-pro0.png ...
                    solo::SoloState st;
                    solo::ComposerParams cp;
                    std::string err;
                    st.settings.style = solo::Style::ExpressiveBlues;
                    st.settings.toComposerParams (146.0, cp, err);
                    for (uint64_t seed : { 1ull, 2ull, 3ull }) { cp.seed = seed; st.history.push (solo::SoloComposer::compose (cp), seed); }
                    st.history.setFavorite (1, true);
                    st.history.rename (1, "Mi favorita");
                    st.slotA = st.history.all()[0].id;
                    st.slotB = st.history.all()[2].id;
                    st.lockedBars = { true, false, false, false, false, false, false, false };
                    st.settings.fxEnabled = true;
                    st.settings.fxPreset = "Blues Lead";
                    st.fx = solo::fxPresets()[(size_t) solo::fxPresetIndex ("Blues Lead")].settings;
                    auto& lane = st.automation.lane (solo::AutoTarget::DelayMix);
                    lane.enabled = true;
                    lane.points = { { 0, 0.2f }, { 4 * 4 * solo::kPPQ, 0.8f }, { 8 * 4 * solo::kPPQ, 0.4f } };
                    st.dnaSummary = "ejemplo: 3 motivos, saltos medios, dirección ascendente";
                    auto capture = [&] (bool pro, int tab, const juce::String& suffix) {
                        st.settings.proMode = pro;
                        st.settings.proTab = tab;
                        const auto text = solo::serializeState (st);
                        juce::XmlElement xml ("VST3PluginState");
                        juce::MemoryBlock raw (text.data(), text.size());
                        xml.createNewChildElement ("IComponent")->addTextElement (raw.toBase64Encoding());
                        juce::MemoryBlock wrapped;
                        juce::AudioProcessor::copyXmlToBinary (xml, wrapped);
                        inst->setStateInformation (wrapped.getData(), (int) wrapped.getSize());
                        for (int k = 0; k < 20; ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
                        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
                        const auto f = snapshot.getSiblingFile (snapshot.getFileNameWithoutExtension() + suffix + ".png");
                        f.deleteFile();
                        juce::FileOutputStream os (f);
                        juce::PNGImageFormat png;
                        check (png.writeImageToStream (img, os), "Captura del editor guardada: " + f.getFileName());
                    };
                    capture (false, 0, "-facil");
                    for (int t = 0; t < 7; ++t) capture (true, t, "-pro" + juce::String (t));
                }
                ed.reset();
            }
            inst->releaseResources();
        }

    std::printf ("\n%s (%d fallos)\n", failures == 0 ? "HOST TEST OK" : "HOST TEST CON FALLOS", failures);
    return failures == 0 ? 0 : 1;
}
