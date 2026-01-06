// No `#pragma once`, because we deliberately get included multiple times by `../wclap.h`, with different WCLAP_API_NAMESPACE, WCLAP_BRIDGE_NAMESPACE and WCLAP_BRIDGE_IS64 values

#include <atomic>
#include <string_view>
#include <fstream>

#include "webview-gui/clap-webview-gui.h"
#include "webview-gui/helpers.h"

namespace WCLAP_BRIDGE_NAMESPACE {

using namespace WCLAP_API_NAMESPACE;

struct Plugin {
	WclapModuleBase &module;
	Instance *mainThread;
	webview_gui::ClapWebviewGui webviewGui;
	
	Pointer<const wclap_plugin> ptr;
	MemoryArenaPtr arena; // this holds the `wclap_host` (and anything else we need) for the lifetime of the plugin, and is also used by audio-thread methods
	std::unique_ptr<Instance> maybeAudioThread;
	Instance *audioThread; // either our dedicated audio thread, or the main (single) thread again
	uint32_t pluginListIndex;
	std::atomic<bool> destroyCalled = false;

	const clap_host *host;
	const clap_host_ambisonic *hostAmbisonic = nullptr;
	const clap_host_audio_ports_config *hostAudioPortsConfig = nullptr;
	const clap_host_audio_ports *hostAudioPorts = nullptr;
	const clap_host_gui *hostGui = nullptr;
	const clap_host_latency *hostLatency = nullptr;
	const clap_host_log *hostLog = nullptr;
	const clap_host_note_name *hostNoteName = nullptr;
	const clap_host_note_ports *hostNotePorts = nullptr;
	const clap_host_params *hostParams = nullptr;
	const clap_host_preset_load *hostPresetLoad = nullptr;
	const clap_host_remote_controls *hostRemoteControls = nullptr;
	const clap_host_state *hostState = nullptr;
	const clap_host_surround *hostSurround = nullptr;
	const clap_host_tail *hostTail = nullptr;
	const clap_host_thread_check *hostThreadCheck = nullptr;
	const clap_host_thread_pool *hostThreadPool = nullptr;
	const clap_host_timer_support *hostTimerSupport = nullptr;
	const clap_host_track_info *hostTrackInfo = nullptr;
	const clap_host_voice_info *hostVoiceInfo = nullptr;
	const clap_host_webview *hostWebview = nullptr;
		
	Plugin(WclapModuleBase &module, const clap_host *host, Pointer<wclap_host> hostPtr, Pointer<const wclap_plugin> ptr, MemoryArenaPtr arena, const clap_plugin_descriptor *desc) : module(module), mainThread(module.mainThread.get()), ptr(ptr), arena(std::move(arena)), maybeAudioThread(module.instanceGroup->startInstance()), audioThread(maybeAudioThread ? maybeAudioThread.get() : mainThread), host(host) {
		// Address using its index in the plugin list (where it's retained)
		pluginListIndex = module.pluginList.retain(this);
		module.setPlugin(hostPtr, pluginListIndex);

		clapPlugin.desc = desc;
		inputEvents.reserve(1024);
	};
	Plugin(const Plugin& other) = delete;
	~Plugin() {
		if (!destroyCalled) { // This means the WclapModule is closing suddenly, without shutting down the plugins
			// TODO: anything sensible
			abort();
		}
		arena->pool.returnToPool(arena);
	}

	clap_plugin clapPlugin{
		.desc=nullptr,
		.plugin_data=this,
		.init=clapPluginMethod<&Plugin::pluginInit>(),
		.destroy=clapPluginMethod<&Plugin::pluginDestroy>(),
		.activate=clapPluginMethod<&Plugin::pluginActivate>(),
		.deactivate=clapPluginMethod<&Plugin::pluginDeactivate>(),
		.start_processing=clapPluginMethod<&Plugin::pluginStartProcessing>(),
		.stop_processing=clapPluginMethod<&Plugin::pluginStopProcessing>(),
		.reset=clapPluginMethod<&Plugin::pluginReset>(),
		.process=clapPluginMethod<&Plugin::pluginProcess>(),
		.get_extension=clapPluginMethod<&Plugin::pluginGetExtension>(),
		.on_main_thread=clapPluginMethod<&Plugin::pluginOnMainThread>()
	};

