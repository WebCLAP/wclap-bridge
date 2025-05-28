#include "./wclap-arenas.h"

#include "./validity.h"
#include "./wclap-thread.h"
#include "./wclap.h"

namespace wclap {

WclapArenas::WclapArenas(Wclap &wclap, WclapThread &currentThread) : wclap(wclap) {
	nativeArena = nativeArenaPos = (unsigned char *)malloc(arenaBytes);
	nativeArenaEnd = nativeArena + arenaBytes;
	wasmArena = wasmArenaPos = currentThread.wasmMalloc(arenaBytes);
	wasmArenaEnd = wasmArena + arenaBytes;
}

WclapArenas::~WclapArenas() {
	if (nativeArena) free(nativeArena);
}

uint8_t * WclapArenas::wasmMemory(uint64_t wasmP) {
	return wclap.wasmMemory(wasmP);
}

} // namespace
