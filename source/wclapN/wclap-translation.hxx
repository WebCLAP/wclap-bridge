/// NO #pragma once or include guard - this is included twice from within two different namespaces
#ifndef WCLAP_MULTIPLE_INCLUDES_NAMESPACE
#	error must not be included directly
#endif
// The matching `wclap-translation.h` will already be included

#include "../wclap-thread.h"
#include "../wclap.h"

#include "clap/all.h"
#include <vector>

#ifndef WCLAP_MAX_STRING_LENGTH
#	define WCLAP_MAX_STRING_LENGTH 2048
#endif
#ifndef WCLAP_MAX_FEATURES_LENGTH
#	define WCLAP_MAX_FEATURES_LENGTH 1000
#endif

namespace wclap { namespace WCLAP_MULTIPLE_INCLUDES_NAMESPACE {

size_t strlen(const char *str, size_t maxLength=WCLAP_MAX_STRING_LENGTH) {
	if (!str) return 0;
	size_t length = 0;
	if (maxLength > WCLAP_MAX_STRING_LENGTH) maxLength = WCLAP_MAX_STRING_LENGTH;
	while (str[length] && length < maxLength) ++length;
	return length;
}

ScopedThread NativeProxyContext::lock(bool realtime) const {
	if (realtime) {
		return wclap->lockThread(realtimeThread.get(), *arenas);
	} else {
		return wclap->lockThread();
	}
}

struct WclapMethods {
	Wclap &wclap;
	bool initSuccess = false;
	
	WclapMethods(Wclap &wclap) : wclap(wclap) {}
	
	bool initClapEntry() {
		auto global = wclap.lockGlobalThread();
		auto wasmEntry = global.view<wclap_plugin_entry>(global.thread.clapEntryP64);
		auto initFn = wasmEntry.init();

		auto reset = global.arenas.scopedWasmReset();
		WasmP wasmStr = nativeToWasm(global, "/plugin/");
		initSuccess = global.thread.callWasm_I(initFn, wasmStr);
		return initSuccess;
	}
	
	void deinitClapEntry() {
		if (!initSuccess) return;
		auto global = wclap.lockGlobalThread();
		auto wasmEntry = global.view<wclap_plugin_entry>(global.thread.clapEntryP64);
		auto deinitFn = wasmEntry.deinit();
		global.thread.callWasm_V(deinitFn);
	}

	struct plugin_factory : public clap_plugin_factory {
		Wclap &wclap;
		WasmP factoryObjP;
	
		std::vector<const clap_plugin_descriptor *> descriptorPointers;
		
		plugin_factory(Wclap &wclap, WasmP factoryObjP) : wclap(wclap), factoryObjP(factoryObjP) {
			this->get_plugin_count = native_get_plugin_count;
			this->get_plugin_descriptor = native_get_plugin_descriptor;
			this->create_plugin = native_create_plugin;

			auto global = wclap.lockGlobalThread();
			
			auto wasmFactory = global.view<wclap_plugin_factory>(factoryObjP);
			auto getPluginCountFn = wasmFactory.get_plugin_count();
			auto getPluginDescFn = wasmFactory.get_plugin_descriptor();

			auto count = global.thread.callWasm_I(getPluginCountFn, factoryObjP);
			
			descriptorPointers.clear();
			for (uint32_t i = 0; i < count; ++i) {
				auto wasmP = global.thread.callWasm_P(getPluginDescFn, factoryObjP, i);
				auto *desc = wasmToNative<const clap_plugin_descriptor>(global, wasmP);
				descriptorPointers.push_back(desc);
			}
			// Keep those descriptors for the lifetime of the WCLAP module
			global.arenas.persistNative();
		}
	
