#pragma once

#include "clap/all.h"

#include "./wclap-arenas.h"

#include <fstream>
#include <vector>
#include <mutex>

namespace wclap {

struct Wclap;
void wclapSetError(Wclap &, const char *message);

struct WclapThread {
	Wclap &wclap;
	std::mutex mutex;

	uint64_t clapEntryP64 = 0; // WASM pointer to clap_entry - might actually be 32-bit
	
	WclapThread(Wclap &wclap) : wclap(wclap) {
		implCreate();
		startInstance();
	}
	~WclapThread() {
		implDestroy();
	}

	void wasmInit();

	uint64_t wasmMalloc(size_t bytes);

	/* Function call return types:
		V: void
		I: int32
		L: int64
		F: float
		D: double
		P: pointer (deduce from the function-pointer size)
	*/

	void callWasm_V(uint64_t fnP);
	template<typename ...Args>
	void callWasm_V(uint64_t fnP, Args ...args);

	int32_t callWasm_I(uint64_t fnP);
	template<typename ...Args>
	int32_t callWasm_I(uint64_t fnP, Args ...args);
	
	int64_t callWasm_L(uint64_t fnP);
	template<typename ...Args>
	int64_t callWasm_L(uint64_t fnP, Args ...args);

	float callWasm_F(uint64_t fnP);
	template<typename ...Args>
	float callWasm_F(uint64_t fnP, Args ...args);

	double callWasm_D(uint64_t fnP);
	template<typename ...Args>
	double callWasm_D(uint64_t fnP, Args ...args);
	
// generates all 1700 callWasm_? templates up to 4 arguments
#define WCLAP_CALLWASM_ARG4(Template, prefix, typeP, type0, type1, type2, type3, type4) // stop here

#define WCLAP_CALLWASM_ARG3(Template, prefix, typeP, type0, type1, type2, type3) \
	Template void prefix callWasm_V<type0, type1, type2, type3>(typeP, type0, type1, type2, type3); \
	Template int32_t prefix callWasm_I<type0, type1, type2, type3>(typeP, type0, type1, type2, type3); \
	Template int64_t prefix callWasm_L<type0, type1, type2, type3>(typeP, type0, type1, type2, type3); \
	Template float prefix callWasm_F<type0, type1, type2, type3>(typeP, type0, type1, type2, type3); \
	Template double prefix callWasm_D<type0, type1, type2, type3>(typeP, type0, type1, type2, type3); \
	WCLAP_CALLWASM_ARG4(Template, prefix, typeP, type0, type1, type2, type3, uint32_t)\
	WCLAP_CALLWASM_ARG4(Template, prefix, typeP, type0, type1, type2, type3, uint64_t)\
	WCLAP_CALLWASM_ARG4(Template, prefix, typeP, type0, type1, type2, type3, float)\
	WCLAP_CALLWASM_ARG4(Template, prefix, typeP, type0, type1, type2, type3, double)

#define WCLAP_CALLWASM_ARG2(Template, prefix, typeP, type0, type1, type2) \
	Template void prefix callWasm_V<type0, type1, type2>(typeP, type0, type1, type2); \
	Template int32_t prefix callWasm_I<type0, type1, type2>(typeP, type0, type1, type2); \
	Template int64_t prefix callWasm_L<type0, type1, type2>(typeP, type0, type1, type2); \
	Template float prefix callWasm_F<type0, type1, type2>(typeP, type0, type1, type2); \
	Template double prefix callWasm_D<type0, type1, type2>(typeP, type0, type1, type2); \
	WCLAP_CALLWASM_ARG3(Template, prefix, typeP, type0, type1, type2, uint32_t)\
	WCLAP_CALLWASM_ARG3(Template, prefix, typeP, type0, type1, type2, uint64_t)\
	WCLAP_CALLWASM_ARG3(Template, prefix, typeP, type0, type1, type2, float)\
	WCLAP_CALLWASM_ARG3(Template, prefix, typeP, type0, type1, type2, double)

#define WCLAP_CALLWASM_ARG1(Template, prefix, typeP, type0, type1) \
	Template void prefix callWasm_V<type0, type1>(typeP, type0, type1); \
	Template int32_t prefix callWasm_I<type0, type1>(typeP, type0, type1); \
	Template int64_t prefix callWasm_L<type0, type1>(typeP, type0, type1); \
	Template float prefix callWasm_F<type0, type1>(typeP, type0, type1); \
	Template double prefix callWasm_D<type0, type1>(typeP, type0, type1); \
	WCLAP_CALLWASM_ARG2(Template, prefix, typeP, type0, type1, uint32_t)\
	WCLAP_CALLWASM_ARG2(Template, prefix, typeP, type0, type1, uint64_t)\
	WCLAP_CALLWASM_ARG2(Template, prefix, typeP, type0, type1, float)\
	WCLAP_CALLWASM_ARG2(Template, prefix, typeP, type0, type1, double)

#define WCLAP_CALLWASM_ARG0(Template, prefix, typeP, type0) \
	Template void prefix callWasm_V<type0>(typeP, type0); \
	Template int32_t prefix callWasm_I<type0>(typeP, type0); \
	Template int64_t prefix callWasm_L<type0>(typeP, type0); \
	Template float prefix callWasm_F<type0>(typeP, type0); \
	Template double prefix callWasm_D<type0>(typeP, type0); \
	WCLAP_CALLWASM_ARG1(Template, prefix, typeP, type0, uint32_t)\
	WCLAP_CALLWASM_ARG1(Template, prefix, typeP, type0, uint64_t)\
	WCLAP_CALLWASM_ARG1(Template, prefix, typeP, type0, float)\
	WCLAP_CALLWASM_ARG1(Template, prefix, typeP, type0, double)

#define WCLAP_CALLWASM_PTYPE(Template, prefix, typeP) \
	WCLAP_CALLWASM_ARG0(Template, prefix, typeP, uint32_t) \
	WCLAP_CALLWASM_ARG0(Template, prefix, typeP, uint64_t) \
	WCLAP_CALLWASM_ARG0(Template, prefix, typeP, float) \
	WCLAP_CALLWASM_ARG0(Template, prefix, typeP, double)

// Everything gets cast to 64-bit index - only the `callWasm_P()` ones care about the function pointer type
#define WCLAP_CALLWASM_SPECIALISE(Template, prefix) \
	WCLAP_CALLWASM_PTYPE(Template, prefix, uint64_t)

	WCLAP_CALLWASM_SPECIALISE(template<>,);

	template<class ...Args>
	uint32_t callWasm_P(uint32_t fnP, Args ...args) {
		return callWasm_I(uint64_t(fnP), std::forward<Args>(args)...);
	}
	template<class ...Args>
	uint64_t callWasm_P(uint64_t fnP, Args ...args) {
		return callWasm_L(fnP, std::forward<Args>(args)...);
	}

	template<void nativeFn(uint32_t), typename WasmP>
	void registerFunction(WasmP &fnP);
	template<uint32_t nativeFn(uint32_t, uint32_t), typename WasmP>
	void registerFunction(WasmP &fnP);
	template<void nativeFn(uint64_t), typename WasmP>
	void registerFunction(WasmP &fnP);
	template<uint64_t nativeFn(uint64_t, uint64_t), typename WasmP>
	void registerFunction(WasmP &fnP);
private:
	friend class Wclap;

	void startInstance();

	struct Impl;
	Impl *impl;
	void implCreate();
	void implDestroy();
};

struct WclapThreadWithArenas : public WclapThread {
	std::unique_ptr<WclapArenas> arenas;

	WclapThreadWithArenas(Wclap &wclap);
};

} // namespace

