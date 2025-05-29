#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "wclap-bridge.h"

#include "./wclap.h"
#include "./validity.h"

#include <atomic>
#include <thread>

static std::string ensureTrailingSlash(const char *dirC) {
	std::string dir = dirC;
	if (dir.size() && dir.back() != '/') dir += "/";
	return dir;
}

static std::atomic_flag globalEpochRunning;
static void epochThreadFunction() {
	while (globalEpochRunning.test()) {
		wasmtime_engine_increment_epoch(wclap::global_wasm_engine);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}
static std::thread globalEpochThread;

bool wclap_global_init(unsigned int validityCheckLevel) {
	wclap::validity = {validityCheckLevel};

	wasm_config_t *config = wasm_config_new();
	if (!config) {
		wclap::wclap_error_message = "couldn't create config";
		return false;
	}

	if (wclap::validity.executionDeadlines) {
		// enable epoch_interruption to prevent locks - has a speed cost (10% according to docs)
		wasmtime_config_epoch_interruption_set(config, true);
	}
	
	wclap::global_wasm_engine = wasm_engine_new_with_config(config);
	if (!wclap::global_wasm_engine) {
		wclap::wclap_error_message = "couldn't create engine";
		return false;
	}

	if (wclap::validity.executionDeadlines) {
		globalEpochRunning.test_and_set();
		globalEpochThread = std::thread{epochThreadFunction};
	}

	return true;
}
void wclap_global_deinit() {
	if (globalEpochThread.joinable()) {
		globalEpochRunning.clear();
		globalEpochThread.join();
	}
	
	if (wclap::global_wasm_engine) {
		wasm_engine_delete(wclap::global_wasm_engine);
		wclap::global_wasm_engine = nullptr;
	}
}

const char * wclap_error() {
	auto *message = wclap::wclap_error_message;
	wclap::wclap_error_message = nullptr;
	return message;
}

void * wclap_open_with_dirs(const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir) {
	if (!wclap::global_wasm_engine) {
		wclap::wclap_error_message = "No WASM engine - did you call wclap_global_init()?";
		return nullptr;
	}

	std::ifstream wasmFile{ensureTrailingSlash(wclapDir) + "module.wasm", std::ios::binary};
	if (!wasmFile) {
		wasmFile = std::ifstream{wclapDir, std::ios::binary};
		if (wasmFile) wclapDir = nullptr; // if it's not a bundle, don't provide /plugin/
	}
	if (!wasmFile) {
		wclap::wclap_error_message = "Couldn't open ?.wclap/module.wasm or ?.wclap";
		return nullptr;
	}
	std::vector<char> wasmBytes{std::istreambuf_iterator<char>{wasmFile}, {}};
	if (!wasmBytes.size()) {
		wclap::wclap_error_message = "Couldn't read WASM file";
		return nullptr;
	}

	auto *wclap = new wclap::Wclap(wclapDir ? wclapDir : "", presetDir ? presetDir : "", cacheDir ? cacheDir : "", varDir ? varDir : "", true);

	wclap->initWasmBytes((uint8_t *)wasmBytes.data(), wasmBytes.size());
	if (wclap->errorMessage) {
		wclap::wclap_error_message_string = wclap->errorMessage;
		wclap::wclap_error_message = wclap::wclap_error_message_string.c_str();
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
		wclap::wclap_error_message = "null pointer";
		return false;
	}
	delete (wclap::Wclap *)wclap;
	return true;
}
const clap_version_t * wclap_version(void *wclap) {
	if (!wclap) {
		wclap::wclap_error_message = "null pointer";
		return nullptr;
	}
	return &((wclap::Wclap *)wclap)->clapVersion;
}
const void * wclap_get_factory(void *wclap, const char *factory_id) {
	if (!wclap) {
		wclap::wclap_error_message = "null pointer";
		return nullptr;
	}
	return ((wclap::Wclap *)wclap)->getFactory(factory_id);
}

