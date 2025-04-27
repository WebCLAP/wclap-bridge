#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "./wclap-bridge-impl.h"
#include "./wclap-translation-scope.h"

std::ostream & operator<<(std::ostream &s, const wasm_byte_vec_t &bytes) {
	for (size_t i = 0; i < bytes.size; ++i) {
		s << bytes.data[i];
	}
	return s;
}

wasm_engine_t *global_wasm_engine;
const char *wclap_error_message;

std::string wclap_error_message_string;

//---------- Thread Context ----------//

void WclapThreadContext::addWclap(Wclap *wclap) {
	std::lock_guard<std::mutex> lock{mutex};
	wclaps.insert(wclap);
}
void WclapThreadContext::removeWclap(Wclap *wclap) {
	// This might be called off-thread, if the Wclap is shutting down (and taking the WclapThreads with it)
	if (isDestroying.load()) return; // this thread is stopping (and taking the Wclap pointer with it), so it's fine
	std::lock_guard<std::mutex> lock{mutex};

	if (auto iter = wclaps.find(wclap); iter != wclaps.end()) {
		wclaps.erase(iter);
	}
}
WclapThreadContext::~WclapThreadContext() {
	isDestroying.store(true);
	std::lock_guard<std::mutex> lock{mutex};
	for (auto &wclap : wclaps) {
		wclap->removeThread();
	}
}

thread_local static WclapThreadContext wclapThreadContext;

//---------- Common Wclap instance ----------//

Wclap::Wclap(const std::string &wclapDir, const std::string &presetDir, const std::string &cacheDir, const std::string &varDir, bool mustLinkDirs) : wclapDir(wclapDir), presetDir(presetDir), cacheDir(cacheDir), varDir(varDir), mustLinkDirs(mustLinkDirs) {}

Wclap::~Wclap() {
	if (initSuccess) {
		if (translatedEntry.deinit) translatedEntry.deinit();
	}

	{ // Lock while we clear all the threads
		auto lock = writeLock();
		threadMap.clear();
		if (singleThread) singleThread = nullptr;
	}
	
	// As a sanity-check, the translation scopes will abort() if they aren't told that the WCLAP is closing
	if (entryTranslationScope32) entryTranslationScope32->wasmReadyToDestroy();
	if (entryTranslationScope64) entryTranslationScope64->wasmReadyToDestroy();
	for (auto &t : poolTranslationScope32) t->wasmReadyToDestroy();
	for (auto &t : poolTranslationScope64) t->wasmReadyToDestroy();

	if (sharedMemory) {
		wasmtime_sharedmemory_delete(sharedMemory);
	}
	
	if (error) wasmtime_error_delete(error);
	if (module) wasmtime_module_delete(module);
}

uint8_t * Wclap::wasmMemory(uint32_t wasmP) {
	if (sharedMemory) {
		return wasmtime_sharedmemory_data(sharedMemory) + wasmP;
	} else {
		return wasmtime_memory_data(singleThread->context, &singleThread->memory) + wasmP;
	}
}
uint8_t * Wclap::wasmMemory(uint64_t wasmP) {
	if (sharedMemory) {
		wasmP = std::min<uint64_t>(wasmP, wasmtime_sharedmemory_data_size(sharedMemory));
		return wasmtime_sharedmemory_data(sharedMemory) + wasmP;
	} else {
		wasmP = std::min<uint64_t>(wasmP, wasmtime_memory_data_size(singleThread->context, &singleThread->memory));
		return wasmtime_memory_data(singleThread->context, &singleThread->memory) + wasmP;
	}
}
	
