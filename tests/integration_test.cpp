/**
 * Headless CLAP Host Integration Tests for wclap-bridge
 *
 * Tests the full plugin lifecycle: load -> init -> activate -> process -> deactivate -> destroy
 */

#include "wclap-bridge.h"
#include <catch2/catch_test_macros.hpp>
#include <clap/clap.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

// Test configuration
static constexpr uint32_t SAMPLE_RATE = 48000;
static constexpr uint32_t BLOCK_SIZE = 256;
static constexpr uint32_t NUM_CHANNELS = 2;

// Path to test WCLAP files (relative to build directory)
static const char* TEST_GAIN_WCLAP = "../tests/wclaps/clack_plugin_gain.wasm";
static const char* TEST_SYNTH_WCLAP = "../tests/wclaps/clack_plugin_polysynth.wasm";

//-----------------------------------------------------------------------------
// Minimal CLAP Host Implementation
//-----------------------------------------------------------------------------

struct TestHost {
    clap_host_t host;
    bool restartRequested = false;
    bool processRequested = false;
    bool callbackRequested = false;

    TestHost() {
        host.clap_version = CLAP_VERSION;
        host.host_data = this;
        host.name = "wclap-bridge-test";
        host.vendor = "WebCLAP";
        host.url = "https://github.com/WebCLAP/wclap-bridge";
        host.version = "1.0.0";
        host.get_extension = hostGetExtension;
        host.request_restart = hostRequestRestart;
        host.request_process = hostRequestProcess;
        host.request_callback = hostRequestCallback;
    }

    static const void* hostGetExtension(const clap_host_t* h, const char* id) {
        // Return nullptr for all extensions - minimal host
        return nullptr;
    }

    static void hostRequestRestart(const clap_host_t* h) {
        auto* self = static_cast<TestHost*>(h->host_data);
        self->restartRequested = true;
    }

    static void hostRequestProcess(const clap_host_t* h) {
        auto* self = static_cast<TestHost*>(h->host_data);
        self->processRequested = true;
    }

    static void hostRequestCallback(const clap_host_t* h) {
        auto* self = static_cast<TestHost*>(h->host_data);
        self->callbackRequested = true;
    }
};

//-----------------------------------------------------------------------------
// Event List Helpers
//-----------------------------------------------------------------------------

struct EmptyInputEvents {
    clap_input_events_t events;

    EmptyInputEvents() {
        events.ctx = this;
        events.size = [](const clap_input_events_t*) -> uint32_t { return 0; };
        events.get = [](const clap_input_events_t*, uint32_t) -> const clap_event_header_t* {
            return nullptr;
        };
    }
};

struct DiscardOutputEvents {
    clap_output_events_t events;

    DiscardOutputEvents() {
        events.ctx = this;
        events.try_push = [](const clap_output_events_t*, const clap_event_header_t*) -> bool {
            return true; // Accept but discard
        };
    }
};

//-----------------------------------------------------------------------------
// Audio Buffer Helpers
//-----------------------------------------------------------------------------

struct TestAudioBuffers {
    std::vector<float> inputData[NUM_CHANNELS];
    std::vector<float> outputData[NUM_CHANNELS];
    float* inputPtrs[NUM_CHANNELS];
    float* outputPtrs[NUM_CHANNELS];
    clap_audio_buffer_t inputBuffer;
    clap_audio_buffer_t outputBuffer;

    TestAudioBuffers(uint32_t blockSize) {
        for (uint32_t ch = 0; ch < NUM_CHANNELS; ++ch) {
            inputData[ch].resize(blockSize, 0.0f);
            outputData[ch].resize(blockSize, 0.0f);
            inputPtrs[ch] = inputData[ch].data();
            outputPtrs[ch] = outputData[ch].data();
        }

        inputBuffer.data32 = inputPtrs;
        inputBuffer.data64 = nullptr;
        inputBuffer.channel_count = NUM_CHANNELS;
        inputBuffer.latency = 0;
        inputBuffer.constant_mask = 0;

        outputBuffer.data32 = outputPtrs;
        outputBuffer.data64 = nullptr;
        outputBuffer.channel_count = NUM_CHANNELS;
        outputBuffer.latency = 0;
        outputBuffer.constant_mask = 0;
    }

