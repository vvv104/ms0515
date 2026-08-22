# RT-11 development tree: the harnesses of the projects under projects/ that
# test a built program through the emulator library.  Pulled in by the
# emulator's top-level CMake when tests are enabled; each project owns its
# own tests/ directory and skips itself when its artifacts are not built.
add_subdirectory(projects/fist/tests)
