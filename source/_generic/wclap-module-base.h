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
const clap_host * getHostFromPlugin(Plugin *plugin);

using MemoryArenaPool = wclap::MemoryArenaPool<Instance, WCLAP_BRIDGE_IS64>;
using MemoryArenaPtr = std::unique_ptr<wclap::MemoryArena<Instance, WCLAP_BRIDGE_IS64>>;

struct WclapModuleBase {
	std::unique_ptr<InstanceGroup> instanceGroup; // Destroyed last
	std::unique_ptr<Instance> mainThread;
	MemoryArenaPool arenaPool; // Goes next because other destructors might make WASM calls, but we need an Instance (most likely the main thread) for that

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
	~WclapModuleBase() {
	}

	wclap::IndexLookup<Plugin> pluginList;
	static const clap_host * getHost(void *context, Pointer<const wclap_host> host) {
		auto &self = *(WclapModuleBase *)context;
		auto *plugin = self.pluginList.get(host.wasmPointer);
		if (!plugin) return nullptr;
		return getHostFromPlugin(plugin);
	}
	
	wclap_host hostTemplate;
	static Pointer<const void> hostGetExtension(void *context, Pointer<const wclap_host> host, Pointer<const char> extId) {
		auto &self = *(WclapModuleBase *)context;
		// null, no extensions for now
		return {0};
	}
	static void hostRequestRestart(void *context, Pointer<const wclap_host> whost) {
		auto *host = getHost(context, whost);
		if (host) host->request_restart(host);
	}
	static void hostRequestProcess(void *context, Pointer<const wclap_host> whost) {
		auto *host = getHost(context, whost);
		if (host) host->request_process(host);
	}
	static void hostRequestCallback(void *context, Pointer<const wclap_host> whost) {
		auto *host = getHost(context, whost);
		if (host) host->request_callback(host);
	}

	wclap_host hostExtAudioPorts;
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
