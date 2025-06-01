#pragma once

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

/* Manages two arena allocators, used for (temporary) translation of function arguments.

	Since free() isn't exposed from the WCLAP, this object should be active until the WASM memory is destroyed.  If a thread is destroyed (does this ever happen?) this object should be returned to the Wclap's pool.
*/
struct WclapArenas {
	const size_t arenaBytes = 65536;

	Wclap &wclap;
	
	WclapArenas(Wclap &wclap, WclapThread &currentThread);
	~WclapArenas();
	
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
	template<class T>
	T * nativeTyped() {
		return (T *)nativeBytes(sizeof(T), alignof(T));
	}
	void nativeReset() {
		nativeArenaPos = nativeArena;
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
	
	uint8_t * wasmMemory(uint64_t wasmP);

	// Object that resets the arena position when it goes out of scope
	struct ScopedWasmPosReset {
		ScopedWasmPosReset(WclapArenas &arena, size_t pos) : arena(arena), pos(pos) {}
		ScopedWasmPosReset(ScopedWasmPosReset &&other) : arena(other.arena), pos(other.pos) {
			other.active = false;
		}
		~ScopedWasmPosReset() {
			if (active) arena.wasmArenaPos = pos;
		}
	private:
		WclapArenas &arena;
		size_t pos;
		bool active = true;
	};
	ScopedWasmPosReset scopedWasmReset() {
		return {*this, wasmArenaPos};
	}

	// Like a pointer into the WASM object
	template<class AutoTranslatedStruct>
	AutoTranslatedStruct view(uint64_t wasmP) {
		return wclap.view<AutoTranslatedStruct>(wasmP);
	}
	
	template<class AutoTranslatedStruct, typename WasmP>
	AutoTranslatedStruct create(WasmP &wasmP) {
		wasmP = (WasmP)wasmBytes(sizeof(AutoTranslatedStruct), alignof(AutoTranslatedStruct));
		return wclap.view<AutoTranslatedStruct>(wasmP);
	}
	
private:
	unsigned char * nativeInWasm(size_t wasmP);
};

} // namespace
