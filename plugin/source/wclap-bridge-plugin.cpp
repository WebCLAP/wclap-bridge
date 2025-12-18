#include "wclap-bridge.h"

#include "clap/all.h"

#include <iostream>
#include <atomic>
#include <string>

#ifndef LOG_EXPR
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

std::atomic_flag initialised = ATOMIC_FLAG_INIT;
std::atomic<void *> wclapHandle = nullptr;

CLAP_EXPORT bool clap_init(const char *modulePath) {
	if (initialised.test_and_set()) {
		return wclapHandle.load() != nullptr;
	}
	
	wclap_global_init(250); // allow 250ms for any given function call
	wclap_set_strings("wclap:", "[WCLAP] ", "");
	
	std::string clapPath = modulePath;
	std::string wclapPath;
	// Attempt to find matching WCLAP based on our path
	size_t index = 0;
	while (index < clapPath.size()) {
		if (clapPath.substr(index, 5) == ".clap" || clapPath.substr(index, 5) == ".vst3") {
			wclapPath += ".wclap";
			break;
		} else if (clapPath.substr(index, 6) == "/CLAP/" || clapPath.substr(index, 6) == "/VST3/") {
			wclapPath += "/WCLAP/";
			index += 6;
			continue;
		} else if (clapPath.substr(index, 6) == "\\CLAP\\" || clapPath.substr(index, 6) == "\\VST3\\") {
			wclapPath += "\\WCLAP\\";
			index += 6;
			continue;
		} else {
			wclapPath += clapPath[index];
		}
		++index;
	}
	
	wclapHandle.store(wclap_open(wclapPath.c_str()));

	char errorMessage[256] = "";
	if (wclap_get_error(wclapHandle.load(), errorMessage, 256)) {
		std::cerr << "WCLAP bridge plugin: couldn't open WCLAP at: " << wclapPath << "\n";
		std::cerr << errorMessage << std::endl;
		wclap_close(wclapHandle.load());
		wclap_global_deinit();
		wclapHandle.store(nullptr);
		return false;
	}
	
	return true;
}

CLAP_EXPORT void clap_deinit() {
	if (!initialised.test()) return;

	if (wclapHandle.load()) {
		wclap_close(wclapHandle.load());
		wclapHandle.store(nullptr);
		wclap_global_deinit();
	}
	
	initialised.clear();
}

CLAP_EXPORT const void * clap_get_factory(const char* factoryId) {
	if (!initialised.test() || wclapHandle.load() == nullptr) return nullptr;
	
	return wclap_get_factory(wclapHandle.load(), factoryId);
}
