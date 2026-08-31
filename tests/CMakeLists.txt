# ---------------------------------------------------------------------------
# Host test build for AmelTech lab's bot
#
# These tests run on a desktop, not on the ESP32. Hardware specific calls are
# satisfied by the small stubs in host_stub/, which deliberately report
# "unsupported" for anything a desktop cannot measure. That is the same path
# the library takes on a chip without a given feature, so the honesty rules get
# exercised too.
#
#   cd tests
#   cmake -B build .
#   cmake --build build
#   ./build/ameltech_tests
#
# Or with ctest:
#   cd build && ctest --output-on-failure
# ---------------------------------------------------------------------------

cmake_minimum_required(VERSION 3.13)
project(ameltech_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(AMELTECH_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src)

# The generated knowledge table is large; list sources explicitly so a stray
# file in src/ cannot silently join the build.
set(AMELTECH_SOURCES
    ${AMELTECH_SRC_DIR}/AmelTechBot.cpp
    ${AMELTECH_SRC_DIR}/AmelTechLog.cpp
    ${AMELTECH_SRC_DIR}/Calculator.cpp
    ${AMELTECH_SRC_DIR}/Diagnostics.cpp
    ${AMELTECH_SRC_DIR}/KnowledgeBase.cpp
    ${AMELTECH_SRC_DIR}/NeuralEngine.cpp
    ${AMELTECH_SRC_DIR}/SensorHub.cpp
    ${AMELTECH_SRC_DIR}/Telemetry.cpp
    ${AMELTECH_SRC_DIR}/ThermalGuard.cpp
    ${AMELTECH_SRC_DIR}/TrainingConsole.cpp
    ${AMELTECH_SRC_DIR}/UserProfile.cpp
    ${AMELTECH_SRC_DIR}/knowledge_generated.cpp
)

add_executable(ameltech_tests
    host_test.cpp
    host_stub/host_stub.cpp
    ${AMELTECH_SOURCES}
)

target_include_directories(ameltech_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/host_stub
    ${AMELTECH_SRC_DIR}
)

# AMELTECH_HOST_NVS points the persistence layer at the in-memory Preferences
# stub, so save and load are covered by the tests instead of being skipped.
target_compile_definitions(ameltech_tests PRIVATE AMELTECH_HOST_NVS)

if(MSVC)
    target_compile_options(ameltech_tests PRIVATE /W4)
else()
    target_compile_options(ameltech_tests PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_test(NAME ameltech_host_tests COMMAND ameltech_tests)

# ---------------------------------------------------------------------------
# Optional: compile check every example sketch.
#   cmake --build build --target check_examples
# ---------------------------------------------------------------------------
add_custom_target(check_examples
    COMMAND ${CMAKE_COMMAND} -E echo "Compile checking example sketches..."
    COMMAND ${CMAKE_COMMAND}
            -DEXAMPLES_DIR=${CMAKE_CURRENT_SOURCE_DIR}/../examples
            -P ${CMAKE_CURRENT_SOURCE_DIR}/check_examples.cmake
    VERBATIM
)
