cmake_minimum_required(VERSION 3.28)

project(wasi-sandbox LANGUAGES C CXX)

add_library(wasi-sandbox INTERFACE)
target_include_directories(wasi-sandbox INTERFACE ${CMAKE_CURRENT_LIST_DIR}/include)
