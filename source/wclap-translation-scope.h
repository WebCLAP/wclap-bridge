#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "clap/all.h"
#include "wasmtime.h"

#include <type_traits>

struct Wclap;

template<class NativeClapStruct>
struct Wclap32TranslateStruct;
template<class NativeClapStruct>
struct Wclap64TranslateStruct;

template<bool use64>
struct WclapTranslationScope {
	using WasmP = typename std::conditional<use64, uint64_t, uint32_t>::type;

	template<class NativeClapStruct>
	using TranslateStruct = typename std::conditional<use64, Wclap64TranslateStruct<NativeClapStruct>, Wclap32TranslateStruct<NativeClapStruct>>::type;

	static constexpr size_t arenaBytes = 65536;

	Wclap &wclap;
	wasm_memory_t *memory;
	WasmP wasmObjectP; // WASM object whose lifetime this scope is tied to
	WasmP wasmPointerToThis; // If we point WASM context fields to here, we can find this

	WclapTranslationScope(Wclap &wclap, wasm_memory_t *memory, WasmP wasmObjectP) : wclap(wclap), memory(memory), wasmObjectP(wasmObjectP) {
		nativeArena = nativeTmpStartP = nativeTmpP = (unsigned char *)malloc(arenaBytes);
LOG_EXPR(nativeArena);
//		wasmArena = wasmTmpStartP = wasmTmpP = wasm_malloc(arenaBytes);
wasmArena = wasmTmpStartP = wasmTmpP = 0;
abort();

		wasmPointerToThis = temporaryWasmBytes(sizeof(this), alignof(decltype(this)));
		*(WclapTranslationScope **)nativeInWasm(wasmPointerToThis) = this;
		commitWasm();
	}
	
	// Should only happen when the WASM instance is destroyed - otherwise it should be returned to a pool (since we can't free the arena memory)
	void wasmReadyToDestroy() {
		_wasmReadyToDestroy = true;
	}
	~WclapTranslationScope() {
		if (!_wasmReadyToDestroy) {
			LOG_EXPR(_wasmReadyToDestroy);
			abort();
		}
		free(nativeArena);
	}
	
	void * nativeInWasm(WasmP wasmP) {
		// TODO: bounds-check
		return wasm_memory_data(memory) + wasmP;
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
	
	template<class V>
	void assignWasmToNative(WasmP wasmP, V &native);
	template<class V>
	void assignNativeToWasm(const V &native, WasmP wasmP);
	
	template<class Return, class Arg1>
	void assignWasmToNative_II(WasmP wasmP, Return(*&fnPointer)(Arg1)) {
		fnPointer = nullptr;
	}
	template<class Return, class Arg1>
	void assignNativeToWasm_II(Return(* const &fnPointer)(Arg1), WasmP wasmP) {
		valueInWasm<WasmP>(wasmP) = 0;
	}
	
	template<class Return, class Arg1>
	void assignWasmToNative_IL(WasmP wasmP, Return(*&fnPointer)(Arg1)) {
		fnPointer = nullptr;
	}
	template<class Return, class Arg1>
	void assignNativeToWasm_IL(Return(* const &fnPointer)(Arg1), WasmP wasmP) {
		valueInWasm<WasmP>(wasmP) = 0;
	}

	template<class Return, class Arg1, class Arg2>
	void assignWasmToNative_III(WasmP wasmP, Return(*&fnPointer)(Arg1, Arg2)) {
		fnPointer = nullptr;
	}
	template<class Return, class Arg1, class Arg2>
	void assignNativeToWasm_III(Return(* const &fnPointer)(Arg1, Arg2), WasmP wasmP) {
		valueInWasm<WasmP>(wasmP) = 0;
	}

	template<class Return, class Arg1, class Arg2>
	void assignWasmToNative_LLI(WasmP wasmP, Return(*&fnPointer)(Arg1, Arg2)) {
		fnPointer = nullptr;
	}
	template<class Return, class Arg1, class Arg2>
	void assignNativeToWasm_LLI(Return(* const &fnPointer)(Arg1, Arg2), WasmP wasmP) {
		valueInWasm<WasmP>(wasmP) = 0;
	}

	template<class Return, class Arg1, class Arg2, class Arg3>
	void assignWasmToNative_IIII(WasmP wasmP, Return(*&fnPointer)(Arg1, Arg2, Arg3)) {
		fnPointer = nullptr;
	}
	template<class Return, class Arg1, class Arg2, class Arg3>
	void assignNativeToWasm_IIII(Return(* const &fnPointer)(Arg1, Arg2, Arg3), WasmP wasmP) {
		valueInWasm<WasmP>(wasmP) = 0;
	}

	template<class Return, class Arg1, class Arg2, class Arg3>
	void assignWasmToNative_LLLL(WasmP wasmP, Return(*&fnPointer)(Arg1, Arg2, Arg3)) {
		fnPointer = nullptr;
	}
	template<class Return, class Arg1, class Arg2, class Arg3>
	void assignNativeToWasm_LLLL(Return(* const &fnPointer)(Arg1, Arg2, Arg3), WasmP wasmP) {
		valueInWasm<WasmP>(wasmP) = 0;
	}

	//---------- custom translators for specific types ----------//

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

// Auto-generated struct translation
#include "./wclap32-translate-struct.generated.h"
#include "./wclap64-translate-struct.generated.h"
