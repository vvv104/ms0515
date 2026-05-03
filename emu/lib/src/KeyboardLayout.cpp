/*
 * KeyboardLayout.cpp — Parse the MS7004 on-screen keyboard layout file
 * and bind each cap to its physical-key identifier.
 */

#include "ms0515/KeyboardLayout.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace ms0515 {

namespace {

/* ── Label → ms0515::Key binding table ────────────────────────────────────
 *
 * Every drawn cap label that should be interactive.  Labels must
 * match exactly what `parseLine` produces after newline expansion
 * and quote stripping. */

struct LabelKey {
    const char *label;
    Key         key;
    bool        sticky;   /* modifier cap: ВР / СУ / КМП */
    bool        toggle;   /* toggle cap: ФКС / РУС-ЛАТ   */
};

const LabelKey kLabelKeys[] = {
    /* ── Function strip ─────────────────────────────────────────── */
    {"\xd0\xa4" "1",    Key::F1,  false, false},  /* Ф1 */
    {"\xd0\xa4" "2",    Key::F2,  false, false},  /* Ф2 */
    {"\xd0\xa4" "3",    Key::F3,  false, false},  /* Ф3 */
    {"\xd0\xa4" "4",    Key::F4,  false, false},  /* Ф4 */
    {"\xd0\xa4" "5",    Key::F5,  false, false},  /* Ф5 */
    {"\xd0\xa4" "6",    Key::F6,  false, false},  /* Ф6 */
    {"\xd0\xa4" "7",    Key::F7,  false, false},  /* Ф7 */
    {"\xd0\xa4" "8",    Key::F8,  false, false},  /* Ф8 */
    {"\xd0\xa4" "9",    Key::F9,  false, false},  /* Ф9 */
    {"\xd0\xa4" "10",   Key::F10, false, false},  /* Ф10 */
    {"\xd0\xa4" "11",   Key::F11, false, false},  /* Ф11 */
    {"\xd0\xa4" "12",   Key::F12, false, false},  /* Ф12 */
    {"\xd0\xa4" "13",   Key::F13, false, false},  /* Ф13 */
    {"\xd0\xa4" "14",   Key::F14, false, false},  /* Ф14 */
    {"\xd0\x9f\xd0\x9c", Key::Help, false, false}, /* ПМ */
    {"\xd0\x98\xd0\xa1\xd0\x9f", Key::Perform, false, false}, /* ИСП */
    {"\xd0\xa4" "17",   Key::F17, false, false},  /* Ф17 */
    {"\xd0\xa4" "18",   Key::F18, false, false},  /* Ф18 */
    {"\xd0\xa4" "19",   Key::F19, false, false},  /* Ф19 */
    {"\xd0\xa4" "20",   Key::F20, false, false},  /* Ф20 */

    /* ── Digit row ──────────────────────────────────────────────── */
    {"{\n|",            Key::LBracePipe,   false, false},
    {";\n+",            Key::SemiPlus,     false, false},
    {"1\n!",            Key::Digit1,       false, false},
    {"2\n\"",           Key::Digit2,       false, false},
    {"3\n#",            Key::Digit3,       false, false},
    {"4\n\xc2\xa4",     Key::Digit4,       false, false},  /* 4\n¤ */
    {"5\n%",            Key::Digit5,       false, false},
    {"6\n&",            Key::Digit6,       false, false},
    {"7\n'",            Key::Digit7,       false, false},
    {"8\n(",            Key::Digit8,       false, false},
    {"9\n)",            Key::Digit9,       false, false},
    {"0",               Key::Digit0,       false, false},
    {"-\n=",            Key::MinusEq,      false, false},
    {"}\n\xe2\x86\x96", Key::RBraceLeftUp, false, false},  /* }\n↖ */

    /* ── Whitespace / navigation ────────────────────────────────── */
    {"\xd0\x97\xd0\x91",     Key::Backspace, false, false},  /* ЗБ */
    {"\xd0\xa2\xd0\x90\xd0\x91", Key::Tab,   false, false},  /* ТАБ */
    {"\xd0\x92\xd0\x9a",     Key::Return,    false, false},  /* ВК */

    /* ── Editing cluster ────────────────────────────────────────── */
    {"\xd0\x9d\xd0\xa2",                   Key::Find,   false, false}, /* НТ */
    {"\xd0\x92\xd0\xa1\xd0\xa2",           Key::Insert, false, false}, /* ВСТ */
    {"\xd0\xa3\xd0\x94\xd0\x90\xd0\x9b",   Key::Remove, false, false}, /* УДАЛ */
    {"\xd0\x92\xd0\xab\xd0\x91\xd0\xa0",   Key::Select, false, false}, /* ВЫБР */
    {"\xd0\x9f\xd0\xa0\xd0\x95\xd0\x94\n\xd0\x9a\xd0\x90\xd0\x94\xd0\xa0",
        Key::Prev, false, false}, /* ПРЕД\nКАДР */
    {"\xd0\xa1\xd0\x9b\xd0\x95\xd0\x94\n\xd0\x9a\xd0\x90\xd0\x94\xd0\xa0",
        Key::Next, false, false}, /* СЛЕД\nКАДР */

    /* ── Arrows ─────────────────────────────────────────────────── */
    {"\xe2\x86\x91", Key::Up,    false, false},  /* ↑ */
    {"\xe2\x86\x93", Key::Down,  false, false},  /* ↓ */
    {"\xe2\x86\x90", Key::Left,  false, false},  /* ← */
    {"\xe2\x86\x92", Key::Right, false, false},  /* → */

    /* ── PF keys ────────────────────────────────────────────────── */
    {"\xd0\x9f\xd0\xa4" "1", Key::Pf1, false, false}, /* ПФ1 */
    {"\xd0\x9f\xd0\xa4" "2", Key::Pf2, false, false}, /* ПФ2 */
    {"\xd0\x9f\xd0\xa4" "3", Key::Pf3, false, false}, /* ПФ3 */
    {"\xd0\x9f\xd0\xa4" "4", Key::Pf4, false, false}, /* ПФ4 */

    /* ── Top letter row: Й Ц У К Е Н Г Ш Щ З Х ─────────────────── */
    {"\xd0\x99\nJ",  Key::J,        false, false},  /* Й\nJ */
    {"\xd0\xa6\nC",  Key::C,        false, false},  /* Ц\nC */
    {"\xd0\xa3\nU",  Key::U,        false, false},  /* У\nU */
    {"\xd0\x9a\nK",  Key::K,        false, false},  /* К\nK */
    {"\xd0\x95\nE",  Key::E,        false, false},  /* Е\nE */
    {"\xd0\x9d\nN",  Key::N,        false, false},  /* Н\nN */
    {"\xd0\x93\nG",  Key::G,        false, false},  /* Г\nG */
    {"\xd0\xa8\n[",  Key::LBracket, false, false},  /* Ш\n[ */
    {"\xd0\xa9\n]",  Key::RBracket, false, false},  /* Щ\n] */
    {"\xd0\x97\nZ",  Key::Z,        false, false},  /* З\nZ */
    {"\xd0\xa5\nH",  Key::H,        false, false},  /* Х\nH */
    {":\n*",         Key::ColonStar,false, false},
    {"~",            Key::Tilde,    false, false},

    /* ── Home row: Ф Ы В А П Р О Л Д Ж Э ───────────────────────── */
    {"\xd0\xa4\nF",  Key::F,         false, false},  /* Ф\nF */
    {"\xd0\xab\nY",  Key::Y,         false, false},  /* Ы\nY */
    {"\xd0\x92\nW",  Key::W,         false, false},  /* В\nW */
    {"\xd0\x90\nA",  Key::A,         false, false},  /* А\nA */
    {"\xd0\x9f\nP",  Key::P,         false, false},  /* П\nP */
    {"\xd0\xa0\nR",  Key::R,         false, false},  /* Р\nR */
    {"\xd0\x9e\nO",  Key::O,         false, false},  /* О\nO */
    {"\xd0\x9b\nL",  Key::L,         false, false},  /* Л\nL */
    {"\xd0\x94\nD",  Key::D,         false, false},  /* Д\nD */
    {"\xd0\x96\nV",  Key::V,         false, false},  /* Ж\nV */
    {"\xd0\xad\n\\", Key::Backslash, false, false},  /* Э\n\ */
    {".\n>",         Key::Period,    false, false},
    {"\xd0\xaa",     Key::HardSign,  false, false},  /* Ъ */

    /* ── Bottom letter row: Я Ч С М И Т Ь Б Ю ──────────────────── */
    {"\xd0\xaf\nQ",        Key::Q,         false, false},  /* Я\nQ */
    {"\xd0\xa7\n\xc2\xac", Key::Che,       false, false},  /* Ч\n¬ */
    {"\xd0\xa1\nS",        Key::S,         false, false},  /* С\nS */
    {"\xd0\x9c\nM",        Key::M,         false, false},  /* М\nM */
    {"\xd0\x98\nI",        Key::I,         false, false},  /* И\nI */
    {"\xd0\xa2\nT",        Key::T,         false, false},  /* Т\nT */
    {"\xd0\xac\nX",        Key::X,         false, false},  /* Ь\nX */
    {"\xd0\x91\nB",        Key::B,         false, false},  /* Б\nB */
    {"\xd0\xae\n@",        Key::At,        false, false},  /* Ю\n@ */
    {",\n<",               Key::Comma,     false, false},
    {"/\n?",               Key::Slash,     false, false},
    {"_",                  Key::Underscore,false, false},

    /* ── Modifiers ──────────────────────────────────────────────── */
    {"\xd0\x92\xd0\xa0",         Key::ShiftL,  true, false},  /* ВР (first = left) */
    {"\xd0\xa1\xd0\xa3",         Key::Ctrl,    true, false},  /* СУ */
    {"\xd0\x9a\xd0\x9c\xd0\x9f", Key::Compose, true, false},  /* КМП */

    /* ── Toggles ────────────────────────────────────────────────── */
    {"\xd0\xa4\xd0\x9a\xd0\xa1", Key::Caps,   false, true},  /* ФКС */
    {"\xd0\xa0\xd0\xa3\xd0\xa1\n\xd0\x9b\xd0\x90\xd0\xa2",
        Key::RusLat, false, true}, /* РУС\nЛАТ */

    /* ── Numpad enter ───────────────────────────────────────────── */
    {"\xd0\x92\xd0\x92\xd0\x9e\xd0\x94", Key::KpEnter, false, false}, /* ВВОД */
};

/* Numpad digit/symbol labels are single characters that collide with
 * digit-row labels.  We handle them separately in `bindCap` by
 * checking whether the cap's width puts it in the wider numpad
 * range. */
struct NumpadLabel {
    const char *label;
    Key         key;
};

const NumpadLabel kNumpad[] = {
    {"7", Key::Kp7}, {"8", Key::Kp8}, {"9", Key::Kp9},
    {"4", Key::Kp4}, {"5", Key::Kp5}, {"6", Key::Kp6},
    {"1", Key::Kp1}, {"2", Key::Kp2}, {"3", Key::Kp3},
    {"0", Key::Kp0Wide},
    {",", Key::KpComma},
    {".", Key::KpDot},
    {"-", Key::KpMinus},
};

const LabelKey *findByLabel(const std::string &lbl)
{
    for (const auto &e : kLabelKeys)
        if (lbl == e.label) return &e;
    return nullptr;
}

const NumpadLabel *findNumpad(const std::string &lbl)
{
    for (const auto &e : kNumpad)
        if (lbl == e.label) return &e;
    return nullptr;
}

/* Trim ASCII whitespace at both ends. */
void rtrim(std::string &s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' ||
                          s.back() == '\t' || s.back() == '\n'))
        s.pop_back();
}

