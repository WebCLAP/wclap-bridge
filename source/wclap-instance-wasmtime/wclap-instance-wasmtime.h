#pragma once

#include "wclap/instance.hpp"

#include <shared_mutex>

struct wclap_wasmtime {
	struct InstanceImpl {
		void *handle;
	
		// no error if empty
		std::shared_mutex errorMessageMutex;
		std::string errorMessage = "not initialised";
		void setError(const std::string &message) {
			std::unique_lock lock{errorMessageMutex};
			errorMessage = message;
		}
	
		// `handle` is added by `wclap::Instance`, other constructor arguments are passed through
		InstanceImpl(void *handle, const char *wasmPath) : handle(handle), wasmPath(wasmPath) {
		}
		InstanceImpl(const InstanceImpl &other) = delete;
		~InstanceImpl() {
		}
	};
};
