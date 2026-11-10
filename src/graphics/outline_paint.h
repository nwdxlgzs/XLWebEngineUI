#pragma once
#include "box_paint.h"
#include <memory>
#include <vector>

namespace xl::detail {
struct OutlineStyle {
    float width = 3, offset = 0;
    uint32_t color = 0xff000000;
    BorderStyle style = BorderStyle::none;
    bool automatic = false;
    bool operator==(const OutlineStyle&) const = default;
    float used_width() const { return automatic ? 2 : width; }
    bool visible() const { return (automatic || style != BorderStyle::none) && used_width() > 0 && (color >> 24); }
};
struct OutlinePaint {
    OutlineStyle style;
    std::vector<xl_node_bounds> fragments;
    Clip clip{};
    std::vector<CornerRadii> radii = {};
};
bool outline_equal(const OutlinePaint& a, const OutlinePaint& b);
// Immutable union geometry: computed once when an outline changes, shared by retained scenes.
class OutlineShape final {
  public:
    explicit OutlineShape(const OutlinePaint& source);
    ~OutlineShape();
    xl_node_bounds ink_bounds() const;
    void paint(SkCanvas* canvas) const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
