cmake_minimum_required(VERSION 3.10)
project(ddnet_demo C)

if(MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_library(ddnet_demo INTERFACE)
target_include_directories(ddnet_demo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

option(EXAMPLES "Build the example executables" ON)

if(EXAMPLES)
    add_executable(example_write example_write.c)
    if(UNIX)
        target_link_libraries(example_write PRIVATE m)
    endif()

    add_executable(example_read example_read.c)

    add_executable(test_ddnet_demo test_ddnet_demo.c)
endif()