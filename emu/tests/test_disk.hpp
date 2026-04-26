/*
 * test_disk.hpp — Shared test helper: TempDisk.
 *
 * Wraps a disk-image path in an RAII object that:
 *   1. copies the source fixture to a unique file under
 *      TESTS_BUILD_DIR/temp/ on construction,
 *   2. exposes the copy's path via .path(),
 *   3. removes the copy on destruction.
 *
 * Tests must NEVER mount their fixture disks (emu/tests/disks/*.dsk)
 * directly — some Soviet OSes flush dirty buffer pages back on
 * close and can corrupt the image (see KNOWN_ISSUES.md, "type
 * STARTS.COM disk-corruption").  The fixtures stay pristine; each
 * test gets a writeable copy and lets the OS do whatever it likes
 * to it.
 *
 * Field-order note for callers: when TempDisk lives alongside an
 * Emulator in the same struct or stack frame, declare TempDisk
 * FIRST so it is destroyed LAST.  Otherwise the temp file is still
 * open inside the emulator when fs::remove() runs, the unlink
 * silently fails (Windows), and stale copies pile up under
 * build/tests/temp/.
 */
#ifndef MS0515_TESTS_TEST_DISK_HPP
#define MS0515_TESTS_TEST_DISK_HPP

#include <filesystem>
#include <random>
#include <string>
#include <system_error>

#ifndef TESTS_BUILD_DIR
#error "TESTS_BUILD_DIR must be defined by the build system"
#endif

namespace ms0515_test {

class TempDisk {
    std::filesystem::path path_;
public:
    explicit TempDisk(const std::filesystem::path &source)
    {
        namespace fs = std::filesystem;
        std::random_device rd;
        const auto stem = source.stem().string();
        const auto ext  = source.extension().string();
        std::error_code ec;
        fs::create_directories(TESTS_BUILD_DIR "/temp", ec);
        path_ = fs::path{TESTS_BUILD_DIR "/temp"}
              / fs::path{"ms0515_test_" + std::to_string(rd())
                         + "_" + stem + ext};
        fs::copy_file(source, path_, fs::copy_options::overwrite_existing);
    }
    ~TempDisk()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }
    TempDisk(const TempDisk &)            = delete;
    TempDisk &operator=(const TempDisk &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const noexcept
    {
        return path_;
    }
};

}  /* namespace ms0515_test */

#endif  /* MS0515_TESTS_TEST_DISK_HPP */
