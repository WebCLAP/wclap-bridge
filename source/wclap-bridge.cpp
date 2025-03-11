#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "wclap-translation-scope.h"
#include "wasi.h"

#include <fstream>
#include <vector>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

static wasm_engine_t *global_wasm_engine = nullptr;
std::string wclap_error_message_string;
static const char *wclap_error_message = nullptr;

static std::string ensureTrailingSlash(const char *dirC) {
	std::string dir = dirC;
	if (dir.size() && dir.back() != '/') dir += "/";
	return dir;
}

struct Wclap;
struct WclapThread;

struct WclapThreadContext {
	WclapThreadContext() {}
	WclapThreadContext(const WclapThreadContext &other) = delete;
	
	~WclapThreadContext() {
		for (auto &wclap : wclaps) {
			wclap->removeThread();
		}
	}
	
	void addWclap(Wclap *wclap) {
		std::lock_guard<std::mutex> lock{mutex};
		wclaps.insert(wclap);
	}
	
	void removeWclap(Wclap *wclap) {
		std::lock_guard<std::mutex> lock{mutex};

		if (auto iter = mySet.find(wclap); iter != mySet.end()) {
			mySet.erase(iter);
		}
	}
private:
	std::mutex mutex; // some methods are called out-of-thread
	// A list of every Wclap which currently has a WclapThread for a given thread
	std::unordered_set<Wclap *> wclaps;
};
thread_local static WclapThreadContext wclapThreadContext;

struct Wclap {
	wasmtime_module_t *module = nullptr;
//	wasmtime_memory_t *sharedMemory = nullptr;

	Wclap() {}
	~Wclap() {
		if (module) wasmtime_module_delete(module);
	}

	const char * setupWasmBytes(const uint8_t *bytes, size_t size) {
		error = wasmtime_module_new(global_wasm_engine, bytes, size, &module);
		if (error) return "Failed to compile module";
		
		return "Not implemented - is it single-threaded?"
		return nullptr;
	}
	
	std::unique_ptr<WclapThread> singleThread;
	struct ScopedThread {
		WclapThread &thread;
		
		ScopedThread(WclapThread &thread, std::shared_mutex *mutex=nullptr) : thread(thread), mutex(mutex) {}
		ScopedThread(const ScopedThread &other) = delete;
		ScopedThread(ScopedThread &&other) : thread(other.thread), mutex(other.mutex) {
			other.mutex = nullptr;
		}
		ScopedThread & operator=(const ScopedThread &other) = delete;
		ScopedThread & operator=(ScopedThread &&other) = delete;
		~ScopedThread() {
			if (mutex) mutex->unlock_shared();
		}
	private:
		std::shared_mutex *mutex;
	};

	// find or create the instance/etc. associated with the current native thread
	ScopedThread getThread() {
		if (singleThread) {
			// If the WCLAP didn't import shared memory, it's single-threaded
			mutex.lock_shared();
			return {*singleThread, &mutex};
		}
	
		{
			auto lock = readLock();
			auto iter = threadMap.find(std::this_thread::id);
			if (iter != threadMap.end()) return {*iter.second};
		}
		
		// Put ourselves in this thread's list, so we get notified when the thread closes
		wclapThreadContext.addWclap(this);

		auto lock = writeLock();
		auto *wclapThread = new WclapThread(*this, wclapThreadContext);
		threadMap[std::this_thread::id] = std::unique_ptr<WclapThread>{wclapThread};
		return {*wclapThread};
	}
	
	// The current thread is shutting down - remove it from our map
	void removeThread() {
		auto lock = writeLock();

		auto iter = threadMap.find(std::this_thread::id);
		if (iter == threadMap.end()) {
			abort(); // This shouldn't be called unless we had a WclapThread for this thread
		}
		threadMap.erase(iter);
	}
	
private:
	mutable std::shared_mutex mutex;
	// Scoped lock suitable for reading the thread map
	std::shared_lock<std::shared_mutex> readLock() const {
		return {mutex};
	}
	// Scoped lock suitable for changing the thread map
	std::unique_lock<std::shared_mutex> writeLock() {
		return {mutex};
	}
	std::unordered_map<std::thread::id, std::unique_ptr<WclapThread>> threadMap;
};

struct WclapThread {
	using WasmP = typename std::conditional<use64, uint64_t, uint32_t>::type;
	
	Wclap &wclap;
	WclapThreadContext *threadContext;

	// We should delete these (in reverse order) if they're defined
	wasi_config_t *wasiConfig = nullptr;
	wasmtime_store_t *store = nullptr;
	wasmtime_linker_t *linker = nullptr;
	wasmtime_error_t *error = nullptr;

	// Maybe defined, but not our job to delete it
	wasmtime_context_t *context = nullptr;
	wasm_trap_t *trap = nullptr;
	wasmtime_memory_t memory;
	wasmtime_table_t functionTable;
	wasmtime_func_t malloc;

