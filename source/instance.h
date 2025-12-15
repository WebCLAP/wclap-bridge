#pragma once

#include "./wclap-instance-wasmtime/wclap-instance-wasmtime.h"

using InstanceGroup = wclap_wasmtime::InstanceGroup;
using Instance = wclap::Instance<wclap_wasmtime::InstanceImpl>;

InstanceGroup * createInstanceGroup(const unsigned char *wasmBytes, size_t wasmLength, const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir) {
	return new InstanceGroup(wasmBytes, wasmLength, wclapDir, presetDir, cacheDir, varDir);
}

bool instanceGlobalInit(unsigned int timeLimitMs) {
	return wclap_wasmtime::InstanceGroup::globalInit(timeLimitMs);
}
void instanceGlobalDeinit() {
	return wclap_wasmtime::InstanceGroup::globalDeinit();
}
