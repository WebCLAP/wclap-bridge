// No `#pragma once`, because we deliberately get included multiple times by `../wclap.h`, with different WCLAP_API_NAMESPACE, WCLAP_BRIDGE_NAMESPACE and WCLAP_BRIDGE_IS64 values

#include "./wclap-module-base.h"

#include "./wclap-plugin-factory.h"

namespace WCLAP_BRIDGE_NAMESPACE {

using namespace WCLAP_API_NAMESPACE;

struct WclapModule : public WclapModuleBase {
	WclapModule(Instance *instance) : WclapModuleBase(instance) {
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

	std::optional<PluginFactory> pluginFactory;
	
	void * getFactory(const char *factoryId) {
		if (!std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) {
			if (!pluginFactory) {
				auto scoped = arenaPool.scoped();
				auto wclapStr = scoped.writeString(CLAP_PLUGIN_FACTORY_ID);
				auto factoryPtr = instance->call(entryPtr[&wclap_plugin_entry::get_factory], wclapStr);
				pluginFactory.emplace(PluginFactory{*this, factoryPtr.cast<wclap_plugin_factory>()});
			}
			if (!pluginFactory->ptr) return nullptr;
			return &pluginFactory->clapFactory;
		}
		return nullptr;
	}
};

}; // namespace
