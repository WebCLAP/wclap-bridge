#pragma once

#ifndef WCLAP_ENGINE_WASMTIME
// Wasmtime is the only supported one for now
#	define WCLAP_ENGINE_WASMTIME
#endif

#include <mutex>
#include <atomic>

namespace wclap {

struct WclapArenas;

template<class ClapStruct>
struct ProxiedClapStruct {
	~ProxiedClapStruct() {
		if (assigned.test()) mutex.unlock();
	}
	
	std::atomic<const ClapStruct *> native;
	
	void assign(const ClapStruct *n) {
		mutex.lock(); // so temporary use doesn't cross over
		native.store(n);
		assigned.test_and_set();
	}
	void clear() { // safe to call even if nothing's stored
		if (assigned.test()) {
			assigned.clear();
			native.store(nullptr);
			mutex.unlock();
		}
	}
	
	operator const ClapStruct *() const {
		return native.load();
	}
private:
	std::atomic_flag assigned = ATOMIC_FLAG_INIT;
	std::mutex mutex;
};

} // namespace
