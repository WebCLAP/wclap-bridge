#pragma once

#ifndef WCLAP_ENGINE_WASMTIME
// Wasmtime is the only supported one for now
#	define WCLAP_ENGINE_WASMTIME
#endif

#include "./class-id.generated.h"

#include <shared_mutex>
#include <atomic>
#include <vector>

namespace wclap {

struct NativeProxyList {

	NativeProxyList() {
		items.reserve(1);
	}

	template<class ClapStruct>
	const ClapStruct * getNative() const {
		ClassId classId = getClassId<ClapStruct>();
		auto lock = readLock();
		for (auto &item : items) {
			if (item.classId == classId) {
				return (ClapStruct *)item.hostNative.load();
			}
		}
		return nullptr;
	}

	// Returns whether an existing proxy was found, and sets the WASM pointer to it if so
	template<class ClapStruct, typename WasmP>
	bool update(const ClapStruct * ptr, WasmP &wasmP) {
		ClassId classId = getClassId<ClapStruct>();
		auto lock = readLock();
		for (auto &item : items) {
			if (item.classId == classId) {
				item.hostNative.store(ptr);
				wasmP = (WasmP)item.wasmP;
				return true;
			}
		}
		wasmP = 0;
		return false;
	}
	
	template<class ClapStruct, typename WasmP>
	void add(const ClapStruct * ptr, WasmP wasmP) {
		ClassId classId = getClassId<ClapStruct>();
		auto lock = writeLock();
		items.emplace_back(classId, ptr, wasmP);
	}

	__attribute__((cdecl)) // __cdecl on Microsoft
	void clear() {
		auto lock = writeLock();
		items.clear();
	}

private:
	struct Item {
		ClassId classId;
		// Atomic because we allow it to be updated with only the "read" lock
		std::atomic<const void *> hostNative;
		size_t wasmP;
		
		Item(ClassId classId, const void *native, size_t wasmP) : classId(classId), hostNative(native), wasmP(wasmP) {}
		// Allow move-construction, since it should only happen when we have the write-lock
		Item(Item &&other) : classId(other.classId), hostNative(other.hostNative.load()), wasmP(other.wasmP) {}
	};

	std::vector<Item> items;

	mutable std::shared_mutex mutex;
	std::shared_lock<std::shared_mutex> readLock() const {
		return std::shared_lock<std::shared_mutex>{mutex};
	}
	std::unique_lock<std::shared_mutex> writeLock() {
		return std::unique_lock<std::shared_mutex>{mutex};
	}
};

} // namespace
