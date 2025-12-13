#pragma once

#define WCLAP_API_NAMESPACE wclap32
#define WCLAP_BRIDGE_NAMESPACE wclap_bridge32
#define WCLAP_BRIDGE_IS64 false
#include "./_generic/wclap-module.h"
#undef WCLAP_API_NAMESPACE
#undef WCLAP_BRIDGE_NAMESPACE
#undef WCLAP_BRIDGE_IS64

#define WCLAP_API_NAMESPACE wclap64
#define WCLAP_BRIDGE_NAMESPACE wclap_bridge64
#define WCLAP_BRIDGE_IS64 true
#include "./_generic/wclap-module.h"
#undef WCLAP_API_NAMESPACE
#undef WCLAP_BRIDGE_NAMESPACE
#undef WCLAP_BRIDGE_IS64

namespace wclap_bridge {

// Routes to the 32-/64-bit implementation as appropriate
struct WclapModule {
	using Wclap32 = wclap_bridge32::WclapModule;
	using Wclap64 = wclap_bridge64::WclapModule;
	std::unique_ptr<Wclap32> wclap32;
	std::unique_ptr<Wclap64> wclap64;

	WclapModule(Instance *instance) {
		if (instance->is64()) {
			wclap64 = std::unique_ptr<Wclap64>{new Wclap64(instance)};
		} else {
			wclap32 = std::unique_ptr<Wclap32>{new Wclap32(instance)};
		}
	}
	
	bool getError(char *buffer, size_t bufferLength) {
		if (wclap32) return wclap32->getError(buffer, bufferLength);
		return wclap64->getError(buffer, bufferLength);
	}

	const clap_version * moduleClapVersion() {
		if (wclap32) return &wclap32->clapVersion;
		return &wclap64->clapVersion;
	}

	void * getFactory(const char *factoryId) {
		if (wclap32) return wclap32->getFactory(factoryId);
		return wclap64->getFactory(factoryId);
	}
};

}; // namespace
