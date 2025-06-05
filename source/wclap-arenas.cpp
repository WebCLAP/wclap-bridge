#include "./wclap-arenas.h"

#include "./validity.h"
#include "./wclap-thread.h"
#include "./wclap.h"

namespace wclap {

WclapArenas::WclapArenas(Wclap &wclap, WclapThread &threadToUse, size_t arenaIndex) : wclap(wclap) {
	nativeArena = nativeArenaPos = nativeArenaReset = (unsigned char *)malloc(arenaBytes);
	nativeArenaEnd = nativeArena + arenaBytes;

	auto wasmBytes = threadToUse.wasmMalloc(arenaBytes);
	
	// Take the first few bytes as a context pointer
	wasmContextP = wasmBytes;
	auto *wasmContextV = (size_t *)wclap.wasmMemory(threadToUse, wasmContextP, sizeof(size_t));
	*wasmContextV = arenaIndex;

	// The rest is our arena
	wasmArena = wasmArenaPos = wasmArenaReset = wasmBytes + sizeof(size_t);
	wasmArenaEnd = wasmArena + arenaBytes;
}

WclapArenas::~WclapArenas() {
	if (nativeArenaReset) free(nativeArenaReset);
}

} // namespace
