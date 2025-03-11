#include <iostream>
#define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;

#import "clap/all.h"
#import "wasmtime.h"

template<class NativeClapStruct>
struct WclapTranslateStruct;

struct WclapTranslationScope {
	static constexpr size_t arenaBytes = 65536;

	wasm_instance_t *instance;
	wasm_memory_t *memory;
	uint32_t wasmObjectP; // WASM object whose lifetime this scope is tied to
	uint32_t wasmPointerToThis; // If we point WASM context fields to here, we can find this

	WclapTranslationScope(wasm_instance_t *instance, wasm_memory_t *memory, uint32_t wasmObjectP) : instance(instance), memory(memory), wasmObjectP(wasmObjectP) {
		nativeArena = nativeTmpStartP = nativeTmpP = (unsigned char *)malloc(arenaBytes);
//		wasmArena = wasmTmpStartP = wasmTmpP = wasm_malloc(arenaBytes);
wasmArena = wasmTmpStartP = wasmTmpP = 0;
abort();

		wasmPointerToThis = temporaryWasmBytes(sizeof(this), alignof(decltype(this)));
		*(WclapTranslationScope **)nativeInWasm(wasmPointerToThis) = this;
		commitWasm();
	}
	// Should only happen when the WASM instance is destroyed - otherwise it should be returned to a pool (since we can't free the arena memory)
	~WclapTranslationScope() {
		free(nativeArena);
	}
	
	void * nativeInWasm(uint32_t wasmP) {
		// TODO: bounds-check
		return wasm_memory_data(memory) + wasmP;
	}
	template<class T>
	T & valueInWasm(uint32_t wasmP) {
		return *(T *)nativeInWasm(wasmP);
	}

	unsigned char *nativeArena, *nativeTmpStartP, *nativeTmpP;
	void clearTemporaryNative() {
		nativeTmpP = nativeTmpStartP;
	}
	void * temporaryNativeBytes(size_t size, size_t align) {
		while (((size_t)nativeTmpP)%align) ++nativeTmpP;
		void *result = nativeTmpP;
		nativeTmpP += size;
		if (nativeTmpP > nativeTmpStartP + arenaBytes) {
			abort(); // TODO: grow list of arenas
		}
		return result;
	}
	void commitNative() {
		nativeTmpStartP = nativeTmpP;
	}

	uint32_t wasmArena, wasmTmpStartP, wasmTmpP;
	void clearTemporaryWasm() {
		wasmTmpP = wasmTmpStartP;
	}
	uint32_t temporaryWasmBytes(uint32_t size, uint32_t align) {
		while (wasmTmpP%align) ++wasmTmpP;
		uint32_t result = wasmTmpP;
		wasmTmpP += size;
		if (wasmTmpP > wasmTmpStartP + arenaBytes) {
			abort(); // TODO: grow list of arenas
		}
		return result;
	}
	void commitWasm() {
		wasmTmpStartP = wasmTmpP;
	}

	// These get called for types which map directly between WASM/native
	template<class V>
	void assignWasmToNativeDirect(uint32_t wasmP, V &native) {
		auto *nativeWasmP = (const V *)nativeInWasm(wasmP);
		native = *nativeWasmP;
	}
	template<class V>
	void assignNativeToWasmDirect(const V &native, uint32_t wasmP) {
		auto *nativeWasmP = (V *)nativeInWasm(wasmP);
		*nativeWasmP = native;
	}
	
	template<class V>
	void assignWasmToNative(uint32_t wasmP, V &native);
	template<class V>
	void assignNativeToWasm(const V &native, uint32_t wasmP);
	
	void assignWasmToNative_clap_plugin_descriptor_t_features(uint32_t wasmP, const char * const * &features);
	// Not sure why the host would ever pass a plugin feature-list back *into* the WASM, but 🤷
	void assignNativeToWasm_clap_plugin_descriptor_t_features(const char * const * const &features, uint32_t wasmP);
	
	template<class Return, class Arg1>
	void assignWasmToNative_ii(uint32_t wasmP, Return(*&fnPointer)(Arg1)) {
		fnPointer = nullptr;
	}
	template<class Return, class Arg1>
	void assignNativeToWasm_ii(Return(* const &fnPointer)(Arg1), uint32_t wasmP) {
		valueInWasm<uint32_t>(wasmP) = 0;
	}

	template<class Return, class Arg1, class Arg2>
	void assignWasmToNative_iii(uint32_t wasmP, Return(*&fnPointer)(Arg1, Arg2)) {
		fnPointer = nullptr;
	}
	template<class Return, class Arg1, class Arg2>
	void assignNativeToWasm_iii(Return(* const &fnPointer)(Arg1, Arg2), uint32_t wasmP) {
		valueInWasm<uint32_t>(wasmP) = 0;
	}
	
	template<class Return, class Arg1, class Arg2, class Arg3>
	void assignWasmToNative_iiii(uint32_t wasmP, Return(*&fnPointer)(Arg1, Arg2, Arg3)) {
		fnPointer = nullptr;
	}
	template<class Return, class Arg1, class Arg2, class Arg3>
	void assignNativeToWasm_iiii(Return(* const &fnPointer)(Arg1, Arg2, Arg3), uint32_t wasmP) {
		valueInWasm<uint32_t>(wasmP) = 0;
	}
};

template<>
void WclapTranslationScope::assignWasmToNative(uint32_t wasmP, const char * &native) {
	auto wasmString = *(uint32_t *)nativeInWasm(wasmP);
	
	if (!wasmString) {
		native = nullptr;
	} else {
		native = (const char *)nativeInWasm(wasmString);
	}
}
template<>
void WclapTranslationScope::assignNativeToWasm(const char * const &native, uint32_t wasmP) {
	auto &wasmString = *(uint32_t *)nativeInWasm(wasmP);
	
	if (!native) {
		wasmString = 0;
	} else {
		size_t size = std::strlen(native);
		// TODO: maximum string length for sanity/safety?
		wasmString = temporaryWasmBytes(size + 1, 1);
		auto *stringInWasm = (char *)nativeInWasm(wasmString);
		for (size_t i = 0; i < size; ++i) {
			stringInWasm[i] = native[i];
		}
		stringInWasm[size] = 0;
	}
}

// Auto-generated struct translation
#include "./wclap-translate-struct.generated.h"
