/*
 * WizardTui.hpp - the native disk wizard: DiskWizard drawn with FTXUI.
 *
 * One screen: on the left one list walked in steps - the diskette, the
 * operating system on it, then the bundles in their groups, a tree folded
 * until opened; the details of the row under the cursor on the right; the
 * plan - how full each volume would be, the startup file - below; the keys
 * at the bottom.  render() and onEvent() are all a host needs, so the screen
 * is exercised by the tests as it is by the terminal.
 */

#ifndef MS0515_TOOLS_DISK_WIZARD_TUI_HPP
#define MS0515_TOOLS_DISK_WIZARD_TUI_HPP

#include <ms0515/disk/Compose.hpp>
#include <ms0515/disk/Manifest.hpp>
#include <ms0515/disk/Wizard.hpp>

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
    /* Where files go when F2 saves a choice or F5 builds a disk; the tests
     * point it at a scratch directory. */
    WizardTui(const disk::Manifest &manifest, const disk::Repository &repo,
              std::filesystem::path workDir = std::filesystem::current_path());

    [[nodiscard]] ftxui::Element render(int width, int height);
    /* false when the event is not the wizard's; F10 sets quit(). */
    bool onEvent(const ftxui::Event &event);
    [[nodiscard]] bool quit() const noexcept { return quit_; }

    /* A saved choice, as F3 opens one. */
    void open(const disk::SavedSelection &saved);

    [[nodiscard]] const disk::DiskWizard &model() const noexcept { return wizard_; }
    [[nodiscard]] const std::string &status() const noexcept { return status_; }

private:
    enum class Ask { none, save, open, build, startup, label, find };

    void changed();
    void moveCursor(int delta);
    [[nodiscard]] std::vector<disk::WizardRow> visibleRows() const;
    [[nodiscard]] int indexOf(const std::string &key, disk::WizardRow::Kind kind) const;
    /* The first row shown that `pred` takes; none: where the cursor is. */
    [[nodiscard]] int firstOf(const std::function<bool(const disk::WizardRow &)> &pred) const;
    void activate(const disk::WizardRow &row);
    void startAsk(Ask ask, std::string value);
    void finishAsk();
    bool onAskEvent(const ftxui::Event &event);
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
    std::vector<std::string> startupLines_;

    int                     cursor_ = 0;
    int                     top_ = 0;
    Ask                     ask_ = Ask::none;
    std::string             input_;
    std::string             status_;
    bool                    quit_ = false;
};

} /* namespace ms0515::tools */

#endif
