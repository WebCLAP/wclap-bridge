#pragma once

#include <cstdlib> // size_t
#include <cstdint>

namespace wclap {

struct ValidityChecks {
	bool range = false;

	bool lengths = false;
	unsigned int maxPlugins = 1000;
	unsigned int maxStringLength = 16384; // 16k strings
	unsigned int maxFeaturesLength = 100;  // clap_plugin_descriptor.features
	
	bool filterOnlyWorking = false;
	
	bool executionDeadlines = false;
	// various deadlines in ms - epoch ticks are every 10ms so it could be longer than this
	struct {
		unsigned int initModule = 500;
		unsigned int malloc = 50;
		unsigned int other = 500;
	} deadlines;
	
	bool correctInvalid = false;
	bool avoidNull = false; // some values (like strings or empty lists) _could_ be NULL, but it's weird.
	
	ValidityChecks(unsigned int level) {
		if (level > 0) {
			executionDeadlines = true;
		}
	
		// basic range/type checks
		if (level >= 10) {
			range = true;
		}
		
		if (level >= 100) {
			filterOnlyWorking = true;
			correctInvalid = true;
		}
		
		// opinionated checks
		if (level >= 200) {
			deadlines.initModule = 150;
			deadlines.other = 50;
			lengths = true;

			avoidNull = true;
		}
	}
	
	size_t strlen(const char *str, size_t max=SIZE_MAX) {
		if (!str) return 0;
		if (lengths && maxStringLength < max) max = maxStringLength;
		size_t length = 0;
		while (str[length] && length < max) ++length;
		return length;
	}

	const char * mandatoryString(const char *maybeStr, const char *fallback) {
		if (correctInvalid) {
			if (!maybeStr || !maybeStr[0] /*empty string*/) {
				return fallback;
			}
		}
		return maybeStr;
	}
	const char * optionalString(const char *maybeStr) {
		if (!maybeStr && avoidNull) return "";
		return maybeStr;
	}

	template<typename PointerT>
	static PointerT nullPointer() {
		static PointerT ptr = nullptr;
		return ptr;
	}
	
	void audioSafety(float **buffers, size_t channels, size_t frames) {
		// TODO: check for NaNs or extremely large output
	}
	void audioSafety(double **buffers, size_t channels, size_t frames) {
		// TODO
	}
};

extern ValidityChecks validity;

} // namespace