		static uint32_t native_get_plugin_count(const struct clap_plugin_factory *obj) {
			auto &factory = *(const plugin_factory *)obj;
			return uint32_t(factory.descriptorPointers.size());
		}
		static const clap_plugin_descriptor_t * native_get_plugin_descriptor(const struct clap_plugin_factory *obj, uint32_t index) {
			auto &factory = *(const plugin_factory *)obj;
			if (index < factory.descriptorPointers.size()) {
				return factory.descriptorPointers[index];
			}
			return nullptr;
		}
		static const clap_plugin_t * native_create_plugin(const struct clap_plugin_factory *obj, const clap_host *host, const char *plugin_id) {
			auto &factory = *(const plugin_factory *)obj;
			auto &wclap = factory.wclap;
			
			auto context = NativeProxyContext::claimRealtime(wclap);
			auto scoped = context.lock(true);
			auto createPluginFn = scoped.view<wclap_plugin_factory>(factory.factoryObjP).create_plugin();

			// Create (and persist) a host proxy in the WASM
			WasmP wasmHostP = nativeToWasm(scoped, host);
			scoped.arenas.persistWasm();
			// So we can find the arenas from the host (and then the context from those arenas)
			scoped.view<wclap_host>(wasmHostP).host_data() = context.arenas->wasmContextP;
			// So we can find the actual host from the context
			context.nativeMap.host = host;

			// Attempt to create the plugin
			WasmP wasmPluginP = 0;
			{
				auto wasmReset = scoped.arenas.scopedWasmReset();
				auto wasmPluginId = nativeToWasm(scoped, plugin_id);
				wasmPluginP = scoped.thread.callWasm_P(createPluginFn, factory.factoryObjP, wasmHostP, wasmPluginId);
			}
			if (!wasmPluginP) return nullptr;
			context.wasmMap.plugin = wasmPluginP;

			// Create (and persist) a plugin proxy in as a native object
			const clap_plugin_t *nativePlugin = wasmToNative<const clap_plugin>(scoped, wasmPluginP);
			scoped.arenas.persistNative();
			// so we can find the context from the proxy
			auto &pluginContext = createNativeProxyContext(scoped, nativePlugin, std::move(context));

			auto pluginGetExtensionFn = scoped.view<wclap_plugin>(wasmPluginP).get_extension();
			if (pluginGetExtensionFn) {
				auto getExtP = [&](const char *){
					auto wasmReset = scoped.arenas.scopedWasmReset();
					auto wasmPluginId = nativeToWasm(scoped, plugin_id);
					WasmP extP = scoped.thread.callWasm_P(createPluginFn, factory.factoryObjP, wasmHostP, wasmPluginId);
					return extP;
				};
				// Load all known extensions
				pluginContext.wasmMap.plugin_ambisonic = getExtP(CLAP_EXT_AMBISONIC);
				pluginContext.wasmMap.plugin_audio_ports = getExtP(CLAP_EXT_AUDIO_PORTS);
				pluginContext.wasmMap.plugin_audio_ports_activation = getExtP(CLAP_EXT_AUDIO_PORTS_ACTIVATION);
				pluginContext.wasmMap.plugin_audio_ports_config = getExtP(CLAP_EXT_AUDIO_PORTS_CONFIG);
				pluginContext.wasmMap.plugin_audio_ports_config_info = getExtP(CLAP_EXT_AUDIO_PORTS_CONFIG_INFO);
				pluginContext.wasmMap.plugin_configurable_audio_ports = getExtP(CLAP_EXT_CONFIGURABLE_AUDIO_PORTS);
				pluginContext.wasmMap.plugin_context_menu = getExtP(CLAP_EXT_CONTEXT_MENU);
				pluginContext.wasmMap.plugin_gui = getExtP(CLAP_EXT_GUI);
				pluginContext.wasmMap.plugin_latency = getExtP(CLAP_EXT_LATENCY);
				pluginContext.wasmMap.plugin_note_name = getExtP(CLAP_EXT_NOTE_NAME);
				pluginContext.wasmMap.plugin_note_ports = getExtP(CLAP_EXT_NOTE_PORTS);
				pluginContext.wasmMap.plugin_params = getExtP(CLAP_EXT_PARAMS);
				pluginContext.wasmMap.plugin_param_indication = getExtP(CLAP_EXT_PARAM_INDICATION);
				pluginContext.wasmMap.plugin_preset_load = getExtP(CLAP_EXT_PRESET_LOAD);
				pluginContext.wasmMap.plugin_remote_controls = getExtP(CLAP_EXT_REMOTE_CONTROLS);
				pluginContext.wasmMap.plugin_render = getExtP(CLAP_EXT_RENDER);
				pluginContext.wasmMap.plugin_state = getExtP(CLAP_EXT_STATE);
				pluginContext.wasmMap.plugin_state_context = getExtP(CLAP_EXT_STATE_CONTEXT);
				pluginContext.wasmMap.plugin_surround = getExtP(CLAP_EXT_SURROUND);
				pluginContext.wasmMap.plugin_tail = getExtP(CLAP_EXT_TAIL);
				pluginContext.wasmMap.plugin_thread_pool = getExtP(CLAP_EXT_THREAD_POOL);
				pluginContext.wasmMap.plugin_timer_support = getExtP(CLAP_EXT_TIMER_SUPPORT);
				pluginContext.wasmMap.plugin_track_info = getExtP(CLAP_EXT_TRACK_INFO);
				pluginContext.wasmMap.plugin_voice_info = getExtP(CLAP_EXT_VOICE_INFO);
				pluginContext.wasmMap.plugin_webview = getExtP(CLAP_EXT_WEBVIEW);
			}

			return nativePlugin;
		}
	};
	
