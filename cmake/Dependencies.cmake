include_guard(GLOBAL)

function(scene_polytree_require_polytree)
    if(TARGET polytree::polytree)
        return()
    endif()

    if(SCENE_POLYTREE_ALGO_SOURCE_DIR)
        set(
            POLYTREE_ALGO_SOURCE_DIR
            "${SCENE_POLYTREE_ALGO_SOURCE_DIR}"
            CACHE PATH
            "Optional path to an algo source checkout"
            FORCE
        )
    endif()

    if(SCENE_POLYTREE_POLYTREE_SOURCE_DIR)
        set(POLYTREE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        add_subdirectory(
            "${SCENE_POLYTREE_POLYTREE_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/_deps/polytree-build"
            EXCLUDE_FROM_ALL
        )
        return()
    endif()

    find_package(polytree CONFIG QUIET)
    if(TARGET polytree::polytree)
        return()
    endif()

    include(FetchContent)
    set(POLYTREE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        polytree
        GIT_REPOSITORY https://github.com/brooteskan/polytree.git
        GIT_TAG v0.0.1-wozzits-baseline
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(polytree)

    if(NOT TARGET polytree::polytree)
        message(FATAL_ERROR "The polytree dependency did not export polytree::polytree")
    endif()
endfunction()
