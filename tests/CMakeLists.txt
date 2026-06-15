cmake_minimum_required(VERSION 3.10)

# Find ZLIB
find_package(ZLIB REQUIRED)

# C99 Tests
set(TEST_FILES
    test_physics_parity.c
    test_demo_reader.c
)

foreach(test_file ${TEST_FILES})
    get_filename_component(test_name ${test_file} NAME_WE)
    add_executable(${test_name} ${test_file} ../ddnet_maploader/ddnet_map_loader.c)
    target_link_libraries(${test_name} PRIVATE ddnet_demo ZLIB::ZLIB)
    if(UNIX)
        target_link_libraries(${test_name} PRIVATE m)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
