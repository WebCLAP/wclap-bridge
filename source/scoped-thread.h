#pragma once

namespace wclap {

struct Wclap;
struct WclapThread;
struct WclapArenas;
struct ScopedThread;

// `Wclap` checks these when it's asked to lock something
extern thread_local ScopedThread *currentScopedThread;
extern thread_local bool currentScopedThreadIsGlobal;

// borrows a locked thread, adding to the pool if necessary
struct ScopedThread {
	Wclap &wclap;
	WclapThread &thread;
	WclapArenas &arenas;

	ScopedThread(WclapThread &alreadyLocked, WclapArenas &arenas);

	ScopedThread(ScopedThread &&other) : wclap(other.wclap), thread(other.thread), arenas(other.arenas), locked(other.locked) {
		other.locked = false;
	}
	~ScopedThread(); // unlocks the mutex (if `locked`)

	// Single-threaded memory *can* change its base pointer after any function calls, so it's a method here so we know it's locked in that case
	uint8_t * wasmMemory(uint64_t wasmP, uint64_t size);

	//---------- helpers for getting/setting stuff from the memory/arenas ----------
	
	size_t wasmBytes(size_t size, size_t align);

	template<class AutoTranslatedStruct>
	AutoTranslatedStruct view(uint64_t wasmP) {
		return AutoTranslatedStruct{wasmMemory(wasmP, AutoTranslatedStruct::wasmSize)};
	}

	template<typename T>
	T * viewDirectPointer(uint64_t wasmP) {
		return (T *)wasmMemory(wasmP, sizeof(T));
	}

	template<class AutoTranslatedStruct>
	struct ArrayView {
		ArrayView(ScopedThread &scoped, uint64_t wasmP) : scoped(scoped), wasmP(wasmP) {}
	
		AutoTranslatedStruct operator[](size_t index) {
			return scoped.view<AutoTranslatedStruct>(wasmP + index*AutoTranslatedStruct::wasmArraySize);
		}
		
		operator bool() const {
			return wasmP;
		}
	private:
		ScopedThread &scoped;
		uint64_t wasmP;
	};
	template<class AutoTranslatedStruct>
	ArrayView<AutoTranslatedStruct> arrayView(uint64_t wasmP) {
		return {*this, wasmP};
	}
	
	template<class AutoTranslatedStruct, typename WasmP>
	AutoTranslatedStruct create(WasmP &wasmP) {
		auto bytes = wasmBytes(AutoTranslatedStruct::wasmSize, AutoTranslatedStruct::wasmAlign);
		wasmP = WasmP(bytes);
		return view<AutoTranslatedStruct>(bytes);
	}

	template<class AutoTranslatedStruct, typename WasmP>
	ArrayView<AutoTranslatedStruct> createArray(size_t count, WasmP &wasmP) {
		auto bytes = wasmBytes(AutoTranslatedStruct::wasmArraySize*count, AutoTranslatedStruct::wasmAlign);;
		wasmP = WasmP(bytes);
		return arrayView<AutoTranslatedStruct>(bytes);
	}

	template<typename T, typename WasmP>
	T * createDirectArray(size_t count, WasmP &wasmP) {
		auto bytes = wasmBytes(sizeof(T)*count, alignof(T));
		wasmP = WasmP(bytes);
		return viewDirectPointer<T>(bytes);
	}
	template<typename T, typename WasmP>
	T * createDirectPointer(WasmP &wasmP) {
		auto bytes = wasmBytes(sizeof(T), alignof(T));
		wasmP = WasmP(bytes);
		return viewDirectPointer<T>(bytes);
	}

private:
	friend class Wclap;

	// Copies the references, but doesn't take responsibility for unlocking
	static ScopedThread weakCopy(const ScopedThread &other) {
		return {other.thread, other.arenas, nullptr};
	}
	static ScopedThread weakCopy(WclapThread &thread, WclapArenas &arenas) {
		return {thread, arenas, nullptr};
	}
	ScopedThread(WclapThread &thread, WclapArenas &arenas, void *);

	bool locked = true;
};

} // namespace
