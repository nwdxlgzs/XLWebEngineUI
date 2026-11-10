#pragma once
#include "text/paragraph.h"
#include "outline_paint.h"
#include "display_list.h"
#include <span>

namespace xl::detail {
struct DocumentFrame;
// Immutable atomic display content. Source text/UTF-16 indexes stay in the
// document frame; a retained scene owns only the data needed for painting.
class PaintScene final {
  public:
    PaintScene(std::span<const BoxPaint> boxes, std::span<const TextItem> texts, std::span<const OutlinePaint> outlines = {});
    explicit PaintScene(const DocumentFrame& source);
    std::shared_ptr<const PaintScene> project(const TextScrollOffsets& offsets) const;
    std::shared_ptr<const PaintScene> images(const ImageUpdates&) const;
    bool scrollable() const;
    void paint(SkCanvas* canvas, float x, float y) const;
    xl_node_bounds ink_bounds() const;
    bool curved() const {return curved_;}
    xl_node_bounds curve_bounds() const {return curve_ink_;}
    bool operator==(const PaintScene&) const;
  private:
    std::vector<BoxPaint> boxes_;
    std::vector<TextPaint> texts_;
    std::vector<OutlinePaint> outlines_;
    std::vector<PaintCommand> order_;
    std::vector<std::shared_ptr<const OutlineShape>> outline_shapes_;
    xl_node_bounds ink_{};
    bool curved_ = false;
    xl_node_bounds curve_ink_{};
    std::shared_ptr<const DocumentFrame> projection_;
};
}
