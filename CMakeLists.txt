cmake_minimum_required(VERSION 3.28)

project(wclap-bridge LANGUAGES C CXX)

if (PROJECT_IS_TOP_LEVEL)
	set(CMAKE_CXX_STANDARD 17)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
	if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
		set(CMAKE_CXX_STANDARD 20) # MSVC is strict about designated initialisers
		set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	endif()
	if (NOT GENERATOR_IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
		message(WARNING "Using single-config generator, but CMAKE_BUILD_TYPE isn't set")
	endif()
endif()

add_library(wclap-bridge STATIC)
target_sources(wclap-bridge PRIVATE
	source/wclap-bridge.cpp
	source/wclap-instance-wasmtime/wclap-instance-wasmtime.cpp
)
target_include_directories(wclap-bridge PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_include_directories(wclap-bridge PUBLIC ${CMAKE_CURRENT_LIST_DIR}/modules/clap/include)

add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/modules/wclap-cpp)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/modules/webview-gui)
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/modules/choc)
if(NOT ${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	# Add CHOC to include path
	target_include_directories(webview-gui PUBLIC ${CMAKE_CURRENT_LIST_DIR}/modules/choc)
endif()
target_link_libraries(wclap-bridge PRIVATE wclap-cpp webview-gui)

include(wasmtime-fetched.cmake)

target_include_directories(wclap-bridge PRIVATE wasmtime)
target_link_libraries(wclap-bridge PRIVATE wasmtime)