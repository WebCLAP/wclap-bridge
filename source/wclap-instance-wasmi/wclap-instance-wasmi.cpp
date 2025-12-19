#include "./wclap-instance-wasmi.h"

#include <atomic>
#include <thread>
#include <vector>

static std::atomic<wasm_engine_t *> globalWasmEngine;

static std::atomic_flag globalEpochRunning = ATOMIC_FLAG_INIT;
static constexpr size_t epochCounterMs = 10;
static void epochThreadFunction() {
	while (globalEpochRunning.test()) {
		wasmi_engine_increment_epoch(globalWasmEngine);
		std::this_thread::sleep_for(std::chrono::milliseconds(epochCounterMs));
	}
}
std::atomic<size_t> timeLimitEpochs = 0;
static std::thread globalEpochThread;

std::unique_ptr<wclap::Instance<wclap_wasmi::InstanceImpl>> wclap_wasmi::InstanceGroup::startInstance() {
	if (singleThread) return nullptr;
	
	auto thread = new wclap::Instance<wclap_wasmi::InstanceImpl>(*this);
	if (!wtSharedMemory) { // single-threaded mode
		singleThread = thread;
	}
	
	return std::unique_ptr<wclap::Instance<wclap_wasmi::InstanceImpl>>{thread};
}

bool wclap_wasmi::InstanceGroup::globalInit(unsigned int timeLimitMs) {
	wasm_config_t *config = wasm_config_new();
	if (!config) {
		std::cerr << "couldn't create Wasmi config\n";
		return false;
	}
	auto error = wasmi_config_cache_config_load(config, nullptr);
	if (error) {
		logError(error);
		wasmi_error_delete(error);
		return false;
	}

	if (timeLimitMs > 0) {
		// enable epoch_interruption to prevent WCLAPs locking everything up - has a speed cost (10% according to docs)
		wasmi_config_epoch_interruption_set(config, true);
		timeLimitEpochs = timeLimitMs/epochCounterMs + 2;
		// TODO: wasmi_store_epoch_deadline_callback(), to have a budget instead of a hard per-call limit
	} else {
		timeLimitEpochs = 0; // not active
	}
	
	globalWasmEngine = wasm_engine_new_with_config(config);
	if (!globalWasmEngine) {
		std::cerr << "couldn't create Wasmi engine\n";
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
void wclap_wasmi::InstanceGroup::globalDeinit() {
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

void wclap_wasmi::InstanceGroup::setup(const unsigned char *wasmBytes, size_t wasmLength) {
	auto *error = wasmi_module_new(globalWasmEngine, wasmBytes, wasmLength, &wtModule);
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
		wasmi_module_exports(wtModule, &exportTypes);
		
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
			
			if (kind == WASMI_I64) {
				wasm64 = true;
				break;
			} else if (kind != WASMI_I32) {
				return stopWithError("clap_entry must be 32-bit or 64-bit memory address");
			}
		}
		wasm_exporttype_vec_delete(&exportTypes);
	}
	if (!foundClapEntry) return stopWithError("clap_entry not exported");

	{ // Check for a shared-memory import - otherwise it's single-threaded
		wasm_importtype_vec_t importTypes;
		wasmi_module_imports(wtModule, &importTypes);
		for (size_t i = 0; i < importTypes.size; ++i) {
			auto *importType = importTypes.data[i];
			auto *module = wasm_importtype_module(importType);
			auto *name = wasm_importtype_name(importType);
			auto *externType = wasm_importtype_type(importType);

			auto *asMemory = wasm_externtype_as_memorytype_const(externType);
			if (!asMemory) continue;

			if (!wasmi_memorytype_isshared(asMemory)) {
				return stopWithError("imports non-shared memory");
			}
			bool mem64 = wasmi_memorytype_is64(asMemory);
			if (mem64 != wasm64) {
				return stopWithError(mem64 ? "64-bit memory but 32-bit clap_entry" : "32-bit memory but 64-bit clap_entry");
			}
			if (wtSharedMemory) return stopWithError("multiple memory imports");

			auto error = wasmi_sharedmemory_new(globalWasmEngine, asMemory, &wtSharedMemory);
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

bool wclap_wasmi::InstanceImpl::setup() {
	auto stopWithError = [&](const char *message) -> bool {
		group.setError(message);
		return false;
	};

	wtStore = wasmi_store_new(globalWasmEngine, nullptr, nullptr);
	if (!wtStore) return stopWithError("Failed to create store");

	wtContext = wasmi_store_context(wtStore);
	if (!wtContext) return stopWithError("Failed to get context");
	
	// Create a linker with WASI functions defined
	wtLinker = wasmi_linker_new(globalWasmEngine);
	if (!wtLinker) return stopWithError("error creating linker");

	{
		auto error = wasmi_linker_define_wasi(wtLinker);
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
		if (!wasi_config_preopen_dir(wasiConfig, group.wclapDir->c_str(), "/plugin.wclap/", WASMI_WASI_DIR_PERMS_READ, WASMI_WASI_FILE_PERMS_READ)) {
			std::cerr << "WASI: failed to link " << *group.wclapDir << std::endl;
		}
	}
	if (group.presetDir) {
		if (!wasi_config_preopen_dir(wasiConfig, group.presetDir->c_str(), "/presets/", WASMI_WASI_DIR_PERMS_READ|WASMI_WASI_DIR_PERMS_WRITE, WASMI_WASI_FILE_PERMS_READ|WASMI_WASI_FILE_PERMS_WRITE)) {
			std::cerr << "WASI: failed to link " << *group.presetDir << std::endl;
		}
	}
	if (group.cacheDir) {
		if (!wasi_config_preopen_dir(wasiConfig, group.cacheDir->c_str(), "/cache/", WASMI_WASI_DIR_PERMS_READ|WASMI_WASI_DIR_PERMS_WRITE, WASMI_WASI_FILE_PERMS_READ|WASMI_WASI_FILE_PERMS_WRITE)) {
			std::cerr << "WASI: failed to link " << *group.cacheDir << std::endl;
		}
	}
	if (group.varDir) {
		if (!wasi_config_preopen_dir(wasiConfig, group.varDir->c_str(), "/var/", WASMI_WASI_DIR_PERMS_READ|WASMI_WASI_DIR_PERMS_WRITE, WASMI_WASI_FILE_PERMS_READ|WASMI_WASI_FILE_PERMS_WRITE)) {
			std::cerr << "WASI: failed to link " << *group.varDir << std::endl;
		}
	}

	{
		auto error = wasmi_context_set_wasi(wtContext, wasiConfig);
		if (error) {
			group.setError(error);
			wasi_config_delete(wasiConfig);
			return stopWithError("Failed to configure WASI");
		}
	}
	wasiConfig = nullptr; // owned by the context now

	//---------- Shared-memory import ----------//

	if (group.wtSharedMemory) { // memory import
		wasmi_extern_t item;
		item.kind = WASMI_EXTERN_SHAREDMEMORY;
		item.of.sharedmemory = group.wtSharedMemory;
		
		auto &module = group.sharedMemoryImportModule;
		auto &name = group.sharedMemoryImportName;
		auto error = wasmi_linker_define(wtLinker, wtContext, module.c_str(), module.size(), name.c_str(), name.size(), &item);
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
		auto *error = wasmi_linker_instantiate(wtLinker, wtContext, group.wtModule, &wtInstance, &trap);
		if (error) {
			group.setError(error);
			return stopWithError("Failed to create instance (error)");
		}
		if (trap) {
			group.setError(trap, "Failed to start instance (trap)");
			return false;
		}
	}

	//---------- Find exports ----------//

	char *name;
	size_t nameSize;
	wasmi_extern_t item;
	
	if (wasmi_instance_export_get(wtContext, &wtInstance, "memory", 6, &item)) {
		if (item.kind == WASMI_EXTERN_MEMORY) {
			wtMemory = item.of.memory;
		} else if (item.kind == WASMI_EXTERN_SHAREDMEMORY) {
			// TODO: it should be the same as the import - not sure how to check this
			if (!group.wtSharedMemory) {
				return stopWithError("exported shared memory, but didn't import it");
			}
		} else {
			wasmi_extern_delete(&item);
			return stopWithError("exported memory isn't a (Shared)Memory");
		}
		wasmi_extern_delete(&item);
	} else if (!group.wtSharedMemory) {
		return stopWithError("must either export memory or import shared memory");
	}

	if (!wasmi_instance_export_get(wtContext, &wtInstance, "clap_entry", 10, &item)) {
		return stopWithError("clap_entry not exported");
	}
	if (item.kind == WASMI_EXTERN_GLOBAL) {
		wasm_val_t v;
		wasm_global_get(wtContext, &item.of.global, &v);
		if (v.kind == WASM_I32 && !group.is64()) {
			wclapEntryAs64 = v.of.i32; // We store it as 64 bits, even though we know it's a 32-bit one
		} else if (v.kind == WASM_I64 && group.is64()) {
			wclapEntryAs64 = v.of.i64;
		} else {
			return stopWithError("clap_entry is not a (correctly-sized) pointer");
		}
	} else {
		wasmi_extern_delete(&item);
		return stopWithError("clap_entry isn't a Global");
	}
	wasmi_extern_delete(&item);

	if (!wasmi_instance_export_get(wtContext, &wtInstance, "malloc", 6, &item)) {
		return stopWithError("malloc not exported");
	}
	if (item.kind == WASMI_EXTERN_FUNC) {
		wasm_functype_t *type = wasmi_func_type(wtContext, &item.of.func);
		const wasm_valtype_vec_t *params = wasm_functype_params(type);
		const wasm_valtype_vec_t *results = wasm_functype_results(type);
		if (params->size != 1 || results->size != 1) {
			wasmi_extern_delete(&item);
			return stopWithError("malloc() function signature mismatch");
		}
		if (wasm_valtype_kind(params->data[0]) != wasm_valtype_kind(results->data[0])) {
			wasmi_extern_delete(&item);
			return stopWithError("malloc() function signature mismatch");
		}
		if (group.is64()) {
			if (wasm_valtype_kind(params->data[0]) != WASMI_I64) {
				wasmi_extern_delete(&item);
				return stopWithError("malloc() function signature mismatch");
			}
		} else {
			if (wasm_valtype_kind(params->data[0]) != WASMI_I32) {
				wasmi_extern_delete(&item);
				return stopWithError("malloc() function signature mismatch");
			}
		}
		wtMallocFunc = item.of.func;
		wasm_functype_delete(type);
	} else {
		wasmi_extern_delete(&item);
		return stopWithError("malloc isn't a Function");
	}
	wasmi_extern_delete(&item);

	// Look for the first function table
	size_t exportIndex = 0;
	while (wasmi_instance_export_nth(wtContext, &wtInstance, exportIndex, &name, &nameSize, &item)) {
		if (item.kind == WASMI_EXTERN_TABLE) {
			wasm_tabletype_t *type = wasmi_table_type(wtContext, &item.of.table);
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
		wasmi_extern_delete(&item);
		++exportIndex;
	}
	return true;
}

bool wclap_wasmi::InstanceImpl::wasiInit() {
	auto stopWithError = [&](const char *message) -> bool {
		group.setError(message);
		return false;
	};

	wasmi_extern_t item;

	// Call the WASI entry-point `_initialize()` if it exists - WCLAPs don't *have* to use WASI, so it's fine not to
	if (wasmi_instance_export_get(wtContext, &wtInstance, "_initialize", 11, &item)) {
		if (item.kind != WASMI_EXTERN_FUNC) {
			wasmi_extern_delete(&item);
			return stopWithError("_initialize isn't a function");
		}
		wasm_functype_t *type = wasmi_func_type(wtContext, &item.of.func);
		const wasm_valtype_vec_t *params = wasm_functype_params(type);
		const wasm_valtype_vec_t *results = wasm_functype_results(type);
		if (params->size != 0 || results->size != 0) {
			wasmi_extern_delete(&item);
			return stopWithError("_initialize() function signature mismatch");
		}
		wasm_functype_delete(type);

		setWasmDeadline();
		wasm_trap_t *trap = nullptr;
		auto error = wasmi_func_call(wtContext, &item.of.func, nullptr, 0, nullptr, 0, &trap);
		if (error) {
			group.setError(error);
			wasmi_extern_delete(&item);
			return stopWithError("error calling _initialize()");
		} else if (trap) {
			group.setError(trap, "_initialize() threw (trapped)");
			wasmi_extern_delete(&item);
			return false;
		}
		wasmi_extern_delete(&item);
	}
	return true;
}

uint64_t wclap_wasmi::InstanceImpl::wtMalloc(size_t bytes) {
	std::lock_guard<std::recursive_mutex> lock(callMutex);

	uint64_t wasmP;
	
	wasmi_val_t args[1];
	wasmi_val_t results[1];
	if (group.is64()) {
		args[0].kind = WASMI_I64;
		args[0].of.i64 = bytes;
	} else {
		args[0].kind = WASMI_I32;
		args[0].of.i32 = (uint32_t)bytes;
	}
	
	setWasmDeadline();
	{
		wasm_trap_t *trap = nullptr;
		auto error = wasmi_func_call(wtContext, &wtMallocFunc, args, 1, results, 1, &trap);
		if (error) {
			group.setError(error);
			group.setError("calling malloc() failed");
			return 0;
		}
		if (trap) {
			group.setError(trap, "malloc() threw (trapped)");
			return 0;
		}
	}
	if (group.is64()) {
		if (results[0].kind != WASMI_I64) return 0;
		return results[0].of.i64;
	} else {
		if (results[0].kind != WASMI_I32) return 0;
		return results[0].of.i32;
	}
}

void wclap_wasmi::InstanceImpl::setWasmDeadline() {
	if (timeLimitEpochs) {
		wasmi_context_set_epoch_deadline(wtContext, timeLimitEpochs);
	}
}

