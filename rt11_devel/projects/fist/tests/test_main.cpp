/*
 * test_main.cpp - doctest entry point with the harness options.
 *
 * Options of the form --fist-<name>=<value> are collected for the tests
 * (fist::opt) and removed before doctest parses the rest.  Paths default to
 * the repo layout; the diagnostics are off unless their option is given.
 * See FistGame.hpp for the names.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "FistGame.hpp"

#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    std::vector<char *> rest;
    rest.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strncmp(a, "--fist-", 7) == 0) {
            const char *eq = std::strchr(a + 7, '=');
            if (eq)
                fist::options()[std::string(a + 7, eq)] = std::string(eq + 1);
            else
                fist::options()[std::string(a + 7)] = "1";
        } else {
            rest.push_back(argv[i]);
        }
    }
    doctest::Context ctx(static_cast<int>(rest.size()), rest.data());
    return ctx.run();
}
