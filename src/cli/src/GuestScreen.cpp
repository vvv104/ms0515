/*
 * GuestScreen.cpp — the shadow's cells, row by row, runs of inverted
 * cells as inverted text.
 */
#include "GuestScreen.hpp"

#include <algorithm>
#include <string>

namespace ms0515::cli {

namespace {

ftxui::Element guestRow(const VramMirror::Snapshot &s, int row)
{
    ftxui::Elements runs;
    std::string run;
    bool runInverted = false;
    const auto flush = [&] {
        if (run.empty()) return;
        ftxui::Element e = ftxui::text(run);
        runs.push_back(runInverted ? e | ftxui::inverted : e);
        run.clear();
    };
    for (int col = 0; col < VramMirror::kCols; ++col) {
        const size_t at = static_cast<size_t>(row) * VramMirror::kCols + static_cast<size_t>(col);
        if (s.inverted[at] != runInverted) { flush(); runInverted = s.inverted[at]; }
        run += VramMirror::utf8FromKoi8(s.cells[at]);
    }
    flush();
    return ftxui::hbox(std::move(runs));
}

} // namespace

ftxui::Element guestRows(const VramMirror::Snapshot &snapshot, int from, int to)
{
    ftxui::Elements rows;
    const int first = std::max(0, from);
    const int last = std::min(to, VramMirror::kRows);
    for (int row = first; row < last; ++row) rows.push_back(guestRow(snapshot, row));
    return ftxui::vbox(std::move(rows));
}

} /* namespace ms0515::cli */
