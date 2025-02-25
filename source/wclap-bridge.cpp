#import "wclap-bridge.h"

#include <iostream>
#define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;

#import "clap/all.h"
#import "wasm.h"

#include <fstream>
#include <vector>

static wasm_engine_t *global_wasm_engine = nullptr;
static const char *wclap_error_message = nullptr;

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

std::ostream& operator<<(std::ostream& os, const wasm_name_t &name) {
	for (size_t i = 0; i < name.size; ++i) {
		os << name.data[i];
	}
	return os;
}

struct Wclap {
	wasm_store_t *store = nullptr;
	wasm_module_t *module = nullptr;
	wasm_shared_module_t *shared = nullptr;
	
	std::string path;
	
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
		
	static Wclap * open(const char *path) {
		std::ifstream wasmFile{path, std::ios::binary};
		if (!wasmFile) {
			wclap_error_message = "Couldn't read WASM file";
			return nullptr;
		}
		std::vector<char> wasmBytes{std::istreambuf_iterator<char>{wasmFile}, {}};
		
		auto *wclap = new Wclap(path);
		wclap->store = wasm_store_new(global_wasm_engine);
		if (!wclap->store) {
			wclap_error_message = "Failed to create wasm_store";
			delete wclap;
			return nullptr;
		}
		LOG_EXPR(wasmBytes.size());
		
		wasm_byte_vec_t bytes;
		bytes.size = wasmBytes.size();
		bytes.data = wasmBytes.data();
		if (!wasm_module_validate(wclap->store, &bytes)) {
			wclap_error_message = "invalid WASM binary";
			delete wclap;
			return nullptr;
		}

		wclap->module = wasm_module_new(wclap->store, &bytes);
		if (!wclap->module) {
			wclap_error_message = "Failed to compile module";
			delete wclap;
			return nullptr;
		}
		
		wclap->shared = wasm_module_share(wclap->module);
		if (!wclap->shared) {
			wclap_error_message = "Failed to create sharable module";
			delete wclap;
			return nullptr;
		}
		
		wasm_exporttype_vec_t exports;
		wasm_module_exports(wclap->module, &exports);
		std::cout << "exports:\n";
		for (size_t i = 0; i < exports.size; ++i) {
			auto *item = exports.data[i];
			auto *name = wasm_exporttype_name(item);
			std::cout << "\t" << *name << "\n";

			auto *externType = wasm_exporttype_type(item);
			if (wasm_externtype_as_functype_const(externType)) {
				std::cout << "\t\tfunction: ";
				auto *results = wasm_functype_results(wasm_externtype_as_functype_const(externType));
				for (size_t i = 0; i < results->size; ++i) std::cout << typeCode(results->data[i]);
				std::cout << "(";
				auto *params = wasm_functype_params(wasm_externtype_as_functype_const(externType));
				for (size_t i = 0; i < params->size; ++i) std::cout << typeCode(params->data[i]);
				std::cout << ")\n";
			} else if (wasm_externtype_as_globaltype_const(externType)) {
				std::cout << "\t\tglobal: " << typeCode(wasm_globaltype_content(wasm_externtype_as_globaltype_const(externType))) << "\n";
			} else if (wasm_externtype_as_memorytype_const(externType)) {
				auto limits = wasm_memorytype_limits(wasm_externtype_as_memorytype_const(externType));
				std::cout << "\t\tmemory (" << limits->min << "-" << limits->max << ")\n";
			} else if (wasm_externtype_as_tabletype_const(externType)) {
				std::cout << "\t\ttable [" << typeCode(wasm_tabletype_element(wasm_externtype_as_tabletype_const(externType))) << "]\n";
			}
		}
		wasm_exporttype_vec_delete(&exports);

		wasm_importtype_vec_t imports;
		wasm_module_imports(wclap->module, &imports);
		std::cout << "imports:\n";
		for (size_t i = 0; i < imports.size; ++i) {
			auto *item = imports.data[i];
			auto *module = wasm_importtype_module(item);
			auto *name = wasm_importtype_name(item);
			std::cout << "\t" << *module << ":" << *name << "\n";
		}
		wasm_importtype_vec_delete(&imports);

		// TODO: find clap_entry and call .init()

		return wclap;
	}
	
	~Wclap() {
		if (shared) wasm_shared_module_delete(shared);
		if (module) wasm_module_delete(module);
		if (store) wasm_store_delete(store);
	}
	
	const void * getFactory(const char *factory_id) {
		LOG_EXPR(factory_id);
		return nullptr;
	}
	
private:
	Wclap(const char *path) : path(path) {}
};

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

