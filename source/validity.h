#pragma once

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
		}
		
		// opinionated checks
		if (level >= 200) {
			deadlines.initModule = 150;
			deadlines.other = 50;
			lengths = true;
		}
	}
};

extern ValidityChecks validity;

} // namespace
