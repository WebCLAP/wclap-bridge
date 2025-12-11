#pragma once

#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "wclap/instance.hpp"

#include "wasmtime.h"

#include <iostream>
#include <shared_mutex>
#include <type_traits>

namespace wclap_wasmtime {

//---------- Logging ----------

inline void logMessage(const wasm_message_t &message) {
	for (size_t i = 0; i < message.size; ++i) {
		std::cout << message.data[i];
	}
	std::cout << std::endl;
}

inline void logError(const wasmtime_error_t *error) {
	wasm_message_t message;
	wasmtime_error_message(error, &message);
	logMessage(message);
	wasm_byte_vec_delete(&message);
}

inline void logTrap(const wasm_trap_t *trap) {
	wasm_message_t message;
	wasm_trap_message(trap, &message);
	logMessage(message);
	wasm_byte_vec_delete(&message);
}

inline bool trapIsTimeout(const wasm_trap_t *trap) {
	wasmtime_trap_code_t code;
	return wasmtime_trap_code(trap, &code) && code == WASMTIME_TRAP_CODE_INTERRUPT;
}

//---------- Other helpers ----------

static bool nameEquals(const wasm_name_t *name, const char *cName) {
	if (name->size != std::strlen(cName)) return false;
	for (size_t i = 0; i < name->size; ++i) {
		if (name->data[i] != cName[i]) return false;
	}
	return true;
}
static std::string nameToStr(const wasm_name_t *name) {
	std::string str;
	str.resize(name->size);
	for (size_t i = 0; i < name->size; ++i) str[i] = name->data[i];
	return str;
}

// Convert arguments to Wasmtime values
inline wasmtime_val_raw argToWasmVal(bool v) {
	return {.i32=int32_t(v)};
}
inline wasmtime_val_raw argToWasmVal(int8_t v) {
	return {.i32=int32_t(v)};
}
inline wasmtime_val_raw argToWasmVal(uint8_t v) {
	return {.i32=int32_t(v)};
}
inline wasmtime_val_raw argToWasmVal(int16_t v) {
	return {.i32=int32_t(v)};
}
inline wasmtime_val_raw argToWasmVal(uint16_t v) {
	return {.i32=int32_t(v)};
}
inline wasmtime_val_raw argToWasmVal(int32_t v) {
	return {.i32=v};
}
inline wasmtime_val_raw argToWasmVal(uint32_t v) {
	return {.i32=int32_t(v)};
}
inline wasmtime_val_raw argToWasmVal(int64_t v) {
	return {.i64=v};
}
inline wasmtime_val_raw argToWasmVal(uint64_t v) {
	return {.i64=int64_t(v)};
}
inline wasmtime_val_raw argToWasmVal(float v) {
	return {.f32=v};
}
inline wasmtime_val_raw argToWasmVal(double v) {
	return {.f64=v};
}
template<class V>
inline wasmtime_val_raw argToWasmVal(wclap32::Pointer<V> v) {
	return {.i32=int32_t(v.wasmPointer)};
}
template<class V>
inline wasmtime_val_raw argToWasmVal(wclap64::Pointer<V> v) {
	return {.i64=int64_t(v.wasmPointer)};
}

// generic form - has to be a class so we can do partial specialisation
template<typename T>
struct WasmValToArg {
	static T toArg(const wasmtime_val_raw &v);
};
template<typename T>
inline T wasmValToArg(const wasmtime_val_raw &v) {
	return WasmValToArg<T>::toArg(v);
}
// Can still specialise the function directly though, which is more concise
template<>
inline bool wasmValToArg<bool>(const wasmtime_val_raw &v) {
	return bool(v.i32);
}
template<>
inline int8_t wasmValToArg<int8_t>(const wasmtime_val_raw &v) {
	return int8_t(v.i32);
}
template<>
inline uint8_t wasmValToArg<uint8_t>(const wasmtime_val_raw &v) {
	return uint8_t(v.i32);
}
template<>
inline int16_t wasmValToArg<int16_t>(const wasmtime_val_raw &v) {
	return int16_t(v.i32);
}
template<>
inline uint16_t wasmValToArg<uint16_t>(const wasmtime_val_raw &v) {
	return uint16_t(v.i32);
}
template<>
inline int32_t wasmValToArg<int32_t>(const wasmtime_val_raw &v) {
	return v.i32;
}
template<>
inline uint32_t wasmValToArg<uint32_t>(const wasmtime_val_raw &v) {
	return uint32_t(v.i32);
}
template<>
inline int64_t wasmValToArg<int64_t>(const wasmtime_val_raw &v) {
	return v.i64;
}
template<>
inline uint64_t wasmValToArg<uint64_t>(const wasmtime_val_raw &v) {
	return uint64_t(v.i64);
}
template<>
inline float wasmValToArg<float>(const wasmtime_val_raw &v) {
	return v.f32;
}
template<>
inline double wasmValToArg<double>(const wasmtime_val_raw &v) {
	return v.f64;
}
// Except for these pointer types
template<class V>
struct WasmValToArg<wclap32::Pointer<V>> {
	inline wclap32::Pointer<V> toArg(const wasmtime_val_raw &v) {
		return {v.i32};
	}
};
template<class V>
struct WasmValToArg<wclap64::Pointer<V>> {
	inline wclap64::Pointer<V> toArg(const wasmtime_val_raw &v) {
		return {v.i64};
	}
};

//---------- Actual implementations ----------

struct InstanceImpl {
	void *handle;
	bool wasm64 = false;
	bool hadInit = false;

