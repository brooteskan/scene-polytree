o3de_gem_setup("ScenePolytree")

# O3DE is the integration host and opts into the otherwise engine-neutral repository.
if(NOT TARGET scene-polytree::motion)
    set(SCENE_POLYTREE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(SCENE_POLYTREE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    add_subdirectory(
        "${CMAKE_CURRENT_SOURCE_DIR}/../.."
        "${CMAKE_CURRENT_BINARY_DIR}/scene-polytree-core"
        EXCLUDE_FROM_ALL
    )
endif()

ly_add_target(
    NAME ${gem_name}.Static STATIC
    NAMESPACE Gem
    FILES_CMAKE scene_polytree_files.cmake
    INCLUDE_DIRECTORIES PUBLIC Code/Include PRIVATE Code/Source
    BUILD_DEPENDENCIES
        PUBLIC AZ::AzCore AZ::AzFramework
        PRIVATE scene-polytree::motion
)

if(MSVC)
    # polytree uses transactional exception rollback internally. O3DE disables unwind semantics
    # by default, so the host target opts in without changing the engine-neutral libraries.
    target_compile_options(${gem_name}.Static PRIVATE /EHsc /wd4100)
endif()

ly_add_target(
    NAME ${gem_name} ${PAL_TRAIT_MONOLITHIC_DRIVEN_MODULE_TYPE}
    NAMESPACE Gem
    FILES_CMAKE scene_polytree_shared_files.cmake
    BUILD_DEPENDENCIES PRIVATE Gem::${gem_name}.Static scene-polytree::motion
)

ly_add_source_properties(
    SOURCES Code/Source/ScenePolytreeModule.cpp
    PROPERTY COMPILE_DEFINITIONS
    VALUES O3DE_GEM_NAME=${gem_name} O3DE_GEM_VERSION=${gem_version}
)

ly_create_alias(NAME ${gem_name}.Clients NAMESPACE Gem TARGETS Gem::${gem_name})
ly_create_alias(NAME ${gem_name}.Servers NAMESPACE Gem TARGETS Gem::${gem_name})
ly_create_alias(NAME ${gem_name}.Unified NAMESPACE Gem TARGETS Gem::${gem_name})

if(PAL_TRAIT_BUILD_HOST_TOOLS)
    ly_create_alias(NAME ${gem_name}.Tools NAMESPACE Gem TARGETS Gem::${gem_name})
    ly_create_alias(NAME ${gem_name}.Builders NAMESPACE Gem TARGETS Gem::${gem_name})
endif()

o3de_add_variant_dependencies_for_gem_dependencies(
    GEM_NAME ${gem_name}
    VARIANTS Builders Tools Clients Servers Unified
)

if(PAL_TRAIT_BUILD_TESTS_SUPPORTED)
    ly_add_target(
        NAME ${gem_name}.Tests ${PAL_TRAIT_TEST_TARGET_TYPE}
        NAMESPACE Gem
        FILES_CMAKE Code/Tests/scene_polytree_tests_files.cmake
        INCLUDE_DIRECTORIES PRIVATE Code/Include Code/Source
        BUILD_DEPENDENCIES PRIVATE
            AZ::AzTest
            AZ::AzFrameworkTestShared
            Gem::${gem_name}.Static
            scene-polytree::motion
    )
    ly_add_googletest(NAME Gem::${gem_name}.Tests LABELS REQUIRES_tiaf)
endif()
