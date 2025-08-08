cmake_minimum_required(VERSION 3.16)
project(ESLpp)

set(CMAKE_CXX_STANDARD 11)

option(BUILD_TEST "build test" ON)

if (BUILD_TEST)
    add_subdirectory(test)
endif ()
