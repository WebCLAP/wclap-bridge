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
	const clap_host_audio_ports *hostAudioPorts = nullptr;
	const clap_host_gui *hostGui = nullptr;
	const clap_host_latency *hostLatency = nullptr;
	const clap_host_params *hostParams = nullptr;
	const clap_host_state *hostState = nullptr;
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
		GET_HOST_EXT(hostAudioPorts, CLAP_EXT_AUDIO_PORTS);
		GET_HOST_EXT(hostGui, CLAP_EXT_GUI);
		GET_HOST_EXT(hostLatency, CLAP_EXT_LATENCY);
		GET_HOST_EXT(hostParams, CLAP_EXT_PARAMS);
		GET_HOST_EXT(hostState, CLAP_EXT_STATE);
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
		
		if (!std::strcmp(pluginExtId, CLAP_EXT_AUDIO_PORTS)) {
			static const clap_plugin_audio_ports ext{
				.count=clapPluginMethod<&Plugin::audioPortsCount>(),
				.get=clapPluginMethod<&Plugin::audioPortsGet>(),
			};
			audioPortsExt = wclapExt.cast<const wclap_plugin_audio_ports>();
			return audioPortsExt ? &ext : nullptr;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_GUI)) {
			static const clap_plugin_gui ext{
				.is_api_supported=clapPluginMethod<&Plugin::guiIsApiSupported>(),
				.get_preferred_api=clapPluginMethod<&Plugin::guiGetPreferredApi>(),
				.create=clapPluginMethod<&Plugin::guiCreate>(),
				.destroy=clapPluginMethod<&Plugin::guiDestroy>(),
				.set_scale=clapPluginMethod<&Plugin::guiSetScale>(),
				.get_size=clapPluginMethod<&Plugin::guiGetSize>(),
				.can_resize=clapPluginMethod<&Plugin::guiCanResize>(),
				.get_resize_hints=clapPluginMethod<&Plugin::guiGetResizeHints>(),
				.adjust_size=clapPluginMethod<&Plugin::guiAdjustSize>(),
				.set_size=clapPluginMethod<&Plugin::guiSetSize>(),
				.set_parent=clapPluginMethod<&Plugin::guiSetParent>(),
				.set_transient=clapPluginMethod<&Plugin::guiSetTransient>(),
				.suggest_title=clapPluginMethod<&Plugin::guiSuggestTitle>(),
				.show=clapPluginMethod<&Plugin::guiShow>(),
				.hide=clapPluginMethod<&Plugin::guiHide>(),
			};
			guiExt = wclapExt.cast<const wclap_plugin_gui>();
			if (!webviewExt) webviewExt = wclapExt.cast<const wclap_plugin_webview>();
			return guiExt ? &ext : nullptr; // depends on the WCLAP's webview extension, not the GUI one
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_LATENCY)) {
			static const clap_plugin_latency ext{
				.get=clapPluginMethod<&Plugin::latencyGet>(),
			};
			latencyExt = wclapExt.cast<const wclap_plugin_latency>();
			return latencyExt ? &ext : nullptr;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_PARAMS)) {
			static const clap_plugin_params ext{
				.count=clapPluginMethod<&Plugin::paramsCount>(),
				.get_info=clapPluginMethod<&Plugin::paramsGetInfo>(),
				.get_value=clapPluginMethod<&Plugin::paramsGetValue>(),
				.value_to_text=clapPluginMethod<&Plugin::paramsValueToText>(),
				.text_to_value=clapPluginMethod<&Plugin::paramsTextToValue>(),
				.flush=clapPluginMethod<&Plugin::paramsFlush>(),
			};
			paramsExt = wclapExt.cast<const wclap_plugin_params>();
			return paramsExt ? &ext : nullptr;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_STATE)) {
			static const clap_plugin_state ext{
				.save=clapPluginMethod<&Plugin::stateSave>(),
				.load=clapPluginMethod<&Plugin::stateLoad>(),
			};
			stateExt = wclapExt.cast<const wclap_plugin_state>();
			return stateExt ? &ext : nullptr;
		} else if (!std::strcmp(pluginExtId, CLAP_EXT_WEBVIEW)) {
			static const clap_plugin_webview ext{
				.get_uri=clapPluginMethod<&Plugin::webviewGetUri>(),
				.get_resource=clapPluginMethod<&Plugin::webviewGetResource>(),
				.receive=clapPluginMethod<&Plugin::webviewReceive>()
			};
			webviewExt = wclapExt.cast<const wclap_plugin_webview>();
			return webviewExt ? &ext : nullptr;
		}
		LOG_EXPR(pluginExtId);
		return nullptr;
	}
	
	Pointer<const wclap_plugin_audio_ports> audioPortsExt;
	uint32_t audioPortsCount(bool isInput) {
		return mainThread->call(audioPortsExt[&wclap_plugin_audio_ports::count], ptr, isInput);
	}
	bool audioPortsGet(uint32_t index, bool isInput, clap_audio_port_info *info) {
		auto scoped = module.arenaPool.scoped();
		auto infoPtr = scoped.copyAcross(wclap_audio_port_info{});
		auto result = mainThread->call(audioPortsExt[&wclap_plugin_audio_ports::get], ptr, index, isInput, infoPtr);
		wclap_audio_port_info wclapInfo = mainThread->get(infoPtr);
		
		const char *portType = nullptr;
		auto wclapPortType = mainThread->getString(wclapInfo.port_type, 16);
		if (wclapPortType == CLAP_PORT_MONO) {
			portType = CLAP_PORT_MONO;
		} else if (wclapPortType == CLAP_PORT_STEREO) {
			portType = CLAP_PORT_STEREO;
		} else if (wclapPortType == CLAP_PORT_SURROUND) {
			portType = CLAP_PORT_SURROUND;
		} else if (wclapPortType == CLAP_PORT_AMBISONIC) {
			portType = CLAP_PORT_AMBISONIC;
		}
		
		*info = clap_audio_port_info{
			.id=wclapInfo.id,
			.name="",
			.flags=wclapInfo.flags,
			.channel_count=wclapInfo.channel_count,
			.port_type=portType,
			.in_place_pair=wclapInfo.in_place_pair
		};
		std::memcpy(info->name, wclapInfo.name, CLAP_NAME_SIZE);
		return result;
	}
	
	Pointer<const wclap_plugin_gui> guiExt;
	bool guiIsApiSupported(const char *api, bool isFloating) {
		return webviewGui.isApiSupported(api, isFloating);
	}
	bool guiGetPreferredApi(const char **api, bool *isFloating) {
		return webviewGui.getPreferredApi(api, isFloating);
	}
	bool guiCreate(const char *api, bool isFloating) {
		if (!webviewGui.create(api, isFloating)) return false;
		if (guiExt) {
			// Create a webview GUI in the WCLAP, but continue whether it succeeds or not
			auto scoped = module.arenaPool.scoped();
			auto str = scoped.writeString(CLAP_WINDOW_API_WEBVIEW);
			mainThread->call(guiExt[&wclap_plugin_gui::create], ptr, str, isFloating);
		}
		return true;
	}
	void guiDestroy() {
		if (guiExt) {
			mainThread->call(guiExt[&wclap_plugin_gui::destroy], ptr);
		}
		webviewGui.destroy();
	}
	bool guiSetScale(double scale) {
		return webviewGui.setScale(scale);
	}
	bool guiGetSize(uint32_t *w, uint32_t *h) {
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
	bool guiCanResize() {
		if (guiExt) {
			return mainThread->call(guiExt[&wclap_plugin_gui::can_resize], ptr);
		}
		return webviewGui.canResize();
	}
	bool guiGetResizeHints(clap_gui_resize_hints *hints) {
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
	bool guiAdjustSize(uint32_t *w, uint32_t *h) {
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
	bool guiSetSize(uint32_t w, uint32_t h) {
		if (guiExt) {
			mainThread->call(guiExt[&wclap_plugin_gui::set_size], ptr, w, h);
		}
		return webviewGui.setSize(w, h);
	}
	bool guiSetParent(const clap_window *window) {
		return webviewGui.setParent(window);
	}
	bool guiSetTransient(const clap_window *window) {
		return webviewGui.setTransient(window);
	}
	void guiSuggestTitle(const char *title) {
		if (guiExt) {
			auto scoped = module.arenaPool.scoped();
			auto titlePtr = scoped.writeString(title);
			mainThread->call(guiExt[&wclap_plugin_gui::suggest_title], ptr, titlePtr);
		}
		webviewGui.suggestTitle(title);
	}
	bool guiShow() {
		if (guiExt) {
			mainThread->call(guiExt[&wclap_plugin_gui::show], ptr);
		}
		return webviewGui.show();
	}
	bool guiHide() {
		if (guiExt) {
			mainThread->call(guiExt[&wclap_plugin_gui::hide], ptr);
		}
		return webviewGui.hide();
	}

	Pointer<const wclap_plugin_latency> latencyExt;
	uint32_t latencyGet() {
		return mainThread->call(latencyExt[&wclap_plugin_latency::get], ptr);
	}

	Pointer<const wclap_plugin_params> paramsExt;
	uint32_t paramsCount() {
		return mainThread->call(paramsExt[&wclap_plugin_params::count], ptr);
	}
	bool paramsGetInfo(uint32_t index, clap_param_info *info) {
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
	
	bool paramsGetValue(clap_id paramId, double *value) {
		auto scoped = module.arenaPool.scoped();
		auto valuePtr = scoped.copyAcross(0.0);
		auto result = mainThread->call(paramsExt[&wclap_plugin_params::get_value], ptr, paramId, valuePtr);
		*value = mainThread->get(valuePtr);
		return result;
	}
	
	bool paramsValueToText(clap_id paramId, double value, char *text, uint32_t textCapacity) {
		auto scoped = module.arenaPool.scoped();
		auto wclapText = scoped.array<char>(textCapacity);
		auto result = mainThread->call(paramsExt[&wclap_plugin_params::value_to_text], ptr, paramId, value, wclapText, textCapacity);
		mainThread->getArray(wclapText, text, textCapacity);
		return result;
	}

	bool paramsTextToValue(clap_id paramId, const char *text, double *value) {
		auto scoped = module.arenaPool.scoped();
		auto wclapText = scoped.writeString(text);
		auto valuePtr = scoped.copyAcross(0.0);
		auto result = mainThread->call(paramsExt[&wclap_plugin_params::text_to_value], ptr, paramId, wclapText, valuePtr);
		*value = mainThread->get(valuePtr);
		return result;
	}
	
	void paramsFlush(const clap_input_events *eventsIn, const clap_output_events *eventsOut) {
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
	bool stateSave(const clap_ostream_t *stream) {
		auto scoped = module.arenaPool.scoped(); // use any arena (main thread)
		auto streamPtr = scoped.copyAcross(module.ostreamTemplate);
		module.setPlugin(streamPtr, pluginListIndex);

		std::unique_lock<std::recursive_mutex> lock{hostStreamsMutex};
		hostOstream = stream;
		auto result = mainThread->call(stateExt[&wclap_plugin_state::save], ptr, streamPtr);
		hostOstream = nullptr;
		return result;
	}
	bool stateLoad(const clap_istream_t *stream) {
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
	int32_t webviewGetUri(char *uri, uint32_t uriCapacity) {
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
	bool webviewGetResource(const char *path, char *mime, uint32_t mimeCapacity, const clap_ostream *ostream) {
		if (wasFileUri) {
			auto mapped = module.instanceGroup->mapPath(path);
			if (!mapped) return false;
			
			auto mimeGuess = webview_gui::helpers::guessMediaType(path);
			std::strncpy(mime, mimeGuess.c_str(), mimeCapacity);
			
			std::ifstream stream{*mapped, std::ios::binary|std::ios::ate};
			std::vector<char> buffer;
			buffer.resize(stream.tellg()); // we opened at the end, so this is the file size
			stream.seekg(0);
			// Read entire file into memory at once
			if (stream.read(buffer.data(), buffer.size())) {
				size_t index = 0;
				while (index < buffer.size()) {
					auto result = ostream->write(ostream, (const void *)(buffer.data() + index), uint64_t(buffer.size() - index));
					if (result <= 0) return false;
					index += result;
				}
				return true;
			}
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
	bool webviewReceive(const void *buffer, uint32_t size) {
		auto scoped = module.arenaPool.scoped();
		auto bufferPtr = scoped.array<unsigned char>(size);
		mainThread->setArray(bufferPtr, (const unsigned char *)buffer, size);
		return mainThread->call(webviewExt[&wclap_plugin_webview::receive], ptr, bufferPtr.cast<const void>(), size);
	}
};

}; // namespace
