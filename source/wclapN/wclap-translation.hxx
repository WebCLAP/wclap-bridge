/// NO #pragma once or include guard - this is included twice from within two different namespaces
#ifndef WCLAP_MULTIPLE_INCLUDES_NAMESPACE
#	error must not be included directly
#endif
// The matching `wclap-translation.h` will already be included

#include "../validity.h"
#include "../wclap-thread.h"

#include "clap/all.h"
#include <vector>

namespace wclap { namespace WCLAP_MULTIPLE_INCLUDES_NAMESPACE {

struct WclapMethods {
	Wclap &wclap;
	
	WclapMethods(Wclap &wclap) : wclap(wclap) {
		uint64_t funcIndex;
		int32_t success;
		auto global = wclap.lockGlobalThread();
		auto wasmEntry = wclap.view<wclap_plugin_entry>(global.thread.clapEntryP64);
		auto initFn = wasmEntry.init();
		auto reset = global.arenas.scopedWasmReset();
		WasmP wasmStr = nativeToWasm(global.arenas, "/plugin/");
		success = global.thread.callWasm_I(initFn, wasmStr);
		if (!success) {
			wclap.errorMessage = "clap_entry.init() returned false";
			return;
		}
	}

	struct plugin_factory : public clap_plugin_factory {
		Wclap &wclap;
		WasmP factoryObjP;
	
		std::vector<const clap_plugin_descriptor *> descriptorPointers;
		
		plugin_factory(Wclap &wclap, WasmP factoryObjP) : wclap(wclap), factoryObjP(factoryObjP) {
			this->get_plugin_count = native_get_plugin_count;
			this->get_plugin_descriptor = native_get_plugin_descriptor;
			this->create_plugin = native_create_plugin;

			auto global = wclap.lockGlobalThread();
			
			auto wasmFactory = wclap.view<wclap_plugin_factory>(factoryObjP);
			auto getPluginCountFn = wasmFactory.get_plugin_count();
			auto getPluginDescFn = wasmFactory.get_plugin_descriptor();

			auto count = global.thread.callWasm_I(getPluginCountFn, factoryObjP);
			if (validity.lengths && count > validity.maxPlugins) {
				wclap.errorMessage = "plugin factory advertised too many plugins";
				descriptorPointers.clear();
				return;
			}
			
			descriptorPointers.clear();
			for (uint32_t i = 0; i < count; ++i) {
				auto wasmP = global.thread.callWasm_P(getPluginDescFn, factoryObjP, i);
				if (wasmP) {
					const clap_plugin_descriptor *desc;
					wasmToNative(global.arenas, wasmP, desc);
					descriptorPointers.push_back(desc);
				} else if (!validity.filterOnlyWorking) {
					descriptorPointers.push_back(nullptr);
				}
			}
		}
	
		static uint32_t native_get_plugin_count(const struct clap_plugin_factory *obj) {
			auto &factory = *(const plugin_factory *)obj;
			return uint32_t(factory.descriptorPointers.size());
		}
		static const clap_plugin_descriptor_t * native_get_plugin_descriptor(const struct clap_plugin_factory *obj, uint32_t index) {
			auto &factory = *(const plugin_factory *)obj;
			if (index < factory.descriptorPointers.size()) {
				return factory.descriptorPointers[index];
			}
			return nullptr;
		}
		static const clap_plugin_t * native_create_plugin(const struct clap_plugin_factory *obj, const clap_host *host, const char *plugin_id) {
			auto &factory = *(const plugin_factory *)obj;
			auto &wclap = factory.wclap;
			auto createPluginFn = factory.wclap.view<wclap_plugin_factory>(factory.factoryObjP).create_plugin();

			// Claim thread/arenas to be owned by this plugin, and released (returned to the pool) when it's destroyed
			auto rtThread = wclap.claimRealtimeThread();
			auto arenas = wclap.claimArenas(rtThread.get());

			// Proxy the host, and make it persistent
			WasmP wasmHostP = nativeToWasm(*arenas, host);
			setWasmProxyContext<wclap_host>(*arenas, wasmHostP);
			arenas->persistWasm();
			
			// Attempt to create the plugin;
			WasmP wasmPluginP;
			{
				auto wasmReset = arenas->scopedWasmReset();
				auto wasmPluginId = nativeToWasm(*arenas, plugin_id);
				wasmPluginP = rtThread->callWasm_P(createPluginFn, factory.factoryObjP, wasmHostP, wasmPluginId);
			}
			if (!wasmPluginP) {
				wclap.returnRealtimeThread(rtThread);
				wclap.returnArenas(arenas);
				return nullptr;
			}
			auto *nativePlugin = wasmToNative<const clap_plugin>(*arenas, wasmPluginP);
			arenas->persistNative();
			
			nativeProxyContextFor(nativePlugin) = {&wclap, wasmPluginP, std::move(arenas), std::move(rtThread)};
			return nativePlugin;
		}
	};
	
