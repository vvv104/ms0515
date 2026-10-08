/*
 * test_main.cpp - doctest entry point with the harness options.
 *
 * Options of the form --stars-<name>=<value> are collected for the tests
 * (stars::options()) and removed before doctest parses the rest:
 * --stars-sav=<folder> is where STARS.SAV is.
 */
#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#include "Flight.hpp"

#include <cstring>
#include <vector>

int main(int argc, char **argv)
{
    std::vector<char *> rest;
    rest.push_back(argv[0]);
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (std::strncmp(a, "--stars-", 8) == 0) {
            const char *eq = std::strchr(a + 8, '=');
            if (eq)
                stars::options()[std::string(a + 8, eq)] = std::string(eq + 1);
            else
                stars::options()[std::string(a + 8)] = "1";
        } else {
            rest.push_back(argv[i]);
        }
    }
    doctest::Context ctx(static_cast<int>(rest.size()), rest.data());
    return ctx.run();
}
