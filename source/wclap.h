#pragma once

#include "wasmtime.h"
#include "clap/all.h"

#include <fstream>
#include <vector>
#include <shared_mutex>
#include <string>

#include "./wclap-arenas.h"

namespace wclap {

extern wasm_engine_t *global_wasm_engine;
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

struct Wclap {
	clap_version clapVersion;
	const char *errorMessage = nullptr; // something that happened while executing the WCLAP - it still exists (and requires cleanup), but isn't reliable

	wasmtime_module_t *module = nullptr;
	wasmtime_error_t *error = nullptr;
	bool wasm64 = false;
	wasmtime_sharedmemory_t *sharedMemory = nullptr;
	
	uint8_t * wasmMemory(uint64_t wasmP);

	template<class AutoTranslatedStruct>
	AutoTranslatedStruct view(uint32_t wasmP) {
		// TODO: bounds check
		return AutoTranslatedStruct{wasmMemory(wasmP)};
	}
	template<class AutoTranslatedStruct>
	AutoTranslatedStruct view(uint64_t wasmP) {
		// TODO: bounds check
		return AutoTranslatedStruct{wasmMemory(wasmP)};
	}

	std::string wclapDir, presetDir, cacheDir, varDir;
	bool mustLinkDirs = false;

	Wclap(const std::string &wclapDir, const std::string &presetDir, const std::string &cacheDir, const std::string &varDir, bool mustLinkDirs);
	~Wclap();

	void initWasmBytes(const uint8_t *bytes, size_t size);
	void deinit();

	std::unique_ptr<WclapThread> singleThread; // if defined, neither pool is ever used
	std::vector<std::unique_ptr<WclapThread>> realtimeThreadPool; // removed from the pool, locked later
	std::vector<std::unique_ptr<WclapThread>> relaxedThreadPool; // never leaves the pool

	// obtains a thread for realtime calls by removing from the pool, or creating if needed
	std::unique_ptr<WclapThread> claimRealtimeThread();
	void returnRealtimeThread(std::unique_ptr<WclapThread> &ptr);
	
	// borrows a locked non-realtime thread, adding to the pool if necessary
	struct ScopedThread {
		WclapThread &thread;
		ScopedThread(WclapThread &alreadyLocked) : thread(alreadyLocked) {}
		ScopedThread(ScopedThread &&other) : thread(other.thread) {
			other.locked = false;
		}
		~ScopedThread(); // unlocks the mutex (if `locked`)
	private:
		bool locked = true;
	};
	ScopedThread lockRelaxedThread();

	const void * getFactory(const char *factory_id);
private:
	bool initSuccess = false;
	
	wclap32::WclapMethods *methods32;

	mutable std::shared_mutex mutex;
	std::shared_lock<std::shared_mutex> readLock() const {
		return std::shared_lock<std::shared_mutex>{mutex};
	}
	std::unique_lock<std::shared_mutex> writeLock() {
		return std::unique_lock<std::shared_mutex>{mutex};
	}
};

} // namespace
