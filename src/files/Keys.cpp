/*
 * Keys.cpp — xterm's function-key sequences decoded.
 */
#include "Keys.hpp"

#include <cctype>
#include <string>

namespace ms0515::files {

namespace {

/* F1..F4 end in a letter P..S; F5..F12 are numbered CSI codes ending in '~'. */
int letterKey(char c)
{
    return c >= 'P' && c <= 'S' ? c - 'P' + 1 : 0;
}

int tildeKey(int code)
{
    switch (code) {
    case 11: return 1;  case 12: return 2;  case 13: return 3;  case 14: return 4;
    case 15: return 5;  case 17: return 6;  case 18: return 7;  case 19: return 8;
    case 20: return 9;  case 21: return 10; case 23: return 11; case 24: return 12;
    default: return 0;
    }
}

/* xterm's modifier parameter: 1 + (shift 1, alt 2, ctrl 4). */
void applyModifier(FunctionKey &key, int param)
{
    const int bits = param - 1;
    key.shift = (bits & 1) != 0;
    key.alt   = (bits & 2) != 0;
    key.ctrl  = (bits & 4) != 0;
}

/* "\x1B[" then digits, optionally ";" digits, then the final byte. */
std::optional<FunctionKey> parseCsi(std::string_view s)
{
    size_t i = 0;
    auto number = [&]() {
        int n = 0;
        bool any = false;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) { n = n * 10 + (s[i] - '0'); ++i; any = true; }
        return any ? n : -1;
    };
    const int first = number();
    if (first < 0) return std::nullopt;
    int modifier = 1;
    if (i < s.size() && s[i] == ';') {
        ++i;
        modifier = number();
        if (modifier < 1) return std::nullopt;
    }
    if (i + 1 != s.size()) return std::nullopt;
    const char final = s[i];
    FunctionKey key;
    if (final == '~') key.number = tildeKey(first);
    else if (first == 1) key.number = letterKey(final);
    if (key.number == 0) return std::nullopt;
    applyModifier(key, modifier);
    return key;
}

} // namespace

std::optional<FunctionKey> parseFunctionKey(std::string_view raw)
{
    if (raw.size() < 3 || raw[0] != '\x1B') return std::nullopt;
    bool escAlt = false;
    if (raw[1] == '\x1B') { escAlt = true; raw.remove_prefix(1); if (raw.size() < 3) return std::nullopt; }
    std::optional<FunctionKey> key;
    if (raw[1] == 'O' && raw.size() == 3) {
        FunctionKey k;
        k.number = letterKey(raw[2]);
        if (k.number) key = k;
    } else if (raw[1] == '[') {
        key = parseCsi(raw.substr(2));
    }
    if (key && escAlt) key->alt = true;
    return key;
}

} /* namespace ms0515::files */
