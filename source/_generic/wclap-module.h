// No `#pragma once`, because we deliberately get included multiple times by `../wclap.h`, with different WCLAP_API_NAMESPACE, WCLAP_BRIDGE_NAMESPACE and WCLAP_BRIDGE_IS64 values

#include "./wclap-module-base.h"

#include "./wclap-plugin-factory.h"

namespace WCLAP_BRIDGE_NAMESPACE {

using namespace WCLAP_API_NAMESPACE;

struct WclapModule : public WclapModuleBase {
	
	template<class Return, class ...Args>
	bool registerHost(Instance *instance, Function<Return, Args...> &wasmFn, Return (*fn)(void *, Args...)) {
		auto prevIndex = wasmFn.wasmPointer;
		wasmFn = registerHostFunction(instance, (void *)this, fn); // defined in the non-generic `../wclap-module.h` so that it produces the correct-sized pointer
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
		
		instanceGroup->wasiThreadSpawnContext = this;
		instanceGroup->wasiThreadSpawn = staticWasiThreadSpawn;

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
		auto version = mainThread->get(entryPtr[&wclap_plugin_entry::clap_version]);
		clapVersion = {version.major, version.minor, version.revision};

		if (!mainThread->call(entryPtr[&wclap_plugin_entry::init], pathStr)) {
			setError("clap_entry::init() returned false");
			return;
		}
		
		hasError = false;
	}
	~WclapModule() {
		// Prevent any new threads from spawning after this point
		auto lock = this->threadLock();
		instanceGroup->wasiThreadSpawn = nullptr;
		instanceGroup->wasiThreadSpawnContext = nullptr;
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

		//---- Extensions ----
		// There's no global arena at this point, so the pointers get copied across later

		HOST_METHOD(hostAmbisonic, changed);

		HOST_METHOD(hostAudioPortsConfig, rescan);

		HOST_METHOD(hostAudioPorts, is_rescan_flag_supported);
		HOST_METHOD(hostAudioPorts, rescan);

		// Skip this for now, because it needs a few more host structs
		// TODO: implement later
		/*
		HOST_METHOD(hostContextMenu, populate);
		HOST_METHOD(hostContextMenu, perform);
		HOST_METHOD(hostContextMenu, can_popup);
		HOST_METHOD(hostContextMenu, popup);
		*/

		// event-registry extension is skipped because we whitelist events for safety

		HOST_METHOD(hostGui, resize_hints_changed);
		HOST_METHOD(hostGui, request_resize);
		HOST_METHOD(hostGui, request_show);
		HOST_METHOD(hostGui, request_hide);
		HOST_METHOD(hostGui, closed);

		HOST_METHOD(hostLatency, changed);

		HOST_METHOD(hostLog, log);

		HOST_METHOD(hostNoteName, changed);

		HOST_METHOD(hostNotePorts, supported_dialects);
		HOST_METHOD(hostNotePorts, rescan);

		HOST_METHOD(hostParams, rescan);
		HOST_METHOD(hostParams, clear);
		HOST_METHOD(hostParams, request_flush);
		
		// posix-fd-support.h skipped, unless we figure out a way to make it portable

		HOST_METHOD(hostPresetLoad, on_error);
		HOST_METHOD(hostPresetLoad, loaded);

		HOST_METHOD(hostRemoteControls, changed);
		HOST_METHOD(hostRemoteControls, suggest_page);

		HOST_METHOD(hostState, mark_dirty);

		HOST_METHOD(hostSurround, changed);

		HOST_METHOD(hostTail, changed);

		HOST_METHOD(hostThreadCheck, is_main_thread);
		HOST_METHOD(hostThreadCheck, is_audio_thread);

		HOST_METHOD(hostThreadPool, request_exec);

		HOST_METHOD(hostTimerSupport, register_timer);
		HOST_METHOD(hostTimerSupport, unregister_timer);

		HOST_METHOD(hostTrackInfo, get);

		HOST_METHOD(hostVoiceInfo, changed);
		
		//---- Draft extensions ----
		// The webview one is essential for WCLAP GUIs
		// We skip the others because the versioning seems like a compatibility headache

		HOST_METHOD(hostWebview, send);

#undef HOST_METHOD
		return true;
	}
	bool bindGlobalArena() {
		auto scoped = arenaPool.scoped();
		
		// The global arena holds all the extensions, for the lifetime of the module
		hostAmbisonicPtr = scoped.copyAcross(hostAmbisonic);
		hostAudioPortsConfigPtr = scoped.copyAcross(hostAudioPortsConfig);
		hostAudioPortsPtr = scoped.copyAcross(hostAudioPorts);
		//hostContextMenuPtr = scoped.copyAcross(hostContextMenu);
		hostGuiPtr = scoped.copyAcross(hostGui);
		hostLatencyPtr = scoped.copyAcross(hostLatency);
		hostLogPtr = scoped.copyAcross(hostLog);
		hostNoteNamePtr = scoped.copyAcross(hostNoteName);
		hostNotePortsPtr = scoped.copyAcross(hostNotePorts);
		hostParamsPtr = scoped.copyAcross(hostParams);
		hostPresetLoadPtr = scoped.copyAcross(hostPresetLoad);
		hostRemoteControlsPtr = scoped.copyAcross(hostRemoteControls);
		hostStatePtr = scoped.copyAcross(hostState);
		hostSurroundPtr = scoped.copyAcross(hostSurround);
		hostTailPtr = scoped.copyAcross(hostTail);
		hostThreadCheckPtr = scoped.copyAcross(hostThreadCheck);
		hostThreadPoolPtr = scoped.copyAcross(hostThreadPool);
		hostTimerSupportPtr = scoped.copyAcross(hostTimerSupport);
		// need to be able to point to these constants
		wclapPortMonoPtr = scoped.writeString(CLAP_PORT_MONO);
		wclapPortStereoPtr = scoped.writeString(CLAP_PORT_STEREO);
		wclapPortSurroundPtr = scoped.writeString(CLAP_PORT_SURROUND);
		wclapPortAmbisonicPtr = scoped.writeString(CLAP_PORT_AMBISONIC);
		wclapPortOtherPtr = scoped.writeString("(unknown host port type)");
		hostTrackInfoPtr = scoped.copyAcross(hostTrackInfo);
		hostVoiceInfoPtr = scoped.copyAcross(hostVoiceInfo);
		
		hostWebviewPtr = scoped.copyAcross(hostWebview);
		
		globalArena = scoped.commit();
		return true;
	}

