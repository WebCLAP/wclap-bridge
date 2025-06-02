#pragma once

namespace wclap { namespace wclap32 {
	using WasmP = uint32_t;
}}

#define WCLAP_MULTIPLE_INCLUDES_NAMESPACE wclap32
#include "../wclapN/wclap-translation-pre.h"
#include "./translate-clap-structs.generated.h"
#include "../wclapN/wclap-translation.h"
#undef WCLAP_MULTIPLE_INCLUDES_NAMESPACE

