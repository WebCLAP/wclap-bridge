cmake_minimum_required(VERSION 3.28)

project(wclap-bridge LANGUAGES C CXX)

add_library(wclap-bridge STATIC)
target_sources(wclap-bridge PRIVATE
	source/wclap-bridge.cpp
)
target_include_directories(wclap-bridge PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_include_directories(wclap-bridge PUBLIC ${CMAKE_CURRENT_LIST_DIR}/modules/clap/include)
target_include_directories(wclap-bridge PUBLIC ${CMAKE_CURRENT_LIST_DIR}/modules/wasm-c-api/include)
