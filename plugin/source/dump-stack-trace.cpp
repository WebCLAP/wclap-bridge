#if defined(__has_include) && __has_include(<stdio.h>) && __has_include(<signal.h>) && __has_include(<execinfo.h>)

#include <stdio.h>
#include <signal.h>
#include <execinfo.h>

struct PosixDumpStackTrace {
	PosixDumpStackTrace() {
		signal(SIGABRT, handler);
		signal(SIGSEGV, handler);
		signal(SIGBUS, handler);
		signal(SIGILL, handler);
		signal(SIGFPE, handler);
  	}
  	
  	static void handler(int signum) {
		const char *name = "???";
		if (signum == SIGABRT) name = "SIGABRT";
		if (signum == SIGSEGV) name = "SIGSEGV";
		if (signum == SIGBUS) name = "SIGBUS";
		if (signum == SIGILL) name = "SIGILL";
		if (signum == SIGFPE) name = "SIGFPE";

		// no streams or anything, as simple as possible
		fprintf(stderr, "Caught signal %d (%s)\n", signum, name);

		void *buffer[64];
		auto frames = backtrace(buffer, 64);
		if (frames > 2) {
			fprintf(stderr, "stack trace:\n");
			// Skip top 2 frames (this handler, and the system signal-trap function)
			backtrace_symbols_fd(buffer + 2, frames - 2, 2/*FD for stderr*/);
		}
		signal(signum, SIG_DFL); // deregister - the signal will get raised again and take down the process
	}
};
static PosixDumpStackTrace dumpStackTrace;

#endif
