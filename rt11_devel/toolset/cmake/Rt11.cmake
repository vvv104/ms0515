# Rt11.cmake - RT-11 programs for the MS 0515 as a CMake project.
#
# The machine's own tools - MACRO, LINK, the compilers - are run from the
# host with ms0515-run (src/tools/run), one program a run, in a work folder
# that is the program's disk.  This module makes build rules of such runs,
# so that a guest program is an ordinary CMake project: it configures with
# any generator, builds what is out of date, and opens in an editor that
# knows CMake.
#
#     cmake_minimum_required(VERSION 3.20)
#     project(myprog LANGUAGES NONE)
#     include(<repository>/rt11_devel/toolset/cmake/Rt11.cmake)
#
#     rt11_tools(TOOLS MACRO.SAV LINK.SAV SYSMAC.SML SYSLIB.OBJ)
#     rt11_stage(SOURCES MYPROG.MAC)
#     rt11_macro(OBJECT MYPROG SOURCES MYPROG DEPENDS ${TOOLS} ${SOURCES})
#     rt11_link(IMAGE MYPROG.SAV OBJECTS MYPROG DEPENDS ${TOOLS})
#     add_custom_target(myprog ALL DEPENDS ${RT11_WORK}/myprog.sav)
#
# What it needs, each a cache variable that an environment variable of the
# same name presets:
#
#   MS0515_RUN        ms0515-run; looked for in $MS0515_PACKAGE and in the
#                     repository's package/ folder
#   MS0515_SOFTWARE   the software collection (the tools come from its
#                     software/development); ../ms0515-software beside the
#                     repository when not said
#
# The work folder is ${RT11_WORK}, in the build tree.  A file a program
# writes there has the name RT-11 gave it, in lower case: MACRO's object of
# X is x.obj, LINK's image x.sav.  Rules name their outputs so.

include_guard(GLOBAL)

