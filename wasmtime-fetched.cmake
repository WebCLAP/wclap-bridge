include(FetchContent)
#set(FETCHCONTENT_QUIET False)
cmake_policy(SET CMP0135 NEW) # sets timestamps to extraction time, so we always rebuild when redownloading

add_library(wasmtime INTERFACE)

set(WASMTIME_VERSION "39.0.1")

message(STATUS "checking/downloading Wasmtime C SDK v${WASMTIME_VERSION}")

if(APPLE)
	# Fetch both AMD64 and ARM64 for universal binary
	FetchContent_Declare(wasmtime-c-api-amd64
		URL https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/wasmtime-v${WASMTIME_VERSION}-x86_64-macos-c-api.tar.xz
		URL_HASH SHA256=0ff4f203106fe2b9b7b940a670282c41b01450b44c2157693a5bfb4848391a09
	)
	FetchContent_Declare(wasmtime-c-api-arm64
		URL https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/wasmtime-v${WASMTIME_VERSION}-aarch64-macos-c-api.tar.xz
		URL_HASH SHA256=afd838069897a4a5246f178c5db95f68d0aa288cd71499970c74ebc8bced970f
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

elseif(WIN32)
	# Windows - fetch appropriate architecture
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(WASMTIME_WIN_ARCH "x86_64")
		set(WASMTIME_WIN_HASH "")  # TODO: Add hash when verified
	else()
		set(WASMTIME_WIN_ARCH "i686")
		set(WASMTIME_WIN_HASH "")  # TODO: Add hash when verified
	endif()

	FetchContent_Declare(wasmtime-c-api
		URL https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/wasmtime-v${WASMTIME_VERSION}-${WASMTIME_WIN_ARCH}-windows-c-api.zip
	)
	FetchContent_MakeAvailable(wasmtime-c-api)

	target_include_directories(wasmtime INTERFACE "${wasmtime-c-api_SOURCE_DIR}/include")
	target_link_libraries(wasmtime INTERFACE "${wasmtime-c-api_SOURCE_DIR}/lib/wasmtime.dll.lib")

	# Store DLL path for runtime copying
	set(WASMTIME_DLL "${wasmtime-c-api_SOURCE_DIR}/lib/wasmtime.dll" CACHE FILEPATH "Wasmtime DLL" FORCE)

elseif(UNIX)
	# Linux - fetch appropriate architecture
	if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
		set(WASMTIME_LINUX_ARCH "aarch64")
		set(WASMTIME_LINUX_HASH "")  # TODO: Add hash when verified
	else()
		set(WASMTIME_LINUX_ARCH "x86_64")
		set(WASMTIME_LINUX_HASH "")  # TODO: Add hash when verified
	endif()

	FetchContent_Declare(wasmtime-c-api
		URL https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/wasmtime-v${WASMTIME_VERSION}-${WASMTIME_LINUX_ARCH}-linux-c-api.tar.xz
	)
	FetchContent_MakeAvailable(wasmtime-c-api)

	target_include_directories(wasmtime INTERFACE "${wasmtime-c-api_SOURCE_DIR}/include")
	target_link_libraries(wasmtime INTERFACE "${wasmtime-c-api_SOURCE_DIR}/lib/libwasmtime.a")

else()
	message(FATAL_ERROR "Unsupported platform for Wasmtime")
endif()
