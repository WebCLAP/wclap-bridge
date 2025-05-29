#pragma once

#include "wasmtime.h"
#include "clap/all.h"

#include <fstream>
#include <vector>
#include <mutex>

#include "./validity.h"
#include "./wclap-arenas.h"

namespace wclap {

struct Wclap;

inline bool trapIsTimeout(const wasm_trap_t *trap) {
	wasmtime_trap_code_t code;
	return wasmtime_trap_code(trap, &code) && code == WASMTIME_TRAP_CODE_INTERRUPT;
}

struct WclapThread {
	Wclap &wclap;
	std::unique_ptr<WclapArenas> translationScope;
	std::mutex mutex;

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
	
	WclapThread(Wclap &wclap, bool andInitModule=false);
	~WclapThread();

	void initModule(); // called only once, on the first thread
	void initEntry(); // also called only once, but after the thread/translation-scope is set up

	uint64_t wasmMalloc(size_t bytes);
	
	void setWasmDeadline(size_t ms) {
		if (validity.executionDeadlines) wasmtime_context_set_epoch_deadline(context, ms/10 + 2);
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
	int32_t callWasm_I(uint64_t fnP, Args ...args) {
		wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
		callWasmFnP(fnP, values, sizeof...(args));
		return values[0].i32;
	}
	template<typename ...Args>
	int64_t callWasm_L(uint64_t fnP, Args ...args) {
		wasmtime_val_raw values[sizeof...(args)] = {argToWasmVal(args)...};
		callWasmFnP(fnP, values, sizeof...(args));
		return values[0].i32;
	}

	/*
	int32_t callWasm_I(uint64_t fnP, uint32_t arg1) {
		wasmtime_val_raw values[] = {{.i32=int32_t(arg1)}};
		callWasmFnP(fnP, values, 1);
		return values[0].i32;
	}
	int32_t callWasm_I(uint64_t fnP, uint64_t arg1) {
		wasmtime_val_raw values[] = {{.i64=int64_t(arg1)}};
		callWasmFnP(fnP, values, 1);
		return values[0].i32;
	}
	int64_t callWasm_L(uint64_t fnP, uint32_t arg1) {
		wasmtime_val_raw values[] = {{.i32=int32_t(arg1)}};
		callWasmFnP(fnP, values, 1);
		return values[0].i64;
	}
	int64_t callWasm_L(uint64_t fnP, uint64_t arg1) {
		wasmtime_val_raw values[] = {{.i64=int64_t(arg1)}};
		callWasmFnP(fnP, values, 1);
		return values[0].i64;
	}
	*/

	template<class ...Args>
	uint32_t callWasm_P(uint32_t fnP, Args ...args) {
		return callWasm_I(fnP, std::forward<Args>(args)...);
	}
	template<class ...Args>
	uint64_t callWasm_P(uint64_t fnP, Args ...args) {
		return callWasm_L(fnP, std::forward<Args>(args)...);
	}

private:
	wasmtime_val_raw argToWasmVal(uint32_t v) {
		return {.i32=int32_t(v)};
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

} // namespace
