#include "./wclap-arenas.h"

#include "./validity.h"
#include "./wclap-thread.h"
#include "./wclap.h"

namespace wclap {

WclapArenas::WclapArenas(Wclap &wclap, WclapThread &currentThread) : wclap(wclap) {
	nativeArena = nativeArenaPos = (unsigned char *)malloc(arenaBytes);
	nativeArenaEnd = nativeArena + arenaBytes;

	wasmContextP = currentThread.wasmMalloc(sizeof(WasmContext) + alignof(WasmContext));
	// ensure alignment
	while (size_t(wclap.wasmMemory(wasmContextP))%alignof(WasmContext)) {
		++wasmContextP;
	}
	wasmArena = wasmArenaPos = currentThread.wasmMalloc(arenaBytes);
	wasmArenaEnd = wasmArena + arenaBytes;
}

WclapArenas::~WclapArenas() {
	if (nativeArena) free(nativeArena);
}

uint8_t * WclapArenas::wasmMemory(uint64_t wasmP) {
	return wclap.wasmMemory(wasmP);
}

template<class AutoTranslatedStruct>
AutoTranslatedStruct WclapArenas::view(uint64_t wasmP) {
	return wclap.view<AutoTranslatedStruct>(wasmP);
}

} // namespace
