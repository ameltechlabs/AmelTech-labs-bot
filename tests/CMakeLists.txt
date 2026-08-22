cmake_minimum_required(VERSION 3.10)
project(AmelTechBotHostTests CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(host_tests
    host_test.cpp
    ${CMAKE_SOURCE_DIR}/../src/KnowledgeBase.cpp
    ${CMAKE_SOURCE_DIR}/../src/Calculator.cpp
)

target_include_directories(host_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/host_stub
    ${CMAKE_SOURCE_DIR}/../src
)

# AmelTechBot.h is intentionally NOT included by the host test build:
# host_test.cpp declares its own minimal AmelTechStatus enum so that
# KnowledgeBase.cpp/Calculator.cpp compile without pulling in
# Telemetry.h/Diagnostics.h, which depend on ESP32-only APIs
# (WiFi.h, Preferences.h, esp_system.h) not present on a desktop host.
