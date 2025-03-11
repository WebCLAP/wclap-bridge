include(FetchContent)
#set(FETCHCONTENT_QUIET False)
cmake_policy(SET CMP0135 NEW) # sets timestamps to extraction time, so we always rebuild when redownloading

add_library(wasmtime INTERFACE)

message(STATUS "checking/downloading Wasmtime C SDK")
if(APPLE)
	# Fetch both AMD64 and ARM64
	FetchContent_Declare(wasmtime-c-api-amd64
		URL https://github.com/bytecodealliance/wasmtime/releases/download/v30.0.1/wasmtime-v30.0.1-x86_64-macos-c-api.tar.xz
		URL_HASH SHA256=c7083258caf236c6042c7c12ac104e34d8c2f707b220ada1abdabca71c6f9a4b
	)
	FetchContent_Declare(wasmtime-c-api-arm64
		URL https://github.com/bytecodealliance/wasmtime/releases/download/v30.0.1/wasmtime-v30.0.1-aarch64-macos-c-api.tar.xz
		URL_HASH SHA256=d0ac00220fab585693189a46ddf84ac8ff2eeb810b3d4d09b498acb5b9268e5d
	)
	FetchContent_MakeAvailable(wasmtime-c-api-amd64 wasmtime-c-api-arm64)
	
	add_custom_command(
		OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libwasmtime-combined.a
		COMMAND lipo "${wasmtime-c-api-amd64_SOURCE_DIR}/lib/libwasmtime.a" "${wasmtime-c-api-arm64_SOURCE_DIR}/lib/libwasmtime.a" -create -output "${CMAKE_CURRENT_BINARY_DIR}/libwasmtime-combined.a"
	)
	add_custom_target(wasmtime-c-api-combined DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/libwasmtime-combined.a")

	target_include_directories(wasmtime INTERFACE "${wasmtime-c-api-amd64_SOURCE_DIR}/include")
	add_dependencies(wasmtime wasmtime-c-api-combined)
	target_link_libraries(wasmtime INTERFACE "${CMAKE_CURRENT_BINARY_DIR}/libwasmtime-combined.a")
else()
	message(FATAL_ERROR "Only Mac for now - add the OS-specific resources to ????-c-api.cmake")
endif()
