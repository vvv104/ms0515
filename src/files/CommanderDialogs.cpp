/*
 * CommanderDialogs.cpp — mc's dialogs and pull-down menus: how they are
 * drawn and how the keys move through them.
 */
#include "TuiImpl.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cctype>

namespace ms0515::files::detail {

using namespace ftxui;

namespace {

constexpr int kDialogWidth = 56;

} // namespace

std::vector<Dialog::Focus> Dialog::parts() const
{
    std::vector<Focus> out;
    if (hasInput) out.push_back(Focus::input);
    if (!radio.empty()) out.push_back(Focus::radio);
    if (!checks.empty()) out.push_back(Focus::checks);
    if (!items.empty()) out.push_back(Focus::items);
    if (!buttons.empty()) out.push_back(Focus::buttons);
    return out;
}

void Dialog::focusNext(int step)
{
    const auto p = parts();
    if (p.empty()) return;
    const auto it = std::find(p.begin(), p.end(), focus);
    const int at = it == p.end() ? 0 : static_cast<int>(it - p.begin());
    const int n = static_cast<int>(p.size());
    focus = p[static_cast<size_t>(((at + step) % n + n) % n)];
}

Element Tui::renderDialog() const
{
    const Dialog &d = *dialog_;
    Elements body;
    for (const auto &l : d.lines) body.push_back(text(" " + l));
    if (d.hasInput) {
        if (!d.inputLabel.empty()) body.push_back(text(" " + d.inputLabel));
        body.push_back(hbox({text(" "), text(d.input + (d.focus == Dialog::Focus::input ? "_" : " ")) | kInput | flex, text(" ")}));
        for (const auto &c : d.completions) body.push_back(text("   " + c) | dim);
    }
    for (int i = 0; i < static_cast<int>(d.radio.size()); ++i) {
        Element el = text(fmt::format(" ({}) {}", i == d.radioAt ? '*' : ' ', d.radio[static_cast<size_t>(i)]));
        body.push_back(d.focus == Dialog::Focus::radio && i == d.radioAt ? el | kCursor : el);
    }
    for (int i = 0; i < static_cast<int>(d.checks.size()); ++i) {
        const auto &[label, on] = d.checks[static_cast<size_t>(i)];
        Element el = text(fmt::format(" [{}] {}", on ? 'x' : ' ', label));
        body.push_back(d.focus == Dialog::Focus::checks && i == d.checkAt ? el | kCursor : el);
    }
    for (int i = 0; i < static_cast<int>(d.items.size()); ++i) {
        Element el = text(" " + d.items[static_cast<size_t>(i)] + " ");
        body.push_back(i == d.itemAt ? el | kCursor : el);
    }
    if (!d.buttons.empty()) {
        Elements buttons;
        for (int i = 0; i < static_cast<int>(d.buttons.size()); ++i) {
            const auto &b = d.buttons[static_cast<size_t>(i)];
            Element el = text(i == 0 ? "[< " + b + " >]" : "[ " + b + " ]");
            if (d.focus == Dialog::Focus::buttons && i == d.buttonAt) el = el | kCursor;
            buttons.push_back(el);
            buttons.push_back(text(" "));
        }
        body.push_back(separator());
        body.push_back(hbox(buttons) | hcenter);
    }
    return window(text(" " + d.title + " ") | bold | hcenter, vbox(body) | size(WIDTH, GREATER_THAN, kDialogWidth)) | kDialog;
}

/* The menu pulled down: a box under its name on the bar. */
Element Tui::renderMenu() const
{
    const Menu &m = menus_[static_cast<size_t>(menu_)];
    int x = 2;
    for (int j = 0; j < menu_; ++j) x += static_cast<int>(menus_[static_cast<size_t>(j)].name.size()) + 6;
    Elements rows;
    for (int i = 0; i < static_cast<int>(m.items.size()); ++i) {
        const MenuItem &it = m.items[static_cast<size_t>(i)];
        if (it.label.empty()) { rows.push_back(separator()); continue; }
        Element el = text(fmt::format(" {:<22} {:>6} ", it.label, it.key));
        rows.push_back(i == menuItem_ ? el | kCursor : el);
    }
    Element box = vbox(rows) | border | kDialog;
    return vbox({filler() | size(HEIGHT, EQUAL, 1),
                 hbox({filler() | size(WIDTH, EQUAL, x), box, filler()}),
                 filler()});
}

bool Tui::onDialogKey(const Event &e)
{
    Dialog &d = *dialog_;
    const auto finish = [this](int button) {
        Dialog done = std::move(*dialog_);
        dialog_.reset();
        if (done.onDone) done.onDone(done, button);
    };
    const auto step = [](int &at, int n, int by) { if (n > 0) at = std::clamp(at + by, 0, n - 1); };
    if (e == Event::Escape) { finish(-1); return true; }
    if (e == Event::Return) { finish(d.focus == Dialog::Focus::buttons ? d.buttonAt : 0); return true; }
    if (e == Event::Tab) {
        if (d.completePaths && d.focus == Dialog::Focus::input) completeInput(); else d.focusNext(1);
        return true;
    }
    if (e == Event::TabReverse) { d.focusNext(-1); return true; }
    /* with no input line, the first letter of a button answers */
    if (!d.hasInput && e.is_character() && !e.character().empty()) {
        const int c = std::tolower(static_cast<unsigned char>(e.character()[0]));
        for (int i = 0; i < static_cast<int>(d.buttons.size()); ++i)
            if (std::tolower(static_cast<unsigned char>(d.buttons[static_cast<size_t>(i)][0])) == c) { finish(i); return true; }
    }
    switch (d.focus) {
    case Dialog::Focus::input:
        if (e == Event::Backspace) { if (!d.input.empty()) d.input.erase(lastUtf8Start(d.input)); d.completions.clear(); }
        else if (e == Event::ArrowDown) d.focusNext(1);
        else if (e == Event::ArrowUp) d.focusNext(-1);
        else if (e.is_character()) { d.input += e.character(); d.completions.clear(); }
        return true;
    case Dialog::Focus::radio:
        if (e == Event::ArrowUp) step(d.radioAt, static_cast<int>(d.radio.size()), -1);
        else if (e == Event::ArrowDown) step(d.radioAt, static_cast<int>(d.radio.size()), 1);
        return true;
    case Dialog::Focus::checks:
        if (e == Event::ArrowUp) step(d.checkAt, static_cast<int>(d.checks.size()), -1);
        else if (e == Event::ArrowDown) step(d.checkAt, static_cast<int>(d.checks.size()), 1);
        else if (e == Event::Character(" ")) d.checks[static_cast<size_t>(d.checkAt)].second = !d.checks[static_cast<size_t>(d.checkAt)].second;
        return true;
    case Dialog::Focus::items:
        if (e == Event::ArrowUp) step(d.itemAt, static_cast<int>(d.items.size()), -1);
        else if (e == Event::ArrowDown) step(d.itemAt, static_cast<int>(d.items.size()), 1);
        else if (e == Event::Home) d.itemAt = 0;
        else if (e == Event::End) d.itemAt = static_cast<int>(d.items.size()) - 1;
        return true;
    case Dialog::Focus::buttons:
        if (e == Event::ArrowLeft || e == Event::ArrowUp) step(d.buttonAt, static_cast<int>(d.buttons.size()), -1);
        else if (e == Event::ArrowRight || e == Event::ArrowDown) step(d.buttonAt, static_cast<int>(d.buttons.size()), 1);
        return true;
    }
    return true;
}

bool Tui::onMenuKey(const Event &e)
{
    const int n = static_cast<int>(menus_.size());
    const auto &items = menus_[static_cast<size_t>(menu_)].items;
    const auto skipSeparators = [&](int by) {
        int i = menuItem_;
        for (int tries = 0; tries < static_cast<int>(items.size()); ++tries) {
            i = ((i + by) % static_cast<int>(items.size()) + static_cast<int>(items.size())) % static_cast<int>(items.size());
            if (!items[static_cast<size_t>(i)].label.empty()) { menuItem_ = i; return; }
        }
    };
    /* Esc folds the menu back onto the bar, then leaves the bar; F9 leaves at once */
    if (e == Event::Escape) { if (menuDown_) menuDown_ = false; else menu_ = -1; return true; }
    if (e == Event::F9) { menu_ = -1; menuDown_ = false; return true; }
    if (e == Event::ArrowLeft)  { menu_ = (menu_ + n - 1) % n; menuItem_ = 0; return true; }
    if (e == Event::ArrowRight) { menu_ = (menu_ + 1) % n; menuItem_ = 0; return true; }
    /* on the bar, not pulled down yet: Enter or Down pulls the menu */
    if (!menuDown_) {
        if (e == Event::Return || e == Event::ArrowDown) menuDown_ = true;
        return true;
    }
    if (e == Event::ArrowUp)    { skipSeparators(-1); return true; }
    if (e == Event::ArrowDown)  { skipSeparators(1); return true; }
    if (e == Event::Home)       { menuItem_ = 0; return true; }
    if (e == Event::End)        { menuItem_ = static_cast<int>(items.size()) - 1; return true; }
    if (e == Event::Return) {
        auto action = items[static_cast<size_t>(menuItem_)].action;
        menu_ = -1;
        menuDown_ = false;
        if (action) action();
        return true;
    }
    return true;
}

void Tui::openMenu(int index)
{
    menu_ = index;
    menuDown_ = false;
    menuItem_ = 0;
}

void Tui::completeInput()
{
    Dialog &d = *dialog_;
    auto cands = completePath(d.input);
    std::sort(cands.begin(), cands.end());
    if (cands.empty()) { d.completions = {"(nothing matches)"}; return; }
    std::string common = cands[0];
    for (const auto &c : cands)
        common.resize(static_cast<size_t>(std::mismatch(common.begin(), common.end(), c.begin(), c.end()).first - common.begin()));
    if (common.size() > d.input.size()) d.input = common;
    d.completions = cands.size() == 1 ? std::vector<std::string>{} : cands;
    if (d.completions.size() > 12) { d.completions.resize(12); d.completions.push_back("..."); }
}

void Tui::message(const std::string &title, const std::vector<std::string> &lines)
{
    Dialog d;
    d.title = title;
    d.lines = lines;
    d.buttons = {"OK"};
    d.focus = Dialog::Focus::buttons;
    dialog_ = std::move(d);
}

void Tui::ask(const std::string &title, const std::vector<std::string> &lines, std::vector<std::string> buttons,
              std::function<void(int)> onDone)
{
    Dialog d;
    d.title = title;
    d.lines = lines;
    d.buttons = std::move(buttons);
    d.focus = Dialog::Focus::buttons;
    d.onDone = [onDone = std::move(onDone)](const Dialog &, int b) { onDone(b); };
    dialog_ = std::move(d);
}

void Tui::inputDialog(const std::string &title, const std::vector<std::string> &lines, const std::string &label,
                      const std::string &initial, bool completePaths, std::function<void(const std::string &)> onDone)
{
    Dialog d;
    d.title = title;
    d.lines = lines;
    d.hasInput = true;
    d.inputLabel = label;
    d.input = initial;
    d.completePaths = completePaths;
    d.buttons = {"OK", "Cancel"};
    d.focus = Dialog::Focus::input;
    d.onDone = [onDone = std::move(onDone)](const Dialog &dd, int b) { if (b == 0) onDone(dd.input); };
    dialog_ = std::move(d);
}

void Tui::pick(const std::string &title, std::vector<std::string> items, std::function<void(int)> onDone)
{
    Dialog d;
    d.title = title;
    d.items = std::move(items);
    d.focus = Dialog::Focus::items;
    d.onDone = [onDone = std::move(onDone)](const Dialog &dd, int b) { onDone(b < 0 ? -1 : dd.itemAt); };
    dialog_ = std::move(d);
}

} /* namespace ms0515::files::detail */
