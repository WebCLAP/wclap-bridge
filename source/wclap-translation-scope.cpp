#include <iostream>
#define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;

#import "wclap-translation-scope.h"

template<class NativeClapStruct>
struct WclapTranslateStruct;

void WclapTranslationScope::assignWasmToNative_clap_plugin_descriptor_t_features(uint32_t wasmP, const char * const * &constFeatures) {
	// This is our object - the constness is for the host using this value
	auto features = (const char**)constFeatures;
	if (!wasmP) {
		// TODO: not sure if this is a valid value.  If not, should we fix it, or pass it on?
		*features = nullptr;
		return;
	}
	
	uint32_t wasmStringArray = valueInWasm<uint32_t>(wasmP);
	size_t featureCount = 0;
	while (1) { // TODO: maximum feature count for sanity/safety?
		uint32_t wasmString = valueInWasm<uint32_t>(wasmStringArray + featureCount);
		if (!wasmString) break; // list is null-terminated
		++featureCount;
	};
	
	features = (const char **)temporaryNativeBytes(sizeof(const char*)*(featureCount + 1), alignof(const char*));
	for (size_t i = 0; i < featureCount; ++i) {
		uint32_t wasmString = valueInWasm<uint32_t>(wasmStringArray + featureCount);
		assignWasmToNative(wasmString, features[i]);
	}
	features[featureCount] = nullptr;
	commitNative();
}
void assignNativeToWasm_clap_plugin_descriptor_t_features(const char * const * const &features, uint32_t wasmP) {
	if (!features) {
		// TODO: not sure if this is a valid value.  If not, should we fix it, or pass it on?
		wasmP = 0;
		return;
	}
	
	size_t featureCount = 0;
	while(1) {
		const char * nativeString = features[featureCount];
		if (!nativeString) break;
		++featureCount;
	}
	valueInWasm(wasmP) = temporaryWasmBytes(sizeof(uint32_t)*(featureCount + 1), alignof(uint32_t));
	uint32_t *wasmStringArray = valueInWasm<uint32_t *>(wasmP);

	for (size_t i = 0; i < featureCount; ++i) {
		assignNativeToWasm(features[i], wasmStringArray[i]);
	}
	wasmStringArray[featureCount] = 0;
	commitWasm();
}

