#ifndef XL_TEXT_ICU_SESSION_H
#define XL_TEXT_ICU_SESSION_H
#include "resources/service.h"
#include <memory>

namespace xl::detail {
// Data must use the SDK allocator, never a host context. Each token pins it;
// destroy the final token only after its ICU objects and operations finish.
std::shared_ptr<void> acquire_icu(ResourceLease data);
// Acquire an already initialized session without loading another data copy.
std::shared_ptr<void> try_acquire_icu();
std::size_t icu_data_size();
}
#endif
