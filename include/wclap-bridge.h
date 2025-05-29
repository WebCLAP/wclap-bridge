#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
	0 - translate WCLAP values as closely as possible, leave validity-checking to the host
	10 - basic range/type checks (e.g. NULL where not allowed)
	100 - semantic checks (e.g. param IDs exist, note-off matches a note-on, etc.)
	200 - opinionated safety checks
 */
bool wclap_global_init(unsigned int validityCheckLevel);
void wclap_global_deinit();

// Opens a WCLAP, returning an opaque identifier (or null on failure)
void * wclap_open(const char *wclapDir);

// Opens a WCLAP with read-only directory `/plugin/` and optional read-write directories `/presets/`, `/cache/` and `/var/`
void * wclap_open_with_dirs(const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir);

// Closes a WCLAP using its opaque identifier.  Unlike clap_entry::deinit(), this MUST be called exactly once after the corresponding wclap_open.
bool wclap_close(void *);

// Returns a pointer to the opened WCLAP's CLAP API version
const struct clap_version * wclap_version(void *);

// Gets a factory (if supported by both the WCLAP and the bridge)
const void * wclap_get_factory(void *, const char *factory_id);

// What went wrong (or null)
const char * wclap_error();

#ifdef __cplusplus
}
#endif
