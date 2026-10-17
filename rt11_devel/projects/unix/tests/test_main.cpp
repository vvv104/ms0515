/*
 * test_main.cpp - doctest entry point with the harness options.
 *
 * Options of the form --unix-<name>=<value> are collected for the tests
 * (unixos::options()) and removed before doctest parses the rest:
 * --unix-dsk=<file> is the diskette image to boot.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "Machine.hpp"

#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    std::vector<char *> rest;
    rest.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strncmp(a, "--unix-", 7) == 0) {
            const char *eq = std::strchr(a + 7, '=');
            if (eq)
                unixos::options()[std::string(a + 7, eq)] = std::string(eq + 1);
            else
                unixos::options()[std::string(a + 7)] = "1";
        } else {
            rest.push_back(argv[i]);
        }
    }
    doctest::Context ctx(static_cast<int>(rest.size()), rest.data());
    return ctx.run();
}
