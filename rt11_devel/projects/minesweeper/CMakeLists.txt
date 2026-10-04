# The Minesweeper Game, Ver. 1.01 - V. V. Voronkov's Pascal minesweeper of
# 1995 for the MS 0515, built from his sources.
#
# OMSI Pascal (PAS1), MACRO and LINK of the machine build it, run from here
# with ms0515-run (rt11_devel/toolset/cmake/Rt11.cmake):
#
#     cmake -S rt11_devel/projects/minesweeper -B <build folder> -G Ninja
#     cmake --build <build folder>
#
# The game comes out in <build folder>/game: K.SAV and its help K.HLP.
# `ms0515-run K` there plays it.

cmake_minimum_required(VERSION 3.20)
project(minesweeper LANGUAGES NONE)

include(../../toolset/cmake/Rt11.cmake)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# The compiler and its libraries, the kit's system library before DEC's,
# FORTRAN's library for RAN, which the help's cipher is made with.
rt11_tools(TOOLS PAS1.SAV PASLIB.OBJ PAS1.OBJ FORLIB.OBJ
                 MACRO.SAV LINK.SAV SYSMAC.SML SYSLIB.OBJ
           FOLDERS pascal fortran fodos)

rt11_stage(SOURCES K.PAS SPR.PAS RND.PAS)

foreach(unit K SPR RND)
    rt11_pascal(OBJECT ${unit} DEPENDS ${TOOLS} ${SOURCES})
endforeach()

# The author's K.COM order: the main program, PASLIB right after it - its
# graphics routines have to lie under the video window - then the modules.
rt11_link(IMAGE K.SAV MAP K OBJECTS K SPR RND
          INPUTS K PASLIB SPR RND PAS1 FORLIB
          DEPENDS ${TOOLS})

# The game's own file: what LINK made, its sum and the sprite table after
# it (tail.py), and the help beside it.
set(GAME "${CMAKE_CURRENT_BINARY_DIR}/game")
add_custom_command(
    OUTPUT  "${GAME}/K.SAV" "${GAME}/K.HLP"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GAME}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tail.py"
            "${RT11_WORK}/k.sav" "${CMAKE_CURRENT_SOURCE_DIR}/K.DAT" "${GAME}/K.SAV"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/K.HLP" "${GAME}/K.HLP"
    DEPENDS "${RT11_WORK}/k.sav" tail.py K.DAT K.HLP
    COMMENT "K.SAV with its sum and sprites"
    VERBATIM)

add_custom_target(minesweeper ALL DEPENDS "${GAME}/K.SAV")
