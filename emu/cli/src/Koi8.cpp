/*
 * Koi8.cpp — KOI-8R ↔ UTF-8 conversion tables.
 *
 * KOI-8R table from RFC 1489.  Bytes 0x00–0x7F are ASCII (passed through
 * as a single UTF-8 byte).  Bytes 0x80–0xFF map to specific Cyrillic /
 * box-drawing code-points; the table below stores them as raw code-
 * points so the encoder can synthesise the UTF-8 sequence on demand.
 */

#include "Koi8.hpp"

#include <unordered_map>

namespace ms0515::cli::koi8 {

namespace {

/* KOI-8R high-half code-points (0x80..0xFF → Unicode).  Entry 0
 * corresponds to byte 0x80. */
constexpr uint32_t kKoi8Hi[128] = {
    0x2500,0x2502,0x250C,0x2510,0x2514,0x2518,0x251C,0x2524,
    0x252C,0x2534,0x253C,0x2580,0x2584,0x2588,0x258C,0x2590,
    0x2591,0x2592,0x2593,0x2320,0x25A0,0x2219,0x221A,0x2248,
    0x2264,0x2265,0x00A0,0x2321,0x00B0,0x00B2,0x00B7,0x00F7,
    0x2550,0x2551,0x2552,0x0451,0x2553,0x2554,0x2555,0x2556,
    0x2557,0x2558,0x2559,0x255A,0x255B,0x255C,0x255D,0x255E,
    0x255F,0x2560,0x2561,0x0401,0x2562,0x2563,0x2564,0x2565,
    0x2566,0x2567,0x2568,0x2569,0x256A,0x256B,0x256C,0x00A9,
    0x044E,0x0430,0x0431,0x0446,0x0434,0x0435,0x0444,0x0433,
    0x0445,0x0438,0x0439,0x043A,0x043B,0x043C,0x043D,0x043E,
    0x043F,0x044F,0x0440,0x0441,0x0442,0x0443,0x0436,0x0432,
    0x044C,0x044B,0x0437,0x0448,0x044D,0x0449,0x0447,0x044A,
    0x042E,0x0410,0x0411,0x0426,0x0414,0x0415,0x0424,0x0413,
    0x0425,0x0418,0x0419,0x041A,0x041B,0x041C,0x041D,0x041E,
    0x041F,0x042F,0x0420,0x0421,0x0422,0x0423,0x0416,0x0412,
    0x042C,0x042B,0x0417,0x0428,0x042D,0x0429,0x0427,0x042A,
};

void encodeUtf8(std::string &out, uint32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

const std::unordered_map<uint32_t, uint8_t> &reverseTable()
{
    static const std::unordered_map<uint32_t, uint8_t> table = []() {
        std::unordered_map<uint32_t, uint8_t> m;
        m.reserve(128);
        for (int i = 0; i < 128; ++i) {
            m.emplace(kKoi8Hi[i], static_cast<uint8_t>(0x80 + i));
        }
        return m;
    }();
    return table;
}

}  /* namespace */

void appendAsUtf8(std::string &out, uint8_t b)
{
    uint32_t cp = (b < 0x80) ? static_cast<uint32_t>(b)
                              : kKoi8Hi[b - 0x80];
    encodeUtf8(out, cp);
}

namespace {

/* KOI-7 N2 (GOST 13052-67) mapping for printable ASCII positions to
 * Cyrillic.  Indices are byte - 0x20 (so kKoi7N2[0] = byte 0x20 = space).
 *
 * Bytes 0x20..0x3F (space + digits + most ASCII punctuation) pass
 * through unchanged: they map to the same printable glyph in both N1
 * and N2.  The Cyrillic substitutions start at 0x40 and run through
 * 0x7F.
 *
 * Case is INVERTED relative to ASCII: lowercase Latin positions
 * (0x60..0x7F) hold UPPERCASE Cyrillic, uppercase Latin positions
 * (0x40..0x5F) hold LOWERCASE Cyrillic.  This is the MS-0515
 * convention (per user report) and matches what produces e.g.
 * "Адаптация" rather than "АДАПТАЦИЯ" out of the boot banner
 * bytes "aDAPTACIQ" — the lowercase 'a' is the capital 'А' and the
 * uppercase 'D' is the lowercase 'д'. */
constexpr uint32_t kKoi7N2[0x60] = {
    /* 0x20.. */ 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    /* 0x28.. */ 0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
    /* 0x30.. */ 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    /* 0x38.. */ 0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    /* 0x40.. uppercase Latin → lowercase Cyrillic */
    0x044E, /* @ → ю */ 0x0430, /* A → а */ 0x0431, /* B → б */
    0x0446, /* C → ц */ 0x0434, /* D → д */ 0x0435, /* E → е */
    0x0444, /* F → ф */ 0x0433, /* G → г */
    /* 0x48.. */
    0x0445, /* H → х */ 0x0438, /* I → и */ 0x0439, /* J → й */
    0x043A, /* K → к */ 0x043B, /* L → л */ 0x043C, /* M → м */
    0x043D, /* N → н */ 0x043E, /* O → о */
    /* 0x50.. */
    0x043F, /* P → п */ 0x044F, /* Q → я */ 0x0440, /* R → р */
    0x0441, /* S → с */ 0x0442, /* T → т */ 0x0443, /* U → у */
    0x0436, /* V → ж */ 0x0432, /* W → в */
    /* 0x58.. */
    0x044C, /* X → ь */ 0x044B, /* Y → ы */ 0x0437, /* Z → з */
    0x0448, /* [ → ш */ 0x044D, /* \ → э */ 0x0449, /* ] → щ */
    0x0447, /* ^ → ч */ 0x044A, /* _ → ъ */
    /* 0x60.. lowercase Latin → uppercase Cyrillic */
    0x042E, /* ` → Ю */ 0x0410, /* a → А */ 0x0411, /* b → Б */
    0x0426, /* c → Ц */ 0x0414, /* d → Д */ 0x0415, /* e → Е */
    0x0424, /* f → Ф */ 0x0413, /* g → Г */
    /* 0x68.. */
    0x0425, /* h → Х */ 0x0418, /* i → И */ 0x0419, /* j → Й */
    0x041A, /* k → К */ 0x041B, /* l → Л */ 0x041C, /* m → М */
    0x041D, /* n → Н */ 0x041E, /* o → О */
    /* 0x70.. */
    0x041F, /* p → П */ 0x042F, /* q → Я */ 0x0420, /* r → Р */
    0x0421, /* s → С */ 0x0422, /* t → Т */ 0x0423, /* u → У */
    0x0416, /* v → Ж */ 0x0412, /* w → В */
    /* 0x78.. */
    0x042C, /* x → Ь */ 0x042B, /* y → Ы */ 0x0417, /* z → З */
    0x0428, /* { → Ш */ 0x042D, /* | → Э */ 0x0429, /* } → Щ */
    0x0427, /* ~ → Ч */ 0x042A, /* DEL → Ъ */
};

}  /* namespace */

void appendAsKoi7N2(std::string &out, uint8_t b)
{
    if (b < 0x20u || b > 0x7Fu) {
        appendAsUtf8(out, b);
        return;
    }
    encodeUtf8(out, kKoi7N2[b - 0x20u]);
}

size_t utf8ToKoi8(const uint8_t *data, size_t size, uint8_t *out)
{
    if (size == 0) return 0;
    uint8_t b0 = data[0];

    /* ASCII fast path. */
    if (b0 < 0x80) {
        *out = b0;
        return 1;
    }

    /* Determine the multi-byte length from the lead byte. */
    size_t len;
    uint32_t cp;
    if ((b0 & 0xE0) == 0xC0) {
        len = 2; cp = b0 & 0x1F;
    } else if ((b0 & 0xF0) == 0xE0) {
        len = 3; cp = b0 & 0x0F;
    } else if ((b0 & 0xF8) == 0xF0) {
        len = 4; cp = b0 & 0x07;
    } else {
        /* Invalid lead byte — emit '?', consume one byte. */
        *out = '?';
        return 1;
    }
    if (size < len) return 0;
    for (size_t i = 1; i < len; ++i) {
        if ((data[i] & 0xC0) != 0x80) {
            *out = '?';
            return i;       /* resync at the bad byte */
        }
        cp = (cp << 6) | (data[i] & 0x3F);
    }

    const auto &rev = reverseTable();
    auto it = rev.find(cp);
    *out = (it != rev.end()) ? it->second : static_cast<uint8_t>('?');
    return len;
}

}  /* namespace ms0515::cli::koi8 */
