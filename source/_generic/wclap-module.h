// No `#pragma once`, because we deliberately get included multiple times by `../wclap.h`, with different WCLAP_API_NAMESPACE, WCLAP_BRIDGE_NAMESPACE and WCLAP_BRIDGE_IS64 values

#include "./wclap-module-base.h"

#include "./wclap-plugin-factory.h"

namespace WCLAP_BRIDGE_NAMESPACE {

using namespace WCLAP_API_NAMESPACE;

struct WclapModule : public WclapModuleBase {
	
	template<class Return, class ...Args>
	bool registerHost(Instance *instance, Function<Return, Args...> &wasmFn, Return (*fn)(void *, Args...)) {
		auto prevIndex = wasmFn.wasmPointer;
		wasmFn = registerHostFunction(instance, (void *)this, fn);
		if (wasmFn.wasmPointer == -1) {
			setError("failed to register function");
			return false;
		}
		if (prevIndex != 0 && wasmFn.wasmPointer != prevIndex) {
			// This is when we've previously registered it on another thread, and it needs to match
			setError("function index mismatch");
			return false;
		}
		return true;
	}

	WclapModule(InstanceGroup *instanceGroup) : WclapModuleBase(instanceGroup) {
		if (hasError) return; // base class failed
		if (!addHostFunctions(mainThread.get())) return;

		mainThread->init();
		if constexpr (WCLAP_BRIDGE_IS64) {
			entryPtr = {Size(mainThread->entry64.wasmPointer)};
		} else {
			entryPtr = {Size(mainThread->entry32.wasmPointer)};
		}
		if (!entryPtr) {
			setError("clap_entry is NULL");
			return;
		}
		
		bindGlobalArena();
		
		auto scoped = arenaPool.scoped();
		auto pathStr = scoped.writeString(mainThread->path());
		auto version = mainThread->get(entryPtr[&wclap_plugin_entry::wclap_version]);
		clapVersion = {version.major, version.minor, version.revision};

		if (!mainThread->call(entryPtr[&wclap_plugin_entry::init], pathStr)) {
			setError("clap_entry::init() returned false");
			return;
		}
		
		hasError = false;
	}

	std::optional<PluginFactory> pluginFactory;
	
	void * getFactory(const char *factoryId) {
		if (!std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID)) {
			if (!pluginFactory) {
				auto scoped = arenaPool.scoped();
				auto wclapStr = scoped.writeString(CLAP_PLUGIN_FACTORY_ID);
				auto factoryPtr = mainThread->call(entryPtr[&wclap_plugin_entry::get_factory], wclapStr);
				pluginFactory.emplace(*this, factoryPtr.cast<wclap_plugin_factory>());
			}
			if (!pluginFactory->ptr) return nullptr;
			return &pluginFactory->clapFactory;
		}
		return nullptr;
	}

