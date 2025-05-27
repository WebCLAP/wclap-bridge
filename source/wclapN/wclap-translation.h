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
	//WclapTranslationScope translationScope;
	
	WclapMethods(Wclap &wclap) : wclap(wclap) /*, translationScope(wclap)*/ {}

	struct plugin_factory : public clap_plugin_factory {
		WclapContext context;
		
		std::vector<clap_plugin_descriptor *> descriptorPointers;
		
		void assign(const WclapContext &c, WclapThread &thread, wclap_plugin_factory wasmFactory) {
			this->get_plugin_count = native_get_plugin_count;
			this->get_plugin_descriptor = native_get_plugin_descriptor;
			this->create_plugin = native_create_plugin;

			context = c;

			auto count = thread.callWasm_IP(wasmFactory.get_plugin_count(), context.wasmP);
			if (validity.lengths && count > validity.maxPlugins) {
				context.wclap->errorMessage = "plugin factory advertised too many plugins";
				descriptorPointers.clear();
				return;
			}
			
			descriptorPointers.assign(count, nullptr);
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
			// TODO: translate descriptor into some persistent storage
			return nullptr;
		}
	};

	bool triedPluginFactory = false;
	std::unique_ptr<plugin_factory> pluginFactory;

	void * getFactory(const char *factory_id) {
		auto scoped = wclap.lockRelaxedThread();
		auto entryP = WasmP(scoped.thread.clapEntryP64);
		auto wasmEntry = wclap.view<wclap_plugin_entry>(entryP);
		
		auto getFactoryFn = wasmEntry.get_factory();
		auto factoryP = scoped.thread.callWasm_PS(getFactoryFn, factory_id);
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
