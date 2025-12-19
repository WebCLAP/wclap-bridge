#pragma once

#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "wclap/instance.hpp"

#include "wasmi.h"

#include <iostream>
#include <shared_mutex>
#include <type_traits>
#include <string>
#include <cstring>
#include <filesystem>

namespace wclap_wasmi {

//---------- Logging ----------

inline void logMessage(const wasm_message_t &message) {
	for (size_t i = 0; i < message.size; ++i) {
		std::cout << message.data[i];
	}
	std::cout << std::endl;
}

inline void logError(const wasmi_error_t *error) {
	wasm_message_t message;
	wasmi_error_message(error, &message);
	logMessage(message);
	wasm_byte_vec_delete(&message);
}

inline void logTrap(const wasm_trap_t *trap) {
	wasm_message_t message;
	wasm_trap_message(trap, &message);
	logMessage(message);
	wasm_byte_vec_delete(&message);
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

// Convert arguments to Wasmi values
inline wasm_val_t argToWasmVal(bool v) {
	return {.kind=WASM_I32, .of={.i32=int32_t(v)}};
}
inline wasm_val_t argToWasmVal(int8_t v) {
	return {.kind=WASM_I32, .of={.i32=int32_t(v)}};
}
inline wasm_val_t argToWasmVal(uint8_t v) {
	return {.kind=WASM_I32, .of={.i32=int32_t(v)}};
}
inline wasm_val_t argToWasmVal(int16_t v) {
	return {.kind=WASM_I32, .of={.i32=int32_t(v)}};
}
inline wasm_val_t argToWasmVal(uint16_t v) {
	return {.kind=WASM_I32, .of={.i32=int32_t(v)}};
}
inline wasm_val_t argToWasmVal(int32_t v) {
	return {.kind=WASM_I32, .of={.i32=v}};
}
inline wasm_val_t argToWasmVal(uint32_t v) {
	return {.kind=WASM_I32, .of={.i32=int32_t(v)}};
}
inline wasm_val_t argToWasmVal(int64_t v) {
	return {.kind=WASM_I64, .of={.i64=v}};
}
inline wasm_val_t argToWasmVal(uint64_t v) {
	return {.kind=WASM_I64, .of={.i64=int64_t(v)}};
}
inline wasm_val_t argToWasmVal(float v) {
	return {.kind=WASM_F32, .of={.f32=v}};
}
inline wasm_val_t argToWasmVal(double v) {
	return {.kind=WASM_F64, .of={.f64=v}};
}
template<class V>
inline wasm_val_t argToWasmVal(wclap32::Pointer<V> v) {
	return {.kind=WASM_I32, .of={.i32=int32_t(v.wasmPointer)}};
}
template<class V>
inline wasm_val_t argToWasmVal(wclap64::Pointer<V> v) {
	return {.kind=WASM_I64, .of={.i64=int64_t(v.wasmPointer)}};
}

// generic form - has to be a class so we can do partial specialisation
template<typename T>
struct WasmValToArg {
	static T toArg(const wasm_val_t &v);
};
template<typename T>
inline T wasmValToArg(const wasm_val_t &v) {
	return WasmValToArg<T>::toArg(v);
}
// Can still specialise the function directly though, which is more concise
template<>
inline bool wasmValToArg<bool>(const wasm_val_t &v) {
	return bool(v.of.i32);
}
template<>
inline int8_t wasmValToArg<int8_t>(const wasm_val_t &v) {
	return int8_t(v.of.i32);
}
template<>
inline uint8_t wasmValToArg<uint8_t>(const wasm_val_t &v) {
	return uint8_t(v.of.i32);
}
template<>
inline int16_t wasmValToArg<int16_t>(const wasm_val_t &v) {
	return int16_t(v.of.i32);
}
template<>
inline uint16_t wasmValToArg<uint16_t>(const wasm_val_t &v) {
	return uint16_t(v.of.i32);
}
template<>
inline int32_t wasmValToArg<int32_t>(const wasm_val_t &v) {
	return v.of.i32;
}
template<>
inline uint32_t wasmValToArg<uint32_t>(const wasm_val_t &v) {
	return uint32_t(v.of.i32);
}
template<>
inline int64_t wasmValToArg<int64_t>(const wasm_val_t &v) {
	return v.of.i64;
}
template<>
inline uint64_t wasmValToArg<uint64_t>(const wasm_val_t &v) {
	return uint64_t(v.of.i64);
}
template<>
inline float wasmValToArg<float>(const wasm_val_t &v) {
	return v.of.f32;
}
template<>
inline double wasmValToArg<double>(const wasm_val_t &v) {
	return v.of.f64;
}
// Except for these pointer types
template<class V>
struct WasmValToArg<wclap32::Pointer<V>> {
	static inline wclap32::Pointer<V> toArg(const wasm_val_t &v) {
		return {uint32_t(v.of.i32)};
	}
};
template<class V>
struct WasmValToArg<wclap64::Pointer<V>> {
	static inline wclap64::Pointer<V> toArg(const wasm_val_t &v) {
		return {uint64_t(v.of.i64)};
	}
};

//---------- Wasmi type-signature helpers ----------

// generic form - has to be a class so we can do partial specialisation
template<typename T>
struct WasmValTypeCode {
	static inline uint8_t toCode();
};
template<typename T>
inline uint8_t wasmValTypeCode() {
	return WasmValTypeCode<T>::toCode();
}
// but we still specialise the function directly
template<>
inline uint8_t wasmValTypeCode<bool>() {
	return WASM_I32;
}
template<>
inline uint8_t wasmValTypeCode<int8_t>() {
	return WASM_I32;
}
template<>
inline uint8_t wasmValTypeCode<uint8_t>() {
	return WASM_I32;
}
template<>
inline uint8_t wasmValTypeCode<int16_t>() {
	return WASM_I32;
}
template<>
inline uint8_t wasmValTypeCode<uint16_t>() {
	return WASM_I32;
}
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
// Except for these pointer types
template<class V>
struct WasmValTypeCode<wclap32::Pointer<V>> {
	static inline uint8_t toCode() {
		return WASM_I32;
	}
};
template<class V>
struct WasmValTypeCode<wclap64::Pointer<V>> {
	static inline uint8_t toCode() {
		return WASM_I64;
	}
};

template<size_t index>
void setWasmiValTypes(wasm_valtype_vec_t *vec) {}

template <size_t index, class First, class ...Args>
void setWasmiValTypes(wasm_valtype_vec_t *vec) {
	vec->data[index] = wasm_valtype_new(wasmValTypeCode<First>());
	setWasmiValTypes<index + 1, Args...>(vec);
}

template<typename Return, typename ...Args>
wasm_functype_t * makeWasmiFuncType() {
	wasm_valtype_vec_t params, results;
	wasm_valtype_vec_new_uninitialized(&params, sizeof...(Args));
	setWasmiValTypes<0, Args...>(&params);
	if constexpr(std::is_void_v<Return>) {
		wasm_valtype_vec_new_empty(&results);
	} else {
		wasm_valtype_vec_new_uninitialized(&results, 1);
		results.data[0] = wasm_valtype_new(wasmValTypeCode<Return>());
	}

	return wasm_functype_new(&params, &results);
}

template <class ...Args, size_t ...Is>
std::tuple<void *, Args...> argsAsTuple(void *context, wasm_val_t *wasmArgs, std::index_sequence<Is...>) {
    return {context, wasmValToArg<Args>(wasmArgs[Is])...};
}

//---------- Actual implementations ----------

struct InstanceImpl;

struct InstanceGroup {
	bool hadInit = false;
	bool is64() const {
		return wasm64;
	}

	static bool globalInit(unsigned int timeLimitMs);
	static void globalDeinit();
	
	wasm_module_t *wtModule = nullptr;
	wasmi_error_t *wtError = nullptr;
	wasm_memory_t *wtSharedMemory = nullptr;
	std::string sharedMemoryImportModule, sharedMemoryImportName;
	const char *constantErrorMessage = nullptr;

	bool setError(const char *message) {
		auto groupLock = lock();
		if (hasError()) {
			std::cerr << "WCLAP: " << message << std::endl;
			return true;
		}
		constantErrorMessage = message;
		return true;
	}
	bool setError(wasmi_error_t *e) {
		if (!e) return false;
		if (hasError()) {
			// Keep first error, but log the new one
			logError(e);
			wasmi_error_delete(e);
			return true;
		}
		auto groupLock = lock();
		wtError = e;
		return true;
	}
	bool setError(wasm_trap_t *trap, const char *timeoutMessage, const char *otherMessage) {
		if (!trap) return false;
		logTrap(trap);
		setError(otherMessage);
		wasm_trap_delete(trap);
		return true;
	}
	bool hasError() const {
		return constantErrorMessage || wtError;
	}
	std::optional<std::string> error() const {
		auto groupLock = lock();
		if (constantErrorMessage) return {constantErrorMessage};
		
		if (!wtError) return {};
		char errorMessage[256] = "";

		wasm_message_t message;
		wasmi_error_message(wtError, &message);
		std::strncpy(errorMessage, message.data, 255);
		wasm_byte_vec_delete(&message);
		return {errorMessage};
	}

	std::optional<std::string> wclapDir, presetDir, cacheDir, varDir;
	static std::optional<std::string> optStr(const char *str) {
		if (!str) return {};
		return {std::string{str}};
	}

	void setup(const unsigned char *wasmBytes, size_t wasmLength);

	// `handle` is added by `wclap::Instance`, other constructor arguments are passed through
	InstanceGroup(const unsigned char *wasmBytes, size_t wasmLength, const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir) : wclapDir(optStr(wclapDir)), presetDir(optStr(presetDir)), cacheDir(optStr(cacheDir)), varDir(optStr(varDir)) {
		// early returns are easier in normal functions
		setup(wasmBytes, wasmLength);
	}
	~InstanceGroup() {
		if (wtSharedMemory) wasm_memory_delete(wtSharedMemory);
		if (wtError) wasmi_error_delete(wtError);
		if (wtModule) wasm_module_delete(wtModule);
	}
	
	std::optional<std::string> mapPath(const std::string &virtualPath) {
		std::filesystem::path path = virtualPath;
		path = std::filesystem::absolute(path.make_preferred()); // remove all `/../` etc.
		auto testPrefix = [&](std::optional<std::string> &dir, const char *prefix) -> bool {
			auto length = std::strlen(prefix);
			if (dir && virtualPath.substr(0, length) == prefix) {
				path = std::filesystem::path(path.string().substr(length));
				path = std::filesystem::path(*dir) / path;
				return true;
			}
			return false;
		};
		if (testPrefix(wclapDir, "/plugin.wclap/")) return path.string();
		if (testPrefix(presetDir, "/presets/")) return path.string();
		if (testPrefix(cacheDir, "/cache/")) return path.string();
		if (testPrefix(varDir, "/var/")) return path.string();
		return {};
	}

	wclap::Instance<InstanceImpl> *singleThread = nullptr;
	// If the WCLAP is single-threaded, this will only succeed once, and return `nullptr` from then on
	std::unique_ptr<wclap::Instance<InstanceImpl>> startInstance();

	std::unique_lock<std::recursive_mutex> lock() const {
		return std::unique_lock<std::recursive_mutex>{groupMutex};
	}
private:
	mutable std::recursive_mutex groupMutex;
	bool wasm64 = false;
};

struct InstanceImpl {
	void *handle;
	InstanceGroup &group;

	uint64_t wclapEntryAs64;
	std::recursive_mutex callMutex; // has to be recursive in case a WCLAP function calls out to a host which then calls a WCLAP function etc.

	// Delete these (in reverse order) if they're defined
	wasmi_store_t *wtStore = nullptr;
	wasmi_linker_t *wtLinker = nullptr;

	// Owned by one of the above, so not our business to delete it
	wasmi_context_t *wtContext = nullptr;
	wasmi_memory_t wtMemory;
	wasmi_table_t wtFunctionTable;
	wasmi_func_t wtMallocFunc; // direct export
	wasmi_instance_t wtInstance;

	InstanceImpl(void *handle, InstanceGroup &group) : handle(handle), group(group) {
		if (!setup()) return;
	}
	InstanceImpl(const InstanceImpl &other) = delete;
	~InstanceImpl() {
		if (wtLinker) wasmi_linker_delete(wtLinker);
		if (wtStore) wasmi_store_delete(wtStore);
	}
	
	bool is64() const {
		return group.is64();
	}
	
	const char * path() const {
		return "/plugin.wclap";
	}
		
	uint32_t init32() {
		return uint32_t(initInner());
	}
	uint64_t init64() {
		return uint64_t(initInner());
	}
	uint64_t initInner() {
		auto lock = group.lock();
		if (group.hasError()) return 0;
		if (group.hadInit) {
			group.setError("Tried to `.init()` WCLAP twice");
			return 0;
		}
		if (!wasiInit()) {
			group.setError("`.wasiInit()` returned false");
			return 0;
		}
		group.hadInit = true;
		return wclapEntryAs64;
	}

	void setWasmDeadline();
	bool setup(); // creates the thread stuff, always called
	bool wasiInit(); // calls `_initialize()`, only once per Instance
	
	uint64_t wtMalloc(size_t bytes);

	uint8_t * wasmMemory(uint64_t wasmP, uint64_t size) {
		if (group.wtSharedMemory) {
			auto memorySize = wasmi_sharedmemory_data_size(group.wtSharedMemory);
			wasmP = std::min<uint64_t>(wasmP, memorySize - size);
			return wasmi_sharedmemory_data(group.wtSharedMemory) + wasmP;
		} else {
			std::lock_guard<std::recursive_mutex> lock(callMutex);
			auto memorySize = wasmi_memory_data_size(wtContext, &wtMemory);
			wasmP = std::min<uint64_t>(wasmP, memorySize - size);
			return wasmi_memory_data(wtContext, &wtMemory) + wasmP;
		}
	}

	void wtCall(uint64_t fnP, wasm_val_t *argsAndResults, size_t argN) {
		std::lock_guard<std::recursive_mutex> lock(callMutex);
		if (group.hasError()) {
			if (argN > 0) argsAndResults[0].i64 = 0; // returns 0
			return;
		}
	
		wasm_val_t funcVal;
		if (!wasmi_table_get(wtContext, &wtFunctionTable, fnP, &funcVal)) {
			group.setError("function pointer doesn't resolve");
			if (argN > 0) argsAndResults[0].i64 = 0; // returns 0
			return;
		}
		if (funcVal.kind != WASMI_FUNCREF) {
			// Shouldn't ever happen, but who knows
			group.setError("function pointer doesn't resolve to a function");
			if (argN > 0) argsAndResults[0].i64 = 0; // returns 0
			return;
		}

		setWasmDeadline();
		wasm_trap_t *trap = nullptr;
		auto *error = wasmi_func_call_unchecked(wtContext, &funcVal.of.funcref, argsAndResults, 1, &trap);
		
		if (error) {
			group.setError(error);
			if (argN > 0) argsAndResults[0].i64 = 0; // returns 0
			return;
		}
		if (trap) {
			group.setError(trap, "WCLAP function call threw (trapped)");
			if (argN > 0) argsAndResults[0].i64 = 0; // returns 0
			return;
		}
	}

	uint32_t malloc32(uint32_t size) {
		return uint32_t(wtMalloc(size));
	}
	uint64_t malloc64(uint64_t size) {
		return uint64_t(wtMalloc(size));
	}

	template<class V>
	bool getArray(wclap32::Pointer<V> ptr, std::remove_cv_t<V> *result, size_t count) {
		auto *wasmMem = wasmMemory(ptr.wasmPointer, sizeof(V)*count);
		if (!wasmMem) return false;
		std::memcpy(result, wasmMem, sizeof(V)*count);
		return true;
	}
	template<class V>
	bool setArray(wclap32::Pointer<V> ptr, const V *value, size_t count) {
		auto *wasmMem = wasmMemory(ptr.wasmPointer, sizeof(V)*count);
		if (!wasmMem) return false;
		std::memcpy(wasmMem, value, sizeof(V)*count);
		return true;
	}

	template<class V>
	bool getArray(wclap64::Pointer<V> ptr, std::remove_cv_t<V> *result, size_t count) {
		auto *wasmMem = wasmMemory(ptr.wasmPointer, sizeof(V)*count);
		if (!wasmMem) return false;
		std::memcpy(result, wasmMem, sizeof(V)*count);
		return true;
	}
	template<class V>
	bool setArray(wclap64::Pointer<V> ptr, const V *value, size_t count) {
		auto *wasmMem = wasmMemory(ptr.wasmPointer, sizeof(V)*count);
		if (!wasmMem) return false;
		std::memcpy(wasmMem, value, sizeof(V)*count);
		return true;
	}

	template<class Return, class... Args>
	Return call(wclap32::Function<Return, Args...> fnPtr, Args... args) {
		if constexpr (std::is_void_v<Return>) {
			wasm_val_t wasmVals[sizeof...(args)] = {argToWasmVal(args)...};
			wtCall(fnPtr.wasmPointer, wasmVals, sizeof...(args));
			return;
		} else if constexpr (sizeof...(args) > 0) {
			wasm_val_t wasmVals[sizeof...(args)] = {argToWasmVal(args)...};
			wtCall(fnPtr.wasmPointer, wasmVals, sizeof...(args));
			return wasmValToArg<Return>(wasmVals[0]);
		} else { // still need one slot for the return value
			wasm_val_t wasmVals[1];
			wtCall(fnPtr.wasmPointer, &wasmVals, 1);
			return wasmValToArg<Return>(wasmVals[0]);
		}
	}
	template<class Return, class... Args>
	Return call(wclap64::Function<Return, Args...> fnPtr, Args... args) {
		if constexpr (std::is_void_v<Return>) {
			wasm_val_t wasmVals[sizeof...(args)] = {argToWasmVal(args)...};
			wtCall(fnPtr.wasmPointer, wasmVals, sizeof...(args));
			return;
		} else if constexpr (sizeof...(args) > 0) {
			wasm_val_t wasmVals[sizeof...(args)] = {argToWasmVal(args)...};
			wtCall(fnPtr.wasmPointer, wasmVals, sizeof...(args));
			return wasmValToArg<Return>(wasmVals[0]);
		} else { // still need one slot for the return value
			wasm_val_t wasmVals[1];
			wtCall(fnPtr.wasmPointer, &wasmVals, 1);
			return wasmValToArg<Return>(wasmVals[0]);
		}
	}

	template<class Return, class... Args>
	Return callAt(wclap32::Pointer<wclap32::Function<Return, Args...>> fnPtrPtr, Args... args) {
		wclap32::Function<Return, Args...> fnPtr;
		if (getArray(fnPtrPtr, &fnPtr, 1)) {
			return call<Return, Args...>(fnPtr, args...);
		}
		if constexpr (!std::is_void_v<Return>) return {};
	}
	template<class Return, class... Args>
	Return callAt(wclap64::Pointer<wclap64::Function<Return, Args...>> fnPtrPtr, Args... args) {
		wclap64::Function<Return, Args...> fnPtr;
		if (getArray(fnPtrPtr, &fnPtr, 1)) {
			return call<Return, Args...>(fnPtr, args...);
		}
		if constexpr (!std::is_void_v<Return>) return {};
	}
	
	template<class Return, class ...Args>
	uint64_t registerHostGeneric(void *context, Return (*nativeFn)(void *, Args...)) {
		struct WrappedFn {
			void *context;
			Return (*nativeFn)(void *, Args...);
			
			static wasm_trap_t * unchecked(void *env, wasmi_caller_t *caller, wasm_val_t *argsResults, size_t argsResultsLength) {
				auto &wrapped = *(WrappedFn *)env;
				auto args = argsAsTuple<Args...>(wrapped.context, argsResults, std::index_sequence_for<Args...>{});
				if constexpr (std::is_void_v<Return>) {
					std::apply(wrapped.nativeFn, args);
				} else {
					argsResults[0] = argToWasmVal(std::apply(wrapped.nativeFn, args));
				}
				return nullptr;
			}
			static void destroy(void *env) {
				auto *wrapped = (WrappedFn *)env;
				delete wrapped;
			}
		};
		auto *wrapped = new WrappedFn(WrappedFn{context, nativeFn});

		// get the function type
		wasm_val_t fnVal{.kind=WASM_FUNCREF};
		auto *fnType = makeWasmiFuncType<Return, Args...>();
		wasmi_func_new_unchecked(wtContext, fnType, WrappedFn::unchecked, wrapped, WrappedFn::destroy, &fnVal.of.funcref);
		wasm_functype_delete(fnType);

		// add it to the table
		uint64_t fnIndex = 0;
		auto *error = wasmi_table_grow(wtContext, &wtFunctionTable, 1, &fnVal, &fnIndex);
		if (error) {
			group.setError(error);
			group.setError("failed to add function-table entries for host methods");
			return -1;
		}
		return fnIndex;
	}

	template<class Return, class ...Args>
	wclap32::Function<Return, Args...> registerHost32(void *context, Return (*fn)(void *, Args...)) {
		return {uint32_t(registerHostGeneric(context, fn))};
	}
	template<class Return, class ...Args>
	wclap64::Function<Return, Args...> registerHost64(void *context, Return (*fn)(void *, Args...)) {
		return {uint32_t(registerHostGeneric(context, fn))};
	}
};

}; // namespace
