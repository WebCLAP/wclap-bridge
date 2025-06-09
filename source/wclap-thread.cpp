#ifndef LOG_EXPR
#	include <iostream>
#	define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;
#endif

#include "./wclap-thread.h"
#include "./wclap.h"
#include "./wclap-arenas.h"
#include "./wclap32/wclap-translation.h"
#include "./wclap64/wclap-translation.h"

#include <cstdlib>

namespace wclap {

WclapThread::WclapThread(Wclap &wclap) : wclap(wclap) {
	startInstance();
}

WclapThreadWithArenas::WclapThreadWithArenas(Wclap &wclap) : WclapThread(wclap) {
	arenas = wclap.claimArenas(this);
}

} // namespace
