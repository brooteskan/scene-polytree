add_library(scene_polytree_tank_example_support STATIC tank_example.cpp)
target_link_libraries(
    scene_polytree_tank_example_support
    PUBLIC
        scene-polytree::motion
)
target_include_directories(
    scene_polytree_tank_example_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(SCENE_POLYTREE_BUILD_EXAMPLES)
    add_executable(scene_polytree_articulated_tank main.cpp)
    target_link_libraries(
        scene_polytree_articulated_tank
        PRIVATE
            scene_polytree_tank_example_support
    )
    if(SCENE_POLYTREE_BUILD_TESTS)
        add_test(
            NAME scene_polytree.example.articulated_tank
            COMMAND scene_polytree_articulated_tank
        )
    endif()
endif()
