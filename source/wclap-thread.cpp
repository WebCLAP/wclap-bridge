#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "./wclap-thread.h"
#include "./wclap.h"
#include "./wclap-arenas.h"
#include "./wclap32/wclap-translation.h"
#include "./wclap64/wclap-translation.h"

#include <cstdlib>

namespace wclap {

WclapThread::WclapThread(Wclap &wclap) : wclap(wclap) {
	startInstance();
}

void WclapThread::startInstance() {
	if (wclap.errorMessage) return;

	store = wasmtime_store_new(global_wasm_engine, nullptr, nullptr);
	if (!store) return wclap.setError("Failed to create store");

	context = wasmtime_store_context(store);
	if (!context) return wclap.setError("Failed to create context");
	
	// Create a linker with WASI functions defined
	linker = wasmtime_linker_new(global_wasm_engine);
	if (!linker) return wclap.setError("error creating linker");

	error = wasmtime_linker_define_wasi(linker);
	if (error) return wclap.setError("error linking WASI");

	//---------- WASI config ----------//

	wasi_config_t *wasiConfig = wasi_config_new();
	if (!wasiConfig) return wclap.setError("Failed to create WASI config");
	// Everything after this point needs to delete `wasiConfig` if it fails
	
	wasi_config_inherit_stdout(wasiConfig);
	wasi_config_inherit_stderr(wasiConfig);
	
	// Set a few specific environment variables
	std::vector<const char *> envNames, envValues;
	auto addEnv = [&](const char *name) {
		auto *value = std::getenv(name);
		if (value) {
			envNames.push_back(name);
			envValues.push_back(value);
		}
	};
	addEnv("TERM");
	addEnv("LANG");
	if (envNames.size()) {
		wasi_config_set_env(wasiConfig, envNames.size(), envNames.data(), envValues.data());
	}
	
	// Link various directories - failure is allowed if `mustLinkDirs` is false
	if (wclap.wclapDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.wclapDir.c_str(), "/plugin/", WASMTIME_WASI_DIR_PERMS_READ, WASMTIME_WASI_FILE_PERMS_READ)) {
			if (wclap.mustLinkDirs) {
				wasi_config_delete(wasiConfig);
				return wclap.setError("Failed to open /plugin/ in WASI config");
			}
		}
	}
	if (wclap.presetDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.presetDir.c_str(), "/presets/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			if (wclap.mustLinkDirs) {
				wasi_config_delete(wasiConfig);
				return wclap.setError("Failed to open /presets/ in WASI config");
			}
		}
	}
	if (wclap.cacheDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.cacheDir.c_str(), "/cache/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			if (wclap.mustLinkDirs) {
				wasi_config_delete(wasiConfig);
				return wclap.setError("Failed to open /cache/ in WASI config");
			}
		}
	}
	if (wclap.varDir.size()) {
		if (!wasi_config_preopen_dir(wasiConfig, wclap.varDir.c_str(), "/var/", WASMTIME_WASI_DIR_PERMS_READ|WASMTIME_WASI_DIR_PERMS_WRITE, WASMTIME_WASI_FILE_PERMS_READ|WASMTIME_WASI_FILE_PERMS_WRITE)) {
			if (wclap.mustLinkDirs) {
				wasi_config_delete(wasiConfig);
				return wclap.setError("Failed to open /var/ in WASI config");
			}
		}
	}

	error = wasmtime_context_set_wasi(context, wasiConfig);
	if (error) {
		wasi_config_delete(wasiConfig);
		return wclap.setError("Failed to configure WASI");
	}
	wasiConfig = nullptr;

	//---------- Start the instance ----------//

	// This doesn't call the WASI _start() or _initialize() methods
	error = wasmtime_linker_instantiate(linker, context, wclap.module, &instance, &trap);
	if (error) return wclap.setError("failed to create instance");
	if (trap) return wclap.setError("failed to start instance");

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
				return wclap.setError("exported shared memory, but didn't import it");
			}
		} else {
			wasmtime_extern_delete(&item);
			return wclap.setError("exported memory isn't a (Shared)Memory");
		}
		wasmtime_extern_delete(&item);
	} else if (!wclap.sharedMemory) {
		return wclap.setError("must either export memory or import shared memory");
	}

	if (!wasmtime_instance_export_get(context, &instance, "clap_entry", 10, &item)) {
		return wclap.setError("clap_entry not exported");
	}
	if (item.kind == WASMTIME_EXTERN_GLOBAL) {
		wasmtime_val_t v;
		wasmtime_global_get(context, &item.of.global, &v);
		if (v.kind == WASM_I32 && !wclap.wasm64) {
			clapEntryP64 = v.of.i32; // We store it as 64 bits, even though we know it's a 32-bit one
		} else if (v.kind == WASM_I64 && wclap.wasm64) {
			clapEntryP64 = v.of.i64;
		} else {
			return wclap.setError("clap_entry is not a (correctly-sized) pointer");
		}
	} else {
		wasmtime_extern_delete(&item);
		return wclap.setError("clap_entry isn't a Global");
	}
	wasmtime_extern_delete(&item);

	if (!wasmtime_instance_export_get(context, &instance, "malloc", 6, &item)) {
		return wclap.setError("malloc not exported");
	}
	if (item.kind == WASMTIME_EXTERN_FUNC) {
		wasm_functype_t *type = wasmtime_func_type(context, &item.of.func);
		const wasm_valtype_vec_t *params = wasm_functype_params(type);
		const wasm_valtype_vec_t *results = wasm_functype_results(type);
		if (params->size != 1 || results->size != 1) {
			wasmtime_extern_delete(&item);
			return wclap.setError("malloc() function signature mismatch");
		}
		if (wasm_valtype_kind(params->data[0]) != wasm_valtype_kind(results->data[0])) {
			wasmtime_extern_delete(&item);
			return wclap.setError("malloc() function signature mismatch");
		}
		if (wclap.wasm64) {
			if (wasm_valtype_kind(params->data[0]) != WASMTIME_I64) {
				wasmtime_extern_delete(&item);
				return wclap.setError("malloc() function signature mismatch");
			}
		} else {
			if (wasm_valtype_kind(params->data[0]) != WASMTIME_I32) {
				wasmtime_extern_delete(&item);
				return wclap.setError("malloc() function signature mismatch");
			}
		}
		mallocFunc = item.of.func;
		wasm_functype_delete(type);
	} else {
		wasmtime_extern_delete(&item);
		return wclap.setError("malloc isn't a Function");
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
					return wclap.setError("exported function table can't grow enough for CLAP host functions");
				}
				functionTable = item.of.table;
				break;
			}
		}
		wasmtime_extern_delete(&item);
		++exportIndex;
	}
}

