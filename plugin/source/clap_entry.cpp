#include "clap/clap.h"

extern bool clap_init(const char *);
extern void clap_deinit();
extern const void * clap_get_factory(const char *);

extern "C" {
	const CLAP_EXPORT clap_plugin_entry clap_entry{
		.clap_version = CLAP_VERSION,
		.init = clap_init,
		.deinit = clap_deinit,
		.get_factory = clap_get_factory
	};
}
