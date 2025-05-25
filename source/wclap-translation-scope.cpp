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
		nativeArena = nativeTmpStartP = nativeTmpP = (unsigned char *)malloc(arenaBytes);
		wasmArena = wasmTmpStartP = wasmTmpP = currentThread.wasmMalloc(arenaBytes);
		LOG_EXPR((void *)nativeArena);
		LOG_EXPR(wasmArena);
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
	} plugin_factory;
};

WclapMethods * createMethods() {
	return new WclapMethods();
}
void destroyMethods(WclapMethods *methods) {
	delete methods;
}
void registerMethods(WclapMethods *methods, WclapThread &thread) {
	methods->registerMethods(thread);
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
