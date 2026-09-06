/*
 * scratch.hpp — a scratch directory under the build tree for one test:
 * fixture disks are copied in (the originals under lib/tests/disks stay
 * pristine), host files are written in, and the whole directory goes away
 * with the object.
 */
#ifndef MS0515_FILES_TESTS_SCRATCH_HPP
#define MS0515_FILES_TESTS_SCRATCH_HPP

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class Scratch {
public:
    explicit Scratch(const std::string &tag)
    {
        static std::atomic<int> counter{0};
        dir_ = fs::path(TESTS_BUILD_DIR) / "scratch" / (tag + "-" + std::to_string(++counter));
        fs::remove_all(dir_);
        fs::create_directories(dir_);
    }
    ~Scratch() { std::error_code ec; fs::remove_all(dir_, ec); }
    Scratch(const Scratch &) = delete;
    Scratch &operator=(const Scratch &) = delete;

    [[nodiscard]] const fs::path &dir() const noexcept { return dir_; }

    /* A copy of a fixture disk, under `name` (default: the fixture's name). */
    fs::path disk(const std::string &fixture, const std::string &name = "")
    {
        const fs::path dst = dir_ / (name.empty() ? fixture : name);
        fs::copy_file(fs::path(FIXTURE_DISKS_DIR) / fixture, dst, fs::copy_options::overwrite_existing);
        return dst;
    }

    /* A host file with the given bytes. */
    fs::path file(const std::string &name, const std::vector<uint8_t> &bytes)
    {
        const fs::path p = dir_ / name;
        std::ofstream out(p, std::ios::binary);
        out.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return p;
    }
    fs::path text(const std::string &name, const std::string &s)
    {
        return file(name, std::vector<uint8_t>(s.begin(), s.end()));
    }
    fs::path subdir(const std::string &name)
    {
        const fs::path p = dir_ / name;
        fs::create_directories(p);
        return p;
    }

private:
    fs::path dir_;
};

#endif /* MS0515_FILES_TESTS_SCRATCH_HPP */
