#pragma once

namespace wclap {

struct ValidityChecks {
	bool range = false;

	bool lengths = false;
	unsigned int maxPlugins = 1000;
	unsigned int maxStringLength = 16384; // 16k strings
	
	bool filterOnlyWorking = false;
	
	ValidityChecks(unsigned int level) {
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
