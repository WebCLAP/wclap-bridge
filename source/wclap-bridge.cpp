#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "wclap-bridge.h"

#include "./wclap.h"

#include <atomic>

static std::string ensureTrailingSlash(const char *dirC) {
	std::string dir = dirC;
	if (dir.size() && dir.back() != '/') dir += "/";
	return dir;
}

namespace wclap {
	unsigned int timeLimitEpochs = 0;
	const char *wclap_error_message;
	std::string wclap_error_message_string;
}

const char * wclap_error() {
	auto *message = wclap::wclap_error_message;
	wclap::wclap_error_message = nullptr;
	return message;
}

void * wclap_open_with_dirs(const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir) {
	if (!wclap::global_config_ready.test()) {
		wclap::wclap_error_message = "WASM engine not configured - did wclap_global_init() succeed?";
		return nullptr;
	}
	if (!wclapDir) {
		wclap::wclap_error_message = "WCLAP path was null";
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
const clap_version * wclap_version(void *wclap) {
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

static const clap_version bridgeVersion{1, 2, 7};
const clap_version * wclap_bridge_version() {
	return &bridgeVersion;
}
