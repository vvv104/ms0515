# SL - the single line editor of RT-11, for the MS 0515.
#
# DEC's own SL.MAC of the V5.4 source kit with a patch for the machine's
# console (SL.diff), assembled behind the conditionals of the monitor it is
# for and its own (SLCND.MAC) and linked the way DEC's SL.COM links it.  The
# machine's MACRO and LINK do it, run from here with ms0515-run
# (rt11_devel/toolset/cmake/Rt11.cmake).
#
#     cmake -S rt11_devel/projects/rt11/handlers/sl -B <build folder>
#     cmake --build <build folder>
#
# DEC's sources are not in the repository: MS0515_RT11_SOURCES names the
# rt11-v5.4 folder.  SL.SYS comes out in <build folder>/work/sl.sys.

cmake_minimum_required(VERSION 3.20)
project(sl LANGUAGES NONE)

include(../../../../toolset/cmake/Rt11.cmake)
rt11_dec_sources()

rt11_tools(TOOLS MACRO.SAV LINK.SAV SYSMAC.SML SYSLIB.OBJ)

# The conditionals of the monitor SL is built for - the dec system's, the
# answers its SYSGEN was given - and SL's own for the machine's console go
# ahead of the source.
rt11_stage(SOURCES
    ../../monitor/SYCDEC.MAC
    SLCND.MAC)

# DEC's source with what the machine changes in it: SL.diff says what.
rt11_patched(SL_MAC SOURCE "${MS0515_RT11_SOURCES}/SL.MAC" PATCH SL.diff)

rt11_macro(OBJECT SL LISTING SL SOURCES SYCDEC SLCND SL
           DEPENDS ${TOOLS} ${SOURCES} ${SL_MAC})

# DEC's SL.COM: no bitmap (/X) - a handler must not carry one - a wide map
# (/W), and the section SETOVR on a block's boundary (/Y:1000), which LINK
# asks the name of.
rt11_link(IMAGE SL.SYS MAP SL OBJECTS SL
          SWITCHES /W/X OBJECT_SWITCHES /Y:1000 ANSWERS SETOVR
          DEPENDS ${TOOLS})

add_custom_target(sl ALL DEPENDS "${RT11_WORK}/sl.sys")
