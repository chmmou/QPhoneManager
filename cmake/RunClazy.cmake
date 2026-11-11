# Script mode: cmake -DCLAZY_EXECUTABLE=... -DCHECKS_FILE=... -DSOURCE_DIR=...
# -DBUILD_DIR=... -P RunClazy.cmake
#
# The build runs under GCC with -Werror, and clazy is clang-based. The compile
# database is therefore cleaned into a copy before the run: -Werror would turn
# the first finding into an error and stop the analysis of that translation
# unit, and GCC-only flags make clang reject the whole unit.

foreach(variable IN ITEMS CLAZY_EXECUTABLE CHECKS_FILE SOURCE_DIR BUILD_DIR)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "RunClazy.cmake: ${variable} is not set")
    endif()
endforeach()

function(_qpm_regex_escape text out_var)
    string(REGEX REPLACE "([][.+*?()^$|\\\\{}])" "\\\\\\1" escaped "${text}")
    set(${out_var} "${escaped}" PARENT_SCOPE)
endfunction()

file(STRINGS "${CHECKS_FILE}" check_lines)
set(checks "")
foreach(line IN LISTS check_lines)
    string(STRIP "${line}" line)
    if(line STREQUAL "" OR line MATCHES "^#")
        continue()
    endif()
    list(APPEND checks "${line}")
endforeach()
list(JOIN checks "," checks_argument)

set(gcc_only_flags
    -mno-direct-extern-access
    -fconcepts-diagnostics-depth=[0-9]+
)

file(READ "${BUILD_DIR}/compile_commands.json" database)
string(JSON entry_count LENGTH "${database}")
math(EXPR last_index "${entry_count} - 1")

set(build_prefix "${BUILD_DIR}/")
set(third_party_prefix "${SOURCE_DIR}/third_party/")
set(files "")
set(cleaned "[]")
set(cleaned_index 0)
foreach(index RANGE ${last_index})
    string(JSON file GET "${database}" ${index} file)
    string(JSON directory GET "${database}" ${index} directory)
    if(NOT IS_ABSOLUTE "${file}")
        set(file "${directory}/${file}")
    endif()
    cmake_path(NORMAL_PATH file)

    string(FIND "${file}" "${SOURCE_DIR}/" in_source)
    string(FIND "${file}" "${build_prefix}" in_build)
    string(FIND "${file}" "${third_party_prefix}" in_third_party)
    if(NOT in_source EQUAL 0 OR in_build EQUAL 0 OR in_third_party EQUAL 0)
        continue()
    endif()

    string(JSON entry GET "${database}" ${index})
    string(REGEX REPLACE " -Werror(=[^ \"]+)?" "" entry "${entry}")
    foreach(flag IN LISTS gcc_only_flags)
        string(REGEX REPLACE " ${flag}([ \"])" "\\1" entry "${entry}")
    endforeach()
    string(JSON cleaned SET "${cleaned}" ${cleaned_index} "${entry}")
    math(EXPR cleaned_index "${cleaned_index} + 1")
    list(APPEND files "${file}")
endforeach()

if(files STREQUAL "")
    message(FATAL_ERROR "RunClazy.cmake: no project translation unit in ${BUILD_DIR}/compile_commands.json")
endif()

set(clazy_dir "${BUILD_DIR}/clazy")
file(MAKE_DIRECTORY "${clazy_dir}")
file(WRITE "${clazy_dir}/compile_commands.json" "${cleaned}")

_qpm_regex_escape("${SOURCE_DIR}" source_regex)
_qpm_regex_escape("${BUILD_DIR}" build_regex)
list(LENGTH files file_count)
message(STATUS "clazy: ${file_count} translation units, checks: ${checks_argument}")

execute_process(
    COMMAND "${CLAZY_EXECUTABLE}"
        -p "${clazy_dir}"
        "--checks=${checks_argument}"
        "--header-filter=^${source_regex}/(src|tests)/"
        "--ignore-dirs=^(${build_regex}|${source_regex}/third_party)/"
        ${files}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)
if(NOT output STREQUAL "")
    message("${output}")
endif()

string(REGEX MATCHALL "(warning|error): " findings "${output}")
list(LENGTH findings finding_count)
if(NOT result EQUAL 0 OR finding_count GREATER 0)
    message(FATAL_ERROR "clazy: ${finding_count} finding(s), exit code ${result}")
endif()
message(STATUS "clazy: no findings")
