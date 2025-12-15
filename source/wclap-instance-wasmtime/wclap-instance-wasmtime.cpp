#include "./wclap-instance-wasmtime.h"

#include <atomic>
#include <thread>
#include <vector>

static std::atomic<wasm_engine_t *> globalWasmEngine;

static std::atomic_flag globalEpochRunning = ATOMIC_FLAG_INIT;
static constexpr size_t epochCounterMs = 10;
static void epochThreadFunction() {
	while (globalEpochRunning.test()) {
		wasmtime_engine_increment_epoch(globalWasmEngine);
		std::this_thread::sleep_for(std::chrono::milliseconds(epochCounterMs));
	}
}
std::atomic<size_t> timeLimitEpochs = 0;
static std::thread globalEpochThread;

std::unique_ptr<wclap::Instance<wclap_wasmtime::InstanceImpl>> wclap_wasmtime::InstanceGroup::startInstance() {
	return std::unique_ptr<wclap::Instance<wclap_wasmtime::InstanceImpl>>{
		new wclap::Instance<wclap_wasmtime::InstanceImpl>(*this)
	};
}

bool wclap_wasmtime::InstanceGroup::globalInit(unsigned int timeLimitMs) {
	wasm_config_t *config = wasm_config_new();
	if (!config) {
		std::cerr << "couldn't create Wasmtime config\n";
		return false;
	}

	if (timeLimitMs > 0) {
		// enable epoch_interruption to prevent WCLAPs locking everything up - has a speed cost (10% according to docs)
		wasmtime_config_epoch_interruption_set(config, true);
		timeLimitEpochs = timeLimitMs/epochCounterMs + 2;
		// TODO: wasmtime_store_epoch_deadline_callback(), to have a budget instead of a hard per-call limit
	} else {
		timeLimitEpochs = 0; // not active
	}
	
	globalWasmEngine = wasm_engine_new_with_config(config);
	if (!globalWasmEngine) {
		std::cerr << "couldn't create Wasmtime engine\n";
		wasm_config_delete(config);
		return false;
	}

	if (timeLimitMs > 0) {
		// start the epoch thread
		globalEpochRunning.test_and_set();
		globalEpochThread = std::thread{epochThreadFunction};
	}
	return true;
}
void wclap_wasmtime::InstanceGroup::globalDeinit() {
	if (globalEpochThread.joinable()) {
		// stop the epoch thread
		globalEpochRunning.clear();
		globalEpochThread.join();
	}
	
	if (globalWasmEngine) {
		wasm_engine_delete(globalWasmEngine);
		globalWasmEngine = nullptr;
	}
}

void wclap_wasmtime::InstanceGroup::setup(const unsigned char *wasmBytes, size_t wasmLength) {
	wtError = wasmtime_module_new(globalWasmEngine, wasmBytes, wasmLength, &wtModule);
	if (wtError) return;
	
	auto stopWithError = [&](const char *message) -> void {
		constantErrorMessage = message;
	};

	bool foundClapEntry = false;
	{ // Find `clap_entry`, which is a memory-address pointer, so the type of it tells us if it's 64-bit
		wasm_exporttype_vec_t exportTypes;
		wasmtime_module_exports(wtModule, &exportTypes);
		
		for (size_t i = 0; i < exportTypes.size; ++i) {
			auto *exportType = exportTypes.data[i];
			auto *name = wasm_exporttype_name(exportType);
			if (!nameEquals(name, "clap_entry")) continue;
			foundClapEntry = true;
			auto *externType = wasm_exporttype_type(exportType);
			auto *globalType = wasm_externtype_as_globaltype_const(externType);
			if (!globalType) return stopWithError("clap_entry is not a global (value) export");
			auto *valType = wasm_globaltype_content(globalType);
			auto kind = wasm_valtype_kind(valType);
			
			if (kind == WASMTIME_I64) {
				wasm64 = true;
				break;
			} else if (kind != WASMTIME_I32) {
				return stopWithError("clap_entry must be 32-bit or 64-bit memory address");
			}
		}
		wasm_exporttype_vec_delete(&exportTypes);
	}
	if (!foundClapEntry) return stopWithError("clap_entry not exported");

	{ // Check for a shared-memory import - otherwise it's single-threaded
		wasm_importtype_vec_t importTypes;
		wasmtime_module_imports(wtModule, &importTypes);
		for (size_t i = 0; i < importTypes.size; ++i) {
			auto *importType = importTypes.data[i];
			auto *module = wasm_importtype_module(importType);
			auto *name = wasm_importtype_name(importType);
			auto *externType = wasm_importtype_type(importType);

			auto *asMemory = wasm_externtype_as_memorytype_const(externType);
			if (!asMemory) continue;

			if (!wasmtime_memorytype_isshared(asMemory)) {
				return stopWithError("imports non-shared memory");
			}
			bool mem64 = wasmtime_memorytype_is64(asMemory);
			if (mem64 != wasm64) {
				return stopWithError(mem64 ? "64-bit memory but 32-bit clap_entry" : "32-bit memory but 64-bit clap_entry");
			}
			if (wtSharedMemory) return stopWithError("multiple memory imports");

			wtError = wasmtime_sharedmemory_new(globalWasmEngine, asMemory, &wtSharedMemory);
			if (wtError) return;
			if (!wtSharedMemory) return stopWithError("Shared memory wasn't created");
			sharedMemoryImportModule = nameToStr(module);
			sharedMemoryImportName = nameToStr(name);
		}
		wasm_importtype_vec_delete(&importTypes);
	}
}

