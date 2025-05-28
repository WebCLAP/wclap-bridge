/// NO #pragma once or include guard - this is included twice from within two different namespaces
#ifndef WCLAP_MULTIPLE_INCLUDES_NAMESPACE
#	error must not be included directly
#endif

#include "../validity.h"
#include "../wclap-thread.h"

#include "clap/all.h"
#include <vector>

namespace wclap { namespace WCLAP_MULTIPLE_INCLUDES_NAMESPACE {

struct WclapContext {
	WasmP wasmP = 0;
	Wclap *wclap = nullptr;
};

struct WclapMethods {
	Wclap &wclap;
	
	WclapMethods(Wclap &wclap) : wclap(wclap) {}

	struct plugin_factory : public clap_plugin_factory {
		WclapContext context;
		std::unique_ptr<WclapArenas> arenas;
		
		std::vector<clap_plugin_descriptor *> descriptorPointers;
		
		void assign(const WclapContext &c, WclapThread &thread, wclap_plugin_factory wasmFactory) {
			this->get_plugin_count = native_get_plugin_count;
			this->get_plugin_descriptor = native_get_plugin_descriptor;
			this->create_plugin = native_create_plugin;

			context = c;
			arenas = std::unique_ptr<WclapArenas>{
				new WclapArenas(*c.wclap, thread)
			};

			auto count = thread.callWasm_I(wasmFactory.get_plugin_count(), context.wasmP);
			if (validity.lengths && count > validity.maxPlugins) {
				context.wclap->errorMessage = "plugin factory advertised too many plugins";
				descriptorPointers.clear();
				return;
			}
			
			descriptorPointers.clear();
			for (uint32_t i = 0; i < count; ++i) {
				auto wasmP = thread.callWasm_P(wasmFactory.get_plugin_descriptor(), context.wasmP, i);
				if (wasmP) {
					auto view = c.wclap->view<wclap_plugin_descriptor>(wasmP);
					if (view) {
						descriptorPointers.push_back(wasmToNative(*arenas, view));
					} else if (!validity.filterOnlyWorking) {
						descriptorPointers.push_back(nullptr);
					}
				}
			}
		}
	
		static uint32_t native_get_plugin_count(const struct clap_plugin_factory *obj) {
			auto &factory = *(plugin_factory *)obj;
			return uint32_t(factory.descriptorPointers.size());
		}
		static const clap_plugin_descriptor_t * native_get_plugin_descriptor(const struct clap_plugin_factory *obj, uint32_t index) {
			auto &factory = *(plugin_factory *)obj;
			if (index < factory.descriptorPointers.size()) {
				return factory.descriptorPointers[index];
			}
			return nullptr;
		}
		static const clap_plugin_t * native_create_plugin(const struct clap_plugin_factory *factory, const clap_host *host, const char *plugin_id) {
			return nullptr;
		}
	};

	static WasmP nativeToWasm(WclapArenas &arenas, const char *str) {
		if (!str) return 0;
		size_t length = std::strlen(str);
		if (validity.lengths && length > validity.maxStringLength) {
			length = validity.maxStringLength;
		}
		auto wasmTmp = arenas.wasmBytes(length + 1);
		auto *nativeTmp = (char *)arenas.wasmMemory(wasmTmp);
		for (size_t i = 0; i < length; ++i) {
			nativeTmp[i] = str[i];
		}
		nativeTmp[length] = 0;
		return wasmTmp;
	}

	static const char * wasmToNativeString(WclapArenas &arenas, WasmP wasmStr) {
		if (!wasmStr) return nullptr;
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
		return nativeTmp;
	}

	static clap_plugin_descriptor * wasmToNative(WclapArenas &arenas, wclap_plugin_descriptor &wasm) {
		auto *native = arenas.nativeTyped<clap_plugin_descriptor>();
		native->clap_version = wasm.clap_version();
		native->id = wasmToNativeString(arenas, wasm.id());
		native->name = wasmToNativeString(arenas, wasm.name());
		native->vendor = wasmToNativeString(arenas, wasm.vendor());
		native->url = wasmToNativeString(arenas, wasm.url());
		native->manual_url = wasmToNativeString(arenas, wasm.manual_url());
		native->support_url = wasmToNativeString(arenas, wasm.support_url());
		native->version = wasmToNativeString(arenas, wasm.version());
		native->description = wasmToNativeString(arenas, wasm.description());
		
		// Null-terminated array of strings
		size_t count = 0;
		WasmP *wasmStrArray = (WasmP *)arenas.wasmMemory(wasm.features());
		while (wasmStrArray[count] && (!validity.lengths || count < validity.maxFeaturesLength)) {
			++count;
		}
		auto *nativeStrArray = (const char **)arenas.nativeBytes(sizeof(char *)*(count + 1));
		for (size_t i = 0; i < count; ++i) {
			nativeStrArray[i] = wasmToNativeString(arenas, wasmStrArray[count]);
		}
		nativeStrArray[count] = nullptr;
		native->features = nativeStrArray;
		return native;
	}
	
	bool triedPluginFactory = false;
	std::unique_ptr<plugin_factory> pluginFactory;

	void * getFactory(const char *factory_id) {
		auto scoped = wclap.lockRelaxedThread();
		auto entryP = WasmP(scoped.thread.clapEntryP64);
		auto wasmEntry = wclap.view<wclap_plugin_entry>(entryP);
		
		auto getFactoryFn = wasmEntry.get_factory();
		WasmP factoryP;
		{
			auto reset = scoped.thread.translationScope->scopedWasmReset();
			auto wasmStr = nativeToWasm(*scoped.thread.translationScope, factory_id);
			factoryP = scoped.thread.callWasm_P(getFactoryFn, wasmStr);
		}
		if (!factoryP) return nullptr;

		if (!std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) {
			if (!triedPluginFactory) {
				triedPluginFactory = true;
				pluginFactory = std::unique_ptr<plugin_factory>(new plugin_factory());
				pluginFactory->assign({factoryP, &wclap}, scoped.thread, wclap.view<wclap_plugin_factory>(factoryP));
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

}} // namespace
