# ── ms0515_web ───────────────────────────────────────────────────────────────
# The browser front-end: the lib layer compiled with Emscripten behind a flat
# C API (src/ms0515_web.cpp), the page that drives it (www/), and the assets
# it ships with (ROMs, the demo disks) - assembled into dist/, a static
# folder any web host (GitHub Pages) or a local `python -m http.server`
# serves as is.  Only configured when the Emscripten toolchain is active.
# ─────────────────────────────────────────────────────────────────────────────

add_executable(ms0515_web src/ms0515_web.cpp)
target_link_libraries(ms0515_web PRIVATE ms0515_lib ms0515_disk)
target_compile_features(ms0515_web PRIVATE cxx_std_20)

set(WEB_DIST "${CMAKE_BINARY_DIR}/web/dist")

set_target_properties(ms0515_web PROPERTIES
    OUTPUT_NAME       ms0515
    SUFFIX            ".js"
    RUNTIME_OUTPUT_DIRECTORY "${WEB_DIST}"
)

# A modularised module usable from a page, a worker and Node (the smoke
# test); the in-memory file system carries the ROM and the disk images.
target_link_options(ms0515_web PRIVATE
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createMs0515
    -sENVIRONMENT=web,worker,node
    -sALLOW_MEMORY_GROWTH=1
    -sINITIAL_MEMORY=67108864
    -sSTACK_SIZE=1048576
    -sFILESYSTEM=1
    -sFORCE_FILESYSTEM=1
    "-sEXPORTED_FUNCTIONS=_malloc,_free"
    "-sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap,HEAP8,HEAPU8,HEAP16,HEAPU16,HEAP32,HEAPU32,UTF8ToString,stringToUTF8,lengthBytesUTF8"
)

# The page and the assets next to the module.
set(WEB_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
set(WEB_DISKS osa.dsk omega-games.dsk omega-lang.dsk mihin.dsk rodionov.dsk vvv.dsk)
set(WEB_COPIES)
# index.html and app.js carry a build stamp in their script URLs (cache
# busting); the rest is copied as is.
foreach(f index.html app.js)
    list(APPEND WEB_COPIES "${WEB_DIST}/${f}")
    add_custom_command(OUTPUT "${WEB_DIST}/${f}"
        COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/www/${f} -DOUT=${WEB_DIST}/${f}
                -P "${CMAKE_CURRENT_SOURCE_DIR}/stamp.cmake"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/www/${f}" "${CMAKE_CURRENT_SOURCE_DIR}/stamp.cmake")
endforeach()
foreach(f keys.js joystick.js softkeys.js fm.js edit.js zip.js audio-worklet.js)
    list(APPEND WEB_COPIES "${WEB_DIST}/${f}")
    add_custom_command(OUTPUT "${WEB_DIST}/${f}"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/www/${f}" "${WEB_DIST}/${f}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/www/${f}")
endforeach()
foreach(f ms0515-roma.rom ms0515-romb.rom)
    list(APPEND WEB_COPIES "${WEB_DIST}/rom/${f}")
    add_custom_command(OUTPUT "${WEB_DIST}/rom/${f}"
        COMMAND ${CMAKE_COMMAND} -E copy "${WEB_ASSETS_DIR}/rom/${f}" "${WEB_DIST}/rom/${f}"
        DEPENDS "${WEB_ASSETS_DIR}/rom/${f}")
endforeach()
foreach(f ${WEB_DISKS})
    list(APPEND WEB_COPIES "${WEB_DIST}/disks/${f}")
    add_custom_command(OUTPUT "${WEB_DIST}/disks/${f}"
        COMMAND ${CMAKE_COMMAND} -E copy "${WEB_ASSETS_DIR}/disks/${f}" "${WEB_DIST}/disks/${f}"
        DEPENDS "${WEB_ASSETS_DIR}/disks/${f}")
endforeach()
add_custom_target(ms0515_web_dist ALL DEPENDS ms0515_web ${WEB_COPIES})
