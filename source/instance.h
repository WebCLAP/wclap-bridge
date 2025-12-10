#pragma once

#include "./wclap-instance-wasmtime/wclap-instance-wasmtime.h"

using Instance = wclap::Instance<wclap_wasmtime::InstanceImpl>;

Instance * createInstance(const unsigned char *wasmBytes, size_t wasmLength, const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir) {
	return new Instance(wasmBytes, wasmLength, wclapDir, presetDir, cacheDir, varDir);
}

