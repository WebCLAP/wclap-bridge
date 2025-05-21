#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "clap/all.h"
#include "wasmtime.h"

#include <type_traits>
#include <unordered_map>

struct Wclap;

template<bool use64>
struct WclapTranslationScope;

template<class T>
struct WclapLifetimeStruct : public T {
	using T::T;

	void *wclapTranslationScope = nullptr;
};

template<class NativeClapStruct>
struct Wclap32TranslateStruct;
template<class NativeClapStruct>
struct Wclap64TranslateStruct;

// A WASM pointer to an unknown type - this is what (void *) should map to when querying extensions or storing parameter cookies
struct WasmPointerUnknown {
	uint64_t wasmP;
};

template<bool use64>
struct WclapTranslator;

template<bool use64>
struct WclapTranslationScope {
	using WasmP = typename std::conditional<use64, uint64_t, uint32_t>::type;

	template<class NativeClapStruct>
	using TranslateStruct = typename std::conditional<use64, Wclap64TranslateStruct<NativeClapStruct>, Wclap32TranslateStruct<NativeClapStruct>>::type;

	static constexpr size_t arenaBytes = 65536;

	Wclap &wclap;
	
	WclapTranslationScope(Wclap &wclap, WclapThread &currentThread) : wclap(wclap) {
		nativeArena = nativeTmpStartP = nativeTmpP = (unsigned char *)malloc(arenaBytes);
		wasmArena = wasmTmpStartP = wasmTmpP = currentThread.wasmMalloc(arenaBytes);
		LOG_EXPR((void *)nativeArena);
		LOG_EXPR(wasmArena);
	}
	~WclapTranslationScope() {
		if (!_wasmReadyToDestroy) {
			LOG_EXPR(_wasmReadyToDestroy);
			abort();
		}
		free(nativeArena);
	}
	
	void * nativeObject = nullptr; // Native object
	WasmP wasmObjectP = 0; // WASM object whose lifetime this scope is tied to
	WasmP wasmPointerToThis = 0; // If we point WASM context fields to here, we can find this

	template<class NativeStruct>
	NativeStruct * bindToWasmObject(WasmP wasmP) {
		wasmObjectP = wasmP;

		wasmPointerToThis = temporaryWasmBytes(sizeof(this), alignof(decltype(this)));
		*(WclapTranslationScope **)nativeInWasm(wasmPointerToThis) = this;
		commitWasm();
		
		nativeObject = temporaryNativeBytes(sizeof(NativeStruct), alignof(NativeStruct));
		return (NativeStruct *)nativeObject;
	}

	void unbindAndReset() {
		nativeObject = nullptr;
		wasmObjectP = wasmPointerToThis = 0;
		nativeTmpP = nativeTmpStartP = nativeArena;
		wasmTmpP = wasmTmpStartP = wasmArena;
	}

	// Should only happen when the WASM instance is destroyed - otherwise it should be returned to a pool (since we can't free the arena memory)
	void wasmReadyToDestroy() {
		_wasmReadyToDestroy = true;
	}
	
