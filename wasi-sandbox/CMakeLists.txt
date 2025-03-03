cmake_minimum_required(VERSION 3.28)

project(wasi-sandbox LANGUAGES C CXX)

add_library(wasi-sandbox STATIC)
target_sources(wasi-sandbox PRIVATE
	source/wasi-sandbox.cpp
)
target_include_directories(wasi-sandbox PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
