#pragma once

#include "wasmtime.h"
#include "clap/all.h"

#include "./wclap-proxies.h"
#include "./wclap-arenas.h"

#include <fstream>
#include <vector>
#include <shared_mutex>
#include <string>
#include <atomic>

namespace wclap {

extern std::atomic<wasm_engine_t *> global_wasm_engine;
extern const char *wclap_error_message;
extern std::string wclap_error_message_string;

static bool nameEquals(const wasm_name_t *name, const char *cName) {
	if (name->size != std::strlen(cName)) return false;
	for (size_t i = 0; i < name->size; ++i) {
		if (name->data[i] != cName[i]) return false;
	}
	return true;
}

struct WclapThread;
struct WclapThreadWithArenas;

namespace wclap32 {
	struct WclapMethods;
}
namespace wclap64 {
	struct WclapMethods;
}

struct Wclap {
	clap_version clapVersion;
	const char *errorMessage = nullptr; // something that happened while executing the WCLAP - it still exists (and requires cleanup), but isn't reliable

	uint8_t * wasmMemory(uint64_t wasmP);

	template<class AutoTranslatedStruct>
	AutoTranslatedStruct view(uint64_t wasmP) {
		// TODO: bounds check
		bool valid = wasmP && !(wasmP%AutoTranslatedStruct::wasmAlign);
		return AutoTranslatedStruct{valid ? wasmMemory(wasmP) : nullptr};
	}

	const std::string wclapDir, presetDir, cacheDir, varDir;
	const bool mustLinkDirs;

	Wclap(const std::string &wclapDir, const std::string &presetDir, const std::string &cacheDir, const std::string &varDir, bool mustLinkDirs);
	~Wclap();

	void initWasmBytes(const uint8_t *bytes, size_t size);
	void deinit();

	// obtains a thread for realtime calls by removing from the pool, or creating if needed
	std::unique_ptr<WclapThread> claimRealtimeThread();
	void returnRealtimeThread(std::unique_ptr<WclapThread> &ptr);

	std::unique_ptr<WclapArenas> claimArenas(WclapThread *lockedThread);
	std::unique_ptr<WclapArenas> claimArenas();
	void returnArenas(std::unique_ptr<WclapArenas> &arenas);

	WclapArenas * arenasForWasmContext(uint64_t wasmContextP) {
		size_t index = *(size_t *)wasmMemory(wasmContextP);
		auto lock = readLock();
		if (index < arenaList.size()) return arenaList[index];
		return nullptr;
	}

	// borrows a locked non-realtime thread, adding to the pool if necessary
	struct ScopedThread {
		WclapThread &thread;
		WclapArenas &arenas;

		ScopedThread(WclapThread &alreadyLocked, WclapArenas &arenas) : thread(alreadyLocked), arenas(arenas) {}

		ScopedThread(ScopedThread &&other) : thread(other.thread), arenas(other.arenas) {
			other.locked = false;
		}
		~ScopedThread(); // unlocks the mutex (if `locked`)
	private:
		bool locked = true;
	};
	ScopedThread lockThread(WclapThread *ptr, WclapArenas &arenas);
	//ScopedThread lockThread(WclapThreadWithArenas *ptr);
	ScopedThread lockRelaxedThread();
	ScopedThread lockGlobalThread();

	const void * getFactory(const char *factory_id);
	
	// All translated host extensions point to the same
	struct {
		//ProxiedClapStruct<clap_ext_host_params> params;
	} extensionProxies;
private:
	bool initSuccess = false;

	// Wasmtime stuff
	friend class WclapThread;
	wasmtime_module_t *module = nullptr;
	wasmtime_error_t *error = nullptr;
	bool wasm64 = false;
	wasmtime_sharedmemory_t *sharedMemory = nullptr;

	wclap32::WclapMethods *methods32 = nullptr;
	wclap64::WclapMethods *methods64 = nullptr;

	mutable std::shared_mutex mutex;
	std::shared_lock<std::shared_mutex> readLock() const {
		return std::shared_lock<std::shared_mutex>{mutex};
	}
	std::unique_lock<std::shared_mutex> writeLock() {
		return std::unique_lock<std::shared_mutex>{mutex};
	}

	// We keep a list of all arenas, and host proxy objects (in WASM memory) reference them by index, so we can check validity
	std::vector<WclapArenas *> arenaList;
	std::vector<std::unique_ptr<WclapArenas>> arenaPool; // removed from pool

	std::unique_ptr<WclapThreadWithArenas> globalThread; // used for creating contexts, or for everything in single-threaded mode
	std::vector<std::unique_ptr<WclapThread>> realtimeThreadPool; // removed from the pool, locked later
	std::vector<std::unique_ptr<WclapThreadWithArenas>> relaxedThreadPool; // never leaves the pool
};

// We need this to resolve a circular dependency in the WclapArenas.view() template
template<class AutoTranslatedStruct>
AutoTranslatedStruct wclapWasmView(Wclap &wclap, uint64_t wasmP) {
	return wclap.view<AutoTranslatedStruct>(wasmP);
}

} // namespace
