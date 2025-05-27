#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "./wclap-thread.h"
#include "./wclap.h"
#include "./wclap32/wclap-translation.h"

namespace wclap {

WclapThread::WclapThread(Wclap &wclap, bool andInitModule) : wclap(wclap) {
	if (wasiConfig) {
		wclap.errorMessage = "instantiate() called twice";
		return;
	}
	
	wasiConfig = wasi_config_new();
	if (!wasiConfig) {
		wclap.errorMessage = "Failed to create WASI config";
		return;
	}

	wasi_config_inherit_stdout(wasiConfig);
	wasi_config_inherit_stderr(wasiConfig);
	// Link various directories - failure is allowed if `mustLinkDirs` is false
	if (wclap.wclapDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.wclapDir.c_str(), "/plugin/", WASMTIME_WASI_DIR_PERMS_READ, WASMTIME_WASI_FILE_PERMS_READ)) {
			if (wclap.mustLinkDirs) {
				wclap.errorMessage = "Failed to open /plugin/ in WASI config";
				return;
			}
		}
	}
	if (wclap.presetDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.presetDir.c_str(), "/presets/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			if (wclap.mustLinkDirs) {
				wclap.errorMessage = "Failed to open /presets/ in WASI config";
				return;
			}
		}
	}
	if (wclap.cacheDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.cacheDir.c_str(), "/cache/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			if (wclap.mustLinkDirs) {
				wclap.errorMessage = "Failed to open /cache/ in WASI config";
				return;
			}
		}
	}
	if (wclap.varDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.varDir.c_str(), "/var/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			if (wclap.mustLinkDirs) {
				wclap.errorMessage = "Failed to open /var/ in WASI config";
				return;
			}
		}
	}
	
	//---------- Start the instance ----------//

	store = wasmtime_store_new(global_wasm_engine, nullptr, nullptr);
	if (!store) {
		wclap.errorMessage = "Failed to create store";
		return;
	}
	context = wasmtime_store_context(store);
	if (!context) {
		wclap.errorMessage = "Failed to create context";
		return;
	}
	
	// Create a linker with WASI functions defined
	linker = wasmtime_linker_new(global_wasm_engine);
	if (!linker) {
		wclap.errorMessage = "error creating linker";
		return;
	}
	error = wasmtime_linker_define_wasi(linker);
	if (error) {
		wclap.errorMessage = "error linking WASI";
		return;
	}

	error = wasmtime_context_set_wasi(context, wasiConfig);
	if (error) {
		wclap.errorMessage = "Failed to configure WASI";
		return;
	}
	wasiConfig = nullptr;

	// This doesn't call the WASI _start() or _initialize() methods
	error = wasmtime_linker_instantiate(linker, context, wclap.module, &instance, &trap);
	if (error) {
		wclap.errorMessage = "failed to create instance";
		return;
	}
	if (trap) {
		wclap.errorMessage = "failed to start instance";
		return;
	}

	//---------- Find exports ----------//

	char *name;
	size_t nameSize;
	wasmtime_extern_t item;
	
	if (wasmtime_instance_export_get(context, &instance, "memory", 6, &item)) {
		if (item.kind == WASMTIME_EXTERN_MEMORY) {
			memory = item.of.memory;
		} else if (item.kind == WASMTIME_EXTERN_SHAREDMEMORY) {
			// TODO: it should be the same as the import - not sure how to check this
			if (!wclap.sharedMemory) {
				wclap.errorMessage = "exported shared memory, but didn't import it";
				return;
			}
		} else {
			wasmtime_extern_delete(&item);
			wclap.errorMessage = "exported memory isn't a (Shared)Memory";
			return;
		}
		wasmtime_extern_delete(&item);
	} else if (!wclap.sharedMemory) {
		wclap.errorMessage = "must either export memory or import shared memory";
		return;
	}

	if (!wasmtime_instance_export_get(context, &instance, "clap_entry", 10, &item)) {
		wclap.errorMessage = "clap_entry not exported";
		return;
	}
	if (item.kind == WASMTIME_EXTERN_GLOBAL) {
		wasmtime_val_t v;
		wasmtime_global_get(context, &item.of.global, &v);
		if (v.kind == WASM_I32 && !wclap.wasm64) {
			clapEntryP64 = v.of.i32; // We store it as 64 bits, even though we know it's a 32-bit one
		} else if (v.kind == WASM_I64 && wclap.wasm64) {
			clapEntryP64 = v.of.i64;
		} else {
			wclap.errorMessage = "clap_entry is not a (correctly-sized) pointer";
			return;
		}
	} else {
		wasmtime_extern_delete(&item);
		wclap.errorMessage = "clap_entry isn't a Global";
		return;
	}
	wasmtime_extern_delete(&item);

	if (!wasmtime_instance_export_get(context, &instance, "malloc", 6, &item)) {
		wclap.errorMessage = "malloc not exported";
		return;
	}
	if (item.kind == WASMTIME_EXTERN_FUNC) {
		wasm_functype_t *type = wasmtime_func_type(context, &item.of.func);
		const wasm_valtype_vec_t *params = wasm_functype_params(type);
		const wasm_valtype_vec_t *results = wasm_functype_results(type);
		if (params->size != 1 || results->size != 1) {
			wclap.errorMessage = "malloc() function signature mismatch";
		}
		if (wasm_valtype_kind(params->data[0]) != wasm_valtype_kind(results->data[0])) {
			wclap.errorMessage = "malloc() function signature mismatch";
			return;
		}
		if (wclap.wasm64) {
			if (wasm_valtype_kind(params->data[0]) != WASMTIME_I64) {
				wclap.errorMessage = "malloc() function signature mismatch";
				return;
			}
		} else {
			if (wasm_valtype_kind(params->data[0]) != WASMTIME_I32) {
				wclap.errorMessage = "malloc() function signature mismatch";
			}
		}
		mallocFunc = item.of.func;
		wasm_functype_delete(type);
	} else {
		wasmtime_extern_delete(&item);
		wclap.errorMessage = "malloc isn't a Function";
		return;
	}
	wasmtime_extern_delete(&item);

	// Look for the first function table
	size_t exportIndex = 0;
	while (wasmtime_instance_export_nth(context, &instance, exportIndex, &name, &nameSize, &item)) {
		if (item.kind == WASMTIME_EXTERN_TABLE) {
			wasm_tabletype_t *type = wasmtime_table_type(context, &item.of.table);
			const wasm_limits_t limits = *wasm_tabletype_limits(type);
			auto elementKind = wasm_valtype_kind(wasm_tabletype_element(type));
			wasm_tabletype_delete(type);

			if (elementKind == WASM_FUNCREF) {
				if (limits.max < 65536 || limits.max - 65536 < limits.min) {
					wclap.errorMessage = "exported function table can't grow enough for CLAP host functions";
					return;
				}
				functionTable = item.of.table;
				break;
			}
		}
		wasmtime_extern_delete(&item);
		++exportIndex;
	}
	
	if (andInitModule) initModule();
	
	if (wclap.wasm64) {
		LOG_EXPR("WclapThread::WclapThread");
		abort();
	} else {
		translationScope32 = std::unique_ptr<wclap32::WclapTranslationScope>{
			new wclap32::WclapTranslationScope(wclap, *this)
		};
	}
}

