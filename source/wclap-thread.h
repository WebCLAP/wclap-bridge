#pragma once

#include "wasmtime.h"
#include "clap/all.h"

#include "./wclap-arenas.h"

#include <fstream>
#include <vector>
#include <mutex>

namespace wclap {

extern unsigned int timeLimitEpochs;

struct Wclap;
void wclapSetError(Wclap &, const char *message);

inline bool trapIsTimeout(const wasm_trap_t *trap) {
	wasmtime_trap_code_t code;
	return wasmtime_trap_code(trap, &code) && code == WASMTIME_TRAP_CODE_INTERRUPT;
}

struct WclapThread {
	Wclap &wclap;
	std::mutex mutex;

	// We should delete these (in reverse order) if they're defined
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
	
	WclapThread(Wclap &wclap);
	~WclapThread();
	
	static void logMessage(const wasm_message_t &message) {
		for (size_t i = 0; i < message.size; ++i) {
			std::cout << message.data[i];
		}
		std::cout << std::endl;
	}
	
	static void logError(const wasmtime_error_t *error) {
		wasm_message_t message;
		wasmtime_error_message(error, &message);
		logMessage(message);
		wasm_byte_vec_delete(&message);
	}
	
	static void logTrap(const wasm_trap_t *trap) {
		wasm_message_t message;
		wasm_trap_message(trap, &message);
		logMessage(message);
		wasm_byte_vec_delete(&message);
	}

	uint64_t wasmMalloc(size_t bytes);
	
	void setWasmDeadline() {
		if (timeLimitEpochs) wasmtime_context_set_epoch_deadline(context, timeLimitEpochs);
	}
	
	void callWasmFnP(uint64_t fnP, wasmtime_val_raw *argsAndResults, size_t argN);
	
	/* Function call signatures: (return) (args...)
		V: void
		I: int32
		L: int64
		F: float
		D: double
		P: pointer (32-bit for now, but listed separately for future 64-bit compatibility)
		S: string (
	*/

	void callWasm_V(uint64_t fnP) {
		callWasmFnP(fnP, nullptr, 0);
	}
	template<typename ...Args>
	void callWasm_V(uint64_t fnP, Args ...args) {
		wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
		callWasmFnP(fnP, values, sizeof...(args));
	}

	int32_t callWasm_I(uint64_t fnP) {
		wasmtime_val_raw values[1];
		callWasmFnP(fnP, values, 1);
		return values[0].i32;
	}
	template<typename ...Args>
	int32_t callWasm_I(uint64_t fnP, Args ...args) {
		wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
		callWasmFnP(fnP, values, sizeof...(args));
		return values[0].i32;
	}
	
	int64_t callWasm_L(uint64_t fnP) {
		wasmtime_val_raw values[1];
		callWasmFnP(fnP, values, 1);
		return values[0].i64;
	}
	template<typename ...Args>
	int64_t callWasm_L(uint64_t fnP, Args ...args) {
		wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
		callWasmFnP(fnP, values, sizeof...(args));
		return values[0].i32;
	}

	float callWasm_F(uint64_t fnP) {
		wasmtime_val_raw values[1];
		callWasmFnP(fnP, values, 1);
		return values[0].f32;
	}
	template<typename ...Args>
	float callWasm_F(uint64_t fnP, Args ...args) {
		wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
		callWasmFnP(fnP, values, sizeof...(args));
		return values[0].f32;
	}

	double callWasm_D(uint64_t fnP) {
		wasmtime_val_raw values[1];
		callWasmFnP(fnP, values, 1);
		return values[0].f64;
	}
	template<typename ...Args>
	double callWasm_D(uint64_t fnP, Args ...args) {
		wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
		callWasmFnP(fnP, values, sizeof...(args));
		return values[0].f64;
	}

	template<class ...Args>
	uint32_t callWasm_P(uint32_t fnP, Args ...args) {
		return callWasm_I(fnP, std::forward<Args>(args)...);
	}
	template<class ...Args>
	uint64_t callWasm_P(uint64_t fnP, Args ...args) {
		return callWasm_L(fnP, std::forward<Args>(args)...);
	}
	
	void wasmInit();
	
	template<typename WasmP>
	void registerFunctionIndex(wasmtime_val_t fnVal, WasmP &fnP) {
		uint64_t fnIndex = WasmP(-1);
		error = wasmtime_table_grow(context, &functionTable, 1, &fnVal, &fnIndex);
		if (error) {
			fnP = WasmP(-1);
			return wclapSetError(wclap, "failed to register function");
		}
		
		if (fnP == 0) {
			fnP = WasmP(fnIndex);
		} else if (fnP != fnIndex) {
			fnP = WasmP(-1);
			return wclapSetError(wclap, "index mismatch when registering function");
		}
	}
	