    void fillInputWithSine(float frequency, float amplitude = 0.5f) {
        for (uint32_t i = 0; i < inputData[0].size(); ++i) {
            float sample = amplitude * std::sin(2.0f * 3.14159265f * frequency * i / SAMPLE_RATE);
            for (uint32_t ch = 0; ch < NUM_CHANNELS; ++ch) {
                inputData[ch][i] = sample;
            }
        }
    }

    bool outputHasNonZero() const {
        for (uint32_t ch = 0; ch < NUM_CHANNELS; ++ch) {
            for (const auto& sample : outputData[ch]) {
                if (sample != 0.0f) return true;
            }
        }
        return false;
    }

    bool outputIsValid() const {
        for (uint32_t ch = 0; ch < NUM_CHANNELS; ++ch) {
            for (const auto& sample : outputData[ch]) {
                if (std::isnan(sample) || std::isinf(sample)) return false;
            }
        }
        return true;
    }
};

//-----------------------------------------------------------------------------
// Integration Tests
//-----------------------------------------------------------------------------

TEST_CASE("Load WCLAP and enumerate plugins", "[integration]") {
    REQUIRE(wclap_global_init(0));

    void* handle = wclap_open(TEST_GAIN_WCLAP);
    if (!handle) {
        // Try absolute path as fallback
        handle = wclap_open("tests/wclaps/clack_plugin_gain.wasm");
    }
    REQUIRE(handle != nullptr);

    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        wclap_get_factory(handle, CLAP_PLUGIN_FACTORY_ID));
    REQUIRE(factory != nullptr);

    uint32_t count = factory->get_plugin_count(factory);
    INFO("Found " << count << " plugin(s)");
    REQUIRE(count >= 1);

    for (uint32_t i = 0; i < count; ++i) {
        const auto* desc = factory->get_plugin_descriptor(factory, i);
        REQUIRE(desc != nullptr);
        REQUIRE(desc->id != nullptr);
        REQUIRE(desc->name != nullptr);
        INFO("Plugin " << i << ": " << desc->name << " (" << desc->id << ")");
    }

    wclap_close(handle);
    wclap_global_deinit();
}

TEST_CASE("Full plugin lifecycle - gain plugin", "[integration]") {
    REQUIRE(wclap_global_init(0));

    void* handle = wclap_open(TEST_GAIN_WCLAP);
    if (!handle) {
        handle = wclap_open("tests/wclaps/clack_plugin_gain.wasm");
    }
    REQUIRE(handle != nullptr);

    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        wclap_get_factory(handle, CLAP_PLUGIN_FACTORY_ID));
    REQUIRE(factory != nullptr);

    const auto* desc = factory->get_plugin_descriptor(factory, 0);
    REQUIRE(desc != nullptr);

    TestHost testHost;
    const clap_plugin_t* plugin = factory->create_plugin(factory, &testHost.host, desc->id);
    REQUIRE(plugin != nullptr);

    // Init
    REQUIRE(plugin->init(plugin));

    // Activate
    REQUIRE(plugin->activate(plugin, SAMPLE_RATE, BLOCK_SIZE, BLOCK_SIZE));

    // Start processing
    REQUIRE(plugin->start_processing(plugin));

    // Process a few blocks
    TestAudioBuffers buffers(BLOCK_SIZE);
    buffers.fillInputWithSine(440.0f);

    EmptyInputEvents inEvents;
    DiscardOutputEvents outEvents;

    clap_process_t process{};
    process.steady_time = 0;
    process.frames_count = BLOCK_SIZE;
    process.transport = nullptr;
    process.audio_inputs = &buffers.inputBuffer;
    process.audio_outputs = &buffers.outputBuffer;
    process.audio_inputs_count = 1;
    process.audio_outputs_count = 1;
    process.in_events = &inEvents.events;
    process.out_events = &outEvents.events;

    for (int i = 0; i < 10; ++i) {
        clap_process_status status = plugin->process(plugin, &process);
        REQUIRE(status != CLAP_PROCESS_ERROR);
        REQUIRE(buffers.outputIsValid());
        process.steady_time += BLOCK_SIZE;
    }

    // Stop processing
    plugin->stop_processing(plugin);

    // Deactivate
    plugin->deactivate(plugin);

    // Destroy
    plugin->destroy(plugin);

    wclap_close(handle);
    wclap_global_deinit();
}

