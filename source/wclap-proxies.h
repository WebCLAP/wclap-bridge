#pragma once

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
	
	void assignNative(const ClapStruct *n) {
		mutex.lock(); // so temporary use doesn't cross over
		native.store(n);
		assigned.test_and_set();
	}
	void clear() {
		assigned.clear();
		native.store({nullptr, 0});
		mutex.unlock();
	}
	
	operator const ClapStruct *() const {
		return native.load();
	}
private:
	std::atomic_flag assigned = ATOMIC_FLAG_INIT;
	std::mutex mutex;
};

} // namespace