	bool addHostFunctions(Instance *instance) {
#define HOST_METHOD(obj, name) \
		if (!registerHost(instance, obj.name, obj##_##name)) return false;
		HOST_METHOD(hostTemplate, get_extension);
		HOST_METHOD(hostTemplate, request_restart);
		HOST_METHOD(hostTemplate, request_process);
		HOST_METHOD(hostTemplate, request_callback);

		// Other host-owned structures, which probably only exist temporarily
		HOST_METHOD(inputEventsTemplate, size);
		HOST_METHOD(inputEventsTemplate, get);
		HOST_METHOD(outputEventsTemplate, try_push);
		HOST_METHOD(istreamTemplate, read);
		HOST_METHOD(ostreamTemplate, write);

		// Extensions - no context pointers, so these will get copied across below
		HOST_METHOD(hostAudioPorts, is_rescan_flag_supported);
		HOST_METHOD(hostAudioPorts, rescan);

		HOST_METHOD(hostLatency, changed);

		HOST_METHOD(hostNotePorts, supported_dialects);
		HOST_METHOD(hostNotePorts, rescan);

		HOST_METHOD(hostParams, rescan);
		HOST_METHOD(hostParams, clear);
		HOST_METHOD(hostParams, request_flush);

		HOST_METHOD(hostState, mark_dirty);

		HOST_METHOD(hostWebview, send);

#undef HOST_METHOD
		return true;
	}
	bool bindGlobalArena() {
		auto scoped = arenaPool.scoped();
		
		// The global arena holds all the extensions, for the lifetime of the module
		hostAudioPortsPtr = scoped.copyAcross(hostAudioPorts);
		hostLatencyPtr = scoped.copyAcross(hostLatency);
		hostNotePortsPtr = scoped.copyAcross(hostNotePorts);
		hostParamsPtr = scoped.copyAcross(hostParams);
		hostStatePtr = scoped.copyAcross(hostState);
		hostWebviewPtr = scoped.copyAcross(hostWebview);
		
		globalArena = scoped.commit();
		return true;
	}
	
	// Host methods
	static Pointer<const void> hostTemplate_get_extension(void *context, Pointer<const wclap_host> wHost, Pointer<const char> extId) {
		auto &self = *(WclapModule *)context;
		auto hostExtStr = self.mainThread->getString(extId, 1024);

		auto *plugin = getPlugin(context, wHost);
		if (!plugin) return {0};
		
		if (hostExtStr == CLAP_EXT_WEBVIEW) {
			// Special-cased because we provide it to the plugin even if the host doesn't
			return self.hostWebviewPtr.cast<const void>();
		}
		
		const void *nativeHostExt = plugin->host->get_extension(plugin->host, hostExtStr.c_str());
		if (!nativeHostExt) return {0};

		if (hostExtStr == CLAP_EXT_AUDIO_PORTS) {
			return self.hostAudioPortsPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_LATENCY) {
			return self.hostLatencyPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_PARAMS) {
			return self.hostParamsPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_STATE) {
			return self.hostStatePtr.cast<const void>();
		}
		// null, no extensions for now
LOG_EXPR(hostExtStr);
		return {0};
	}
	static void hostTemplate_request_restart(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->host->request_restart(plugin->host);
	}
	static void hostTemplate_request_process(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->host->request_process(plugin->host);
	}
	static void hostTemplate_request_callback(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->host->request_callback(plugin->host);
	}

	static uint32_t inputEventsTemplate_size(void *context, Pointer<const wclap_input_events> obj) {
		auto *plugin = getPlugin(context, obj);
		if (plugin) return plugin->inputEventsSize();
		return 0;
	}
	static Pointer<const wclap_event_header> inputEventsTemplate_get(void *context, Pointer<const wclap_input_events> obj, uint32_t index) {
		auto *plugin = getPlugin(context, obj);
		if (plugin) return plugin->inputEventsGet(index);
		return {0};
	}
	static bool outputEventsTemplate_try_push(void *context, Pointer<const wclap_output_events> obj, Pointer<const wclap_event_header> event) {
		auto *plugin = getPlugin(context, obj);
		if (plugin) return plugin->outputEventsTryPush(event);
		return false;
	}
	static int64_t istreamTemplate_read(void *context, Pointer<const wclap_istream> obj, Pointer<void> buffer, uint64_t size) {
		auto *plugin = getPlugin(context, obj);
		if (plugin) return plugin->istreamRead(buffer, size);
		return -1;
	}
	static int64_t ostreamTemplate_write(void *context, Pointer<const wclap_ostream> obj, Pointer<const void> buffer, uint64_t size) {
		auto *plugin = getPlugin(context, obj);
		if (plugin) return plugin->ostreamWrite(buffer, size);
		return -1;
	}

	wclap_host_audio_ports hostAudioPorts;
	Pointer<wclap_host_audio_ports> hostAudioPortsPtr;
	static bool hostAudioPorts_is_rescan_flag_supported(void *context, Pointer<const wclap_host> wHost, uint32_t flag) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostAudioPorts->is_rescan_flag_supported(plugin->host, flag);
		return false;
	}
	static void hostAudioPorts_rescan(void *context, Pointer<const wclap_host> wHost, uint32_t flags) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostAudioPorts->rescan(plugin->host, flags);
	}

	wclap_host_latency hostLatency;
	Pointer<wclap_host_latency> hostLatencyPtr;
	static void hostLatency_changed(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostLatency->changed(plugin->host);
	}

	wclap_host_note_ports hostNotePorts;
	Pointer<wclap_host_note_ports> hostNotePortsPtr;
	static uint32_t hostNotePorts_supported_dialects(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostNotePorts->supported_dialects(plugin->host);
		return false;
	}
	static void hostNotePorts_rescan(void *context, Pointer<const wclap_host> wHost, uint32_t flags) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostNotePorts->rescan(plugin->host, flags);
	}

	wclap_host_params hostParams;
	Pointer<wclap_host_params> hostParamsPtr;
	static void hostParams_rescan(void *context, Pointer<const wclap_host> wHost, uint32_t flags) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostParams->rescan(plugin->host, flags);
	}
	static void hostParams_clear(void *context, Pointer<const wclap_host> wHost, uint32_t paramId, uint32_t flags) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostParams->clear(plugin->host, paramId, flags);
	}
	static void hostParams_request_flush(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostParams->request_flush(plugin->host);
	}

	wclap_host_state hostState;
	Pointer<wclap_host_state> hostStatePtr;
	static void hostState_mark_dirty(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostState->mark_dirty(plugin->host);
	}

	wclap_host_webview hostWebview;
	Pointer<wclap_host_webview> hostWebviewPtr;
	static bool hostWebview_send(void *context, Pointer<const wclap_host> wHost, Pointer<const void> buffer, uint32_t size) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->webviewSend(buffer, size);
		return false;
	}
};

}; // namespace
