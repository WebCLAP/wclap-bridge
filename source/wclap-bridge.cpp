#include <iostream>
#define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;

#import "./wclap-bridge.h"
#include "./wasi-sandbox.h"

#import "clap/all.h"
#import "wasm.h"

#include <fstream>
#include <vector>

static wasm_engine_t *global_wasm_engine = nullptr;
std::string wclap_error_message_string;
static const char *wclap_error_message = nullptr;

struct Wclap {
	wasm_store_t *store;
	
	wasm_module_t *module = nullptr;
	wasm_shared_module_t *shared = nullptr;
	
	WasiSandbox wasi;
	
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
			
	wasm_extern_t *getImport(wasm_importtype_t *type) {
		auto *module = wasm_importtype_module(type);
		auto *name = wasm_importtype_name(type);
		
		auto *wasiImport = wasi.resolve(type);
		if (wasiImport) return wasiImport;
		
		std::cout << "\tUnresolved import: " << *module << ":" << *name << "\n";
		return nullptr;
	}
		
	static Wclap * open(const char *path) {
		std::ifstream wasmFile{path, std::ios::binary};
		if (!wasmFile) {
			wclap_error_message = "Couldn't open WASM file";
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

		auto *wclap = new Wclap(path, store);

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

		// Assemble imports
		wasm_importtype_vec_t importTypes;
		wasm_module_imports(wclap->module, &importTypes);
		wasm_extern_vec_t imports;
		wasm_extern_vec_new_uninitialized(&imports, importTypes.size);

		for (size_t i = 0; i < importTypes.size; ++i) {
			wasm_importtype_t *type = importTypes.data[i];
			imports.data[i] = wclap->getImport(type);
			if (!imports.data[i]) {
				wclap_error_message_string = "unrecognised import #" + std::to_string(i);
				wclap_error_message = wclap_error_message_string.c_str();
				wasm_importtype_vec_delete(&importTypes);
				wasm_extern_vec_delete(&imports);
				delete wclap;
				return nullptr;
			}
		}
		wasm_importtype_vec_delete(&importTypes);
		wasm_extern_vec_delete(&imports);

		// TODO: find clap_entry and call .init()

		return wclap;
	}
	
	~Wclap() {
		if (shared) wasm_shared_module_delete(shared);
		if (module) wasm_module_delete(module);
		wasm_store_delete(store);
	}
	
	const void * getFactory(const char *factory_id) {
		LOG_EXPR(factory_id);
		return nullptr;
	}
	
private:
	Wclap(const char *path, wasm_store_t *store) : store(store), wasi(store) {
		wasi.fileRoot(path);
	}
};

/*---------- WCLAP bridge API ----------*/

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

void * wclap_open(const char *path) {
	if (!global_wasm_engine) {
		wclap_error_message = "No WASM engine - did you call wclap_global_init()?";
		return nullptr;
	}
	return Wclap::open(path);
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