	static int32_t staticWasiThreadSpawn(void *context, uint64_t threadArg) {
		auto *module = (WclapModule *)context;
		return module->wasiThreadSpawn(threadArg);
	}
	int32_t wasiThreadSpawn(uint64_t threadArg) {
		if (hasError) return -1;

		auto locked = threadLock();

		auto instance = instanceGroup->startInstance();
		if (!instance) {
			setError("failed to start instance for new WCLAP thread");
			return -1;
		}

		if (!addHostFunctions(instance.get())) {
			setError("failed to register host functions for new WCLAP thread");
			return -1;
		}

		// Use empty thread or start new one
		size_t index = threads.size();
		for (size_t i = 1; i < threads.size(); ++i) {
			if (!threads[i]) {
				index = i;
				break;
			}
		}
		if (index == threads.size()) threads.emplace_back();
		threads[index] = std::unique_ptr<Thread>{new Thread{
			.index=uint32_t(index),
			.threadArg=threadArg,
			.thread=std::thread{runThread, this, index},
			.instance=std::move(instance)
		}};

		return index;
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
		
		if (hostExtStr == CLAP_EXT_AMBISONIC) {
			return self.hostAmbisonicPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_AUDIO_PORTS_CONFIG) {
			return self.hostAudioPortsConfigPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_AUDIO_PORTS) {
			return self.hostAudioPortsPtr.cast<const void>();
		//} else if (hostExtStr == CLAP_EXT_CONTEXT_MENU) {
		//	return self.hostContextMenuPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_GUI) {
			return self.hostGuiPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_LATENCY) {
			return self.hostLatencyPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_LOG) {
			return self.hostLogPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_NOTE_NAME) {
			return self.hostNoteNamePtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_NOTE_PORTS) {
			return self.hostNotePortsPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_PARAMS) {
			return self.hostParamsPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_PRESET_LOAD) {
			return self.hostPresetLoadPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_REMOTE_CONTROLS) {
			return self.hostRemoteControlsPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_STATE) {
			return self.hostStatePtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_SURROUND) {
			return self.hostSurroundPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_TAIL) {
			return self.hostTailPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_THREAD_CHECK) {
			return self.hostThreadCheckPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_THREAD_POOL) {
			return self.hostThreadPoolPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_TIMER_SUPPORT) {
			return self.hostTimerSupportPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_TRACK_INFO) {
			return self.hostTrackInfoPtr.cast<const void>();
		} else if (hostExtStr == CLAP_EXT_VOICE_INFO) {
			return self.hostVoiceInfoPtr.cast<const void>();
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

	wclap_host_ambisonic hostAmbisonic;
	Pointer<wclap_host_ambisonic> hostAmbisonicPtr;
	static void hostAmbisonic_changed(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostAmbisonic->changed(plugin->host);
	}

	wclap_host_audio_ports_config hostAudioPortsConfig;
	Pointer<wclap_host_audio_ports_config> hostAudioPortsConfigPtr;
	static void hostAudioPortsConfig_rescan(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostAudioPortsConfig->rescan(plugin->host);
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

	wclap_host_gui hostGui;
	Pointer<wclap_host_gui> hostGuiPtr;
	static void hostGui_resize_hints_changed(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostGui->resize_hints_changed(plugin->host);
	}
	static bool hostGui_request_resize(void *context, Pointer<const wclap_host> wHost, uint32_t width, uint32_t height) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostGui->request_resize(plugin->host, width, height);
		return false;
	}
	static bool hostGui_request_show(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostGui->request_show(plugin->host);
		return false;
	}
	static bool hostGui_request_hide(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostGui->request_hide(plugin->host);
		return false;
	}
	static void hostGui_closed(void *context, Pointer<const wclap_host> wHost, bool was_destroyed) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostGui->closed(plugin->host, was_destroyed);
	}

	wclap_host_latency hostLatency;
	Pointer<wclap_host_latency> hostLatencyPtr;
	static void hostLatency_changed(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostLatency->changed(plugin->host);
	}

	wclap_host_log hostLog;
	Pointer<wclap_host_log> hostLogPtr;
	static void hostLog_log(void *context, Pointer<const wclap_host> wHost, int32_t severity, Pointer<const char> msg) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) {
			auto msgString = plugin->mainThread->getString(msg, wclap_bridge::maxLogStringLength);
			return plugin->hostLog->log(plugin->host, severity, msgString.c_str());
		}
	}