	static bool globalInit(unsigned int timeLimitMs);
	static void globalDeinit();

	wasmtime_module_t *wtModule = nullptr;
	wasmtime_error_t *wtError = nullptr;
	wasmtime_sharedmemory_t *wtSharedMemory = nullptr;
	std::string sharedMemoryImportModule, sharedMemoryImportName;
	const char *constantErrorMessage = nullptr;

	std::optional<std::string> error() const {
		if (constantErrorMessage) return {constantErrorMessage};
		
		if (!wtError) return {};
		char errorMessage[256] = "";

		wasm_message_t message;
		wasmtime_error_message(wtError, &message);
		std::strncpy(errorMessage, message.data, 255);
		wasm_byte_vec_delete(&message);
		return {errorMessage};
	}
	
	std::optional<std::string> wclapDir, presetDir, cacheDir, varDir;
	static std::optional<std::string> optStr(const char *str) {
		if (!str) return {};
		return {std::string{str}};
	}

	// `handle` is added by `wclap::Instance`, other constructor arguments are passed through
	InstanceImpl(void *handle, const unsigned char *wasmBytes, size_t wasmLength, const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir) : handle(handle), wclapDir(optStr(wclapDir)), presetDir(optStr(presetDir)), cacheDir(optStr(cacheDir)), varDir(optStr(varDir)) {
		// early returns are easier in normal functions
		setup(wasmBytes, wasmLength);

		// start one thread
		mainThread.setup();
	}
	InstanceImpl(const InstanceImpl &other) = delete;
	~InstanceImpl() {
		if (wtSharedMemory) wasmtime_sharedmemory_delete(wtSharedMemory);
		if (wtError) wasmtime_error_delete(wtError);
		if (wtModule) wasmtime_module_delete(wtModule);
	}
	void setup(const unsigned char *wasmBytes, size_t wasmLength);
	
	bool is64() const {
		return wasm64;
	}
	
	const char * path() const {
		return "/plugin.wclap";
	}
		
	uint32_t init32() {
		if (!mainThread.wasiInit()) return 0;
		hadInit = true;
		return uint32_t(mainThread.wclapEntryAs64);
	}
	uint64_t init64() {
		if (!mainThread.wasiInit()) return 0;
		hadInit = true;
		return mainThread.wclapEntryAs64;
	}

