/*
 * TuiImpl.hpp — the commander's insides, shared by its source files:
 * the state, the dialogs, the menus, the palette.  Internal to the ui
 * library; the public face is Commander.hpp.
 *
 * The screen is Midnight Commander's: the menu bar, two panels with the
 * title on the top border and the free space on the bottom one, a hint
 * line, the key bar.  The dialogs are mc's too - grey boxes with a title,
 * a cyan input line, radio and check items, "[< OK >] [ Cancel ]".
 */
#ifndef MS0515_FILES_TUI_IMPL_HPP
#define MS0515_FILES_TUI_IMPL_HPP

#include "Commander.hpp"
#include "Ops.hpp"
#include "Panel.hpp"
#include "Viewer.hpp"

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ms0515::files::detail {

using ftxui::Decorator;
using ftxui::Element;
using ftxui::Elements;
using ftxui::Event;

/* Rows of the page that are not the panels: the menu bar, the hint line,
 * the key bar. */
constexpr int kPageChrome = 3;
/* Rows of a panel that are not files: the top border, the column header,
 * the rule with the marks summary, the current-file line, the bottom
 * border. */
constexpr int kPanelChrome = 5;
/* How many of the guest's rows go under the panels when the host gives them. */
constexpr int kGuestRows = 2;

/* mc's default skin. */
inline const Decorator kPanel   = ftxui::bgcolor(ftxui::Color::Blue) | ftxui::color(ftxui::Color::White);
inline const Decorator kCursor  = ftxui::bgcolor(ftxui::Color::Cyan) | ftxui::color(ftxui::Color::Black);
inline const Decorator kMarked  = ftxui::color(ftxui::Color::Yellow) | ftxui::bold;
inline const Decorator kProgram = ftxui::color(ftxui::Color::GreenLight);   /* mc paints executables green */
inline const Decorator kHeader  = ftxui::color(ftxui::Color::Yellow);
inline const Decorator kBar     = ftxui::bgcolor(ftxui::Color::Cyan) | ftxui::color(ftxui::Color::Black);
inline const Decorator kBarOpen = ftxui::bgcolor(ftxui::Color::Black) | ftxui::color(ftxui::Color::White);
inline const Decorator kDialog  = ftxui::bgcolor(ftxui::Color::GrayLight) | ftxui::color(ftxui::Color::Black);
inline const Decorator kInput   = ftxui::bgcolor(ftxui::Color::Cyan) | ftxui::color(ftxui::Color::Black);
inline const Decorator kKeyNum  = ftxui::bgcolor(ftxui::Color::Black) | ftxui::color(ftxui::Color::White);

[[nodiscard]] std::string today();
[[nodiscard]] std::string utf8(const std::filesystem::path &p);
[[nodiscard]] std::string join(const std::vector<std::string> &v, const char *sep);
[[nodiscard]] std::string upperCase(std::string s);
[[nodiscard]] size_t lastUtf8Start(const std::string &s);

/* A dialog: a title, lines of text, then whichever of these it has - an
 * input line, radio items, check items, a list to pick from - and the
 * buttons.  Tab moves between the parts; Enter is the focused button or
 * the first one; Esc cancels; with no input line the first letter of a
 * button answers. */
struct Dialog {
    enum class Focus { input, radio, checks, items, buttons };

    std::string title;
    std::vector<std::string> lines;
    bool hasInput = false;
    std::string inputLabel;                 /* "to:" above the input */
    std::string input;
    bool completePaths = false;             /* Tab completes a host path in the input */
    std::vector<std::string> completions;
    std::vector<std::string> radio;
    int radioAt = 0;
    std::vector<std::pair<std::string, bool>> checks;
    int checkAt = 0;
    std::vector<std::string> items;
    int itemAt = 0;
    std::vector<std::string> buttons;       /* the first is the default, shown "< >" */
    int buttonAt = 0;
    Focus focus = Focus::buttons;
    std::function<void(const Dialog &d, int button)> onDone;   /* button < 0: cancelled */

    [[nodiscard]] std::vector<Focus> parts() const;
    void focusNext(int step);
};

struct MenuItem {
    std::string label;                      /* empty: a separator */
    std::string key;                        /* shown at the right: "F3", "C-u" */
    std::function<void()> action;
};

struct Menu {
    std::string name;
    std::vector<MenuItem> items;
};

struct ViewState {
    std::string name;
    std::vector<uint8_t> bytes;
    ViewOptions opts;
    std::vector<std::string> lines;
    int top = 0;
    std::string lastSearch;
};

/* A panel picking an image to mount: the host directory it lists. */
struct HostBrowse {
    std::filesystem::path dir;
    std::vector<HostEntry> items;
    int cursor = 0;
    int top = 0;
};

/* A copy or move in flight: the files still to go, and what the user
 * answered about the ones that exist on the target. */
struct Transfer {
    bool move = false;
    std::vector<Entry> queue;
    size_t at = 0;
    Location *from = nullptr;
    Location *to = nullptr;
    std::optional<Location> ownTo;          /* when no panel shows the target */
    Policy policy;
    bool overwriteAll = false;
    bool skipAll = false;
    bool overwriteNext = false;
    OpResult result;
};

class Tui {
public:
    Tui(Mounts mounts, app::Config &config, CommanderHooks hooks);

    Element render(Element guest);
    bool onEvent(const Event &e);
    [[nodiscard]] bool takeQuitRequest() noexcept { const bool q = quit_; quit_ = false; return q; }
    [[nodiscard]] bool modal() const noexcept { return dialog_ || view_ || menu_ >= 0 || browse_[0] || browse_[1]; }
    [[nodiscard]] const Mounts &mounts() const noexcept { return mounts_; }
    void setSize(int width, int height) noexcept { width_ = width; height_ = height; }
    void refreshPanels();

    /* --- state, open to the source files of the class --- */
    Mounts mounts_;
    app::Config &config_;
    CommanderHooks hooks_;
    int width_ = 80;
    int height_ = 25;
    int guestRows_ = 0;
    bool quit_ = false;
    Panel panels_[2];
    int active_ = 0;
    std::string status_;
    std::optional<Dialog> dialog_;
    std::optional<ViewState> view_;
    std::optional<HostBrowse> browse_[2];
    std::optional<Transfer> transfer_;
    std::vector<Menu> menus_;
    int menu_ = -1;                         /* the menu chosen on the bar, -1 none */
    bool menuDown_ = false;                 /* and pulled down (mc: F9 picks, Enter / Down pulls) */
    int menuItem_ = 0;
    std::filesystem::path lastDir_;         /* where the image picker starts */
    std::string newFileDate_;               /* the date host files get on a volume */
    bool showUnused_ = true;                /* the unused areas in the panels, mc's hidden files */
    Encoding viewEncoding_ = Encoding::koi8r;

    [[nodiscard]] Panel &panel() { return panels_[active_]; }
    [[nodiscard]] Panel &other() { return panels_[1 - active_]; }
    [[nodiscard]] int panelRows() const { return std::max(3, height_ - kPanelChrome - kPageChrome - guestRows_); }

    /* drawing - Commander.cpp */
    Element renderMenuBar() const;
    Element renderPanel(int index);
    Element renderHost(int index);
    Element frame(bool isActive, const std::string &title, Element columns, Element rule, Element info, const std::string &foot);
    Element renderKeyBar(const std::vector<std::pair<const char *, const char *>> &keys) const;
    Element panelKeyBar() const;

    /* keys - Commander.cpp */
    bool onBrowseKey(const Event &e);
    bool onHostKey(const Event &e);

    /* the disks - Commander.cpp */
    void openDevices(int panelIndex);
    void showDevice(const Device &device);
    void startHostBrowse(int panelIndex, const std::filesystem::path &dir);
    void mountPicked(int panelIndex, const std::filesystem::path &image);
    void doMount(Slot slot, const std::string &path, int side, bool force);
    void changedImage(const std::filesystem::path &image);
    void changed(Panel &p);

    /* dialogs and menus - CommanderDialogs.cpp */
    Element renderDialog() const;
    Element renderMenu() const;
    bool onDialogKey(const Event &e);
    bool onMenuKey(const Event &e);
    void completeInput();
    void message(const std::string &title, const std::vector<std::string> &lines);
    void ask(const std::string &title, const std::vector<std::string> &lines, std::vector<std::string> buttons,
             std::function<void(int)> onDone);
    void inputDialog(const std::string &title, const std::vector<std::string> &lines, const std::string &label,
                     const std::string &initial, bool completePaths, std::function<void(const std::string &)> onDone);
    void pick(const std::string &title, std::vector<std::string> items, std::function<void(int)> onDone);
    void openMenu(int index);

    /* the actions - CommanderActions.cpp */
    void buildMenus();
    void userMenu();
    void help();
    void doView();
    void doEnter();
    [[nodiscard]] static std::string runCommand(const std::string &device, const std::string &name);
    void doCopy(bool move);
    void startTransfer(bool move, const std::vector<Entry> &sel, const Device &target);
    void continueTransfer();
    void askExists(const Entry &entry, const Entry &existing);
    void finishTransfer();
    void doRename(const Entry &entry, const std::string &newName);
    void doSqueeze();
    void doDelete();
    void doInit();
    void doProtect(bool on);
    void doUndelete();
    void setShowUnused(bool on);
    void doSetDate();
    void doImport();
    void doExport();
    void doSelect(bool on);
    void doSort();
    void swapPanels();
    void protectedGuard(const std::vector<Entry> &entries, const char *verb, const std::function<void(Policy)> &go);
    void report(const OpResult &r, const char *verb);

    /* the viewer - CommanderViewer.cpp */
    Element renderViewer() const;
    bool onViewerKey(const Event &e);
    void viewerGoto();
    void viewerSearch();
};

} /* namespace ms0515::files::detail */

#endif /* MS0515_FILES_TUI_IMPL_HPP */
