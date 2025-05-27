#include "./wclap-translation-scope.h"
#include "./wclap-thread.h"

namespace wclap {
namespace wclap32 {

WclapTranslationScope::~WclapTranslationScope() {
	if (!_wasmReadyToDestroy) {
		LOG_EXPR(_wasmReadyToDestroy);
		abort();
	}
	if (nativeArena) free(nativeArena);
}

void WclapTranslationScope::mallocIfNeeded(WclapThread &currentThread) {
	if (!nativeArena) {
		nativeArena = nativeArenaPos = (unsigned char *)malloc(arenaBytes);
		nativeArenaEnd = nativeArena + arenaBytes;
		wasmArena = wasmArenaPos = currentThread.wasmMalloc(arenaBytes);
		wasmArenaEnd = wasmArena + arenaBytes;
	}
}

//---------------

struct WclapMethods {
	// clap_plugin_factory - has no context field, so we must assume it's being used as ClapStructWithContext
	struct {
		static uint32_t get_plugin_count(const struct clap_plugin_factory *factory) {
			auto *withContext = (ClapStructWithContext<clap_plugin_factory> *)factory;
			auto &context = withContext->wclapContext;
			auto wasmFactory = context.wclap->view<wclap_plugin_factory>(context.wasmP);
			auto scopedThread = context.wclap->lockRelaxedThread();
			return scopedThread.thread.callWasm_IP(wasmFactory.get_plugin_count(), context.wasmP);
		}
		static const clap_plugin_descriptor_t * get_plugin_descriptor(const struct clap_plugin_factory *factory, uint32_t index) {
//			auto *withContext = (ClapStructWithContext<clap_plugin_factory> *)factory;
//			auto &context = withContext->wclapContext;
//			auto wasmFactory = context.wclap->view<wclap_plugin_factory>(context.wasmP);
//			auto scopedThread = context.wclap->lockRelaxedThread();
//			auto descriptorP = scopedThread.thread.callWasm_PPI(wasmFactory.get_plugin_descriptor(), context.wasmP, index);
//			if (!descriptorP) return nullptr;
			// TODO: translate descriptor into some persistent storage
			return nullptr;
		}
		static const clap_plugin_t * create_plugin(const struct clap_plugin_factory *factory, const clap_host *host, const char *plugin_id) {
			// TODO: translate descriptor into some persistent storage
			return nullptr;
		}
	} plugin_factory;
	
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

WclapMethods * createMethods() {
	return new WclapMethods();
}
void destroyMethods(WclapMethods *methods) {
	delete methods;
}
void registerHostMethods(WclapMethods *methods, WclapThread &thread) {
	methods->registerHostMethods(thread);
}

template<>
void WclapTranslationScope::assignWasmToNative<ClapStructWithContext<clap_plugin_factory>>(WasmP wasmP, ClapStructWithContext<clap_plugin_factory> &native) {
	LOG_EXPR("assignWasmToNative: clap_plugin_factory");

	// extra info so we can find ourselves again - usually this would be in a struct pointed to by the `void *` context pointer
	native.wclapContext = {wasmP, &wclap};
	
	//auto wasmFactory = get<wclap_plugin_factory>(wasmP);
	native.get_plugin_count = methods.plugin_factory.get_plugin_count;
	native.get_plugin_descriptor = methods.plugin_factory.get_plugin_descriptor;
	native.create_plugin = methods.plugin_factory.create_plugin;
}
	
}} // namespace
