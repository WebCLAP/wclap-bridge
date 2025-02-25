#ifdef __cplusplus
extern "C" {
#endif

bool wclap_global_init();
void wclap_global_deinit();

// What went wrong (or null - clears old message)
const char * wclap_error();

// Opens a WCLAP, returning an opaque identifier
void * wclap_open(const char *path);
// Closes a WCLAP using its opaque identifier
bool wclap_close(void *);
const void * wclap_get_factory(void *, const char *factory_id);

#ifdef __cplusplus
}
#endif
