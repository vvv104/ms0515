/*
 * Location.hpp — a panel's place: the RT-11 volume of one mounted device.
 *
 * ms0515-files shows the machine's disks, not the host's: DZ0: and DZ2:
 * (the two sides of drive A), DZ1: and DZ3: (drive B), HD0:, or a DV0: /
 * MZ0: whole-diskette volume - the same devices the web commander lists,
 * named as the guest names them.  Which image is behind a device is the
 * mounts' business (Mounts.hpp); a Location just opens the volume.
 *
 * The image is read whole into memory on open and written back after
 * every change through ms0515_disk (Image / Build), so the file on the
 * host is always consistent.  Every operation reports failure as a
 * message, never by throwing - the panel shows the message.  The host
 * file system enters only through read()/write() of a whole file, which
 * is how F1/F2 (from / to the host) move bytes.
 */
#ifndef MS0515_FILES_LOCATION_HPP
#define MS0515_FILES_LOCATION_HPP

#include "ms0515/disk/Build.hpp"
#include "ms0515/disk/Image.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ms0515::files {

/* One row of a panel. */
/* A directory entry: a file, or an unused area.  An area keeps the name
 * of the file that was deleted from it, as RT-11's DELETE leaves it - the
 * candidate for undelete(); the free space INIT left has none. */
struct Entry {
    std::string name;                  /* "DIR.SAV"; an area's: the deleted file's, or "" */
    int         blocks = 0;
    uint64_t    bytes = 0;             /* blocks * 512 */
    std::string date;                  /* "1990-12-27", or "" */
    bool        protectedFlag = false; /* the RT-11 [P] flag */
    int         offset = 0;            /* the first block, as DIR/FULL shows it */
    bool        empty = false;         /* an unused area, not a file */
    int         ordinal = 0;           /* the entry's place in the directory */

    bool operator==(const Entry &) const = default;
};

/* The metadata a written file carries. */
struct FileMeta {
    std::string date;                  /* "YYYY-MM-DD", or "" for none */
    bool protectedFlag = false;
};

/* A device: its guest name and the volume behind it. */
struct Device {
    std::string           name;        /* "DZ0:", "HD0:", "DV0:" */
    std::filesystem::path image;       /* the image file */
    disk::VolumeSpec      spec;        /* how the volume is addressed in it */
    [[nodiscard]] std::string label() const;   /* "DZ0: osa.dsk" */
};

class Location {
public:
    /* Open the device's volume.  nullopt when the image cannot be read at
     * all; an image without an RT-11 directory opens as an empty volume
     * that only init() can write. */
    static std::optional<Location> open(const Device &device);

    [[nodiscard]] const Device &device() const noexcept { return device_; }
    [[nodiscard]] bool hasDirectory() const noexcept;
    /* The panel's header: "DZ0: osa.dsk". */
    [[nodiscard]] std::string title() const;
    /* The status line: "N files, M blocks free" or "no RT-11 directory - F9 initialises". */
    [[nodiscard]] std::string summary() const;
    /* The volume's ID and owner from the home block, "" when none. */
    [[nodiscard]] std::string volumeId() const;

    /* The listing, fresh from the image, in the directory's order (by
     * offset): the files and the unused areas.  find() knows the files. */
    [[nodiscard]] std::vector<Entry> list() const;
    [[nodiscard]] std::optional<Entry> find(const std::string &name) const;
    /* The blocks of an entry - a file's, or an unused area's, for a look
     * at what lies there.  nullopt when the entry is off the volume. */
    [[nodiscard]] std::optional<std::vector<uint8_t>> readArea(const Entry &entry) const;
    /* An unused area back as a file: under the deleted file's name it
     * still carries, or `newName` - which also makes a nameless area a
     * file, a recovery of whatever lies in it.  "" or why not. */
    std::string undelete(const Entry &area, const std::string &newName);

    /* File access; names are RT-11 names, upper-case.  Errors come back as
     * a non-empty message. */
    [[nodiscard]] std::optional<std::vector<uint8_t>> read(const std::string &name) const;
    std::string write(const std::string &name, std::span<const uint8_t> data, const FileMeta &meta);
    std::string remove(const std::string &name);
    std::string rename(const std::string &name, const std::string &newName);
    std::string setProtected(const std::string &name, bool on);
    std::string setDate(const std::string &name, const std::string &date);
    std::string squeeze();
    std::string init(const disk::InitOptions &opts);

    /* Re-read the image (another program, or the emulator, may have
     * written it). */
    [[nodiscard]] bool reload();

    /* "FOO.SAV" -> true: what RT-11 holds as a name (6.3, RAD50 chars). */
    [[nodiscard]] static bool validName(const std::string &name);
    /* A host name reduced to what a volume holds: "my-file.txt" -> "MYFILE.TXT". */
    [[nodiscard]] static std::string toVolumeName(const std::string &hostName);
    /* "1990-12-27" <-> the directory date word; 0 for "" or a bad string. */
    [[nodiscard]] static uint16_t dateWord(const std::string &date);
    [[nodiscard]] static std::string dateText(uint16_t word);

private:
    Device                     device_;
    std::optional<disk::Image> image_;

    [[nodiscard]] std::string save() const;
};

/* The bytes of a host file / a host file written whole - the two ends of
 * F1 (from the host) and F2 (to the host).  Errors as messages. */
[[nodiscard]] std::optional<std::vector<uint8_t>> readHostFile(const std::filesystem::path &path);
[[nodiscard]] std::string writeHostFile(const std::filesystem::path &path, std::span<const uint8_t> data);

} /* namespace ms0515::files */

#endif /* MS0515_FILES_LOCATION_HPP */
