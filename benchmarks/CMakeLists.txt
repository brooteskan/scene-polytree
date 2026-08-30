add_executable(
    scene_polytree_motion_storage_benchmark
    motion_storage_benchmark.cpp
)
target_link_libraries(
    scene_polytree_motion_storage_benchmark
    PRIVATE scene-polytree::motion
)
