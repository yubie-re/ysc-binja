cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 23)
include_directories(
./
)

set(SRC_DIR "${PROJECT_SOURCE_DIR}/src")

file(GLOB_RECURSE SRC_MAIN
    "${SRC_DIR}/**.hpp"
    "${SRC_DIR}/**.h"
    "${SRC_DIR}/**.cpp"
    "${SRC_DIR}/**.cc"
    "${SRC_DIR}/**.cxx"
    "${SRC_DIR}/**.asm"
)

add_compile_options(/bigobj)
add_library(BinjaYSC SHARED ${SRC_MAIN})

target_precompile_headers(BinjaYSC PRIVATE "${SRC_DIR}/inc.hpp")
target_link_libraries(BinjaYSC binaryninjacore binaryninjaapi fmt nlohmann_json::nlohmann_json)

bn_install_plugin(BinjaYSC)

add_definitions(/MP)