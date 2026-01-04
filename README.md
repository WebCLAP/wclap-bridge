# WCLAP Bridge

This repo provides:

* a C-API library which loads a WCLAP and provides a native CLAP interface, starting from `get_factory()`.
* a CLAP/VST3 bridge plugin (in `plugin/`) which makes WCLAPs available in native DAWs

It's based on [Wasmtime](https://wasmtime.dev/), through the [C API](https://docs.wasmtime.dev/c-api/index.html).  Alternative runtimes (for different speed/binary-size tradeoffs) and `wasm64` support are on the wishlist.

It's only been properly tested on MacOS, but builds on Windows/Linux.

### Bridge plugin

The bridge plugin (in `plugin/`) scans standard locations for WCLAPs, and collects them into a single plugin factory.

To keep things separate (and allow native/WCLAP comparison), it prefixes plugin IDs with `wclap:` and names with `[WCLAP]`.  For example, a WCLAP which provides "Example Plugin" as `com.example.plugin` will be advertised as "[WCLAP] Example Plugin" / `wclap:com.example.plugin`.

## How to use the C API

The API is only 10 functions - see [`wclap-bridge.h`](include/wclap-bridge.h) for details.

* `wclap_global_init(timeoutMs)`
* `wclap_global_deinit()`
* `wclap_open()`: opens a WCLAP (including calling its `clap_entry->init()`), returning an opaque pointer which is non-`NULL` on success
* `wclap_open_with_dirs()`: opens a WCLAP, providing optional preset/cache/var directories for WASI
* `wclap_get_error()`: returns a `bool`, and optionally fills a `char *` buffer with the (latest) API failure for a WCLAP module
* `wclap_get_factory()`: returns a CLAP-compatible factory, if supported by the WCLAP and the bridge
* `wclap_close()`: closes a WCLAP (including calling its `clap_entry->deinit()`) which was opened using `wclap_open()`
* `wclap_version()`: returns the `x.y.z` version reported by the WCLAP
* `wclap_bridge_version()`: returns the maximum CLAP version which the bridge supports
* `wclap_set_strings()`: sets optional prefixes for plugin IDs and names (to avoid confusion/collision with the native ones)

The factories returned from `wclap_get_factory()` are equivalent to a native CLAP's `clap_entry.get_factory()`.

### Errors

The strings returned by `wclap_get_error()` are valid until the WCLAP is closed with `wclap_close()`.

Errors reported by `wclap_get_error()` are recoverable (in that you can `wclap_close()`, with no memory corruptions or leaks), but the WCLAP instance won't work properly.  For now, catastrophic errors call `abort()`.

### WCLAP virtual filesystem structure

WCLAPs _may_ use WASI for sandboxed access to plugin resources.  The structure of that virtual filesystem is unspecified, but here are some possible paths which this bridge will (in future) support:

* `/plugin/...` - the plugin folder, e.g. `my-plugin.wclap/`, read-only
* `/presets/` - suitable for storing user presets, persistent and shared between instances of this WCLAP.  May be modified by the host to share/merge/reset the preset collection.
* `/var/` - file storage for plugin use, persistent and shared between instances of this WCLAP.  Must not be modified by the host.
* `/cache/` - temporary file storage for the plugin to avoid redundant work.  Ideally persistent (for performance) but may be deleted/emptied by the host whenever no instances of this WCLAP are active.

## Design

This uses the `wclap-cpp` definitions, and provides a Wasmtime-based implementation of the `Instance` API from that.

From there, it's a manually-implemented CLAP module, where the implementation refers to its internal WCLAP.  There is no automatic translation of return values or structs.

Each plugin owns its own memory arena and `Instance` (basically a thread context) in the WCLAP, so that it's guaranteed to be available for realtime calls.

### 32-/64-bit versions

To avoid duplicate code, any 32-/64-bit specific details are in `source/_generic/`, and get included twice (by `source/wclap-module.h`) with different values for `WCLAP_API_NAMESPACE`, `WCLAP_BRIDGE_NAMESPACE` and `WCLAP_BRIDGE_IS64`.  This means we have to be a _bit_ more careful how those files include each other, since they don't have include-guards or `#pragma once`.

This could be organised differently, with either templates, or compiling two sub-libraries files with multiple definitions.  Open an issue (or even a PR? 😉) if you have strong opinions about this.

### Validity checking

The CLAP module implementation only uses or returns values it understands, so there is an implicit whitelist.  No events or values passed across the WCLAP bridge (in either direction) should have un-translated pointers.

There may (in future) be additional checks to make sure that parameter/note events refer to IDs which actually exist, and that host functions are being called on appropriate threads.  Ideally hosts should code defensively and handle this kind of error safely anyway, but (since native plugins are inherently more trusted than WCLAPs) it's understandable that they might be less cautious, which is why this is on the wishlist.

### Threads

WASM instances themselves are single-threaded, and threading is (currently) only achieved through each thread having its own instance, pointing to a common shared memory.  The `Instance` provides "wasi-threads" imports, so WCLAPs can start their own threads, and this is handled internally by the `Instance`.

Each incoming CLAP API call (from this bridge's host) is assumed to be either on the main thread or the audio thread.  There are two pre-allocated `Instance`s for these two threads.

If a WCLAP doesn't implement threads (i.e. it has no imported shared memory) then only one `Instance` is allocated, and it is locked and used for all incoming API calls.

## Limitations

### Extensions

The following CLAP extensions are currently supported:

| Extension | Plugin → Host | Host → Plugin |
|-----------|---------------|---------------|
| `audio-ports` | `count()`, `get()` | `is_rescan_flag_supported()`, `rescan()` |
| `gui` | Full implementation (15 methods) | — |
| `latency` | `get()` | `changed()` |
| `note-ports` | `count()`, `get()` | `supported_dialects()`, `rescan()` |
| `params` | `count()`, `get_info()`, `get_value()`, `value_to_text()`, `text_to_value()`, `flush()` | `rescan()`, `clear()`, `request_flush()` |
| `preset-load` | `from_location()` | `on_error()`, `loaded()` |
| `render` | `has_hard_realtime_requirement()`, `set()` | — |
| `state` | `save()`, `load()` | `mark_dirty()` |
| `tail` | `get()` | `changed()` |
| `timer-support` | `on_timer()` | `register_timer()`, `unregister_timer()` |
| `track-info` | `changed()` | `get()` |
| `voice-info` | `get()` | `changed()` |
| `webview` | `get_uri()`, `get_resource()`, `receive()` | `send()` |

Additional extensions (including drafts like `thread-pool`, `context-menu`, `surround`, `ambisonic`) are on the wishlist.

### Threads

If the WCLAP doesn't support threads (i.e. it exports memory instead of importing it), then the main thread is used for everything, so the audio thread _may_ block.

There should probably be an API for checking this, so the host can adapt as appropriate.
