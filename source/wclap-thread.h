#pragma once

#include "clap/all.h"

#include "./wclap-arenas.h"

#ifdef WCLAP_ENGINE_WASMTIME
#	include "./wasmtime/call-wasm.h"
#else
#	error No WASM engine selected
#endif

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
	
	template<typename ...Args>
	void callWasm_V(uint64_t fnP, Args ...args) {
		_impl::callWasm_V(*this, fnP, args...);
	}
	template<typename ...Args>
	int32_t callWasm_I(uint64_t fnP, Args ...args) {
		return _impl::callWasm_I(*this, fnP, args...);
	}
	template<typename ...Args>
	int64_t callWasm_L(uint64_t fnP, Args ...args) {
		return _impl::callWasm_L(*this, fnP, args...);
	}
	template<typename ...Args>
	float callWasm_F(uint64_t fnP, Args ...args) {
		return _impl::callWasm_F(*this, fnP, args...);
	}
	template<typename ...Args>
	double callWasm_D(uint64_t fnP, Args ...args) {
		return _impl::callWasm_D(*this, fnP, args...);
	}
	
	template<class ...Args>
	uint32_t callWasm_P(uint32_t fnP, Args ...args) {
		return callWasm_I(uint64_t(fnP), std::forward<Args>(args)...);
	}
	template<class ...Args>
	uint64_t callWasm_P(uint64_t fnP, Args ...args) {
		return callWasm_L(fnP, std::forward<Args>(args)...);
	}

	template<typename FnStruct>
	void registerFunction(FnStruct &fnStruct) {
		_impl::registerFunction<FnStruct::native>(*this, fnStruct.wasmP);
	}

	struct Impl;
	Impl *impl;

private:
	void startInstance();

	void implCreate();
	void implDestroy();
};

struct WclapThreadWithArenas : public WclapThread {
	std::unique_ptr<WclapArenas> arenas;

	WclapThreadWithArenas(Wclap &wclap);
};

} // namespace

