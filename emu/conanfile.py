"""
Conan 2 recipe for the MS0515 emulator project.

Build with: conan build .
(runs conan install + cmake configure + cmake build in one step)

ImGui backend sources (SDL2 + SDLRenderer2) are compiled directly from
the Conan package cache — no copying into the source or build tree.
"""

from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout


class Ms0515Recipe(ConanFile):
    name        = "ms0515"
    version     = "0.1.0"
    settings    = "os", "arch", "compiler", "build_type"
    generators  = "CMakeDeps", "CMakeToolchain"

    options = {
        "trace": [True, False],
    }
    default_options = {
        "sdl/*:shared": False,
        "trace": False,
    }

    requires = (
        "sdl/2.30.7",
        "imgui/1.91.5",
        "doctest/2.4.11",
    )

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        variables = {"MS0515_BUILD_TESTS": "ON"}
        if self.options.trace:
            variables["MS0515_TRACE"] = "ON"
        cmake.configure(variables=variables)
        cmake.build()
