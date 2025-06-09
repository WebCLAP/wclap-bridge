#pragma once

#include "clap/all.h"

#include "./wclap-proxies.h"
#include "./wclap-arenas.h"

#include <fstream>
#include <vector>
#include <shared_mutex>
#include <string>
#include <atomic>
#include <iostream>

namespace wclap {

extern const char *wclap_error_message;
extern std::string wclap_error_message_string;
extern std::atomic_flag global_config_ready;

struct WclapThread;
struct WclapThreadWithArenas;
struct ScopedThread;

struct WclapImpl;

namespace wclap32 {
	struct WclapMethods;
}
namespace wclap64 {
	struct WclapMethods;
}

struct Wclap {
	clap_version clapVersion;
	const char *errorMessage = nullptr; // something that happened while executing the WCLAP - it still exists (and requires cleanup), but isn't reliable

	// Call this and it'll get logged as well
	void setError(const char *message) {
		// Only keep the first error, that's probably the root of any problems
		if (!errorMessage) errorMessage = message;
		std::cerr << message << std::endl;
	}

	uint8_t * wasmMemory(WclapThread &lockedThread, uint64_t wasmP, uint64_t size);
	uint64_t wasmMemorySize(WclapThread &lockedThread);

	const std::string wclapDir, presetDir, cacheDir, varDir;
	const bool mustLinkDirs;

	Wclap(const std::string &wclapDir, const std::string &presetDir, const std::string &cacheDir, const std::string &varDir, bool mustLinkDirs);
	~Wclap();

	void initWasmBytes(const uint8_t *bytes, size_t size);
	void deinit();
	
	bool singleThreaded() const;

	// obtains a thread for realtime calls by removing from the pool, or creating if needed
	std::unique_ptr<WclapThread> claimRealtimeThread();
	// hands it back
	void returnRealtimeThread(std::unique_ptr<WclapThread> &ptr);

	std::unique_ptr<WclapArenas> claimArenas();
	// if we have a thread already, we'll use that if we need to malloc() anything
	std::unique_ptr<WclapArenas> claimArenas(WclapThread *lockedThread);
	void returnArenas(std::unique_ptr<WclapArenas> &arenas);

	WclapArenas * arenasForWasmContext(uint64_t wasmContextP);

	// Used to lock a specific thread (e.g. a realtime one owned by a plugin/whatever)
	ScopedThread lockThread(WclapThread *ptr, WclapArenas &arenas);
	// Either locks a relaxed thread from the pool, or continues the current locked thread if there's already one further up the OS thread's stack
	ScopedThread lockThread();
	// Can be locked multiple times on the same OS thread
	ScopedThread lockGlobalThread();
	ScopedThread lockGlobalThread(WclapArenas &arenas);

	const void * getFactory(const char *factory_id);
	
	// All translated plugin extensions point to the same structs
	struct {
		//ProxiedClapStruct<clap_ext_plugin_params> params;
	} pluginExtensions;
	
	wclap32::WclapMethods & methods(uint32_t) {
		return *methods32;
	}
	wclap64::WclapMethods & methods(uint64_t) {
		return *methods64;
	}

	WclapImpl *impl = nullptr;

	bool wasm64 = false;
private:
	bool initSuccess = false;

	void implCreate();
	void implDestroy();

	wclap32::WclapMethods *methods32 = nullptr;
	wclap64::WclapMethods *methods64 = nullptr;

	mutable std::shared_mutex mutex;
	std::shared_lock<std::shared_mutex> readLock() const {
		return std::shared_lock<std::shared_mutex>{mutex};
	}
	std::unique_lock<std::shared_mutex> writeLock() {
		return std::unique_lock<std::shared_mutex>{mutex};
	}

	// We keep a list of all arenas, and host proxy objects (in WASM memory) reference them by index, instead of trusting the WCLAP to give us anything valid
	std::vector<WclapArenas *> arenaList;
	std::vector<std::unique_ptr<WclapArenas>> arenaPool; // removed from pool

	std::unique_ptr<WclapThread> globalThread;
	std::unique_ptr<WclapArenas> globalArenas;
	std::vector<std::unique_ptr<WclapThread>> realtimeThreadPool; // removed from the pool, to be locked later
	std::vector<std::unique_ptr<WclapThreadWithArenas>> relaxedThreadPool; // never leaves the pool, arenas are always temporary.  Called "relaxed" because (unlike realtime threads which are known to already exist) this might need to call Wclap::writeLock() for an exclusive lock while it allocates a new one
};

void wclapSetError(Wclap &wclap, const char *message);

} // namespace

#ifdef WCLAP_ENGINE_WASMTIME
#	include "./wasmtime/wclap-impl.h"
#else
#	error No WASM engine selected
#endif
