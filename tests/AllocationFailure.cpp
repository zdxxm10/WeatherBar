#include "TestSupport.h"
#include <cstdlib>
#include <new>

namespace {
// 故障只作用于发起请求的测试线程，后台线程不受影响。
thread_local int allocationsBeforeFailure = -1;
}

void FailAllocationAfter(int successfulAllocations) {
	allocationsBeforeFailure = successfulAllocations;
}

void* operator new(size_t size) {
	if (allocationsBeforeFailure == 0) {
		allocationsBeforeFailure = -1;
		throw std::bad_alloc();
	}
	if (allocationsBeforeFailure > 0)
		--allocationsBeforeFailure;
	if (void* memory = std::malloc(size ? size : 1))
		return memory;
	throw std::bad_alloc();
}

void operator delete(void* memory) noexcept {
	std::free(memory);
}

void operator delete(void* memory, size_t) noexcept {
	std::free(memory);
}
