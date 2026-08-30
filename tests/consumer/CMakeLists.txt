cmake_minimum_required(VERSION 3.24)

project(scene-polytree-consumer LANGUAGES CXX)

include(CTest)

find_package(scene-polytree 0.3 CONFIG REQUIRED)

add_executable(scene_polytree_consumer main.cpp)
target_link_libraries(scene_polytree_consumer PRIVATE scene-polytree::core)

add_test(NAME scene_polytree.consumer COMMAND scene_polytree_consumer)