get_filename_component(RT11_REPOSITORY "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(RT11_RUN_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/Rt11Run.cmake")
set(RT11_PATCH_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/Rt11Patch.cmake")

find_program(MS0515_RUN NAMES ms0515-run
    HINTS "$ENV{MS0515_PACKAGE}" "${RT11_REPOSITORY}/package"
    DOC "ms0515-run, which runs one RT-11 program from its folder")
if(NOT MS0515_RUN)
    message(FATAL_ERROR "ms0515-run is not found: build the emulator "
                        "(its package/ folder) or set MS0515_RUN")
endif()

if(NOT MS0515_SOFTWARE)
    if(DEFINED ENV{MS0515_SOFTWARE})
        file(TO_CMAKE_PATH "$ENV{MS0515_SOFTWARE}" _rt11_software)
    else()
        get_filename_component(_rt11_software
            "${RT11_REPOSITORY}/../ms0515-software" ABSOLUTE)
    endif()
    set(MS0515_SOFTWARE "${_rt11_software}" CACHE PATH
        "The software collection the machine's tools are taken from")
endif()

set(RT11_WORK "${CMAKE_CURRENT_BINARY_DIR}/work")
file(MAKE_DIRECTORY "${RT11_WORK}")

# rt11_dec_sources()
#
# For a project built over DEC's own sources, which are not in this
# repository: MS0515_RT11_SOURCES is set to the folder of the RT-11 V5.4
# source kit - the cache or the environment variable of that name, else
# sources/rt11-v5.4 of the software collection - or the configuration
# stops, saying so.
function(rt11_dec_sources)
    if(NOT MS0515_RT11_SOURCES)
        if(DEFINED ENV{MS0515_RT11_SOURCES})
            file(TO_CMAKE_PATH "$ENV{MS0515_RT11_SOURCES}" folder)
        else()
            set(folder "${MS0515_SOFTWARE}/sources/rt11-v5.4")
        endif()
        set(MS0515_RT11_SOURCES "${folder}" CACHE PATH
            "DEC's RT-11 V5.4 source kit (the rt11-v5.4 folder)")
    endif()
    if(NOT EXISTS "${MS0515_RT11_SOURCES}/SL.MAC")
        message(FATAL_ERROR "no DEC sources in ${MS0515_RT11_SOURCES}: set "
                            "MS0515_RT11_SOURCES to the rt11-v5.4 folder")
    endif()
endfunction()

# rt11_stage(<variable> <file>...)
#
# The files copied into the work folder, each under its own name, again
# whenever one changes.  <variable> receives the copies, for the DEPENDS of
# the rules that read them.  A relative file is the project's own.
function(rt11_stage variable)
    set(staged "")
    foreach(file IN LISTS ARGN)
        get_filename_component(file "${file}" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        get_filename_component(name "${file}" NAME)
        add_custom_command(
            OUTPUT  "${RT11_WORK}/${name}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${file}" "${RT11_WORK}/${name}"
            DEPENDS "${file}"
            COMMENT "Staging ${name}"
            VERBATIM)
        list(APPEND staged "${RT11_WORK}/${name}")
    endforeach()
    set(${variable} "${staged}" PARENT_SCOPE)
endfunction()

# rt11_patched(<variable> SOURCE <file> PATCH <diff>...)
#
# A file of DEC's with the project's patches on it, in the work folder under
# its own name, made again whenever any of them changes.  DEC's file stays
# as it is, outside the repository; what the machine changes in it is a
# patch, a unified diff (`--- a/SL.MAC`, `+++ b/SL.MAC`) that says why at
# its head.  Several are applied in the order given, each to what the one
# before it left.  <variable> receives the patched copy, for the DEPENDS of
# what reads it.
function(rt11_patched variable)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "SOURCE" "PATCH")
    find_package(Git REQUIRED)
    set(diffs "")
    foreach(diff IN LISTS arg_PATCH)
        get_filename_component(diff "${diff}" ABSOLUTE
                               BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND diffs "${diff}")
    endforeach()
    string(REPLACE ";" "|" joined "${diffs}")
    get_filename_component(name "${arg_SOURCE}" NAME)
    add_custom_command(
        OUTPUT  "${RT11_WORK}/${name}"
        COMMAND ${CMAKE_COMMAND}
                "-DGIT=${GIT_EXECUTABLE}" "-DSOURCE=${arg_SOURCE}"
                "-DDIFFS=${joined}" "-DOUTPUT=${RT11_WORK}/${name}"
                -P "${RT11_PATCH_SCRIPT}"
        DEPENDS "${arg_SOURCE}" ${diffs} "${RT11_PATCH_SCRIPT}"
        COMMENT "Patching ${name}"
        VERBATIM)
    set(${variable} "${RT11_WORK}/${name}" PARENT_SCOPE)
endfunction()

# rt11_tools(<variable> <name>... [FOLDERS <folder>...])
#
# The collection's programs and libraries by name (MACRO.SAV, SYSLIB.OBJ),
# staged.  Each is taken from the first of FOLDERS, under the collection's
# software/development, that has it; the root alone when none is said.  A
# kit's toolchain names its folder and fodos/ before the root, so that the
# kit's system library wins over DEC's: FOLDERS pascal fodos "".
function(rt11_tools variable)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "FOLDERS")
    if(NOT DEFINED arg_FOLDERS)
        set(arg_FOLDERS "")
    endif()
    set(root "${MS0515_SOFTWARE}/software/development")
    set(found "")
    foreach(name IN LISTS arg_UNPARSED_ARGUMENTS)
        set(where "")
        foreach(folder IN LISTS arg_FOLDERS ITEMS "")
            if(NOT where AND EXISTS "${root}/${folder}/${name}")
                set(where "${root}/${folder}/${name}")
            endif()
        endforeach()
        if(NOT where)
            message(FATAL_ERROR "the collection at ${MS0515_SOFTWARE} has no "
                                "${name} (set MS0515_SOFTWARE)")
        endif()
        list(APPEND found "${where}")
    endforeach()
    rt11_stage(staged ${found})
    set(${variable} "${staged}" PARENT_SCOPE)
endfunction()

