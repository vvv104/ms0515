/*
 * WizardTui.hpp - the native disk wizard: DiskWizard drawn with FTXUI.
 *
 * One screen: on the left one list walked in steps - the diskette, the
 * operating system on it, then the bundles in their groups, a tree folded
 * until opened - and at its end the buttons: save the choice, open one,
 * build the disk, quit, each with its window; under it, the width of the
 * screen, the details of the row under the cursor and the plan - how full
 * each volume would be, and how many blocks each group takes.  Nothing side
 * by side: the screen stays readable in a terminal of 120 columns or less.
 * render() and onEvent() are all a host needs, so the screen is exercised by
 * the tests as it is by the terminal.
 */

#ifndef MS0515_TOOLS_DISK_WIZARD_TUI_HPP
#define MS0515_TOOLS_DISK_WIZARD_TUI_HPP

#include <ms0515/disk/Compose.hpp>
#include <ms0515/disk/Manifest.hpp>
#include <ms0515/disk/Wizard.hpp>

#include "FileDialog.hpp"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ms0515::tools {

class WizardTui {
public:
    /* Where the file windows start; the tests point it at a scratch
     * directory. */
    WizardTui(const disk::Manifest &manifest, const disk::Repository &repo,
              std::filesystem::path workDir = std::filesystem::current_path());

    [[nodiscard]] ftxui::Element render(int width, int height);
    /* false when the event is not the wizard's; Quit sets quit(). */
    bool onEvent(const ftxui::Event &event);
    [[nodiscard]] bool quit() const noexcept { return quit_; }

    /* A saved choice, as Open reads one. */
    void open(const disk::SavedSelection &saved);

    [[nodiscard]] const disk::DiskWizard &model() const noexcept { return wizard_; }
    [[nodiscard]] const std::string &status() const noexcept { return status_; }
    /* The key of the row under the cursor; a button's: #save, #open, #build, #quit. */
    [[nodiscard]] std::string cursorKey() const;

private:
    enum class Ask { none, find };
    enum class Button { save, open, build, quit };

    void changed();
    void moveCursor(int delta);
    [[nodiscard]] std::vector<disk::WizardRow> visibleRows() const;
    [[nodiscard]] int indexOf(const std::string &key, disk::WizardRow::Kind kind) const;

    void activate(const disk::WizardRow &row);
    void startAsk(Ask ask, std::string value);
    void pressButton(Button button);
    bool onWindowEvent(const ftxui::Event &event);
    void finishFile(const std::filesystem::path &path);
    [[nodiscard]] ftxui::Element rowLine(const disk::WizardRow &row, bool here) const;
    [[nodiscard]] ftxui::Element renderWindow(int width, int height) const;
    void finishAsk();
    bool onAskEvent(const ftxui::Event &event);
    void startEdit(const disk::WizardRow &row, std::string text);
    void advance(const std::string &key, disk::WizardRow::Kind kind);
    bool onEditEvent(const ftxui::Event &event);
    bool onListEvent(const ftxui::Event &event);
    void findNext(const std::string &text);
    void save(const std::filesystem::path &path);
    void openFile(const std::filesystem::path &path);
    void build(const std::filesystem::path &path);
    [[nodiscard]] std::string defaultName(const char *ext) const;

    [[nodiscard]] ftxui::Element renderTop() const;
    [[nodiscard]] ftxui::Element renderList(int rows);
    [[nodiscard]] ftxui::Element renderDetails() const;
    [[nodiscard]] ftxui::Element renderPlan(int width) const;
    [[nodiscard]] ftxui::Element renderBottom() const;

    const disk::Manifest   &manifest_;
    const disk::Repository &repo_;
    std::filesystem::path   workDir_;
    std::map<std::string, int> blocks_;       /* bundle key -> blocks, measured once */
    disk::DiskWizard        wizard_;
    std::optional<disk::ComposePlan> plan_;
    std::string             planProblem_;

    std::optional<FileDialog> files_;             /* the window of save, open or build */
    Button                  filesFor_ = Button::save;
    std::string             message_;             /* a window saying what went wrong */
    bool                    quitting_ = false;    /* "leave the composer?" asked */

    int                     cursor_ = 0;          /* a row; past the rows, a button */
    int                     top_ = 0;
    Ask                     ask_ = Ask::none;
    std::string             input_;
    bool                    editing_ = false;    /* a field's text being typed */
    bool                    editHadText_ = false;   /* the field held text when the edit began */
    std::string             editKey_;
    std::string             edit_;
    std::string             status_;
    bool                    quit_ = false;
};

} /* namespace ms0515::tools */

#endif
