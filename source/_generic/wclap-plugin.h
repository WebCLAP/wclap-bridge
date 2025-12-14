// No `#pragma once`, because we deliberately get included multiple times by `../wclap.h`, with different WCLAP_API_NAMESPACE, WCLAP_BRIDGE_NAMESPACE and WCLAP_BRIDGE_IS64 values

#include <atomic>

namespace WCLAP_BRIDGE_NAMESPACE {

using namespace WCLAP_API_NAMESPACE;

struct Plugin {
	WclapModuleBase &module;
	Pointer<const wclap_plugin> ptr;
	MemoryArenaPtr arena; // this holds the `wclap_host` (and anything else we need) for the lifetime of the plugin
	uint32_t pluginListIndex;
	std::atomic<bool> destroyCalled = false;
	
	Plugin(WclapModuleBase &module, Pointer<wclap_host> hostPtr, Pointer<const wclap_plugin> ptr, MemoryArenaPtr arena, const clap_plugin_descriptor *desc) : module(module), ptr(ptr), arena(std::move(arena)) {
LOG_EXPR(hostPtr.wasmPointer);
		// Address using its index in the plugin list (where it's retained)
		pluginListIndex = module.pluginList.retain(this);
		module.instance->set(hostPtr[&wclap_host::host_data], {pluginListIndex});

		clapPlugin.desc = desc;
LOG_EXPR(clapPlugin.desc);
	};
	Plugin(const Plugin& other) = delete;
	~Plugin() {
		if (!destroyCalled) { // This means the WclapModule is closing suddenly, without shutting down the plugins
			// TODO: anything sensible
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
private:

	bool pluginInit() {
LOG_EXPR("pluginInit");
		return true;
	}
	void pluginDestroy() {
		module.instance->call(ptr[&wclap_plugin::destroy], ptr);
		destroyCalled = true;
		module.pluginList.release(pluginListIndex);
	}
	bool pluginActivate(double sRate, uint32_t minFrames, uint32_t maxFrames) {
LOG_EXPR("pluginActivate");
		return true;
	}
	void pluginDeactivate() {
LOG_EXPR("pluginDeactivate");
	}
	bool pluginStartProcessing() {
LOG_EXPR("pluginStartProcessing");
		return true;
	}
	void pluginStopProcessing() {
LOG_EXPR("pluginStopProcessing");
	}
	void pluginReset() {
LOG_EXPR("pluginReset");
	}
	clap_process_status pluginProcess(const clap_process *process) {
LOG_EXPR("pluginProcess");
		size_t length = process->frames_count;
		for (uint32_t o = 0; o < process->audio_outputs_count; ++o) {
			auto &audioOutput = process->audio_outputs[o];
			for (uint32_t c = 0; c < audioOutput.channel_count; ++c) {
				if (audioOutput.data32) {
					auto *buffer = audioOutput.data32[c];
					for (size_t i = 0; i < length; ++i) {
						buffer[i] = (float(i)/length - 0.5f)*0.1f;
					}
				} else if (audioOutput.data64) {
					auto *buffer = audioOutput.data64[c];
					for (size_t i = 0; i < length; ++i) {
						buffer[i] = (float(i)/length - 0.5f)*0.1;
					}
				}
			}
		}
		
		return CLAP_PROCESS_CONTINUE;
	}

	void pluginOnMainThread() {
LOG_EXPR("pluginOnMainThread");
	}

	const void * pluginGetExtension(const char *extId) {
LOG_EXPR("pluginGetExtension");
LOG_EXPR(extId);
		return nullptr;
	}
};
}; // namespace
