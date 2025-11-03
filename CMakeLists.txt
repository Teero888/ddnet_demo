cmake_minimum_required(VERSION 3.10)
project(ddnet_demo C)

add_library(ddnet_demo INTERFACE)
target_include_directories(ddnet_demo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

option(EXAMPLES "Build the example executables" ON)

if(EXAMPLES)
    add_executable(example_write example_write.c)

    add_executable(example_read example_read.c)
    if(UNIX)
        target_link_libraries(example_read PRIVATE m)
    endif()
endif()