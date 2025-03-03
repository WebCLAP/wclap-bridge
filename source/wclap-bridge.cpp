#include <iostream>
#define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;

#include "wclap-translation-scope.h"

#include "wasi.h"

#include <fstream>
#include <vector>

static wasm_engine_t *global_wasm_engine = nullptr;
std::string wclap_error_message_string;
static const char *wclap_error_message = nullptr;

struct Wclap;

struct Wclap {
	// Always defined
	wasm_store_t *store;
	
	// Maybe defined (but not if we bail halfway through construction) and we should delete
	wasm_module_t *module = nullptr;
	wasm_shared_module_t *shared = nullptr;
	wasm_instance_t *instance = nullptr;
	wasi_config_t *wasiConfig = nullptr;
	wasi_instance_t *wasiInstance = nullptr;

	// Maybe defined, but not our job to delete it
	wasm_memory_t *memory = nullptr;
	wasm_table_t *functionTable = nullptr;
	uint32_t clapEntryP = 0; // WASM pointer to clap_entry
	wasm_func_t *malloc = nullptr;
	
	static char typeCode(wasm_valkind_t k) {
		if (k < 4) {
			return "iIfF"[k];
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
	
	static Wclap * open(const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir, bool mustLinkDirs) {
		std::ifstream wasmFile{ensureTrailingSlash(wclapDir) + "module.wasm", std::ios::binary};
		if (!wasmFile) {
			wclap_error_message = "Couldn't open plugin/module.wasm file";
			return nullptr;
		}
		std::vector<char> wasmBytes{std::istreambuf_iterator<char>{wasmFile}, {}};
		if (!wasmBytes.size()) {
			wclap_error_message = "Couldn't read WASM file";
			return nullptr;
		}

		auto *store = wasm_store_new(global_wasm_engine);
		if (!store) {
			wclap_error_message = "Failed to create wasm_store";
			return nullptr;
		}

		wasm_byte_vec_t bytes;
		bytes.size = wasmBytes.size();
		bytes.data = wasmBytes.data();
		if (!wasm_module_validate(store, &bytes)) {
			wclap_error_message = "invalid WASM binary";
			return nullptr;
		}

		auto *wclap = new Wclap(store);

		wclap->module = wasm_module_new(store, &bytes);
		if (!wclap->module) {
			wclap_error_message = "Failed to compile module";
			delete wclap;
			return nullptr;
		}

		wclap->shared = wasm_module_share(wclap->module);
		if (!wclap->shared) {
			wclap_error_message = "Failed to create sharable (multi-thread) module";
			delete wclap;
			return nullptr;
		}
		
		wclap->wasiConfig = wasi_config_new();
		if (!wclap->wasiConfig) {
			wclap_error_message = "Failed to create WASI config";
			delete wclap;
			return nullptr;
		}
		// Link various directories - failure is a dealbreaker if mustLinkDirs is set
		if (!wasi_config_preopen_dir(wclap->wasiConfig, wclapDir, "/plugin/", WASI_DIR_PERMS_READ, WASI_FILE_PERMS_READ)) {
			wclap_error_message = "Failed to open /plugin/ in WASI config";
			if (mustLinkDirs) {
				delete wclap;
				return nullptr;
			} else {
				std::cerr << wclap_error_message << std::endl;
			}
		}
		if (presetDir) {
			if (!wasi_config_preopen_dir(wclap->wasiConfig, presetDir, "/presets/", WASI_DIR_PERMS_READ|WASI_DIR_PERMS_WRITE, WASI_FILE_PERMS_READ|WASI_FILE_PERMS_WRITE)) {
				wclap_error_message = "Failed to open /presets/ in WASI config";
				if (mustLinkDirs) {
					delete wclap;
					return nullptr;
				} else {
					std::cerr << wclap_error_message << std::endl;
				}
			}
		}
		if (cacheDir) {
			if (!wasi_config_preopen_dir(wclap->wasiConfig, cacheDir, "/cache/", WASI_DIR_PERMS_READ|WASI_DIR_PERMS_WRITE, WASI_FILE_PERMS_READ|WASI_FILE_PERMS_WRITE)) {
				wclap_error_message = "Failed to open /cache/ in WASI config";
				if (mustLinkDirs) {
					delete wclap;
					return nullptr;
				} else {
					std::cerr << wclap_error_message << std::endl;
				}
			}
		}
		if (varDir) {
			if (!wasi_config_preopen_dir(wclap->wasiConfig, varDir, "/var/", WASI_DIR_PERMS_READ|WASI_DIR_PERMS_WRITE, WASI_FILE_PERMS_READ|WASI_FILE_PERMS_WRITE)) {
				wclap_error_message = "Failed to open /var/ in WASI config";
				if (mustLinkDirs) {
					delete wclap;
					return nullptr;
				} else {
					std::cerr << wclap_error_message << std::endl;
				}
			}
		}
		
		wclap->wasiInstance = wasi_instance_new(wclap->wasiConfig, wclap->store);
		if (!wclap->wasiInstance) {
			wclap_error_message = "Failed to create WASI instance";
			delete wclap;
			return nullptr;
		}

		// Assemble imports
		wasm_importtype_vec_t importTypes;
		wasm_module_imports(wclap->module, &importTypes);
		wasm_extern_vec_t imports;
		wasm_extern_vec_new_uninitialized(&imports, importTypes.size);

		for (size_t i = 0; i < importTypes.size; ++i) {
			wasm_importtype_t *type = importTypes.data[i];
			imports.data[i] = wasi_instance_resolve(wclap->wasiInstance, type);
			if (!imports.data[i]) {
				wclap_error_message_string = "unresolved import #" + std::to_string(i) + ": ";
				auto *moduleName = wasm_importtype_module(type);
				auto *name = wasm_importtype_name(type);
				for (size_t i = 0; i < moduleName->size; ++i) {
					wclap_error_message_string += moduleName->data[i];
				};
				wclap_error_message_string += " / ";
				for (size_t i = 0; i < name->size; ++i) {
					wclap_error_message_string += name->data[i];
				};
				wclap_error_message = wclap_error_message_string.c_str();

				wasm_importtype_vec_delete(&importTypes);
				wasm_extern_vec_delete(&imports);
				delete wclap;
				return nullptr;
			}
		}
		wasm_trap_t *trap = nullptr;
		wclap->instance = wasm_instance_new(wclap->store, wclap->module, &imports, &trap);
		wasm_importtype_vec_delete(&importTypes);
		wasm_extern_vec_delete(&imports);

		if (trap) { // this calls `start()`, which could throw
			// get the error message
			wasm_message_t message;
			wasm_trap_message(trap, &message);
			wclap_error_message_string = "failed to create instance: ";
			wclap_error_message_string.append(message.data, message.data + message.size);
			wclap_error_message = wclap_error_message_string.c_str();
			wasm_byte_vec_delete(&message);

			wasm_trap_delete(trap);
			delete wclap;
			return nullptr;
		} else if (!wclap->instance) {
			wclap_error_message = "Failed to create instance";
			delete wclap;
			return nullptr;
		}

		wasm_exporttype_vec_t exportTypes;
		wasm_module_exports(wclap->module, &exportTypes);
		wasm_extern_vec_t exports;
		wasm_instance_exports(wclap->instance, &exports);
		for (size_t i = 0; i < exports.size; ++i) {
			const wasm_name_t *name = wasm_exporttype_name(exportTypes.data[i]);
			wasm_func_t *func = wasm_extern_as_func(exports.data[i]);
			wasm_global_t *global = wasm_extern_as_global(exports.data[i]);
			wasm_memory_t *memory = wasm_extern_as_memory(exports.data[i]);
			wasm_table_t *table = wasm_extern_as_table(exports.data[i]);
			if (func) {
				wasm_functype_t *type = wasm_func_type(func);
				const wasm_valtype_vec_t *params = wasm_functype_params(type);
				const wasm_valtype_vec_t *results = wasm_functype_results(type);
				if (nameEquals(name, "malloc")) {
					if (params->size == 1 && wasm_valtype_kind(params->data[0]) == WASM_I32 && results->size == 1 && wasm_valtype_kind(results->data[0]) == WASM_I32) {
						wclap->malloc = func;
					}
				} else if (nameEquals(name, "_initialize") || nameEquals(name, "_start")) { // WASI init methods
					if (params->size == 0 && results->size == 0) {
						wasm_val_vec_t args, results;
						args.size = 0;
						results.size = 0;
						wasm_trap_t *trap = wasm_func_call(func, &args, &results);
						if (trap) {
							wasm_message_t message;
							wasm_trap_message(trap, &message);
							wclap_error_message_string = "calling _start()/_initialize() failed: ";
							wclap_error_message_string.append(message.data, message.data + message.size);
							wclap_error_message = wclap_error_message_string.c_str();
							wasm_byte_vec_delete(&message);

							wasm_exporttype_vec_delete(&exportTypes);
							wasm_extern_vec_delete(&exports);
							delete wclap;
							return nullptr;
						}
					}
				}
				wasm_functype_delete(type);
			} else if (global) {
				if (nameEquals(name, "clap_entry")) {
					wasm_val_t val;
					wasm_global_get(global, &val);
					if (val.kind == WASM_I32) {
						wclap->clapEntryP = (uint32_t)val.of.i32;
					}
				}
			} else if (memory) {
				if (!wclap->memory) {
					wclap->memory = memory;
					wasi_instance_link_memory(wclap->wasiInstance, memory);
				}
			} else if (table) {
				wasm_tabletype_t *type = wasm_table_type(table);
				const wasm_limits_t limits = *wasm_tabletype_limits(type);
				auto elementKind = wasm_valtype_kind(wasm_tabletype_element(type));
				wasm_tabletype_delete(type);
				
				if (elementKind == WASM_FUNCREF) {
					if (limits.max < 65536 || limits.max - 65536 < limits.min) {
						wasm_exporttype_vec_delete(&exportTypes);
						wasm_extern_vec_delete(&exports);
						wclap_error_message = "exported function table is can't grow sufficiently";
						delete wclap;
						return nullptr;
					}
					if (!wclap->functionTable) wclap->functionTable = table;
				}
			}
		}
		wasm_exporttype_vec_delete(&exportTypes);
		wasm_extern_vec_delete(&exports);

		// TODO: find clap_entry and call .init(), with "/plugin/" as plugin location

		return wclap;
	}
	
	~Wclap() {
		if (wasiInstance) wasi_instance_delete(wasiInstance);
		if (wasiConfig) wasi_config_delete(wasiConfig);
		if (instance) wasm_instance_delete(instance);
		if (shared) wasm_shared_module_delete(shared);
		if (module) wasm_module_delete(module);
		wasm_store_delete(store);
	}
	
	const void * getFactory(const char *factory_id) {
		LOG_EXPR(factory_id);
		return nullptr;
	}

private:
	Wclap(wasm_store_t *store) : store(store) {}

	static bool nameEquals(const wasm_name_t *name, const char *cName) {
		if (name->size != std::strlen(cName)) return false;
		for (size_t i = 0; i < name->size; ++i) {
			if (name->data[i] != cName[i]) return false;
		}
		return true;
	}
	
	static std::string ensureTrailingSlash(const char *dirC) {
		std::string dir = dirC;
		if (dir.size() && dir.back() != '/') dir += "/";
		return dir;
	}
};

/*---------- WCLAP bridge C API ----------*/

#import "./wclap-bridge.h"

bool wclap_global_init() {
	wasm_config_t *config = wasm_config_new();
	if (!config) {
		wclap_error_message = "couldn't create config";
		return false;
	}

	global_wasm_engine = wasm_engine_new_with_config(config);
	if (!global_wasm_engine) {
		wclap_error_message = "couldn't create engine";
		return false;
	}
	return true;
}
void wclap_global_deinit() {
	if (global_wasm_engine) {
		wasm_engine_delete(global_wasm_engine);
		global_wasm_engine = nullptr;
	}
}

const char * wclap_error() {
	auto *message = wclap_error_message;
	wclap_error_message = nullptr;
	return message;
}

void * wclap_open_with_dirs(const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir) {
	if (!global_wasm_engine) {
		wclap_error_message = "No WASM engine - did you call wclap_global_init()?";
		return nullptr;
	}
	return Wclap::open(wclapDir, presetDir, cacheDir, varDir, true);
}
void * wclap_open(const char *wclapDir) {
	if (!global_wasm_engine) {
		wclap_error_message = "No WASM engine - did you call wclap_global_init()?";
		return nullptr;
	}
	return Wclap::open(wclapDir, nullptr, nullptr, nullptr, false);
}
bool wclap_close(void *wclap) {
	if (!wclap) {
		wclap_error_message = "null pointer";
		return false;
	}
	delete (Wclap *)wclap;
	return true;
}
const void * wclap_get_factory(void *wclap, const char *factory_id) {
	if (!wclap) {
		wclap_error_message = "null pointer";
		return nullptr;
	}
	return ((Wclap *)wclap)->getFactory(factory_id);
}

