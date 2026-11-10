#pragma once
#include "document.h"

namespace xl::detail {
// Geometry produced by layout, with scroll translations kept separate from
// static placement and clipping. It contains no DOM or mutable paragraph data.
class FrameProjection {
    struct Anchor {
        struct Layer {
            Clip clip;
            std::vector<xl_node_id> scrolls;
        };
        xl_node_bounds rect;
        std::vector<xl_node_id> scrolls;
        std::vector<Layer> clips;
        Anchor(xl_node_bounds bounds, const Clip &clip = {});
        void move(float x, float y);
        void scroll(xl_node_id id);
        void clip(const Clip &value);
        std::pair<xl_node_bounds, Clip> project(const ScrollOffsets &offsets) const;
    };
    std::vector<Anchor> paint_, texts_;
    std::vector<std::vector<Anchor>> outlines_;
    std::unordered_map<xl_node_id, std::vector<Anchor>> boxes_, shapes_;
    std::unordered_map<xl_node_id, Anchor> legends_, scrolls_;
    template <class Function> void each(Function &&function);

  public:
    void capture(const DocumentFrame &frame);
    void include(xl_node_id id, xl_node_bounds bounds, const Clip &clip);
    void move(float x, float y);
    void scroll(xl_node_id id);
    void clip(const Clip &value);
    void merge(const FrameProjection &child, bool embedded);
    void fix_paint(size_t index, const BoxPaint &paint);
    void project(DocumentFrame &frame, const ScrollOffsets &offsets) const;
};
} // namespace xl::detail
