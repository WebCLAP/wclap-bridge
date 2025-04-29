#include "wasmtime.h"
#include "clap/all.h"

#include <fstream>
#include <vector>
#include <thread>
#include <atomic>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

extern wasm_engine_t *global_wasm_engine;
extern const char *wclap_error_message;

struct Wclap;
struct WclapThread;

template<bool use64>
struct WclapTranslationScope;

static bool nameEquals(const wasm_name_t *name, const char *cName) {
	if (name->size != std::strlen(cName)) return false;
	for (size_t i = 0; i < name->size; ++i) {
		if (name->data[i] != cName[i]) return false;
	}
	return true;
}

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
	std::atomic<bool> isDestroying{false};
};

struct Wclap {
	clap_version clapVersion;

	wasmtime_module_t *module = nullptr;
	wasmtime_error_t *error = nullptr;
	bool wasm64 = false;
	wasmtime_sharedmemory_t *sharedMemory = nullptr;
	
	uint8_t * wasmMemory(uint32_t wasmP);
	uint8_t * wasmMemory(uint64_t wasmP);

	std::string wclapDir, presetDir, cacheDir, varDir;
	bool mustLinkDirs = false;

	Wclap(const std::string &wclapDir, const std::string &presetDir, const std::string &cacheDir, const std::string &varDir, bool mustLinkDirs);
	~Wclap();

	const char * initWasmBytes(const uint8_t *bytes, size_t size);
	const char * deinit();
	
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

	const void * getFactory(const char *factory_id);
	
private:
	bool initSuccess = false;

	bool hasPluginFactory = false;
	clap_plugin_factory nativePluginFactory;
	std::unique_ptr<WclapTranslationScope<false>> entryTranslationScope32;
	std::unique_ptr<WclapTranslationScope<true>> entryTranslationScope64;
	std::vector<std::unique_ptr<WclapTranslationScope<false>>> poolTranslationScope32;
	std::vector<std::unique_ptr<WclapTranslationScope<true>>> poolTranslationScope64;
	
	void returnToPool(std::unique_ptr<WclapTranslationScope<false>> &ptr);
	void returnToPool(std::unique_ptr<WclapTranslationScope<true>> &ptr);

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
	wasmtime_func_t mallocFunc; // direct export

	uint64_t clapEntryP64 = 0; // WASM pointer to clap_entry - might actually be 32-bit
	wasmtime_instance_t instance;
	
	WclapThread(Wclap &wclap, WclapThreadContext *threadContext) : wclap(wclap), threadContext(threadContext) {
	
	}

	// destructor is the only method allowed to be called from outside the assigned thread
	~WclapThread();
	
	const char * startInstance();
	
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

	uint64_t wasmMalloc(size_t bytes) {
		uint64_t wasmP;
		
		wasmtime_val_t args[1];
		wasmtime_val_t results[1];
		if (wclap.wasm64) {
			args[0].kind = WASMTIME_I64;
			args[0].of.i64 = bytes;
		} else {
			args[0].kind = WASMTIME_I32;
			args[0].of.i32 = (uint32_t)bytes;
		}
		
		error = wasmtime_func_call(context, &mallocFunc, args, 1, results, 1, &trap);
		if (error) return 0;
		if (trap) return 0;
		if (wclap.wasm64) {
			if (results[0].kind != WASMTIME_I64) return 0;
			return results[0].of.i64;
		} else {
			if (results[0].kind != WASMTIME_I32) return 0;
			return results[0].of.i32;
		}
	}

