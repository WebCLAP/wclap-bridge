// No `#pragma once`, because we deliberately get included multiple times by `../wclap.h`, with different WCLAP_API_NAMESPACE, WCLAP_BRIDGE_NAMESPACE and WCLAP_BRIDGE_IS64 values

#include "clap/all.h"

#include "wclap/wclap.hpp"
#include "wclap/instance.hpp"
#include "wclap/memory-arena.hpp"
#include "wclap/index-lookup.hpp"

#include "../instance.h"

namespace WCLAP_BRIDGE_NAMESPACE {

using namespace WCLAP_API_NAMESPACE;

using MemoryArenaPool = wclap::MemoryArenaPool<Instance, WCLAP_BRIDGE_IS64>;

struct Wclap {
	std::unique_ptr<Instance> instance;
	MemoryArenaPool arenaPool;
	
	std::atomic<bool> hasError = true;
	std::string errorMessage = "not initialised";
	std::mutex errorMutex;
	void setError(const std::string &error) {
		std::lock_guard<std::mutex> lock{errorMutex};
		hasError = true;
		errorMessage = error;
	}

	bool getError(char *buffer, size_t bufferLength) {
		if (!hasError) {
			auto instanceError = instance->error();
			if (!instanceError) return false;
			setError(*instanceError);
		}
		std::lock_guard<std::mutex> lock{errorMutex};
		if (bufferLength > 0) {
			std::strncpy(buffer, errorMessage.c_str(), bufferLength - 1);
		}
		buffer[bufferLength - 1] = 0; // guarantee null-termination
		return true;
	}
	
	clap_version clapVersion = {0, 0, 0};
	Pointer<const wclap_plugin_entry> entryPtr;
		
	Wclap(Instance *instance) : instance(instance), arenaPool(instance) {
		// TODO: add host functions

		instance->init();
		if (instance->entry32) {
			entryPtr = {Size(instance->entry32.wasmPointer)};
		} else {
			entryPtr = {Size(instance->entry64.wasmPointer)};
		}
		if (!entryPtr) {
			setError("clap_entry is NULL");
			return;
		}
		
		auto scoped = arenaPool.scoped();
		auto pathStr = scoped.writeString(instance->path());
		auto version = instance->get(entryPtr[&wclap_plugin_entry::wclap_version]);
		clapVersion = {version.major, version.minor, version.revision};

		if (!instance->call(entryPtr[&wclap_plugin_entry::init], pathStr)) {
			setError("clap_entry::init() returned false");
			return;
		}
		
		hasError = false;
	}
	
	void * getFactory(const char *factoryId) {
		return nullptr;
	}
};

}; // namespace
