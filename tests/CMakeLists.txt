cmake_minimum_required(VERSION 3.10)
project(ameltech_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(host_test
  host_test.cpp
)

target_include_directories(host_test PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/host_stub
  ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_compile_definitions(host_test PRIVATE
  HOST_TEST=1
)

# On host we do not define ESP32 so NVS and hardware paths stay inactive
