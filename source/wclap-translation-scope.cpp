#include "./wclap-translation-scope.h"

namespace wclap {
namespace wclap32 {

WclapTranslationScope::WclapTranslationScope(Wclap &wclap, WclapThread &currentThread) : wclap(wclap) {
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

void * WclapTranslationScope::nativeInWasm(WasmP wasmP) {
	// TODO: bounds-check
	return wclap.wasmMemory(wasmP);
}

}} // namespace