	bool triedPluginFactory = false;
	std::unique_ptr<plugin_factory> pluginFactory;

	void * getFactory(const char *factory_id) {
		WasmP factoryP;
		{
			auto scoped = wclap.lockThread();
			auto entryP = WasmP(scoped.thread.clapEntryP64);
			auto wasmEntry = scoped.view<wclap_plugin_entry>(entryP);
			auto getFactoryFn = wasmEntry.get_factory();

			auto reset = scoped.arenas.scopedWasmReset();
			WasmP wasmStr = nativeToWasm(scoped, factory_id);
			factoryP = scoped.thread.callWasm_P(getFactoryFn, wasmStr);
		}
		if (!factoryP) return nullptr;

		if (!std::strcmp(factory_id, CLAP_PLUGIN_FACTORY_ID)) {
			if (!triedPluginFactory) {
				triedPluginFactory = true;
				pluginFactory = std::unique_ptr<plugin_factory>(new plugin_factory(wclap, factoryP));
			}
			return pluginFactory.get();
		}
		return nullptr;
	}
	
	struct HostFn {
		WasmP wasmP = 0;
	};
	
	struct {
		struct : public HostFn {
			static WasmP native(Wclap &wclap, WasmP hostP, WasmP extStr) {
				auto scoped = wclap.lockThread();
				auto view = scoped.view<wclap_host>(hostP);
				// This is the arena owned by the plugin, since that's where the WASM-side host proxy was created
				auto *boundArenas = wclap.arenasForWasmContext(view.host_data());
				if (!boundArenas) return 0;
				// Get the associated context
				auto *context = (NativeProxyContext *)boundArenas->currentContext;
				if (!context) return 0;
				auto *host = context->nativeMap.host;
				if (!host) return 0;

				auto reset = scoped.arenas.scopedNativeReset();
				const char *extNativeStr = wasmToNative<const char>(scoped, extStr);
				if (!extNativeStr) return 0;

				const void *ext = host->get_extension(host, extNativeStr);
				if (!ext) return 0;
				
				// TODO: these should live globally in the WCLAP
				if (!std::strcmp(extNativeStr, CLAP_EXT_LOG)) {
					//return context->wclap->hostExtensionProxies.host_log;
				}
				
				std::cout << "untranslated: host.get_extension(" << extNativeStr << ") = " << ext << "\n";
				return 0;
			}
		} get_extension;

		struct : public HostFn {
			static void native(Wclap &wclap, WasmP hostP) {
				auto scoped = wclap.lockThread();
				auto view = scoped.view<wclap_host>(hostP);
				// This is the arena owned by the plugin, since that's where the WASM-side host proxy was created
				auto *boundArenas = wclap.arenasForWasmContext(view.host_data());
				if (!boundArenas) return;
				// Get the associated context
				auto *context = (NativeProxyContext *)boundArenas->currentContext;
				if (!context) return;
				auto *host = context->nativeMap.host;
				if (!host) return;
				
				host->request_restart(host);
			}
		} request_restart;
		struct : public HostFn {
			static void native(Wclap &wclap, WasmP hostP) {
				auto scoped = wclap.lockThread();
				auto view = scoped.view<wclap_host>(hostP);
				// This is the arena owned by the plugin, since that's where the WASM-side host proxy was created
				auto *boundArenas = wclap.arenasForWasmContext(view.host_data());
				if (!boundArenas) return;
				// Get the associated context
				auto *context = (NativeProxyContext *)boundArenas->currentContext;
				if (!context) return;
				auto *host = context->nativeMap.host;
				if (!host) return;
				
				host->request_process(host);
			}
		} request_process;
		struct : public HostFn {
			static void native(Wclap &wclap, WasmP hostP) {
				auto scoped = wclap.lockThread();
				auto view = scoped.view<wclap_host>(hostP);
				// This is the arena owned by the plugin, since that's where the WASM-side host proxy was created
				auto *boundArenas = wclap.arenasForWasmContext(view.host_data());
				if (!boundArenas) return;
				// Get the associated context
				auto *context = (NativeProxyContext *)boundArenas->currentContext;
				if (!context) return;
				auto *host = context->nativeMap.host;
				if (!host) return;
				
				host->request_callback(host);
			}
		} request_callback;

