// No `#pragma once`, because we deliberately get included multiple times by `../wclap.h`, with different WCLAP_API_NAMESPACE, WCLAP_BRIDGE_NAMESPACE and WCLAP_BRIDGE_IS64 values

#include "clap/all.h"

#include "wclap/wclap.hpp"
#include "wclap/instance.hpp"
#include "wclap/memory-arena.hpp"
#include "wclap/index-lookup.hpp"

#include "../instance.h"

#include <thread>

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

	std::atomic<bool> hasError = false;
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

	WclapModuleBase(InstanceGroup *instanceGroup) : instanceGroup(instanceGroup), mainThread(instanceGroup->startInstance()), arenaPool(mainThread.get()) {
		if (!hasError) {
			auto error = instanceGroup->error();
			if (error) setError(*error);
		}
		threads.emplace_back(); // first entry is empty, because we address threads by index, and 0 is a reserved thread ID
	}
	WclapModuleBase(const WclapModuleBase &other) = delete;
	~WclapModuleBase() {
		auto checkThreadsStopped = [&]() -> bool {
			bool allStopped = true;
			
			auto lock = threadLock();
			for (auto &thread : threads) {
				if (!thread) continue;
				thread->instance->requestStop();
				allStopped = false;
			}
			return allStopped;
		};
		while (!checkThreadsStopped()) {
			std::this_thread::yield();
		}
	}

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

	// Other constants
	Pointer<const char> wclapPortMonoPtr, wclapPortStereoPtr, wclapPortSurroundPtr, wclapPortAmbisonicPtr, wclapPortOtherPtr;
	Pointer<const char> translatePortType(const char *portType) {
		if (!std::strcmp(portType, CLAP_PORT_MONO)) {
			return wclapPortMonoPtr;
		} else if (!std::strcmp(portType, CLAP_PORT_STEREO)) {
			return wclapPortStereoPtr;
		} else if (!std::strcmp(portType, CLAP_PORT_SURROUND)) {
			return wclapPortSurroundPtr;
		} else if (!std::strcmp(portType, CLAP_PORT_AMBISONIC)) {
			return wclapPortAmbisonicPtr;
		}
		return wclapPortOtherPtr;
	}

	struct Thread {
		uint32_t index;
		uint64_t threadArg;
		
		std::thread thread;
		std::unique_ptr<Instance> instance;
		
		~Thread() {
			// This should block for at most the WASM function-call timeout period
			if (thread.joinable()) {
				instance->requestStop();
				thread.join();
			}
		}
	};
	std::mutex threadMutex;
	std::lock_guard<std::mutex> threadLock() {
		return std::lock_guard<std::mutex>{threadMutex};
	}
	std::vector<std::unique_ptr<Thread>> threads;
	static void runThread(WclapModuleBase *module, size_t index) {
		Thread *thread;
		{
			auto lock = module->threadLock();
			thread = module->threads[index].get();
		}
		
		LOG_EXPR("WCLAP thread starting");
		
		thread->instance->runThread(thread->index, thread->threadArg);

		// Remove ourselves from the thread list
		LOG_EXPR("WCLAP thread finished");
		auto lock = module->threadLock();
		thread->thread.detach(); // this is the thread running this function, so calling `.join()` from here would break, but we're about to finish anyway
		// The thread vector doesn't get destroyed until all the threads have stopped, so this is safe
		module->threads[index] = nullptr;
	}
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
