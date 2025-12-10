#pragma once

#ifdef __cplusplus
#include <cstdint>
extern "C" {
#else
#include <stdint.h>
#endif

// 0 means no limit
bool wclap_global_init(unsigned int timeLimitMs);
void wclap_global_deinit();

// Opens a WCLAP, returning an opaque identifier (or null on failure)
void * wclap_open(const char *wclapDir);

// Opens a WCLAP with read-only directory `/plugin/` and optional read-write directories `/presets/`, `/cache/` and `/var/`
void * wclap_open_with_dirs(const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir);

// Thread safe, non-blocking unless there's an error (in which case the buffer is filled, and `true` returned)
bool wclap_get_error(void *, char *buffer, size_t bufferCapacity);

// Closes a WCLAP using its opaque identifier.  Unlike clap_entry::deinit(), this MUST be called exactly once after the corresponding wclap_open.
// This really *shouldn't* fail - if it does, then there might be a memory leak.  Normally this would be an `abort()`-worthy bug, but we might not want to lose user data
bool wclap_close(void *);

// identical to `clap_version`
typedef struct wclap_version_triple {
	uint32_t major, minor, revision;
} wclap_version_triple_t;

// Returns a pointer to the opened WCLAP's CLAP API version
const struct wclap_version_triple * wclap_version(void *);

// The CLAP version which this bridge supports (as completely as possible)
const struct wclap_version_triple * wclap_bridge_version();

// Gets a factory (if supported by both the WCLAP and the bridge)
const void * wclap_get_factory(void *, const char *factory_id);

#ifdef __cplusplus
}
#endif
