# Rt11Run.cmake - one run of an RT-11 program, for a build rule (Rt11.cmake).
#
#     cmake -DRUN=<ms0515-run> -DWORK=<folder> -DPROGRAM=<name>
#           -DLINE=<command line> [-DANSWERS=<line>|<line>...]
#           [-DSWITCHES=<switch>|...] [-DFAILS_WITH=<regex>] -P Rt11Run.cmake
#
# Runs `ms0515-run [switches] PROGRAM line` in WORK, with ANSWERS as what is
# typed to the program when it asks for a key, shows what it printed, and
# fails when it failed: by the tool's exit status - the monitor's own
# account of the program - or by an error or fatal diagnostic (?XXX-E-,
# ?XXX-F-, ?XXX-U-) in what it printed, or by what matches FAILS_WITH.  A
# warning (-W-) is shown and left to the reader.

string(REPLACE "|" ";" answers "${ANSWERS}")
string(REPLACE "|" ";" switches "${SWITCHES}")
separate_arguments(line UNIX_COMMAND "${LINE}")

# What is typed: a file, since a build has no terminal.  Kept beside the
# work folder, not in it - the folder is the program's disk.
string(MAKE_C_IDENTIFIER "${PROGRAM}_${LINE}" tag)
set(typed "${WORK}/../typed/${tag}.txt")
set(text "")
foreach(answer IN LISTS answers)
    string(APPEND text "${answer}\r\n")
endforeach()
file(WRITE "${typed}" "${text}")

execute_process(
    COMMAND "${RUN}" ${switches} "${PROGRAM}" ${line}
    WORKING_DIRECTORY "${WORK}"
    INPUT_FILE "${typed}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE printed
    ERROR_VARIABLE complained)

string(STRIP "${printed}${complained}" printed)
if(printed)
    message("${printed}")
endif()
if(printed MATCHES "\\?[A-Z]+-[FEU]-[^\r\n]*")
    message(FATAL_ERROR "${PROGRAM} ${LINE}: ${CMAKE_MATCH_0}")
endif()
if(FAILS_WITH AND printed MATCHES "${FAILS_WITH}")
    message(FATAL_ERROR "${PROGRAM} ${LINE}: ${CMAKE_MATCH_0}")
endif()
if(NOT status EQUAL 0)
    message(FATAL_ERROR "${PROGRAM} ${LINE}: ms0515-run ended with ${status}")
endif()