WclapThread::~WclapThread() {
	if (trap) {
		wclap_error_message_string = wclap_error_message;
		wclap_error_message_string += ": ";
		wasm_message_t message;
		wasm_trap_message(trap, &message);
		wclap_error_message_string += message.data; // should always be null-terminated C string
		wclap_error_message = wclap_error_message_string.c_str();
	}

	if (error) {
		wclap_error_message_string = wclap_error_message;
		wclap_error_message_string += ": ";
		wasm_name_t message;
		wasmtime_error_message(error, &message);
		wclap_error_message_string.append(message.data, message.size);
		wclap_error_message = wclap_error_message_string.c_str();

		wasmtime_error_delete(error);
	}
	if (linker) wasmtime_linker_delete(linker);
	if (store) wasmtime_store_delete(store);
}

void WclapThread::initModule() {
	wasmtime_extern_t item;

	// Call the WASI entry-point `_initialize()` if it exists - WCLAPs don't *have* to use WASI, so it's fine not to
	if (wasmtime_instance_export_get(context, &instance, "_initialize", 11, &item)) {
		if (item.kind == WASMTIME_EXTERN_FUNC) {
			wasm_functype_t *type = wasmtime_func_type(context, &item.of.func);
			const wasm_valtype_vec_t *params = wasm_functype_params(type);
			const wasm_valtype_vec_t *results = wasm_functype_results(type);
			if (params->size != 0 || results->size != 0) {
				wclap.errorMessage = "_initialize() function signature mismatch";
				return;
			}
			wasm_functype_delete(type);

			wasmtime_func_call(context, &item.of.func, nullptr, 0, nullptr, 0, &trap);
			if (trap) {
				wasmtime_extern_delete(&item);
				wclap.errorMessage = "_initialize() threw an error";
				return;
			}
		} else {
			wasmtime_extern_delete(&item);
			wclap.errorMessage = "_initialize isn't a function";
			return;
		}
		wasmtime_extern_delete(&item);
	}
}

void WclapThread::initEntry() {
	uint64_t funcIndex;
	if (wclap.wasm64) {
		wclap.errorMessage = "64-bit not supported";
		return;
	} else {
		auto entryP = uint32_t(clapEntryP64);
		auto wasmEntry = wclap.view<wclap32::wclap_plugin_entry>(entryP);
		auto initFn = wasmEntry.init();
		auto success = callWasm_IS(initFn, "/plugin/");
		if (trap) {
			wclap.errorMessage = "clap_entry.init() threw (trapped)";
			return;
		}
		if (!success) {
			wclap.errorMessage = "clap_entry.init() returned false";
			return;
		}
	}
}

uint64_t WclapThread::wasmMalloc(size_t bytes) {
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

void WclapThread::callWasmFnP32(wclap32::WasmP fnP, wasmtime_val_raw *argsAndResults, size_t argN) {
	wasmtime_val_t funcVal;
	if (!wasmtime_table_get(context, &functionTable, fnP, &funcVal)) {
		wclap.errorMessage = "function pointer doesn't resolve";
		return;
	}
	if (funcVal.kind != WASMTIME_FUNCREF) {
		wclap.errorMessage = "function pointer didn't resolve to a function";
		return;
	}

	auto pos = translationScope32->wasmArenaPos;
	error = wasmtime_func_call_unchecked(context, &funcVal.of.funcref, argsAndResults, 1, &trap);
	if (trap) wclap.errorMessage = "function call threw (trapped)";
	if (error) wclap.errorMessage = "calling function failed";
	translationScope32->wasmArenaPos = pos;
}

} // namespace
