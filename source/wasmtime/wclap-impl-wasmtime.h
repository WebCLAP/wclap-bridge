#pragma once

#include "wasmtime.h"

#include <thread>
#include <atomic>

namespace wclap {

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

//---------- Actual implementations ----------

struct Wclap::Impl {
	wasmtime_module_t *module = nullptr;
	wasmtime_error_t *error = nullptr;
	wasmtime_sharedmemory_t *sharedMemory = nullptr;
};

struct WclapThread::Impl {
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
	wasmtime_instance_t instance;

	void setWasmDeadline();

	template<typename WasmP>
	void registerFunctionIndex(Wclap &wclap, wasmtime_val_t fnVal, WasmP &fnP) {
		uint64_t fnIndex = WasmP(-1);
		error = wasmtime_table_grow(context, &functionTable, 1, &fnVal, &fnIndex);
		if (error) {
			fnP = WasmP(-1);
			logError(error);
			return wclapSetError(wclap, "failed to register function");
		}
		
		if (fnP == 0) {
			fnP = WasmP(fnIndex);
		} else if (fnP != fnIndex) {
			fnP = WasmP(-1);
			return wclapSetError(wclap, "index mismatch when registering function");
		}
	}

	void callWasmFnP(Wclap &wclap, uint64_t fnP, wasmtime_val_raw *argsAndResults, size_t argN) {
		wasmtime_val_t funcVal;
		if (!wasmtime_table_get(context, &functionTable, fnP, &funcVal)) {
			return wclap.setError("function pointer doesn't resolve");
		}
		if (funcVal.kind != WASMTIME_FUNCREF) {
			// Shouldn't ever happen, but who knows
			return wclap.setError("function pointer doesn't resolve to a function");
		}

		setWasmDeadline();
		error = wasmtime_func_call_unchecked(context, &funcVal.of.funcref, argsAndResults, 1, &trap);
		if (error) {
			logError(error);
			return wclap.setError("calling function failed");
		}
		if (trap) {
			logTrap(trap);
			return wclap.setError(trapIsTimeout(trap) ? "function call timeout" : "function call threw (trapped)");
		}
	}
};

} // namespace
