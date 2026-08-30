add_executable(scene_polytree_contract_tests contracts_tests.cpp)
target_link_libraries(
    scene_polytree_contract_tests
    PRIVATE
        scene-polytree::core
        scene-polytree::motion
)

add_test(NAME scene_polytree.contracts COMMAND scene_polytree_contract_tests)

add_executable(scene_polytree_transform_tests transform_tests.cpp)
target_link_libraries(
    scene_polytree_transform_tests
    PRIVATE
        scene-polytree::core
)

add_test(NAME scene_polytree.transforms COMMAND scene_polytree_transform_tests)

add_executable(scene_polytree_motion_tests motion_tests.cpp)
target_link_libraries(
    scene_polytree_motion_tests
    PRIVATE
        scene-polytree::motion
)

add_test(NAME scene_polytree.motion COMMAND scene_polytree_motion_tests)

add_executable(scene_polytree_tank_example_tests tank_example_tests.cpp)
target_link_libraries(
    scene_polytree_tank_example_tests
    PRIVATE
        scene_polytree_tank_example_support
)

add_test(NAME scene_polytree.tank_example COMMAND scene_polytree_tank_example_tests)

find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME scene_polytree.no_handwritten_loops
        COMMAND
            ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tools/check_control_flow_policy.py
    )
endif()
