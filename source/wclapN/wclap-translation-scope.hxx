/// NO #pragma once or include guard - this is included twice from within two different namespaces
#ifndef WCLAP_MULTIPLE_INCLUDES_NAMESPACE
#	error must not be included directly
#endif

#include "./wclapN-translation-scope.h"
#include "./wclap-thread.h"
#include "./validity.h"

namespace wclap {
namespace WCLAP_MULTIPLE_INCLUDES_NAMESPACE {

WclapTranslationScope::WclapTranslationScope(Wclap &wclap, WclapThread &currentThread) {
	nativeArena = nativeArenaPos = (unsigned char *)malloc(arenaBytes);
	nativeArenaEnd = nativeArena + arenaBytes;
	wasmArena = wasmArenaPos = currentThread.wasmMalloc(arenaBytes);
	wasmArenaEnd = wasmArena + arenaBytes;
}

WclapTranslationScope::~WclapTranslationScope() {
	if (nativeArena) free(nativeArena);
}

WasmP WclapTranslationScope::copyStringToWasm(const char *str) {
	size_t length = std::strlen(str);
	if (validity.length && length > validity.maxStringLength) {
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

}} // namespace
