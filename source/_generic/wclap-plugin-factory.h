// No `#pragma once`, because we deliberately get included multiple times by `../wclap.h`, with different WCLAP_API_NAMESPACE, WCLAP_BRIDGE_NAMESPACE and WCLAP_BRIDGE_IS64 values

#include "./wclap-plugin.h"
#include "../config.h"

#include <string_view>

namespace WCLAP_BRIDGE_NAMESPACE {

using namespace WCLAP_API_NAMESPACE;

struct PluginFactory {
	clap_plugin_factory clapFactory{
		.get_plugin_count=get_plugin_count,
		.get_plugin_descriptor=get_plugin_descriptor,
		.create_plugin=create_plugin
	};

	WclapModuleBase &module;
	Pointer<wclap_plugin_factory> ptr;
	
	std::vector<std::unique_ptr<std::string>> strings;
	std::vector<std::vector<const char *>> featureArrays;
	std::vector<clap_plugin_descriptor> descriptors;
	
	const char * readString(Pointer<const char> ptr, const char *nullValue=nullptr, const char *prefix="", const char *suffix="") {
		if (!ptr) return nullValue;
		auto str = prefix + module.mainThread->getString(ptr, 2048) + suffix;
		strings.emplace_back(std::unique_ptr<std::string>{new std::string(std::move(str))});
		return strings.back()->data();
	}

	clap_plugin *createPlugin(const clap_host *host, const char *pluginId) const {
		const clap_plugin_descriptor *desc = nullptr;
		for (auto &d : descriptors) {
			if (!std::strcmp(d.id, pluginId)) {
				desc = &d;
				break;
			}
		}
		if (!desc) return nullptr;
		
		auto scoped = module.arenaPool.scoped();

		// In order to get to this point, it must've started with the WCLAP prefix (if defined), so skip it
		pluginId += wclap_bridge::pluginIdPrefix.size();
		auto strPtr = scoped.writeString(pluginId);
		auto hostPtr = scoped.copyAcross(module.hostTemplate);
		auto pluginPtr = module.mainThread->call(ptr[&wclap_plugin_factory::create_plugin], ptr, hostPtr, strPtr);
		if (!pluginPtr) return nullptr;
		auto *plugin = new Plugin(module, host, hostPtr, pluginPtr, scoped.commit(), desc);
		return &plugin->clapPlugin;
	}

	PluginFactory(WclapModuleBase &module, Pointer<wclap_plugin_factory> ptr) : module(module), ptr(ptr) {
		auto &instance = *module.mainThread;
		// Enumerate all the descriptors up-front
		auto count = instance.call(ptr[&wclap_plugin_factory::get_plugin_count], ptr);
		for (uint32_t i = 0; i < count; ++i) {
			auto descPtr = instance.call(ptr[&wclap_plugin_factory::get_plugin_descriptor], ptr, i);
			if (!descPtr) continue;
			auto wclapDesc = module.mainThread->get(descPtr);
			descriptors.push_back({
				.clap_version{
					.major=wclapDesc.wclap_version.major,
					.minor=wclapDesc.wclap_version.minor,
					.revision=wclapDesc.wclap_version.revision,
				},
				.id=readString(wclapDesc.id, "unknown-clap-id", wclap_bridge::pluginIdPrefix.c_str()),
				.name=readString(wclapDesc.name, "Unknown CLAP plugin", wclap_bridge::pluginNamePrefix.c_str(), wclap_bridge::pluginNameSuffix.c_str()),
				.vendor=readString(wclapDesc.vendor),
				.url=readString(wclapDesc.url),
				.manual_url=readString(wclapDesc.manual_url),
				.support_url=readString(wclapDesc.support_url),
				.version=readString(wclapDesc.version),
				.description=readString(wclapDesc.description)
			});
			auto &desc = descriptors.back();

			featureArrays.emplace_back();
			auto &featureArray = featureArrays.back();
			for (size_t featureIndex = 0; featureIndex < 1000; ++featureIndex) {
				auto featureStr = instance.get(wclapDesc.features, featureIndex);
				if (!featureStr) break;
				featureArray.push_back(readString(featureStr));
			}
			featureArray.push_back(nullptr); // feature array is itself null-terminated
			desc.features = featureArray.data();
		}
	}
	
	static uint32_t get_plugin_count(const clap_plugin_factory *factory) {
		auto &self = *(const PluginFactory *)factory;
		return uint32_t(self.descriptors.size());
	}
	static const clap_plugin_descriptor * get_plugin_descriptor(const clap_plugin_factory *factory, uint32_t index) {
		auto &self = *(const PluginFactory *)factory;
		if (index >= self.descriptors.size()) return nullptr;
		return &self.descriptors[index];
	}
	static const clap_plugin_t * create_plugin(const clap_plugin_factory *factory, const clap_host *host, const char *plugin_id) {
		auto &self = *(const PluginFactory *)factory;
		return self.createPlugin(host, plugin_id);
	}
};

}; // namespace
