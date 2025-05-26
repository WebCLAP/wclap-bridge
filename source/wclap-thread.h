#pragma once

#include "wasmtime.h"
#include "clap/all.h"

#include <fstream>
#include <vector>
#include <mutex>

#include "./wclap.h"
#include "./wclap-translation-scope.h"

namespace wclap {

struct WclapThread {
	Wclap &wclap;
	std::unique_ptr<wclap32::WclapTranslationScope> translationScope32;
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
	
	WclapThread(Wclap &wclap, std::unique_ptr<wclap32::WclapTranslationScope> &&translationScope32);
	~WclapThread();

	// We're about to be destroyed (instead of returned to a pool)
	void wasmReadyToDestroy() {
		if (translationScope32) translationScope32->wasmReadyToDestroy();
	}

	void initModule(); // called only once, on the first thread
	void initEntry(); // also called only once, but after the thread/translation-scope is set up

	uint64_t wasmMalloc(size_t bytes) {
		uint64_t wasmP;
		
		wasmtime_val_t args[1];
		wasmtime_val_t results[1];
		if (wclap.wasm64) {
			args[0].kind = WASMTIME_I64;
			args[0].of.i64 = bytes;
		} else {
			args[0].kind = WASMTIME_I32;
			args[0].of.i32 = (uint32_t)bytes;
		}
		
		error = wasmtime_func_call(context, &mallocFunc, args, 1, results, 1, &trap);
		if (error) {
			wclap.errorMessage = "calling malloc() failed";
			return 0;
		}
		if (trap) {
			wclap.errorMessage = "calling malloc() threw (trapped)";
			return 0;
		}
		if (wclap.wasm64) {
			if (results[0].kind != WASMTIME_I64) return 0;
			return results[0].of.i64;
		} else {
			if (results[0].kind != WASMTIME_I32) return 0;
			return results[0].of.i32;
		}
	}
	
	void callWasmFnP32(wclap32::WasmP fnP, wasmtime_val_raw *argsAndResults, size_t argN) {
		wasmtime_val_t funcVal;
		if (!wasmtime_table_get(context, &functionTable, fnP, &funcVal)) {
			wclap.errorMessage = "function pointer doesn't resolve";
			return;
		}
		if (funcVal.kind != WASMTIME_FUNCREF) {
			wclap.errorMessage = "function pointer didn't resolve to a function";
			return;
		}

		++callDepth;
		error = wasmtime_func_call_unchecked(context, &funcVal.of.funcref, argsAndResults, 1, &trap);
		if (trap) wclap.errorMessage = "function call threw (trapped)";
		if (error) wclap.errorMessage = "calling function failed";
		if (!--callDepth) translationScope32->rewindWasm();
	}
		
	size_t callDepth = 0;
	
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

	uint32_t callWasm_IS(wclap32::WasmP fnP, const char *str) {
		wasmtime_val_raw values[] = {{.i32=int32_t(temporaryStringToWasm32(str))}};
		callWasmFnP32(fnP, values, 1);
		return values[0].i32;
	}
	wclap32::WasmP callWasm_PS(wclap32::WasmP fnP, const char *str) {
		wasmtime_val_raw values[] = {{.i32=int32_t(temporaryStringToWasm32(str))}};
		callWasmFnP32(fnP, values, 1);
		return uint32_t(values[0].i32);
	}
	uint32_t callWasm_IP(wclap32::WasmP fnP, wclap32::WasmP arg1) {
		wasmtime_val_raw values[] = {{.i32=int32_t(arg1)}};
		callWasmFnP32(fnP, values, 1);
		return values[0].i32;
	}

	wclap32::WasmP temporaryStringToWasm32(const char *str) {
		size_t length = std::strlen(str);
		auto wasmTmp = translationScope32->wasmBytes(length + 1);
		auto *nativeTmp = (char *)wclap.wasmMemory(wasmTmp);
		for (size_t i = 0; i < length; ++i) {
			nativeTmp[i] = str[i];
		}
		nativeTmp[length] = 0;
		return wasmTmp;
	}

};

} // namespace
