#pragma once

#include <functional>

#include "./common.h"

namespace wclap {

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

inline void WclapThread::callWasm_V(uint64_t fnP) {
	impl->callWasmFnP(wclap, fnP, nullptr, 0);
}
template<typename ...Args>
void WclapThread::callWasm_V(uint64_t fnP, Args ...args) {
	wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
	impl->callWasmFnP(wclap, fnP, values, sizeof...(args));
}

inline int32_t WclapThread::callWasm_I(uint64_t fnP) {
	wasmtime_val_raw values[1];
	impl->callWasmFnP(wclap, fnP, values, 1);
	return values[0].i32;
}
template<typename ...Args>
int32_t WclapThread::callWasm_I(uint64_t fnP, Args ...args) {
	wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
	impl->callWasmFnP(wclap, fnP, values, sizeof...(args));
	return values[0].i32;
}

inline int64_t WclapThread::callWasm_L(uint64_t fnP) {
	wasmtime_val_raw values[1];
	impl->callWasmFnP(wclap, fnP, values, 1);
	return values[0].i64;
}
template<typename ...Args>
int64_t WclapThread::callWasm_L(uint64_t fnP, Args ...args) {
	wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
	impl->callWasmFnP(wclap, fnP, values, sizeof...(args));
	return values[0].i32;
}

inline float WclapThread::callWasm_F(uint64_t fnP) {
	wasmtime_val_raw values[1];
	impl->callWasmFnP(wclap, fnP, values, 1);
	return values[0].f32;
}
template<typename ...Args>
float WclapThread::callWasm_F(uint64_t fnP, Args ...args) {
	wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
	impl->callWasmFnP(wclap, fnP, values, sizeof...(args));
	return values[0].f32;
}

inline double WclapThread::callWasm_D(uint64_t fnP) {
	wasmtime_val_raw values[1];
	impl->callWasmFnP(wclap, fnP, values, 1);
	return values[0].f64;
}
template<typename ...Args>
double WclapThread::callWasm_D(uint64_t fnP, Args ...args) {
	wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
	impl->callWasmFnP(wclap, fnP, values, sizeof...(args));
	return values[0].f64;
}

//template<auto nativeFn /* hooray for C++17 */, typename WasmP, typename Return, class ...Args>
//void registerFunctionOnThread(WclapThread &thread, WasmP &fnP, const std::function<Return(Args...)> &/*ignored, just used for its type*/) {
//	fnP = 0;
//}

//*
template<void nativeFn(Wclap &, uint32_t), typename WasmP>
void registerFunctionOnThread(WclapThread &thread, WasmP &fnP) {
	struct S {
		static wasm_trap_t * unchecked(void *env, wasmtime_caller_t *caller, wasmtime_val_raw_t *argsResults, size_t argsResultsLength) {
			nativeFn(*(Wclap *)env, argsResults[0].i32);
			return nullptr;
		}
	};

	wasm_valtype_vec_t params, results;
	wasm_valtype_vec_new_uninitialized(&params, 1);
	params.data[0] = wasm_valtype_new(WASM_I32);
	wasm_valtype_vec_new_empty(&results);
	wasm_functype_t *fnType = wasm_functype_new(&params, &results);

	wasmtime_val_t fnVal{WASMTIME_FUNCREF};
	wasmtime_func_new_unchecked(thread.impl->context, fnType, S::unchecked, &thread.wclap, nullptr, &fnVal.of.funcref);
	thread.impl->registerFunctionIndex(thread.wclap, fnVal, fnP);
}

template<uint32_t nativeFn(Wclap &, uint32_t, uint32_t), typename WasmP>
void registerFunctionOnThread(WclapThread &thread, WasmP &fnP) {
	struct S {
		static wasm_trap_t * unchecked(void *env, wasmtime_caller_t *caller, wasmtime_val_raw_t *argsResults, size_t argsResultsLength) {
			argsResults[0].i32 = nativeFn(*(Wclap *)env, argsResults[0].i32, argsResults[1].i32);
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
	wasmtime_func_new_unchecked(thread.impl->context, fnType, S::unchecked, &thread.wclap, nullptr, &fnVal.of.funcref);
	thread.impl->registerFunctionIndex(thread.wclap, fnVal, fnP);
}

template<void nativeFn(Wclap &, uint64_t), typename WasmP>
void registerFunctionOnThread(WclapThread &thread, WasmP &fnP) {
	struct S {
		static wasm_trap_t * unchecked(void *env, wasmtime_caller_t *caller, wasmtime_val_raw_t *argsResults, size_t argsResultsLength) {
			nativeFn(*(Wclap *)env, argsResults[0].i64);
			return nullptr;
		}
	};

	wasm_valtype_vec_t params, results;
	wasm_valtype_vec_new_uninitialized(&params, 1);
	params.data[0] = wasm_valtype_new(WASM_I64);
	wasm_valtype_vec_new_empty(&results);
	wasm_functype_t *fnType = wasm_functype_new(&params, &results);

	wasmtime_val_t fnVal{WASMTIME_FUNCREF};
	wasmtime_func_new_unchecked(thread.impl->context, fnType, S::unchecked, &thread.wclap, nullptr, &fnVal.of.funcref);
	thread.impl->registerFunctionIndex(thread.wclap, fnVal, fnP);
}

template<uint64_t nativeFn(Wclap &, uint64_t, uint64_t), typename WasmP>
void registerFunctionOnThread(WclapThread &thread, WasmP &fnP) {
	struct S {
		static wasm_trap_t * unchecked(void *env, wasmtime_caller_t *caller, wasmtime_val_raw_t *argsResults, size_t argsResultsLength) {
			argsResults[0].i64 = nativeFn(*(Wclap *)env, argsResults[0].i64, argsResults[1].i64);
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
	wasmtime_func_new_unchecked(thread.impl->context, fnType, S::unchecked, &thread.wclap, nullptr, &fnVal.of.funcref);
	thread.impl->registerFunctionIndex(thread.wclap, fnVal, fnP);
}
//*/

template<typename FnStruct>
void WclapThread::registerFunction(FnStruct &fnStruct) {
//	using NativeFnType = decltype(std::function{FnStruct::native});
//	registerFunctionOnThread<FnStruct::native>(*this, fnStruct.wasmP, NativeFnType{});
	registerFunctionOnThread<FnStruct::native>(*this, fnStruct.wasmP);
}

} // namespace
