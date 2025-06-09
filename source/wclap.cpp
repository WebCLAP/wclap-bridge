#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "./wclap.h"
#include "./wclap-thread.h"
#include "./wclap32/wclap-translation.h"
#include "./wclap64/wclap-translation.h"

std::ostream & operator<<(std::ostream &s, const wasm_byte_vec_t &bytes) {
	for (size_t i = 0; i < bytes.size; ++i) {
		s << bytes.data[i];
	}
	return s;
}

namespace wclap {

std::atomic<wasm_engine_t *> global_wasm_engine;
const char *wclap_error_message;
std::string wclap_error_message_string;

//---------- Common Wclap instance ----------//

Wclap::Wclap(const std::string &wclapDir, const std::string &presetDir, const std::string &cacheDir, const std::string &varDir, bool mustLinkDirs) : wclapDir(wclapDir), presetDir(presetDir), cacheDir(cacheDir), varDir(varDir), mustLinkDirs(mustLinkDirs) {}

Wclap::~Wclap() {
	if (methods32) wclap::wclap32::methodsDeinitAndDelete(methods32);
	if (methods64) wclap::wclap64::methodsDeinitAndDelete(methods64);

	{ // Clear all threads/scopes
		auto lock = writeLock();
		globalThread = nullptr;
		realtimeThreadPool.clear();
		relaxedThreadPool.clear();
	}

	if (sharedMemory) {
		wasmtime_sharedmemory_delete(sharedMemory);
	}
	
	if (error) wasmtime_error_delete(error);
	if (module) wasmtime_module_delete(module);
}

uint8_t * Wclap::wasmMemory(WclapThread &lockedThread, uint64_t wasmP, uint64_t size) {
	if (sharedMemory) {
		auto memorySize = wasmtime_sharedmemory_data_size(sharedMemory);
		wasmP = std::min<uint64_t>(wasmP, memorySize - size);
		return wasmtime_sharedmemory_data(sharedMemory) + wasmP;
	} else {
		auto memorySize = wasmtime_memory_data_size(lockedThread.context, &lockedThread.memory);
		wasmP = std::min<uint64_t>(wasmP, memorySize - size);
		return wasmtime_memory_data(lockedThread.context, &lockedThread.memory) + wasmP;
	}
}

uint64_t Wclap::wasmMemorySize(WclapThread &lockedThread) {
	if (sharedMemory) {
		return wasmtime_sharedmemory_data_size(sharedMemory);
	} else {
		return wasmtime_memory_data_size(lockedThread.context, &lockedThread.memory);
	}
}

void Wclap::initWasmBytes(const uint8_t *bytes, size_t size) {
	error = wasmtime_module_new(global_wasm_engine, bytes, size, &module);
	if (error) return setError("Failed to compile module");
	
	bool foundClapEntry = false;
	{ // Find `clap_entry`, which is a memory-address pointer, so the type of it tells us if it's 64-bit
		wasm_exporttype_vec_t exportTypes;
		wasmtime_module_exports(module, &exportTypes);
		
		for (size_t i = 0; i < exportTypes.size; ++i) {
			auto *exportType = exportTypes.data[i];
			auto *name = wasm_exporttype_name(exportType);
			if (!nameEquals(name, "clap_entry")) continue;
			foundClapEntry = true;
			auto *externType = wasm_exporttype_type(exportType);
			auto *globalType = wasm_externtype_as_globaltype_const(externType);
			if (!globalType) {
				return setError("clap_entry is not a global (value) export");
			}
			auto *valType = wasm_globaltype_content(globalType);
			auto kind = wasm_valtype_kind(valType);
			
			if (kind == WASMTIME_I64) {
				wasm64 = true;
				break;
			} else if (kind != WASMTIME_I32) {
				return setError("clap_entry must be 32-bit or 64-bit memory address");
			}
		}
		wasm_exporttype_vec_delete(&exportTypes);
	}
	if (!foundClapEntry) return setError("clap_entry not found");

	{ // Check for a shared-memory import - otherwise it's single-threaded
		wasm_importtype_vec_t importTypes;
		wasmtime_module_imports(module, &importTypes);
		for (size_t i = 0; i < importTypes.size; ++i) {
			auto *importType = importTypes.data[i];
			auto *module = wasm_importtype_module(importType);
			auto *name = wasm_importtype_name(importType);
			auto *externType = wasm_importtype_type(importType);

			auto *asMemory = wasm_externtype_as_memorytype_const(externType);
			if (!asMemory) continue;

			if (!wasmtime_memorytype_isshared(asMemory)) {
				setError("imports non-shared memory");
				break;
			}
			bool mem64 = wasmtime_memorytype_is64(asMemory);
			if (mem64 != wasm64) {
				setError(mem64 ? "64-bit memory but 32-bit clap_entry" : "32-bit memory but 64-bit clap_entry");
				break;
			}
			if (sharedMemory) {
				setError("multiple memory imports");
				break;
			}

			error = wasmtime_sharedmemory_new(global_wasm_engine, asMemory, &sharedMemory);
			if (error || !sharedMemory) {
				setError("failed to create shared memory");
				break;
			}
		}
		wasm_importtype_vec_delete(&importTypes);
	}
	if (errorMessage) return;

	globalThread = std::unique_ptr<WclapThread>{
		new WclapThread(*this)
	};
	// Normally directly after creation we'd register the methods, but we can't create those until after `wasmInit()` and `claimArenas()` have made arenas/`malloc()` usable
	if (errorMessage) return;
	
	globalThread->wasmInit();
	if (errorMessage) return;
	
	globalArenas = claimArenas(globalThread.get());
	if (errorMessage) return;

	// These also call clap_entry.init();
	if (wasm64) {
		methods64 = wclap::wclap64::methodsCreateAndInit(*this);
		wclap::wclap64::methodsRegister(methods64, *globalThread);
	} else {
		methods32 = wclap::wclap32::methodsCreateAndInit(*this);
		wclap::wclap32::methodsRegister(methods32, *globalThread);
	}
}

std::unique_ptr<WclapThread> Wclap::claimRealtimeThread() {
	if (!sharedMemory) return {}; // single-threaded mode - null pointer
	auto lock = writeLock();
	{
		if (realtimeThreadPool.size()) {
			std::unique_ptr<WclapThread> result = std::move(realtimeThreadPool.back());
			realtimeThreadPool.pop_back();
			return result;
		}
	}
	auto *rawPtr = new WclapThread(*this);
	if (wasm64) {
		wclap::wclap64::methodsRegister(methods64, *rawPtr);
	} else {
		wclap::wclap32::methodsRegister(methods32, *rawPtr);
	}
	return std::unique_ptr<WclapThread>(rawPtr);
}

const void * Wclap::getFactory(const char *factory_id) {
	if (wasm64) {
		return wclap::wclap64::methodsGetFactory(methods64, factory_id);
	} else {
		return wclap::wclap32::methodsGetFactory(methods32, factory_id);
	}
}

void Wclap::returnRealtimeThread(std::unique_ptr<WclapThread> &ptr) {
	auto lock = writeLock();
	realtimeThreadPool.emplace_back(std::move(ptr));
}

std::unique_ptr<WclapArenas> Wclap::claimArenas() {
	auto scoped = lockThread(); // Shouldn't get stuck in a cycle even if no relaxed threads are available, because the `WclapThread` constructor always passes itself to `.claimArenas()`
	scoped.arenas.resetTemporary();
	return claimArenas(&scoped.thread);
}

std::unique_ptr<WclapArenas> Wclap::claimArenas(WclapThread *lockedThread) {
	if (!lockedThread) return claimArenas();
	auto lock = writeLock();
	if (arenaPool.size()) {
		std::unique_ptr<WclapArenas> result = std::move(arenaPool.back());
		arenaPool.pop_back();
		return result;
	}
	std::unique_ptr<WclapArenas> result{
		new WclapArenas(*this, *lockedThread, arenaList.size())
	};
	arenaList.push_back(result.get());
	return result;
}
void Wclap::returnArenas(std::unique_ptr<WclapArenas> &arenas) {
	arenas->resetIncludingPersistent();
	auto lock = writeLock();
	arenaPool.emplace_back(std::move(arenas));
}

WclapArenas * Wclap::arenasForWasmContext(uint64_t wasmContextP) {
	size_t index = *lockThread().viewDirectPointer<size_t>(wasmContextP);
	auto lock = readLock();
	if (index < arenaList.size()) return arenaList[index];
	return nullptr;
}

ScopedThread Wclap::lockThread() {
	if (currentScopedThread) {
		// already a scoped thread somewhere up the stack for this thread, re-use that
		return ScopedThread::weakCopy(*currentScopedThread);
	}
	if (!sharedMemory) return lockGlobalThread();

	{
		auto lock = readLock();
		for (auto &threadPtr : relaxedThreadPool) {
			if (threadPtr->mutex.try_lock()) return {*threadPtr, *threadPtr->arenas};
		}
	}

	auto lock = writeLock();
	auto *rawPtr = new WclapThreadWithArenas(*this);
	if (wasm64) {
		wclap::wclap64::methodsRegister(methods64, *rawPtr);
	} else {
		wclap::wclap32::methodsRegister(methods32, *rawPtr);
	}
	rawPtr->mutex.lock();
	relaxedThreadPool.emplace_back(rawPtr);
	return {*rawPtr, *rawPtr->arenas};
}
ScopedThread Wclap::lockThread(WclapThread *ptr, WclapArenas &arenas) {
	arenas.resetTemporary();
	if (!ptr) {
		// We're expecting an exclusive lock (e.g. on a realtime thread).  If it's null, it's almost certainly because the WCLAP is single-threaded.
		// But either way, we need something consistent so that the arena doesn't get used simultaneously
		return lockGlobalThread(arenas);
	}
	ptr->mutex.lock();
	return {*ptr, arenas};
}
ScopedThread Wclap::lockGlobalThread() {
	if (currentScopedThread && currentScopedThreadIsGlobal) {
		// Global thread is already locked somewhere up the stack
		return ScopedThread::weakCopy(*currentScopedThread);
	}
	currentScopedThreadIsGlobal = true;
	return lockThread(globalThread.get(), *globalArenas);
}
ScopedThread Wclap::lockGlobalThread(WclapArenas &arenas) {
	if (currentScopedThread && currentScopedThreadIsGlobal) {
		// Global thread is already locked somewhere up the stack
		return ScopedThread::weakCopy(*globalThread, arenas);
	}
	currentScopedThreadIsGlobal = true;
	return lockThread(globalThread.get(), arenas);
}

void wclapSetError(Wclap &wclap, const char *message) {
	wclap.setError(message);
}

} // namespace