	void * nativeInWasm(WasmP wasmP) {
		// TODO: bounds-check
		return wclap.wasmMemory(wasmP);
	}
	template<class T>
	T & valueInWasm(WasmP wasmP) {
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
			LOG_EXPR(nativeTmpStartP + arenaBytes);
			LOG_EXPR(nativeTmpP);
			abort(); // TODO: grow list of arenas
		}
		return result;
	}
	void commitNative() {
		nativeTmpStartP = nativeTmpP;
	}

	WasmP wasmArena, wasmTmpStartP, wasmTmpP;
	void clearTemporaryWasm() {
		wasmTmpP = wasmTmpStartP;
	}
	WasmP temporaryWasmBytes(WasmP size, WasmP align) {
		while (wasmTmpP%align) ++wasmTmpP;
		WasmP result = wasmTmpP;
		wasmTmpP += size;
		if (wasmTmpP > wasmTmpStartP + arenaBytes) {
			abort(); // TODO: grow list of arenas
		}
		return result;
	}
	void commitWasm() {
		wasmTmpStartP = wasmTmpP;
	}

	//---------- The main two categories of translators: simple or generated ----------//

	// These get called for types which map directly between WASM/native
	template<class V>
	void assignWasmToNativeDirect(WasmP wasmP, V &native) {
		auto *nativeWasmP = (const V *)nativeInWasm(wasmP);
		native = *nativeWasmP;
	}
	template<class V>
	void assignNativeToWasmDirect(const V &native, WasmP wasmP) {
		auto *nativeWasmP = (V *)nativeInWasm(wasmP);
		*nativeWasmP = native;
	}
	
	// For everything else, we use the struct translators
	template<class V>
	void assignWasmToNative(WasmP wasmP, V &native) {
		wclap.translator<use64>().assignWasmToNative(this, wasmP, native);
	}
	template<class V>
	void assignNativeToWasm(const V &native, WasmP wasmP) {
		wclap.translator<use64>().assignNativeToWasm(this, native, wasmP);
	}

	//---------- custom translators for specific types ----------//

	void assignWasmToNative_t(WasmP wasmP, const void * &native) {
		LOG_EXPR("assignWasmToNative_t: const void *");
		auto *nativeUnknown = (WasmPointerUnknown *)temporaryNativeBytes(sizeof(WasmPointerUnknown), alignof(WasmPointerUnknown));
		nativeUnknown->wasmP = wasmP;
		native = nativeUnknown;
	}
	void assignNativeToWasm_t(const void * const &native, WasmP wasmP) {
		LOG_EXPR("assignNativeToWasm_t: const void *");
		auto *nativeUnknown = (WasmPointerUnknown *)native;
		auto &wasmVoidPointer = *(WasmP *)nativeInWasm(wasmP);
		wasmVoidPointer = (WasmP)nativeUnknown->wasmP; // WasmPointerUnknown always holds 64-bit, but it could be only 32
	}

	void assignWasmToNative_t(WasmP wasmP, const char * &native) {
		auto wasmString = *(WasmP *)nativeInWasm(wasmP);
		
		if (!wasmString) {
			native = nullptr;
		} else {
			native = (const char *)nativeInWasm(wasmString);
		}
	}
	void assignNativeToWasm_t(const char * const &native, WasmP wasmP) {
		auto &wasmString = *(WasmP *)nativeInWasm(wasmP);
		
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

	//---------- custom translators for individual fields ----------//

	void assignWasmToNative_clap_plugin_descriptor_t_features(WasmP wasmP, const char * const * &constFeatures) {
		// This is our object - the constness is for the host using this value
		auto features = (const char**)constFeatures;
		if (!wasmP) {
			// TODO: not sure if this is a valid value.  If not, should we fix it, or pass it on?
			*features = nullptr;
			return;
		}
		
		WasmP wasmStringArray = valueInWasm<WasmP>(wasmP);
		size_t featureCount = 0;
		while (1) { // TODO: maximum feature count for sanity/safety?
			WasmP wasmString = valueInWasm<WasmP>(wasmStringArray + featureCount);
			if (!wasmString) break; // list is null-terminated
			++featureCount;
		};
		
		features = (const char **)temporaryNativeBytes(sizeof(const char*)*(featureCount + 1), alignof(const char*));
		for (size_t i = 0; i < featureCount; ++i) {
			WasmP wasmString = valueInWasm<WasmP>(wasmStringArray + featureCount);
			assignWasmToNative(wasmString, features[i]);
		}
		features[featureCount] = nullptr;
		commitNative();
	}
	// Not sure why the host would ever pass a plugin feature-list back *into* the WASM, but 🤷
	void assignNativeToWasm_clap_plugin_descriptor_t_features(const char * const * const &features, WasmP wasmP) {
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
		WasmP &wasmStringArray = valueInWasm<WasmP>(wasmP);
		wasmStringArray = temporaryWasmBytes(sizeof(WasmP)*(featureCount + 1), alignof(WasmP));

		for (size_t i = 0; i < featureCount; ++i) {
			assignNativeToWasm(features[i], wasmStringArray + i*sizeof(WasmP));
		}
		valueInWasm<WasmP>(wasmStringArray + featureCount*sizeof(WasmP)) = 0;
		commitWasm();
	}
private:
	bool _wasmReadyToDestroy = false;
};

// Default translator bounces back to `WclapTranslationScope::assign???_t()` for explicit custom translation
template<class NativeClapStruct>
struct Wclap32TranslateStruct {
	using WasmP = uint32_t;
	static void assignWasmToNative(WclapTranslationScope<false> *translate, WasmP wasmP, NativeClapStruct &native) {
		translate->assignWasmToNative_t(wasmP, native);
	}
	static void assignNativeToWasm(WclapTranslationScope<false> *translate, NativeClapStruct const &native, WasmP wasmP) {
		translate->assignNativeToWasm_t(native, wasmP);
	}
};
template<class NativeClapStruct>
struct Wclap64TranslateStruct {
	using WasmP = uint64_t;
	static void assignWasmToNative(WclapTranslationScope<true> *translate, WasmP wasmP, NativeClapStruct &native) {
		translate->assignWasmToNative_t(wasmP, native);
	}
	static void assignNativeToWasm(WclapTranslationScope<true> *translate, NativeClapStruct const &native, WasmP wasmP) {
		translate->assignNativeToWasm_t(native, wasmP);
	}
};

// Auto-generated struct translation
#include "./wclap32-translate-struct.generated.h"
#include "./wclap64-translate-struct.generated.h"
