# WCLAP Bridge

This provides a bridge which loads a WCLAP (CLAP API compiled to `.wasm`) and provides a native CLAP interface, starting from `get_factory()`.

It's based on [Wasmtime](https://wasmtime.dev/), through the [C API](https://docs.wasmtime.dev/c-api/index.html).  Alternative runtimes (for different speed/binary-size tradeoffs) and `wasm64` support are on the wishlist.

## How to use the bridge

It's only 7 functions - see [`include/wclap-bridge.h`](include/wclap-bridge.h) for details.

* `wclap_global_init()`: call only once, before any other API calls
* `wclap_global_deinit()`: call at the end to clean up
* `wclap_open()`: opens a WCLAP (including calling its `clap_entry->init()`), returning an opaque pointer which is non-`NULL` on success
* `wclap_open_with_dirs()`: opens a WCLAP, providing optional preset/cache/var directories for WASI
* `wclap_close()`: closes a WCLAP (including calling its `clap_entry->deinit()`) which was _successfully_ opened using `wclap_open()`
* `wclap_get_factory()`: returns a CLAP-compatible factory, if supported by the WCLAP and the bridge
* `wclap_error()`: returns a `const char *` string for the latest API-call failure, or null pointer.

The factories returned from `wclap_get_factory()` are equivalent to a native CLAP's `clap_entry.get_factory()`.

## Errors

The strings returned by `wclap_error()` are only valid until the next API call (including `wclap_error()`), so make a copy if you want to store it.

Errors reported by `wclap_error()` are recoverable, but the WCLAP instance won't work properly.  For now, catastrophic errors call `abort()`.

## What is a WCLAP?

A WCLAP is a CLAP plugin compiled to the `wasm32` architecture.  CLAP plugins are normally dynamic libraries (to be loaded into the host's address space), but WASM doesn't support that.  WCLAPs are therefore standalone, and must have the following imports:

* export `clap_entry` - memory address for the entry struct.
* at least one of:
	* import `env`:`memory` - this must be shared memory (recommended, since it allows multi-threaded use)
	* export `memory` - plugin will be single-threaded only (not recommended)
* export `malloc()` - accepts a size, returns a pointer
* export a single function table, which can be grown/edited by the host.  This is _probably_ named `__indirect_function_table` as per the [original WASI spec](https://github.com/WebAssembly/WASI/blob/main/legacy/application-abi.md), but WCLAP hosts are encouraged to use the first exported function table regardless of name.

The module is placed at the top level of the plugin folder, e.g. `my-plugin.wclap/module.wasm`.  The folder can also contain any other resources used by the plugin.  If made available HTTP, the plugin folder should be compressed as a `.tar.gz`, so that hosts have immediate (synchronous) access to all bundle resources.

WCLAPs _may_ use WASI for sandboxed access to plugin resources.  From the plugin's perspective, it has the following paths (mapped through WASI to platform-appropriate storage):

* `/plugin/` - the plugin folder, e.g. `my-plugin.wclap/`
* `/presets/` - suitable for storing user presets, persistent and shared between instances of this WCLAP.  May be modified by the host to share/merge/reset the preset collection.
* `/var/` - file storage for plugin use, persistent and shared between instances of this WCLAP.  Must not be modified by the host.
* `/cache/` - temporary file storage for the plugin to avoid redundant work.  Ideally persistent (for performance) but may be deleted/emptied by the host while no instances of this WCLAP are active.

## Design

Aside from the kerfuffle of setting up the WASM engine, this repo also provides translates every value in the CLAP API.  This is handled by `WclapTranslationScope`, which

All CLAP API functions only accept/return basic numerical values or pointers to structs. 

Pointers to structs made entirely of numerical values can be used by native code by simply offsetting to their location in the WASM instance's memory.  Native structs need to be copied into the WASM memory first.

Pointers to other structs (containing pointers or function-pointers) need to be translated, by translating each of their members individually.  There are no circular references in the CLAP API, so pointers to structs can be translated by translating the pointed-to struct and referencing that.

### Threads

WASM instances are single-threaded, and multiple execution is achieved through having multiple instances pointing to a common shared memory.

The `Wclap` (having loaded a particular WCLAP) keeps a pool of instances (`WclapThread`s) for use by non-realtime threads.  Plugins themselves also reserve a `WclapThread` for audio-thread calls.  While "audio thread" is a role rather than a particular thread, CLAP guarantees that hosts don't make simultaneous audio-thread calls, so a `WclapThread` per plugin is sufficient for all realtime calls.

### Host methods

All supported host functions are added to the WCLAP's exported function table every time a `WclapThread` is constructed, so that they all have consistent function reference numbers which can be used in host structs.

### Host structures in WASM memory

Each `WclapThread` has a `WclapTranslationScope`s, which own a section of WASM memory (obtained by calling the WCLAP's exported `malloc()`) which it uses to store temporary structures.  It can then pass pointers to these temporary structures as arguments when calling WASM functions.

There are also `WclapTranslationScope`s associated with long-lived host structures (such as `clap_host_t` and associated extensions).  Since the WCLAP doesn't export `free()`, these are returned to a pool when no longer needed, instead of being destroyed.

`WclapTranslationScope`s also own a section of native memory, used as temporary storage for translating values when the WCLAP calls host functions.  By default, this is 64KB, but

## Known issues

There are a few `abort()` calls, some of which get called if the WASM instance throws an error.  These should instead put the `Wclap` in some failure state so no further calls are attempted but cleanup still works.
