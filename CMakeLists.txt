cmake_minimum_required(VERSION 3.28)

project(wclap-bridge LANGUAGES C CXX)

add_library(wclap-bridge STATIC)
target_sources(wclap-bridge PRIVATE
	source/wclap-bridge.cpp
	source/wclap-bridge-impl.cpp
)
target_include_directories(wclap-bridge PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_include_directories(wclap-bridge PUBLIC ${CMAKE_CURRENT_LIST_DIR}/modules/clap/include)

#include(FetchContent)
#set(FETCHCONTENT_QUIET False)
#FetchContent_Declare(
#	wasmtime
#	GIT_REPOSITORY https://github.com/bytecodealliance/wasmtime.git
#	GIT_TAG v30.0.2
#	GIT_SHALLOW ON
#	SOURCE_SUBDIR crates/c-api
#)
#FetchContent_MakeAvailable(wasmtime)
include(wasmtime-fetched.cmake)

target_include_directories(wclap-bridge PUBLIC wasmtime)
target_link_libraries(wclap-bridge PUBLIC wasmtime)