		void registerMethods(WclapThread &thread) {
			thread.registerFunction(get_extension);
			thread.registerFunction(request_restart);
			thread.registerFunction(request_process);
			thread.registerFunction(request_callback);
		}
	} host;

	struct {
		struct : public HostFn {
			static void native(Wclap &wclap, WasmP hostP, uint32_t severity, WasmP msgStr) {
				auto scoped = wclap.lockThread();
				auto view = scoped.view<wclap_host>(hostP);
				// This is the arena owned by the plugin, since that's where the WASM-side host proxy was created
				auto *boundArenas = wclap.arenasForWasmContext(view.host_data());
				if (!boundArenas) return;
				// Get the associated context
				auto *context = (NativeProxyContext *)boundArenas->currentContext;
				if (!context) return;
				auto *host = context->nativeMap.host;
				if (!host) return;

				auto reset = scoped.arenas.scopedNativeReset();
				const char *nativeMsgStr = wasmToNative<const char>(scoped, msgStr);
				if (!nativeMsgStr) return;

				const clap_host_log *extLog = context->nativeMap.host_log;
				LOG_EXPR(extLog);
				if (extLog) extLog->log(host, severity, nativeMsgStr);
			}
		} log;

		void registerMethods(WclapThread &thread) {
			thread.registerFunction(log);
		}
	} hostExtLog;

	struct {
		struct : public HostFn {
			static uint32_t native(Wclap &wclap, WasmP inputEventsP) {
				return 0;
			}
		} size;
		struct : public HostFn {
			static WasmP native(Wclap &wclap, WasmP inputEventsP, uint32_t index) {
				return 0;
			}
		} get;

		void registerMethods(WclapThread &thread) {
			thread.registerFunction(size);
			thread.registerFunction(get);
		}
	} inputEvents;

	struct {
		struct : public HostFn {
			static uint32_t native(Wclap &wclap, WasmP inputEventsP, WasmP eventP) {
				return 0;
			}
		} try_push;

		void registerMethods(WclapThread &thread) {
			thread.registerFunction(try_push);
		}
	} outputEvents;

	struct : public HostFn {
		static void native(Wclap &wclap, WasmP) {
			std::cout << "unimplemented V(P)\n";
		}
	} notImplementedVP;
	struct : public HostFn {
		static WasmP native(Wclap &wclap, WasmP, WasmP) {
			std::cout << "unimplemented P(PP)\n";
			return 0;
		}
	} notImplementedPPP;
	
