#include "wclap-bridge.h"
#include "metadata-cache.h"

#include "clap/all.h"

#include "semver/semver.hpp"

#include <iostream>
#include <atomic>
#include <mutex>
#include <string>
#include <cstring>
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
#elif defined(_WIN32)
#	include <shlobj.h>
#	include <stringapiset.h>
std::string stringFromPWSTR(PWSTR pwstr) {
	auto length = WideCharToMultiByte(CP_UTF8, 0, pwstr, -1, nullptr, 0, nullptr, nullptr);
	std::string result;
	result.resize(length);
	WideCharToMultiByte(CP_UTF8, 0, pwstr, -1, result.data(), length, nullptr, nullptr);
	return result;
}
void scanWclapDirectories() {
	PWSTR knownPath;
	
	if (SHGetKnownFolderPath(FOLDERID_ProgramFilesCommon, 0, nullptr, &knownPath) == S_OK) {
		scanWclapDirectory(stringFromPWSTR(knownPath) + "\\WCLAP\\");
	}
	CoTaskMemFree(knownPath);

	if (SHGetKnownFolderPath(FOLDERID_UserProgramFilesCommon, 0, nullptr, &knownPath) == S_OK) {
		scanWclapDirectory(stringFromPWSTR(knownPath) + "\\WCLAP\\");
	}
	CoTaskMemFree(knownPath);
}
#elif defined(__linux__)
#	include <stdlib.h>
void scanWclapDirectories() {
	scanWclapDirectory("/usr/lib/wclap/");

	// ~/.wclap
	const char *home = getenv("HOME");
	if (home) {
		scanWclapDirectory(home + std::string("/.wclap/"));
	}
}
#else
#	error "Unsupported OS - please add to wclap-bridge-plugin.cpp"
#endif

std::mutex initMutex;
std::atomic<int> initCounter = 0;

// Lazily-loaded WCLAP bundle
struct Wclap {
	std::string path;
	void *handle = nullptr;
	const clap_plugin_factory *pluginFactory = nullptr;
	std::vector<wclap_cache::CachedDescriptor> cachedDescriptors;
	bool loadedFromCache = false;

	Wclap(const std::string &path) : path(path) {}
	Wclap(const Wclap &other) = delete;
	Wclap(Wclap &&other) : path(std::move(other.path)), handle(other.handle),
		pluginFactory(other.pluginFactory), cachedDescriptors(std::move(other.cachedDescriptors)),
		loadedFromCache(other.loadedFromCache) {
		other.handle = nullptr;
	}
	~Wclap() {
		if (handle) wclap_close(handle);
	}

	// Actually load the WASM module (called on-demand)
	bool ensureLoaded() {
		if (handle) return true;

		handle = wclap_open(path.c_str());
		char errorMessage[256] = "";
		if (wclap_get_error(handle, errorMessage, 256)) {
			std::cerr << "WCLAP bridge plugin: couldn't open WCLAP at: " << path << "\n";
			std::cerr << errorMessage << std::endl;
			wclap_close(handle);
			handle = nullptr;
			return false;
		}
		std::cout << "Loaded WCLAP: " << path << std::endl;
		pluginFactory = (const clap_plugin_factory *)wclap_get_factory(handle, CLAP_PLUGIN_FACTORY_ID);
		return pluginFactory != nullptr;
	}
};

static std::vector<std::string> wclapDirs;
static std::vector<Wclap> wclapList;
static wclap_cache::MetadataCache metadataCache;