	WasmP clapEntryP = 0; // WASM pointer to clap_entry
	wasmtime_instance_t instance;
	
	WclapThread(Wclap &wclap, WclapThreadContext *threadContext) : wclap(wclap), threadContext(threadContext) {}

	// destructor is the only method allowed to be called from outside the assigned thread
	~WclapThread() {
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
		if (module) wasmtime_module_delete(module);
		if (linker) wasmtime_linker_delete(linker);
		if (store) wasmtime_store_delete(store);
	}
	
	const char * startInstance(const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir, bool mustLinkDirs) {
		wasiConfig = wasi_config_new();
		if (!wasiConfig) return "Failed to create WASI config";

		wasi_config_inherit_stdout(wasiConfig);
		wasi_config_inherit_stderr(wasiConfig);
		// Link various directories - failure is allowed if `mustLinkDirs` is false
		if (wclapDir) {
			if (!wasi_config_preopen_dir(wasiConfig, wclapDir, "/plugin/", WASMTIME_WASI_DIR_PERMS_READ, WASMTIME_WASI_FILE_PERMS_READ)) {
				if (mustLinkDirs) return "Failed to open /plugin/ in WASI config";
			}
		}
		if (presetDir) {
			if (!wasi_config_preopen_dir(wasiConfig, presetDir, "/presets/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
				if (mustLinkDirs) return "Failed to open /presets/ in WASI config";
			}
		}
		if (cacheDir) {
			if (!wasi_config_preopen_dir(wasiConfig, cacheDir, "/cache/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
				if (mustLinkDirs) return "Failed to open /cache/ in WASI config";
			}
		}
		if (varDir) {
			if (!wasi_config_preopen_dir(wasiConfig, varDir, "/var/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
				if (mustLinkDirs) return "Failed to open /var/ in WASI config";
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
		error = wasmtime_linker_instantiate(linker, context, module, &instance, &trap);
		if (error) return "failed to create instance";
		if (trap) return "failed to start instance";

		//---------- Find exports ----------//

		char *name;
		size_t nameSize;
		wasmtime_extern_t item;
		
		if (!wasmtime_instance_export_get(context, &instance, "memory", 6, &item)) {
			return "memory not exported";
		}
		if (item.kind == WASMTIME_EXTERN_MEMORY || item.kind == WASMTIME_EXTERN_SHAREDMEMORY) {
			memory = item.of.memory; // Shared memory is (in Wasmtime) a type of memory
		} else {
			wasmtime_extern_delete(&item);
			return "memory isn't a (Shared)Memory";
		}
		wasmtime_extern_delete(&item);

		if (!wasmtime_instance_export_get(context, &instance, "clap_entry", 10, &item)) {
			return "clap_entry not exported";
		}
		if (item.kind == WASMTIME_EXTERN_GLOBAL) {
			wasmtime_val_t v;
			wasmtime_global_get(context, &item.of.global, &v);
			if (v.kind != WASM_I32) return "clap_entry is not a 32-bit pointer";
			clapEntryP = v.of.i32;
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
			if (params->size == 1 && wasm_valtype_kind(params->data[0]) == WASM_I32 && results->size == 1 && wasm_valtype_kind(results->data[0]) == WASM_I32) {
				malloc = item.of.func;
			} else {
				return "malloc() function signature mismatch";
			}
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
		return "not implemented yet";
		return nullptr;
	}
	
	static char typeCode(wasm_valkind_t k) {
		if (k < 4) {
			return "iIfF"[k];
		} else if (k == WASM_EXTERNREF) {
			return 'X';
		} else if (k == WASM_FUNCREF) {
			return '$';
		}
		return '?';
	}
	static char typeCode(const wasm_valtype_t *t) {
		return typeCode(wasm_valtype_kind(t));
	}
	
	bool init() {
		wasm_ref_t *initFuncRef = nullptr;
		wasm_func_t *initFunc = nullptr;
		{
			auto *wasmEntry = (Wasm32PluginEntry *)(wasmtime_memory_data(context, &memory) + clapEntryP);
			clapVersion = wasmEntry->clap_version;
			wasmtime_val_t val;
			if (!wasmtime_table_get(context, &functionTable, wasmEntry->initP, &val)) return false;
			
		}
		LOG_EXPR(initFunc);
		LOG_EXPR(wasm_func_param_arity(initFunc));
		LOG_EXPR(wasm_func_result_arity(initFunc));
		
		uint32_t pluginPath = copyStringConstantToWasm("/plugin/");
		if (!pluginPath) {
			return false;
		}
		
		wasm_val_t args[1];
		wasm_val_vec_t argsV{1, args};
		wasm_val_t results[1];
		wasm_val_vec_t resultsV{1, results};
		
		{
			auto *wasmEntry = (Wasm32PluginEntry *)(wasm_memory_data(memory) + clapEntryP);
			args[0].kind = WASM_I32;
			args[0].of.i32 = pluginPath;
			auto *trap = wasm_func_call(initFunc, &argsV, &resultsV);
			if (trap) return false;
		}

		return results[0].of.i32;
	}
	
	clap_plugin_factory nativePluginFactory;
	std::unique_ptr<WclapTranslationScope> pluginFactoryScope;
	
	const void * getFactory(const char *factory_id) {
		if (!std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) {
			if (!pluginFactoryScope) {
				pluginFactoryScope = std::unique_ptr<WclapTranslationScope>{
					
				};
			}
			return &nativePluginFactory;
		}
		LOG_EXPR(factory_id);
		return nullptr;
	}
	
	clap_version clapVersion;

private:

	static bool nameEquals(const wasm_name_t *name, const char *cName) {
		if (name->size != std::strlen(cName)) return false;
		for (size_t i = 0; i < name->size; ++i) {
			if (name->data[i] != cName[i]) return false;
		}
		return true;
	}
	
	// clap_entry as it will appear in a 32-bit WCLAP
	struct Wasm32PluginEntry {
		clap_version_t clap_version;
		uint32_t initP;
		uint32_t deinitP;
		uint32_t getFactoryP;
	};
	
	uint32_t copyStringConstantToWasm(const char *str) {
		size_t bytes = std::strlen(str) + 1;
		
		wasm_val_t args[1];
		args[0].kind = WASM_I32;
		args[0].of.i32 = bytes;
		wasm_val_vec_t argsV{1, args};
		wasm_val_t results[1];
		wasm_val_vec_t resultsV{1, results};
		
		auto *trap = wasm_func_call(malloc, &argsV, &resultsV);
		if (trap) return 0;
		uint32_t wasmP = results[0].of.i32;
		
		auto *wasmBytes = (char *)(wasm_memory_data(memory) + wasmP);
		for (size_t i = 0; i < bytes; ++i) {
			wasmBytes[i] = str[i];
		}
		return wasmP;
	}
};

/*---------- WCLAP bridge C API ----------*/

#import "./wclap-bridge.h"

bool wclap_global_init() {
	wasm_config_t *config = wasm_config_new();
	if (!config) {
		wclap_error_message = "couldn't create config";
		return false;
	}

	// TODO: enable epoch_interruption to prevent locks
	global_wasm_engine = wasm_engine_new_with_config(config);
	if (!global_wasm_engine) {
		wclap_error_message = "couldn't create engine";
		return false;
	}
	return true;
}
void wclap_global_deinit() {
	if (global_wasm_engine) {
		wasm_engine_delete(global_wasm_engine);
		global_wasm_engine = nullptr;
	}
}

const char * wclap_error() {
	auto *message = wclap_error_message;
	wclap_error_message = nullptr;
	return message;
}

void * wclap_open_with_dirs(const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir) {
	if (!global_wasm_engine) {
		wclap_error_message = "No WASM engine - did you call wclap_global_init()?";
		return nullptr;
	}

	std::ifstream wasmFile{ensureTrailingSlash(wclapDir) + "module.wasm", std::ios::binary};
	if (!wasmFile) {
		wclap_error_message = "Couldn't open plugin/module.wasm file";
		return nullptr;
	}
	std::vector<char> wasmBytes{std::istreambuf_iterator<char>{wasmFile}, {}};
	if (!wasmBytes.size()) {
		wclap_error_message = "Couldn't read WASM file";
		return nullptr;
	}

	auto *wclap = new Wclap();

	wclap_error_message = wclap->setupWasiDirs(wclapDir, presetDir, cacheDir, varDir, true);
	if (wclap_error_message) {
		delete wclap;
		return nullptr;
	}

	wclap_error_message = wclap->setupWasmBytes((uint8_t *)wasmBytes.data(), wasmBytes.size());
	if (wclap_error_message) {
		delete wclap;
		return nullptr;
	}

	wclap_error_message = wclap->findExports();
	if (wclap_error_message) {
		delete wclap;
		return nullptr;
	}

	return wclap;
}
void * wclap_open(const char *wclapDir) {
	return wclap_open_with_dirs(wclapDir, nullptr, nullptr, nullptr);
}
bool wclap_close(void *wclap) {
	if (!wclap) {
		wclap_error_message = "null pointer";
		return false;
	}
	delete (Wclap *)wclap;
	return true;
}
const clap_version_t * wclap_version(void *wclap) {
	if (!wclap) {
		wclap_error_message = "null pointer";
		return nullptr;
	}
	return &((Wclap *)wclap)->clapVersion;
}
const void * wclap_get_factory(void *wclap, const char *factory_id) {
	if (!wclap) {
		wclap_error_message = "null pointer";
		return nullptr;
	}
	return ((Wclap *)wclap)->getFactory(factory_id);
}

