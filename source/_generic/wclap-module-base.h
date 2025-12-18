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
using MemoryArenaScope = typename wclap::MemoryArena<Instance, WCLAP_BRIDGE_IS64>::Scoped;

struct WclapModuleBase {
	std::unique_ptr<InstanceGroup> instanceGroup; // Destroyed last
	std::unique_ptr<Instance> mainThread;
	MemoryArenaPool arenaPool; // Goes next because other destructors might make WASM calls, but we need an Instance (most likely the main thread) for that
	MemoryArenaPtr globalArena; // stores data common across all plugin instances

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
			auto instanceError = instanceGroup->error();
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

	WclapModuleBase(InstanceGroup *instanceGroup) : instanceGroup(instanceGroup), mainThread(instanceGroup->startInstance()), arenaPool(mainThread.get()) {}
	~WclapModuleBase() {}

	wclap::IndexLookup<Plugin> pluginList;
	
	// Use the `void *` context pointer of a struct to find the `Plugin`.
	static Plugin * getPlugin(void *context, Pointer<const wclap_host> host) {
		auto &self = *(WclapModuleBase *)context;
		auto dataPtr = self.mainThread->get(host[&wclap_host::host_data]);
		return self.pluginList.get(dataPtr.wasmPointer);
	}
	static Plugin * getPlugin(void *context, Pointer<const wclap_input_events> events) {
		auto &self = *(WclapModuleBase *)context;
		auto ctxPtr = self.mainThread->get(events[&wclap_input_events::ctx]);
		return self.pluginList.get(ctxPtr.wasmPointer);
	}
	static Plugin * getPlugin(void *context, Pointer<const wclap_output_events> events) {
		auto &self = *(WclapModuleBase *)context;
		auto ctxPtr = self.mainThread->get(events[&wclap_output_events::ctx]);
		return self.pluginList.get(ctxPtr.wasmPointer);
	}
	static Plugin * getPlugin(void *context, Pointer<const wclap_istream> stream) {
		auto &self = *(WclapModuleBase *)context;
		auto ctxPtr = self.mainThread->get(stream[&wclap_istream::ctx]);
		return self.pluginList.get(ctxPtr.wasmPointer);
	}
	static Plugin * getPlugin(void *context, Pointer<const wclap_ostream> stream) {
		auto &self = *(WclapModuleBase *)context;
		auto ctxPtr = self.mainThread->get(stream[&wclap_ostream::ctx]);
		return self.pluginList.get(ctxPtr.wasmPointer);
	}

	void setPlugin(Pointer<const wclap_host> host, uint32_t pluginListIndex) {
		mainThread->set(host[&wclap_host::host_data], {Size(pluginListIndex)});
	}
	void setPlugin(Pointer<const wclap_input_events> events, uint32_t pluginListIndex) {
		mainThread->set(events[&wclap_input_events::ctx], {Size(pluginListIndex)});
	}
	void setPlugin(Pointer<const wclap_output_events> events, uint32_t pluginListIndex) {
		mainThread->set(events[&wclap_output_events::ctx], {Size(pluginListIndex)});
	}
	void setPlugin(Pointer<const wclap_istream> stream, uint32_t pluginListIndex) {
		mainThread->set(stream[&wclap_istream::ctx], {Size(pluginListIndex)});
	}
	void setPlugin(Pointer<const wclap_ostream> stream, uint32_t pluginListIndex) {
		mainThread->set(stream[&wclap_ostream::ctx], {Size(pluginListIndex)});
	}

	// These will get filled with registered host functions.  If you put the `pluginList` index into the context pointer (as above) they will forward calls to the appropriate `Plugin`.
	wclap_host hostTemplate;
	wclap_input_events inputEventsTemplate;
	wclap_output_events outputEventsTemplate;
	wclap_istream istreamTemplate;
	wclap_ostream ostreamTemplate;
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
