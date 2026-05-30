set(TEST_FILES
    test_physics_parity.c
    test_demo_reader.c
    test_ddnet_demo.c
)

foreach(test_file ${TEST_FILES})
    get_filename_component(test_name ${test_file} NAME_WE)
    add_executable(${test_name} ${test_file} ../ddnet_maploader/ddnet_map_loader.c)
    target_link_libraries(${test_name} PRIVATE ddnet_demo z)
    if(UNIX)
        target_link_libraries(${test_name} PRIVATE m)
    endif()
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
