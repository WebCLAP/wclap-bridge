# WCLAP Bridge

This provides a C-API library which loads a WCLAP and provides a native CLAP interface, starting from `get_factory()`.

It's based on [Wasmtime](https://wasmtime.dev/), through the [C API](https://docs.wasmtime.dev/c-api/index.html).  Alternative runtimes (for different speed/binary-size tradeoffs) and `wasm64` support are on the wishlist.

## How to use the bridge

It's only 7 functions - see [`include/wclap-bridge.h`](include/wclap-bridge.h) for details.

* `wclap_global_init(validityCheckLevel)`
* `wclap_global_deinit()`
* `wclap_open()`: opens a WCLAP (including calling its `clap_entry->init()`), returning an opaque pointer which is non-`NULL` on success
* `wclap_open_with_dirs()`: opens a WCLAP, providing optional preset/cache/var directories for WASI
* `wclap_close()`: closes a WCLAP (including calling its `clap_entry->deinit()`) which was _successfully_ opened using `wclap_open()`
* `wclap_get_error()`: returns a `bool`, and optionally fills a `char *` buffer with the (latest) API failure for a WCLAP module
* `wclap_get_factory()`: returns a CLAP-compatible factory, if supported by the WCLAP and the bridge

The factories returned from `wclap_get_factory()` are equivalent to a native CLAP's `clap_entry.get_factory()`.

## Errors

The strings returned by `wclap_error()` are only valid until the next API call (including `wclap_error()`), so make a copy if you want to store it.

Errors reported by `wclap_error()` are recoverable, but the WCLAP instance won't work properly.  For now, catastrophic errors call `abort()`.

## WCLAP virtual filesystem structure

WCLAPs _may_ use WASI for sandboxed access to plugin resources.  The structure of that virtual filesystem is unspecified, but here are some possible paths which this bridge will (in future) support:

* `/plugin/...` - the plugin folder, e.g. `my-plugin.wclap/`, read-only
* `/presets/` - suitable for storing user presets, persistent and shared between instances of this WCLAP.  May be modified by the host to share/merge/reset the preset collection.
* `/var/` - file storage for plugin use, persistent and shared between instances of this WCLAP.  Must not be modified by the host.
* `/cache/` - temporary file storage for the plugin to avoid redundant work.  Ideally persistent (for performance) but may be deleted/emptied by the host whenever no instances of this WCLAP are active.

## Design

This uses the `wclap-cpp` definitions, and provides a Wasmtime-based implementation of the `Instance` API from that.

From there, it's a manually-implemented CLAP module, where the implementation refers to its internal WCLAP.  There is no automatic translation of return values or structs.

### 32-/64-bit versions

To avoid duplicate code, any 32-/64-bit specific details are in `source/_generic/`, and get included twice (by `source/wclap-module.h`) with different values for `WCLAP_API_NAMESPACE`, `WCLAP_BRIDGE_NAMESPACE` and `WCLAP_BRIDGE_IS64`.  This means we have to be a _bit_ more careful how those files include each other, since they don't have include-guards or `#pragma once`.

This could be organised differently, with either templates, or compiling two sub-libraries files with multiple definitions.  Open an issue (or even a PR? 😉) if you have strong opinions about this.

### Validity checking

The CLAP module implementation only uses or returns values it understands, so there is an implicit whitelist.  No events or values passed across the WCLAP bridge (in either direction) should have un-translated pointers

There may (in future) be additional checks to make sure that parameter/note events refer to IDs which actually exist, and that host functions are being called on appropriate threads.  Ideally hosts should code defensively and handle this kind of error safely anyway, but (since native plugins are inherently more trusted than WCLAPs) it's understandable that they might be less cautious, which is why this is on the wishlist.

### Threads

WASM instances themselves are single-threaded, and threading is (currently) only achieved through each thread having its own instance, pointing to a common shared memory.  The `Instance` provides "wasi-threads" imports, so WCLAPs can start their own threads, and this is handled internally by the `Instance`.

Each incoming CLAP API call (from this bridge's host) is assumed to be either on the main thread or the audio thread.  There are two pre-allocated `Instance`s for these two threads.

If a WCLAP doesn't implement threads (i.e. it has no imported shared memory) then only one `Instance` is allocated, and it is locked and used for all incoming API calls.
