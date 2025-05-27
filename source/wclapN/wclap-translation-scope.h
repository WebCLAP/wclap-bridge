/// NO #pragma once or include guard - this is included twice from within two different namespaces
#ifndef WCLAP_MULTIPLE_INCLUDES_NAMESPACE
#	error must not be included directly
#endif

#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "clap/all.h"

#include <type_traits>
#include <unordered_map>

namespace wclap {
struct Wclap;
struct WclapThread;
}

namespace wclap {namespace WCLAP_MULTIPLE_INCLUDES_NAMESPACE {

using WasmP = WCLAP_MULTIPLE_INCLUDES_WASMP;

struct WclapMethods;

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

	WasmP wasmArena, wasmArenaEnd, wasmArenaPos;
	WasmP wasmBytes(WasmP size, WasmP align=1) {
		while (wasmArenaPos%align) ++wasmArenaPos;
		WasmP result = wasmArenaPos;
		wasmArenaPos += size;
		if (wasmArenaPos > wasmArenaEnd) {
			LOG_EXPR(wasmArenaPos > wasmArenaEnd);
			abort(); // TODO: grow list of arenas
		}
		return result;
	}

	WasmP copyStringToWasm(const char *str);

private:
	unsigned char * nativeInWasm(WasmP wasmP);
};

}} // namespace
