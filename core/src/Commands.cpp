#include "solo/Commands.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace solo
{
namespace
{
bool has (const std::string& s, const char* w) { return s.find (w) != std::string::npos; }

bool hasWord (const std::string& s, const std::string& w)
{
    size_t p = 0;
    while ((p = s.find (w, p)) != std::string::npos)
    {
        const bool l = p == 0 || ! std::isalnum ((unsigned char) s[p - 1]);
        const bool r = p + w.size() >= s.size() || ! std::isalnum ((unsigned char) s[p + w.size()]);
        if (l && r) return true;
        ++p;
    }
    return false;
}

std::vector<int> numbersIn (const std::string& s)
{
    std::vector<int> v;
    std::string cur;
    for (char c : s + " ")
    {
        if (std::isdigit ((unsigned char) c)) cur += c;
        else if (! cur.empty()) { v.push_back (std::atoi (cur.c_str())); cur.clear(); }
    }
    return v;
}

// "compases 3 a 5", "compas 2", "del 1 al 4", "ultimo compas", "primeros 2 compases"
bool barRange (const std::string& s, int& from, int& to)
{
    const bool mentionsBar = has (s, "compas");
    if (! mentionsBar) return false;
    // Solo cuentan los números que van después de "compas" ("sube 1 octava el compas 2" -> compás 2).
    auto n = numbersIn (s.substr (s.find ("compas")));
    if (n.empty() && has (s, "primeros")) n = numbersIn (s);
    if (has (s, "ultimo") && n.empty()) { from = to = -2; return true; } // -2 = último (se resuelve al ejecutar)
    if (has (s, "primer") && ! n.empty() && has (s, "primeros")) { from = 0; to = n[0] - 1; return true; }
    if (n.size() >= 2) { from = n[0] - 1; to = n[1] - 1; if (to < from) std::swap (from, to); return true; }
    if (n.size() == 1) { from = to = n[0] - 1; return true; }
    if (has (s, "primer")) { from = to = 0; return true; }
    return false;
}

std::vector<std::string> splitClauses (const std::string& s)
{
    // separa por " y " / "," / " luego " / " despues " / " tambien " / ";" sin romper "del 3 y 4"
    std::vector<std::string> out;
    std::string cur;
    std::istringstream in (s);
    std::string w;
    std::vector<std::string> words;
    while (in >> w) words.push_back (w);
    auto flush = [&] { if (! cur.empty()) out.push_back (cur); cur.clear(); };
    for (size_t i = 0; i < words.size(); ++i)
    {
        std::string x = words[i];
        bool sepAfter = false;
        if (! x.empty() && (x.back() == ',' || x.back() == ';' || x.back() == '.')) { x.pop_back(); sepAfter = true; }
        const bool isSep = x == "luego" || x == "despues" || x == "tambien" || x == "ademas";
        const bool isY = x == "y" || x == "e";
        const bool nextIsNum = i + 1 < words.size() && std::isdigit ((unsigned char) words[i + 1][0]);
        if (isSep || (isY && ! nextIsNum)) { flush(); continue; }
        if (! x.empty()) cur += (cur.empty() ? "" : " ") + x;
        if (sepAfter) flush();
    }
    flush();
    return out;
}

bool styleFrom (const std::string& s, Style& st)
{
    if (has (s, "blues")) { st = Style::ExpressiveBlues; return true; }
    if (has (s, "virtuos") || has (s, "shred")) { st = Style::VirtuosoRock; return true; }
    if (has (s, "ambient")) { st = Style::Ambient; return true; }
    if (has (s, "cine") || has (s, "epic")) { st = Style::Cinematic; return true; }
    if (has (s, "emocion") || has (s, "balada")) { st = Style::Emotional; return true; }
    if (has (s, "improvis") || hasWord (s, "jazz")) { st = Style::SoftImprov; return true; }
    if (has (s, "respuesta") || has (s, "vocal") || has (s, "voz")) { st = Style::VocalResponse; return true; }
    if (has (s, "rock")) { st = Style::MelodicRock; return true; }
    return false;
}

bool fxPresetFrom (const std::string& s, std::string& name)
{
    if (has (s, "limpi") || has (s, "clean")) { name = "Clean Guitar"; return true; }
    if (has (s, "alta ganancia") || has (s, "high gain") || has (s, "metal")) { name = "High Gain Solo"; return true; }
    if (has (s, "rock clasico") || has (s, "classic rock")) { name = "Classic Rock Lead"; return true; }
    if (has (s, "sinte") || has (s, "synth")) { name = "Wide Synth Lead"; return true; }
    if (has (s, "ambient")) { name = "Ambient Guitar"; return true; }
    if (has (s, "cine")) { name = "Cinematic Lead"; return true; }
    if (has (s, "blues")) { name = "Blues Lead"; return true; }
    if (has (s, "emocion")) { name = "Emotional Lead"; return true; }
    return false;
}

bool articulationFrom (const std::string& s, Articulation& a)
{
    if (has (s, "vibrato")) { a = Articulation::Vibrato; return true; }
    if (has (s, "bend") || has (s, "estirad")) { a = Articulation::Bend; return true; }
    if (has (s, "slide") || has (s, "desliz") || has (s, "glissando")) { a = Articulation::Slide; return true; }
    if (has (s, "legato") || has (s, "ligad") || has (s, "hammer")) { a = Articulation::Legato; return true; }
    if (has (s, "palm") || has (s, "apagad") || has (s, "mute")) { a = Articulation::PalmMute; return true; }
    return false;
}

bool parseClause (const std::string& c, CommandOp& op)
{
    const bool more = hasWord (c, "mas") || has (c, "sube") || has (c, "aumenta");
    const bool less = hasWord (c, "menos") || has (c, "baja") || has (c, "reduce") || has (c, "quita");
    int from = -1, to = -1;
    const bool range = barRange (c, from, to);

    // Deshacer / rehacer / transporte
    if (has (c, "deshaz") || has (c, "deshacer") || hasWord (c, "atras") || has (c, "vuelve como estaba")) { op.kind = OpKind::Undo; return true; }
    if ((has (c, "rehaz") || has (c, "rehacer")) && ! range) { op.kind = OpKind::Redo; return true; }
    if (has (c, "para") && (c.size() < 12 || has (c, "parar")) ) { op.kind = OpKind::Stop; return true; }
    if (has (c, "deten") || hasWord (c, "stop") || hasWord (c, "silencio")) { op.kind = OpKind::Stop; return true; }
    if (has (c, "reproduc") || has (c, "escucha") || hasWord (c, "toca") || hasWord (c, "play")) { op.kind = OpKind::Play; return true; }
    if (has (c, "favorit")) { op.kind = OpKind::Favorite; return true; }
    if (has (c, "compara") || has (c, "cambia a b") || has (c, "a/b") || hasWord (c, "ab")) { op.kind = OpKind::SwitchAB; return true; }

    // Bloqueos
    if ((has (c, "desbloque") || has (c, "libera")) && range) { op.kind = OpKind::UnlockBars; op.fromBar = from; op.toBar = to; return true; }
    if ((has (c, "bloque") || has (c, "protege") || has (c, "conserva") || has (c, "manten")) && range)
    { op.kind = OpKind::LockBars; op.fromBar = from; op.toBar = to; return true; }

    // Articulaciones
    Articulation art;
    if (articulationFrom (c, art))
    {
        op.articulation = art;
        op.kind = (has (c, "quita") || has (c, "sin ") || has (c, "elimina") || hasWord (c, "menos")) ? OpKind::RemoveArticulation : OpKind::AddArticulation;
        op.fromBar = range ? from : -1;
        op.toBar = range ? to : -1;
        return true;
    }

    // Efectos
    if (has (c, "sin efectos") || has (c, "quita los efectos") || has (c, "efectos fuera") || has (c, "en seco"))
    { op.kind = OpKind::FxOff; return true; }
    struct FxWord { const char* w; AutoTarget t; };
    const FxWord fxw[] = { { "delay", AutoTarget::DelayMix }, { "eco", AutoTarget::DelayMix }, { "reverb", AutoTarget::ReverbMix },
                           { "distorsion", AutoTarget::Drive }, { "saturacion", AutoTarget::Drive }, { "ganancia", AutoTarget::Drive },
                           { "chorus", AutoTarget::ChorusMix }, { "brillo", AutoTarget::FilterCutoff }, { "brillante", AutoTarget::FilterCutoff },
                           { "oscuro", AutoTarget::FilterCutoff }, { "volumen", AutoTarget::Volume } };
    for (const auto& f : fxw)
        if (has (c, f.w) && (more || less || has (c, "oscuro") || has (c, "brillante")))
        {
            op.kind = OpKind::FxAdjust;
            op.fxTarget = f.t;
            double d = less ? -0.2 : 0.2;
            if (has (c, "oscuro")) d = -0.2;
            if (has (c, "mucho")) d *= 2;
            if (has (c, "poco") || has (c, "un poco")) d *= 0.5;
            op.amount = d;
            return true;
        }
    std::string preset;
    if ((has (c, "sonido") || has (c, "preset") || has (c, "efecto") || has (c, "tono")) && fxPresetFrom (c, preset))
    { op.kind = OpKind::FxPreset; op.what = preset; return true; }

    // Transporte de altura
    if ((has (c, "octava") || has (c, "semitono") || hasWord (c, "tono")) && (more || less || has (c, "transporta")))
    {
        op.kind = OpKind::Transpose;
        const auto n = numbersIn (c);
        int semis = has (c, "octava") ? 12 : hasWord (c, "tono") ? 2 : 1;
        if (has (c, "semitono") && ! n.empty() && ! range) semis = n[0];
        else if (has (c, "semitono") && n.size() >= 3) semis = n.back();
        op.amount = less ? -semis : semis;
        op.fromBar = range ? from : -1;
        op.toBar = range ? to : -1;
        return true;
    }

    // Regenerar / generar
    if ((has (c, "regenera") || has (c, "cambia") || has (c, "rehaz") || has (c, "otra cosa") || has (c, "nuevo")) && range)
    { op.kind = OpKind::RegenerateBars; op.fromBar = from; op.toBar = to; return true; }
    if (has (c, "genera") || has (c, "otro solo") || has (c, "otra version") || has (c, "nueva variacion") || has (c, "otra variacion")
        || has (c, "otra idea") || has (c, "crea"))
    { op.kind = OpKind::Generate; return true; }

    // Estilo / instrumento
    if (has (c, "flauta")) { op.kind = OpKind::SetInstrument; op.instrument = InstrumentId::Flute; return true; }
    if (has (c, "sinte") || has (c, "synth")) { op.kind = OpKind::SetInstrument; op.instrument = InstrumentId::Synth; return true; }
    if (has (c, "guitarra") && ! has (c, "estilo")) { op.kind = OpKind::SetInstrument; op.instrument = has (c, "ligera") ? InstrumentId::GuitarLite : InstrumentId::Guitar; return true; }
    Style st;
    if ((has (c, "estilo") || has (c, "como") || has (c, "mas ") || has (c, "en plan") || has (c, "tipo")) && styleFrom (c, st))
    { op.kind = OpKind::SetStyle; op.style = st; return true; }

    // Ajustes generales
    if (more || less)
    {
        double d = more ? 0.15 : -0.15;
        if (has (c, "mucho")) d *= 2;
        if (has (c, "poco")) d *= 0.5;
        if (has (c, "intens") || has (c, "energia") || has (c, "fuerte") || has (c, "agresiv")) { op.kind = OpKind::Adjust; op.what = "energy"; op.amount = d; return true; }
        if (has (c, "suave") || has (c, "tranquil")) { op.kind = OpKind::Adjust; op.what = "energy"; op.amount = -std::abs (d); return true; }
        if (has (c, "notas") || has (c, "rapido") || has (c, "denso")) { op.kind = OpKind::Adjust; op.what = "density"; op.amount = d; return true; }
        if (has (c, "espacio") || has (c, "lento") || has (c, "silencio")) { op.kind = OpKind::Adjust; op.what = "density"; op.amount = -std::abs (d); return true; }
        if (has (c, "complej") || has (c, "dificil") || has (c, "elabor")) { op.kind = OpKind::Adjust; op.what = "complexity"; op.amount = d; return true; }
        if (has (c, "sencill") || has (c, "simple")) { op.kind = OpKind::Adjust; op.what = "complexity"; op.amount = -std::abs (d); return true; }
    }
    return false;
}

std::string barsText (int a, int b)
{
    if (a < 0) return a == -2 ? " en el último compás" : "";
    if (a == b) return " en el compás " + std::to_string (a + 1);
    return " en los compases " + std::to_string (a + 1) + "-" + std::to_string (b + 1);
}
} // namespace

std::string normaliseSpanish (const std::string& in)
{
    // Sustituye vocales acentuadas y ñ (UTF-8 de 2 bytes) y pasa a minúsculas.
    static const std::map<std::string, char> acc = {
        { "\xC3\xA1", 'a' }, { "\xC3\xA9", 'e' }, { "\xC3\xAD", 'i' }, { "\xC3\xB3", 'o' }, { "\xC3\xBA", 'u' }, { "\xC3\xBC", 'u' },
        { "\xC3\x81", 'a' }, { "\xC3\x89", 'e' }, { "\xC3\x8D", 'i' }, { "\xC3\x93", 'o' }, { "\xC3\x9A", 'u' }, { "\xC3\xB1", 'n' }, { "\xC3\x91", 'n' }
    };
    std::string s;
    for (size_t i = 0; i < in.size(); ++i)
    {
        if ((unsigned char) in[i] == 0xC3 && i + 1 < in.size())
        {
            const auto it = acc.find (in.substr (i, 2));
            if (it != acc.end()) { s += it->second; ++i; continue; }
        }
        if ((unsigned char) in[i] == 0xC2 && i + 1 < in.size()) { ++i; continue; } // ¿ ¡
        s += (char) std::tolower ((unsigned char) in[i]);
    }
    // Números escritos con palabras -> cifras
    static const std::pair<const char*, const char*> nums[] = {
        { "dieciseis", "16" }, { "quince", "15" }, { "catorce", "14" }, { "trece", "13" }, { "doce", "12" }, { "once", "11" },
        { "diez", "10" }, { "nueve", "9" }, { "ocho", "8" }, { "siete", "7" }, { "seis", "6" }, { "cinco", "5" },
        { "cuatro", "4" }, { "tres", "3" }, { "dos", "2" }, { "uno", "1" }, { "una", "1" }, { "un", "1" }
    };
    std::istringstream ws (s);
    std::vector<std::string> toks;
    std::string w;
    while (ws >> w) toks.push_back (w);
    std::string out;
    for (size_t i = 0; i < toks.size(); ++i)
    {
        std::string core = toks[i], tail;
        while (! core.empty() && std::string (",.;?!").find (core.back()) != std::string::npos) { tail = core.back() + tail; core.pop_back(); }
        const bool article = core == "un" || core == "una" || core == "uno";
        if (article)
        {
            // "un/una" es número solo delante de una unidad ("un semitono", "una octava", "un compas")
            const std::string next = i + 1 < toks.size() ? toks[i + 1] : "";
            if (next.rfind ("semitono", 0) == 0 || next.rfind ("compas", 0) == 0 || next.rfind ("tono", 0) == 0 || next.rfind ("octava", 0) == 0) core = "1";
        }
        else
            for (const auto& [a2, b2] : nums)
                if (core == a2) { core = b2; break; }
        out += (out.empty() ? "" : " ") + core + tail;
    }
    return out;
}

std::string CommandOp::describe() const
{
    switch (kind)
    {
        case OpKind::Generate: return "Generar una variación nueva";
        case OpKind::RegenerateBars: return "Regenerar" + barsText (fromBar, toBar) + " (respetando bloqueos)";
        case OpKind::LockBars: return "Bloquear" + barsText (fromBar, toBar);
        case OpKind::UnlockBars: return "Desbloquear" + barsText (fromBar, toBar);
        case OpKind::Adjust:
        {
            const char* nm = what == "energy" ? "intensidad" : what == "density" ? "densidad" : "complejidad";
            return std::string (amount > 0 ? "Subir " : "Bajar ") + nm + " (para la próxima generación)";
        }
        case OpKind::SetStyle: return std::string ("Cambiar el estilo a ") + styleName (style);
        case OpKind::SetInstrument: return std::string ("Cambiar el instrumento a ") + instrumentName (instrument);
        case OpKind::FxPreset: return "Sonido: " + what;
        case OpKind::FxAdjust: return std::string (amount > 0 ? "Más " : "Menos ") + autoTargetName (fxTarget);
        case OpKind::FxOff: return "Quitar los efectos";
        case OpKind::Transpose: return "Transportar " + std::to_string ((int) amount) + " semitonos" + barsText (fromBar, toBar);
        case OpKind::AddArticulation: return std::string ("Añadir ") + articulationName (articulation) + barsText (fromBar, toBar);
        case OpKind::RemoveArticulation: return std::string ("Quitar ") + articulationName (articulation) + barsText (fromBar, toBar);
        case OpKind::Undo: return "Deshacer el último cambio";
        case OpKind::Redo: return "Rehacer";
        case OpKind::Play: return "Escuchar";
        case OpKind::Stop: return "Parar";
        case OpKind::Favorite: return "Marcar la variación actual como favorita";
        case OpKind::SwitchAB: return "Alternar entre A y B";
    }
    return "?";
}

std::string ParsedCommand::summary() const
{
    std::string s;
    for (const auto& op : ops) s += (s.empty() ? "" : "; ") + op.describe();
    if (! unrecognised.empty())
    {
        s += (s.empty() ? "" : ". ") + std::string ("No entendido: ");
        for (size_t i = 0; i < unrecognised.size(); ++i) s += (i ? ", " : "") + ("\"" + unrecognised[i] + "\"");
    }
    return s.empty() ? "No se entendió ninguna orden" : s;
}

ParsedCommand parseCommand (const RecognizedText& input)
{
    ParsedCommand pc;
    const auto clauses = splitClauses (normaliseSpanish (input.text));
    int understood = 0;
    for (const auto& c : clauses)
    {
        CommandOp op;
        if (parseClause (c, op)) { pc.ops.push_back (op); ++understood; }
        else pc.unrecognised.push_back (c);
    }
    pc.confidence = clauses.empty() ? 0.0 : (double) understood / (double) clauses.size() * std::clamp (input.confidence, 0.0, 1.0);
    // Solo reproducir/parar/deshacer/rehacer pueden ejecutarse sin confirmación (son inmediatas y reversibles).
    pc.needsConfirmation = false;
    for (const auto& op : pc.ops)
        if (op.kind != OpKind::Play && op.kind != OpKind::Stop && op.kind != OpKind::Undo && op.kind != OpKind::Redo)
            pc.needsConfirmation = true;
    if (! pc.unrecognised.empty() || input.confidence < 0.8) pc.needsConfirmation = true;
    return pc;
}

namespace
{
OpResult runOp (ProjectController& p, const CommandOp& opIn, uint64_t seed, CommandResult& cr)
{
    CommandOp op = opIn;
    const int bars = p.hasSequence() ? p.current()->bars : p.state.settings.bars;
    if (op.fromBar == -2) op.fromBar = op.toBar = bars - 1;
    const int a = op.fromBar < 0 ? 0 : op.fromBar, b = op.toBar < 0 ? bars - 1 : op.toBar;
    switch (op.kind)
    {
        case OpKind::Generate: return p.generate (seed);
        case OpKind::RegenerateBars: return p.regenerateBars (a, b, seed);
        case OpKind::LockBars: return p.setBarsLocked (a, b, true);
        case OpKind::UnlockBars: return p.setBarsLocked (a, b, false);
        case OpKind::Adjust: return p.adjust (op.what, op.amount);
        case OpKind::SetStyle: return p.setStyle (op.style);
        case OpKind::SetInstrument: return p.setInstrument (op.instrument);
        case OpKind::FxPreset: return p.setFxPreset (op.what);
        case OpKind::FxAdjust: return p.adjustFx (op.fxTarget, op.amount);
        case OpKind::FxOff: return p.setFxPreset ("Sin efectos");
        case OpKind::Transpose: return p.transpose (a, b, (int) op.amount);
        case OpKind::AddArticulation: return p.setArticulation (a, b, op.articulation, true);
        case OpKind::RemoveArticulation: return p.setArticulation (a, b, op.articulation, false);
        case OpKind::Undo: return p.undoLast();
        case OpKind::Redo: return p.redoLast();
        case OpKind::Favorite: return p.hasSequence() ? p.setFavorite (p.state.history.currentIndex(), true) : OpResult::fail ("No hay solo");
        case OpKind::SwitchAB: return p.switchAB();
        case OpKind::Play: { cr.requestPlay = true; OpResult r; r.ok = true; r.message = "Escuchar"; return r; }
        case OpKind::Stop: { cr.requestStop = true; OpResult r; r.ok = true; r.message = "Parar"; return r; }
    }
    return OpResult::fail ("Operación desconocida");
}
} // namespace

CommandResult executeCommand (ProjectController& project, const ParsedCommand& cmd, uint64_t seed)
{
    CommandResult cr;
    uint64_t s = seed;
    for (const auto& op : cmd.ops)
    {
        auto r = runOp (project, op, s++, cr);
        cr.ok = cr.ok && r.ok;
        cr.sequenceChanged |= r.sequenceChanged;
        cr.fxChanged |= r.fxChanged;
        cr.settingsChanged |= r.settingsChanged;
        cr.results.push_back (std::move (r));
    }
    return cr;
}

CommandPreview previewCommand (const ProjectController& project, const ParsedCommand& cmd, uint64_t seed)
{
    CommandPreview pv;
    ProjectController copy = project; // la vista previa nunca toca el proyecto real
    pv.notesBefore = copy.hasSequence() ? (int) copy.current()->notes.size() : 0;
    const int before = copy.state.history.size();
    const auto cr = executeCommand (copy, cmd, seed);
    for (size_t i = 0; i < cr.results.size(); ++i)
        pv.steps.push_back ((cr.results[i].ok ? "✓ " : "✗ ") + cmd.ops[i].describe() + (cr.results[i].message.empty() ? "" : " — " + cr.results[i].message));
    pv.notesAfter = copy.hasSequence() ? (int) copy.current()->notes.size() : 0;
    pv.variationsAdded = copy.state.history.size() - before;
    pv.ok = cr.ok && ! cmd.ops.empty();
    return pv;
}

} // namespace solo
