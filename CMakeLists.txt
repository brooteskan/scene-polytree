cmake_minimum_required(VERSION 3.24)

project(
    scene-polytree
    VERSION 0.2.0
    DESCRIPTION "Engine-neutral scene hierarchy composed over a functional polytree"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(_scene_polytree_tests_default OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(_scene_polytree_tests_default ON)
endif()

option(
    SCENE_POLYTREE_BUILD_TESTS
    "Build scene-polytree contract tests"
    ${_scene_polytree_tests_default}
)
option(
    SCENE_POLYTREE_REQUIRE_POLYTREE
    "Require and link the external polytree::polytree package"
    ON
)
set(
    SCENE_POLYTREE_POLYTREE_SOURCE_DIR
    ""
    CACHE PATH
    "Optional path to a polytree source checkout"
)
set(
    SCENE_POLYTREE_ALGO_SOURCE_DIR
    ""
    CACHE PATH
    "Optional path to an algo source checkout used by polytree"
)

add_library(scene_polytree INTERFACE)
add_library(scene-polytree::core ALIAS scene_polytree)
set_target_properties(scene_polytree PROPERTIES EXPORT_NAME core)
target_compile_features(scene_polytree INTERFACE cxx_std_20)
target_include_directories(
    scene_polytree
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(SCENE_POLYTREE_REQUIRE_POLYTREE)
    include(cmake/Dependencies.cmake)
    scene_polytree_require_polytree()
    target_link_libraries(
        scene_polytree
        INTERFACE
            $<BUILD_INTERFACE:polytree::polytree>
            $<INSTALL_INTERFACE:polytree::polytree>
    )
endif()

add_library(scene_polytree_motion INTERFACE)
add_library(scene-polytree::motion ALIAS scene_polytree_motion)
set_target_properties(scene_polytree_motion PROPERTIES EXPORT_NAME motion)
target_compile_features(scene_polytree_motion INTERFACE cxx_std_20)
target_include_directories(
    scene_polytree_motion
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/extensions/motion/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(scene_polytree_motion INTERFACE scene-polytree::core)

if(SCENE_POLYTREE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(
    TARGETS scene_polytree scene_polytree_motion
    EXPORT scene-polytree-targets
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY extensions/motion/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
    EXPORT scene-polytree-targets
    FILE scene-polytree-targets.cmake
    NAMESPACE scene-polytree::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scene-polytree
)
set(SCENE_POLYTREE_FIND_POLYTREE "")
if(SCENE_POLYTREE_REQUIRE_POLYTREE)
    set(
        SCENE_POLYTREE_FIND_POLYTREE
        "include(CMakeFindDependencyMacro)\nfind_dependency(polytree CONFIG)"
    )
endif()

configure_package_config_file(
    cmake/scene-polytree-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/scene-polytree-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scene-polytree
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/scene-polytree-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(
    FILES
        ${CMAKE_CURRENT_BINARY_DIR}/scene-polytree-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/scene-polytree-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scene-polytree
)
