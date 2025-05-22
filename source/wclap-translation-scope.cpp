#include "./wclap-translation-scope.h"

namespace wclap {
namespace wclap32 {

WclapTranslationScope::WclapTranslationScope(Wclap &wclap, WclapThread &currentThread, WclapMethods &methods) : wclap(wclap), methods(methods) {
	nativeArena = nativeTmpStartP = nativeTmpP = (unsigned char *)malloc(arenaBytes);
	wasmArena = wasmTmpStartP = wasmTmpP = currentThread.wasmMalloc(arenaBytes);
	LOG_EXPR((void *)nativeArena);
	LOG_EXPR(wasmArena);
}
WclapTranslationScope::~WclapTranslationScope() {
	if (!_wasmReadyToDestroy) {
		LOG_EXPR(_wasmReadyToDestroy);
		abort();
	}
	free(nativeArena);
}

unsigned char * WclapTranslationScope::nativeInWasm(WasmP wasmP) {
	// TODO: bounds-check here? Or in .get<type> where we know the size
	return wclap.wasmMemory(wasmP);
}

}} // namespace