	template<void nativeFn(uint32_t), typename WasmP>
	void registerFunction(WasmP &fnP) {
		struct S {
			static wasm_trap_t * unchecked(void *env, wasmtime_caller_t *caller, wasmtime_val_raw_t *argsResults, size_t argsResultsLength) {
				nativeFn(argsResults[0].i32);
				return nullptr;
			}
		};

		wasm_valtype_vec_t params, results;
		wasm_valtype_vec_new_uninitialized(&params, 1);
		params.data[0] = wasm_valtype_new(WASM_I32);
		wasm_valtype_vec_new_empty(&results);
		wasm_functype_t *fnType = wasm_functype_new(&params, &results);

		wasmtime_val_t fnVal{WASMTIME_FUNCREF};
		wasmtime_func_new_unchecked(context, fnType, S::unchecked, nullptr, nullptr, &fnVal.of.funcref);
		registerFunctionIndex(fnVal, fnP);
	}

	template<uint32_t nativeFn(uint32_t, uint32_t), typename WasmP>
	void registerFunction(WasmP &fnP) {
		struct S {
			static wasm_trap_t * unchecked(void *env, wasmtime_caller_t *caller, wasmtime_val_raw_t *argsResults, size_t argsResultsLength) {
				argsResults[0].i32 = nativeFn(argsResults[0].i32, argsResults[1].i32);
				return nullptr;
			}
		};

		wasm_valtype_vec_t params, results;
		wasm_valtype_vec_new_uninitialized(&params, 2);
		params.data[0] = wasm_valtype_new(WASM_I32);
		params.data[1] = wasm_valtype_new(WASM_I32);
		wasm_valtype_vec_new_uninitialized(&results, 1);
		results.data[0] = wasm_valtype_new(WASM_I32);
		wasm_functype_t *fnType = wasm_functype_new(&params, &results);

		wasmtime_val_t fnVal{WASMTIME_FUNCREF};
		wasmtime_func_new_unchecked(context, fnType, S::unchecked, nullptr, nullptr, &fnVal.of.funcref);
		registerFunctionIndex(fnVal, fnP);
	}

	template<void nativeFn(uint64_t), typename WasmP>
	void registerFunction(WasmP &fnP) {
		struct S {
			static wasm_trap_t * unchecked(void *env, wasmtime_caller_t *caller, wasmtime_val_raw_t *argsResults, size_t argsResultsLength) {
				nativeFn(argsResults[0].i64);
				return nullptr;
			}
		};

		wasm_valtype_vec_t params, results;
		wasm_valtype_vec_new_uninitialized(&params, 1);
		params.data[0] = wasm_valtype_new(WASM_I64);
		wasm_valtype_vec_new_empty(&results);
		wasm_functype_t *fnType = wasm_functype_new(&params, &results);

		wasmtime_val_t fnVal{WASMTIME_FUNCREF};
		wasmtime_func_new_unchecked(context, fnType, S::unchecked, nullptr, nullptr, &fnVal.of.funcref);
		registerFunctionIndex(fnVal, fnP);
	}

	template<uint64_t nativeFn(uint64_t, uint64_t), typename WasmP>
	void registerFunction(WasmP &fnP) {
		struct S {
			static wasm_trap_t * unchecked(void *env, wasmtime_caller_t *caller, wasmtime_val_raw_t *argsResults, size_t argsResultsLength) {
				argsResults[0].i64 = nativeFn(argsResults[0].i64, argsResults[1].i64);
				return nullptr;
			}
		};

		wasm_valtype_vec_t params, results;
		wasm_valtype_vec_new_uninitialized(&params, 2);
		params.data[0] = wasm_valtype_new(WASM_I64);
		params.data[1] = wasm_valtype_new(WASM_I64);
		wasm_valtype_vec_new_uninitialized(&results, 1);
		results.data[0] = wasm_valtype_new(WASM_I64);
		wasm_functype_t *fnType = wasm_functype_new(&params, &results);

		wasmtime_val_t fnVal{WASMTIME_FUNCREF};
		wasmtime_func_new_unchecked(context, fnType, S::unchecked, nullptr, nullptr, &fnVal.of.funcref);
		registerFunctionIndex(fnVal, fnP);
	}
private:
	void startInstance();

	wasmtime_val_raw argToWasmVal(int32_t v) {
		return {.i32=v};
	}
	wasmtime_val_raw argToWasmVal(uint32_t v) {
		return {.i32=int32_t(v)};
	}
	wasmtime_val_raw argToWasmVal(int64_t v) {
		return {.i64=v};
	}
	wasmtime_val_raw argToWasmVal(uint64_t v) {
		return {.i64=int64_t(v)};
	}
	wasmtime_val_raw argToWasmVal(float v) {
		return {.f32=v};
	}
	wasmtime_val_raw argToWasmVal(double v) {
		return {.f64=v};
	}
};

struct WclapThreadWithArenas : public WclapThread {
	std::unique_ptr<WclapArenas> arenas;

	WclapThreadWithArenas(Wclap &wclap);
};

} // namespace
