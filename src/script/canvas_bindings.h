#pragma once
#include <quickjs.h>
namespace xl::detail {
JSValue canvas_native(JSContext *, JSValueConst, int, JSValueConst *);
extern const char canvas_bootstrap[];
} // namespace xl::detail
