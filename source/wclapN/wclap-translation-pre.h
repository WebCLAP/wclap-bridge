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
	std::unique_ptr<WclapArenas> arenas = nullptr;
	std::unique_ptr<WclapThread> realtimeThread = nullptr;

	NativeProxyContext() {}
	NativeProxyContext(Wclap *wclap, WasmP wasmObjP) : wclap(wclap), wasmObjP(wasmObjP) {}
	NativeProxyContext(Wclap *wclap, WasmP wasmObjP, std::unique_ptr<WclapArenas> &&arenas) : wclap(wclap), wasmObjP(wasmObjP), arenas(std::move(arenas)) {}
	NativeProxyContext(Wclap *wclap, WasmP wasmObjP, std::unique_ptr<WclapArenas> &&arenas, std::unique_ptr<WclapThread> &&rtThread) : wclap(wclap), wasmObjP(wasmObjP), arenas(std::move(arenas)), realtimeThread(std::move(rtThread)) {}

	// Move only, no copy
	NativeProxyContext(NativeProxyContext &&other) {
		*this = std::move(other);
	}
	NativeProxyContext & operator=(NativeProxyContext &&other) {
		wclap = other.wclap;
		wasmObjP = other.wasmObjP;
		arenas = std::move(other.arenas);
		realtimeThread = std::move(other.realtimeThread);
		return *this;
	}
	
	// Call when the native proxy is destroyed
	void reset() {
		if (!wclap) {
			LOG_EXPR("NativeProxyContext::reset()");
			LOG_EXPR(wclap);
			abort();
		}
		if (arenas) wclap->returnArenas(arenas);
		if (realtimeThread) wclap->returnRealtimeThread(realtimeThread);
	}
};

template<class NativeT>
void wasmToNative(WclapArenas &arenas, WasmP wasmP, NativeT *&native);
template<class NativeT>
NativeT * wasmToNative(WclapArenas &arenas, WasmP wasmP) {
	NativeT *native;
	wasmToNative(arenas, wasmP, native);
	return native;
}
template<class NativeT>
void nativeToWasm(WclapArenas &arenas, NativeT *native, WasmP &wasmP);
template<class NativeT>
WasmP nativeToWasm(WclapArenas &arenas, NativeT *native) {
	WasmP wasmP;
	nativeToWasm(arenas, native, wasmP);
	return wasmP;
}

template<class NativeT>
NativeProxyContext & nativeProxyContextFor(const NativeT *native);
template<class WclapStruct>
void setWasmProxyContext(WclapArenas &arenas, WasmP wasmP);

// non-struct specialisations

template<>
void nativeToWasm<const char>(WclapArenas &arenas, const char *str, WasmP &wasmP);
template<>
void wasmToNative<const char>(WclapArenas &arenas, WasmP wasmStr, const char *&str);
template<>
void wasmToNative<const char * const>(WclapArenas &arenas, WasmP stringList, const char * const * &features);

}} // namespace