	void registerHostMethods(WclapThread &thread) {
		thread.registerFunction(notImplementedVP);
		thread.registerFunction(notImplementedPPP);

		host.registerMethods(thread);
		hostExtLog.registerMethods(thread);
		inputEvents.registerMethods(thread);
		outputEvents.registerMethods(thread);
	}
};

WclapMethods * methodsCreateAndInit(Wclap &wclap) {
	auto *methods = new WclapMethods(wclap);
	if (!methods->initClapEntry()) {
		wclap.setError("clap_entry.init() returned false");
	}
	return methods;
}
void methodsDeinitAndDelete(WclapMethods *methods) {
	methods->deinitClapEntry();
	delete methods;
}
void methodsRegister(WclapMethods *methods, WclapThread &thread) {
	methods->registerHostMethods(thread);
}
void * methodsGetFactory(WclapMethods *methods, const char *factoryId) {
	return methods->getFactory(factoryId);
}

//-------------- Translating some basic types

template<>
void nativeToWasm<const char>(ScopedThread &scoped, const char *str, WasmP &wasmP) {
	if (!str) {
		wasmP = 0;
		return;
	}
	size_t length = strlen(str);
	auto *strInWasm = scoped.createDirectArray<char>(length + 1, wasmP);
	for (size_t i = 0; i < length; ++i) {
		strInWasm[i] = str[i];
	}
	strInWasm[length] = 0;
}

template<>
void wasmToNative<const char>(ScopedThread &scoped, WasmP wasmStr, const char *&str) {
	if (!wasmStr) {
		str = nullptr;
		return;
	}
	auto maxLength = scoped.wclap.wasmMemorySize(scoped.thread) - wasmStr;
	auto *nativeInWasm = (char *)scoped.wasmMemory(wasmStr, maxLength);
	size_t length = strlen(nativeInWasm, maxLength);
	auto *nativeTmp = (char *)scoped.arenas.nativeBytes(length + 1, 1);
	for (size_t i = 0; i < length; ++i) {
		nativeTmp[i] = nativeInWasm[i];
	}
	nativeTmp[length] = 0;
	str = nativeTmp;
}

template<>
void wasmToNative<const char * const>(ScopedThread &scoped, WasmP stringList, const char * const * &features) {
	if (!stringList) {
		features = nullptr;
		return;
	}
	// Null-terminated array of strings
	size_t count = 0;
	auto *wasmStrArray = scoped.viewDirectPointer<WasmP>(stringList);
	while (wasmStrArray[count] && count < WCLAP_MAX_FEATURES_LENGTH) {
		++count;
	}
	auto *nativeStrArray = (const char **)scoped.arenas.nativeBytes(sizeof(const char *)*(count + 1), alignof(const char *));
	for (size_t i = 0; i < count; ++i) {
		wasmToNative(scoped, wasmStrArray[i], nativeStrArray[i]);
	}
	nativeStrArray[count] = nullptr;
	features = nativeStrArray;
}

//-------------- Translating CLAP structs: WASM -> Native

template<>
void wasmToNative<const clap_plugin_descriptor>(ScopedThread &scoped, WasmP wasmP, const clap_plugin_descriptor *&nativeP) {
	generated_wasmToNative(scoped, wasmP, nativeP);
	if (!nativeP) return;

	// Append ` (WCLAP)` to the name
	auto *desc = (clap_plugin_descriptor *)nativeP; // Yes, un-const it.  It's ours anyway.
	const char *name = desc->name;
	if (!name) name = "unknown";
	size_t nameLength = strlen(name);
	size_t newNameLength = nameLength + strlen(" (WCLAP)");
	auto *newName = (char *)scoped.arenas.nativeBytes(newNameLength + 1, 1);
	std::strcpy(newName, name);
	std::strcpy(newName + nameLength, " (WCLAP)");
	desc->name = newName;
}

static clap_process_status nativeProxy_plugin_process_andCopyOutput(const struct clap_plugin *plugin, const clap_process_t *process) {
	auto &context = getNativeProxyContext(plugin);
	if (context.wclap->errorMessage) return CLAP_PROCESS_ERROR; // Don't even attempt if there have been any errors

	auto scoped = context.lock(true); // realtime if we have one
	auto resetW = scoped.arenas.scopedWasmReset();
	WasmP wasmFn = scoped.view<wclap_plugin>(context.wasmMap.plugin).process();

	WasmP wasmProcessP = nativeToWasm(scoped, process);
	int32_t status = scoped.thread.callWasm_I(wasmFn, context.wasmMap.plugin, wasmProcessP);
	if (status == CLAP_PROCESS_ERROR) return status; // Spec says to discard output, no point copying

	const uint32_t frames = process->frames_count;

	// Copy sample data back to native
	auto processView = scoped.view<wclap_process>(wasmProcessP);
	auto outputBufferList = scoped.arrayView<wclap_audio_buffer>(processView.audio_outputs());
	if (!outputBufferList) return status;
	for (uint32_t o = 0; o < process->audio_outputs_count; ++o) {
		auto *buffer = process->audio_outputs + o;
		
		auto wasmBufferView = outputBufferList[o];
		buffer->latency = wasmBufferView.latency();
		buffer->constant_mask = wasmBufferView.constant_mask();
		
		auto *wasmData32 = scoped.viewDirectPointer<WasmP>(wasmBufferView.data32());
		if (buffer->data32 && wasmData32) {
			for (uint32_t c = 0; c < buffer->channel_count; ++c) {
				auto *channel = buffer->data32[c];
				auto *wasmChannel = scoped.viewDirectPointer<float>(wasmData32[c]);
				if (wasmChannel) {
					for (uint32_t i = 0; i < frames; ++i) {
						channel[i] = wasmChannel[i];
					}
				}
			}
		}
		auto *wasmData64 = scoped.viewDirectPointer<WasmP>(wasmBufferView.data64());
		if (buffer->data64 && wasmData64) {
			for (uint32_t c = 0; c < buffer->channel_count; ++c) {
				auto *channel = buffer->data64[c];
				auto *wasmChannel = scoped.viewDirectPointer<double>(wasmData64[c]);
				if (wasmChannel) {
					for (uint32_t i = 0; i < frames; ++i) {
						channel[i] = wasmChannel[i];
					}
				}
			}
		}
	}
	
	return status;
}

static const void * nativeProxy_plugin_get_extension_fromWclap(const struct clap_plugin *plugin, const char *extId) {
	auto &context = getNativeProxyContext(plugin);
	auto &wclap = *context.wclap;

	if (!std::strcmp(extId, CLAP_EXT_PARAMS)) {
		static clap_plugin_params proxy{
			.count=wclap_plugin_params::nativeProxy_count<false>,
			.get_info=wclap_plugin_params::nativeProxy_get_info<false>,
			.get_value=wclap_plugin_params::nativeProxy_get_value<false>,
			.value_to_text=wclap_plugin_params::nativeProxy_value_to_text<false>,
			.text_to_value=wclap_plugin_params::nativeProxy_text_to_value<false>,
			.flush=wclap_plugin_params::nativeProxy_flush<true>
		};

		auto extP = context.wasmMap.plugin_params;
		return extP ? &proxy : nullptr;
	}

/*
	if (!std::strcmp(extId, CLAP_EXT_AMBISONIC)) {
		auto extP = context.wasmMap.plugin_ambisonic;
		static clap_$1 ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_AUDIO_PORTS)) {
		auto extP = context.wasmMap.plugin_audio_ports;
		static clap_plugin_audio_ports ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_AUDIO_PORTS_ACTIVATION)) {
		auto extP = context.wasmMap.plugin_audio_ports_activation;
		static clap_plugin_audio_ports_activation ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_AUDIO_PORTS_CONFIG)) {
		auto extP = context.wasmMap.plugin_audio_ports_config;
		static clap_plugin_audio_ports_config ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO)) {
		auto extP = context.wasmMap.plugin_audio_ports_config_info;
		static clap_plugin_audio_ports_config_info ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS)) {
		auto extP = context.wasmMap.plugin_configurable_audio_ports;
		static clap_plugin_configurable_audio_ports ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_CONTEXT_MENU)) {
		auto extP = context.wasmMap.plugin_context_menu;
		static clap_plugin_context_menu ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_GUI)) {
		auto extP = context.wasmMap.plugin_gui;
		static clap_plugin_gui ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_LATENCY)) {
		auto extP = context.wasmMap.plugin_latency;
		static clap_plugin_latency ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_NOTE_NAME)) {
		auto extP = context.wasmMap.plugin_note_name;
		static clap_plugin_note_name ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_NOTE_PORTS)) {
		auto extP = context.wasmMap.plugin_note_ports;
		static clap_plugin_note_ports ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_PARAMS)) {
		auto extP = context.wasmMap.plugin_params;
		static clap_plugin_params ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_PARAM_INDICATION)) {
		auto extP = context.wasmMap.plugin_param_indication;
		static clap_plugin_param_indication ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_PRESET_LOAD)) {
		auto extP = context.wasmMap.plugin_preset_load;
		static clap_plugin_preset_load ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_REMOTE_CONTROLS)) {
		auto extP = context.wasmMap.plugin_remote_controls;
		static clap_plugin_remote_controls ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_RENDER)) {
		auto extP = context.wasmMap.plugin_render;
		static clap_plugin_render ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_STATE)) {
		auto extP = context.wasmMap.plugin_state;
		static clap_plugin_state ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_STATE_CONTEXT)) {
		auto extP = context.wasmMap.plugin_state_context;
		static clap_plugin_state_context ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_SURROUND)) {
		auto extP = context.wasmMap.plugin_surround;
		static clap_plugin_surround ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_TAIL)) {
		auto extP = context.wasmMap.plugin_tail;
		static clap_plugin_tail ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_THREAD_POOL)) {
		auto extP = context.wasmMap.plugin_thread_pool;
		static clap_plugin_thread_pool ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_TIMER_SUPPORT)) {
		auto extP = context.wasmMap.plugin_timer_support;
		static clap_plugin_timer_support ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_TRACK_INFO)) {
		auto extP = context.wasmMap.plugin_track_info;
		static clap_plugin_track_info ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_VOICE_INFO)) {
		auto extP = context.wasmMap.plugin_voice_info;
		static clap_plugin_voice_info ext {
		
		};
		return extP ? &ext : nullptr;
	}
	if (!std::strcmp(extId, CLAP_EXT_WEBVIEW)) {
		auto extP = context.wasmMap.plugin_webview;
		static clap_plugin_webview ext {
		
		};
		return extP ? &ext : nullptr;
	}
*/
	return nullptr;
}

static bool nativeProxy_plugin_fillHostExtensions_andInit(const struct clap_plugin *plugin) {
	// This is the earliest we can query the host's extensions
	// TODO: fill out all known host extensions
	
	return wclap_plugin::nativeProxy_init(plugin);
}

static void nativeProxy_plugin_destroy_andResetContext(const struct clap_plugin *plugin) {
	wclap_plugin::nativeProxy_destroy(plugin); // uses the context, so this goes first
	destroyNativeProxyContext(plugin); // this includes returning the memory that the `clap_plugin *` above lives in
}

template<>
void wasmToNative<const clap_plugin>(ScopedThread &scoped, WasmP wasmP, const clap_plugin *&constNativeP) {
	// based on generated_wasmToNative()
	auto wasm = scoped.view<wclap_plugin>(wasmP);
	auto *native = scoped.arenas.nativeTyped<clap_plugin_t>();
	constNativeP = native;
	wasmToNative(scoped, wasm.desc(), native->desc);
	//wasmToNative(scoped, wasm.plugin_data(), native->plugin_data);
	//native->init = wclap_plugin::nativeProxy_init;
	//native->destroy = wclap_plugin::nativeProxy_destroy;
	native->activate = wclap_plugin::nativeProxy_activate;
	native->deactivate = wclap_plugin::nativeProxy_deactivate;
	//native->start_processing = wclap_plugin::nativeProxy_start_processing;
	//native->stop_processing = wclap_plugin::nativeProxy_stop_processing;
	//native->reset = wclap_plugin::nativeProxy_reset; // audio thread
	//native->process = wclap_plugin::nativeProxy_process;
	//native->get_extension = wclap_plugin::nativeProxy_get_extension;
	native->on_main_thread = wclap_plugin::nativeProxy_on_main_thread;

	// Replacements (which we commented out above)
	native->plugin_data = nullptr; // filled in by `create_plugin()`
	native->init = nativeProxy_plugin_fillHostExtensions_andInit;
	native->destroy = nativeProxy_plugin_destroy_andResetContext;
	native->start_processing = wclap_plugin::nativeProxy_start_processing<true>; // default is fine, but should be on audio thread
	native->stop_processing = wclap_plugin::nativeProxy_stop_processing<true>; // audio thread
	native->reset = wclap_plugin::nativeProxy_reset<true>; // audio thread
	native->process = nativeProxy_plugin_process_andCopyOutput;
	native->get_extension = nativeProxy_plugin_get_extension_fromWclap;
}

template<>
void * & nativeProxyContextPointer<clap_plugin>(const clap_plugin *plugin) {
	return (void * &)plugin->plugin_data;
}

// We translate all the structs here, even if they just forward to the generated one, to ensure we've *actually* checked them (in particular, whether methods should obtain a realtime lock or not)

template<>
void wasmToNative<const clap_param_info>(ScopedThread &scoped, WasmP wasmP, const clap_param_info_t *&nativeP) {
	return generated_wasmToNative(scoped, wasmP, nativeP);
}

//-------------- Translating CLAP structs: Native -> WASM

template<>
void nativeToWasm<const clap_host>(ScopedThread &scoped, const clap_host *native, WasmP &wasmP) {
	if (!native) return void(wasmP = 0);

	auto view = scoped.create<wclap_host>(wasmP); // claim the appropriate number of bytes
	view.clap_version() = native->clap_version;
	view.host_data() = 0;
	view.name() = nativeToWasm(scoped, native->name);
	view.vendor() = nativeToWasm(scoped, native->vendor);
	view.url() = nativeToWasm(scoped, native->url);
	view.version() = nativeToWasm(scoped, native->version);

	auto &methods = scoped.wclap.methods(wasmP);
	view.get_extension() = methods.host.get_extension.wasmP;
	view.request_restart() = methods.host.request_restart.wasmP;
	view.request_process() = methods.host.request_process.wasmP;
	view.request_callback() = methods.host.request_callback.wasmP;
}

template<>
void nativeToWasm<const clap_process>(ScopedThread &scoped, const clap_process *native, WasmP &wasmP) {
	auto view = scoped.create<wclap_process>(wasmP);

	view.steady_time() = native->steady_time;
	uint32_t frames = view.frames_count() = native->frames_count;
	nativeToWasmDirectArray(scoped, native->transport, view.transport(), 1); // sets the pointer to 0 if native->transport is NULL

	view.audio_inputs_count() = native->audio_inputs_count;
	auto inputBufferList = scoped.createArray<wclap_audio_buffer>(native->audio_inputs_count, view.audio_inputs());
	for (uint32_t i = 0; i < native->audio_inputs_count; ++i) {
		auto *buffer = native->audio_inputs + i;
		auto wasmBuffer = inputBufferList[i];
		uint32_t channels = wasmBuffer.channel_count() = buffer->channel_count;
		wasmBuffer.latency() = buffer->latency;
		wasmBuffer.constant_mask() = buffer->constant_mask;
		
		if (buffer->data32) {
			auto *bufferP = scoped.createDirectArray<WasmP>(channels, wasmBuffer.data32());
			for (uint32_t c = 0; c < channels; ++c) {
				nativeToWasmDirectArray(scoped, buffer->data32[c], bufferP[c], frames);
			}
		} else {
			wasmBuffer.data32() = 0;
		}
		if (buffer->data64) {
			auto *bufferP = scoped.createDirectArray<WasmP>(channels, wasmBuffer.data64());
			for (uint32_t c = 0; c < channels; ++c) {
				nativeToWasmDirectArray(scoped, buffer->data64[c], bufferP[c], frames);
			}
		} else {
			wasmBuffer.data64() = 0;
		}
	}
	view.audio_outputs_count() = native->audio_outputs_count;
	auto outputBufferList = scoped.createArray<wclap_audio_buffer>(native->audio_outputs_count, view.audio_outputs());
	for (uint32_t o = 0; o < native->audio_outputs_count; ++o) {
		auto *buffer = native->audio_outputs + o;
		auto wasmBuffer = outputBufferList[o];
		uint32_t channels = wasmBuffer.channel_count() = buffer->channel_count;
		wasmBuffer.latency() = 0;
		wasmBuffer.constant_mask() = 0;
		
		if (buffer->data32) {
			auto *bufferP = scoped.createDirectArray<WasmP>(channels, wasmBuffer.data32());
			for (uint32_t c = 0; c < channels; ++c) {
				nativeToWasmDirectArray(scoped, buffer->data32[c], bufferP[c], frames);
			}
		} else {
			wasmBuffer.data32() = 0;
		}
		if (buffer->data64) {
			auto *bufferP = scoped.createDirectArray<WasmP>(channels, wasmBuffer.data64());
			for (uint32_t c = 0; c < channels; ++c) {
				nativeToWasmDirectArray(scoped, buffer->data32[c], bufferP[c], frames);
			}
		} else {
			wasmBuffer.data64() = 0;
		}
	}
	nativeToWasm(scoped, native->in_events, view.in_events());
	nativeToWasm(scoped, native->out_events, view.out_events());
}

template<>
void nativeToWasm<const clap_input_events_t>(ScopedThread &scoped, const clap_input_events_t *native, WasmP &wasmP) {
	auto view = scoped.create<wclap_input_events>(wasmP);
	view.ctx() = 0;
	view.size() = scoped.wclap.methods(wasmP).inputEvents.size.wasmP;
	view.get() = scoped.wclap.methods(wasmP).inputEvents.get.wasmP;
}

template<>
void nativeToWasm<const clap_output_events_t>(ScopedThread &scoped, const clap_output_events_t *native, WasmP &wasmP) {
	auto view = scoped.create<wclap_output_events>(wasmP);
	view.ctx() = 0;
	view.try_push() = scoped.wclap.methods(wasmP).outputEvents.try_push.wasmP;
}

}} // namespace
