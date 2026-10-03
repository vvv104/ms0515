# Rt11Patch.cmake - a source of DEC's with a patch on it, for a build rule
# (Rt11.cmake).
#
#     cmake -DGIT=<git> -DSOURCE=<DEC's file> -DDIFF=<the patch>
#           -DOUTPUT=<the patched copy> -P Rt11Patch.cmake
#
# DEC's file is never changed and never kept in the repository: it is copied
# to OUTPUT and the patch - a unified diff, `--- a/NAME` / `+++ b/NAME`,
# with whatever explanation ahead of it - is applied to the copy.  Both are
# read with their line ends taken as they come, CR LF or LF, so that a kit
# unpacked on one system and a patch checked out on another still meet.

get_filename_component(folder "${OUTPUT}" DIRECTORY)
get_filename_component(name "${OUTPUT}" NAME)
file(MAKE_DIRECTORY "${folder}")

file(READ "${SOURCE}" text)
string(REPLACE "\r\n" "\n" text "${text}")
file(WRITE "${OUTPUT}" "${text}")

file(READ "${DIFF}" text)
string(REPLACE "\r\n" "\n" text "${text}")
file(WRITE "${OUTPUT}.diff" "${text}")

execute_process(
    COMMAND "${GIT}" apply -p1 "${name}.diff"
    WORKING_DIRECTORY "${folder}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE said
    ERROR_VARIABLE said)
file(REMOVE "${OUTPUT}.diff")
if(NOT status EQUAL 0)
    file(REMOVE "${OUTPUT}")
    message(FATAL_ERROR "${DIFF} does not apply to ${SOURCE}:\n${said}")
endif()