	bool triedPluginFactory = false;
	std::unique_ptr<plugin_factory> pluginFactory;

	void * getFactory(const char *factory_id) {
		WasmP factoryP;
		{
			auto scoped = wclap.lockRelaxedThread();
			auto entryP = WasmP(scoped.thread.clapEntryP64);
			auto wasmEntry = wclap.view<wclap_plugin_entry>(entryP);
			auto getFactoryFn = wasmEntry.get_factory();

			auto reset = scoped.arenas.scopedWasmReset();
			WasmP wasmStr = nativeToWasm(scoped.arenas, factory_id);
			factoryP = scoped.thread.callWasm_P(getFactoryFn, wasmStr);
		}
		if (!factoryP) return nullptr;

		if (!std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) {
			if (!triedPluginFactory) {
				triedPluginFactory = true;
				pluginFactory = std::unique_ptr<plugin_factory>(new plugin_factory(wclap, factoryP));
			}
			return pluginFactory.get();
		}
		return nullptr;
	}
	
	struct {
		static uint32_t some_dummy_method_fn() {
			return 5;
		}
		WasmP some_dummy_method = 0;

		void registerMethods(WclapThread &thread) {
			// This should add to the WASM instance's function table, and store (or compare) the index, so it can be used
			//thread.addHostMethod_I(some_dummy_method, some_dummy_method_fn);
		}
	} host_example_struct_dummy;
	
	void registerHostMethods(WclapThread &thread) {
		host_example_struct_dummy.registerMethods(thread);
	}
};

WclapMethods * methodsCreate(Wclap &wclap) {
	return new WclapMethods(wclap);
}
void methodsDelete(WclapMethods *methods) {
	delete methods;
}
void methodsRegister(WclapMethods *methods, WclapThread &thread) {
	methods->registerHostMethods(thread);
}
void * methodsGetFactory(WclapMethods *methods, const char *factoryId) {
	return methods->getFactory(factoryId);
}

//--------------

template<>
void nativeToWasm<const char>(WclapArenas &arenas, const char *str, WasmP &wasmP) {
	if (!str) {
		wasmP = 0;
		return;
	}
	size_t length = std::strlen(str);
	if (validity.lengths && length > validity.maxStringLength) {
		length = validity.maxStringLength;
	}
	wasmP = arenas.wasmBytes(length + 1);
	auto *nativeTmp = (char *)arenas.wasmMemory(wasmP);
	for (size_t i = 0; i < length; ++i) {
		nativeTmp[i] = str[i];
	}
	nativeTmp[length] = 0;
}

template<>
void wasmToNative<const char>(WclapArenas &arenas, WasmP wasmStr, const char *&str) {
	if (!wasmStr) {
		str = nullptr;
		return;
	}
	auto *nativeInWasm = (char *)arenas.wasmMemory(wasmStr);
	size_t length = std::strlen(nativeInWasm);
	if (validity.lengths && length > validity.maxStringLength) {
		length = validity.maxStringLength;
	}
	auto *nativeTmp = (char *)arenas.nativeBytes(length + 1);
	for (size_t i = 0; i < length; ++i) {
		nativeTmp[i] = nativeInWasm[i];
	}
	nativeTmp[length] = 0;
	str = nativeTmp;
}

template<>
void wasmToNative<const char * const>(WclapArenas &arenas, WasmP stringList, const char * const * &features) {
	if (!stringList) {
		features = nullptr;
		return;
	}
	// Null-terminated array of strings
	size_t count = 0;
	WasmP *wasmStrArray = (WasmP *)arenas.wasmMemory(stringList);
	while (wasmStrArray[count] && (!validity.lengths || count < validity.maxFeaturesLength)) {
		++count;
	}
	auto *nativeStrArray = (const char **)arenas.nativeBytes(sizeof(char *)*(count + 1));
	for (size_t i = 0; i < count; ++i) {
		wasmToNative(arenas, wasmStrArray[count], nativeStrArray[i]);
	}
	nativeStrArray[count] = nullptr;
	features = nativeStrArray;
}

//--------------

template<>
void wasmToNative<const clap_plugin_descriptor>(WclapArenas &arenas, WasmP wasmP, const clap_plugin_descriptor *&nativeP) {
	generated_wasmToNative(arenas, wasmP, nativeP);
	// TODO: validity checks for the mandatory fields, maybe replace optional ones with ""
}

}} // namespace
