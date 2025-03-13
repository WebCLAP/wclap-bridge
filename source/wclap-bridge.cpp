#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "./wclap-bridge-impl.h"

#include "wclap-bridge.h"

static std::string ensureTrailingSlash(const char *dirC) {
	std::string dir = dirC;
	if (dir.size() && dir.back() != '/') dir += "/";
	return dir;
}

bool wclap_global_init() {
	wasm_config_t *config = wasm_config_new();
	if (!config) {
		wclap_error_message = "couldn't create config";
		return false;
	}

	// TODO: enable epoch_interruption to prevent locks
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

	auto *wclap = new Wclap(wclapDir ? wclapDir : "", presetDir ? presetDir : "", cacheDir ? cacheDir : "", varDir ? varDir : "", true);

	wclap_error_message = wclap->setupWasmBytes((uint8_t *)wasmBytes.data(), wasmBytes.size());
	if (wclap_error_message) {
		delete wclap;
		return nullptr;
	}

	wclap_error_message = wclap->findExports();
	if (wclap_error_message) {
		delete wclap;
		return nullptr;
	}

	return wclap;
}
void * wclap_open(const char *wclapDir) {
	return wclap_open_with_dirs(wclapDir, nullptr, nullptr, nullptr);
}
bool wclap_close(void *wclap) {
	if (!wclap) {
		wclap_error_message = "null pointer";
		return false;
	}
	delete (Wclap *)wclap;
	return true;
}
const clap_version_t * wclap_version(void *wclap) {
	if (!wclap) {
		wclap_error_message = "null pointer";
		return nullptr;
	}
	return &((Wclap *)wclap)->clapVersion;
}
const void * wclap_get_factory(void *wclap, const char *factory_id) {
	if (!wclap) {
		wclap_error_message = "null pointer";
		return nullptr;
	}
	return ((Wclap *)wclap)->getFactory(factory_id);
}

