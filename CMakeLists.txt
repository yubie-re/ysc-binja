cmake_minimum_required(VERSION 3.20)
project(BinjaYSC CXX C)

message("\nFetching modules")
include(cmake/binja.cmake)
include(cmake/json.cmake)

add_subdirectory(src)
