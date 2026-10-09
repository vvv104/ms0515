/*
 * test_main.cpp - doctest entry point with the harness options.
 *
 * Options of the form --gcc-<name>=<value> are collected for the tests
 * (gcc_tests::options()) and removed before doctest parses the rest:
 * --gcc-sav=<folder> is where the example programs' .SAVs are.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "Programs.hpp"

#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    std::vector<char *> rest;
    rest.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strncmp(a, "--gcc-", 6) == 0) {
            const char *eq = std::strchr(a + 6, '=');
            if (eq)
                gcc_tests::options()[std::string(a + 6, eq)] = std::string(eq + 1);
            else
                gcc_tests::options()[std::string(a + 6)] = "1";
        } else {
            rest.push_back(argv[i]);
        }
    }
    doctest::Context ctx(static_cast<int>(rest.size()), rest.data());
    return ctx.run();
}
