#pragma once

#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

// 32-bit only (for now)
namespace wclap { namespace wclap32 {
	struct WclapTranslationScope;
	
	class WclapStructView {
	protected:
		unsigned char *pointerInWasm;
		WclapTranslationScope &translationScope;
	public:
		WclapStructView(unsigned char *p, WclapTranslationScope &scope) : pointerInWasm(p), translationScope(scope) {}
	};

}}
#include "clap/all.h"
#include "./wclap32-translate-struct.generated.h"

#include <type_traits>
#include <unordered_map>

struct Wclap;
struct WclapThread;

namespace wclap {namespace wclap32 {

struct WclapContext {
	WasmP wasmP = 0;
	Wclap *wclap = nullptr;
};

template<class ClapStruct>
struct ClapStructWithContext : public ClapStruct {
	using ClapStruct::ClapStruct;
	WclapContext wclapContext;
};

struct WclapMethods {
	struct {
		static uint32_t get_plugin_count(const struct clap_plugin_factory *factory) {
			
		}
	} plugin_factory;
};

/* Manages function calls and translating values across the boundary.

	Owns a chunk of WASM memory, from the WCLAP's malloc().

	Since free() isn't exposed, this object should be active until the WASM memory is destroyed.  When the bound object is destroyed, it should be returned to the Wclap's pool.
*/
struct WclapTranslationScope {
	static constexpr size_t arenaBytes = 65536;

	Wclap &wclap;
	WclapMethods &methods;
	
	WclapTranslationScope(Wclap &wclap, WclapThread &currentThread, WclapMethods &methods);
	~WclapTranslationScope();
	
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
	
	template<class AutoTranslatedStruct>
	AutoTranslatedStruct get(WasmP wasmP) {
		return WclapStruct(nativeInWasm(wasmP), *this);
	}

	void assignWasmToNative(WasmP wasmP, ClapStructWithContext<clap_plugin_factory> &native) {
		LOG_EXPR("assignWasmToNative: clap_plugin_factory");

		// extra info so we can find ourselves again - usually this would be in a struct pointed to by the `void *` context pointer
		native.wclapContext = {wasmP, &wclap};
		
		//auto wasmFactory = get<wclap_plugin_factory>(wasmP);
		native.get_plugin_count = methods.plugin_factory.get_plugin_count;
		native.get_plugin_descriptor = methods.plugin_factory.get_plugin_descriptor;
		native.create_plugin = methods.plugin_factory.create_plugin;
	}

	/*
	//---------- The main two categories of translators: simple or generated ----------//

	template<class T>
	T & valueInWasm(WasmP wasmP) {
		return *(T *)nativeInWasm(wasmP);
	}

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
	
	void assignWasmToNative(WasmP wasmP, const void * &native) {
		LOG_EXPR("assignWasmToNative_t: const void *");
		auto *nativeUnknown = (WasmPointerUnknown *)temporaryNativeBytes(sizeof(WasmPointerUnknown), alignof(WasmPointerUnknown));
		nativeUnknown->wasmP = wasmP;
		native = nativeUnknown;
	}
	void assignNativeToWasm(const void * const &native, WasmP wasmP) {
		LOG_EXPR("assignNativeToWasm_t: const void *");
		auto *nativeUnknown = (WasmPointerUnknown *)native;
		auto &wasmVoidPointer = *(WasmP *)nativeInWasm(wasmP);
		wasmVoidPointer = (WasmP)nativeUnknown->wasmP;
	}

	void assignWasmToNative(WasmP wasmP, const char * &native) {
		auto wasmString = *(WasmP *)nativeInWasm(wasmP);
		
		if (!wasmString) {
			native = nullptr;
		} else {
			native = (const char *)nativeInWasm(wasmString);
		}
	}
	void assignNativeToWasm(const char * const &native, WasmP wasmP) {
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
	*/
private:
	unsigned char * nativeInWasm(WasmP wasmP);
	
	bool _wasmReadyToDestroy = false;
};

}} // namespace
