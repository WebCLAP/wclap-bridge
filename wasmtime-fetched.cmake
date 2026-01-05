include(FetchContent)
#set(FETCHCONTENT_QUIET False)
cmake_policy(SET CMP0135 NEW) # sets timestamps to extraction time, so we always rebuild when redownloading

add_library(wasmtime INTERFACE)

message(STATUS "checking/downloading Wasmtime C SDK")
if(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
	# Fetch both AMD64 and ARM64
	FetchContent_Declare(wasmtime-c-api-amd64
		URL https://github.com/bytecodealliance/wasmtime/releases/download/v39.0.1/wasmtime-v39.0.1-x86_64-macos-c-api.tar.xz
		URL_HASH SHA256=0ff4f203106fe2b9b7b940a670282c41b01450b44c2157693a5bfb4848391a09
	)
	FetchContent_Declare(wasmtime-c-api-arm64
		URL https://github.com/bytecodealliance/wasmtime/releases/download/v39.0.1/wasmtime-v39.0.1-aarch64-macos-c-api.tar.xz
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
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	if(NOT ${CMAKE_SYSTEM_PROCESSOR} STREQUAL "AMD64")
		message(FATAL_ERROR "Only x86_64 (64-bit) Windows supported - we need Cranelift for Wasmtime")
	endif()

	FetchContent_Declare(wasmtime-c-api
		URL https://github.com/bytecodealliance/wasmtime/releases/download/v39.0.1/wasmtime-v39.0.1-x86_64-windows-c-api.zip
		URL_HASH SHA256=4939453176b7a86cf4f2cd9839da100a4e30f0505a4a471f2dba92a012e4975c
	)
	FetchContent_MakeAvailable(wasmtime-c-api)
	target_include_directories(wasmtime INTERFACE "${wasmtime-c-api_SOURCE_DIR}/include")
	target_link_libraries(wasmtime INTERFACE "${wasmtime-c-api_SOURCE_DIR}/lib/wasmtime.lib")
	# as suggested in wasmtime.h for static linking on Windows
	target_compile_definitions(wasmtime INTERFACE WASM_API_EXTERN= WASI_API_EXTERN=)
	target_link_libraries(wasmtime INTERFACE ws2_32.lib advapi32.lib userenv.lib ntdll.lib shell32.lib ole32.lib bcrypt.lib)
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
	if(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "x86_64")
		FetchContent_Declare(wasmtime-c-api
			URL https://github.com/bytecodealliance/wasmtime/releases/download/v39.0.1/wasmtime-v39.0.1-x86_64-linux-c-api.tar.xz
			URL_HASH SHA256=f1e2b41f9c1d8d097506ad584e7d56140ebfdc6b6e5ae7cbfc8c829dfff3487e
		)
	elseif(${CMAKE_SYSTEM_PROCESSOR} STREQUAL "aarch64")
		FetchContent_Declare(wasmtime-c-api
			URL https://github.com/bytecodealliance/wasmtime/releases/download/v39.0.1/wasmtime-v39.0.1-aarch64-linux-c-api.tar.xz
			URL_HASH SHA256=959f9ca5ccd2c9d997576a2ba460e9df529a7705e84dbdc8ab89ae9e0fdd1d51
		)
	else()
		message(FATAL_ERROR "Unsupported Linux architecture (${CMAKE_SYSTEM_PROCESSOR}) - add to wasmtime-fetched.cmake")
	endif()

	FetchContent_MakeAvailable(wasmtime-c-api)
	target_include_directories(wasmtime INTERFACE "${wasmtime-c-api_SOURCE_DIR}/include")
	target_link_libraries(wasmtime INTERFACE "${wasmtime-c-api_SOURCE_DIR}/lib/libwasmtime.a")
else()
	message(FATAL_ERROR "Unsupported OS: add the OS-specific resources to wasmtime-fetched.cmake")
endif()
