#pragma once

namespace wclap {

struct ValidityChecks {
	bool range = false;

	bool lengths = false;
	unsigned int maxPlugins = 1000;
	unsigned int maxStringLength = 16384; // 16k strings
	
	bool filterOnlyWorking = false;
	
	bool executionDeadlines = false;
	// various deadlines in ms - epoch ticks are every 10ms so it could be longer than this
	struct {
		unsigned int initModule = 100;
		unsigned int malloc = 10;
		unsigned int other = 10;
	} deadlines;
	
	ValidityChecks(unsigned int level) {
		if (level > 0) {
			executionDeadlines = true;
		}
	
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
