#pragma once

#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "clap/all.h"
#include "./wclap32-translate-struct.generated.h"

#include <type_traits>
#include <unordered_map>

namespace wclap {
struct Wclap;
struct WclapThread;
struct WclapTranslationScope;
}

namespace wclap {namespace wclap32 {

struct WclapContext {
	WasmP wasmP = 0;
	Wclap *wclap = nullptr;

	/*
	template<class AutoTranslatedStruct>
	AutoTranslatedStruct view(WasmP wasmP) {
		return wclap->view<AutoTranslatedStruct>(wasmP);
	}
	
	struct Scoped {
		Wclap::ScopedThread scoped;
		WclapThread &thread;
		WclapTranslationScope &translation;
	};
	
	Scoped lockRelaxed() {
		auto scoped = wclap->lockRelaxedThread();
		auto &thread = scoped.thread;
		return {
			std::move(scoped),
			thread,
			*thread.translationScope32
		};
	}
	*/
};

template<class ClapStruct>
struct ClapStructWithContext : public ClapStruct {
	using ClapStruct::ClapStruct;
	WclapContext wclapContext;
};

struct WclapMethods;
WclapMethods * createMethods();
void destroyMethods(WclapMethods *methods);
void registerHostMethods(WclapMethods *methods, WclapThread &thread);

/* Manages function calls and translating values across the boundary.

	Owns a chunk of WASM memory, from the WCLAP's malloc().

	Since free() isn't exposed, this object should be active until the WASM memory is destroyed.  When the bound object is destroyed, it should be returned to the Wclap's pool.
*/
struct WclapTranslationScope {
	const size_t arenaBytes = 65536;

	Wclap &wclap;
	WclapMethods &methods;
	
	WclapTranslationScope(Wclap &wclap, WclapMethods &methods) : wclap(wclap), methods(methods) {}
	~WclapTranslationScope();
	
	void mallocIfNeeded(WclapThread &currentThread);
	
	// Should only happen when the WASM instance is destroyed - otherwise it should be returned to a pool (since we can't free the arena memory)
	void wasmReadyToDestroy() {
		_wasmReadyToDestroy = true;
	}
	
	unsigned char *nativeArena = nullptr, *nativeArenaEnd = nullptr, *nativeArenaPos = nullptr;
	void rewindNative() {
		nativeArenaPos = nativeArena;
	}
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
	void rewindWasm() {
		wasmArenaPos = wasmArena;
	}
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
	
	template<class Native>
	void assignWasmToNative(WasmP wasmP, Native &native);

private:
	unsigned char * nativeInWasm(WasmP wasmP);
	
	bool _wasmReadyToDestroy = false;
};

}} // namespace
