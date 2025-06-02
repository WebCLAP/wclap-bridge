#pragma once

namespace wclap { namespace wclap64 {
	using WasmP = uint64_t;
}}

#define WCLAP_MULTIPLE_INCLUDES_NAMESPACE wclap64
#include "../wclapN/wclap-translation-pre.h"
#include "./translate-clap-structs.generated.h"
#include "../wclapN/wclap-translation.h"
#undef WCLAP_MULTIPLE_INCLUDES_NAMESPACE

