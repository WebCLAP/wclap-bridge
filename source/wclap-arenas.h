#pragma once

#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "clap/all.h"
#include "./wclap-proxies.h"

#include <type_traits>
#include <unordered_map>
#include <new>

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
					if (pos > arena.wasmArenaPos || pos < arena.wasmArena) {
						// This means we tried to persist the arena *while* holding a scoped reset
						LOG_EXPR(pos > arena.wasmArenaPos);
						LOG_EXPR(pos < arena.wasmArena);
						abort();
					}
					arena.wasmArenaPos = pos;
				}
			} else {
				if (active) {
					if ((unsigned char *)pos > arena.nativeArenaPos || (unsigned char *)pos < arena.nativeArena) {
						// This means we tried to persist the arena *while* holding a scoped reset
						LOG_EXPR((unsigned char *)pos > arena.nativeArenaPos);
						LOG_EXPR((unsigned char *)pos < arena.nativeArena);
						abort();
					}
					arena.nativeArenaPos = (unsigned char *)pos;
				}
			}
		}
	private:
		WclapArenas &arena;
		size_t pos;
		bool active = true;
	};
	
	unsigned char *nativeArena = nullptr, *nativeArenaReset = nullptr, *nativeArenaEnd = nullptr, *nativeArenaPos = nullptr;
	unsigned char * nativeBytes(size_t size, size_t align) {
		while (((size_t)nativeArenaPos)%align) ++nativeArenaPos;
		unsigned char *result = nativeArenaPos;
		nativeArenaPos += size;
		if (nativeArenaPos > nativeArenaEnd) {
			LOG_EXPR(nativeArenaPos > nativeArenaEnd);
			LOG_EXPR((void *)nativeArena);
			LOG_EXPR((void *)nativeArenaEnd);
			LOG_EXPR((void *)nativeArenaPos);
			abort(); // TODO: grow list of arenas
		}
		return result;
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
			LOG_EXPR(wasmArenaPos)
			LOG_EXPR(wasmArenaEnd);
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
			LOG_EXPR(nativeArenaPos != nativeArena);
			LOG_EXPR((void *)nativeArenaPos);
			LOG_EXPR((void *)nativeArena);
		}
		if (wasmArenaPos != wasmArena) {
			LOG_EXPR(wasmArenaPos != wasmArena);
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

	template<class T>
	T * nativeTyped() {
		return (T *)nativeBytes(sizeof(T), alignof(T));
	}

private:
	unsigned char * nativeInWasm(size_t wasmP);
};

// This doesn't release any memory, but it calls the destructor
// Appropriate to use on native-arena objects before the arena is reset
template<class T>
void arenaNativeDelete(T *&obj) {
	obj->~T();
	obj = nullptr;
}

} // namespace
