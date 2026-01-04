#pragma once

#include "clap/plugin.h"
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <sstream>
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
    return dir + "\\plugin-cache.txt";
#else
    return dir + "/plugin-cache.txt";
#endif
}

// Simple text-based serialization (avoiding JSON dependency)
// Format:
//   WCLAP_CACHE_V1
//   PATH:<path>
//   MTIME:<mtime>
//   PLUGIN_COUNT:<n>
//   ID:<id>
//   NAME:<name>
//   ...
//   END_PLUGIN
//   END_WCLAP

class MetadataCache {
public:
    std::map<std::string, CachedWclap> entries;

    bool load() {
        std::string path = getCacheFilePath();
        if (path.empty()) return false;

        std::ifstream file(path);
        if (!file.is_open()) return false;

        std::string line;
        if (!std::getline(file, line) || line != "WCLAP_CACHE_V1") {
            return false;
        }

        entries.clear();
        CachedWclap* currentWclap = nullptr;
        CachedDescriptor* currentDesc = nullptr;

        while (std::getline(file, line)) {
            size_t colonPos = line.find(':');
            if (colonPos == std::string::npos) continue;

            std::string key = line.substr(0, colonPos);
            std::string value = line.substr(colonPos + 1);

            if (key == "PATH") {
                entries[value] = CachedWclap{};
                currentWclap = &entries[value];
                currentWclap->path = value;
                currentDesc = nullptr;
            } else if (key == "MTIME" && currentWclap) {
                currentWclap->mtime = std::stoll(value);
            } else if (key == "BEGIN_PLUGIN" && currentWclap) {
                currentWclap->descriptors.emplace_back();
                currentDesc = &currentWclap->descriptors.back();
            } else if (key == "END_PLUGIN") {
                currentDesc = nullptr;
            } else if (key == "END_WCLAP") {
                currentWclap = nullptr;
            } else if (currentDesc) {
                if (key == "ID") currentDesc->id = value;
                else if (key == "NAME") currentDesc->name = value;
                else if (key == "VENDOR") currentDesc->vendor = value;
                else if (key == "URL") currentDesc->url = value;
                else if (key == "MANUAL_URL") currentDesc->manual_url = value;
                else if (key == "SUPPORT_URL") currentDesc->support_url = value;
                else if (key == "VERSION") currentDesc->version = value;
                else if (key == "DESCRIPTION") currentDesc->description = value;
                else if (key == "FEATURE") currentDesc->features.push_back(value);
            }
        }

        return true;
    }

    bool save() {
        std::string dir = getCacheDir();
        if (dir.empty()) return false;

        // Create cache directory if needed
        std::filesystem::create_directories(dir);

        std::string path = getCacheFilePath();
        std::ofstream file(path);
        if (!file.is_open()) return false;

        file << "WCLAP_CACHE_V1\n";

        for (const auto& [wclapPath, wclap] : entries) {
            file << "PATH:" << wclap.path << "\n";
            file << "MTIME:" << wclap.mtime << "\n";

            for (const auto& desc : wclap.descriptors) {
                file << "BEGIN_PLUGIN:\n";
                file << "ID:" << desc.id << "\n";
                file << "NAME:" << desc.name << "\n";
                file << "VENDOR:" << desc.vendor << "\n";
                file << "URL:" << desc.url << "\n";
                file << "MANUAL_URL:" << desc.manual_url << "\n";
                file << "SUPPORT_URL:" << desc.support_url << "\n";
                file << "VERSION:" << desc.version << "\n";
                file << "DESCRIPTION:" << desc.description << "\n";
                for (const auto& f : desc.features) {
                    file << "FEATURE:" << f << "\n";
                }
                file << "END_PLUGIN\n";
            }
            file << "END_WCLAP\n";
        }

        return true;
    }

    // Check if cache entry is valid for given path
    bool isValid(const std::string& wclapPath) const {
        auto it = entries.find(wclapPath);
        if (it == entries.end()) return false;

        // Check if module.wasm mtime matches
        std::string wasmPath = wclapPath + "/module.wasm";
        if (!std::filesystem::exists(wasmPath)) return false;

        auto fileTime = std::filesystem::last_write_time(wasmPath);
        auto sctp = std::chrono::time_point_cast<std::chrono::seconds>(
            fileTime - std::filesystem::file_time_type::clock::now() +
            std::chrono::system_clock::now());
        std::time_t mtime = std::chrono::system_clock::to_time_t(sctp);

        return it->second.mtime == mtime;
    }

    // Get mtime for a wclap's module.wasm
    static std::time_t getWclapMtime(const std::string& wclapPath) {
        std::string wasmPath = wclapPath + "/module.wasm";
        if (!std::filesystem::exists(wasmPath)) return 0;

        auto fileTime = std::filesystem::last_write_time(wasmPath);
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
