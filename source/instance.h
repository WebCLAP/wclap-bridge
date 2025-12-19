#pragma once

#include "./wclap-instance-wasmi/wclap-instance-wasmi.h"

using InstanceGroup = wclap_wasmi::InstanceGroup;
using Instance = wclap::Instance<wclap_wasmi::InstanceImpl>;

InstanceGroup * createInstanceGroup(const unsigned char *wasmBytes, size_t wasmLength, const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir) {
	return new InstanceGroup(wasmBytes, wasmLength, wclapDir, presetDir, cacheDir, varDir);
}

bool instanceGlobalInit(unsigned int timeLimitMs) {
	return wclap_wasmi::InstanceGroup::globalInit(timeLimitMs);
}
void instanceGlobalDeinit() {
	return wclap_wasmi::InstanceGroup::globalDeinit();
}
