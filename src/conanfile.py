"""
Conan 2 recipe for the MS0515 emulator project.

Build with: conan build .
(runs conan install + cmake configure + cmake build in one step)

ImGui backend sources (SDL2 + SDLRenderer2) are compiled directly from
the Conan package cache — no copying into the source or build tree.
"""

import os

from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout


class Ms0515Recipe(ConanFile):
    name        = "ms0515"
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

    def requirements(self):
        # The browser build (os=Emscripten, profiles/emscripten) compiles the
        # core and the lib only: no SDL / ImGui, no host tests.
        if self.settings.os == "Emscripten":
            return
        self.requires("sdl/2.30.7")
        self.requires("imgui/1.91.5")
        self.requires("doctest/2.4.11")
        self.requires("stb/cci.20240213")

    def set_version(self):
        # Single source of truth for the version: src/VERSION (also read by
        # CMakeLists.txt).  Keeps the Conan recipe and the CMake project in
        # lock-step without two literals to bump.
        with open(os.path.join(self.recipe_folder, "VERSION")) as f:
            self.version = f.read().strip()

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        web = self.settings.os == "Emscripten"
        variables = {"MS0515_BUILD_TESTS":    "OFF" if web else "ON",
                     "MS0515_BUILD_FRONTEND": "OFF" if web else "ON"}
        cmake.configure(variables=variables)
        cmake.build()