bool wclap_wasmtime::InstanceImpl::setup() {
	auto stopWithError = [&](const char *message) -> bool {
		group.setError(message);
		return false;
	};

	wtStore = wasmtime_store_new(globalWasmEngine, nullptr, nullptr);
	if (!wtStore) return stopWithError("Failed to create store");

	wtContext = wasmtime_store_context(wtStore);
	if (!wtContext) return stopWithError("Failed to get context");
	
	// Create a linker with WASI functions defined
	wtLinker = wasmtime_linker_new(globalWasmEngine);
	if (!wtLinker) return stopWithError("error creating linker");

	wtError = wasmtime_linker_define_wasi(wtLinker);
	if (wtError) return stopWithError("error linking WASI");

	//---------- WASI config ----------//

	wasi_config_t *wasiConfig = wasi_config_new();
	if (!wasiConfig) return stopWithError("Failed to create WASI config");
	// Everything after this point needs to delete `wasiConfig` if it fails
	
	wasi_config_inherit_stdout(wasiConfig);
	wasi_config_inherit_stderr(wasiConfig);
	
	// Set a few specific environment variables
	std::vector<const char *> envNames, envValues;
	auto addEnv = [&](const char *name) {
		auto *value = std::getenv(name);
		if (value) {
			envNames.push_back(name);
			envValues.push_back(value);
		}
	};
	addEnv("TERM");
	addEnv("LANG");
	if (envNames.size()) {
		wasi_config_set_env(wasiConfig, envNames.size(), envNames.data(), envValues.data());
	}
	
	// Link various directories - failure is allowed if `mustLinkDirs` is false
	if (group.wclapDir) {
		if (!wasi_config_preopen_dir(wasiConfig, group.wclapDir->c_str(), "/plugin/", WASMTIME_WASI_DIR_PERMS_READ, WASMTIME_WASI_FILE_PERMS_READ)) {
			std::cerr << "WASI: failed to link " << *group.wclapDir << std::endl;
		}
	}
	if (group.presetDir) {
		if (!wasi_config_preopen_dir(wasiConfig, group.presetDir->c_str(), "/presets/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			std::cerr << "WASI: failed to link " << *group.presetDir << std::endl;
		}
	}
	if (group.cacheDir) {
		if (!wasi_config_preopen_dir(wasiConfig, group.cacheDir->c_str(), "/cache/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			std::cerr << "WASI: failed to link " << *group.cacheDir << std::endl;
		}
	}
	if (group.varDir) {
		if (!wasi_config_preopen_dir(wasiConfig, group.varDir->c_str(), "/var/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			std::cerr << "WASI: failed to link " << *group.varDir << std::endl;
		}
	}

	wtError = wasmtime_context_set_wasi(wtContext, wasiConfig);
	if (wtError) {
		logError(wtError);
		wasi_config_delete(wasiConfig);
		return stopWithError("Failed to configure WASI");
	}
	wasiConfig = nullptr; // owned by the context now

	//---------- Shared-memory import ----------//

	if (group.wtSharedMemory) { // memory import
		wasmtime_extern_t item;
		item.kind = WASMTIME_EXTERN_SHAREDMEMORY;
		item.of.sharedmemory = group.wtSharedMemory;
		
		auto &module = group.sharedMemoryImportModule;
		auto &name = group.sharedMemoryImportName;
		wtError = wasmtime_linker_define(wtLinker, wtContext, module.c_str(), module.size(), name.c_str(), name.size(), &item);
		if (wtError) return stopWithError("error linking shared-memory import");
	}

	//---------- Start the instance ----------//

	// This doesn't call the WASI _start() or _initialize() methods
	setWasmDeadline();
	wtError = wasmtime_linker_instantiate(wtLinker, wtContext, group.wtModule, &wtInstance, &wtTrap);
	if (wtError) return stopWithError("Failed to create instance (error)");
	if (wtTrap) return stopWithError("Failed to start instance (trap)");

	//---------- Find exports ----------//

	char *name;
	size_t nameSize;
	wasmtime_extern_t item;
	
	if (wasmtime_instance_export_get(wtContext, &wtInstance, "memory", 6, &item)) {
		if (item.kind == WASMTIME_EXTERN_MEMORY) {
			wtMemory = item.of.memory;
		} else if (item.kind == WASMTIME_EXTERN_SHAREDMEMORY) {
			// TODO: it should be the same as the import - not sure how to check this
			if (!group.wtSharedMemory) {
				return stopWithError("exported shared memory, but didn't import it");
			}
		} else {
			wasmtime_extern_delete(&item);
			return stopWithError("exported memory isn't a (Shared)Memory");
		}
		wasmtime_extern_delete(&item);
	} else if (!group.wtSharedMemory) {
		return stopWithError("must either export memory or import shared memory");
	}

	if (!wasmtime_instance_export_get(wtContext, &wtInstance, "clap_entry", 10, &item)) {
		return stopWithError("clap_entry not exported");
	}
	if (item.kind == WASMTIME_EXTERN_GLOBAL) {
		wasmtime_val_t v;
		wasmtime_global_get(wtContext, &item.of.global, &v);
		if (v.kind == WASM_I32 && !group.is64()) {
			wclapEntryAs64 = v.of.i32; // We store it as 64 bits, even though we know it's a 32-bit one
		} else if (v.kind == WASM_I64 && group.is64()) {
			wclapEntryAs64 = v.of.i64;
		} else {
			return stopWithError("clap_entry is not a (correctly-sized) pointer");
		}
	} else {
		wasmtime_extern_delete(&item);
		return stopWithError("clap_entry isn't a Global");
	}
	wasmtime_extern_delete(&item);

	if (!wasmtime_instance_export_get(wtContext, &wtInstance, "malloc", 6, &item)) {
		return stopWithError("malloc not exported");
	}
	if (item.kind == WASMTIME_EXTERN_FUNC) {
		wasm_functype_t *type = wasmtime_func_type(wtContext, &item.of.func);
		const wasm_valtype_vec_t *params = wasm_functype_params(type);
		const wasm_valtype_vec_t *results = wasm_functype_results(type);
		if (params->size != 1 || results->size != 1) {
			wasmtime_extern_delete(&item);
			return stopWithError("malloc() function signature mismatch");
		}
		if (wasm_valtype_kind(params->data[0]) != wasm_valtype_kind(results->data[0])) {
			wasmtime_extern_delete(&item);
			return stopWithError("malloc() function signature mismatch");
		}
		if (group.is64()) {
			if (wasm_valtype_kind(params->data[0]) != WASMTIME_I64) {
				wasmtime_extern_delete(&item);
				return stopWithError("malloc() function signature mismatch");
			}
		} else {
			if (wasm_valtype_kind(params->data[0]) != WASMTIME_I32) {
				wasmtime_extern_delete(&item);
				return stopWithError("malloc() function signature mismatch");
			}
		}
		wtMallocFunc = item.of.func;
		wasm_functype_delete(type);
	} else {
		wasmtime_extern_delete(&item);
		return stopWithError("malloc isn't a Function");
	}
	wasmtime_extern_delete(&item);

	// Look for the first function table
	size_t exportIndex = 0;
	while (wasmtime_instance_export_nth(wtContext, &wtInstance, exportIndex, &name, &nameSize, &item)) {
		if (item.kind == WASMTIME_EXTERN_TABLE) {
			wasm_tabletype_t *type = wasmtime_table_type(wtContext, &item.of.table);
			const wasm_limits_t limits = *wasm_tabletype_limits(type);
			auto elementKind = wasm_valtype_kind(wasm_tabletype_element(type));
			wasm_tabletype_delete(type);

			if (elementKind == WASM_FUNCREF) {
				if (limits.max < 65536 || limits.max - 65536 < limits.min) {
					return stopWithError("exported function table can't grow enough for CLAP host functions");
				}
				wtFunctionTable = item.of.table;
				break;
			}
		}
		wasmtime_extern_delete(&item);
		++exportIndex;
	}
	return true;
}

bool wclap_wasmtime::InstanceImpl::wasiInit() {
	auto stopWithError = [&](const char *message) -> bool {
		group.setError(message);
		return false;
	};

	wasmtime_extern_t item;

	// Call the WASI entry-point `_initialize()` if it exists - WCLAPs don't *have* to use WASI, so it's fine not to
	if (wasmtime_instance_export_get(wtContext, &wtInstance, "_initialize", 11, &item)) {
		if (item.kind != WASMTIME_EXTERN_FUNC) {
			wasmtime_extern_delete(&item);
			return stopWithError("_initialize isn't a function");
		}
		wasm_functype_t *type = wasmtime_func_type(wtContext, &item.of.func);
		const wasm_valtype_vec_t *params = wasm_functype_params(type);
		const wasm_valtype_vec_t *results = wasm_functype_results(type);
		if (params->size != 0 || results->size != 0) {
			wasmtime_extern_delete(&item);
			return stopWithError("_initialize() function signature mismatch");
		}
		wasm_functype_delete(type);

		setWasmDeadline();
		wtError = wasmtime_func_call(wtContext, &item.of.func, nullptr, 0, nullptr, 0, &wtTrap);
		if (wtError) {
			wasmtime_extern_delete(&item);
			logError(wtError);
			return stopWithError("error calling _initialize()");
		} else if (wtTrap) {
			wasmtime_extern_delete(&item);
			logTrap(wtTrap);
			return stopWithError(trapIsTimeout(wtTrap) ? "_initialize() timeout" : "_initialize() threw (trapped)");
		}
		wasmtime_extern_delete(&item);
	}
	return true;
}

uint64_t wclap_wasmtime::InstanceImpl::wtMalloc(size_t bytes) {
	std::lock_guard<std::mutex> lock(mutex);
	auto stopWithError = [&](const char *message) -> uint64_t {
		group.setError(message);
		return 0;
	};

	uint64_t wasmP;
	
	wasmtime_val_t args[1];
	wasmtime_val_t results[1];
	if (group.is64()) {
		args[0].kind = WASMTIME_I64;
		args[0].of.i64 = bytes;
	} else {
		args[0].kind = WASMTIME_I32;
		args[0].of.i32 = (uint32_t)bytes;
	}
	
	setWasmDeadline();
	wtError = wasmtime_func_call(wtContext, &wtMallocFunc, args, 1, results, 1, &wtTrap);
	if (wtError) {
		logError(wtError);
		return stopWithError("calling malloc() failed");
	}
	if (wtTrap) {
		logTrap(wtTrap);
		return stopWithError(trapIsTimeout(wtTrap) ? "malloc() timeout" : "malloc() threw (trapped)");
	}
	if (group.is64()) {
		if (results[0].kind != WASMTIME_I64) return 0;
		return results[0].of.i64;
	} else {
		if (results[0].kind != WASMTIME_I32) return 0;
		return results[0].of.i32;
	}
}

void wclap_wasmtime::InstanceImpl::setWasmDeadline() {
	if (timeLimitEpochs) {
		wasmtime_context_set_epoch_deadline(wtContext, timeLimitEpochs);
	}
}

