#pragma once

#include "clap/plugin.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <ctime>

// Simple metadata cache for WCLAP plugin descriptors
// Avoids loading WASM modules just to query plugin info

namespace wclap_cache {

// Cached plugin descriptor with owned strings
struct CachedDescriptor {
    std::string id;
    std::string name;
    std::string vendor;
    std::string url;
    std::string manual_url;
    std::string support_url;
    std::string version;
    std::string description;
    std::vector<std::string> features;

    // Storage for feature pointers (clap_plugin_descriptor needs const char**)
    mutable std::vector<const char*> feature_ptrs;
    mutable clap_plugin_descriptor clap_desc;

    const clap_plugin_descriptor* toClapDescriptor() const {
        feature_ptrs.clear();
        for (const auto& f : features) {
            feature_ptrs.push_back(f.c_str());
        }
        feature_ptrs.push_back(nullptr);

        clap_desc = clap_plugin_descriptor{
            .clap_version = CLAP_VERSION,
            .id = id.c_str(),
            .name = name.c_str(),
            .vendor = vendor.c_str(),
            .url = url.c_str(),
            .manual_url = manual_url.c_str(),
            .support_url = support_url.c_str(),
            .version = version.c_str(),
            .description = description.c_str(),
            .features = feature_ptrs.data()
        };
        return &clap_desc;
    }

    static CachedDescriptor fromClapDescriptor(const clap_plugin_descriptor* desc) {
        CachedDescriptor cached;
        cached.id = desc->id ? desc->id : "";
        cached.name = desc->name ? desc->name : "";
        cached.vendor = desc->vendor ? desc->vendor : "";
        cached.url = desc->url ? desc->url : "";
        cached.manual_url = desc->manual_url ? desc->manual_url : "";
        cached.support_url = desc->support_url ? desc->support_url : "";
        cached.version = desc->version ? desc->version : "";
        cached.description = desc->description ? desc->description : "";

        if (desc->features) {
            for (auto f = desc->features; *f; ++f) {
                cached.features.push_back(*f);
            }
        }
        return cached;
    }
};

// Cached info for a single .wclap bundle
struct CachedWclap {
    std::string path;
    std::time_t mtime;
    std::vector<CachedDescriptor> descriptors;
};

// Get platform-specific cache directory
inline std::string getCacheDir() {
#if __APPLE__
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/Library/Caches/wclap-bridge";
    }
#elif defined(_WIN32)
    char localAppData[260];
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData))) {
        return std::string(localAppData) + "\\wclap-bridge\\cache";
    }
#elif defined(__linux__)
    const char* xdgCache = getenv("XDG_CACHE_HOME");
    if (xdgCache) {
        return std::string(xdgCache) + "/wclap-bridge";
    }
    const char* home = getenv("HOME");
    if (home) {
        return std::string(home) + "/.cache/wclap-bridge";
    }
#endif
    return "";
}

inline std::string getCacheFilePath() {
    std::string dir = getCacheDir();
    if (dir.empty()) return "";
#ifdef _WIN32
    return dir + "\\plugin-cache.json";
#else
    return dir + "/plugin-cache.json";
#endif
}

class MetadataCache {
public:
    std::map<std::string, CachedWclap> entries;

    bool load() {
        std::string path = getCacheFilePath();
        if (path.empty()) return false;

        std::ifstream file(path);
        if (!file.is_open()) return false;

        try {
            nlohmann::json j;
            file >> j;

            if (j.value("version", 0) != 2) {
                return false;  // Unknown version
            }

            entries.clear();
            for (const auto& [wclapPath, wclapJson] : j["wclaps"].items()) {
                CachedWclap wclap;
                wclap.path = wclapPath;
                wclap.mtime = wclapJson.value("mtime", std::time_t{0});

                for (const auto& descJson : wclapJson["plugins"]) {
                    CachedDescriptor desc;
                    desc.id = descJson.value("id", "");
                    desc.name = descJson.value("name", "");
                    desc.vendor = descJson.value("vendor", "");
                    desc.url = descJson.value("url", "");
                    desc.manual_url = descJson.value("manual_url", "");
                    desc.support_url = descJson.value("support_url", "");
                    desc.version = descJson.value("version", "");
                    desc.description = descJson.value("description", "");
                    desc.features = descJson.value("features", std::vector<std::string>{});
                    wclap.descriptors.push_back(std::move(desc));
                }

                entries[wclapPath] = std::move(wclap);
            }

            return true;
        } catch (const nlohmann::json::exception&) {
            return false;
        }
    }

    bool save() {
        std::string dir = getCacheDir();
        if (dir.empty()) return false;

        // Create cache directory if needed
        std::filesystem::create_directories(dir);

        std::string path = getCacheFilePath();
        std::ofstream file(path);
        if (!file.is_open()) return false;

        nlohmann::json j;
        j["version"] = 2;
        j["wclaps"] = nlohmann::json::object();

        for (const auto& [wclapPath, wclap] : entries) {
            nlohmann::json wclapJson;
            wclapJson["mtime"] = wclap.mtime;
            wclapJson["plugins"] = nlohmann::json::array();

            for (const auto& desc : wclap.descriptors) {
                nlohmann::json descJson;
                descJson["id"] = desc.id;
                descJson["name"] = desc.name;
                descJson["vendor"] = desc.vendor;
                descJson["url"] = desc.url;
                descJson["manual_url"] = desc.manual_url;
                descJson["support_url"] = desc.support_url;
                descJson["version"] = desc.version;
                descJson["description"] = desc.description;
                descJson["features"] = desc.features;
                wclapJson["plugins"].push_back(descJson);
            }

            j["wclaps"][wclapPath] = wclapJson;
        }

        file << j.dump(2);  // Pretty print with 2-space indent
        return true;
    }

    // Check if cache entry is valid for given path
    bool isValid(const std::string& wclapPath) const {
        auto it = entries.find(wclapPath);
        if (it == entries.end()) return false;

        std::time_t mtime = getWclapMtime(wclapPath);
        if (mtime == 0) return false;

        return it->second.mtime == mtime;
    }

    // Get mtime for a wclap (either bundle/module.wasm or single .wclap file)
    static std::time_t getWclapMtime(const std::string& wclapPath) {
        std::string targetPath;

        // Check if it's a bundle with module.wasm inside
        std::string bundlePath = wclapPath + "/module.wasm";
        if (std::filesystem::exists(bundlePath)) {
            targetPath = bundlePath;
        } else if (std::filesystem::exists(wclapPath) && std::filesystem::is_regular_file(wclapPath)) {
            // Single file .wclap
            targetPath = wclapPath;
        } else {
            return 0;
        }

        auto fileTime = std::filesystem::last_write_time(targetPath);
        auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
            fileTime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
        return std::chrono::system_clock::to_time_t(sctp);
    }

    void updateEntry(const std::string& wclapPath, const std::vector<CachedDescriptor>& descriptors) {
        CachedWclap entry;
        entry.path = wclapPath;
        entry.mtime = getWclapMtime(wclapPath);
        entry.descriptors = descriptors;
        entries[wclapPath] = std::move(entry);
    }
};

} // namespace wclap_cache
