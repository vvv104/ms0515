# Rt11Gcc.cmake - C programs for the MS 0515 as a CMake project.
#
# GCC built for pdp11-aout (rt11_devel/toolset/gcc/build-toolchain.sh)
# compiles on the host, the machine takes no part in the build: a program
# links with the runtime of rt11_devel/toolset/gcc (its start, the 16-bit
# arithmetic the processor lacks, RT-11's requests, the C library) and
# aout2sav.py wraps the a.out into a .SAV, which ms0515-run runs.
#
#     cmake_minimum_required(VERSION 3.21)
#     include(<repository>/rt11_devel/toolset/cmake/Rt11Gcc.cmake)
#     project(myprog LANGUAGES C ASM)
#
#     rt11_c_program(NAME MYPROG SOURCES myprog.c)
#
# The include comes BEFORE project(): it names the toolchain file, which
# project() reads.  The compiler is found under $MS0515_GCC/bin, or on the
# PATH.  Programs come out as <build folder>/sav/NAME.SAV.

include_guard(GLOBAL)

get_filename_component(RT11_GCC_DIR "${CMAKE_CURRENT_LIST_DIR}/../gcc" ABSOLUTE)
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${RT11_GCC_DIR}/pdp11-rt11.cmake")
endif()
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Release: -O2 (the toolchain file)")
endif()

# The runtime, built once per project with the same compiler: crt0 as
# objects every program links (the entry is not looked for in a library),
# the rest as the library rt11.
function(_rt11_gcc_runtime)
    if(TARGET rt11)
        return()
    endif()
    add_library(rt11_crt0 OBJECT "${RT11_GCC_DIR}/rt/crt0.s")
    add_library(rt11 STATIC
        "${RT11_GCC_DIR}/rt/arith.s"
        "${RT11_GCC_DIR}/rt/emt.s")
    target_include_directories(rt11 PUBLIC "${RT11_GCC_DIR}/include")
endfunction()

# rt11_c_program(NAME <NAME> SOURCES <file>... [STACK <bytes>])
#
# The sources compiled and linked with the runtime into NAME.SAV under
# <build folder>/sav, with STACK bytes for the stack above the program
# (1024 when not said).  The a.out stays beside the objects for objdump.
function(rt11_c_program)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;STACK" "SOURCES")
    if(NOT arg_NAME OR NOT arg_SOURCES)
        message(FATAL_ERROR "rt11_c_program needs NAME and SOURCES")
    endif()
    _rt11_gcc_runtime()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    string(TOLOWER "${arg_NAME}" lower)
    if(NOT arg_STACK)
        set(arg_STACK 1024)
    endif()
    string(REGEX REPLACE "gcc(\\.exe)?$" "" prefix "${CMAKE_C_COMPILER}")
    set(sav_dir "${CMAKE_BINARY_DIR}/sav")
    set(sav "${sav_dir}/${arg_NAME}.SAV")

    add_executable(${lower} ${arg_SOURCES})
    target_link_libraries(${lower} PRIVATE rt11_crt0 rt11)
    add_custom_command(
        OUTPUT  "${sav}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${sav_dir}"
        COMMAND ${Python3_EXECUTABLE} "${RT11_GCC_DIR}/aout2sav.py"
                "$<TARGET_FILE:${lower}>" "${sav}"
                --tools "${prefix}" --stack ${arg_STACK}
        DEPENDS ${lower} "${RT11_GCC_DIR}/aout2sav.py"
        COMMENT "${arg_NAME}.SAV"
        VERBATIM)
    add_custom_target(${lower}_sav ALL DEPENDS "${sav}")
endfunction()
