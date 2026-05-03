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
    version     = "0.4.0"
    settings    = "os", "arch", "compiler", "build_type"
    generators  = "CMakeDeps", "CMakeToolchain"

    default_options = {
        "sdl/*:shared": False,
        # Linux SDL2 backends: keep only ALSA for sound.  Disabling pulse
        # removes pulseaudio + openssl + libxml2 + libcap + a dozen audio
        # codec deps (flac/opus/mpg123/…) that the emulator never uses.
        # Disabling libunwind saves another chunky transitive build.
        "sdl/*:pulse":     False,
        "sdl/*:libunwind": False,
    }

    requires = (
        "sdl/2.30.7",
        "imgui/1.91.5",
        "doctest/2.4.11",
        "stb/cci.20240213",
    )

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        variables = {"MS0515_BUILD_TESTS": "ON"}
        cmake.configure(variables=variables)
        cmake.build()
