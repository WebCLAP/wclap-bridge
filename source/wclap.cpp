#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "./wclap.h"
#include "./wclap-thread.h"
#include "./wclap-translation-scope.h"

std::ostream & operator<<(std::ostream &s, const wasm_byte_vec_t &bytes) {
	for (size_t i = 0; i < bytes.size; ++i) {
		s << bytes.data[i];
	}
	return s;
}

namespace wclap {

wasm_engine_t *global_wasm_engine;
const char *wclap_error_message;
std::string wclap_error_message_string;

//---------- Common Wclap instance ----------//

Wclap::Wclap(const std::string &wclapDir, const std::string &presetDir, const std::string &cacheDir, const std::string &varDir, bool mustLinkDirs) : wclapDir(wclapDir), presetDir(presetDir), cacheDir(cacheDir), varDir(varDir), mustLinkDirs(mustLinkDirs) {}

Wclap::~Wclap() {
	if (initSuccess) {
		auto scoped = lockRelaxedThread();

		if (wasm64) {
			abort(); // 64-bit not supported
		} else {
			auto entryP = uint32_t(scoped.thread.clapEntryP64);
			auto wasmEntry = view<wclap32::wclap_plugin_entry>(entryP);
			auto deinitFn = wasmEntry.deinit();
			scoped.thread.callWasm_V(deinitFn);
		}
	}

	{ // Clear all threads/scopes
		auto lock = writeLock();
		if (singleThread) singleThread->wasmReadyToDestroy();
		singleThread = nullptr;
		for (auto &thread : realtimeThreadPool) thread->wasmReadyToDestroy();
		realtimeThreadPool.clear();
		for (auto &thread : relaxedThreadPool) thread->wasmReadyToDestroy();
		relaxedThreadPool.clear();
		for (auto &scope : translationScopePool32) scope->wasmReadyToDestroy();
		translationScopePool32.clear();
	}
	
	if (methods32) wclap32::destroyMethods(methods32);

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
	
void Wclap::initWasmBytes(const uint8_t *bytes, size_t size) {
	error = wasmtime_module_new(global_wasm_engine, bytes, size, &module);
	if (error) {
		errorMessage = "Failed to compile module";
		return;
	}
	
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
			if (!globalType) {
				errorMessage = "clap_entry is not a global (value) export";
				return;
			}
			auto *valType = wasm_globaltype_content(globalType);
			auto kind = wasm_valtype_kind(valType);
			
			if (kind == WASMTIME_I64) {
				wasm64 = true;
				break;
			} else if (kind != WASMTIME_I32) {
				errorMessage = "clap_entry must be 32-bit or 64-bit memory address";
				return;
			}
		}
		wasm_exporttype_vec_delete(&exportTypes);
	}
	if (!foundClapEntry) {
		errorMessage = "clap_entry not found";
		return;
	}

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
				errorMessage = "imports non-shared memory";
				return;
			}
			bool mem64 = wasmtime_memorytype_is64(asMemory);
			if (mem64 != wasm64) {
				errorMessage = mem64 ? "64-bit memory but 32-bit clap_entry" : "32-bit memory but 64-bit clap_entry";
				return;
			}
			if (sharedMemory) {
				errorMessage = "multiple memory imports";
				return;
			}

			error = wasmtime_sharedmemory_new(global_wasm_engine, asMemory, &sharedMemory);
			if (error || !sharedMemory) {
				errorMessage = "failed to create shared memory";
			}
		}
		wasm_importtype_vec_delete(&importTypes);
	}

	WclapThread *rawPtr;

	if (wasm64) {
		errorMessage = "64-bit WASM not currently supported";
		return;
	} else {
		methods32 = wclap::wclap32::createMethods();
		rawPtr = new WclapThread(*this, claimTranslationScope32());
		wclap32::registerMethods(methods32, *rawPtr);
	}

	if (!errorMessage) rawPtr->initModule();
	if (!errorMessage) rawPtr->translationScope32->mallocIfNeeded(*rawPtr);
	if (errorMessage) {
		delete rawPtr;
		return;
	}
	initSuccess = true;
	
	if (!sharedMemory) {
		singleThread = std::unique_ptr<WclapThread>(rawPtr);
	} else {
		realtimeThreadPool.emplace_back(rawPtr);
	}
}

std::unique_ptr<WclapThread> Wclap::claimRealtimeThread() {
	if (singleThread) return {}; // null pointer
	{
		auto lock = writeLock();
		if (realtimeThreadPool.size()) {
			std::unique_ptr<WclapThread> result = std::move(realtimeThreadPool.back());
			realtimeThreadPool.pop_back();
			return result;
		}
	}
	if (wasm64) {
		abort();
	} else {
		auto threadPtr = std::unique_ptr<WclapThread>(new WclapThread(*this, claimTranslationScope32()));
		if (threadPtr) threadPtr->translationScope32->mallocIfNeeded(*threadPtr);
		return threadPtr;
	}
}

void Wclap::returnRealtimeThread(std::unique_ptr<WclapThread> &ptr) {
	auto lock = writeLock();
	realtimeThreadPool.emplace_back(std::move(ptr));
}

Wclap::ScopedThread::~ScopedThread() {
	if (locked) thread.mutex.unlock();
}

Wclap::ScopedThread Wclap::lockRelaxedThread() {
	if (singleThread) {
		singleThread->mutex.lock();
		return {*singleThread};
	}
	{
		auto lock = readLock();
		for (auto &threadPtr : relaxedThreadPool) {
			if (threadPtr->mutex.try_lock()) return {*threadPtr};
		}
	}

	WclapThread *rawPtr;
	if (wasm64) {
		abort();
	} else {
		rawPtr = new WclapThread(*this, claimTranslationScope32());
		rawPtr->translationScope32->mallocIfNeeded(*rawPtr);
	}
	rawPtr->mutex.lock();

	auto lock = writeLock();
	relaxedThreadPool.emplace_back(rawPtr);
	return {*rawPtr};
}

const void * Wclap::getFactory(const char *factory_id) {
	LOG_EXPR(factory_id);
	if (!std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) {
		LOG_EXPR(hasPluginFactory);
		if (wasm64) {
			return nullptr;
		} else {
			if (!hasPluginFactory) {
				auto scoped = lockRelaxedThread();

				auto entryP = uint32_t(scoped.thread.clapEntryP64);
				auto wasmEntry = view<wclap32::wclap_plugin_entry>(entryP);
				
				auto getFactoryFn = wasmEntry.get_factory();
				auto factoryP = scoped.thread.callWasm_PS(getFactoryFn, factory_id);
				if (!factoryP) return nullptr;
				scoped.thread.translationScope32->assignWasmToNative(factoryP, nativePluginFactory32);
				hasPluginFactory = true;
			}
			return &nativePluginFactory32;
		}
	}
	return nullptr;
}

} // namespace
