cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 23)
include(FetchContent)

FetchContent_Declare(
    minitrace
    GIT_REPOSITORY https://github.com/hrydgard/minitrace.git
    GIT_TAG 242da50d4bc757ca79ca4925690683612f0e7457
)
set(FETCHCONTENT_UPDATES_DISCONNECTED_MINITRACE ON)
set(MTR_BUILD_TEST OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
FetchContent_GetProperties(minitrace)
if(NOT minitrace_POPULATED)
    cmake_policy(PUSH)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(minitrace)
    cmake_policy(POP)
    add_subdirectory("${minitrace_SOURCE_DIR}" "${minitrace_BINARY_DIR}" EXCLUDE_FROM_ALL)
endif()

include_directories(
./
${PROJECT_SOURCE_DIR}
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

if(MSVC)
    add_compile_options(/bigobj)
endif()

add_library(BinjaYSC SHARED ${SRC_MAIN})

target_include_directories(BinjaYSC PRIVATE "${minitrace_SOURCE_DIR}")
target_precompile_headers(BinjaYSC PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${SRC_DIR}/inc.hpp>")
target_link_libraries(BinjaYSC binaryninjaapi fmt nlohmann_json::nlohmann_json minitrace::minitrace)

bn_install_plugin(BinjaYSC)

if(MSVC)
    add_definitions(/MP)
endif()
