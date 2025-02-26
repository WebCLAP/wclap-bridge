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

Aside from the kerfuffle of setting up the WASM engine, this repo also provides a translation layer for every value in the CLAP API.  All CLAP API functions only accept/return basic numerical values or pointers to structs. 

Pointers to structs made entirely of numerical values can be used by native code by simply offsetting to their location in the WASM instance's memory.  Native structs need to be copied into the WASM memory first.

Pointers to other structs (containing pointers or function-pointers) need to be translated, by translating each of their members individually.  There are no circular references in the CLAP API, so pointers to structs can be translated by translating the pointed-to struct and referencing that.

### Objects and methods

With the exception of `clap_entry::get_factory`, all CLAP API functions are effectively methods, called with an "object" first argument which always contains an opaque `void *` context/data field.

Whenever we translate a function, we expect to know what object will be used as this first argument, and we can store translation data in that object.

We create a distinct native function for each "API position" (struct/field combination) in the CLAP API, and this position is used as part of mapping as well.  

### Native → WASM

We assume(⚠️) that the native host is not constantly compiling/assembling new functions, and therefore there are a finite number of function pointers which could possibly be passed to the WCLAP.  We therefore adapt/insert native functions into the WASM instance's function table.  This requires an `O(log(N))` lookup (for a `std::map`) for each incoming function (to re-use the existing index), but `O(1)` overhead when calling them from WASM.

Structs which contain a `.destroy` method are given a persistent mapping, and their own chunk of memory for temporary data.

### WASM → Native

For WASM functions, 

When making a native proxy for a WASM structs, we fill that pointer to a support data structure which contains:

* which WASM instance it lives in
* a (WASM) pointer to the equivalent WASM struct
* an array mapping every CLAP API function (which might use this struct as its "object" argument) to a WASM function-table index

We assume(⚠️) that the WCLAP will only use one function for a given API-position/object combo, so whenever we translate a WASM function pointer, we set the corresponding entry in the object's function mapping.

### Structs and lifetime

When making a WASM proxy for a native struct, we fill that pointer to a support data structure which contains:

* a (native) pointer to the equivalent native struct

When translating any function 

### Sandboxed WASI

This repo includes an extremely incomplete `wasi_snapshot_preview1` implementation, and functions are only being added when one of our WCLAPs needs them.

**⚠️ I am not an infosec professional, so don't use this with untrusted code without a proper review by someone more qualified.  I have based it on Wasmtime's `preview1.rs` implementation where possible.**

Although a generic sandboxed WASI implementation seems like a sensible thing to exist, the existing sandboxed implementations (Wasmer/Wasmtime) are tightly coupled to their WASM engines, and the others I've found (uvwasi, Wasm3's `m3_api_wasi.c`) aren't sandboxed. 
