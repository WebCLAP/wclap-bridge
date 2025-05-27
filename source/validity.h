#pragma once

namespace wclap {

struct ValidityChecks {
	bool range = false;

	bool lengths = false;
	uint32_t maxPlugins = 1000;
	uint32_t maxStringLength = 16384; // 16k strings
	
	bool filterOnlyWorking = false;
	
	ValidityChecks(size_t level) {
		// basic range/type checks
		if (level >= 10) {
			range = true;
		}
		
		if (level >= 100) {
			// semantic = true
		}
		
		// opinionated checks
		if (level >= 200) {
			lengths = true;
			filterOnlyWorking = true;
		}
	}
};

extern ValidityChecks validity;

} // namespace
