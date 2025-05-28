#pragma once

#include "./translate-clap-structs.generated.h"

#define WCLAP_MULTIPLE_INCLUDES_NAMESPACE wclap64
#define WCLAP_MULTIPLE_INCLUDES_WASMP uint64_t
#include "../wclapN/wclap-translation.h"
#undef WCLAP_MULTIPLE_INCLUDES_NAMESPACE
#undef WCLAP_MULTIPLE_INCLUDES_WASMP

