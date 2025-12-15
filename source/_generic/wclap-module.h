// No `#pragma once`, because we deliberately get included multiple times by `../wclap.h`, with different WCLAP_API_NAMESPACE, WCLAP_BRIDGE_NAMESPACE and WCLAP_BRIDGE_IS64 values

#include "./wclap-module-base.h"

#include "./wclap-plugin-factory.h"

namespace WCLAP_BRIDGE_NAMESPACE {

using namespace WCLAP_API_NAMESPACE;

struct WclapModule : public WclapModuleBase {
	
	template<class Return, class ...Args>
	bool registerHost(Instance *instance, Function<Return, Args...> &wasmFn, Return (*fn)(void *, Args...)) {
		auto prevIndex = wasmFn.wasmPointer;
		auto index = registerHostFunction(instance, (void *)this, fn);
		if (index.wasmPointer == -1) {
			setError("failed to register function");
			return false;
		}
		if (prevIndex != 0 && index.wasmPointer != prevIndex) {
			// This is when we've previously registered it on another thread, and it needs to match
			setError("function index mismatch");
			return false;
		}
		return true;
	}
	
	bool addHostFunctions(Instance *instance) {
		if (!registerHost(instance, hostTemplate.get_extension, hostGetExtension)) return false;
		if (!registerHost(instance, hostTemplate.request_restart, hostRequestRestart)) return false;
		if (!registerHost(instance, hostTemplate.request_process, hostRequestProcess)) return false;
		if (!registerHost(instance, hostTemplate.request_callback, hostRequestCallback)) return false;
		return true;
	}
	
	WclapModule(InstanceGroup *instanceGroup) : WclapModuleBase(instanceGroup) {
		if (!addHostFunctions(mainThread.get())) return;

		mainThread->init();
		if constexpr (WCLAP_BRIDGE_IS64) {
			entryPtr = {Size(mainThread->entry64.wasmPointer)};
		} else {
			entryPtr = {Size(mainThread->entry32.wasmPointer)};
		}
		if (!entryPtr) {
			setError("clap_entry is NULL");
			return;
		}
		
		auto scoped = arenaPool.scoped();
		auto pathStr = scoped.writeString(mainThread->path());
		auto version = mainThread->get(entryPtr[&wclap_plugin_entry::wclap_version]);
		clapVersion = {version.major, version.minor, version.revision};

		if (!mainThread->call(entryPtr[&wclap_plugin_entry::init], pathStr)) {
			setError("clap_entry::init() returned false");
			return;
		}
		
		hasError = false;
	}

	std::optional<PluginFactory> pluginFactory;
	
	void * getFactory(const char *factoryId) {
		if (!std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) {
			if (!pluginFactory) {
				auto scoped = arenaPool.scoped();
				auto wclapStr = scoped.writeString(CLAP_PLUGIN_FACTORY_ID);
				auto factoryPtr = mainThread->call(entryPtr[&wclap_plugin_entry::get_factory], wclapStr);
				pluginFactory.emplace(PluginFactory{*this, factoryPtr.cast<wclap_plugin_factory>()});
			}
			if (!pluginFactory->ptr) return nullptr;
			return &pluginFactory->clapFactory;
		}
		return nullptr;
	}
};

}; // namespace
