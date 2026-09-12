/*
 * test_main.cpp - doctest entry point with the harness options.
 *
 * Options of the form --manicm-<name>=<value> are collected for the tests
 * (manicm::options()) and removed before doctest parses the rest.  Paths
 * default to the repo layout.  See ManicmGame.hpp for the names.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "ManicmGame.hpp"

#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    std::vector<char *> rest;
    rest.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strncmp(a, "--manicm-", 9) == 0) {
            const char *eq = std::strchr(a + 9, '=');
            if (eq)
                manicm::options()[std::string(a + 9, eq)] = std::string(eq + 1);
            else
                manicm::options()[std::string(a + 9)] = "1";
        } else {
            rest.push_back(argv[i]);
        }
    }
    doctest::Context ctx(static_cast<int>(rest.size()), rest.data());
    return ctx.run();
}
