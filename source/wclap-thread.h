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

	/*
	static char typeCode(wasm_valkind_t k) {
		if (k < 4) {
			return "ILFD"[k];
		} else if (k == WASM_EXTERNREF) {
			return 'X';
		} else if (k == WASM_FUNCREF) {
			return '$';
		}
		return '?';
	}
	static char typeCode(const wasm_valtype_t *t) {
		return typeCode(wasm_valtype_kind(t));
	}
	*/

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
		if (!--callDepth) translationScope32->clearTemporaryWasm();
	}
		
	size_t callDepth = 0;
	
	/* Function call signatures: (return) (args...)
		V: void
		I: int32
		L: int64
		F: float
		D: double
		P: pointer (32-bit for now)
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
		return callWasm_IS(fnP, str);
	}

	wclap32::WasmP temporaryStringToWasm32(const char *str) {
		size_t length = std::strlen(str);
		auto wasmTmp = translationScope32->temporaryWasmBytes(length + 1);
		auto *nativeTmp = (char *)wclap.wasmMemory(wasmTmp);
		for (size_t i = 0; i < length; ++i) {
			nativeTmp[i] = str[i];
		}
		nativeTmp[length] = 0;
		return wasmTmp;
	}
	
/*
	void entryDeinit() {
		uint64_t funcIndex;
		if (wclap.wasm64) {
			auto *wasmEntry = (WasmClapEntry64 *)wclap.wasmMemory(clapEntryP64);
			funcIndex = wasmEntry->deinit;
		} else {
			auto *wasmEntry = (WasmClapEntry32 *)wclap.wasmMemory(clapEntryP64);
			funcIndex = wasmEntry->deinit;
		}

		wasmtime_val_t funcVal;
		if (!wasmtime_table_get(context, &functionTable, funcIndex, &funcVal)) return;
		if (funcVal.kind != WASMTIME_FUNCREF) return;

		// We completely ignore this result
		wasmtime_func_call(context, &funcVal.of.funcref, nullptr, 0, nullptr, 0, &trap);
	}

	uint64_t entryGetFactory(const char *factoryId) {
		uint64_t funcIndex;
		if (wclap.wasm64) {
			auto *wasmEntry = (WasmClapEntry64 *)wclap.wasmMemory(clapEntryP64);
			funcIndex = wasmEntry->get_factory;
		} else {
			auto *wasmEntry = (WasmClapEntry32 *)wclap.wasmMemory(clapEntryP64);
			funcIndex = wasmEntry->get_factory;
		}

		wasmtime_val_t funcVal;
		if (!wasmtime_table_get(context, &functionTable, funcIndex, &funcVal)) return 0;//"clap_entry.get_factory doesn't resolve";
		if (funcVal.kind != WASMTIME_FUNCREF) return 0; // should never happen, since we checked the function table type

		uint64_t wasmStr = copyStringConstantToWasm(factoryId);
		if (!wasmStr) return 0;

		wasmtime_val_t args[1], results[1];
		if (wclap.wasm64) {
			args[0].kind = WASMTIME_I64;
			args[0].of.i64 = wasmStr;
		} else {
			args[0].kind = WASMTIME_I32;
			args[0].of.i32 = (uint32_t)wasmStr;
		}

		error = wasmtime_func_call(context, &funcVal.of.funcref, args, 1, results, 1, &trap);
		if (error) return 0;
		if (trap) return 0; // "get_factory() threw (trapped)";
		return (results[0].kind == WASMTIME_I64) ? results[0].of.i64 : results[0].of.i32;
	}
*/

private:
	/*
	uint64_t copyStringConstantToWasm(const char *str) {
		size_t bytes = std::strlen(str) + 1;
		uint64_t wasmP = wasmMalloc(bytes);
		if (!wasmP) return wasmP;
		
		auto *wasmBytes = (char *)(wclap.wasmMemory(wasmP));
		for (size_t i = 0; i < bytes; ++i) {
			wasmBytes[i] = str[i];
		}
		return wasmP;
	}
	*/
};

} // namespace
