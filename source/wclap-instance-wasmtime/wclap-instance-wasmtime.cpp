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
	if (singleThread) return nullptr;
	
	auto thread = new wclap::Instance<wclap_wasmtime::InstanceImpl>(*this);
	if (!wtSharedMemory) { // single-threaded mode
		singleThread = thread;
	}
	
	return std::unique_ptr<wclap::Instance<wclap_wasmtime::InstanceImpl>>{thread};
}

bool wclap_wasmtime::InstanceGroup::globalInit(unsigned int timeLimitMs) {
	wasm_config_t *config = wasm_config_new();
	if (!config) {
		std::cerr << "couldn't create Wasmtime config\n";
		return false;
	}
	auto error = wasmtime_config_cache_config_load(config, nullptr);
	if (error) {
		logError(error);
		wasmtime_error_delete(error);
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
	auto *error = wasmtime_module_new(globalWasmEngine, wasmBytes, wasmLength, &wtModule);
	if (error) {
		setError(error);
		return;
	}
	
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

			auto error = wasmtime_sharedmemory_new(globalWasmEngine, asMemory, &wtSharedMemory);
			if (error) {
				setError(error);
				return;
			}
			if (!wtSharedMemory) return stopWithError("Shared memory wasn't created");
			sharedMemoryImportModule = nameToStr(module);
			sharedMemoryImportName = nameToStr(name);
		}
		wasm_importtype_vec_delete(&importTypes);
	}
}

bool wclap_wasmtime::InstanceImpl::setup() {
	if (group.hasError()) return false;
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

	{
		auto error = wasmtime_linker_define_wasi(wtLinker);
		if (error) return stopWithError("error linking WASI");
	}

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
	
	// Link various directories
	if (group.wclapDir) {
		if (!wasi_config_preopen_dir(wasiConfig, group.wclapDir->c_str(), "/plugin.wclap/", WASMTIME_WASI_DIR_PERMS_READ, WASMTIME_WASI_FILE_PERMS_READ)) {
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

	{
		auto error = wasmtime_context_set_wasi(wtContext, wasiConfig);
		if (error) {
			group.setError(error);
			wasi_config_delete(wasiConfig);
			return stopWithError("Failed to configure WASI");
		}
	}
	wasiConfig = nullptr; // owned by the context now

	//---------- WASI threads ----------//

	if (group.wtSharedMemory) { // threads are only possible with a shared-memory import
		wasmtime_extern_t item;
		item.kind = WASMTIME_EXTERN_FUNC;

		auto *fnType = group.is64() ? makeWasmtimeFuncType<uint32_t, uint64_t>() : makeWasmtimeFuncType<uint32_t, uint32_t>();
		wasmtime_func_new_unchecked(wtContext, fnType, InstanceGroup::wtWasiThreadSpawn, &group, nullptr, &item.of.func);
		wasm_functype_delete(fnType);

		auto &module = group.sharedMemoryImportModule;
		auto &name = group.sharedMemoryImportName;
		auto error = wasmtime_linker_define(wtLinker, wtContext, "wasi", 4, "thread-spawn", 12, &item);
		if (error) {
			group.setError(error);
			return stopWithError("error linking wasi::thread-spawn import");
		}
	}

	//---------- Shared-memory import ----------//

	if (group.wtSharedMemory) { // memory import
		wasmtime_extern_t item;
		item.kind = WASMTIME_EXTERN_SHAREDMEMORY;
		item.of.sharedmemory = group.wtSharedMemory;
		
		auto &module = group.sharedMemoryImportModule;
		auto &name = group.sharedMemoryImportName;
		auto error = wasmtime_linker_define(wtLinker, wtContext, module.c_str(), module.size(), name.c_str(), name.size(), &item);
		if (error) {
			group.setError(error);
			return stopWithError("error linking shared-memory import");
		}
	}

	//---------- Start the instance ----------//

	{
		// This doesn't call the WASI _start() or _initialize() methods
		setWasmDeadline();
		wasm_trap_t *trap = nullptr;
		auto *error = wasmtime_linker_instantiate(wtLinker, wtContext, group.wtModule, &wtInstance, &trap);
		if (error) {
			group.setError(error);
			return stopWithError("Failed to create instance (error)");
		}
		if (trap) {
			group.setError(trap, "Failed to start instance (timeout)", "Failed to start instance (trap)");
			return false;
		}
	}

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
	bool foundTable = false;
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
				foundTable = true;
				break;
			}
		}
		wasmtime_extern_delete(&item);
		++exportIndex;
	}
	if (!foundTable) return stopWithError("couldn't find function table in WCLAP");
	return true;
}

