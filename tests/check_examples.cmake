# ---------------------------------------------------------------------------
# check_examples.cmake
#
# Compile checks every example sketch against the host stubs. An .ino file is
# ordinary C++ with the Arduino headers already included, so each one is
# wrapped in a tiny translation unit that supplies main().
#
# Run through the test build:
#   cmake --build build --target check_examples
# ---------------------------------------------------------------------------

if(NOT DEFINED EXAMPLES_DIR)
    message(FATAL_ERROR "EXAMPLES_DIR was not set")
endif()

get_filename_component(ROOT_DIR "${EXAMPLES_DIR}/.." ABSOLUTE)
set(SRC_DIR "${ROOT_DIR}/src")
set(STUB_DIR "${ROOT_DIR}/tests/host_stub")
set(WORK_DIR "${CMAKE_CURRENT_BINARY_DIR}/example_checks")

file(MAKE_DIRECTORY "${WORK_DIR}")

find_program(CXX_COMPILER NAMES g++ clang++ c++)
if(NOT CXX_COMPILER)
    message(FATAL_ERROR "No C++ compiler found for the example check")
endif()

file(GLOB EXAMPLE_DIRS "${EXAMPLES_DIR}/*")

set(FAILED_EXAMPLES "")
set(CHECKED 0)

foreach(dir ${EXAMPLE_DIRS})
    if(NOT IS_DIRECTORY "${dir}")
        continue()
    endif()

    get_filename_component(name "${dir}" NAME)
    set(sketch "${dir}/${name}.ino")

    if(NOT EXISTS "${sketch}")
        message(SEND_ERROR "Missing sketch: ${sketch}")
        list(APPEND FAILED_EXAMPLES "${name} (missing .ino)")
        continue()
    endif()

    set(wrapper "${WORK_DIR}/${name}_wrapper.cpp")
    file(WRITE "${wrapper}"
        "#include <Arduino.h>\n"
        "#include <WiFi.h>\n"
        "#include \"${sketch}\"\n"
        "int main() { setup(); loop(); return 0; }\n"
    )

    execute_process(
        COMMAND "${CXX_COMPILER}" -std=c++17 -fsyntax-only -Wall
                -DAMELTECH_HOST_NVS
                "-I${STUB_DIR}" "-I${SRC_DIR}"
                "${wrapper}"
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE err
    )

    math(EXPR CHECKED "${CHECKED} + 1")

    if(rc EQUAL 0)
        message(STATUS "ok    ${name}")
    else()
        message(STATUS "FAIL  ${name}")
        message(STATUS "${err}")
        list(APPEND FAILED_EXAMPLES "${name}")
    endif()
endforeach()

list(LENGTH FAILED_EXAMPLES fail_count)
message(STATUS "----------------------------------------")
message(STATUS "examples checked: ${CHECKED}, failures: ${fail_count}")

if(fail_count GREATER 0)
    message(FATAL_ERROR "Example compile check failed: ${FAILED_EXAMPLES}")
endif()
