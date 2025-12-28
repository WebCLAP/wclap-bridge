#include "wclap-bridge.h"

#include "clap/all.h"

#include <iostream>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>

#ifndef LOG_EXPR
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

void scanWclapDirectory(const std::string &pathStr);

#if __APPLE__ && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)
#	include <stdlib.h>
void scanWclapDirectories() {
	std::string wclapPath = "/Library/Audio/Plug-Ins/WCLAP/";
	scanWclapDirectory(wclapPath);
	
	const char *home = getenv("HOME");
	if (home) {
		scanWclapDirectory(home + wclapPath);
	}
}
#else
#	error "Only MacOS implemented - please add this OS to wclap-bridge-plugin.cpp"
#endif

std::mutex initMutex;
std::atomic<int> initCounter = 0;

struct Wclap {
	void *handle;
	const clap_plugin_factory *pluginFactory;
	
	Wclap(void *handle) : handle(handle) {
		pluginFactory = (const clap_plugin_factory *)wclap_get_factory(handle, CLAP_PLUGIN_FACTORY_ID);
	}
	Wclap(const Wclap &other) = delete;
	Wclap(Wclap &&other) : handle(other.handle), pluginFactory(other.pluginFactory) {
		other.handle = nullptr;
	}
	~Wclap() {
		if (handle) wclap_close(handle);
	}
};
static std::vector<std::string> wclapDirs;
static std::vector<Wclap> wclapList;
void scanWclapDirectory(const std::string &pathStr) {
	wclapDirs.push_back(pathStr);
	
	if (!std::filesystem::exists(pathStr)) return;
	for (auto &entry : std::filesystem::recursive_directory_iterator(pathStr)) {
		auto wclapPath = entry.path().string();
		auto wclapEnd = wclapPath.substr(wclapPath.size() - 6);
		if (wclapEnd == ".wclap") {
			std::cout << "Found: " << wclapPath << std::endl;
			auto *handle = wclap_open(wclapPath.c_str());
			char errorMessage[256] = "";
			if (wclap_get_error(handle, errorMessage, 256)) {
				std::cerr << "WCLAP bridge plugin: couldn't open WCLAP at: " << wclapPath << "\n";
				std::cerr << errorMessage << std::endl;
				wclap_close(handle);
				continue;
			}
			std::cout << "Opened WCLAP: " << wclapPath << std::endl;
			wclapList.emplace_back(handle);
		}
	}
}

static std::vector<clap_plugin_invalidation_source> invalidations;
void makeInvalidations() {
	for (auto &str : wclapDirs) {
		invalidations.push_back(clap_plugin_invalidation_source{
			.directory=str.c_str(),
			.filename_glob="*.wclap",
			.recursive_scan=true
		});
	}
}

struct Plugin {
	const clap_plugin_descriptor *desc;
	size_t wclapIndex;
};
struct std::vector<Plugin> pluginList;

void scanWclapPlugins() {
	for (size_t wclapIndex = 0; wclapIndex < wclapList.size(); ++wclapIndex) {
		auto &wclap = wclapList[wclapIndex];
		if (!wclap.pluginFactory) continue;
		
		auto count = wclap.pluginFactory->get_plugin_count(wclap.pluginFactory);
		for (size_t i = 0; i < count; ++i) {
			auto *desc = wclap.pluginFactory->get_plugin_descriptor(wclap.pluginFactory, i);
			if (desc) {
				pluginList.push_back({desc, wclapIndex});
			}
		}
	}
}

CLAP_EXPORT bool clap_init(const char *modulePath) {
	std::lock_guard<std::mutex> lock{initMutex};
	if (initCounter++) return true;
	
	auto globalInit = wclap_global_init(250); // allow 250ms for any given function call
	if (!globalInit) return false;
	wclap_set_strings("wclap:", "[WCLAP] ", "");
	
	scanWclapDirectories();
	scanWclapPlugins();
	
	return true;
}

CLAP_EXPORT void clap_deinit() {
	std::lock_guard<std::mutex> lock{initMutex};
	if (--initCounter) return;

	wclapList.clear();
	wclap_global_deinit();
}

static uint32_t pluginFactory_get_plugin_count(const struct clap_plugin_factory *factory) {
	return uint32_t(pluginList.size());
}
static const clap_plugin_descriptor_t * pluginFactory_get_plugin_descriptor(const struct clap_plugin_factory *factory, uint32_t index) {
	if (index >= pluginList.size()) return nullptr;
	return pluginList[index].desc;
}
static const clap_plugin_t * pluginFactory_create_plugin(const struct clap_plugin_factory *factory, const clap_host *host, const char *pluginId) {
	for (auto &plugin : pluginList) {
		if (!std::strcmp(pluginId, plugin.desc->id)) {
			auto &wclap = wclapList[plugin.wclapIndex];
			return wclap.pluginFactory->create_plugin(wclap.pluginFactory, host, pluginId);
		}
	}
	return nullptr;
}

static uint32_t pluginInvalidationFactory_count(const struct clap_plugin_invalidation_factory *factory) {
	return uint32_t(invalidations.size());
}
static const clap_plugin_invalidation_source * pluginInvalidationFactory_get(const struct clap_plugin_invalidation_factory *factory, uint32_t index) {
	if (index >= invalidations.size()) return nullptr;
	return invalidations.data() + index;
}
static bool pluginInvalidationFactory_refresh(const struct clap_plugin_invalidation_factory *factory) {
	return true;
}

CLAP_EXPORT const void * clap_get_factory(const char* factoryId) {
	if (!std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) {
		static const clap_plugin_factory factory{
			.get_plugin_count=pluginFactory_get_plugin_count,
			.get_plugin_descriptor=pluginFactory_get_plugin_descriptor,
			.create_plugin=pluginFactory_create_plugin
		};
		return &factory;
	}
	if (!std::strcmp(factoryId, CLAP_PLUGIN_INVALIDATION_FACTORY_ID)) {
		static const clap_plugin_invalidation_factory factory{
			.count=pluginInvalidationFactory_count,
			.get=pluginInvalidationFactory_get,
			.refresh=pluginInvalidationFactory_refresh
		};
		return &factory;
	}
	return nullptr;
}
