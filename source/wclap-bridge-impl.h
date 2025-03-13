#include "wclap-translation-scope.h"
#include "wasi.h"

#include <fstream>
#include <vector>
#include <thread>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>


const char *wclap_error_message = nullptr;

struct Wclap;
struct WclapThread;

struct WclapThreadContext {
	WclapThreadContext() {}
	WclapThreadContext(const WclapThreadContext &other) = delete;
	
	~WclapThreadContext();
	
	void addWclap(Wclap *wclap);
	void removeWclap(Wclap *wclap);
private:
	std::mutex mutex; // some methods are called out-of-thread
	// A list of every Wclap which currently has a WclapThread for a given thread
	std::unordered_set<Wclap *> wclaps;
};

struct Wclap {
	wasmtime_module_t *module = nullptr;
	wasmtime_error_t *error = nullptr;
//	wasmtime_memory_t *sharedMemory = nullptr;

	Wclap() {}
	~Wclap() {
		if (error) wasmtime_error_delete(error);
		if (module) wasmtime_module_delete(module);
	}

	const char * setupWasmBytes(const uint8_t *bytes, size_t size);
	
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
	ScopedThread getThread();
	
	// The current native thread is shutting down - remove it from our map
	void removeThread();
	
private:
	mutable std::shared_mutex mutex;
	// Scoped lock suitable for reading the thread map
	std::shared_lock<std::shared_mutex> readLock() const {
		return std::shared_lock<std::shared_mutex>{mutex};
	}
	// Scoped lock suitable for changing the thread map
	std::unique_lock<std::shared_mutex> writeLock() {
		return std::unique_lock<std::shared_mutex>{mutex};
	}
	std::unordered_map<std::thread::id, std::unique_ptr<WclapThread>> threadMap;
};

struct WclapThread {
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

	uint64_t clapEntryP64 = 0; // WASM pointer to clap_entry
	wasmtime_instance_t instance;
	
	WclapThread(Wclap &wclap, WclapThreadContext *threadContext) : wclap(wclap), threadContext(threadContext) {}

	// destructor is the only method allowed to be called from outside the assigned thread
	~WclapThread();
	
	const char * startInstance(const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir, bool mustLinkDirs);
	
	static char typeCode(wasm_valkind_t k) {
		if (k < 4) {
			return "ILFD"[k];
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
//		wasm_ref_t *initFuncRef = nullptr;
		wasmtime_func_t initFunc;
		if (wclap.wasm64) {
			abort();
		} else {
			auto *wasmEntry = (Wasm32PluginEntry *)(wasmtime_memory_data(context, &memory) + clapEntryP64);
			clapVersion = wasmEntry->clap_version;
			wasmtime_val_t val;
			if (!wasmtime_table_get(context, &functionTable, wasmEntry->initP, &val)) return false;
			abort();
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
		
		if (wclap.wasm64) {
			assert(false); // WCLAP-64 not implemented yet
			abort();
		} else {
			auto *wasmEntry = (Wasm32PluginEntry *)(wasm_memory_data(memory) + clapEntryP64);
			args[0].kind = WASM_I32;
			args[0].of.i32 = pluginPath;
			auto *trap = wasm_func_call(initFunc, &argsV, &resultsV);
			if (trap) return false;
		}

		return results[0].of.i32;
	}
	
	clap_plugin_factory nativePluginFactory;
	std::unique_ptr<WclapTranslationScope<true>> pluginFactoryScope64;
	std::unique_ptr<WclapTranslationScope<false>> pluginFactoryScope32;
	
	const void * getFactory(const char *factory_id) {
		if (!std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) {
			if (wclap.wasm64) {
				assert(false); // WCLAP-64 not implemented yet
				abort();
			} else {
				if (!pluginFactoryScope32) {
					pluginFactoryScope32 = std::unique_ptr<WclapTranslationScope<false>>{
						
					};
					assert(false); // native factory not created
					abort();
				}
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