	struct Thread {
		InstanceImpl &instance;
		uint64_t wclapEntryAs64;

		// Delete these (in reverse order) if they're defined
		wasmtime_store_t *wtStore = nullptr;
		wasmtime_linker_t *wtLinker = nullptr;
		wasmtime_error_t *wtError = nullptr;
		wasm_trap_t *wtTrap = nullptr;

		// Owned by one of the above, so not our business to delete it
		wasmtime_context_t *wtContext = nullptr;
		wasmtime_memory_t wtMemory;
		wasmtime_table_t wtFunctionTable;
		wasmtime_func_t wtMallocFunc; // direct export
		wasmtime_instance_t wtInstance;

		void setWasmDeadline();

		Thread(InstanceImpl &instance) : instance(instance) {
		
		}
		Thread(const Thread &) = delete;
		~Thread() {
			if (wtTrap) {
				logTrap(wtTrap);
				wasm_trap_delete(wtTrap);
			}
			if (wtError) {
				logError(wtError);
				wasmtime_error_delete(wtError);
			}
			if (wtLinker) wasmtime_linker_delete(wtLinker);
			if (wtStore) wasmtime_store_delete(wtStore);
		}
		
		bool setup(); // creates the thread stuff, always called
		bool wasiInit(); // calls `_initialize()`, only once per Instance
		
		uint64_t wasmMalloc(size_t bytes);

		uint8_t * wasmMemory(uint64_t wasmP, uint64_t size) {
			if (instance.wtSharedMemory) {
				auto memorySize = wasmtime_sharedmemory_data_size(instance.wtSharedMemory);
				wasmP = std::min<uint64_t>(wasmP, memorySize - size);
				return wasmtime_sharedmemory_data(instance.wtSharedMemory) + wasmP;
			} else {
				auto memorySize = wasmtime_memory_data_size(wtContext, &wtMemory);
				wasmP = std::min<uint64_t>(wasmP, memorySize - size);
				return wasmtime_memory_data(wtContext, &wtMemory) + wasmP;
			}
		}

		void call(uint64_t fnP, wasmtime_val_raw *argsAndResults, size_t argN) {
			if (wtError || wtTrap || instance.wtError || instance.constantErrorMessage) {
				if (argN > 0) argsAndResults[0].i64 = 0; // returns 0
				return;
			}
		
			wasmtime_val_t funcVal;
			if (!wasmtime_table_get(wtContext, &wtFunctionTable, fnP, &funcVal)) {
				instance.constantErrorMessage = "function pointer doesn't resolve";
				if (argN > 0) argsAndResults[0].i64 = 0; // returns 0
				return;
			}
			if (funcVal.kind != WASMTIME_FUNCREF) {
				// Shouldn't ever happen, but who knows
				instance.constantErrorMessage = "function pointer doesn't resolve to a function";
				if (argN > 0) argsAndResults[0].i64 = 0; // returns 0
				return;
			}

			setWasmDeadline();
			wtError = wasmtime_func_call_unchecked(wtContext, &funcVal.of.funcref, argsAndResults, 1, &wtTrap);
			if (wtError) {
				logError(wtError);
				instance.constantErrorMessage = "WCLAP function call failed";
				if (argN > 0) argsAndResults[0].i64 = 0; // returns 0
				return;
			}
			if (wtTrap) {
				logTrap(wtTrap);
				instance.constantErrorMessage = (trapIsTimeout(wtTrap) ? "WCLAP function call timeout" : "WCLAP function call threw (trapped)");
				if (argN > 0) argsAndResults[0].i64 = 0; // returns 0
				return;
			}
		}

	};
	Thread mainThread{*this};

	uint32_t malloc32(uint32_t size) {
		return uint32_t(mainThread.wasmMalloc(size));
	}
	uint64_t malloc64(uint64_t size) {
		return uint64_t(mainThread.wasmMalloc(size));
	}

