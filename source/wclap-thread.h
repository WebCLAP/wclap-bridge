#pragma once

#include "clap/all.h"

#include "./wclap-arenas.h"

#include <fstream>
#include <vector>
#include <mutex>

namespace wclap {

struct Wclap;
void wclapSetError(Wclap &, const char *message);

struct WclapThread {
	Wclap &wclap;
	std::mutex mutex;

	struct Impl;
	Impl *impl;
	void implCreate();
	void implDestroy();

	uint64_t clapEntryP64 = 0; // WASM pointer to clap_entry - might actually be 32-bit
	
	WclapThread(Wclap &wclap);
	~WclapThread();
	
	uint64_t wasmMalloc(size_t bytes);
	
	void callWasmFnP(uint64_t fnP, wasmtime_val_raw *argsAndResults, size_t argN);
	
	/* Function call return types:
		V: void
		I: int32
		L: int64
		F: float
		D: double
		P: pointer (deduce from the function-pointer size)
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
