add_executable(
    scene_polytree_motion_storage_benchmark
    motion_storage_benchmark.cpp
)
target_link_libraries(
    scene_polytree_motion_storage_benchmark
    PRIVATE scene-polytree::motion
)

execute_process(
    COMMAND git -c safe.directory=${PROJECT_SOURCE_DIR} rev-parse --short=12 HEAD
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE SCENE_POLYTREE_BENCHMARK_GIT_REVISION
    RESULT_VARIABLE SCENE_POLYTREE_BENCHMARK_GIT_REVISION_RESULT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(
    NOT SCENE_POLYTREE_BENCHMARK_GIT_REVISION_RESULT EQUAL 0
    OR NOT SCENE_POLYTREE_BENCHMARK_GIT_REVISION
)
    set(SCENE_POLYTREE_BENCHMARK_GIT_REVISION unknown)
endif()
execute_process(
    COMMAND git -c safe.directory=${PROJECT_SOURCE_DIR} status --porcelain
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
    OUTPUT_VARIABLE SCENE_POLYTREE_BENCHMARK_GIT_STATUS
    RESULT_VARIABLE SCENE_POLYTREE_BENCHMARK_GIT_STATUS_RESULT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(
    SCENE_POLYTREE_BENCHMARK_GIT_STATUS_RESULT EQUAL 0
    AND SCENE_POLYTREE_BENCHMARK_GIT_STATUS
)
    set(SCENE_POLYTREE_BENCHMARK_GIT_DIRTY true)
else()
    set(SCENE_POLYTREE_BENCHMARK_GIT_DIRTY false)
endif()

add_executable(
    scene_polytree_performance_benchmark
    benchmark_fixture.cpp
    performance_benchmark.cpp
)
target_link_libraries(
    scene_polytree_performance_benchmark
    PRIVATE
        scene-polytree::motion
        scene-polytree::cpu-executor
)
target_compile_definitions(
    scene_polytree_performance_benchmark
    PRIVATE
        SCENE_POLYTREE_BENCHMARK_GIT_REVISION="${SCENE_POLYTREE_BENCHMARK_GIT_REVISION}"
        SCENE_POLYTREE_BENCHMARK_GIT_DIRTY="${SCENE_POLYTREE_BENCHMARK_GIT_DIRTY}"
        SCENE_POLYTREE_BENCHMARK_PROJECT_VERSION="${PROJECT_VERSION}"
        SCENE_POLYTREE_BENCHMARK_COMPILER_ID="${CMAKE_CXX_COMPILER_ID}"
        SCENE_POLYTREE_BENCHMARK_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
        SCENE_POLYTREE_BENCHMARK_SYSTEM_NAME="${CMAKE_SYSTEM_NAME}"
        SCENE_POLYTREE_BENCHMARK_SYSTEM_PROCESSOR="${CMAKE_SYSTEM_PROCESSOR}"
        SCENE_POLYTREE_BENCHMARK_BUILD_CONFIG="$<CONFIG>"
)

if(SCENE_POLYTREE_BUILD_TESTS)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME scene_polytree.benchmark.smoke
            COMMAND
                ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tools/validate_benchmark_output.py
                $<TARGET_FILE:scene_polytree_performance_benchmark>
        )
        set_tests_properties(scene_polytree.benchmark.smoke PROPERTIES TIMEOUT 120)
    endif()
endif()
