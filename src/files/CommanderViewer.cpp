/*
 * CommanderViewer.cpp — the file viewer, mc's: the name and the position
 * on the top line, the key bar below; F2 wraps, F4 hex, F5 goes to a
 * line or an offset, F7 searches, F8 changes the encoding, F9 the octal
 * dump the machine's DUMP would print; F3 / F10 / Esc leave.
 */
#include "TuiImpl.hpp"

#include <fmt/format.h>

#include <algorithm>

namespace ms0515::files::detail {

using namespace ftxui;

Element Tui::renderViewer() const
{
    const ViewState &v = *view_;
    const int rows = std::max(1, height_ - 2);
    Elements lines;
    for (int i = v.top; i < v.top + rows; ++i)
        lines.push_back(text(i < static_cast<int>(v.lines.size()) ? v.lines[static_cast<size_t>(i)] : ""));
    const int total = static_cast<int>(v.lines.size());
    const int percent = total == 0 || v.top + rows >= total ? 100 : (v.top + rows) * 100 / total;
    const std::string pos = fmt::format("{}/{}  {}%  {}  {}", std::min(v.top + 1, total), total, percent,
                                        viewName(v.opts.view), encodingName(v.opts.encoding));
    const std::vector<std::pair<const char *, const char *>> keys = {
        {"1", "Help"}, {"2", v.opts.wrap ? "UnWrap" : "Wrap"}, {"3", "Quit"},
        {"4", v.opts.view == View::hex ? "Ascii" : "Hex"}, {"5", "Goto"}, {"6", ""}, {"7", "Search"},
        {"8", "Codepg"}, {"9", v.opts.view == View::octal ? "Ascii" : "Octal"}, {"10", "Quit"}};
    return vbox({hbox({text(" " + v.name), filler(), text(pos + " ")}) | kCursor,
                 vbox(lines) | flex | kPanel,
                 renderKeyBar(keys)});
}

bool Tui::onViewerKey(const Event &e)
{
    ViewState &v = *view_;
    const int rows = std::max(1, height_ - 2);
    const int maxTop = std::max(0, static_cast<int>(v.lines.size()) - rows);
    auto rerender = [&] {
        v.lines = renderLines(v.bytes, v.opts);
        v.top = std::min(v.top, std::max(0, static_cast<int>(v.lines.size()) - rows));
    };
    if (e == Event::Escape || e == Event::F3 || e == Event::F10) { view_.reset(); return true; }
    if (e == Event::ArrowUp)        v.top = std::max(0, v.top - 1);
    else if (e == Event::ArrowDown) v.top = std::min(maxTop, v.top + 1);
    else if (e == Event::PageUp)    v.top = std::max(0, v.top - rows);
    else if (e == Event::PageDown)  v.top = std::min(maxTop, v.top + rows);
    else if (e == Event::Home)      v.top = 0;
    else if (e == Event::End)       v.top = maxTop;
    else if (e == Event::F1) {
        message("Help", {"F2 wrap / unwrap      F4 hex dump / text     F9 octal dump / text",
                         "F5 go to a line or an offset                 F7 search",
                         "F8 the next encoding (ASCII, KOI-8R, KOI-7, KOI-7 ^N/^O, CP866)",
                         "F3 / F10 / Esc back to the panels"});
    }
    else if (e == Event::F2) { v.opts.wrap = !v.opts.wrap; rerender(); }
    else if (e == Event::F4) { v.opts.view = v.opts.view == View::hex ? View::text : View::hex; rerender(); }
    else if (e == Event::F9) { v.opts.view = v.opts.view == View::octal ? View::text : View::octal; rerender(); }
    else if (e == Event::F8) { v.opts.encoding = nextEncoding(v.opts.encoding); rerender(); }
    else if (e == Event::F5) viewerGoto();
    else if (e == Event::F7) viewerSearch();
    return true;
}

/* A line number in the text, an offset (octal, or hex in the hex dump)
 * in the dumps - 16 bytes a line. */
void Tui::viewerGoto()
{
    const ViewState &v = *view_;
    const bool textView = v.opts.view == View::text;
    const int base = v.opts.view == View::hex ? 16 : 8;
    const std::string what = textView ? fmt::format("Line number (1..{}):", v.lines.size())
                                      : (base == 16 ? "Offset (hex):" : "Offset (octal):");
    inputDialog("Goto", {what}, "", "", false, [this, textView, base](const std::string &s) {
        ViewState &vs = *view_;
        const int rows = std::max(1, height_ - 2);
        const int maxTop = std::max(0, static_cast<int>(vs.lines.size()) - rows);
        try {
            const long n = std::stol(s, nullptr, textView ? 10 : base);
            vs.top = std::clamp(static_cast<int>(textView ? n - 1 : n / 16), 0, maxTop);
        } catch (const std::exception &) {
            status_ = s + ": not a number";
        }
    });
}

void Tui::viewerSearch()
{
    const ViewState &v = *view_;
    inputDialog("Search", {"Enter search string (in " + std::string(encodingName(v.opts.encoding)) + "):"}, "", v.lastSearch, false,
                [this](const std::string &s) {
        ViewState &vs = *view_;
        const int rows = std::max(1, height_ - 2);
        vs.lastSearch = s;
        const auto needle = encodeString(s, vs.opts.encoding);
        if (!needle) { status_ = "Not in this encoding."; return; }
        const size_t from = vs.opts.view == View::text ? 0 : static_cast<size_t>(vs.top) * 16;
        const auto at = findBytes(vs.bytes, *needle, from);
        if (!at) { status_ = "Not found."; return; }
        if (vs.opts.view != View::text) vs.top = static_cast<int>(*at / 16);
        else {
            /* the line the offset falls in: count line breaks before it */
            int line = 0, col = 0;
            for (size_t i = 0; i < *at; ++i) {
                if (vs.bytes[i] == '\n') { ++line; col = 0; }
                else if (++col >= 80 && vs.opts.wrap) { ++line; col = 0; }
            }
            vs.top = line;
        }
        vs.top = std::clamp(vs.top, 0, std::max(0, static_cast<int>(vs.lines.size()) - rows));
    });
}

} /* namespace ms0515::files::detail */