TEST_CASE("Plugin params extension", "[integration][params]") {
    REQUIRE(wclap_global_init(0));

    void* handle = wclap_open(TEST_GAIN_WCLAP);
    if (!handle) {
        handle = wclap_open("tests/wclaps/clack_plugin_gain.wasm");
    }
    REQUIRE(handle != nullptr);

    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        wclap_get_factory(handle, CLAP_PLUGIN_FACTORY_ID));
    REQUIRE(factory != nullptr);

    const auto* desc = factory->get_plugin_descriptor(factory, 0);
    REQUIRE(desc != nullptr);

    TestHost testHost;
    const clap_plugin_t* plugin = factory->create_plugin(factory, &testHost.host, desc->id);
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->init(plugin));

    // Get params extension
    const auto* params = static_cast<const clap_plugin_params_t*>(
        plugin->get_extension(plugin, CLAP_EXT_PARAMS));

    if (params) {
        uint32_t paramCount = params->count(plugin);
        INFO("Plugin has " << paramCount << " parameter(s)");

        for (uint32_t i = 0; i < paramCount; ++i) {
            clap_param_info_t info{};
            if (params->get_info(plugin, i, &info)) {
                INFO("Param " << i << ": " << info.name << " (id=" << info.id << ")");

                double value = 0.0;
                if (params->get_value(plugin, info.id, &value)) {
                    INFO("  Current value: " << value);
                    CHECK(value >= info.min_value);
                    CHECK(value <= info.max_value);
                }

                // Test value_to_text
                char textBuf[256] = {0};
                if (params->value_to_text(plugin, info.id, value, textBuf, sizeof(textBuf))) {
                    INFO("  Formatted: " << textBuf);
                }
            }
        }
    } else {
        INFO("Plugin does not support params extension");
    }

    plugin->destroy(plugin);
    wclap_close(handle);
    wclap_global_deinit();
}

TEST_CASE("Plugin audio-ports extension", "[integration][audio-ports]") {
    REQUIRE(wclap_global_init(0));

    void* handle = wclap_open(TEST_GAIN_WCLAP);
    if (!handle) {
        handle = wclap_open("tests/wclaps/clack_plugin_gain.wasm");
    }
    REQUIRE(handle != nullptr);

    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        wclap_get_factory(handle, CLAP_PLUGIN_FACTORY_ID));
    REQUIRE(factory != nullptr);

    const auto* desc = factory->get_plugin_descriptor(factory, 0);
    TestHost testHost;
    const clap_plugin_t* plugin = factory->create_plugin(factory, &testHost.host, desc->id);
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->init(plugin));

    const auto* audioPorts = static_cast<const clap_plugin_audio_ports_t*>(
        plugin->get_extension(plugin, CLAP_EXT_AUDIO_PORTS));

    if (audioPorts) {
        uint32_t inputCount = audioPorts->count(plugin, true);
        uint32_t outputCount = audioPorts->count(plugin, false);
        INFO("Audio ports: " << inputCount << " input(s), " << outputCount << " output(s)");

        for (uint32_t i = 0; i < inputCount; ++i) {
            clap_audio_port_info_t info{};
            if (audioPorts->get(plugin, i, true, &info)) {
                INFO("Input " << i << ": " << info.name << " (" << info.channel_count << " ch)");
            }
        }

        for (uint32_t i = 0; i < outputCount; ++i) {
            clap_audio_port_info_t info{};
            if (audioPorts->get(plugin, i, false, &info)) {
                INFO("Output " << i << ": " << info.name << " (" << info.channel_count << " ch)");
            }
        }
    } else {
        INFO("Plugin does not support audio-ports extension");
    }

    plugin->destroy(plugin);
    wclap_close(handle);
    wclap_global_deinit();
}

