#ifdef __cplusplus
extern "C" {
#endif

bool wclap_global_init();
void wclap_global_deinit();

// Opens a WCLAP, returning an opaque identifier (or null on failure)
void * wclap_open(const char *wclapDir);

// Opens a WCLAP with read-only directory `/plugin/` and optional read-write directories `/presets/`, `/cache/` and `/var/`
void * wclap_open_with_dirs(const char *wclapDir, const char *presetDir, const char *cacheDir, const char *varDir);

// Closes a WCLAP using its opaque identifier
bool wclap_close(void *);

// Gets a factory (if supported by both the WCLAP and the bridge)
const void * wclap_get_factory(void *, const char *factory_id);

// What went wrong (or null)
const char * wclap_error();

#ifdef __cplusplus
}
#endif
