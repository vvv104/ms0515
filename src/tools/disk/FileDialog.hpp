/*
 * FileDialog.hpp - the wizard's window for a file to open or to write: the
 * directory's subdirectories and its files of one extension to pick from
 * with the cursor, and a name to type.
 *
 * Enter on a directory goes into it; Enter otherwise takes the name - a
 * file to open must be there, one to write that is there is asked about
 * first.  Esc gives up.  No drawing of the wizard's own here: render() is
 * the window alone, laid over the screen by its host.
 */

#ifndef MS0515_TOOLS_DISK_FILE_DIALOG_HPP
#define MS0515_TOOLS_DISK_FILE_DIALOG_HPP

#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace ms0515::tools {

class FileDialog {
public:
    enum class Mode { open, write };
    enum class Result { none, cancelled, accepted };

    /* `extension` with its dot (".toml"): the files listed, and what a name
     * typed without one gets. */
    FileDialog(std::string title, Mode mode, std::filesystem::path dir, std::string extension, std::string name = {});

    Result onEvent(const ftxui::Event &event);
    [[nodiscard]] ftxui::Element render(int width, int height) const;

    /* Once accepted: the file. */
    [[nodiscard]] std::filesystem::path path() const { return dir_ / name_; }

    [[nodiscard]] const std::filesystem::path &dir() const noexcept { return dir_; }
    [[nodiscard]] const std::string &name() const noexcept { return name_; }
    /* What the list shows: "..", "name/" for a directory, then the files. */
    [[nodiscard]] const std::vector<std::string> &entries() const noexcept { return entries_; }
    [[nodiscard]] int selected() const noexcept { return selected_; }
    [[nodiscard]] const std::string &problem() const noexcept { return problem_; }
    [[nodiscard]] bool asking() const noexcept { return replacing_; }

private:
    void list();
    void select(int index);
    Result accept();

    std::string              title_;
    Mode                     mode_;
    std::filesystem::path    dir_;
    std::string              extension_;
    std::string              name_;
    std::vector<std::string> entries_;
    int                      selected_ = -1;       /* -1: the name, nothing picked */
    std::string              problem_;
    bool                     replacing_ = false;   /* "replace it?" asked */
};

/* A window laid over the screen, with a blank margin so that its border does
 * not join the lines of the panels under it; centred. */
[[nodiscard]] ftxui::Element framed(ftxui::Element window);

} /* namespace ms0515::tools */

#endif
