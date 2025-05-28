#include "./wclap-arenas.h"

#include "./validity.h"
#include "./wclap-thread.h"
#include "./wclap.h"

namespace wclap {

WclapTranslationScope::WclapTranslationScope(Wclap &wclap, WclapThread &currentThread) : wclap(wclap) {
	nativeArena = nativeArenaPos = (unsigned char *)malloc(arenaBytes);
	nativeArenaEnd = nativeArena + arenaBytes;
	wasmArena = wasmArenaPos = currentThread.wasmMalloc(arenaBytes);
	wasmArenaEnd = wasmArena + arenaBytes;
}

WclapTranslationScope::~WclapTranslationScope() {
	if (nativeArena) free(nativeArena);
}

size_t WclapTranslationScope::copyStringToWasm(const char *str) {
	size_t length = std::strlen(str);
	if (validity.lengths && length > validity.maxStringLength) {
		length = validity.maxStringLength;
	}
	auto wasmTmp = wasmBytes(length + 1);
	auto *nativeTmp = (char *)wclap.wasmMemory(wasmTmp);
	for (size_t i = 0; i < length; ++i) {
		nativeTmp[i] = str[i];
	}
	nativeTmp[length] = 0;
	return wasmTmp;
}

} // namespace
