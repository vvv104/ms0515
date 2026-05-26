/*
 * test_platform.cpp — Smoke tests for the frontend's Platform layer.
 *
 * The real work attachConsoleForOutput() does on Windows (AttachConsole
 * + stdout rebind) only has user-visible effects when run from a GUI-
 * subsystem binary, and the test runner here is a console-subsystem
 * binary — so most of the production code path is unexercised.  What
 * we CAN check is that the call doesn't crash, stays idempotent, and
 * leaves stdout writable.
 */

#include <doctest/doctest.h>

#include "../src/Platform.hpp"

#include <cstdio>

TEST_CASE("attachConsoleForOutput doesn't crash") {
    ms0515_frontend::attachConsoleForOutput();
    CHECK(true);
}

TEST_CASE("attachConsoleForOutput is idempotent") {
    /* Second call from the same process is documented to be a no-op
     * thanks to a static guard inside the implementation — double
     * binding would leak file descriptors.  We can't observe the
     * guard directly, but if the call paths still cooperate stdout
     * has to remain writable. */
    ms0515_frontend::attachConsoleForOutput();
    ms0515_frontend::attachConsoleForOutput();
    ms0515_frontend::attachConsoleForOutput();
    CHECK(std::fputc('\0', stdout) != EOF);
    std::fflush(stdout);
}
