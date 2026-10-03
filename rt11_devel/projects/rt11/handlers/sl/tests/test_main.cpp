/*
 * test_main.cpp - doctest entry point with the harness options.
 *
 * Options of the form --sl-<name>=<value> are collected for the tests
 * (sl::options()) and removed before doctest parses the rest: --sl-sys=
 * names the SL.SYS under test, --sl-disk= the system diskette, --sl-rom=
 * the ROM.  See SlMachine.hpp.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "SlMachine.hpp"

#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    std::vector<char *> rest;
    rest.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strncmp(a, "--sl-", 5) == 0) {
            const char *eq = std::strchr(a + 5, '=');
            if (eq)
                sl::options()[std::string(a + 5, eq)] = std::string(eq + 1);
            else
                sl::options()[std::string(a + 5)] = "1";
        } else {
            rest.push_back(argv[i]);
        }
    }
    doctest::Context ctx(static_cast<int>(rest.size()), rest.data());
    return ctx.run();
}
