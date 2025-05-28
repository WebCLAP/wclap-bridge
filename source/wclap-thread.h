#pragma once

#include "wasmtime.h"
#include "clap/all.h"

#include <fstream>
#include <vector>
#include <mutex>

#include "./validity.h"
#include "./wclap-arenas.h"

namespace wclap {

// TODO: remove this and make the callWasm_* methods only have a return value, inferring bit-depth from the function pointer type and everything else from type overloads
namespace wclap32 {
	using WasmP = uint32_t;
}
namespace wclap64 {
	using WasmP = uint64_t;
}

struct Wclap;

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
	
	void callWasmFnP32(wclap32::WasmP fnP, wasmtime_val_raw *argsAndResults, size_t argN);
	
	/* Function call signatures: (return) (args...)
		V: void
		I: int32
		L: int64
		F: float
		D: double
		P: pointer (32-bit for now, but listed separately for future 64-bit compatibility)
		S: string (
	*/

	void callWasm_V(wclap32::WasmP fnP) {
		callWasmFnP32(fnP, nullptr, 0);
	}

	int32_t callWasm_IS(wclap32::WasmP fnP, const char *str) {
		auto reset = translationScope->scopedWasmReset();
		auto wasmStr = translationScope->copyStringToWasm(str);
		wasmtime_val_raw values[] = {{.i32=int32_t(wasmStr)}};
		callWasmFnP32(fnP, values, 1);
		return values[0].i32;
	}
	wclap32::WasmP callWasm_PS(wclap32::WasmP fnP, const char *str) {
		auto reset = translationScope->scopedWasmReset();
		auto wasmStr = translationScope->copyStringToWasm(str);
		wasmtime_val_raw values[] = {{.i32=int32_t(wasmStr)}};
		callWasmFnP32(fnP, values, 1);
		return uint32_t(values[0].i32);
	}
	uint32_t callWasm_IP(wclap32::WasmP fnP, wclap32::WasmP arg1) {
		wasmtime_val_raw values[] = {{.i32=int32_t(arg1)}};
		callWasmFnP32(fnP, values, 1);
		return values[0].i32;
	}
};

} // namespace
