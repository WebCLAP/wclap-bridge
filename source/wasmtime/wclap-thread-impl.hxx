#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "../wclap-thread.h"
#include "../wclap.h"
#include "../wclap-arenas.h"
#include "../wclap32/wclap-translation.h"
#include "../wclap64/wclap-translation.h"

#include "./wclap-impl-wasmtime.h"

#include <cstdlib>

namespace wclap {

extern std::atomic<wasm_engine_t *> global_wasm_engine;
extern unsigned int timeLimitEpochs;

namespace _impl {
	void callWithThread(WclapThread &thread, uint64_t fnP, wasmtime_val_raw *argsAndResults, size_t argN) {
		return thread.impl->callWasmFnP(thread.wclap, fnP, argsAndResults, argN);
	}
	wasmtime_context_t * contextForThread(WclapThread &thread) {
		return thread.impl->context;
	}
	void registerFunctionIndex(WclapThread &thread, wasmtime_val_t fnVal, uint32_t &fnP) {
		thread.impl->registerFunctionIndex(thread.wclap, fnVal, fnP);
	}
	void registerFunctionIndex(WclapThread &thread, wasmtime_val_t fnVal, uint64_t &fnP) {
		thread.impl->registerFunctionIndex(thread.wclap, fnVal, fnP);
	}
}

void WclapThread::Impl::setWasmDeadline() {
	if (timeLimitEpochs) wasmtime_context_set_epoch_deadline(context, timeLimitEpochs);
}

void WclapThread::startInstance() {
	if (wclap.errorMessage) return;

	impl->store = wasmtime_store_new(global_wasm_engine, nullptr, nullptr);
	if (!impl->store) return wclap.setError("Failed to create store");

	impl->context = wasmtime_store_context(impl->store);
	if (!impl->context) return wclap.setError("Failed to get context");
	
	// Create a linker with WASI functions defined
	impl->linker = wasmtime_linker_new(global_wasm_engine);
	if (!impl->linker) return wclap.setError("error creating linker");

	impl->error = wasmtime_linker_define_wasi(impl->linker);
	if (impl->error) return wclap.setError("error linking WASI");

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

	impl->error = wasmtime_context_set_wasi(impl->context, wasiConfig);
	if (impl->error) {
		logError(impl->error);
		wasi_config_delete(wasiConfig);
		return wclap.setError("Failed to configure WASI");
	}
	wasiConfig = nullptr; // owned by the context now

	//---------- Start the instance ----------//

	// This doesn't call the WASI _start() or _initialize() methods
	impl->error = wasmtime_linker_instantiate(impl->linker, impl->context, wclap.impl->module, &impl->instance, &impl->trap);
	if (impl->error) return wclap.setError("failed to create instance");
	if (impl->trap) return wclap.setError("failed to start instance");

	//---------- Find exports ----------//

	char *name;
	size_t nameSize;
	wasmtime_extern_t item;
	
	if (wasmtime_instance_export_get(impl->context, &impl->instance, "memory", 6, &item)) {
		if (item.kind == WASMTIME_EXTERN_MEMORY) {
			impl->memory = item.of.memory;
		} else if (item.kind == WASMTIME_EXTERN_SHAREDMEMORY) {
			// TODO: it should be the same as the import - not sure how to check this
			if (!wclap.impl->sharedMemory) {
				return wclap.setError("exported shared memory, but didn't import it");
			}
		} else {
			wasmtime_extern_delete(&item);
			return wclap.setError("exported memory isn't a (Shared)Memory");
		}
		wasmtime_extern_delete(&item);
	} else if (!wclap.impl->sharedMemory) {
		return wclap.setError("must either export memory or import shared memory");
	}

	if (!wasmtime_instance_export_get(impl->context, &impl->instance, "clap_entry", 10, &item)) {
		return wclap.setError("clap_entry not exported");
	}
	if (item.kind == WASMTIME_EXTERN_GLOBAL) {
		wasmtime_val_t v;
		wasmtime_global_get(impl->context, &item.of.global, &v);
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

	if (!wasmtime_instance_export_get(impl->context, &impl->instance, "malloc", 6, &item)) {
		return wclap.setError("malloc not exported");
	}
	if (item.kind == WASMTIME_EXTERN_FUNC) {
		wasm_functype_t *type = wasmtime_func_type(impl->context, &item.of.func);
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
		impl->mallocFunc = item.of.func;
		wasm_functype_delete(type);
	} else {
		wasmtime_extern_delete(&item);
		return wclap.setError("malloc isn't a Function");
	}
	wasmtime_extern_delete(&item);

	// Look for the first function table
	size_t exportIndex = 0;
	while (wasmtime_instance_export_nth(impl->context, &impl->instance, exportIndex, &name, &nameSize, &item)) {
		if (item.kind == WASMTIME_EXTERN_TABLE) {
			wasm_tabletype_t *type = wasmtime_table_type(impl->context, &item.of.table);
			const wasm_limits_t limits = *wasm_tabletype_limits(type);
			auto elementKind = wasm_valtype_kind(wasm_tabletype_element(type));
			wasm_tabletype_delete(type);

			if (elementKind == WASM_FUNCREF) {
				if (limits.max < 65536 || limits.max - 65536 < limits.min) {
					return wclap.setError("exported function table can't grow enough for CLAP host functions");
				}
				impl->functionTable = item.of.table;
				break;
			}
		}
		wasmtime_extern_delete(&item);
		++exportIndex;
	}
}

void WclapThread::implCreate() {
	impl = new Impl();
}

void WclapThread::implDestroy() {
	if (impl->trap) {
		wclap_error_message_string = (wclap.errorMessage ? wclap.errorMessage : "[unknown trap]");
		wclap_error_message_string += ": ";
		wasm_message_t message;
		wasm_trap_message(impl->trap, &message);
		wclap_error_message_string += message.data; // should always be null-terminated C string
		wclap_error_message = wclap_error_message_string.c_str();

		wasm_trap_delete(impl->trap);
	}

	if (impl->error) {
		wclap_error_message_string = (wclap.errorMessage ? wclap.errorMessage : "[unknown error]");
		wclap_error_message_string += ": ";
		wasm_name_t message;
		wasmtime_error_message(impl->error, &message);
		wclap_error_message_string.append(message.data, message.size);
		wclap_error_message = wclap_error_message_string.c_str();

		wasmtime_error_delete(impl->error);
	}
	if (impl->linker) wasmtime_linker_delete(impl->linker);
	if (impl->store) wasmtime_store_delete(impl->store);
}

void WclapThread::wasmInit() {
	wasmtime_extern_t item;

	// Call the WASI entry-point `_initialize()` if it exists - WCLAPs don't *have* to use WASI, so it's fine not to
	if (wasmtime_instance_export_get(impl->context, &impl->instance, "_initialize", 11, &item)) {
		if (item.kind != WASMTIME_EXTERN_FUNC) {
			wasmtime_extern_delete(&item);
			return wclap.setError("_initialize isn't a function");
		}
		wasm_functype_t *type = wasmtime_func_type(impl->context, &item.of.func);
		const wasm_valtype_vec_t *params = wasm_functype_params(type);
		const wasm_valtype_vec_t *results = wasm_functype_results(type);
		if (params->size != 0 || results->size != 0) {
			wasmtime_extern_delete(&item);
			return wclap.setError("_initialize() function signature mismatch");
		}
		wasm_functype_delete(type);

		impl->setWasmDeadline();
		impl->error = wasmtime_func_call(impl->context, &item.of.func, nullptr, 0, nullptr, 0, &impl->trap);
		if (impl->error) {
			wasmtime_extern_delete(&item);
			logError(impl->error);
			return wclap.setError("error calling _initialize()");
		} else if (impl->trap) {
			wasmtime_extern_delete(&item);
			logTrap(impl->trap);
			return wclap.setError(trapIsTimeout(impl->trap) ? "_initialize() timeout" : "_initialize() threw (trapped)");
		}
		wasmtime_extern_delete(&item);
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
	
	impl->setWasmDeadline();
	impl->error = wasmtime_func_call(impl->context, &impl->mallocFunc, args, 1, results, 1, &impl->trap);
	if (impl->error) {
		logError(impl->error);
		wclap.setError("calling malloc() failed");
		return 0;
	}
	if (impl->trap) {
		logTrap(impl->trap);
		wclap.setError(trapIsTimeout(impl->trap) ? "malloc() timeout" : "malloc() threw (trapped)");
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

} // namespace
