#include "./wclap-arenas.h"

#include "./validity.h"
#include "./wclap-thread.h"
#include "./wclap.h"

namespace wclap {

WclapArenas::WclapArenas(Wclap &wclap, WclapThread &currentThread, size_t arenaIndex) : wclap(wclap) {
	nativeArena = nativeArenaPos = nativeArenaReset = (unsigned char *)malloc(arenaBytes);
	nativeArenaEnd = nativeArena + arenaBytes;

	wasmArena = wasmArenaPos = wasmArenaReset = currentThread.wasmMalloc(arenaBytes);
	wasmArenaEnd = wasmArena + arenaBytes;

	wasmContextP = wasmBytes(sizeof(size_t), alignof(size_t));
	wasmArenaReset = wasmArena = wasmArenaPos; // never erase
	*(size_t *)wclap.wasmMemory(wasmContextP) = arenaIndex;
}

WclapArenas::~WclapArenas() {
	if (nativeArena) free(nativeArena);
}

uint8_t * WclapArenas::wasmMemory(uint64_t wasmP) {
	return wclap.wasmMemory(wasmP);
}

} // namespace
