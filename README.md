# WCLAP C-API bridge

This provides a bridge which loads a WCLAP (CLAP API compiled to `.wasm`) and provides a native CLAP interface, starting from `get_factory()`.

It can be linked against any WASM engine which implements the [C API](https://github.com/WebAssembly/wasm-c-api).

## How to use it

It's only 6 functions - see [`include/wclap-bridge.h`](include/wclap-bridge.h) for details.

* `wclap_global_init()`: call only once, before any other API calls
* `wclap_global_deinit()`: call at the end to clean up
* `wclap_open()`: opens a WCLAP (including calling its `clap_entry->init()`), returning an opaque pointer
* `wclap_close()`: closes WCLAP (including calling its `clap_entry->deinit()`)
* `wclap_get_factory()`: returns a CLAP-compatible factory, if supported by the WCLAP and the bridge
* `wclap_error()`: returns a string for the latest error, or null pointer.  The returned string is only valid until the next API call (including `wclap_error()`), so make a copy if you want to store it.

The factory returned from `wclap_get_factory()` is a suitable factory from the CLAP API, and can be used as normal.

## WCLAP overview

At its core, WCLAP is the CLAP API compiled to the `wasm32` architecture, provided as a `.wasm`.  As well as `clap_entry`, modules must also:

* _either_ export their memory as `memory`, or import it as `env:memory` (accepting any size of growable memory).
* export `malloc()` (so the host can implement an [arena allocator](https://en.wikipedia.org/wiki/Region-based_memory_management) for its own data structures).  Other memory APIs aren't needed, so this can be a simple shim.
* export a single function table, which can be grown/edited by the host.  This is _probably_ named `__indirect_function_table` as per the [original WASI spec](https://github.com/WebAssembly/WASI/blob/main/legacy/application-abi.md).  

Modules may require WASI imports (only `wasi_snapshot_preview1` is supported here).  If they export _either_ a `_start()` or `_initialize()` function, the host must call it first.

## Design

Aside from the kerfuffle of setting up the WASM engine, this repo also provides a translation layer for every value in the CLAP API.

A "compatible" value is a basic numerical value (32/64-bit ints and floats), or a struct only containing compatible values.  These can be read and written directly by native code, even when located within the WASM instance's memory.

*Non-*compatible values need to be translated.  Pointers to compatible values can be translated from WASM to native by just adding an offset, but structs need to be duplicated (with each field translated), and functions need a more complicated wrapping.  

### Sandboxed WASI

This repo includes an extremely incomplete `wasi_snapshot_preview1` implementation, and functions are only being added when one of our WCLAPs needs them.

**⚠️ I am not a security professional, so don't use this with untrusted code without a proper security review.  I have based it on Wasmtime's `preview1.rs` implementation where possible.**

Although a generic sandboxed WASI implementation seems like a sensible thing to exist, the existing sandboxed implementations (Wasmer/Wasmtime) are tightly coupled to their WASM engines, and the others I've found (uvwasi, Wasm3's `m3_api_wasi.c`) aren't sandboxed. 
