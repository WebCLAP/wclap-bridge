#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "wclap-bridge.h"
#include "../wclap.h"
#include "../wclap-thread.h"
#include "../wclap32/wclap-translation.h"
#include "../wclap64/wclap-translation.h"

#include "./common.h"

#include <thread>
#include <atomic>

std::ostream & operator<<(std::ostream &s, const wasm_byte_vec_t &bytes) {
	for (size_t i = 0; i < bytes.size; ++i) {
		s << bytes.data[i];
	}
	return s;
}

namespace wclap {
	extern unsigned int timeLimitEpochs;

	std::atomic<wasm_engine_t *> global_wasm_engine;

	static std::atomic_flag globalEpochRunning;
	static void epochThreadFunction() {
		while (globalEpochRunning.test()) {
			wasmtime_engine_increment_epoch(wclap::global_wasm_engine);
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
	}
	static std::thread globalEpochThread;
}

bool wclap_global_init(unsigned int timeLimitMs) {
	wasm_config_t *config = wasm_config_new();
	if (!config) {
		wclap::wclap_error_message = "couldn't create config";
		return false;
	}

	if (timeLimitMs > 0) {
		// enable epoch_interruption to prevent WCLAPs locking everything up - has a speed cost (10% according to docs)
		wasmtime_config_epoch_interruption_set(config, true);
		wclap::timeLimitEpochs = timeLimitMs/10 + 2;
	}
	
	wclap::global_wasm_engine = wasm_engine_new_with_config(config);
	if (!wclap::global_wasm_engine) {
		wclap::wclap_error_message = "couldn't create engine";
		wasm_config_delete(config);
		return false;
	}

	if (timeLimitMs > 0) {
		wclap::globalEpochRunning.test_and_set();
		wclap::globalEpochThread = std::thread{wclap::epochThreadFunction};
	}
	wclap::global_config_ready.test_and_set();
	return true;
}
void wclap_global_deinit() {
	if (wclap::globalEpochThread.joinable()) {
		wclap::globalEpochRunning.clear();
		wclap::globalEpochThread.join();
	}
	
	if (wclap::global_wasm_engine) {
		wasm_engine_delete(wclap::global_wasm_engine);
		wclap::global_wasm_engine = nullptr;
	}
	wclap::global_config_ready.clear();
}

namespace wclap {

static bool nameEquals(const wasm_name_t *name, const char *cName) {
	if (name->size != std::strlen(cName)) return false;
	for (size_t i = 0; i < name->size; ++i) {
		if (name->data[i] != cName[i]) return false;
	}
	return true;
}

//---------- Common Wclap instance ----------//

void Wclap::implCreate() {
	impl = new WclapImpl();
}

void Wclap::implDestroy() {
	if (impl->sharedMemory) {
		wasmtime_sharedmemory_delete(impl->sharedMemory);
	}
	
	if (impl->error) wasmtime_error_delete(impl->error);
	if (impl->module) wasmtime_module_delete(impl->module);
	delete impl;
}

uint8_t * Wclap::wasmMemory(WclapThread &lockedThread, uint64_t wasmP, uint64_t size) {
	if (impl->sharedMemory) {
		auto memorySize = wasmtime_sharedmemory_data_size(impl->sharedMemory);
		wasmP = std::min<uint64_t>(wasmP, memorySize - size);
		return wasmtime_sharedmemory_data(impl->sharedMemory) + wasmP;
	} else {
		auto memorySize = wasmtime_memory_data_size(lockedThread.impl->context, &lockedThread.impl->memory);
		wasmP = std::min<uint64_t>(wasmP, memorySize - size);
		return wasmtime_memory_data(lockedThread.impl->context, &lockedThread.impl->memory) + wasmP;
	}
}

uint64_t Wclap::wasmMemorySize(WclapThread &lockedThread) {
	if (impl->sharedMemory) {
		return wasmtime_sharedmemory_data_size(impl->sharedMemory);
	} else {
		return wasmtime_memory_data_size(lockedThread.impl->context, &lockedThread.impl->memory);
	}
}

void Wclap::initWasmBytes(const uint8_t *bytes, size_t size) {
	impl->error = wasmtime_module_new(global_wasm_engine, bytes, size, &impl->module);
	if (impl->error) return setError("Failed to compile module");
	
	bool foundClapEntry = false;
	{ // Find `clap_entry`, which is a memory-address pointer, so the type of it tells us if it's 64-bit
		wasm_exporttype_vec_t exportTypes;
		wasmtime_module_exports(impl->module, &exportTypes);
		
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
		wasmtime_module_imports(impl->module, &importTypes);
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
			if (impl->sharedMemory) {
				setError("multiple memory imports");
				break;
			}

			impl->error = wasmtime_sharedmemory_new(global_wasm_engine, asMemory, &impl->sharedMemory);
			if (impl->error || !impl->sharedMemory) {
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

bool Wclap::singleThreaded() const {
	return !impl->sharedMemory;
}

} // namespace
