cmake_minimum_required(VERSION 3.10)
project(ddnet_demo C)

add_library(ddnet_demo INTERFACE)
target_include_directories(ddnet_demo INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

option(EXAMPLE "Build the example executable" OFF)

if(EXAMPLE)
    add_executable(example example.c ddnet_demo.c)
    target_link_libraries(example PRIVATE m)
endif()