	template<class V>
	bool getArray(wclap32::Pointer<V> ptr, std::remove_cv_t<V> *result, size_t count) {
		auto *wasmMem = mainThread.wasmMemory(ptr.wasmPointer, sizeof(V)*count);
		if (!wasmMem) return false;
		std::memcpy(result, wasmMem, sizeof(V)*count);
		return true;
	}
	template<class V>
	bool setArray(wclap32::Pointer<V> ptr, const V *value, size_t count) {
		auto *wasmMem = mainThread.wasmMemory(ptr.wasmPointer, sizeof(V)*count);
		if (!wasmMem) return false;
		std::memcpy(wasmMem, value, sizeof(V)*count);
		return true;
	}

	template<class V>
	bool getArray(wclap64::Pointer<V> ptr, std::remove_cv_t<V> *result, size_t count) {
		auto *wasmMem = mainThread.wasmMemory(ptr.wasmPointer, sizeof(V)*count);
		if (!wasmMem) return false;
		std::memcpy(result, wasmMem, sizeof(V)*count);
		return true;
	}
	template<class V>
	bool setArray(wclap64::Pointer<V> ptr, const V *value, size_t count) {
		auto *wasmMem = mainThread.wasmMemory(ptr.wasmPointer, sizeof(V)*count);
		if (!wasmMem) return false;
		std::memcpy(wasmMem, value, sizeof(V)*count);
		return true;
	}

	template<class Return, class... Args>
	Return call(wclap32::Function<Return, Args...> fnPtr, Args... args) {
		if constexpr (std::is_void_v<Return>) {
			wasmtime_val_raw wasmVals[sizeof...(args)] = {argToWasmVal(args)...};
			mainThread.call(fnPtr.wasmPointer, wasmVals, sizeof...(args));
			return {};
		} else if constexpr (sizeof...(args) > 0) {
			wasmtime_val_raw wasmVals[sizeof...(args)] = {argToWasmVal(args)...};
			mainThread.call(fnPtr.wasmPointer, wasmVals, sizeof...(args));
			return wasmValToArg<Return>(wasmVals[0]);
		} else { // still need one slot for the return value
			wasmtime_val_raw wasmVals[1];
			mainThread.call(fnPtr.wasmPointer, &wasmVals, 1);
			return wasmValToArg<Return>(wasmVals[0]);
		}
	}
	template<class Return, class... Args>
	Return call(wclap64::Function<Return, Args...> fnPtr, Args... args) {
		if constexpr (std::is_void_v<Return>) {
			wasmtime_val_raw wasmVals[sizeof...(args)] = {argToWasmVal(args)...};
			mainThread.call(fnPtr.wasmPointer, wasmVals, sizeof...(args));
			return {};
		} else if constexpr (sizeof...(args) > 0) {
			wasmtime_val_raw wasmVals[sizeof...(args)] = {argToWasmVal(args)...};
			mainThread.call(fnPtr.wasmPointer, wasmVals, sizeof...(args));
			return wasmValToArg<Return>(wasmVals[0]);
		} else { // still need one slot for the return value
			wasmtime_val_raw wasmVals[1];
			mainThread.call(fnPtr.wasmPointer, &wasmVals, 1);
			return wasmValToArg<Return>(wasmVals[0]);
		}
	}

	template<class Return, class... Args>
	Return callAt(wclap32::Pointer<wclap32::Function<Return, Args...>> fnPtrPtr, Args... args) {
		wclap32::Function<Return, Args...> fnPtr;
		if (!getArray(fnPtrPtr, &fnPtr, 1)) return {};
		return call<Return, Args...>(fnPtr, args...);
	}
	template<class Return, class... Args>
	Return callAt(wclap64::Pointer<wclap64::Function<Return, Args...>> fnPtrPtr, Args... args) {
		wclap64::Function<Return, Args...> fnPtr;
		if (!getArray(fnPtrPtr, &fnPtr, 1)) return {};
		return call<Return, Args...>(fnPtr, args...);
	}
};

}; // namespace
