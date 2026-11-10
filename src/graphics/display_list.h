#pragma once
#include <XLWebEngineUI/xl.h>
#include <cstddef>
#include <optional>

namespace xl::detail {
struct PaintCommand {
  enum Kind { box, text, outline } kind = box;
  size_t index = 0, part = 0;
  xl_node_id owner = 0;
  bool operator==(const PaintCommand &) const = default;
};
struct PaintNode {
  xl_node_id parent = 0;
  bool positioned = false, inline_level = false, atomic = false, flex = false,
       grid = false, isolate = false, out_of_flow = false;
  std::optional<int32_t> z;
  int order = 0;
};
} // namespace xl::detail
