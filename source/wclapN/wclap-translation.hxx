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
	
	WclapMethods(Wclap &wclap) : wclap(wclap) {}

	struct plugin_factory : public clap_plugin_factory {
		NativeProxyContext context;
		std::unique_ptr<WclapArenas> arenas; // TODO: have a WCLAP global arena, shared between all factories?
		std::mutex mutex;
		
		std::vector<const clap_plugin_descriptor *> descriptorPointers;
		
		void assign(NativeProxyContext &&c, WclapThread &thread, WasmP factoryP) {
			this->get_plugin_count = native_get_plugin_count;
			this->get_plugin_descriptor = native_get_plugin_descriptor;
			this->create_plugin = native_create_plugin;

			context = std::move(c);
			arenas = std::unique_ptr<WclapArenas>{
				new WclapArenas(*c.wclap, thread)
			};
			
			auto wasmFactory = arenas.view<wclap_plugin_factory>(factoryP);
			auto getPluginCountFn = wasmFactory.get_plugin_count();
			auto getPluginDescFn = wasmFactory.get_plugin_descriptor();

			auto count = thread.callWasm_I(getPluginCountFn, context.wasmObjP);
			if (validity.lengths && count > validity.maxPlugins) {
				context.wclap->errorMessage = "plugin factory advertised too many plugins";
				descriptorPointers.clear();
				return;
			}
			
			descriptorPointers.clear();
			for (uint32_t i = 0; i < count; ++i) {
				auto wasmP = thread.callWasm_P(getPluginDescFn, context.wasmObjP, i);
				if (wasmP) {
					const clap_plugin_descriptor *desc;
					wasmToNative(*arenas, wasmP, desc);
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
			// Claim a thread exclusively for this plugin
			auto ownedThread = factory.context.wclap->claimRealtimeThread();
			auto &ownedArenas = ownedThread->arenas;

			WasmP wasmHostP, wasmPluginId, wasmPluginP;
			nativeToWasm(ownedArenas, host, wasmHostP); // persistent, tied to plugin lifetime
			{
				auto wasmReset = ownedArenas.scopedWasmReset();
				nativeToWasm(ownedArenas, plugin_id, wasmPluginId); // temporary
				auto createPluginFn = ownedArenas.view<wclap_plugin_factory>.create_plugin();
				wasmPluginP = ownedThread->callWasm_P(createPluginFn, wasmHostP, wasmPluginId);
			}
			clap_plugin_t *nativePluginP;
			wasnToNative(ownedArenas, wasmPluginP, nativePluginP);
			return nativePluginP;
		}
	};
	
	bool triedPluginFactory = false;
	std::unique_ptr<plugin_factory> pluginFactory;

	void * getFactory(const char *factory_id) {
		auto scoped = wclap.lockRelaxedThread();
		auto entryP = WasmP(scoped.thread.clapEntryP64);
		auto wasmEntry = wclap.view<wclap_plugin_entry>(entryP);
		
		auto getFactoryFn = wasmEntry.get_factory();
		WasmP factoryP;
		{
			auto reset = scoped.thread.arenas->scopedWasmReset();
			WasmP wasmStr;
			nativeToWasm(*scoped.thread.arenas, factory_id, wasmStr);
			factoryP = scoped.thread.callWasm_P(getFactoryFn, wasmStr);
		}
		if (!factoryP) return nullptr;

		if (!std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) {
			if (!triedPluginFactory) {
				triedPluginFactory = true;
				pluginFactory = std::unique_ptr<plugin_factory>(new plugin_factory());
				pluginFactory->assign({&wclap, factoryP}, scoped.thread, factoryP);
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
