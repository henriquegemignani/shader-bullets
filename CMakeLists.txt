
cmake_minimum_required(VERSION 2.8.11)
project(shader-bullets)

set(UGDK_BUILD_TESTS     OFF CACHE BOOL "Set to ON to enable building tests.")
set(UGDK_BUILD_EXAMPLES  OFF CACHE BOOL "Set to ON to enable building examples.")
set(UGDK_CREATE_BINDINGS OFF CACHE BOOL "Set to ON to generate bindings for supported script languages.")

add_subdirectory(externals/ugdk)
add_subdirectory(src)

