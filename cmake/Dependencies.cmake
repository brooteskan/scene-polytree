include_guard(GLOBAL)

function(scene_polytree_require_polytree)
    if(NOT TARGET polytree::polytree)
        find_package(polytree CONFIG REQUIRED)
    endif()
endfunction()