WclapThread::~WclapThread() {
	if (trap) {
		wclap_error_message_string = (wclap.errorMessage ? wclap.errorMessage : "[unknown trap]");
		wclap_error_message_string += ": ";
		wasm_message_t message;
		wasm_trap_message(trap, &message);
		wclap_error_message_string += message.data; // should always be null-terminated C string
		wclap_error_message = wclap_error_message_string.c_str();
	}

	if (error) {
		wclap_error_message_string = (wclap.errorMessage ? wclap.errorMessage : "[unknown error]");
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
	
	setWasmDeadline();
	error = wasmtime_func_call(context, &mallocFunc, args, 1, results, 1, &trap);
	if (error) {
		logError(error);
		wclap.setError("calling malloc() failed");
		return 0;
	}
	if (trap) {
		logTrap(trap);
		wclap.setError(trapIsTimeout(trap) ? "malloc() timeout" : "malloc() threw (trapped)");
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

void WclapThread::wasmInit() {
	wasmtime_extern_t item;

	// Call the WASI entry-point `_initialize()` if it exists - WCLAPs don't *have* to use WASI, so it's fine not to
	if (wasmtime_instance_export_get(context, &instance, "_initialize", 11, &item)) {
		if (item.kind != WASMTIME_EXTERN_FUNC) {
			wasmtime_extern_delete(&item);
			return wclap.setError("_initialize isn't a function");
		}
		wasm_functype_t *type = wasmtime_func_type(context, &item.of.func);
		const wasm_valtype_vec_t *params = wasm_functype_params(type);
		const wasm_valtype_vec_t *results = wasm_functype_results(type);
		if (params->size != 0 || results->size != 0) {
			wasmtime_extern_delete(&item);
			return wclap.setError("_initialize() function signature mismatch");
		}
		wasm_functype_delete(type);

		setWasmDeadline();
		error = wasmtime_func_call(context, &item.of.func, nullptr, 0, nullptr, 0, &trap);
		if (error) {
			wasmtime_extern_delete(&item);
			logError(error);
			return wclap.setError("error calling _initialize()");
		} else if (trap) {
			wasmtime_extern_delete(&item);
			logTrap(trap);
			return wclap.setError(trapIsTimeout(trap) ? "_initialize() timeout" : "_initialize() threw (trapped)");
		}
		wasmtime_extern_delete(&item);
	}
}

void WclapThread::callWasmFnP(uint64_t fnP, wasmtime_val_raw *argsAndResults, size_t argN) {
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

WclapThreadWithArenas::WclapThreadWithArenas(Wclap &wclap) : WclapThread(wclap) {
	arenas = wclap.claimArenas(this);
}

} // namespace