# rt11_run(PROGRAM <name> LINE <command line> OUTPUT <file>...
#          [ANSWERS <line>...] [DEPENDS <file>...] [SWITCHES <switch>...])
#
# One run of a program in the work folder: `ms0515-run PROGRAM line`, with
# ANSWERS typed to it when it asks (LINK's "Boundary section?").  OUTPUT
# names what it writes, as the files are called in the work folder - lower
# case.  SWITCHES are ms0515-run's own (--em).  The run fails the build
# when the program fails or prints an error.
function(rt11_run)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "PROGRAM;LINE" "OUTPUT;ANSWERS;DEPENDS;SWITCHES")
    if(NOT arg_PROGRAM OR NOT arg_OUTPUT)
        message(FATAL_ERROR "rt11_run needs PROGRAM and OUTPUT")
    endif()
    set(outputs "")
    foreach(file IN LISTS arg_OUTPUT)
        list(APPEND outputs "${RT11_WORK}/${file}")
    endforeach()
    string(REPLACE ";" "|" answers "${arg_ANSWERS}")
    string(REPLACE ";" "|" switches "${arg_SWITCHES}")
    add_custom_command(
        OUTPUT  ${outputs}
        COMMAND ${CMAKE_COMMAND}
                "-DRUN=${MS0515_RUN}" "-DWORK=${RT11_WORK}"
                "-DPROGRAM=${arg_PROGRAM}" "-DLINE=${arg_LINE}"
                "-DANSWERS=${answers}" "-DSWITCHES=${switches}"
                -P "${RT11_RUN_SCRIPT}"
        DEPENDS ${arg_DEPENDS} "${RT11_RUN_SCRIPT}"
        COMMENT "${arg_PROGRAM} ${arg_LINE}"
        VERBATIM)
endfunction()

# rt11_macro(OBJECT <name> SOURCES <name>... [LISTING <name>]
#            [DEPENDS <file>...])
#
# MACRO: the sources assembled, in order, as one - a prefix file that sets
# conditionals, then the source - into <name>.OBJ, with a listing when one
# is asked for.  Names are RT-11's, without the .MAC and .OBJ.
function(rt11_macro)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "OBJECT;LISTING" "SOURCES;DEPENDS")
    string(REPLACE ";" "," sources "${arg_SOURCES}")
    string(TOLOWER "${arg_OBJECT}.obj" output)
    set(line "${arg_OBJECT}")
    if(arg_LISTING)
        string(APPEND line ",${arg_LISTING}")
        string(TOLOWER "${arg_LISTING}.lst" listing)
        list(APPEND output "${listing}")
    endif()
    rt11_run(PROGRAM MACRO LINE "${line}=${sources}" OUTPUT ${output}
             DEPENDS ${arg_DEPENDS})
endfunction()

# rt11_link(IMAGE <file> OBJECTS <name>... [MAP <name>]
#           [SWITCHES <switches>] [OBJECT_SWITCHES <switches>]
#           [ANSWERS <line>...] [DEPENDS <file>...])
#
# LINK: the objects into the image - X.SAV, or X.SYS for a handler - with a
# map when one is asked for.  SWITCHES follow the outputs (/X for no
# bitmap, /W for a wide map), OBJECT_SWITCHES the first object (/Y:1000 for
# a boundary, which LINK then asks the section of: ANSWERS).
function(rt11_link)
    cmake_parse_arguments(PARSE_ARGV 0 arg ""
        "IMAGE;MAP;SWITCHES;OBJECT_SWITCHES" "OBJECTS;ANSWERS;DEPENDS")
    string(REPLACE ";" "," objects "${arg_OBJECTS}")
    string(TOLOWER "${arg_IMAGE}" output)
    set(line "${arg_IMAGE}")
    set(depends ${arg_DEPENDS})
    if(arg_MAP)
        string(APPEND line ",${arg_MAP}")
        string(TOLOWER "${arg_MAP}.map" map)
        list(APPEND output "${map}")
    endif()
    foreach(object IN LISTS arg_OBJECTS)
        string(TOLOWER "${object}.obj" file)
        list(APPEND depends "${RT11_WORK}/${file}")
    endforeach()
    rt11_run(PROGRAM LINK
             LINE "${line}${arg_SWITCHES}=${objects}${arg_OBJECT_SWITCHES}"
             OUTPUT ${output} ANSWERS ${arg_ANSWERS} DEPENDS ${depends})
endfunction()
