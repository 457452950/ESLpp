cmake_minimum_required(VERSION 3.16)
project(test_eslpp)

set(CMAKE_CXX_STANDARD 11)

add_executable(
        test_context
        test_context.cpp
)

target_include_directories(
        test_context
        PRIVATE "../include/"
)
if (WIN32)
    target_compile_options(
            test_context
            PRIVATE -Wall
    )
else ()
    target_compile_options(
            test_context
            PRIVATE -Wall -Werror
    )
endif ()

set(Boost_ROOT "I:/boost-1.88.0/lib/cmake/Boost-1.88.0")
set(Boost_VERBOSE ON)
find_package(Boost CONFIG)
if (Boost_FOUND)
    message(STATUS ${Boost_INCLUDE_DIRS})
    add_executable(test_service test_service.cpp)
    target_include_directories(
            test_service
            PRIVATE "../include/"
            PRIVATE ${Boost_INCLUDE_DIRS}
    )
endif ()
