#pragma once
/*
 * Status — the outcome of an operation that either succeeds or fails with a
 * human-readable reason.
 *
 * This is `std::expected<void, std::string>` written out by hand.  The
 * project targets C++20 so that it builds with the system compiler on older
 * macOS (Apple clang there ships a libc++ without <expected> and <format>),
 * and snapshot save/load was the only place that needed the C++23 type.
 * The call-site surface is unchanged:
 *
 *     if (auto r = emu.saveState(path); !r)
 *         report(r.error());
 */

#include <optional>
#include <string>
#include <utility>

namespace ms0515 {

class Status {
public:
    /* Success. */
    Status() = default;

    /* Failure, carrying the reason to show the user. */
    explicit Status(std::string error) : error_(std::move(error)) {}

    [[nodiscard]] bool has_value() const noexcept { return !error_.has_value(); }

    explicit operator bool() const noexcept { return has_value(); }

    /* The reason for the failure; only meaningful when !has_value(). */
    [[nodiscard]] const std::string &error() const noexcept { return *error_; }

private:
    std::optional<std::string> error_;
};

}  // namespace ms0515
