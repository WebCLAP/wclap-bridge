// No `#pragma once`, because we deliberately get included multiple times by `../wclap.h`, with different WCLAP_API_NAMESPACE, WCLAP_BRIDGE_NAMESPACE and WCLAP_BRIDGE_IS64 values

#include <atomic>

namespace WCLAP_BRIDGE_NAMESPACE {

using namespace WCLAP_API_NAMESPACE;

struct Plugin {
	WclapModuleBase &module;
	
	Pointer<const wclap_plugin> ptr;
	MemoryArenaPtr arena; // this holds the `wclap_host` (and anything else we need) for the lifetime of the plugin
	std::unique_ptr<Instance> audioThread;
	uint32_t pluginListIndex;
	std::atomic<bool> destroyCalled = false;
	const clap_host *host;
	
	Plugin(WclapModuleBase &module, const clap_host *host, Pointer<wclap_host> hostPtr, Pointer<const wclap_plugin> ptr, MemoryArenaPtr arena, const clap_plugin_descriptor *desc) : module(module), ptr(ptr), arena(std::move(arena)), audioThread(module.instanceGroup->startInstance()) {
		// Address using its index in the plugin list (where it's retained)
		pluginListIndex = module.pluginList.retain(this);
		module.mainThread->set(hostPtr[&wclap_host::host_data], {pluginListIndex});

		clapPlugin.desc = desc;
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
		return module.mainThread->call(ptr[&wclap_plugin::init], ptr);
	}
	void pluginDestroy() {
		module.mainThread->call(ptr[&wclap_plugin::destroy], ptr);
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
		module.mainThread->call(ptr[&wclap_plugin::on_main_thread], ptr);
	}

	const void * pluginGetExtension(const char *extId) {
LOG_EXPR("pluginGetExtension");
LOG_EXPR(extId);
		return nullptr;
	}
};

const clap_host * getHostFromPlugin(Plugin *plugin) {
	return plugin->host;
}

}; // namespace
