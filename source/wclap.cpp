#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "wclap-bridge.h"
#include "./wclap.h"
#include "./wclap-thread.h"
#include "./wclap32/wclap-translation.h"
#include "./wclap64/wclap-translation.h"

#include <thread>
#include <atomic>

namespace wclap {

std::atomic_flag global_config_ready = ATOMIC_FLAG_INIT;

//---------- Common Wclap instance ----------//

Wclap::Wclap(const std::string &wclapDir, const std::string &presetDir, const std::string &cacheDir, const std::string &varDir, bool mustLinkDirs) : wclapDir(wclapDir), presetDir(presetDir), cacheDir(cacheDir), varDir(varDir), mustLinkDirs(mustLinkDirs) {
	implCreate();
}

Wclap::~Wclap() {
	if (methods32) wclap::wclap32::methodsDeinitAndDelete(methods32);
	if (methods64) wclap::wclap64::methodsDeinitAndDelete(methods64);

	{ // Clear all threads/scopes
		auto lock = writeLock();
		globalThread = nullptr;
		realtimeThreadPool.clear();
		relaxedThreadPool.clear();
	}
	
	implDestroy();
}

const void * Wclap::getFactory(const char *factory_id) {
	if (wasm64) {
		return wclap::wclap64::methodsGetFactory(methods64, factory_id);
	} else {
		return wclap::wclap32::methodsGetFactory(methods32, factory_id);
	}
}

std::unique_ptr<WclapThread> Wclap::claimRealtimeThread() {
	if (singleThreaded()) return {}; // single-threaded mode - null pointer
	auto lock = writeLock();
	{
		if (realtimeThreadPool.size()) {
			std::unique_ptr<WclapThread> result = std::move(realtimeThreadPool.back());
			realtimeThreadPool.pop_back();
			return result;
		}
	}
	auto *rawPtr = new WclapThread(*this);
	if (wasm64) {
		wclap::wclap64::methodsRegister(methods64, *rawPtr);
	} else {
		wclap::wclap32::methodsRegister(methods32, *rawPtr);
	}
	return std::unique_ptr<WclapThread>(rawPtr);
}

void Wclap::returnRealtimeThread(std::unique_ptr<WclapThread> &ptr) {
	auto lock = writeLock();
	realtimeThreadPool.emplace_back(std::move(ptr));
}

std::unique_ptr<WclapArenas> Wclap::claimArenas() {
	auto scoped = lockThread(); // Shouldn't get stuck in a cycle even if no relaxed threads are available, because the `WclapThread` constructor always passes itself to `.claimArenas()`
	scoped.arenas.resetTemporary();
	return claimArenas(&scoped.thread);
}

std::unique_ptr<WclapArenas> Wclap::claimArenas(WclapThread *lockedThread) {
	if (!lockedThread) return claimArenas();
	auto lock = writeLock();
	if (arenaPool.size()) {
		std::unique_ptr<WclapArenas> result = std::move(arenaPool.back());
		arenaPool.pop_back();
		return result;
	}
	std::unique_ptr<WclapArenas> result{
		new WclapArenas(*this, *lockedThread, arenaList.size())
	};
	arenaList.push_back(result.get());
	return result;
}
void Wclap::returnArenas(std::unique_ptr<WclapArenas> &arenas) {
	arenas->resetIncludingPersistent();
	auto lock = writeLock();
	arenaPool.emplace_back(std::move(arenas));
}

const WclapArenas * Wclap::arenasForWasmContext(uint64_t wasmContextP) {
	size_t index = *lockThread().viewDirectPointer<size_t>(wasmContextP);
	auto lock = readLock();
	if (index < arenaList.size()) return arenaList[index];
	return nullptr;
}

ScopedThread Wclap::lockThread() {
	if (currentScopedThread) {
		// already a scoped thread somewhere up the stack for this thread, re-use that
		return ScopedThread::weakCopy(*currentScopedThread);
	}
	if (singleThreaded()) return lockGlobalThread();

	{
		auto lock = readLock();
		for (auto &threadPtr : relaxedThreadPool) {
			if (threadPtr->mutex.try_lock()) return {*threadPtr, *threadPtr->arenas};
		}
	}

	auto lock = writeLock();
	auto *rawPtr = new WclapThreadWithArenas(*this);
	if (wasm64) {
		wclap::wclap64::methodsRegister(methods64, *rawPtr);
	} else {
		wclap::wclap32::methodsRegister(methods32, *rawPtr);
	}
	rawPtr->mutex.lock();
	relaxedThreadPool.emplace_back(rawPtr);
	return {*rawPtr, *rawPtr->arenas};
}
ScopedThread Wclap::lockThread(WclapThread *ptr, WclapArenas &arenas) {
	arenas.resetTemporary();
	if (!ptr) {
		// We're expecting an exclusive lock (e.g. on a realtime thread).  If it's null, it's almost certainly because the WCLAP is single-threaded.
		// But either way, we need something consistent so that the arena doesn't get used simultaneously
		return lockGlobalThread(arenas);
	}
	ptr->mutex.lock();
	return {*ptr, arenas};
}
ScopedThread Wclap::lockGlobalThread() {
	if (currentScopedThread && currentScopedThreadIsGlobal) {
		// Global thread is already locked somewhere up the stack
		return ScopedThread::weakCopy(*currentScopedThread);
	}
	currentScopedThreadIsGlobal = true;
	return lockThread(globalThread.get(), *globalArenas);
}
ScopedThread Wclap::lockGlobalThread(WclapArenas &arenas) {
	if (currentScopedThread && currentScopedThreadIsGlobal) {
		// Global thread is already locked somewhere up the stack
		return ScopedThread::weakCopy(*globalThread, arenas);
	}
	currentScopedThreadIsGlobal = true;
	return lockThread(globalThread.get(), arenas);
}

void wclapSetError(Wclap &wclap, const char *message) {
	wclap.setError(message);
}

} // namespace

#ifdef WCLAP_ENGINE_WASMTIME
#	include "./wasmtime/wclap-impl.hxx"
#else
#	error No WASM engine selected
#endif
