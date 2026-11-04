cmake_minimum_required(VERSION 3.24)
project(StreetPano VERSION 0.1.0 LANGUAGES CXX)

option(STREETPANO_WERROR "Treat compiler warnings as errors" OFF)
option(STREETPANO_BUILD_TESTS "Build the tests" ON)
set(STREETPANO_SANITIZE "" CACHE STRING "Sanitizers, for example address,undefined or thread")
option(STREETPANO_COVERAGE "Measure test coverage" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Test)

include(cmake/ProjectOptions.cmake)

add_subdirectory(src)
add_subdirectory(app)

if(STREETPANO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