	wclap_host_note_name hostNoteName;
	Pointer<wclap_host_note_name> hostNoteNamePtr;
	static void hostNoteName_changed(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostNoteName->changed(plugin->host);
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

	wclap_host_preset_load hostPresetLoad;
	Pointer<wclap_host_preset_load> hostPresetLoadPtr;
	static void hostPresetLoad_on_error(void *context, Pointer<const wclap_host> wHost, uint32_t location_kind, Pointer<const char> location, Pointer<const char> load_key, int32_t os_error, Pointer<const char> msg) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) {
			auto locationString = plugin->mainThread->getString(location, wclap_bridge::maxLogStringLength);
			auto loadKeyString = plugin->mainThread->getString(load_key, wclap_bridge::maxLogStringLength);
			auto msgString = plugin->mainThread->getString(msg, wclap_bridge::maxLogStringLength);
			return plugin->hostPresetLoad->on_error(plugin->host, location_kind, locationString.c_str(), loadKeyString.c_str(), os_error, msgString.c_str());
		}
	}
	static void hostPresetLoad_loaded(void *context, Pointer<const wclap_host> wHost, uint32_t location_kind, Pointer<const char> location, Pointer<const char> load_key) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) {
			auto locationString = plugin->mainThread->getString(location, wclap_bridge::maxLogStringLength);
			auto loadKeyString = plugin->mainThread->getString(load_key, wclap_bridge::maxLogStringLength);
			return plugin->hostPresetLoad->loaded(plugin->host, location_kind, locationString.c_str(), loadKeyString.c_str());
		}
	}

	wclap_host_remote_controls hostRemoteControls;
	Pointer<wclap_host_remote_controls> hostRemoteControlsPtr;
	static void hostRemoteControls_changed(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostRemoteControls->changed(plugin->host);
	}
	static void hostRemoteControls_suggest_page(void *context, Pointer<const wclap_host> wHost, uint32_t page_id) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostRemoteControls->suggest_page(plugin->host, page_id);
	}

	wclap_host_state hostState;
	Pointer<wclap_host_state> hostStatePtr;
	static void hostState_mark_dirty(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostState->mark_dirty(plugin->host);
	}

	wclap_host_surround hostSurround;
	Pointer<wclap_host_surround> hostSurroundPtr;
	static void hostSurround_changed(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostSurround->changed(plugin->host);
	}

	wclap_host_tail hostTail;
	Pointer<wclap_host_tail> hostTailPtr;
	static void hostTail_changed(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostTail->changed(plugin->host);
	}

	wclap_host_thread_check hostThreadCheck;
	Pointer<wclap_host_thread_check> hostThreadCheckPtr;
	static bool hostThreadCheck_is_main_thread(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostThreadCheck->is_main_thread(plugin->host);
		return true;
	}
	static bool hostThreadCheck_is_audio_thread(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostThreadCheck->is_audio_thread(plugin->host);
		return true;
	}

	wclap_host_thread_pool hostThreadPool;
	Pointer<wclap_host_thread_pool> hostThreadPoolPtr;
	static bool hostThreadPool_request_exec(void *context, Pointer<const wclap_host> wHost, uint32_t num_tasks) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostThreadPool->request_exec(plugin->host, num_tasks);
		return false;
	}

	wclap_host_timer_support hostTimerSupport;
	Pointer<wclap_host_timer_support> hostTimerSupportPtr;
	static bool hostTimerSupport_register_timer(void *context, Pointer<const wclap_host> wHost, uint32_t period_ms, Pointer<uint32_t> timer_id) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) {
			uint32_t nativeTimerId = 0;
			if (plugin->hostTimerSupport->register_timer(plugin->host, period_ms, &nativeTimerId)) {
				plugin->mainThread->set(timer_id, nativeTimerId);
				return true;
			}
			return false;
		}
		return false;
	}
	static bool hostTimerSupport_unregister_timer(void *context, Pointer<const wclap_host> wHost, uint32_t timer_id) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostTimerSupport->unregister_timer(plugin->host, timer_id);
		return false;
	}

	wclap_host_track_info hostTrackInfo;
	Pointer<wclap_host_track_info> hostTrackInfoPtr;
	static bool hostTrackInfo_get(void *context, Pointer<const wclap_host> wHost, Pointer<wclap_track_info> infoPtr) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) {
			clap_track_info info{.flags=0, .name={}, .color={0, 0, 0, 0}, .audio_channel_count=0, .audio_port_type=nullptr};
			if (plugin->hostTrackInfo->get(plugin->host, &info)) {
				wclap_track_info wclapInfo;
				wclapInfo.flags = info.flags;
				std::memcpy(wclapInfo.name, info.name, CLAP_NAME_SIZE);
				wclapInfo.color = {info.color.alpha, info.color.red, info.color.green, info.color.blue};
				wclapInfo.audio_channel_count = info.audio_channel_count;
				wclapInfo.audio_port_type = plugin->module.wclapPortOtherPtr;
				// Only assign port-type string if it's one of the known values
				if (info.flags&CLAP_TRACK_INFO_HAS_AUDIO_CHANNEL) {
					wclapInfo.audio_port_type = plugin->module.translatePortType(info.audio_port_type);
				};
				plugin->mainThread->set(infoPtr, wclapInfo);
				return true;
			}
			return false;
		}
		return false;
	}

	wclap_host_voice_info hostVoiceInfo;
	Pointer<wclap_host_voice_info> hostVoiceInfoPtr;
	static void hostVoiceInfo_changed(void *context, Pointer<const wclap_host> wHost) {
		auto *plugin = getPlugin(context, wHost);
		if (plugin) return plugin->hostVoiceInfo->changed(plugin->host);
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
