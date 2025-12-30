cmake_minimum_required(VERSION 3.28)

project(wclap-bridge-plugin VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

if (APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET 10.15)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64")
    enable_language(OBJCXX)
endif ()

################# CLAP wrapper

# CLAP wrapper
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE)
set(CLAP_WRAPPER_DONT_ADD_TARGETS TRUE)
include(FetchContent)
FetchContent_Declare(
	clap-wrapper
	GIT_REPOSITORY https://github.com/free-audio/clap-wrapper
	GIT_TAG "v0.12.1"
	GIT_SHALLOW ON
)
FetchContent_MakeAvailable(clap-wrapper)

################# Other deps

FetchContent_Declare(
    cpp-semver
    GIT_REPOSITORY https://github.com/z4kn4fein/cpp-semver.git
    GIT_TAG v0.4.0)
FetchContent_MakeAvailable(cpp-semver)

################# Bridge plugin

# Plugin identifier / target prefix
set(PLUGIN wclap-bridge-plugin)

add_subdirectory(../ parent-build)

add_library(${PLUGIN}_static STATIC)
target_link_libraries(${PLUGIN}_static PUBLIC
	clap
	wclap-bridge
	semver
)
target_sources(${PLUGIN}_static PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/source/wclap-bridge-plugin.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/source/dump-stack-trace.cpp
)

make_clapfirst_plugins(
	TARGET_NAME ${PLUGIN}
	IMPL_TARGET ${PLUGIN}_static

	OUTPUT_NAME "${PLUGIN}"
	ENTRY_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/source/clap_entry.cpp"

	BUNDLE_IDENTIFIER "uk.co.signalsmith.clap.${PLUGIN}"
	BUNDLE_VERSION "1.0.0"
	WINDOWS_FOLDER_VST3 TRUE

	PLUGIN_FORMATS CLAP VST3 # AUV2
	COPY_AFTER_BUILD FALSE

	AUV2_MANUFACTURER_NAME "Signalsmith Audio"
	AUV2_MANUFACTURER_CODE "SigA"
	AUV2_SUBTYPE_CODE "Wclp" # plugin ID
	AUV2_INSTRUMENT_TYPE "aufx" # kAudioUnitType_Effect
)