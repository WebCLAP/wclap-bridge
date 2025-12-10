#include "./wclap-instance-wasmtime.cpp"

std::atomic<wasm_engine_t *> global_wasm_engine;

static std::atomic_flag globalEpochRunning;
static void epochThreadFunction() {
	while (globalEpochRunning.test()) {
		wasmtime_engine_increment_epoch(wclap::global_wasm_engine);
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}
static std::thread globalEpochThread;

