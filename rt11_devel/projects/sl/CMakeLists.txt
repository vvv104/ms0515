# SL with Tab - DEC's single line editor for the MS 0515, and completion.
#
# DEC's SL.MAC as ../rt11/handlers/sl builds it for the machine's console
# (its SL.diff and SLCND.MAC), with Tab given to a completion overlay of our
# own: SLTAB.diff hooks it in, SLTAB.MAC is what stays in memory, SLOVL.MAC
# the overlay.  The machine's MACRO and LINK build it, run from here with
# ms0515-run (rt11_devel/toolset/cmake/Rt11.cmake).
#
#     cmake -S rt11_devel/projects/sl -B rt11_devel/projects/sl/build -G Ninja
#     cmake --build rt11_devel/projects/sl/build
#
# DEC's sources are not in the repository: MS0515_RT11_SOURCES names the
# rt11-v5.4 folder.  SL.SYS comes out in <build folder>/work/sl.sys.

cmake_minimum_required(VERSION 3.20)
project(sl LANGUAGES NONE)

include(../../toolset/cmake/Rt11.cmake)
rt11_dec_sources()

rt11_tools(TOOLS MACRO.SAV LINK.SAV SYSMAC.SML SYSLIB.OBJ)

# The dec monitor's conditionals, SL's own for the console and for a single
# job, and our sources, which the patched SL.MAC includes.
set(DEC_SL ../rt11/handlers/sl)
rt11_stage(SOURCES
    ../rt11/monitor/SYCDEC.MAC
    ${DEC_SL}/SLCND.MAC
    SLOPT.MAC
    SLTAB.MAC
    SLHIS.MAC
    SLKEY.MAC
    SLOVL.MAC)

# DEC's source: first what the console needs, then Tab's hooks, then the
# history's, then the keys'.
rt11_patched(SL_MAC SOURCE "${MS0515_RT11_SOURCES}/SL.MAC"
             PATCH ${DEC_SL}/SL.diff SLTAB.diff SLHIS.diff SLKEY.diff)

rt11_macro(OBJECT SL LISTING SL SOURCES SYCDEC SLCND SLOPT SL
           DEPENDS ${TOOLS} ${SOURCES} ${SL_MAC})

# DEC's SL.COM: no bitmap (/X), a wide map (/W), and the section SETOVR on
# a block's boundary (/Y:1000), which LINK asks the name of.
rt11_link(IMAGE SL.SYS MAP SL OBJECTS SL
          SWITCHES /W/X OBJECT_SWITCHES /Y:1000 ANSWERS SETOVR
          DEPENDS ${TOOLS})

add_custom_target(sl ALL DEPENDS "${RT11_WORK}/sl.sys")
