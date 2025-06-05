/// NO #pragma once or include guard - this is included twice from within two different namespaces
#ifndef WCLAP_MULTIPLE_INCLUDES_NAMESPACE
#	error must not be included directly
#endif
// `WasmP` is defined in the namespace, but nothing else

#include "../scoped-thread.h"
#include "../wclap-thread.h"
#include "../wclap-arenas.h"

#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

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
	
	// Only gets called when it's a temporary variable on the stack
	~NativeProxyContext() {
		reset();
	}

	static NativeProxyContext claimRealtime(Wclap &wclap, WasmP wasmObjP=0) {
		auto rtThread = wclap.claimRealtimeThread();
		auto arenas = wclap.claimArenas(rtThread.get());
		return {&wclap, wasmObjP, std::move(arenas), std::move(rtThread)};
	}
	
	ScopedThread lock(bool realtime=false) const;

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
void wasmToNative(ScopedThread &scoped, WasmP wasmP, NativeT *&native);
template<class NativeT>
NativeT * wasmToNative(ScopedThread &scoped, WasmP wasmP) {
	const NativeT *native;
	wasmToNative(scoped, wasmP, native);
	return (NativeT *)native;
}
template<class NativeT>
void nativeToWasm(ScopedThread &scoped, NativeT *native, WasmP &wasmP);
template<class NativeT>
WasmP nativeToWasm(ScopedThread &scoped, NativeT *native) {
	WasmP wasmP;
	nativeToWasm(scoped, native, wasmP);
	return wasmP;
}
template<class DirectT>
void nativeToWasmDirectArray(ScopedThread &scoped, const DirectT *native, WasmP &wasmP, size_t length) {
	if (!native) {
		wasmP = 0;
		return;
	}
	auto *inWasm = scoped.createDirectArray<DirectT>(length, wasmP);
	for (size_t i = 0; i < length; ++i) {
		inWasm[i] = native[i];
	}
}

template<class NativeT>
void * & nativeProxyContextPointer(const NativeT *native);
// WASM equivalent is wclap.arenasForWasmContext(wasmContextP), which is much more consistent

template<class NativeT, class ...Args>
NativeProxyContext & createNativeProxyContext(ScopedThread &scoped, NativeT *v, Args &&...args) {
	void *&ptr = nativeProxyContextPointer(v);
	if (ptr) {
		LOG_EXPR("context fields should be NULL before they're created");
		abort();
	}
	ptr = scoped.arenas.nativeTyped<NativeProxyContext>();
	// Call proper constructor
	return *(new(ptr) NativeProxyContext(std::forward<Args>(args)...));
}

template<class NativeT>
const NativeProxyContext & getNativeProxyContext(const NativeT *native) {
	return *(const NativeProxyContext *)nativeProxyContextPointer(native);
}

template<class NativeT>
void destroyNativeProxyContext(const NativeT *native) {
	void *&ptr = nativeProxyContextPointer(native);
	auto *context = (NativeProxyContext *)ptr;
	ptr = nullptr;
	context->~NativeProxyContext();
}

//---------- non-struct specialisations ----------

template<>
void nativeToWasm<const char>(ScopedThread &scoped, const char *str, WasmP &wasmP);
template<>
void wasmToNative<const char>(ScopedThread &scoped, WasmP wasmStr, const char *&str);
template<>
void wasmToNative<const char * const>(ScopedThread &scoped, WasmP stringList, const char * const * &features);

}} // namespace