const char * Wclap::initWasmBytes(const uint8_t *bytes, size_t size) {
	error = wasmtime_module_new(global_wasm_engine, bytes, size, &module);
	if (error) return "Failed to compile module";
	
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
			if (!globalType) return "clap_entry is not a global (value) export";
			auto *valType = wasm_globaltype_content(globalType);
			auto kind = wasm_valtype_kind(valType);
			
			if (kind == WASMTIME_I64) {
				wasm64 = true;
				break;
			} else if (kind != WASMTIME_I32) {
				return "clap_entry must be 32-bit or 64-bit memory address";
			}
		}
		wasm_exporttype_vec_delete(&exportTypes);
	}
	if (!foundClapEntry) return "clap_entry not found";

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
				return "imports non-shared memory";
			}
			bool mem64 = wasmtime_memorytype_is64(asMemory);
			if (mem64 != wasm64) {
				return mem64 ? "64-bit memory but 32-bit clap_entry" : "32-bit memory but 64-bit clap_entry";
			}
			if (sharedMemory) return "multiple memory imports";

			error = wasmtime_sharedmemory_new (global_wasm_engine, asMemory, &sharedMemory);
			if (error || !sharedMemory) return "failed to create shared memory";
		}
		wasm_importtype_vec_delete(&importTypes);
	}
	
	LOG_EXPR(wasm64);
	if (!sharedMemory) {
		auto *wclapThread = new WclapThread(*this, &wclapThreadContext);
		auto lock = writeLock();
		auto *errorMessage = wclapThread->startInstance();
		if (errorMessage) return errorMessage;
		singleThread = std::unique_ptr<WclapThread>(wclapThread);
	}
	LOG_EXPR(sharedMemory);
	LOG_EXPR(singleThread);
	
	{
		auto scoped = getThread();
		auto wasmP = scoped.thread.clapEntryP64;
		if (wasm64) {
			entryTranslationScope64 = std::unique_ptr<WclapTranslationScope<true>>{
				new WclapTranslationScope<true>(*this, scoped.thread)
			};
			entryTranslationScope64->assignWasmToNative(wasmP, translatedEntry);
		} else {
			entryTranslationScope32 = std::unique_ptr<WclapTranslationScope<false>>{
				new WclapTranslationScope<false>(*this, scoped.thread)
			};
			entryTranslationScope32->assignWasmToNative((uint32_t)wasmP, translatedEntry);
		}
		
		// TODO: check version compatibility, use minimum of plugin/bridge version

		if (!translatedEntry.init) return "clap_entry.init = 0";
		initSuccess = translatedEntry.init("/plugin/");
		if (!initSuccess) return "init() failed";
	}

	return nullptr;
}

Wclap::ScopedThread Wclap::getThread() {
	if (singleThread) {
		// If the WCLAP didn't import shared memory, it's single-threaded
		mutex.lock_shared();
		return {*singleThread, &mutex};
	}

	{
		auto lock = readLock();
		auto iter = threadMap.find(std::this_thread::get_id());
		if (iter != threadMap.end()) return {*iter->second};
	}
	
	// Put ourselves in this thread's list, so we get notified when the thread closes
	// The thread will remove us from this list when it gets destroyed
	wclapThreadContext.addWclap(this);

	auto *wclapThread = new WclapThread(*this, &wclapThreadContext);
	auto lock = writeLock();
	auto *errorMessage = wclapThread->startInstance();
	if (errorMessage) {
		LOG_EXPR(errorMessage);
		abort(); // TODO: something better - this could be an error within the WCLAP, so *we* shouldn't crash
	}
	threadMap[std::this_thread::get_id()] = std::unique_ptr<WclapThread>{wclapThread};
	return {*wclapThread};
}

void Wclap::removeThread() {
	auto lock = writeLock();

	auto iter = threadMap.find(std::this_thread::get_id());
	if (iter == threadMap.end()) {
		LOG_EXPR(iter == threadMap.end());
		abort(); // This shouldn't be called unless we had a WclapThread for this thread
	}
	threadMap.erase(iter);
}

const void * Wclap::getFactory(const char *factory_id) {
	if (!translatedEntry.get_factory) return "clap_entry.get_factory = 0";
	if (!std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) {
		LOG_EXPR(hasPluginFactory);
		if (!hasPluginFactory) {
			// Since it's an unknown void (and we have to know what to cast it to), it's translated to this wrapper object
			auto *unknown = (WasmPointerUnknown *)translatedEntry.get_factory(factory_id);
			
			LOG_EXPR(factory_id);
			LOG_EXPR(unknown);
			LOG_EXPR(unknown->wasmP);
			if (wasm64) {
				entryTranslationScope64->assignWasmToNative(unknown->wasmP, nativePluginFactory);
				entryTranslationScope64->commitNative(); // assigned object needs to be persistent
			} else {
				entryTranslationScope32->assignWasmToNative((uint32_t)unknown->wasmP, nativePluginFactory);
				entryTranslationScope32->commitNative(); // assigned object needs to be persistent
			}
			hasPluginFactory = true;
		}
		return &nativePluginFactory;
	}
	LOG_EXPR(factory_id);
	return nullptr;
}