wasm_trap_t * wclap_wasmtime::InstanceGroup::wtWasiThreadSpawn(void *context, wasmtime_caller *, wasmtime_val_raw *values, size_t argCount) {
	auto &group = *(InstanceGroup *)context;
	if (!group.wasiThreadSpawn) {
		values[0].i32 = -1; // failure
		return nullptr;
	}
	uint64_t threadArg = group.is64() ? uint64_t(values[0].i64) : uint32_t(values[0].i32);
	values[0].i32 = group.wasiThreadSpawn(group.wasiThreadSpawnContext, threadArg);
	return nullptr;
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
		wasm_trap_t *trap = nullptr;
		auto error = wasmtime_func_call(wtContext, &item.of.func, nullptr, 0, nullptr, 0, &trap);
		if (error) {
			group.setError(error);
			wasmtime_extern_delete(&item);
			return stopWithError("error calling _initialize()");
		} else if (trap) {
			group.setError(trap, "_initialize() timeout", "_initialize() threw (trapped)");
			wasmtime_extern_delete(&item);
			return false;
		}
		wasmtime_extern_delete(&item);
	}
	return true;
}

void wclap_wasmtime::InstanceImpl::runThread(uint32_t threadId, uint64_t threadArg) {
	auto stopWithError = [&](const char *message) -> void {
		group.setError(message);
	};

	if (!group.hadInit) {
		return stopWithError("Instance Group not initialised before .runThread()");
	}

	wasmtime_extern_t item;
	if (!wasmtime_instance_export_get(wtContext, &wtInstance, "wasi_thread_start", 17, &item)) {
		return stopWithError("wasi_thread_start not found");
	}
	if (item.kind != WASMTIME_EXTERN_FUNC) {
		wasmtime_extern_delete(&item);
		return stopWithError("wasi_thread_start isn't a function");
	}

	wasmtime_val_raw_t wasmVals[2];
	wasmVals[0] = {.i32=int32_t(threadId)};
	if (group.is64()) {
		wasmVals[1] = {.i64=int64_t(threadArg)};
	} else {
		wasmVals[1] = {.i32=int32_t(uint32_t(threadArg))};
	}

	setWasmDeadline();
	// Threads are allowed to continue unless explicitly stopped, but we use the deadline for checking in
	wasmtime_store_epoch_deadline_callback(wtStore, continueChecker, handle, nullptr);

	wasm_trap_t *trap = nullptr;
	auto error = wasmtime_func_call_unchecked(wtContext, &item.of.func, wasmVals, 2, &trap);
	if (error) {
		group.setError(error);
		wasmtime_extern_delete(&item);
		return stopWithError("error calling wasi_thread_start()");
	} else if (trap) {
		group.setError(trap, "wasi_thread_start() terminated early", "wasi_thread_start() threw (trapped)");
		wasmtime_extern_delete(&item);
	}
}

uint64_t wclap_wasmtime::InstanceImpl::wtMalloc(size_t bytes) {
	std::lock_guard<std::recursive_mutex> lock(callMutex);

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
	{
		wasm_trap_t *trap = nullptr;
		auto error = wasmtime_func_call(wtContext, &wtMallocFunc, args, 1, results, 1, &trap);
		if (error) {
			group.setError(error);
			group.setError("calling malloc() failed");
			return 0;
		}
		if (trap) {
			group.setError(trap, "malloc() timeout", "malloc() threw (trapped)");
			return 0;
		}
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

wasmtime_error_t * wclap_wasmtime::InstanceImpl::continueChecker(wasmtime_context_t *context, void *data, uint64_t *epochsDelta, wasmtime_update_deadline_kind_t *updateKind) {
	auto *instance = (wclap::Instance<InstanceImpl> *)data;
	if (instance->shouldStop()) {
		return wasmtime_error_new("WCLAP thread terminated");
	}
	// Move the deadline back
	*epochsDelta = timeLimitEpochs;
	*updateKind = WASMTIME_UPDATE_DEADLINE_CONTINUE;
	return nullptr;
}
