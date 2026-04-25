# ── ms0515_frontend ──────────────────────────────────────────────────────────
# SDL2 + Dear ImGui application.
# ─────────────────────────────────────────────────────────────────────────────

find_package(SDL2 REQUIRED)
find_package(imgui REQUIRED)
find_package(stb REQUIRED)

# ImGui backend sources live in the Conan package cache under res/bindings/.
# The variable imgui_PACKAGE_FOLDER_RELEASE is set by Conan's CMakeDeps.
set(IMGUI_BACKENDS_DIR "${imgui_PACKAGE_FOLDER_RELEASE}/res/bindings")

# Platform-specific source file.
if(WIN32)
    set(PLATFORM_SRC src/Platform_win32.cpp)
else()
    set(PLATFORM_SRC src/Platform_unix.cpp)
endif()

# Windows version resource: embeds VERSIONINFO (Publisher, FileVersion,
# ProductVersion, etc.) so the SmartScreen dialog and Explorer's
# Properties → Details pane show real metadata instead of
# "Unknown publisher".  Version comes from the top-level project() call.
if(WIN32)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
        @ONLY
    )
    set(VERSION_RC "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
endif()

add_executable(ms0515 WIN32
    src/main.cpp
    src/Audio.cpp
    src/Video.cpp
    src/Keymap.cpp
    src/PhysicalKeyboard.cpp
    src/OnScreenKeyboard.cpp
    ${PLATFORM_SRC}
    ${IMGUI_BACKENDS_DIR}/imgui_impl_sdl2.cpp
    ${IMGUI_BACKENDS_DIR}/imgui_impl_sdlrenderer2.cpp
    ${VERSION_RC}
)

target_include_directories(ms0515 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${IMGUI_BACKENDS_DIR}
)

target_link_libraries(ms0515
    PRIVATE ms0515_lib
    PRIVATE SDL2::SDL2 SDL2::SDL2main
    PRIVATE imgui::imgui
    PRIVATE stb::stb
)

# ── Package output ──────────────────────────────────────────────────────────
# After build, copy exe + assets into a self-contained directory.
set(PACKAGE_DIR "${CMAKE_SOURCE_DIR}/package")

add_custom_command(TARGET ms0515 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/assets/rom"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/assets/keyboard"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DIR}/assets/disks"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:ms0515>" "${PACKAGE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/rom" "${PACKAGE_DIR}/assets/rom"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/keyboard" "${PACKAGE_DIR}/assets/keyboard"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/disks" "${PACKAGE_DIR}/assets/disks"
    COMMENT "Packaging ms0515 into ${PACKAGE_DIR}"
)
