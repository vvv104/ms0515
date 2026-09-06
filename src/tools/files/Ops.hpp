/*
 * Ops.hpp — the operations: copy and move between two volumes, delete,
 * rename, and the two ends of the host - F1 brings host files onto a
 * volume, F2 puts volume files into a host directory.
 *
 * Bytes travel as they are - no encoding conversion, that is the viewer's
 * business - and a file keeps its date and [P] flag from volume to
 * volume.  A host file gets an RT-11 name (Location::toVolumeName) and
 * the date the policy carries; a volume file keeps its name on the host.
 * Nothing here asks questions: the front-end decides about overwriting
 * and protected files beforehand and passes the answer in `Policy`.
 */
#ifndef MS0515_FILES_OPS_HPP
#define MS0515_FILES_OPS_HPP

#include "Location.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ms0515::files {

struct Policy {
    bool overwrite = false;          /* replace a file of the same name */
    bool touchProtected = false;     /* delete / move / overwrite a [P] file */
    std::string date;                /* the date a host file gets on a volume, "YYYY-MM-DD"; "" = none */
};

struct OpResult {
    int done = 0;                    /* files carried through */
    std::vector<std::string> errors; /* "NAME: why", one per file that failed */
    [[nodiscard]] bool ok() const noexcept { return errors.empty(); }
};

/* Volume to volume. */
OpResult copyFiles(const Location &from, const std::vector<Entry> &entries, Location &to, const Policy &policy);
/* Copy, then delete the originals that copied. */
OpResult moveFiles(Location &from, const std::vector<Entry> &entries, Location &to, const Policy &policy);
OpResult deleteFiles(Location &where, const std::vector<Entry> &entries, const Policy &policy);
/* Rename in place; `newName` is checked as an RT-11 name first. */
OpResult renameFile(Location &where, const Entry &entry, const std::string &newName, const Policy &policy);
OpResult protectFiles(Location &where, const std::vector<Entry> &entries, bool on);

/* F1: host files onto the volume, named by Location::toVolumeName. */
OpResult importFiles(const std::vector<std::filesystem::path> &hostFiles, Location &to, const Policy &policy);
/* F2: volume files into a host directory (created when missing), under
 * their own names. */
OpResult exportFiles(const Location &from, const std::vector<Entry> &entries,
                     const std::filesystem::path &hostDir, const Policy &policy);

/* Before an operation: which of `entries` already exist in `to` (the
 * overwrite question), and which are protected (the [P] question). */
[[nodiscard]] std::vector<std::string> clashes(const std::vector<Entry> &entries, const Location &to);
[[nodiscard]] std::vector<std::string> protectedOnes(const std::vector<Entry> &entries);

} /* namespace ms0515::files */

#endif /* MS0515_FILES_OPS_HPP */
