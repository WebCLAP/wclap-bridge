#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "./scoped-thread.h"
#include "./wclap.h"
#include "./wclap-thread.h"

namespace wclap {

thread_local ScopedThread *currentScopedThread = nullptr;
thread_local bool currentScopedThreadIsGlobal = false;

ScopedThread::ScopedThread(WclapThread &alreadyLocked, WclapArenas &arenas) : wclap(arenas.wclap), thread(alreadyLocked), arenas(arenas) {
	currentScopedThread = this;
}

// private constructor for weak copies
ScopedThread::ScopedThread(WclapThread &thread, WclapArenas &arenas, void *) : wclap(arenas.wclap), thread(thread), arenas(arenas), locked(false) {}


ScopedThread::~ScopedThread() {
	if (locked) {
		currentScopedThread = nullptr;
		currentScopedThreadIsGlobal = false;
		thread.mutex.unlock();
	}
}

uint8_t * ScopedThread::wasmMemory(uint64_t wasmP, uint64_t size) {
	return wclap.wasmMemory(thread, wasmP, size);
}

size_t ScopedThread::wasmBytes(size_t size, size_t align) {
	return arenas.wasmBytes(size, align);
}

} // namespace
