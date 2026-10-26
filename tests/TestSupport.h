#pragma once
#include <stdexcept>

inline void Require(bool condition, const char* message) {
	if (!condition)
		throw std::runtime_error(message);
}

void TestDisplayResources();
void FailAllocationAfter(int successfulAllocations);
void TestSettingsBehavior();
void TestTooltipAndRequestGeneration();
