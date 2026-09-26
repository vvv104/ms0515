/*
 * test_main.cpp - doctest entry point with the harness options.
 *
 * Options of the form --sab2tune-<name>=<value> are collected for the
 * tests (sab2tune::options()) and removed before doctest parses the rest.
 * Paths default to the repo layout.  See TuneRun.hpp for the names.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "TuneRun.hpp"

#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    std::vector<char *> rest;
    rest.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strncmp(a, "--sab2tune-", 11) == 0) {
            const char *eq = std::strchr(a + 11, '=');
            if (eq)
                sab2tune::options()[std::string(a + 11, eq)] = std::string(eq + 1);
            else
                sab2tune::options()[std::string(a + 11)] = "1";
        } else {
            rest.push_back(argv[i]);
        }
    }
    doctest::Context ctx(static_cast<int>(rest.size()), rest.data());
    return ctx.run();
}
