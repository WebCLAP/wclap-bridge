#pragma once

#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "clap/all.h"
#include "./wclap-proxies.h"

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
	
	uint64_t wasmContextP; // store this in the `void *` context field of WASM proxies
	ProxiedClapStruct<clap_host> proxied_clap_host;
	
	WclapArenas(Wclap &wclap, WclapThread &currentThread, size_t arenaIndex);
	~WclapArenas();

	// Object that resets the arena position when it goes out of scope
	template<bool forWasm>
	struct ScopedReset {
		ScopedReset(WclapArenas &arena, size_t pos) : arena(arena), pos(pos) {}
		ScopedReset(WclapArenas &arena, unsigned char *pos) : arena(arena), pos(size_t(pos)) {}
		ScopedReset(ScopedReset &&other) : arena(other.arena), pos(other.pos) {
			other.active = false;
		}
		~ScopedReset() {
			if (forWasm) {
				if (active) {
					if (pos < arena.wasmArenaPos) {
						arena.wasmArenaPos = pos;
					} else {
						
					}
				}
			} else {
				if (active) arena.nativeArenaPos = (unsigned char *)pos;
			}
		}
	private:
		WclapArenas &arena;
		size_t pos;
		bool active = true;
	};
	
	unsigned char *nativeArena = nullptr, *nativeArenaReset = nullptr, *nativeArenaEnd = nullptr, *nativeArenaPos = nullptr;
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
	ScopedReset<false> scopedNativeReset() {
		return {*this, nativeArenaPos};
	}
	void persistNative() {
		nativeArena = nativeArenaPos;
	}
	
	size_t wasmArena = 0, wasmArenaEnd = 0, wasmArenaPos = 0, wasmArenaReset = 0;
	size_t wasmBytes(size_t size, size_t align) {
		while (wasmArenaPos%align) ++wasmArenaPos;
		size_t result = wasmArenaPos;
		wasmArenaPos += size;
		if (wasmArenaPos > wasmArenaEnd) {
			LOG_EXPR(wasmArenaPos > wasmArenaEnd);
			abort(); // TODO: grow list of arenas
		}
		return result;
	}
	
	ScopedReset<true> scopedWasmReset() {
		return {*this, wasmArenaPos};
	}
	void persistWasm() {
		wasmArena = wasmArenaPos;
	}

	void resetTemporary() {
		if (nativeArenaPos != nativeArena) {
			LOG_EXPR(nativeArenaPos);
			LOG_EXPR(nativeArena);
		}
		if (wasmArenaPos != wasmArena) {
			LOG_EXPR(wasmArenaPos);
			LOG_EXPR(wasmArena);
		}
		nativeArenaPos = nativeArena;
		wasmArenaPos = wasmArena;
	}

	// Called when returning to a pool
	void resetIncludingPersistent() {
		nativeArenaPos = nativeArena = nativeArenaReset;
		wasmArenaPos = wasmArena = wasmArenaReset;
		
		proxied_clap_host.clear();
	}

private:
	unsigned char * nativeInWasm(size_t wasmP);
};

} // namespace
