/// NO #pragma once or include guard - this is included twice from within two different namespaces
#ifndef WCLAP_MULTIPLE_INCLUDES_NAMESPACE
#	error must not be included directly
#endif
// `WasmP` is defined in the namespace, but nothing else

#include "../scoped-thread.h"
#include "../wclap-thread.h"
#include "../wclap-arenas.h"

#include <cstring>

#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

namespace wclap { namespace WCLAP_MULTIPLE_INCLUDES_NAMESPACE {

// This is what we store in the `void *` context fields of native proxies, and associate with the arenas stored in the WASM proxies
struct NativeProxyContext {
	Wclap *wclap = nullptr;
	std::unique_ptr<WclapArenas> arenas = nullptr;
	std::unique_ptr<WclapThread> realtimeThread = nullptr;
	
	// This context can be associated with at most one WASM value (e.g. clap_plugin) of each type
	struct WasmMap {
		WasmP plugin = 0;
		WasmP plugin_ambisonic = 0;
		WasmP plugin_audio_ports = 0;
		WasmP plugin_audio_ports_activation = 0;
		WasmP plugin_audio_ports_config = 0;
		WasmP plugin_audio_ports_config_info = 0;
		WasmP plugin_configurable_audio_ports = 0;
		WasmP context_menu_builder = 0;
		WasmP plugin_context_menu = 0;
		WasmP plugin_gui = 0;
		WasmP plugin_latency = 0;
		WasmP plugin_note_name = 0;
		WasmP plugin_note_ports = 0;
		WasmP plugin_params = 0;
		WasmP plugin_param_indication = 0;
		WasmP plugin_preset_load = 0;
		WasmP plugin_remote_controls = 0;
		WasmP plugin_render = 0;
		WasmP plugin_state = 0;
		WasmP plugin_state_context = 0;
		WasmP plugin_surround = 0;
		WasmP plugin_tail = 0;
		WasmP plugin_thread_pool = 0;
		WasmP plugin_timer_support = 0;
		WasmP plugin_track_info = 0;
		WasmP plugin_voice_info = 0;
		WasmP plugin_webview = 0;
		
		WasmP input_events = 0, output_events = 0;
		
		WasmP istream = 0, ostream = 0;
		
		WasmP preset_discovery_provider = 0;
		WasmP preset_discovery_indexer = 0;
		
		// Anything we don't expect to actually use here, but might still want translatable
		union {
			WasmP plugin_entry;
			WasmP plugin_factory;
			WasmP preset_discovery_factory;
			WasmP preset_discovery_metadata_receiver;
		};
	} wasmMap;
	struct NativeMap {
		const clap_host *host = nullptr;
		const clap_host_log *host_log = nullptr;
	} nativeMap;

	NativeProxyContext() {}
	NativeProxyContext(Wclap *wclap) : wclap(wclap) {}
	NativeProxyContext(Wclap *wclap, std::unique_ptr<WclapArenas> &&arenas) : wclap(wclap), arenas(std::move(arenas)) {}
	NativeProxyContext(Wclap *wclap, std::unique_ptr<WclapArenas> &&arenas, std::unique_ptr<WclapThread> &&rtThread) : wclap(wclap), arenas(std::move(arenas)), realtimeThread(std::move(rtThread)) {
		arenas->currentContext = this;
	}

	// Move only, no copy
	NativeProxyContext(NativeProxyContext &&other) {
		*this = std::move(other);
	}
	NativeProxyContext & operator=(NativeProxyContext &&other) {
		wclap = other.wclap;
		arenas = std::move(other.arenas);
		arenas->currentContext = this;
		realtimeThread = std::move(other.realtimeThread);
		wasmMap = other.wasmMap;
		nativeMap = other.nativeMap;
		return *this;
	}
	
	// Only gets called when it's a temporary variable on the stack, or the whole WASM instance/module is being destroyed
	~NativeProxyContext() {
		reset();
	}

	static NativeProxyContext claimRealtime(Wclap &wclap) {
		auto rtThread = wclap.claimRealtimeThread();
		auto arenas = wclap.claimArenas(rtThread.get());
		return {&wclap, std::move(arenas), std::move(rtThread)};
	}
	
	ScopedThread lock(bool realtime=false) const;

	// Call when the native proxy is destroyed
	void reset() {
		if (!wclap) {
			LOG_EXPR("NativeProxyContext::reset()");
			LOG_EXPR(wclap);
			abort();
		}
		wasmMap = {};
		nativeMap = {};
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
	if (length > 0) std::memcpy(inWasm, native, length*sizeof(DirectT));
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