bool parseLine(const std::string &raw, KeyboardLayout::Cap &out)
{
    std::string s = raw;
    rtrim(s);
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    if (i >= s.size() || s[i] == '#' || s[i] == '[') return false;

    size_t wStart = i;
    while (i < s.size() && s[i] != ' ' && s[i] != '\t') ++i;
    std::string wStr = s.substr(wStart, i - wStart);
    float w = 1.0f;
    try { w = std::stof(wStr); } catch (...) { return false; }

    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    std::string lbl = s.substr(i);

    /* Strip " # ..." inline comments (outside quotes). */
    if (!lbl.empty() && lbl.front() != '"') {
        auto hashPos = lbl.find(" #");
        if (hashPos != std::string::npos) lbl = lbl.substr(0, hashPos);
        while (!lbl.empty() && (lbl.back() == ' ' || lbl.back() == '\t'))
            lbl.pop_back();
    }

    out = {};
    out.widthUnits = w;

    if (lbl == "_") { out.drawn = false; return true; }

    if (lbl.size() >= 2 && lbl.front() == '"' && lbl.back() == '"')
        lbl = lbl.substr(1, lbl.size() - 2);

    bool forceDim = false;
    if (!lbl.empty() && lbl.front() == '=') { forceDim = true; lbl.erase(0, 1); }

    /* Expand literal "\n" → real newline. */
    for (size_t p = 0; (p = lbl.find("\\n", p)) != std::string::npos; ) {
        lbl.replace(p, 2, "\n");
        p += 1;
    }

    out.label = lbl;
    out.drawn = true;
    out.dim   = forceDim;
    return true;
}