	const char * entryInit() {
		uint64_t funcIndex;
		if (wclap.wasm64) {
			auto *wasmEntry = (WasmClapEntry64 *)wclap.wasmMemory(clapEntryP64);
			wclap.clapVersion = wasmEntry->clap_version;
			funcIndex = wasmEntry->init;
		} else {
			auto *wasmEntry = (WasmClapEntry32 *)wclap.wasmMemory(clapEntryP64);
			wclap.clapVersion = wasmEntry->clap_version;
			funcIndex = wasmEntry->init;
		}

		LOG_EXPR(funcIndex);

		wasmtime_val_t funcVal;
		if (!wasmtime_table_get(context, &functionTable, funcIndex, &funcVal)) return "clap_entry.init doesn't resolve";
		if (funcVal.kind != WASMTIME_FUNCREF) return "wtf"; // should never happen, since we checked the function table type

		uint64_t pluginPath = copyStringConstantToWasm("/plugin/");
		if (!pluginPath) return "failed to copy string into WASM";

		wasmtime_val_t args[1], results[1];
		if (wclap.wasm64) {
			args[0].kind = WASMTIME_I64;
			args[0].of.i64 = pluginPath;
		} else {
			args[0].kind = WASMTIME_I32;
			args[0].of.i32 = (uint32_t)pluginPath;
		}

		LOG_EXPR((int)funcVal.kind);
		wasmtime_func_call(context, &funcVal.of.funcref, args, 1, results, 1, &trap);
		LOG_EXPR(trap);
		if (trap) return "init() threw (trapped)";
		if (!results[0].of.i32) { // bool, regardless of 32/64 bits
			return "init() returned false";
		}
		return nullptr;
	}

	void entryDeinit() {
		uint64_t funcIndex;
		if (wclap.wasm64) {
			auto *wasmEntry = (WasmClapEntry64 *)wclap.wasmMemory(clapEntryP64);
			funcIndex = wasmEntry->deinit;
		} else {
			auto *wasmEntry = (WasmClapEntry32 *)wclap.wasmMemory(clapEntryP64);
			funcIndex = wasmEntry->deinit;
		}

		wasmtime_val_t funcVal;
		if (!wasmtime_table_get(context, &functionTable, funcIndex, &funcVal)) return;
		if (funcVal.kind != WASMTIME_FUNCREF) return;

		// We completely ignore this result
		wasmtime_func_call(context, &funcVal.of.funcref, nullptr, 0, nullptr, 0, &trap);
	}

	uint64_t entryGetFactory(const char *factoryId) {
		uint64_t funcIndex;
		if (wclap.wasm64) {
			auto *wasmEntry = (WasmClapEntry64 *)wclap.wasmMemory(clapEntryP64);
			funcIndex = wasmEntry->get_factory;
		} else {
			auto *wasmEntry = (WasmClapEntry32 *)wclap.wasmMemory(clapEntryP64);
			funcIndex = wasmEntry->get_factory;
		}

		wasmtime_val_t funcVal;
		if (!wasmtime_table_get(context, &functionTable, funcIndex, &funcVal)) return 0;//"clap_entry.get_factory doesn't resolve";
		if (funcVal.kind != WASMTIME_FUNCREF) return 0; // should never happen, since we checked the function table type

		uint64_t wasmStr = copyStringConstantToWasm(factoryId);
		if (!wasmStr) return 0;

		wasmtime_val_t args[1], results[1];
		if (wclap.wasm64) {
			args[0].kind = WASMTIME_I64;
			args[0].of.i64 = wasmStr;
		} else {
			args[0].kind = WASMTIME_I32;
			args[0].of.i32 = (uint32_t)wasmStr;
		}

		error = wasmtime_func_call(context, &funcVal.of.funcref, args, 1, results, 1, &trap);
		if (error) return 0;
		if (trap) return 0; // "get_factory() threw (trapped)";
		return (results[0].kind == WASMTIME_I64) ? results[0].of.i64 : results[0].of.i32;
	}

private:
	struct WasmClapEntry64 {
		clap_version_t clap_version;
		uint64_t init;
		uint64_t deinit;
		uint64_t get_factory;
	};
	struct WasmClapEntry32 {
		clap_version_t clap_version;
		uint32_t init;
		uint32_t deinit;
		uint32_t get_factory;
	};

	uint64_t copyStringConstantToWasm(const char *str) {
		size_t bytes = std::strlen(str) + 1;
		uint64_t wasmP = wasmMalloc(bytes);
		if (!wasmP) return wasmP;
		
		auto *wasmBytes = (char *)(wclap.wasmMemory(wasmP));
		for (size_t i = 0; i < bytes; ++i) {
			wasmBytes[i] = str[i];
		}
		return wasmP;
	}
};
