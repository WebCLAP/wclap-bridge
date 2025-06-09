#pragma once

#include "wasmtime.h"

namespace wclap { namespace _impl {

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

void callWithThread(WclapThread &thread, uint64_t fnP, wasmtime_val_raw *argsAndResults, size_t argN);

inline void callWasm_V(WclapThread &thread, uint64_t fnP) {
	callWithThread(thread, fnP, nullptr, 0);
}
template<typename ...Args>
void callWasm_V(WclapThread &thread, uint64_t fnP, Args ...args) {
	wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
	callWithThread(thread, fnP, values, sizeof...(args));
}

inline int32_t callWasm_I(WclapThread &thread, uint64_t fnP) {
	wasmtime_val_raw values[1];
	callWithThread(thread, fnP, values, 1);
	return values[0].i32;
}
template<typename ...Args>
int32_t callWasm_I(WclapThread &thread, uint64_t fnP, Args ...args) {
	wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
	callWithThread(thread, fnP, values, sizeof...(args));
	return values[0].i32;
}

inline int64_t callWasm_L(WclapThread &thread, uint64_t fnP) {
	wasmtime_val_raw values[1];
	callWithThread(thread, fnP, values, 1);
	return values[0].i64;
}
template<typename ...Args>
int64_t callWasm_L(WclapThread &thread, uint64_t fnP, Args ...args) {
	wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
	callWithThread(thread, fnP, values, sizeof...(args));
	return values[0].i32;
}

inline float callWasm_F(WclapThread &thread, uint64_t fnP) {
	wasmtime_val_raw values[1];
	callWithThread(thread, fnP, values, 1);
	return values[0].f32;
}
template<typename ...Args>
float callWasm_F(WclapThread &thread, uint64_t fnP, Args ...args) {
	wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
	callWithThread(thread, fnP, values, sizeof...(args));
	return values[0].f32;
}

inline double callWasm_D(WclapThread &thread, uint64_t fnP) {
	wasmtime_val_raw values[1];
	callWithThread(thread, fnP, values, 1);
	return values[0].f64;
}
template<typename ...Args>
double callWasm_D(WclapThread &thread, uint64_t fnP, Args ...args) {
	wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
	callWithThread(thread, fnP, values, sizeof...(args));
	return values[0].f64;
}

wasmtime_context_t * contextForThread(WclapThread &thread);
void registerFunctionIndex(WclapThread &thread, wasmtime_val_t fnVal, uint32_t &fnP);
void registerFunctionIndex(WclapThread &thread, wasmtime_val_t fnVal, uint64_t &fnP);

template<void nativeFn(uint32_t), typename WasmP>
void registerFunction(WclapThread &thread, WasmP &fnP) {
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
	wasmtime_func_new_unchecked(contextForThread(thread), fnType, S::unchecked, nullptr, nullptr, &fnVal.of.funcref);
	registerFunctionIndex(thread, fnVal, fnP);
}

template<uint32_t nativeFn(uint32_t, uint32_t), typename WasmP>
void registerFunction(WclapThread &thread, WasmP &fnP) {
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
	wasmtime_func_new_unchecked(contextForThread(thread), fnType, S::unchecked, nullptr, nullptr, &fnVal.of.funcref);
	registerFunctionIndex(thread, fnVal, fnP);
}

template<void nativeFn(uint64_t), typename WasmP>
void registerFunction(WclapThread &thread, WasmP &fnP) {
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
	wasmtime_func_new_unchecked(contextForThread(thread), fnType, S::unchecked, nullptr, nullptr, &fnVal.of.funcref);
	registerFunctionIndex(thread, fnVal, fnP);
}

template<uint64_t nativeFn(uint64_t, uint64_t), typename WasmP>
void registerFunction(WclapThread &thread, WasmP &fnP) {
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
	wasmtime_func_new_unchecked(contextForThread(thread), fnType, S::unchecked, nullptr, nullptr, &fnVal.of.funcref);
	registerFunctionIndex(thread, fnVal, fnP);
}

}} // namespace