void Wclap::returnToPool(std::unique_ptr<WclapTranslationScope<false>> &ptr) {
	ptr->unbindAndReset();
	poolTranslationScope32.emplace_back(std::move(ptr));
}
void Wclap::returnToPool(std::unique_ptr<WclapTranslationScope<true>> &ptr) {
	ptr->unbindAndReset();
	poolTranslationScope64.emplace_back(std::move(ptr));
}

//---------- Wclap Thread ----------//

const char * WclapThread::startInstance() {
	if (wasiConfig) return "startInstance() called twice";
	
	wasiConfig = wasi_config_new();
	if (!wasiConfig) return "Failed to create WASI config";

	wasi_config_inherit_stdout(wasiConfig);
	wasi_config_inherit_stderr(wasiConfig);
	// Link various directories - failure is allowed if `mustLinkDirs` is false
	if (wclap.wclapDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.wclapDir.c_str(), "/plugin/", WASMTIME_WASI_DIR_PERMS_READ, WASMTIME_WASI_FILE_PERMS_READ)) {
			if (wclap.mustLinkDirs) return "Failed to open /plugin/ in WASI config";
		}
	}
	if (wclap.presetDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.presetDir.c_str(), "/presets/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			if (wclap.mustLinkDirs) return "Failed to open /presets/ in WASI config";
		}
	}
	if (wclap.cacheDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.cacheDir.c_str(), "/cache/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			if (wclap.mustLinkDirs) return "Failed to open /cache/ in WASI config";
		}
	}
	if (wclap.varDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.varDir.c_str(), "/var/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			if (wclap.mustLinkDirs) return "Failed to open /var/ in WASI config";
		}
	}
	
	//---------- Start the instance ----------//

	store = wasmtime_store_new(global_wasm_engine, nullptr, nullptr);
	if (!store)  return "Failed to create store";
	context = wasmtime_store_context(store);
	if (!context) return "Failed to create context";
	
	// Create a linker with WASI functions defined
	linker = wasmtime_linker_new(global_wasm_engine);
	if (!linker) return "error creating linker";
	error = wasmtime_linker_define_wasi(linker);
	if (error) return "error linking WASI";

	error = wasmtime_context_set_wasi(context, wasiConfig);
	if (error) return "Failed to configure WASI";
	wasiConfig = nullptr;

	// This includes calling the WASI _start() or _initialize() methods
	error = wasmtime_linker_instantiate(linker, context, wclap.module, &instance, &trap);
	if (error) return "failed to create instance";
	if (trap) return "failed to start instance";

	//---------- Find exports ----------//

	char *name;
	size_t nameSize;
	wasmtime_extern_t item;
	
	if (wasmtime_instance_export_get(context, &instance, "memory", 6, &item)) {
		if (item.kind == WASMTIME_EXTERN_MEMORY || item.kind == WASMTIME_EXTERN_SHAREDMEMORY) {
			memory = item.of.memory; // Shared memory is (in Wasmtime) a type of memory
		} else {
			wasmtime_extern_delete(&item);
			return "exported memory isn't a (Shared)Memory";
		}
		wasmtime_extern_delete(&item);
	} else if (!wclap.sharedMemory) {
		return "must either export memory or import shared memory";
	}

	if (!wasmtime_instance_export_get(context, &instance, "clap_entry", 10, &item)) {
		return "clap_entry not exported";
	}
	if (item.kind == WASMTIME_EXTERN_GLOBAL) {
		wasmtime_val_t v;
		wasmtime_global_get(context, &item.of.global, &v);
		if (v.kind == WASM_I32 && !wclap.wasm64) {
			clapEntryP64 = v.of.i32; // We store it as 64 bits, even though we know it's a 32-bit one
		} else if (v.kind == WASM_I64 && wclap.wasm64) {
			clapEntryP64 = v.of.i64;
		} else {
			return "clap_entry is not a (correctly-sized) pointer";
		}
	} else {
		wasmtime_extern_delete(&item);
		return "clap_entry isn't a Global";
	}
	wasmtime_extern_delete(&item);

	if (!wasmtime_instance_export_get(context, &instance, "malloc", 6, &item)) {
		return "malloc not exported";
	}
	if (item.kind == WASMTIME_EXTERN_FUNC) {
		wasm_functype_t *type = wasmtime_func_type(context, &item.of.func);
		const wasm_valtype_vec_t *params = wasm_functype_params(type);
		const wasm_valtype_vec_t *results = wasm_functype_results(type);
		if (params->size != 1 || results->size != 1) return "malloc() function signature mismatch";
		if (wasm_valtype_kind(params->data[0]) != wasm_valtype_kind(results->data[0])) return "malloc() function signature mismatch";
		if (wclap.wasm64) {
			if (wasm_valtype_kind(params->data[0]) != WASMTIME_I64) return "malloc() function signature mismatch";
		} else {
			if (wasm_valtype_kind(params->data[0]) != WASMTIME_I32) return "malloc() function signature mismatch";
		}
		mallocFunc = item.of.func;
		wasm_functype_delete(type);
	} else {
		wasmtime_extern_delete(&item);
		return "malloc isn't a Function";
	}
	wasmtime_extern_delete(&item);

	// Look for the first function table
	size_t exportIndex = 0;
	while (wasmtime_instance_export_nth(context, &instance, exportIndex, &name, &nameSize, &item)) {
		if (item.kind == WASMTIME_EXTERN_TABLE) {
			wasm_tabletype_t *type = wasmtime_table_type(context, &item.of.table);
			const wasm_limits_t limits = *wasm_tabletype_limits(type);
			auto elementKind = wasm_valtype_kind(wasm_tabletype_element(type));
			wasm_tabletype_delete(type);

			if (elementKind == WASM_FUNCREF) {
				if (limits.max < 65536 || limits.max - 65536 < limits.min) {
					return "exported function table can't grow enough for CLAP host functions";
				}
				functionTable = item.of.table;
				break;
			}
		}
		wasmtime_extern_delete(&item);
		++exportIndex;
	}

	// Call the WASI entry-point `_initialize()` if it exists
	if (wasmtime_instance_export_get(context, &instance, "_initialize", 11, &item)) {
		if (item.kind == WASMTIME_EXTERN_FUNC) {
			wasm_functype_t *type = wasmtime_func_type(context, &item.of.func);
			const wasm_valtype_vec_t *params = wasm_functype_params(type);
			const wasm_valtype_vec_t *results = wasm_functype_results(type);
			if (params->size != 0 || results->size != 0) return "_initialize() function signature mismatch";
			wasm_functype_delete(type);

			wasmtime_func_call(context, &item.of.func, nullptr, 0, nullptr, 0, &trap);
			if (trap) {
				wasmtime_extern_delete(&item);
				return "_initialize() threw an error";
			}
		} else {
			wasmtime_extern_delete(&item);
			return "_initialize isn't a function";
		}
		wasmtime_extern_delete(&item);
	}

	return nullptr;
}

WclapThread::~WclapThread() {
	if (threadContext) threadContext->removeWclap(&wclap);
	
	if (trap) {
		wclap_error_message_string = wclap_error_message;
		wclap_error_message_string += ": ";
		wasm_message_t message;
		wasm_trap_message(trap, &message);
		wclap_error_message_string += message.data; // should always be null-terminated C string
		wclap_error_message = wclap_error_message_string.c_str();
	}

	if (error) {
		wclap_error_message_string = wclap_error_message;
		wclap_error_message_string += ": ";
		wasm_name_t message;
		wasmtime_error_message(error, &message);
		wclap_error_message_string.append(message.data, message.size);
		wclap_error_message = wclap_error_message_string.c_str();

		wasmtime_error_delete(error);
	}
	if (linker) wasmtime_linker_delete(linker);
	if (store) wasmtime_store_delete(store);
}
