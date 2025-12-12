#include <iostream>
#ifndef LOG_EXPR
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "wclap-bridge.h"

#include "./instance.h"
#include "./wclap-module.h"

#include <mutex>
#include <fstream>

std::mutex globalInitMutex;
std::atomic<size_t> globalInitMs = 0;
std::atomic<size_t> activeWclapCount = 0;
std::atomic<bool> globalInitOK = false;

bool wclap_global_init(unsigned int timeLimitMs) {
	std::lock_guard<std::mutex> lock{globalInitMutex};
	auto ms = (size_t)timeLimitMs;
	if (globalInitOK) {
		if (ms == globalInitMs) return true;
		if (activeWclapCount > 0) {
			std::cerr << "Tried to reconfigure WCLAP bridge while WCLAPs are still active\n";
			abort();
		}
		instanceGlobalDeinit();
	}
	globalInitMs = ms;
	globalInitOK = instanceGlobalInit(ms);
	return globalInitOK;
}
void wclap_global_deinit() {
	std::lock_guard<std::mutex> lock{globalInitMutex};
	if (!globalInitOK) return;
	if (activeWclapCount > 0) {
		std::cerr << "Tried to de-init WCLAP bridge while WCLAPs are still active\n";
		abort();
	}
	instanceGlobalDeinit();
	globalInitOK = false;
}

static std::string ensureTrailingSlash(const char *dirC) {
	std::string dir = dirC;
	if (dir.size() && dir.back() != '/') dir += "/";
	return dir;
}

void * wclap_open_with_dirs(const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir) {
	if (!globalInitOK) {
		std::cerr << "WASM engine not configured - did wclap_global_init() succeed?\n";
		return nullptr;
	}
	if (!wclapDir) {
		std::cerr << "WCLAP path was null\n";
		return nullptr;
	}

	std::ifstream wasmFile{ensureTrailingSlash(wclapDir) + "module.wasm", std::ios::binary};
	if (!wasmFile) {
		wasmFile = std::ifstream{wclapDir, std::ios::binary};
		if (wasmFile) wclapDir = nullptr; // if it's not a bundle, don't provide /plugin/
	}
	if (!wasmFile) {
		std::cerr << "Couldn't open ?.wclap/module.wasm or ?.wclap\n";
		return nullptr;
	}
	std::vector<char> wasmBytes{std::istreambuf_iterator<char>{wasmFile}, {}};
	if (!wasmBytes.size()) {
		std::cerr << "Couldn't read WASM file\n";
		return nullptr;
	}

	auto *instance = createInstance((unsigned char *)wasmBytes.data(), wasmBytes.size(), wclapDir, presetDir, cacheDir, varDir);
	auto error = instance->error();
	if (error) {
		std::cerr << *error << std::endl;
		delete instance;
		return nullptr;
	}
	
	++activeWclapCount;
	return new wclap_bridge::WclapModule(instance);
}
void * wclap_open(const char *wclapDir) {
	return wclap_open_with_dirs(wclapDir, nullptr, nullptr, nullptr);
}
bool wclap_get_error(void *wclap, char *buffer, uint32_t bufferCapacity) {
	return ((wclap_bridge::WclapModule *)wclap)->getError(buffer, (size_t)bufferCapacity);
}
bool wclap_close(void *wclap) {
	if (!wclap) {
		std::cerr << "null WCLAP pointer\n";
		abort();
	}
	--activeWclapCount;
	delete (wclap_bridge::WclapModule *)wclap;
	return true;
}
const wclap_version_triple * wclap_version(void *wclap) {
	if (!wclap) {
		std::cerr << "null WCLAP pointer\n";
		abort();
	}
	auto *version = ((wclap_bridge::WclapModule *)wclap)->moduleClapVersion();
	return (const wclap_version_triple *)version;
}
const void * wclap_get_factory(void *wclap, const char *factory_id) {
	if (!wclap) {
		std::cerr << "null WCLAP pointer\n";
		abort();
	}
	return ((wclap_bridge::WclapModule *)wclap)->getFactory(factory_id);
}

static const wclap_version_triple bridgeVersion = WCLAP_VERSION_INIT;
const wclap_version_triple * wclap_bridge_version() {
	return &bridgeVersion;
}