void scanWclapDirectory(const std::string &pathStr) {
	wclapDirs.push_back(pathStr);

	if (!std::filesystem::exists(pathStr)) return;
	for (auto &entry : std::filesystem::recursive_directory_iterator(pathStr)) {
		auto wclapPath = entry.path().string();
		if (wclapPath.size() < 6) continue;
		auto wclapEnd = wclapPath.substr(wclapPath.size() - 6);
		if (wclapEnd == ".wclap") {
			wclapList.emplace_back(wclapPath);
			auto &wclap = wclapList.back();

			// Check if we have valid cached metadata
			if (metadataCache.isValid(wclapPath)) {
				// Use cached descriptors - no need to load WASM
				wclap.cachedDescriptors = metadataCache.entries[wclapPath].descriptors;
				wclap.loadedFromCache = true;
				std::cout << "Using cached metadata for: " << wclapPath << std::endl;
			} else {
				// Need to load to get descriptors
				if (!wclap.ensureLoaded()) {
					wclapList.pop_back();
					continue;
				}

				// Cache the descriptors for next time
				std::vector<wclap_cache::CachedDescriptor> descriptors;
				auto count = wclap.pluginFactory->get_plugin_count(wclap.pluginFactory);
				for (uint32_t i = 0; i < count; ++i) {
					auto *desc = wclap.pluginFactory->get_plugin_descriptor(wclap.pluginFactory, i);
					if (desc) {
						descriptors.push_back(wclap_cache::CachedDescriptor::fromClapDescriptor(desc));
					}
				}
				metadataCache.updateEntry(wclapPath, descriptors);
				wclap.cachedDescriptors = std::move(descriptors);
			}
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
	size_t wclapIndex;
	size_t descriptorIndex;  // Index into wclap's cachedDescriptors
};
static std::vector<Plugin> pluginList;

void scanWclapPlugins() {
	for (size_t wclapIndex = 0; wclapIndex < wclapList.size(); ++wclapIndex) {
		auto &wclap = wclapList[wclapIndex];

		for (size_t descIdx = 0; descIdx < wclap.cachedDescriptors.size(); ++descIdx) {
			auto &cached = wclap.cachedDescriptors[descIdx];

			bool duplicate = false;
			for (auto &existing : pluginList) {
				auto &existingCached = wclapList[existing.wclapIndex].cachedDescriptors[existing.descriptorIndex];
				if (cached.id == existingCached.id) {
					duplicate = true;
					bool newer = false;
					if (existingCached.version.empty()) {
						newer = true;
					} else if (!cached.version.empty()) {
						try {
							auto ver = semver::version::parse(cached.version);
							auto existingVer = semver::version::parse(existingCached.version);
							newer = ver > existingVer;
						} catch (...) {
							// If version parsing fails, keep existing
						}
					}
					if (newer) existing = {wclapIndex, descIdx};
					break;
				}
			}
			if (duplicate) continue;

			pluginList.push_back({wclapIndex, descIdx});
		}
	}
}

CLAP_EXPORT bool clap_init(const char *modulePath) {
	std::lock_guard<std::mutex> lock{initMutex};
	if (initCounter++) return true;

	auto globalInit = wclap_global_init(250); // allow 250ms for any given function call
	if (!globalInit) return false;
	wclap_set_strings("wclap:", "[WCLAP] ", "");

	// Load metadata cache (failures are non-fatal)
	metadataCache.load();

	scanWclapDirectories();
	// TODO: search CLAP_PATH environment variable
	scanWclapPlugins();

	// Save updated cache
	metadataCache.save();

	return true;
}

CLAP_EXPORT void clap_deinit() {
	std::lock_guard<std::mutex> lock{initMutex};
	if (--initCounter) return;

	wclapList.clear();
	invalidations.clear();
	pluginList.clear();
	wclap_global_deinit();
}

static uint32_t pluginFactory_get_plugin_count(const struct clap_plugin_factory *factory) {
	return uint32_t(pluginList.size());
}
static const clap_plugin_descriptor_t * pluginFactory_get_plugin_descriptor(const struct clap_plugin_factory *factory, uint32_t index) {
	if (index >= pluginList.size()) return nullptr;
	auto &plugin = pluginList[index];
	auto &wclap = wclapList[plugin.wclapIndex];
	// Return cached descriptor (converted to clap_plugin_descriptor)
	return wclap.cachedDescriptors[plugin.descriptorIndex].toClapDescriptor();
}
static const clap_plugin_t * pluginFactory_create_plugin(const struct clap_plugin_factory *factory, const clap_host *host, const char *pluginId) {
	for (auto &plugin : pluginList) {
		auto &wclap = wclapList[plugin.wclapIndex];
		auto &cached = wclap.cachedDescriptors[plugin.descriptorIndex];
		if (cached.id == pluginId) {
			// Lazy load: ensure the WASM module is loaded before creating
			if (!wclap.ensureLoaded()) {
				std::cerr << "Failed to load WCLAP for plugin: " << pluginId << std::endl;
				return nullptr;
			}
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
