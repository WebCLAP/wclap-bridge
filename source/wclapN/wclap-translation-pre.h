/// NO #pragma once or include guard - this is included twice from within two different namespaces
#ifndef WCLAP_MULTIPLE_INCLUDES_NAMESPACE
#	error must not be included directly
#endif
// `WasmP` is defined in the namespace, but nothing else

#include "../wclap-thread.h"

namespace wclap { namespace WCLAP_MULTIPLE_INCLUDES_NAMESPACE {

// This is what we store in the `void *` context fields of native proxies
struct NativeProxyContext {
	Wclap *wclap = nullptr;
	WasmP wasmObjP = 0;
	std::unique_ptr<WclapThread> realtimeThread = nullptr;

	NativeProxyContext() {}
	NativeProxyContext(Wclap *wclap, WasmP wasmObjP) : wclap(wclap), wasmObjP(wasmObjP) {}
	NativeProxyContext(NativeProxyContext &&other) {
		*this = std::move(other);
	}
	
	NativeProxyContext & operator=(NativeProxyContext &&other) {
		wclap = other.wclap;
		wasmObjP = other.wasmObjP;
		realtimeThread = std::move(other.realtimeThread);
		return *this;
	}
};

template<class NativeT>
void wasmToNative(WclapArenas &arenas, WasmP wasmP, NativeT *&native);
template<class NativeT>
void nativeToWasm(WclapArenas &arenas, NativeT *native, WasmP &wasmP);
template<class NativeT>
const NativeProxyContext & getNativeProxyContext(const NativeT *native);

// non-struct specialisations

template<>
void nativeToWasm<const char>(WclapArenas &arenas, const char *str, WasmP &wasmP);
template<>
void wasmToNative<const char>(WclapArenas &arenas, WasmP wasmStr, const char *&str);
template<>
void wasmToNative<const char * const>(WclapArenas &arenas, WasmP stringList, const char * const * &features);

}} // namespace
