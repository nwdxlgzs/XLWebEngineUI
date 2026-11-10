#include "native.h"
#ifndef _WIN32
namespace xl::detail {
std::unique_ptr<NativeAX> NativeAX::create(uint32_t, uintptr_t, bool) { throw DocumentFailure{XL_UNSUPPORTED}; }
uint64_t native_ax_objects() noexcept { return 0; }
}
#endif
