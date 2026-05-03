/*
 * OnScreenKeyboard.cpp — MS7004 virtual keyboard.
 *
 * Layout is data-driven (ms7004_layout.txt).  Each cap is bound at load
 * time to an ms0515::Key physical key identifier.  Clicks, highlight
 * state, and sticky modifier logic all route through the ms7004
 * microcontroller model — no raw scancode manipulation.
 */

#include "OnScreenKeyboard.hpp"
#include "Config.hpp"          /* Paths::searchRoots */

#include <ms0515/Emulator.hpp>

#include <SDL.h>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>

namespace ms0515_frontend {

namespace {

/* ── Label → ms0515::Key binding table ─────────────────────────────── */

struct LabelKey {
    const char  *label;
    ms0515::Key key;
    bool         sticky;   /* modifier cap: ВР / СУ / КМП */
    bool         toggle;   /* toggle cap: ФКС / РУС-ЛАТ */
};

/* Every drawn cap label that should be interactive.  Labels must match
 * what parseLine produces after newline expansion and quote stripping. */
const LabelKey kLabelKeys[] = {
    /* ── Function strip ─────────────────────────────────────────── */
    {"\xd0\xa4" "1",    MS7004_KEY_F1,  false, false},  /* Ф1 */
    {"\xd0\xa4" "2",    MS7004_KEY_F2,  false, false},  /* Ф2 */
    {"\xd0\xa4" "3",    MS7004_KEY_F3,  false, false},  /* Ф3 */
    {"\xd0\xa4" "4",    MS7004_KEY_F4,  false, false},  /* Ф4 */
    {"\xd0\xa4" "5",    MS7004_KEY_F5,  false, false},  /* Ф5 */
    {"\xd0\xa4" "6",    MS7004_KEY_F6,  false, false},  /* Ф6 */
    {"\xd0\xa4" "7",    MS7004_KEY_F7,  false, false},  /* Ф7 */
    {"\xd0\xa4" "8",    MS7004_KEY_F8,  false, false},  /* Ф8 */
    {"\xd0\xa4" "9",    MS7004_KEY_F9,  false, false},  /* Ф9 */
    {"\xd0\xa4" "10",   MS7004_KEY_F10, false, false},  /* Ф10 */
    {"\xd0\xa4" "11",   MS7004_KEY_F11, false, false},  /* Ф11 */
    {"\xd0\xa4" "12",   MS7004_KEY_F12, false, false},  /* Ф12 */
    {"\xd0\xa4" "13",   MS7004_KEY_F13, false, false},  /* Ф13 */
    {"\xd0\xa4" "14",   MS7004_KEY_F14, false, false},  /* Ф14 */
    {"\xd0\x9f\xd0\x9c", MS7004_KEY_HELP, false, false}, /* ПМ */
    {"\xd0\x98\xd0\xa1\xd0\x9f", MS7004_KEY_PERFORM, false, false}, /* ИСП */
    {"\xd0\xa4" "17",   MS7004_KEY_F17, false, false},  /* Ф17 */
    {"\xd0\xa4" "18",   MS7004_KEY_F18, false, false},  /* Ф18 */
    {"\xd0\xa4" "19",   MS7004_KEY_F19, false, false},  /* Ф19 */
    {"\xd0\xa4" "20",   MS7004_KEY_F20, false, false},  /* Ф20 */

    /* ── Digit row ──────────────────────────────────────────────── */
    {"{\n|",           MS7004_KEY_LBRACE_PIPE,  false, false},
    {";\n+",           MS7004_KEY_SEMI_PLUS,    false, false},
    {"1\n!",           MS7004_KEY_1,            false, false},
    {"2\n\"",          MS7004_KEY_2,            false, false},
    {"3\n#",           MS7004_KEY_3,            false, false},
    {"4\n\xc2\xa4",    MS7004_KEY_4,            false, false},  /* 4\n¤ */
    {"5\n%",           MS7004_KEY_5,            false, false},
    {"6\n&",           MS7004_KEY_6,            false, false},
    {"7\n'",           MS7004_KEY_7,            false, false},
    {"8\n(",           MS7004_KEY_8,            false, false},
    {"9\n)",           MS7004_KEY_9,            false, false},
    {"0",              MS7004_KEY_0,            false, false},
    {"-\n=",           MS7004_KEY_MINUS_EQ,     false, false},
    {"}\n\xe2\x86\x96", MS7004_KEY_RBRACE_LEFTUP, false, false},  /* }\n↖ */

    /* ── Whitespace / navigation ────────────────────────────────── */
    {"\xd0\x97\xd0\x91",     MS7004_KEY_BS,     false, false},  /* ЗБ */
    {"\xd0\xa2\xd0\x90\xd0\x91", MS7004_KEY_TAB, false, false}, /* ТАБ */
    {"\xd0\x92\xd0\x9a",     MS7004_KEY_RETURN, false, false},  /* ВК */

    /* ── Editing cluster ────────────────────────────────────────── */
    {"\xd0\x9d\xd0\xa2",           MS7004_KEY_FIND,   false, false},  /* НТ */
    {"\xd0\x92\xd0\xa1\xd0\xa2",   MS7004_KEY_INSERT, false, false},  /* ВСТ */
    {"\xd0\xa3\xd0\x94\xd0\x90\xd0\x9b", MS7004_KEY_REMOVE, false, false}, /* УДАЛ */
    {"\xd0\x92\xd0\xab\xd0\x91\xd0\xa0", MS7004_KEY_SELECT, false, false}, /* ВЫБР */
    {"\xd0\x9f\xd0\xa0\xd0\x95\xd0\x94\n\xd0\x9a\xd0\x90\xd0\x94\xd0\xa0", MS7004_KEY_PREV, false, false}, /* ПРЕД\nКАДР */
    {"\xd0\xa1\xd0\x9b\xd0\x95\xd0\x94\n\xd0\x9a\xd0\x90\xd0\x94\xd0\xa0", MS7004_KEY_NEXT, false, false}, /* СЛЕД\nКАДР */

    /* ── Arrows ─────────────────────────────────────────────────── */
    {"\xe2\x86\x91",   MS7004_KEY_UP,    false, false},  /* ↑ */
    {"\xe2\x86\x93",   MS7004_KEY_DOWN,  false, false},  /* ↓ */
    {"\xe2\x86\x90",   MS7004_KEY_LEFT,  false, false},  /* ← */
    {"\xe2\x86\x92",   MS7004_KEY_RIGHT, false, false},  /* → */

    /* ── PF keys ────────────────────────────────────────────────── */
    {"\xd0\x9f\xd0\xa4" "1", MS7004_KEY_PF1, false, false}, /* ПФ1 */
    {"\xd0\x9f\xd0\xa4" "2", MS7004_KEY_PF2, false, false}, /* ПФ2 */
    {"\xd0\x9f\xd0\xa4" "3", MS7004_KEY_PF3, false, false}, /* ПФ3 */
    {"\xd0\x9f\xd0\xa4" "4", MS7004_KEY_PF4, false, false}, /* ПФ4 */

    /* ── Top letter row: Й Ц У К Е Н Г Ш Щ З Х ──────────────── */
    {"\xd0\x99\nJ",   MS7004_KEY_J,         false, false},  /* Й\nJ */
    {"\xd0\xa6\nC",   MS7004_KEY_C,         false, false},  /* Ц\nC */
    {"\xd0\xa3\nU",   MS7004_KEY_U,         false, false},  /* У\nU */
    {"\xd0\x9a\nK",   MS7004_KEY_K,         false, false},  /* К\nK */
    {"\xd0\x95\nE",   MS7004_KEY_E,         false, false},  /* Е\nE */
    {"\xd0\x9d\nN",   MS7004_KEY_N,         false, false},  /* Н\nN */
    {"\xd0\x93\nG",   MS7004_KEY_G,         false, false},  /* Г\nG */
    {"\xd0\xa8\n[",   MS7004_KEY_LBRACKET,  false, false},  /* Ш\n[ */
    {"\xd0\xa9\n]",   MS7004_KEY_RBRACKET,  false, false},  /* Щ\n] */
    {"\xd0\x97\nZ",   MS7004_KEY_Z,         false, false},  /* З\nZ */
    {"\xd0\xa5\nH",   MS7004_KEY_H,         false, false},  /* Х\nH */
    {":\n*",           MS7004_KEY_COLON_STAR, false, false},
    {"~",              MS7004_KEY_TILDE,      false, false},

    /* ── Home row: Ф Ы В А П Р О Л Д Ж Э ────────────────────── */
    {"\xd0\xa4\nF",   MS7004_KEY_F,         false, false},  /* Ф\nF */
    {"\xd0\xab\nY",   MS7004_KEY_Y,         false, false},  /* Ы\nY */
    {"\xd0\x92\nW",   MS7004_KEY_W,         false, false},  /* В\nW */
    {"\xd0\x90\nA",   MS7004_KEY_A,         false, false},  /* А\nA */
    {"\xd0\x9f\nP",   MS7004_KEY_P,         false, false},  /* П\nP */
    {"\xd0\xa0\nR",   MS7004_KEY_R,         false, false},  /* Р\nR */
    {"\xd0\x9e\nO",   MS7004_KEY_O,         false, false},  /* О\nO */
    {"\xd0\x9b\nL",   MS7004_KEY_L,         false, false},  /* Л\nL */
    {"\xd0\x94\nD",   MS7004_KEY_D,         false, false},  /* Д\nD */
    {"\xd0\x96\nV",   MS7004_KEY_V,         false, false},  /* Ж\nV */
    {"\xd0\xad\n\\",  MS7004_KEY_BACKSLASH, false, false},  /* Э\n\ */
    {".\n>",           MS7004_KEY_PERIOD,    false, false},
    {"\xd0\xaa",       MS7004_KEY_HARDSIGN,  false, false},  /* Ъ */

    /* ── Bottom letter row: Я Ч С М И Т Ь Б Ю ────────────────── */
    {"\xd0\xaf\nQ",   MS7004_KEY_Q,         false, false},  /* Я\nQ */
    {"\xd0\xa7\n\xc2\xac", MS7004_KEY_CHE,  false, false},  /* Ч\n¬ */
    {"\xd0\xa1\nS",   MS7004_KEY_S,         false, false},  /* С\nS */
    {"\xd0\x9c\nM",   MS7004_KEY_M,         false, false},  /* М\nM */
    {"\xd0\x98\nI",   MS7004_KEY_I,         false, false},  /* И\nI */
    {"\xd0\xa2\nT",   MS7004_KEY_T,         false, false},  /* Т\nT */
    {"\xd0\xac\nX",   MS7004_KEY_X,         false, false},  /* Ь\nX */
    {"\xd0\x91\nB",   MS7004_KEY_B,         false, false},  /* Б\nB */
    {"\xd0\xae\n@",   MS7004_KEY_AT,        false, false},  /* Ю\n@ */
    {",\n<",           MS7004_KEY_COMMA,     false, false},
    {"/\n?",           MS7004_KEY_SLASH,     false, false},
    {"_",              MS7004_KEY_UNDERSCORE, false, false},

    /* ── Modifiers ──────────────────────────────────────────────── */
    {"\xd0\x92\xd0\xa0", MS7004_KEY_SHIFT_L, true, false},  /* ВР (first = left) */
    {"\xd0\xa1\xd0\xa3", MS7004_KEY_CTRL,    true, false},  /* СУ */
    {"\xd0\x9a\xd0\x9c\xd0\x9f", MS7004_KEY_COMPOSE, true, false}, /* КМП */

    /* ── Toggles ────────────────────────────────────────────────── */
    {"\xd0\xa4\xd0\x9a\xd0\xa1", MS7004_KEY_CAPS, false, true},   /* ФКС */
    {"\xd0\xa0\xd0\xa3\xd0\xa1\n\xd0\x9b\xd0\x90\xd0\xa2", MS7004_KEY_RUSLAT, false, true}, /* РУС\nЛАТ */

    /* ── Numpad enter ───────────────────────────────────────────── */
    {"\xd0\x92\xd0\x92\xd0\x9e\xd0\x94", MS7004_KEY_KP_ENTER, false, false}, /* ВВОД */
};

/* Numpad digit/symbol labels are single characters that collide with
 * digit-row labels.  We handle them separately in bindCap by checking
 * whether the cap's position puts it in the numpad region (rightmost
 * columns in rows 1-5). */
struct NumpadLabel {
    const char  *label;
    ms0515::Key key;
};

const NumpadLabel kNumpad[] = {
    {"7", MS7004_KEY_KP_7}, {"8", MS7004_KEY_KP_8}, {"9", MS7004_KEY_KP_9},
    {"4", MS7004_KEY_KP_4}, {"5", MS7004_KEY_KP_5}, {"6", MS7004_KEY_KP_6},
    {"1", MS7004_KEY_KP_1}, {"2", MS7004_KEY_KP_2}, {"3", MS7004_KEY_KP_3},
    {"0", MS7004_KEY_KP0_WIDE},
    {",", MS7004_KEY_KP_COMMA},
    {".", MS7004_KEY_KP_DOT},
    {"-", MS7004_KEY_KP_MINUS},
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

/* Mode-dependent letter classification.  In ЛАТ mode, only the 26 keys
 * with dual Latin+Cyrillic letter labels count as "letters" for ФКС/ВР
 * purposes.  In РУС mode, the symbol-on-letter keys (Ш/[ Щ/] Э/\ Ч/¬
 * Ю/@ Ъ) also count — they produce Cyrillic letters in that mode.
 * See "UX convenience layer" in handleClick. */
bool isLetterKey(ms0515::Key k, bool rusMode)
{
    switch (k) {
    /* Pure letter keys: always letters in both modes. */
    case MS7004_KEY_A: case MS7004_KEY_B: case MS7004_KEY_C:
    case MS7004_KEY_D: case MS7004_KEY_E: case MS7004_KEY_F:
    case MS7004_KEY_G: case MS7004_KEY_H: case MS7004_KEY_I:
    case MS7004_KEY_J: case MS7004_KEY_K: case MS7004_KEY_L:
    case MS7004_KEY_M: case MS7004_KEY_N: case MS7004_KEY_O:
    case MS7004_KEY_P: case MS7004_KEY_Q: case MS7004_KEY_R:
    case MS7004_KEY_S: case MS7004_KEY_T: case MS7004_KEY_U:
    case MS7004_KEY_V: case MS7004_KEY_W: case MS7004_KEY_X:
    case MS7004_KEY_Y: case MS7004_KEY_Z:
        return true;
    /* Symbol-on-letter: Cyrillic letter in РУС, symbol in ЛАТ. */
    case MS7004_KEY_LBRACKET:   /* Ш/[ */
    case MS7004_KEY_RBRACKET:   /* Щ/] */
    case MS7004_KEY_BACKSLASH:  /* Э/\ */
    case MS7004_KEY_CHE:        /* Ч/¬ */
    case MS7004_KEY_AT:         /* Ю/@ */
    case MS7004_KEY_HARDSIGN:   /* Ъ   */
        return rusMode;
    default:
        return false;
    }
}

/* Symbol-on-letter keys that are immune to ВР (Shift) in ЛАТ mode.
 * In РУС mode they act as letters and respond to Shift normally. */
bool isShiftImmuneSymbol(ms0515::Key k, bool rusMode)
{
    if (rusMode) return false;
    return k == MS7004_KEY_LBRACKET    /* Ш/[  — Shift would give { */
        || k == MS7004_KEY_RBRACKET    /* Щ/]  — Shift would give } */
        || k == MS7004_KEY_BACKSLASH   /* Э/\  — Shift would give | */
        || k == MS7004_KEY_CHE;        /* Ч/¬  — Shift would give ~ */
}

} /* anonymous namespace */

/* ── OnScreenKeyboard ─────────────────────────────────────────────────── */

OnScreenKeyboard::OnScreenKeyboard() = default;

/* ── Layout parsing ───────────────────────────────────────────────────── */

bool OnScreenKeyboard::parseLine(const std::string &raw, Cap &out) const
{
    std::string s = raw;
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' ||
                          s.back() == '\t' || s.back() == '\n'))
        s.pop_back();
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
    out.w = w;

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

void OnScreenKeyboard::bindCap(Cap &k, int &shiftCountInRow,
                               bool /*inFnRow*/) const
{
    if (!k.drawn || k.dim) return;

    /* Wide blank cap = spacebar. */
    if (k.label.empty() && k.w >= 5.0f) {
        k.ms7004key = MS7004_KEY_SPACE;
        return;
    }

    /* Try numpad first for wide caps (numpad 0 is 2 units wide and its
     * "0" label collides with the digit-row "0" in kLabelKeys). */
    if (k.w >= 1.5f) {
        if (const NumpadLabel *m = findNumpad(k.label)) {
            k.ms7004key = m->key;
            return;
        }
    }

    /* Try the main label table. */
    if (const LabelKey *m = findByLabel(k.label)) {
        k.ms7004key = m->key;
        k.sticky    = m->sticky;
        k.toggle    = m->toggle;
        /* Second ВР cap on the same row = right Shift. */
        if (k.ms7004key == MS7004_KEY_SHIFT_L && shiftCountInRow++ > 0)
            k.ms7004key = MS7004_KEY_SHIFT_R;
        return;
    }

    /* Try numpad (short labels like "1", ".", "-", ","). */
    if (const NumpadLabel *m = findNumpad(k.label)) {
        k.ms7004key = m->key;
        return;
    }

    k.dim = true;  /* unknown label → inert */
}

bool OnScreenKeyboard::loadLayout(const std::string &path)
{
    rows_.clear();
    stickyKeys_.clear();

    std::vector<std::string> candidates;
    if (!path.empty()) {
        candidates.push_back(path);
    } else {
        const char *rels[] = {
            "assets/keyboard/ms7004_layout.txt",
        };
        for (const auto &root : Paths::searchRoots())
            for (const char *rel : rels)
                candidates.push_back((root / rel).string());
    }
    std::string chosen;
    for (const auto &p : candidates)
        if (std::filesystem::exists(p)) { chosen = p; break; }
    if (chosen.empty()) return false;

    std::ifstream f(chosen);
    if (!f) return false;

    std::vector<Cap> cur;
    bool inSection = false;
    bool inFnRow   = false;
    int  shiftCountInRow = 0;
    std::string curSection;

    auto flush = [&]() {
        if (inSection && !cur.empty()) {
            if (!inFnRow) {
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

    std::string line;
    while (std::getline(f, line)) {
        std::string t = line;
        while (!t.empty() && (t.back() == '\r' || t.back() == ' ' ||
                              t.back() == '\t'))
            t.pop_back();
        size_t j = 0;
        while (j < t.size() && (t[j] == ' ' || t[j] == '\t')) ++j;
        if (j < t.size() && t[j] == '[') {
            flush();
            inSection = true;
            size_t e = t.find(']', j);
            curSection = (e != std::string::npos)
                ? t.substr(j + 1, e - j - 1) : std::string();
            inFnRow = (curSection == "fn");
            continue;
        }
        Cap k;
        if (!parseLine(line, k)) continue;
        if (inFnRow) k.gray = k.drawn;
        bindCap(k, shiftCountInRow, inFnRow);
        cur.push_back(std::move(k));
    }
    flush();

    /* Diagnostic dump (uncomment for debugging layout issues). */
#if 0
    std::fprintf(stderr, "[OSK] layout loaded from: %s\n", chosen.c_str());
    for (size_t r = 0; r < rows_.size(); ++r) {
        std::fprintf(stderr, "[OSK] row %zu:\n", r);
        for (size_t i = 0; i < rows_[r].size(); ++i) {
            const Cap &k = rows_[r][i];
            if (!k.drawn) continue;
            std::string label;
            for (char c : k.label) {
                if (c == '\n') label += "\\n";
                else           label += c;
            }
            if (k.ms7004key != MS7004_KEY_NONE) {
                uint8_t sc = ms7004_scancode(k.ms7004key);
                std::fprintf(stderr,
                    "  [%2zu] %-16s key=%d sc=0%o%s%s\n",
                    i, ("\"" + label + "\"").c_str(),
                    (int)k.ms7004key, (unsigned)sc,
                    k.sticky ? " sticky" : "",
                    k.toggle ? " toggle" : "");
            } else {
                std::fprintf(stderr,
                    "  [%2zu] %-16s UNBOUND%s\n",
                    i, ("\"" + label + "\"").c_str(),
                    k.dim ? " (dim)" : "");
            }
        }
    }
    std::fflush(stderr);
#endif

    return !rows_.empty();
}

/* ── Geometry ─────────────────────────────────────────────────────────── */

float OnScreenKeyboard::pixelWidth() const
{
    float maxW = 0;
    for (const auto &row : rows_) {
        float w = 0;
        for (const auto &k : row) w += k.w * unit_;
        if (w > maxW) maxW = w;
    }
    return maxW + 24.0f;
}

float OnScreenKeyboard::pixelHeight() const
{
    return (float)rows_.size() * (unit_ * 0.95f + 4.0f) + 28.0f;
}

/* ── Click dispatch ───────────────────────────────────────────────────── */

void OnScreenKeyboard::handleClick(const Cap &c, ms0515::Emulator &emu)
{
    if (c.dim || c.ms7004key == MS7004_KEY_NONE) return;

    /* Ъ and _ share scancode 0o361.  The ROM renders it as Ъ in РУС
     * and _ in ЛАТ.  Suppress Ъ in ЛАТ (it has no Latin equivalent).
     * _ in РУС is handled by RUSLAT-immunity below (temporarily
     * switches to ЛАТ so the ROM outputs _). */
    if (c.ms7004key == MS7004_KEY_HARDSIGN && !emu.ruslatOn())
        return;

    /* Sticky modifier cap: toggle latch. */
    if (c.sticky) {
        int k = (int)c.ms7004key;
        if (stickyKeys_.count(k)) {
            /* Unlatch: release in ms7004. */
            emu.keyPress(c.ms7004key, false);
            stickyKeys_.erase(k);
        } else {
            /* Latch: press in ms7004. */
            emu.keyPress(c.ms7004key, true);
            stickyKeys_.insert(k);
        }
        return;
    }

    /* Toggle caps (ФКС, РУС-ЛАТ): press + release.  The ms7004 model
     * flips the internal toggle flag on press; release is a no-op. */
    if (c.toggle) {
        emu.keyPress(c.ms7004key, true);
        emu.keyPress(c.ms7004key, false);
        return;
    }

    /* ── UX convenience layer ──────────────────────────────────────
     *
     * Four deviations from authentic MS7004 / ROM behaviour, applied
     * only to OSK clicks (physical keyboard goes through the model
     * unmodified).  See comments marked [DEVIATE] in ms7004.c.
     *
     * 1. ВР (Shift) does not change symbol-on-letter keys (Ш/[ Щ/]
     *    Э/\ Ч/¬) in ЛАТ mode.  On real hardware, Shift + [ → {.
     *    Here, the OSK releases Shift before emitting the key.
     *    In РУС mode these keys are letters and respond to Shift
     *    normally.
     *
     * 2. ФКС (CapsLock) only affects letter keys.  Digits, symbols,
     *    and function keys are immune.  Which keys count as "letters"
     *    is mode-dependent: in РУС mode, Ш/Щ/Э/Ч/Ю/Ъ are letters.
     *    Implemented by temporarily toggling CAPS off around emission.
     *
     * 3. ВР inverts ФКС on letter keys.  On real MS7004, CAPS +
     *    Shift still produces uppercase.  Here, CAPS + Shift + letter
     *    produces lowercase (modern CapsLock + Shift cancellation).
     *
     * 4. РУС/ЛАТ does not change non-letter keys.  On real hardware,
     *    the ROM maps some symbol scancodes to Cyrillic in РУС mode
     *    (e.g. { → Ш, } → Щ).  Here, non-letter keys temporarily
     *    switch to ЛАТ so their symbol output is preserved.
     * ──────────────────────────────────────────────────────────────── */

    const bool rusMode      = emu.ruslatOn();
    const bool shiftLatched = stickyKeys_.count((int)MS7004_KEY_SHIFT_L)
                           || stickyKeys_.count((int)MS7004_KEY_SHIFT_R);
    const bool capsOn       = emu.capsOn();
    const bool letter       = isLetterKey(c.ms7004key, rusMode);
    const bool shiftImmune  = isShiftImmuneSymbol(c.ms7004key, rusMode);

    /* Do we need to suppress Shift before the key? */
    const bool dropShift = shiftLatched
                        && (shiftImmune                     /* fix 1 */
                         || (letter && capsOn));             /* fix 3 */

    /* Do we need to temporarily toggle CAPS off? */
    const bool toggleCapsOff = capsOn
                            && (!letter                     /* fix 2 */
                             || shiftLatched);               /* fix 3 */

    /* Do we need to temporarily switch to ЛАТ? */
    const bool toggleRusOff = rusMode && !letter;            /* fix 4 */

    if (dropShift || toggleCapsOff || toggleRusOff) {
        /* Release Shift sticky keys from the ms7004 model. */
        if (dropShift) {
            for (auto it = stickyKeys_.begin(); it != stickyKeys_.end(); ) {
                auto mk = static_cast<ms0515::Key>(*it);
                if (mk == MS7004_KEY_SHIFT_L || mk == MS7004_KEY_SHIFT_R) {
                    emu.keyPress(mk, false);
                    it = stickyKeys_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        /* Temporarily switch to ЛАТ so the ROM outputs the Latin
         * symbol, not a Cyrillic letter for this scancode. */
        if (toggleRusOff) {
            emu.keyPress(MS7004_KEY_RUSLAT, true);
            emu.keyPress(MS7004_KEY_RUSLAT, false);
        }

        /* Temporarily flip CAPS off in both the ms7004 model and the
         * guest ROM (they track toggle state independently). */
        if (toggleCapsOff) {
            emu.keyPress(MS7004_KEY_CAPS, true);
            emu.keyPress(MS7004_KEY_CAPS, false);
        }

        /* Emit the key itself. */
        emu.keyPress(c.ms7004key, true);
        emu.keyPress(c.ms7004key, false);

        /* Restore CAPS to its original state. */
        if (toggleCapsOff) {
            emu.keyPress(MS7004_KEY_CAPS, true);
            emu.keyPress(MS7004_KEY_CAPS, false);
        }

        /* Restore РУС mode. */
        if (toggleRusOff) {
            emu.keyPress(MS7004_KEY_RUSLAT, true);
            emu.keyPress(MS7004_KEY_RUSLAT, false);
        }

        /* Release any remaining sticky modifiers (Ctrl, Compose). */
        if (!stickyKeys_.empty()) {
            for (int k : stickyKeys_)
                emu.keyPress(static_cast<ms0515::Key>(k), false);
            stickyKeys_.clear();
        }
        return;
    }

    /* Regular key: press, release, then release any sticky mods
     * (one-shot behaviour). */
    emu.keyPress(c.ms7004key, true);
    emu.keyPress(c.ms7004key, false);

    if (!stickyKeys_.empty()) {
        for (int k : stickyKeys_)
            emu.keyPress(static_cast<ms0515::Key>(k), false);
        stickyKeys_.clear();
    }
}

/* ── Rendering ────────────────────────────────────────────────────────── */

bool OnScreenKeyboard::highlighted(const Cap &c,
                                   const ms0515::Emulator &emu) const
{
    if (c.ms7004key == MS7004_KEY_NONE) return false;

    /* Toggle caps: lit when the toggle is on. */
    if (c.toggle) {
        if (c.ms7004key == MS7004_KEY_CAPS)   return emu.capsOn();
        if (c.ms7004key == MS7004_KEY_RUSLAT) return emu.ruslatOn();
    }

    /* Sticky modifiers: lit when latched OR held physically. */
    if (c.sticky) {
        if (stickyKeys_.count((int)c.ms7004key)) return true;
    }

    /* All keys: lit when physically held in the ms7004 model. */
    return emu.keyHeld(c.ms7004key);
}

void OnScreenKeyboard::drawRow(size_t rowIdx, ms0515::Emulator &emu)
{
    const auto &keys    = rows_[rowIdx];
    const float spacing = 3.0f;
    const float capH    = unit_ * 0.95f;

    for (size_t i = 0; i < keys.size(); ++i) {
        const Cap &k = keys[i];
        float btnW = unit_ * k.w - spacing;
        if (btnW < 1) btnW = 1;
        ImVec2 sz(btnW, capH);

        if (!k.drawn) {
            auto id = std::format("##gap{}_{}", rowIdx, i);
            ImGui::InvisibleButton(id.c_str(), sz);
            ImGui::SameLine(0, spacing);
            continue;
        }

        int colorsPushed = 0;
        if (highlighted(k, emu)) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.95f, 0.65f, 0.20f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.72f, 0.28f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.85f, 0.55f, 0.15f, 1.0f));
            colorsPushed += 3;
        } else if (k.gray) {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.62f, 0.62f, 0.64f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.70f, 0.72f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.55f, 0.55f, 0.57f, 1.0f));
            colorsPushed += 3;
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.94f, 0.94f, 0.94f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 1.00f, 1.00f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
            colorsPushed += 3;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
        ++colorsPushed;

        ImGui::PushID((int)(rowIdx * 1024 + i));
        if (ImGui::Button(k.label.empty() ? " " : k.label.c_str(), sz))
            handleClick(k, emu);
        ImGui::PopID();

        ImGui::PopStyleColor(colorsPushed);
        ImGui::SameLine(0, spacing);
    }
    ImGui::NewLine();
}

void OnScreenKeyboard::draw(ms0515::Emulator &emu, bool &open)
{
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.22f, 0.22f, 0.23f, 1.0f));
    if (!ImGui::Begin("Keyboard (MS7004)", &open,
                      ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    if (rows_.empty()) {
        ImGui::TextUnformatted("ms7004_layout.txt not found.");
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    for (size_t r = 0; r < rows_.size(); ++r) {
        drawRow(r, emu);
        if (r == 0) ImGui::Dummy(ImVec2(1, 4));
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

} /* namespace ms0515_frontend */
