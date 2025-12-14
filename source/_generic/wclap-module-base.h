// No `#pragma once`, because we deliberately get included multiple times by `../wclap.h`, with different WCLAP_API_NAMESPACE, WCLAP_BRIDGE_NAMESPACE and WCLAP_BRIDGE_IS64 values

#include "clap/all.h"

#include "wclap/wclap.hpp"
#include "wclap/instance.hpp"
#include "wclap/memory-arena.hpp"
#include "wclap/index-lookup.hpp"

#include "../instance.h"

namespace WCLAP_BRIDGE_NAMESPACE {

using namespace WCLAP_API_NAMESPACE;

class Plugin;

using MemoryArenaPool = wclap::MemoryArenaPool<Instance, WCLAP_BRIDGE_IS64>;
using MemoryArenaPtr = std::unique_ptr<wclap::MemoryArena<Instance, WCLAP_BRIDGE_IS64>>;

struct WclapModuleBase {
	std::unique_ptr<Instance> instance; // Destroyed last
	MemoryArenaPool arenaPool; // Goes next because other destructors might make WASM calls, but we need the Instance for that

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

	WclapModuleBase(Instance *instance) : instance(instance), arenaPool(instance) {}
	~WclapModuleBase() {
	}

	wclap::IndexLookup<Plugin> pluginList;
	
	wclap_host hostTemplate;
};

template <typename T>
struct ClapPluginMethodHelper;

// Returns a plain-C function which calls a given C++ method
template<auto methodPtr>
auto clapPluginMethod() {
	using C = ClapPluginMethodHelper<decltype(methodPtr)>;
	return C::template callMethod<methodPtr>;
}

// Partial specialisation used to expand the method signature
template <class Object, typename Return, typename... Args>
struct ClapPluginMethodHelper<Return (Object::*)(Args...)> {
	// Templated static method which forwards to a specific method
	template<Return (Object::*methodPtr)(Args...)>
	static Return callMethod(const clap_plugin *plugin, Args... args) {
		auto *obj = (Object *)plugin->plugin_data;
		return (obj->*methodPtr)(args...);
	}
};

}; // namespace