void bindCap(KeyboardLayout::Cap &k, int &shiftCountInRow)
{
    if (!k.drawn || k.dim) return;

    /* Wide blank cap = spacebar. */
    if (k.label.empty() && k.widthUnits >= 5.0f) {
        k.key = Key::Space;
        return;
    }

    /* Try numpad first for wide caps (numpad 0 is 2 units wide and
     * its "0" label collides with the digit-row "0" in kLabelKeys). */
    if (k.widthUnits >= 1.5f) {
        if (const NumpadLabel *m = findNumpad(k.label)) {
            k.key = m->key;
            return;
        }
    }

    /* Try the main label table. */
    if (const LabelKey *m = findByLabel(k.label)) {
        k.key    = m->key;
        k.sticky = m->sticky;
        k.toggle = m->toggle;
        /* Second ВР cap on the same row = right Shift. */
        if (k.key == Key::ShiftL && shiftCountInRow++ > 0)
            k.key = Key::ShiftR;
        return;
    }

    /* Try numpad (short labels like "1", ".", "-", ","). */
    if (const NumpadLabel *m = findNumpad(k.label)) {
        k.key = m->key;
        return;
    }

    k.dim = true;  /* unknown label → inert */
}

} /* anonymous namespace */

bool KeyboardLayout::loadFromFile(std::string_view path)
{
    std::ifstream f{std::string{path}};
    if (!f) {
        rows_.clear();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    return loadFromString(ss.str());
}

bool KeyboardLayout::loadFromString(std::string_view content)
{
    rows_.clear();

    std::vector<Cap> cur;
    bool inSection = false;
    bool inFnRow   = false;
    int  shiftCountInRow = 0;
    std::string curSection;

    auto flush = [&]() {
        if (inSection && !cur.empty()) {
            if (!inFnRow) {
                /* Detect the "right-cluster" gap pattern: the row has at
                 * least two `drawn=false` separators after the typewriter
                 * block; caps between those two gaps belong to the
                 * gray-chassis arrow / edit / numpad cluster. */
                std::vector<size_t> gaps;
                bool seen = false;
                for (size_t n = 0; n < cur.size(); ++n) {
                    if (cur[n].drawn) seen = true;
                    else if (seen)    gaps.push_back(n);
                }
                if (gaps.size() >= 2)
                    for (size_t n = gaps[0] + 1; n < gaps[1]; ++n)
                        if (cur[n].drawn) cur[n].gray = true;
            }
            rows_.push_back(std::move(cur));
        }
        cur.clear();
        shiftCountInRow = 0;
    };

    /* Iterate lines of `content`. */
    size_t pos = 0;
    while (pos <= content.size()) {
        size_t nl = content.find('\n', pos);
        std::string line = (nl == std::string::npos)
            ? std::string{content.substr(pos)}
            : std::string{content.substr(pos, nl - pos)};

        std::string t = line;
        rtrim(t);
        size_t j = 0;
        while (j < t.size() && (t[j] == ' ' || t[j] == '\t')) ++j;
        if (j < t.size() && t[j] == '[') {
            flush();
            inSection = true;
            size_t e = t.find(']', j);
            curSection = (e != std::string::npos)
                ? t.substr(j + 1, e - j - 1) : std::string();
            inFnRow = (curSection == "fn");
        } else {
            Cap k;
            if (parseLine(line, k)) {
                if (inFnRow) k.gray = k.drawn;
                bindCap(k, shiftCountInRow);
                cur.push_back(std::move(k));
            }
        }

        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    flush();

    return !rows_.empty();
}

/* ── Mode-dependent keyboard semantics ──────────────────────────────────── */

bool isLetterKey(Key k, bool rusMode) noexcept
{
    switch (k) {
    /* Pure letters: always letters in both modes. */
    case Key::A: case Key::B: case Key::C:
    case Key::D: case Key::E: case Key::F:
    case Key::G: case Key::H: case Key::I:
    case Key::J: case Key::K: case Key::L:
    case Key::M: case Key::N: case Key::O:
    case Key::P: case Key::Q: case Key::R:
    case Key::S: case Key::T: case Key::U:
    case Key::V: case Key::W: case Key::X:
    case Key::Y: case Key::Z:
        return true;
    /* Symbol-on-letter: Cyrillic letter in РУС, symbol in ЛАТ. */
    case Key::LBracket:   /* Ш/[ */
    case Key::RBracket:   /* Щ/] */
    case Key::Backslash:  /* Э/\ */
    case Key::Che:        /* Ч/¬ */
    case Key::At:         /* Ю/@ */
    case Key::HardSign:   /* Ъ   */
        return rusMode;
    default:
        return false;
    }
}

bool isShiftImmuneSymbol(Key k, bool rusMode) noexcept
{
    if (rusMode) return false;
    return k == Key::LBracket    /* Ш/[  — Shift would give { */
        || k == Key::RBracket    /* Щ/]  — Shift would give } */
        || k == Key::Backslash   /* Э/\  — Shift would give | */
        || k == Key::Che;        /* Ч/¬  — Shift would give ~ */
}

} /* namespace ms0515 */