TEST_CASE("Synth plugin with note events", "[integration][synth]") {
    REQUIRE(wclap_global_init(0));

    void* handle = wclap_open(TEST_SYNTH_WCLAP);
    if (!handle) {
        handle = wclap_open("tests/wclaps/clack_plugin_polysynth.wasm");
    }

    if (!handle) {
        WARN("Synth WCLAP not found, skipping test");
        wclap_global_deinit();
        return;
    }

    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        wclap_get_factory(handle, CLAP_PLUGIN_FACTORY_ID));
    REQUIRE(factory != nullptr);

    const auto* desc = factory->get_plugin_descriptor(factory, 0);
    REQUIRE(desc != nullptr);

    TestHost testHost;
    const clap_plugin_t* plugin = factory->create_plugin(factory, &testHost.host, desc->id);
    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->init(plugin));
    REQUIRE(plugin->activate(plugin, SAMPLE_RATE, BLOCK_SIZE, BLOCK_SIZE));
    REQUIRE(plugin->start_processing(plugin));

    // Create note-on event
    clap_event_note_t noteOn{};
    noteOn.header.size = sizeof(noteOn);
    noteOn.header.time = 0;
    noteOn.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
    noteOn.header.type = CLAP_EVENT_NOTE_ON;
    noteOn.header.flags = 0;
    noteOn.note_id = 1;
    noteOn.port_index = 0;
    noteOn.channel = 0;
    noteOn.key = 60; // Middle C
    noteOn.velocity = 0.8;

    // Input events with note-on
    struct NoteOnEvents {
        clap_input_events_t events;
        clap_event_note_t* noteEvent;

        static uint32_t size(const clap_input_events_t* list) { return 1; }
        static const clap_event_header_t* get(const clap_input_events_t* list, uint32_t index) {
            auto* self = static_cast<NoteOnEvents*>(list->ctx);
            return index == 0 ? &self->noteEvent->header : nullptr;
        }
    };

    NoteOnEvents noteOnEvents;
    noteOnEvents.events.ctx = &noteOnEvents;
    noteOnEvents.events.size = NoteOnEvents::size;
    noteOnEvents.events.get = NoteOnEvents::get;
    noteOnEvents.noteEvent = &noteOn;

    DiscardOutputEvents outEvents;
    TestAudioBuffers buffers(BLOCK_SIZE);

    clap_process_t process{};
    process.steady_time = 0;
    process.frames_count = BLOCK_SIZE;
    process.transport = nullptr;
    process.audio_inputs = nullptr;
    process.audio_outputs = &buffers.outputBuffer;
    process.audio_inputs_count = 0;
    process.audio_outputs_count = 1;
    process.in_events = &noteOnEvents.events;
    process.out_events = &outEvents.events;

    // Process first block with note-on
    clap_process_status status = plugin->process(plugin, &process);
    REQUIRE(status != CLAP_PROCESS_ERROR);
    REQUIRE(buffers.outputIsValid());

    // Synth should produce output after note-on
    INFO("Checking if synth produced output after note-on");
    CHECK(buffers.outputHasNonZero());

    plugin->stop_processing(plugin);
    plugin->deactivate(plugin);
    plugin->destroy(plugin);

    wclap_close(handle);
    wclap_global_deinit();
}