	// Host methods
	std::recursive_mutex hostEventsMutex;
	std::vector<Pointer<const wclap_event_header>> inputEvents;
	const clap_output_events *hostOutputEvents = nullptr;
	uint32_t inputEventsSize() {
		std::unique_lock<std::recursive_mutex> lock{hostEventsMutex};
		return uint32_t(inputEvents.size());
	}
	Pointer<const wclap_event_header> inputEventsGet(uint32_t index) {
		std::unique_lock<std::recursive_mutex> lock{hostEventsMutex};
		if (index < inputEvents.size()) return inputEvents[index];
		return {0};
	}
	void tryCopyInputEvent(MemoryArenaScope &scope, const clap_event_header *event) {
		if (event->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
		if (event->type <= 4 || (event->type >= 7 && event->type <= 10) || event->type == 12) {
			// clap_event_note, clap_event_note_expression, clap_event_param_gesture, clap_event_transport, clap_event_midi or clap_event_midi2
			auto bytes = scope.reserve(event->size, 8).cast<unsigned char>();
			audioThread->setArray(bytes, (unsigned char *)event, event->size);
			inputEvents.push_back(bytes.cast<const wclap_event_header>());
		} else if (event->type == 5 || event->type == 6) {
			// Treat `wclap_event_param_mod` as `wclap_event_param_value`, since they're identical aside from the `value`/`amount` field name
			// only the value/amount field names differ (for clarity)
			// so we use the same code for both
			auto valueEvent = *(clap_event_param_value *)event;
			wclap_event_param_value wValueEvent{
				.header=*(wclap_event_header *)event,
				.param_id=valueEvent.param_id,
				.cookie={Size(size_t(valueEvent.cookie))}, // for wasm64, this entire event could be a bitwise copy, but that's unnerving
				.note_id=valueEvent.note_id,
				.port_index=valueEvent.port_index,
				.channel=valueEvent.channel,
				.key=valueEvent.key,
				.value=valueEvent.value
			};
			Pointer<wclap_event_param_value> wValueEventPtr = scope.copyAcross(wValueEvent);
			inputEvents.push_back(wValueEventPtr.cast<const wclap_event_header>());
		} else if (event->type == 11) {
			auto *sysex = (clap_event_midi_sysex *)event;
			auto size = sysex->size;
			auto wBuffer = scope.array<uint8_t>(size);
			audioThread->setArray(wBuffer, sysex->buffer, size);
			wclap_event_midi_sysex wSysex{
				.header=*(wclap_event_header *)event,
				.port_index=sysex->port_index,
				.buffer=wBuffer,
				.size=size
			};
			Pointer<wclap_event_midi_sysex> wSysexPtr = scope.copyAcross(wSysex);
			inputEvents.push_back(wSysexPtr.cast<const wclap_event_header>());
		}
	}
	bool outputEventsTryPush(Pointer<const wclap_event_header> event) {
		std::unique_lock<std::recursive_mutex> lock{hostEventsMutex};
		if (!hostOutputEvents) return false;
		auto eventHeader = audioThread->get(event);
		if (eventHeader.space_id != CLAP_CORE_EVENT_SPACE_ID) return false;

		if (eventHeader.type < 4) {
			clap_event_note nativeEvent;
			audioThread->getArray(event.cast<unsigned char>(), (unsigned char *)&nativeEvent, sizeof(nativeEvent));
			nativeEvent.header.size = sizeof(nativeEvent);
			return hostOutputEvents->try_push(hostOutputEvents, &nativeEvent.header);
		} else if (eventHeader.type == 4) {
			clap_event_note_expression nativeEvent;
			audioThread->getArray(event.cast<unsigned char>(), (unsigned char *)&nativeEvent, sizeof(nativeEvent));
			nativeEvent.header.size = sizeof(nativeEvent);
			return hostOutputEvents->try_push(hostOutputEvents, &nativeEvent.header);
		} else if (eventHeader.type == 5 || eventHeader.type == 6) {
			// Again, treat `wclap_event_param_mod` as `wclap_event_param_value`
			auto wEvent = audioThread->get(event.cast<const wclap_event_param_value>());

			void *cookie = nullptr;
			// Store cookie, assuming host pointer size is larger enough (which is almost certainly true)
			if constexpr (sizeof(cookie) >= sizeof(wEvent.cookie)) {
				cookie = (void *)size_t(wEvent.cookie.wasmPointer);
			}

			clap_event_param_value nativeEvent{
				.header=*(clap_event_header *)&wEvent.header,
				.param_id=wEvent.param_id,
				.cookie=cookie,
				.note_id=wEvent.note_id,
				.port_index=wEvent.port_index,
				.channel=wEvent.channel,
				.key=wEvent.key,
				.value=wEvent.value
			};
			nativeEvent.header.size = sizeof(nativeEvent);
			return hostOutputEvents->try_push(hostOutputEvents, &nativeEvent.header);
		} else if (eventHeader.type == 7 || eventHeader.type == 8) {
			clap_event_param_gesture nativeEvent;
			audioThread->getArray(event.cast<unsigned char>(), (unsigned char *)&nativeEvent, sizeof(nativeEvent));
			nativeEvent.header.size = sizeof(nativeEvent);
			return hostOutputEvents->try_push(hostOutputEvents, &nativeEvent.header);
		} else if (eventHeader.type == 9) {
			clap_event_transport nativeEvent;
			audioThread->getArray(event.cast<unsigned char>(), (unsigned char *)&nativeEvent, sizeof(nativeEvent));
			nativeEvent.header.size = sizeof(nativeEvent);
			return hostOutputEvents->try_push(hostOutputEvents, &nativeEvent.header);
		} else if (eventHeader.type == 10) {
			clap_event_midi nativeEvent;
			audioThread->getArray(event.cast<unsigned char>(), (unsigned char *)&nativeEvent, sizeof(nativeEvent));
			nativeEvent.header.size = sizeof(nativeEvent);
			return hostOutputEvents->try_push(hostOutputEvents, &nativeEvent.header);
		} else if (eventHeader.type == 11) {
			auto wEvent = audioThread->get(event.cast<const wclap_event_midi_sysex>());
			if (wEvent.size > 1024) return false; // too big, and we don't want to allocate here
			uint8_t buffer[1024];
			audioThread->getArray(wEvent.buffer, buffer, wEvent.size);
			clap_event_midi_sysex nativeEvent{
				.header=*(clap_event_header *)&wEvent.header,
				.port_index=wEvent.port_index,
				.buffer=buffer,
				.size=wEvent.size
			};
			nativeEvent.header.size = sizeof(nativeEvent);
			return hostOutputEvents->try_push(hostOutputEvents, &nativeEvent.header);
		} else if (eventHeader.type == 12) {
			clap_event_midi2 nativeEvent;
			audioThread->getArray(event.cast<unsigned char>(), (unsigned char *)&nativeEvent, sizeof(nativeEvent));
			nativeEvent.header.size = sizeof(nativeEvent);
			return hostOutputEvents->try_push(hostOutputEvents, &nativeEvent.header);
		}
		return false;
	}

	std::recursive_mutex hostStreamsMutex;
	const clap_istream *hostIstream = nullptr;
	const clap_ostream *hostOstream = nullptr;
	int64_t istreamRead(Pointer<void> buffer, uint64_t size) {
		std::unique_lock<std::recursive_mutex> lock{hostStreamsMutex};
		if (!hostIstream) return -1;
		
		if (size > 1024) size = 1024; // 1kB max
		unsigned char localBuffer[1024];
		auto result = hostIstream->read(hostIstream, localBuffer, size);
		if (result > 0 && result <= 1024) {
			mainThread->setArray(buffer.cast<unsigned char>(), localBuffer, result);
		}
		return result;
	}
	int64_t ostreamWrite(Pointer<const void> buffer, uint64_t size) {
		std::unique_lock<std::recursive_mutex> lock{hostStreamsMutex};
		if (!hostOstream) return -1;
		
		if (size > 1024) size = 1024; // 1kB max
		unsigned char localBuffer[1024];
		mainThread->getArray(buffer.cast<unsigned char>(), localBuffer, size);
		return hostOstream->write(hostOstream, localBuffer, size);
	}
	std::mutex webviewMessageMutex;
	std::vector<unsigned char> webviewMessageBuffer;
	bool webviewSend(Pointer<const void> buffer, uint64_t size) {
		std::unique_lock<std::mutex> lock{webviewMessageMutex};
		webviewMessageBuffer.resize(size); // main thread, it's fine
		mainThread->getArray(buffer.cast<unsigned char>(), webviewMessageBuffer.data(), size);
		return hostWebview->send(host, webviewMessageBuffer.data(), size);
	}
private:

	bool pluginInit() {
#define GET_HOST_EXT(field, extId) \
		field = (decltype(field))host->get_extension(host, extId);
		GET_HOST_EXT(hostAmbisonic, CLAP_EXT_AMBISONIC);
		GET_HOST_EXT(hostAudioPortsConfig, CLAP_EXT_AUDIO_PORTS_CONFIG);
		GET_HOST_EXT(hostAudioPorts, CLAP_EXT_AUDIO_PORTS);
		GET_HOST_EXT(hostGui, CLAP_EXT_GUI);
		GET_HOST_EXT(hostLatency, CLAP_EXT_LATENCY);
		GET_HOST_EXT(hostLog, CLAP_EXT_LOG);
		GET_HOST_EXT(hostNoteName, CLAP_EXT_NOTE_NAME);
		GET_HOST_EXT(hostNotePorts, CLAP_EXT_NOTE_PORTS);
		GET_HOST_EXT(hostParams, CLAP_EXT_PARAMS);
		GET_HOST_EXT(hostPresetLoad, CLAP_EXT_PRESET_LOAD);
		GET_HOST_EXT(hostRemoteControls, CLAP_EXT_REMOTE_CONTROLS);
		GET_HOST_EXT(hostState, CLAP_EXT_STATE);
		GET_HOST_EXT(hostSurround, CLAP_EXT_SURROUND);
		GET_HOST_EXT(hostTail, CLAP_EXT_TAIL);
		GET_HOST_EXT(hostThreadCheck, CLAP_EXT_THREAD_CHECK);
		GET_HOST_EXT(hostThreadPool, CLAP_EXT_THREAD_POOL);
		GET_HOST_EXT(hostTimerSupport, CLAP_EXT_TIMER_SUPPORT);
		GET_HOST_EXT(hostTrackInfo, CLAP_EXT_TRACK_INFO);
		GET_HOST_EXT(hostVoiceInfo, CLAP_EXT_VOICE_INFO);
#undef GET_HOST_EXT

		// Webview -> GUI helper
		webviewGui.init(&clapPlugin, host);
		// Don't query the actual host - the helper does that, and provides this proxy which routes messages appropriately
		hostWebview = (const clap_host_webview *)webviewGui.extHostWebview;

		return mainThread->call(ptr[&wclap_plugin::init], ptr);
	}
	void pluginDestroy() {
		mainThread->call(ptr[&wclap_plugin::destroy], ptr);
		destroyCalled = true;
		module.pluginList.release(pluginListIndex);
	}
	bool pluginActivate(double sRate, uint32_t minFrames, uint32_t maxFrames) {
		return audioThread->call(ptr[&wclap_plugin::activate], ptr, sRate, minFrames, maxFrames);
	}
	void pluginDeactivate() {
		audioThread->call(ptr[&wclap_plugin::deactivate], ptr);
	}
	bool pluginStartProcessing() {
		return audioThread->call(ptr[&wclap_plugin::start_processing], ptr);
	}
	void pluginStopProcessing() {
		audioThread->call(ptr[&wclap_plugin::stop_processing], ptr);
	}
	void pluginReset() {
		audioThread->call(ptr[&wclap_plugin::reset], ptr);
	}
	clap_process_status pluginProcess(const clap_process *process) {
		auto scoped = arena->scoped(); // use the audio-thread arena

		auto inEvents = scoped.copyAcross(module.inputEventsTemplate);
		auto outEvents = scoped.copyAcross(module.outputEventsTemplate);
		module.setPlugin(inEvents, pluginListIndex);
		module.setPlugin(outEvents, pluginListIndex);

		// Input/output events
		std::unique_lock<std::recursive_mutex> lock{hostEventsMutex};
		inputEvents.resize(0);
		// Copy across (a recognised/translatable subset of) input events
		auto *eventsIn = process->in_events;
		uint32_t count = eventsIn->size(eventsIn);
		for (uint32_t i = 0; i < count; ++i) {
			tryCopyInputEvent(scoped, eventsIn->get(eventsIn, i));
		}
		hostOutputEvents = process->out_events;

		// The process structure
		wclap_process wProcess{
			.steady_time=process->steady_time,
			.frames_count=process->frames_count,
			.transport={0},
			.audio_inputs={0},
			.audio_outputs={0},
			.audio_inputs_count=process->audio_inputs_count,
			.audio_outputs_count=process->audio_outputs_count,
			.in_events=inEvents,
			.out_events=outEvents
		};
		if (process->transport) {
			// The transport event contains no pointers, so translates directly.
			auto wTransport = *(wclap_event_transport *)process->transport;
			wProcess.transport = scoped.copyAcross(wTransport);
		}

		auto translateBuffer = [&](const clap_audio_buffer &buffer, Pointer<const wclap_audio_buffer> wBufferPtr){
			wclap_audio_buffer wBuffer{
				.data32={0},
				.data64={0},
				.channel_count=buffer.channel_count,
				.latency=buffer.latency,
				.constant_mask=buffer.constant_mask
			};
			// Copy audio data across
			if (buffer.data32) {
				wBuffer.data32 = scoped.array<Pointer<float>>(wBuffer.channel_count);
				for (uint32_t c = 0; c < wBuffer.channel_count; ++c) {
					auto array = scoped.array<float>(wProcess.frames_count);
					audioThread->setArray(array, buffer.data32[c], wProcess.frames_count);
					audioThread->set(wBuffer.data32, array, c);
				}
			}
			if (buffer.data64) {
				wBuffer.data64 = scoped.array<Pointer<double>>(wBuffer.channel_count);
				for (uint32_t c = 0; c < wBuffer.channel_count; ++c) {
					auto array = scoped.array<double>(wProcess.frames_count);
					audioThread->setArray(array, buffer.data64[c], wProcess.frames_count);
					audioThread->set(wBuffer.data64, array, c);
				}
			}
			audioThread->set(wBufferPtr.cast<wclap_audio_buffer>(), wBuffer);
		};
		// Audio inputs
		wProcess.audio_inputs = scoped.array<const wclap_audio_buffer>(wProcess.audio_inputs_count);
		for (uint32_t portIndex = 0; portIndex < wProcess.audio_inputs_count; ++portIndex) {
			translateBuffer(process->audio_inputs[portIndex], wProcess.audio_inputs + portIndex);
		}
		wProcess.audio_outputs = scoped.array<wclap_audio_buffer>(wProcess.audio_outputs_count);
		for (uint32_t portIndex = 0; portIndex < wProcess.audio_outputs_count; ++portIndex) {
			translateBuffer(process->audio_outputs[portIndex], wProcess.audio_outputs + portIndex);
		}

		// Ready - copy the process structure across and call
		auto processPtr = scoped.copyAcross(wProcess);
		auto resultCode = mainThread->call(ptr[&wclap_plugin::process], ptr, processPtr);

		// Events cleanup
		hostOutputEvents = nullptr;
		// Copy back output buffers
		for (uint32_t portIndex = 0; portIndex < wProcess.audio_outputs_count; ++portIndex) {
			auto &buffer = process->audio_outputs[portIndex];
			auto wBuffer = audioThread->get(wProcess.audio_outputs, portIndex);
			if (buffer.data32) {
				for (uint32_t c = 0; c < buffer.channel_count; ++c) {
					Pointer<float> channelPtr = audioThread->get(wBuffer.data32, c);
					audioThread->getArray(channelPtr, buffer.data32[c], wProcess.frames_count);
					checkBuffers(buffer.data32[c], wProcess.frames_count);
				}
			}
			if (buffer.data64) {
				for (uint32_t c = 0; c < buffer.channel_count; ++c) {
					Pointer<double> channelPtr = audioThread->get(wBuffer.data64, c);
					audioThread->getArray(channelPtr, buffer.data64[c], wProcess.frames_count);
					checkBuffers(buffer.data64[c], wProcess.frames_count);
				}
			}
		}
		
		return resultCode;
	}
	template<class S>
	void checkBuffers(S *buffer, size_t length) {
		static constexpr S limit = 100;
		for (size_t i = 0; i < length; ++i) {
			auto a = std::abs(buffer[i]);
			if (!(a < limit)) buffer[i] = 0;
		}
	}

	void pluginOnMainThread() {
		mainThread->call(ptr[&wclap_plugin::on_main_thread], ptr);
	}

	const void * pluginGetExtension(const char *pluginExtId) {
		auto scoped = module.arenaPool.scoped();
		auto extIdPtr = scoped.writeString(pluginExtId);
		auto wclapExt = mainThread->call(ptr[&wclap_plugin::get_extension], ptr, extIdPtr);
		if (!wclapExt) return nullptr;
		
		if (!std::strcmp(pluginExtId, CLAP_EXT_AMBISONIC)) {
			ambisonicExt = wclapExt.cast<const wclap_plugin_ambisonic>();
			static const clap_plugin_ambisonic ext{
				.is_config_supported=clapPluginMethod<&Plugin::ambisonic_is_config_supported>(),
				.get_config=clapPluginMethod<&Plugin::ambisonic_get_config>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_AUDIO_PORTS_ACTIVATION)) {
			audioPortsActivationExt = wclapExt.cast<const wclap_plugin_audio_ports_activation>();
			static const clap_plugin_audio_ports_activation ext{
				.can_activate_while_processing=clapPluginMethod<&Plugin::audioPortsActivation_can_activate_while_processing>(),
				.set_active=clapPluginMethod<&Plugin::audioPortsActivation_set_active>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_AUDIO_PORTS_CONFIG)) {
			audioPortsConfigExt = wclapExt.cast<const wclap_plugin_audio_ports_config>();
			static const clap_plugin_audio_ports_config ext{
				.count=clapPluginMethod<&Plugin::audioPortsConfig_count>(),
				.get=clapPluginMethod<&Plugin::audioPortsConfig_get>(),
				.select=clapPluginMethod<&Plugin::audioPortsConfig_select>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_AUDIO_PORTS_CONFIG_INFO)) {
			audioPortsConfigInfoExt = wclapExt.cast<const wclap_plugin_audio_ports_config_info>();
			static const clap_plugin_audio_ports_config_info ext{
				.current_config=clapPluginMethod<&Plugin::audioPortsConfigInfo_current_config>(),
				.get=clapPluginMethod<&Plugin::audioPortsConfigInfo_get>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_AUDIO_PORTS)) {
			audioPortsExt = wclapExt.cast<const wclap_plugin_audio_ports>();
			static const clap_plugin_audio_ports ext{
				.count=clapPluginMethod<&Plugin::audioPorts_count>(),
				.get=clapPluginMethod<&Plugin::audioPorts_get>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_CONFIGURABLE_AUDIO_PORTS)) {
			configurableAudioPortsExt = wclapExt.cast<const wclap_plugin_configurable_audio_ports>();
			static const clap_plugin_configurable_audio_ports ext{
				.can_apply_configuration=clapPluginMethod<&Plugin::configurableAudioPorts_can_apply_configuration>(),
				.apply_configuration=clapPluginMethod<&Plugin::configurableAudioPorts_apply_configuration>(),
			};
			return &ext;
		// TODO: context-menu (CLAP_EXT_CONTEXT_MENU)
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_GUI)) {
			static const clap_plugin_gui ext{
				.is_api_supported=clapPluginMethod<&Plugin::gui_is_api_supported>(),
				.get_preferred_api=clapPluginMethod<&Plugin::gui_get_preferred_api>(),
				.create=clapPluginMethod<&Plugin::gui_create>(),
				.destroy=clapPluginMethod<&Plugin::gui_destroy>(),
				.set_scale=clapPluginMethod<&Plugin::gui_set_scale>(),
				.get_size=clapPluginMethod<&Plugin::gui_get_size>(),
				.can_resize=clapPluginMethod<&Plugin::gui_can_resize>(),
				.get_resize_hints=clapPluginMethod<&Plugin::gui_get_resize_hints>(),
				.adjust_size=clapPluginMethod<&Plugin::gui_adjust_size>(),
				.set_size=clapPluginMethod<&Plugin::gui_set_size>(),
				.set_parent=clapPluginMethod<&Plugin::gui_set_parent>(),
				.set_transient=clapPluginMethod<&Plugin::gui_set_transient>(),
				.suggest_title=clapPluginMethod<&Plugin::gui_suggest_title>(),
				.show=clapPluginMethod<&Plugin::gui_show>(),
				.hide=clapPluginMethod<&Plugin::gui_hide>(),
			};
			guiExt = wclapExt.cast<const wclap_plugin_gui>();
			if (!webviewExt) webviewExt = wclapExt.cast<const wclap_plugin_webview>();
			return guiExt ? &ext : nullptr; // depends on the WCLAP's webview extension, not the GUI one
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_LATENCY)) {
			latencyExt = wclapExt.cast<const wclap_plugin_latency>();
			static const clap_plugin_latency ext{
				.get=clapPluginMethod<&Plugin::latency_get>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_NOTE_NAME)) {
			noteNameExt = wclapExt.cast<const wclap_plugin_note_name>();
			static const clap_plugin_note_name ext{
				.count=clapPluginMethod<&Plugin::noteName_count>(),
				.get=clapPluginMethod<&Plugin::noteName_get>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_NOTE_PORTS)) {
			notePortsExt = wclapExt.cast<const wclap_plugin_note_ports>();
			static const clap_plugin_note_ports ext{
				.count=clapPluginMethod<&Plugin::notePorts_count>(),
				.get=clapPluginMethod<&Plugin::notePorts_get>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_PARAM_INDICATION)) {
			paramIndicationExt = wclapExt.cast<const wclap_plugin_param_indication>();
			static const clap_plugin_param_indication ext{
				.set_mapping=clapPluginMethod<&Plugin::paramIndication_set_mapping>(),
				.set_automation=clapPluginMethod<&Plugin::paramIndication_set_automation>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_PARAMS)) {
			paramsExt = wclapExt.cast<const wclap_plugin_params>();
			static const clap_plugin_params ext{
				.count=clapPluginMethod<&Plugin::params_count>(),
				.get_info=clapPluginMethod<&Plugin::params_get_info>(),
				.get_value=clapPluginMethod<&Plugin::params_get_value>(),
				.value_to_text=clapPluginMethod<&Plugin::params_value_to_text>(),
				.text_to_value=clapPluginMethod<&Plugin::params_text_to_value>(),
				.flush=clapPluginMethod<&Plugin::params_flush>(),
			};
			return &ext;
		// skipping posix-fd-support
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_REMOTE_CONTROLS)) {
			remoteControlsExt = wclapExt.cast<const wclap_plugin_remote_controls>();
			static const clap_plugin_remote_controls ext{
				.count=clapPluginMethod<&Plugin::remoteControls_count>(),
				.get=clapPluginMethod<&Plugin::remoteControls_get>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_RENDER)) {
			renderExt = wclapExt.cast<const wclap_plugin_render>();
			static const clap_plugin_render ext{
				.has_hard_realtime_requirement=clapPluginMethod<&Plugin::render_has_hard_realtime_requirement>(),
				.set=clapPluginMethod<&Plugin::render_set>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_STATE_CONTEXT)) {
			stateContextExt = wclapExt.cast<const wclap_plugin_state_context>();
			static const clap_plugin_state_context ext{
				.save=clapPluginMethod<&Plugin::stateContext_save>(),
				.load=clapPluginMethod<&Plugin::stateContext_load>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_STATE)) {
			stateExt = wclapExt.cast<const wclap_plugin_state>();
			static const clap_plugin_state ext{
				.save=clapPluginMethod<&Plugin::state_save>(),
				.load=clapPluginMethod<&Plugin::state_load>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_SURROUND)) {
			surroundExt = wclapExt.cast<const wclap_plugin_surround>();
			static const clap_plugin_surround ext{
				.is_channel_mask_supported=clapPluginMethod<&Plugin::surround_is_channel_mask_supported>(),
				.get_channel_map=clapPluginMethod<&Plugin::surround_get_channel_map>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_TAIL)) {
			tailExt = wclapExt.cast<const wclap_plugin_tail>();
			static const clap_plugin_tail ext{
				.get=clapPluginMethod<&Plugin::tail_get>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_THREAD_POOL)) {
			threadPoolExt = wclapExt.cast<const wclap_plugin_thread_pool>();
			static const clap_plugin_thread_pool ext{
				.exec=clapPluginMethod<&Plugin::threadPool_exec>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_TIMER_SUPPORT)) {
			timerSupportExt = wclapExt.cast<const wclap_plugin_timer_support>();
			static const clap_plugin_timer_support ext{
				.on_timer=clapPluginMethod<&Plugin::timerSupport_on_timer>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_TRACK_INFO)) {
			trackInfoExt = wclapExt.cast<const wclap_plugin_track_info>();
			static const clap_plugin_track_info ext{
				.changed=clapPluginMethod<&Plugin::trackInfo_changed>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_VOICE_INFO)) {
			voiceInfoExt = wclapExt.cast<const wclap_plugin_voice_info>();
			static const clap_plugin_voice_info ext{
				.get=clapPluginMethod<&Plugin::voiceInfo_get>(),
			};
			return &ext;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_WEBVIEW)) {
			webviewExt = wclapExt.cast<const wclap_plugin_webview>();
			static const clap_plugin_webview ext{
				.get_uri=clapPluginMethod<&Plugin::webview_get_uri>(),
				.get_resource=clapPluginMethod<&Plugin::webview_get_resource>(),
				.receive=clapPluginMethod<&Plugin::webview_receive>()
			};
			return &ext;
		}
		LOG_EXPR(pluginExtId);
		return nullptr;
	}

	const char * translateWclapPortType(Instance &instance, Pointer<const char> portType) {
		auto wclapPortStr = mainThread->getString(wclapInfo.port_type, 16);
		if (wclapPortType == CLAP_PORT_MONO) {
			return CLAP_PORT_MONO;
		} else if (wclapPortType == CLAP_PORT_STEREO) {
			return CLAP_PORT_STEREO;
		} else if (wclapPortType == CLAP_PORT_SURROUND) {
			return CLAP_PORT_SURROUND;
		} else if (wclapPortType == CLAP_PORT_AMBISONIC) {
			return CLAP_PORT_AMBISONIC;
		}
		return "(unknown WCLAP port type)";
	}

	Pointer<const wclap_plugin_ambisonic> ambisonicExt;
	bool ambisonic_is_config_supported(const clap_ambisonic_config_t *config) {
		auto scoped = module.arenaPool.scoped();
		auto configPtr = scoped.copyAcross(wclap_ambisonic_config{config->ordering, config->normalization});
		return mainThread->call(ambisonicExt[&wclap_plugin_ambisonic::is_config_supported], ptr, configPtr);
	}
	bool ambisonic_get_config(bool isInput, uint32_t portIndex, clap_ambisonic_config_t *config) {
		auto scoped = module.arenaPool.scoped();
		auto configPtr = scoped.reserveBlank<wclap_ambisonic_config>();
		if (!mainThread->call(ambisonicExt[&wclap_plugin_ambisonic::get_config], ptr, isInput, portIndex, configPtr)) {
			return false;
		}
		auto wConfig = scoped.instance.get(configPtr);
		*config = {.ordering=wConfig.ordering, .normalization=wConfig.normalization};
		return true;
	}

	Pointer<const wclap_plugin_audio_ports_activation> audioPortsActivationExt;
	bool audioPortsActivation_can_activate_while_processing() {
		return mainThread->call(audioPortsActivationExt[&wclap_plugin_audio_ports_activation::can_activate_while_processing], ptr);
	}
	bool audioPortsActivation_set_active(bool is_input, uint32_t port_index, bool is_active, uint32_t sample_size) {
		return mainThread->call(audioPortsActivationExt[&wclap_plugin_audio_ports_activation::set_active], ptr, is_input, port_index, is_active, sample_size);
	}
	
	Pointer<const wclap_plugin_audio_ports_config> audioPortsConfigExt;
	uint32_t audioPortsConfig_count() {
		return mainThread->call(audioPortsConfigExt[&wclap_plugin_audio_ports_config::count], ptr);
	}
	bool audioPortsConfig_get(uint32_t index, clap_audio_ports_config *config) {
		auto scoped = module.arenaPool.scoped();
		auto configPtr = scoped.reserveBlank<wclap_audio_ports_config>();
		if (!mainThread->call(audioPortsConfigExt[&wclap_plugin_audio_ports_config::get], ptr, index, configPtr)) return false;
		auto wConfig = scoped.instance.get(configPtr);
		*config = clap_audio_ports_config{
			.id=wConfig.id,
			.name=wConfig[&wclap_audio_ports_config::name],
			.input_port_count=wConfig.input_port_count,
			.output_port_count=wConfig.output_port_count,
			.has_main_input=wConfig.has_main_input,
			.main_input_channel_count=wConfig.main_input_channel_count,
			.main_input_port_type=translateWclapPortType(scoped.instance, wConfig.main_input_port_type),
			.has_main_output=wConfig.has_main_output,
			.main_output_channel_count=wConfig.main_output_channel_count,
			.main_output_port_type=translateWclapPortType(scoped.instance, wConfig.main_output_port_type),
		};
		//scoped.instance->getArray(, config->name, CLAP_NAME_SIZE);
		return true;
	}
	bool audioPortsConfig_select(uint32_t config_id) {
		return mainThread->call(audioPortsConfigExt[&wclap_plugin_audio_ports_config::select], ptr, config_id);
	}

	Pointer<const wclap_plugin_audio_ports_config_info> audioPortsConfigInfoExt;
	uint32_t audioPortsConfigInfo_current_config() {
		return mainThread->call(audioPortsConfigInfoExt[&wclap_plugin_audio_ports_config_info::current_config], ptr);
	}
	bool audioPortsConfigInfo_get(uint32_t config_id, uint32_t index, bool isInput, clap_audio_port_info *info) {
		auto scoped = module.arenaPool.scoped();
		auto infoPtr = scoped.reserveBlank<wclap_audio_port_info>();
		if (!mainThread->call(audioPortsConfigInfoExt[&wclap_plugin_audio_ports_config_info::get], ptr, config_id, index, isInput, infoPtr)) return false;
		wclap_audio_port_info wclapInfo = mainThread->get(infoPtr);
		
		const char *portType = nullptr;
		
		*info = clap_audio_port_info{
			.id=wclapInfo.id,
			.name="",
			.flags=wclapInfo.flags,
			.channel_count=wclapInfo.channel_count,
			.port_type=translateWclapPortType(mainThread, wclapInfo.port_type),
			.in_place_pair=wclapInfo.in_place_pair
		};
		std::memcpy(info->name, wclapInfo.name, CLAP_NAME_SIZE);
		return true;
	}

	Pointer<const wclap_plugin_audio_ports> audioPortsExt;
	uint32_t audioPorts_count(bool isInput) {
		return mainThread->call(audioPortsExt[&wclap_plugin_audio_ports::count], ptr, isInput);
	}
	bool audioPorts_get(uint32_t index, bool isInput, clap_audio_port_info *info) {
		auto scoped = module.arenaPool.scoped();
		auto infoPtr = scoped.reserveBlank<wclap_audio_port_info>();
		auto result = mainThread->call(audioPortsExt[&wclap_plugin_audio_ports::get], ptr, index, isInput, infoPtr);
		wclap_audio_port_info wclapInfo = mainThread->get(infoPtr);
		
		const char *portType = nullptr;
		
		*info = clap_audio_port_info{
			.id=wclapInfo.id,
			.name="",
			.flags=wclapInfo.flags,
			.channel_count=wclapInfo.channel_count,
			.port_type=translateWclapPortType(mainThread, wclapInfo.port_type),
			.in_place_pair=wclapInfo.in_place_pair
		};
		std::memcpy(info->name, wclapInfo.name, CLAP_NAME_SIZE);
		return result;
	}

	Pointer<const wclap_plugin_configurable_audio_ports> configurableAudioPortsExt;
	// Both methods have the same shape, but one actually applies the changes
	template<bool applyChanges>
	bool configurableAudioPorts_method(const clap_audio_port_configuration_request *requests, uint32_t request_count) {
		auto scoped = module.arenaPool.scoped();
		auto requestsPtr = scoped.array<wclap_audio_port_configuration_request>(request_count);
		for (size_t i = 0; i < request_count; ++i) {
			auto &request = requests[i];
			wclap_audio_port_configuration_request wRequest{
				.is_input=request.is_input,
				.port_index=request.port_index,
				.channel_count=request.channel_count,
				.port_type=module.translatePortType(request.port_type),
				.port_details={0}
			};
			if (!std::strcmp(request.port_type, CLAP_PORT_SURROUND) && request.port_details) {
				auto channelMapPtr = scoped.array<uint8_t>(request.channel_count);
				scoped.instance.setArray(channelMapPtr, (const uint8_t *)request.port_details, request.channel_count);
				wRequest.port_details = channelMapPtr.cast<const void>();
			} else if (!std::strcmp(request.port_type, CLAP_PORT_AMBISONIC) && request.port_details) {
				auto &info = *(const clap_ambisonic_config *)request.port_details;
				auto infoPtr = scoped.copyAcross(wclap_ambisonic_config{info.ordering, info.normalization});
				wRequest.port_details = infoPtr.cast<const void>();
			}
			scoped.instance.set(requestsPtr + i, wRequest);
		}
		if (applyChanges) {
			return mainThread->call(configurableAudioPortsExt[&wclap_plugin_configurable_audio_ports::apply_configuration], ptr, requestsPtr, request_count);
		} else {
			return mainThread->call(configurableAudioPortsExt[&wclap_plugin_configurable_audio_ports::can_apply_configuration], ptr, requestsPtr, request_count);
		}
	}
	bool configurableAudioPorts_can_apply_configuration(const clap_audio_port_configuration_request *requests, uint32_t request_count) {
		return configurableAudioPorts_method<false>(requests, request_count);
	}
	bool configurableAudioPorts_apply_configuration(const clap_audio_port_configuration_request *requests, uint32_t request_count) {
		return configurableAudioPorts_method<true>(requests, request_count);
	}
	
	Pointer<const wclap_plugin_gui> guiExt;
	bool gui_is_api_supported(const char *api, bool isFloating) {
		return webviewGui.isApiSupported(api, isFloating);
	}
	bool gui_get_preferred_api(const char **api, bool *isFloating) {
		return webviewGui.getPreferredApi(api, isFloating);
	}
	bool gui_create(const char *api, bool isFloating) {
		if (!webviewGui.create(api, isFloating)) return false;
		if (guiExt) {
			// Create a webview GUI in the WCLAP, but continue whether it succeeds or not
			auto scoped = module.arenaPool.scoped();
			auto str = scoped.writeString(CLAP_WINDOW_API_WEBVIEW);
			mainThread->call(guiExt[&wclap_plugin_gui::create], ptr, str, isFloating);
		}
		return true;
	}
	void gui_destroy() {
		if (guiExt) {
			mainThread->call(guiExt[&wclap_plugin_gui::destroy], ptr);
		}
		webviewGui.destroy();
	}
	bool gui_set_scale(double scale) {
		return webviewGui.setScale(scale);
	}
	bool gui_get_size(uint32_t *w, uint32_t *h) {
		if (guiExt) {
			auto scoped = module.arenaPool.scoped();
			auto wPtr = scoped.copyAcross(uint32_t(0));
			auto hPtr = scoped.copyAcross(uint32_t(0));
			if (mainThread->call(guiExt[&wclap_plugin_gui::get_size], ptr, wPtr, hPtr)) {
				*w = mainThread->get(wPtr);
				*h = mainThread->get(hPtr);
				webviewGui.setSize(*w, *h);
				return true;
			}
		}
		return webviewGui.getSize(w, h);
	}
	bool gui_can_resize() {
		if (guiExt) {
			return mainThread->call(guiExt[&wclap_plugin_gui::can_resize], ptr);
		}
		return webviewGui.canResize();
	}
	bool gui_get_resize_hints(clap_gui_resize_hints *hints) {
		if (guiExt) {
			auto scoped = module.arenaPool.scoped();
			auto hintsPtr = scoped.copyAcross(wclap_gui_resize_hints{});
			if (mainThread->call(guiExt[&wclap_plugin_gui::get_resize_hints], ptr, hintsPtr)) {
				auto wHints = mainThread->get(hintsPtr);
				*hints = *(clap_gui_resize_hints *)&wHints; // struct translates directly
				return true;
			}
		}
		return webviewGui.getResizeHints(hints);
	}
	bool gui_adjust_size(uint32_t *w, uint32_t *h) {
		if (guiExt) {
			auto scoped = module.arenaPool.scoped();
			auto wPtr = scoped.copyAcross(*w);
			auto hPtr = scoped.copyAcross(*h);
			if (mainThread->call(guiExt[&wclap_plugin_gui::adjust_size], ptr, wPtr, hPtr)) {
				*w = mainThread->get(wPtr);
				*h = mainThread->get(hPtr);
				return true;
			}
		}
		return webviewGui.adjustSize(w, h);
	}
	bool gui_set_size(uint32_t w, uint32_t h) {
		if (guiExt) {
			mainThread->call(guiExt[&wclap_plugin_gui::set_size], ptr, w, h);
		}
		return webviewGui.setSize(w, h);
	}
	bool gui_set_parent(const clap_window *window) {
		return webviewGui.setParent(window);
	}
	bool gui_set_transient(const clap_window *window) {
		return webviewGui.setTransient(window);
	}
	void gui_suggest_title(const char *title) {
		if (guiExt) {
			auto scoped = module.arenaPool.scoped();
			auto titlePtr = scoped.writeString(title);
			mainThread->call(guiExt[&wclap_plugin_gui::suggest_title], ptr, titlePtr);
		}
		webviewGui.suggestTitle(title);
	}
	bool gui_show() {
		if (guiExt) {
			mainThread->call(guiExt[&wclap_plugin_gui::show], ptr);
		}
		return webviewGui.show();
	}
	bool gui_hide() {
		if (guiExt) {
			mainThread->call(guiExt[&wclap_plugin_gui::hide], ptr);
		}
		return webviewGui.hide();
	}

	Pointer<const wclap_plugin_latency> latencyExt;
	uint32_t latency_get() {
		return mainThread->call(latencyExt[&wclap_plugin_latency::get], ptr);
	}

	Pointer<const wclap_plugin_note_name> noteNameExt;
	uint32_t noteName_count() {
		return mainThread->call(notePortsExt[&wclap_plugin_note_name::count], ptr);
	}
	bool noteName_get(uint32_t index, clap_note_name *note_name) {
		auto scoped = module.arenaPool.scoped();
		auto namePtr = scoped.reserveBlank<wclap_note_name>();
		if (!mainThread->call(notePortsExt[&wclap_plugin_note_name::get], ptr, index namePtr)) return false;
		auto wName = scoped.instance.get(namePtr);
		*note_name = clap_note_name{
			.name=wName.name,
			.port=wName.port,
			.key=wName.key,
			.channel=wName.channel
		};
		return true;
	}

	Pointer<const wclap_plugin_note_ports> notePortsExt;
	uint32_t notePorts_count(bool isInput) {
		return mainThread->call(notePortsExt[&wclap_plugin_note_ports::count], ptr, isInput);
	}
	bool notePorts_get(uint32_t index, bool isInput, clap_note_port_info *info) {
		auto scoped = module.arenaPool.scoped();
		auto infoPtr = scoped.copyAcross(wclap_note_port_info{});
		auto result = mainThread->call(notePortsExt[&wclap_plugin_note_ports::get], ptr, index, isInput, infoPtr);
		wclap_note_port_info wclapInfo = mainThread->get(infoPtr);
		
		*info = clap_note_port_info{
			.id=wclapInfo.id,
			.supported_dialects=wclapInfo.supported_dialects,
			.preferred_dialect=wclapInfo.preferred_dialect,
			.name="",
		};
		std::memcpy(info->name, wclapInfo.name, CLAP_NAME_SIZE);
		return result;
	}

	Pointer<const wclap_plugin_param_indication> paramIndicationExt;
	void paramIndication_set_mapping(uint32_t param_id, bool has_mapping, const clap_color *color, const char *label, const char *description) {
		auto scoped = module.arenaPool.scoped();
		Pointer<wclap_color> colorPtr = (color ? scoped.copyAcross(wclap_color{color->alpha, color->red, color->green, color->blue}) : {0});
		Pointer<const char> labelPtr = (label ? scoped.writeString(label) : {0});
		Pointer<const char> descriptionPtr = (description ? scoped.writeString(description) : {0});
		return mainThread->call(paramIndicationExt[&wclap_plugin_param_indication::set_mapping], ptr, param_id, has_mapping, colorPtr, labelPtr, descriptionPtr);
	}
	void paramIndication_set_automation(uint32_t param_id, uint32_t automation_state, const clap_color *color) {
		auto scoped = module.arenaPool.scoped();
		Pointer<wclap_color> colorPtr = (color ? scoped.copyAcross(wclap_color{color->alpha, color->red, color->green, color->blue}) : {0});
		return mainThread->call(paramIndicationExt[&wclap_plugin_param_indication::set_automation], ptr, param_id, automation_state, colorPtr);
	}

	Pointer<const wclap_plugin_params> paramsExt;
	uint32_t params_count() {
		return mainThread->call(paramsExt[&wclap_plugin_params::count], ptr);
	}
	bool params_get_info(uint32_t index, clap_param_info *info) {
		auto scoped = module.arenaPool.scoped();
		auto infoPtr = scoped.copyAcross(wclap_param_info{});
		auto result = mainThread->call(paramsExt[&wclap_plugin_params::get_info], ptr, index, infoPtr);
		auto wclapInfo = mainThread->get(infoPtr);
		
		void *cookie = nullptr;
		// Store cookie, assuming host pointer size is larger enough (which is almost certainly true)
		if constexpr (sizeof(cookie) >= sizeof(wclapInfo.cookie)) {
			cookie = (void *)size_t(wclapInfo.cookie.wasmPointer);
		}
		
		*info = clap_param_info{
			.id=wclapInfo.id,
			.flags=wclapInfo.flags,
			.cookie=cookie,
			.name="",
			.module="",
			.min_value=wclapInfo.min_value,
			.max_value=wclapInfo.max_value,
			.default_value=wclapInfo.default_value
		};
		std::memcpy(info->name, wclapInfo.name, CLAP_NAME_SIZE);
		std::memcpy(info->module, wclapInfo.module, CLAP_PATH_SIZE);
		
		return result;
	}
	bool params_get_value(clap_id paramId, double *value) {
		auto scoped = module.arenaPool.scoped();
		auto valuePtr = scoped.copyAcross(0.0);
		auto result = mainThread->call(paramsExt[&wclap_plugin_params::get_value], ptr, paramId, valuePtr);
		*value = mainThread->get(valuePtr);
		return result;
	}
	bool params_value_to_text(clap_id paramId, double value, char *text, uint32_t textCapacity) {
		auto scoped = module.arenaPool.scoped();
		auto wclapText = scoped.array<char>(textCapacity);
		auto result = mainThread->call(paramsExt[&wclap_plugin_params::value_to_text], ptr, paramId, value, wclapText, textCapacity);
		mainThread->getArray(wclapText, text, textCapacity);
		return result;
	}
	bool params_text_to_value(clap_id paramId, const char *text, double *value) {
		auto scoped = module.arenaPool.scoped();
		auto wclapText = scoped.writeString(text);
		auto valuePtr = scoped.copyAcross(0.0);
		auto result = mainThread->call(paramsExt[&wclap_plugin_params::text_to_value], ptr, paramId, wclapText, valuePtr);
		*value = mainThread->get(valuePtr);
		return result;
	}
	void params_flush(const clap_input_events *eventsIn, const clap_output_events *eventsOut) {
		auto scoped = arena->scoped(); // use the audio-thread arena
		auto inEvents = scoped.copyAcross(module.inputEventsTemplate);
		auto outEvents = scoped.copyAcross(module.outputEventsTemplate);
		module.setPlugin(inEvents, pluginListIndex);
		module.setPlugin(outEvents, pluginListIndex);

		std::unique_lock<std::recursive_mutex> lock{hostEventsMutex};
		// Copy across (a recognised/translatable subset of) input events
		inputEvents.resize(0);
		uint32_t count = eventsIn->size(eventsIn);
		for (uint32_t i = 0; i < count; ++i) {
			tryCopyInputEvent(scoped, eventsIn->get(eventsIn, i));
		}
		hostOutputEvents = eventsOut;

		mainThread->call(paramsExt[&wclap_plugin_params::flush], ptr, inEvents, outEvents);

		hostOutputEvents = nullptr;
	}

	Pointer<const wclap_plugin_state> stateExt;
	bool state_save(const clap_ostream_t *stream) {
		auto scoped = module.arenaPool.scoped(); // use any arena (main thread)
		auto streamPtr = scoped.copyAcross(module.ostreamTemplate);
		module.setPlugin(streamPtr, pluginListIndex);

		std::unique_lock<std::recursive_mutex> lock{hostStreamsMutex};
		hostOstream = stream;
		auto result = mainThread->call(stateExt[&wclap_plugin_state::save], ptr, streamPtr);
		hostOstream = nullptr;
		return result;
	}
	bool state_load(const clap_istream_t *stream) {
		auto scoped = module.arenaPool.scoped(); // use any arena (main thread)
		auto streamPtr = scoped.copyAcross(module.istreamTemplate);
		module.setPlugin(streamPtr, pluginListIndex);

		std::unique_lock<std::recursive_mutex> lock{hostStreamsMutex};
		hostIstream = stream;
		auto result = mainThread->call(stateExt[&wclap_plugin_state::load], ptr, streamPtr);
		hostIstream = nullptr;
		return result;
	}

	std::atomic<bool> wasFileUri = false;
	Pointer<const wclap_plugin_webview> webviewExt;
	int32_t webview_get_uri(char *uri, uint32_t uriCapacity) {
		auto scoped = module.arenaPool.scoped(); // use any arena (main thread)
		auto uriPtr = scoped.array<char>(uriCapacity);
		auto result = mainThread->call(webviewExt[&wclap_plugin_webview::get_uri], ptr, uriPtr, uriCapacity);
		if (result <= 0 || result > uriCapacity) return result;
		if (uri) mainThread->getArray(uriPtr, uri, uriCapacity);
		if (uri[result] == 0) {
			// Complain, but also try to fix it
			std::cerr << "WCLAP clap_plugin_webview.get_uri() length didn't include NULL terminator. Extending by 1 char." << std::endl;
			++result;
		}
		if (std::string_view(uri, 5) == "file:") {
			wasFileUri = true;
			// Strip all but one leading `/`
			auto *path = uri + 5, *pathEnd = uri + (result - 1);
			while (path[0] == '/' && path[1] == '/') ++path;
			std::string pathStr{path, pathEnd};
			std::strncpy(uri, pathStr.c_str(), uriCapacity);
			return pathStr.size() + 1;
		}
		wasFileUri = false;
		return result;
	}
	bool webview_get_resource(const char *path, char *mime, uint32_t mimeCapacity, const clap_ostream *ostream) {
		if (wasFileUri) {
			auto mapped = module.instanceGroup->mapPath(path);
			if (!mapped) return false;
			for (size_t i = 0; i < mapped->size(); ++i) {
				auto c = (*mapped)[i];
				if (c == '?' || c == '#') { // trim query/hash
					mapped->resize(i);
					break;
				}
			}
			
			auto mimeGuess = webview_gui::helpers::guessMediaType(path);
			std::strncpy(mime, mimeGuess.c_str(), mimeCapacity);
			
			std::ifstream stream{*mapped, std::ios::binary|std::ios::ate};
			if (!stream) {
				std::cerr << "WCLAP: couldn't open file: " << *mapped << std::endl;
				return false;
			}
			
			std::vector<char> buffer;
			auto bufferSize = stream.tellg();
			if (bufferSize > 100*1024*1024) {
				std::cerr << "WCLAP: refused to serve webview UI resource of > 100MB: " << bufferSize << std::endl;
				return false; // This is a webview UI, 100MB max file-size is more than generous
			}
			buffer.resize(bufferSize); // we opened at the end, so this is the file size
			stream.seekg(0);
			// Read entire file into memory at once
			if (stream.read(buffer.data(), buffer.size())) {
				std::cout << "WCLAP: read " << buffer.size() << " bytes for file: " << *mapped << std::endl;
				size_t index = 0;
				while (index < buffer.size()) {
					auto result = ostream->write(ostream, (const void *)(buffer.data() + index), uint64_t(buffer.size() - index));
					if (result <= 0) {
						std::cerr << "WCLAP: failed to write to stream: " << result << std::endl;
						return false;
					}
					index += result;
				}
				return true;
			}
			std::cerr << "WCLAP: couldn't read file: " << *mapped << std::endl;
			return false;
		}

		auto scoped = module.arenaPool.scoped();
		auto streamPtr = scoped.copyAcross(module.ostreamTemplate);
		module.setPlugin(streamPtr, pluginListIndex);
		auto pathPtr = scoped.writeString(path);
		auto mimePtr = scoped.array<char>(mimeCapacity);
		
		std::unique_lock<std::recursive_mutex> lock{hostStreamsMutex};
		hostOstream = ostream;
		auto result = mainThread->call(webviewExt[&wclap_plugin_webview::get_resource], ptr, pathPtr, mimePtr, mimeCapacity, streamPtr);
		mainThread->getArray(mimePtr, mime, mimeCapacity);
		hostOstream = nullptr;
		return result;
	}
	bool webview_receive(const void *buffer, uint32_t size) {
		auto scoped = module.arenaPool.scoped();
		auto bufferPtr = scoped.array<unsigned char>(size);
		mainThread->setArray(bufferPtr, (const unsigned char *)buffer, size);
		return mainThread->call(webviewExt[&wclap_plugin_webview::receive], ptr, bufferPtr.cast<const void>(), size);
	}
};

}; // namespace
