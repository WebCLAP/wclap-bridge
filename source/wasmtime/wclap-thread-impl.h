#pragma once

#include <functional>
#include <utility>
#include <atomic>

#include "./common.h"

namespace wclap {

template<typename T>
inline T wasmValToArg(const wasmtime_val_raw &v);

inline wasmtime_val_raw argToWasmVal(int32_t v) {
	return {.i32=v};
}
template<>
inline int32_t wasmValToArg<int32_t>(const wasmtime_val_raw &v) {
	return v.i32;
}
inline wasmtime_val_raw argToWasmVal(uint32_t v) {
	return {.i32=int32_t(v)};
}
template<>
inline uint32_t wasmValToArg<uint32_t>(const wasmtime_val_raw &v) {
	return uint32_t(v.i32);
}
inline wasmtime_val_raw argToWasmVal(int64_t v) {
	return {.i64=v};
}
template<>
inline int64_t wasmValToArg<int64_t>(const wasmtime_val_raw &v) {
	return v.i64;
}
inline wasmtime_val_raw argToWasmVal(uint64_t v) {
	return {.i64=int64_t(v)};
}
template<>
inline uint64_t wasmValToArg<uint64_t>(const wasmtime_val_raw &v) {
	return uint64_t(v.i64);
}
inline wasmtime_val_raw argToWasmVal(float v) {
	return {.f32=v};
}
template<>
inline float wasmValToArg<float>(const wasmtime_val_raw &v) {
	return v.f32;
}
inline wasmtime_val_raw argToWasmVal(double v) {
	return {.f64=v};
}
template<>
inline double wasmValToArg<double>(const wasmtime_val_raw &v) {
	return v.f64;
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

template<typename T>
inline uint8_t wasmValTypeCode();
template<>
inline uint8_t wasmValTypeCode<int32_t>() {
	return WASM_I32;
}
template<>
inline uint8_t wasmValTypeCode<uint32_t>() {
	return WASM_I32;
}
template<>
inline uint8_t wasmValTypeCode<int64_t>() {
	return WASM_I64;
}
template<>
inline uint8_t wasmValTypeCode<uint64_t>() {
	return WASM_I64;
}
template<>
inline uint8_t wasmValTypeCode<float>() {
	return WASM_F32;
}
template<>
inline uint8_t wasmValTypeCode<double>() {
	return WASM_F64;
}

template<size_t index>
inline void setWasmtimeValTypes(wasm_valtype_vec_t *vec) {}

template <size_t index, class First, class ...Args>
inline void setWasmtimeValTypes(wasm_valtype_vec_t *vec) {
	vec->data[index] = wasm_valtype_new(wasmValTypeCode<First>());
	setWasmtimeValTypes<index + 1, Args...>(vec);
}

template<typename Return, typename ...Args>
inline const wasm_functype_t * getWasmtimeFuncType() {
	static std::atomic<wasm_functype_t *> type = nullptr;
	if (type.load()) return type.load();

	wasm_valtype_vec_t params, results;
	wasm_valtype_vec_new_uninitialized(&params, sizeof...(Args));
	setWasmtimeValTypes<0, Args...>(&params);
	if constexpr(std::is_void_v<Return>) {
		wasm_valtype_vec_new_empty(&results);
	} else {
		wasm_valtype_vec_new_uninitialized(&results, 1);
		results.data[0] = wasm_valtype_new(wasmValTypeCode<Return>());
	}

	type.store(wasm_functype_new(&params, &results));
	return type.load();
}

template <class ...Args, size_t ...Is>
std::tuple<Wclap &, Args...> argsAsTuple(Wclap &wclap, wasmtime_val_raw_t *wasmArgs, std::index_sequence<Is...>) {
    return {wclap, wasmValToArg<Args>(wasmArgs[Is])...};
}
template<auto nativeFn /* hooray for C++17 */, typename WasmP, typename Return, class ...Args>
void registerFunctionOnThread(WclapThread &thread, WasmP &fnP, const std::function<Return(Wclap &, Args...)> &/*ignored, just used for its type*/) {
	struct S {
		static wasm_trap_t * unchecked(void *env, wasmtime_caller_t *caller, wasmtime_val_raw_t *argsResults, size_t argsResultsLength) {
			auto args = argsAsTuple<Args...>(*(Wclap *)env, argsResults, std::index_sequence_for<Args...>{});
			if constexpr (std::is_void_v<Return>) {
				std::apply(nativeFn, args);
			} else {
				argsResults[0] = argToWasmVal(std::apply(nativeFn, args));
			}
			return nullptr;
		}
	};

	wasmtime_val_t fnVal{WASMTIME_FUNCREF};
	auto *fnType = getWasmtimeFuncType<Return, Args...>();
	wasmtime_func_new_unchecked(thread.impl->context, fnType, S::unchecked, &thread.wclap, nullptr, &fnVal.of.funcref);
	thread.impl->registerFunctionIndex(thread.wclap, fnVal, fnP);
}

template<typename FnStruct>
void WclapThread::registerFunction(FnStruct &fnStruct) {
	using NativeFnType = decltype(std::function{FnStruct::native});
	registerFunctionOnThread<FnStruct::native>(*this, fnStruct.wasmP, NativeFnType{});
}

} // namespace
