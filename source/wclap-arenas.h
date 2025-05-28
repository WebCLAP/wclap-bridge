#pragma once

#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "clap/all.h"

#include <type_traits>
#include <unordered_map>

namespace wclap {

namespace wclap32 {
	using WasmP = uint32_t;
	struct WclapMethods;
}
namespace wclap64 {
	using WasmP = uint64_t;
	struct WclapMethods;
}

struct Wclap;
struct WclapThread;

/* Manages two arena allocators, used for (temporary) translation of function arguments.

	Since free() isn't exposed from the WCLAP, this object should be active until the WASM memory is destroyed.  If a thread is destroyed (does this ever happen?) this object should be returned to the Wclap's pool.
*/
struct WclapTranslationScope {
	const size_t arenaBytes = 65536;

	Wclap &wclap;
	
	WclapTranslationScope(Wclap &wclap, WclapThread &currentThread);
	~WclapTranslationScope();
	
	void mallocIfNeeded();
	
	unsigned char *nativeArena = nullptr, *nativeArenaEnd = nullptr, *nativeArenaPos = nullptr;
	unsigned char * nativeBytes(size_t size, size_t align=1) {
		while (((size_t)nativeArenaPos)%align) ++nativeArenaPos;
		unsigned char *result = nativeArenaPos;
		nativeArenaPos += size;
		if (nativeArenaPos > nativeArenaEnd) {
			LOG_EXPR(nativeArena);
			LOG_EXPR(nativeArenaEnd);
			LOG_EXPR(nativeArenaPos);
			abort(); // TODO: grow list of arenas
		}
		return result;
	}

	size_t wasmArena, wasmArenaEnd, wasmArenaPos;
	size_t wasmBytes(size_t size, size_t align=1) {
		while (wasmArenaPos%align) ++wasmArenaPos;
		size_t result = wasmArenaPos;
		wasmArenaPos += size;
		if (wasmArenaPos > wasmArenaEnd) {
			LOG_EXPR(wasmArenaPos > wasmArenaEnd);
			abort(); // TODO: grow list of arenas
		}
		return result;
	}

	size_t copyStringToWasm(const char *str);

private:
	unsigned char * nativeInWasm(size_t wasmP);
};

} // namespace
