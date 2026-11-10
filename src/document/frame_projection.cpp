#include "frame_projection.h"
#include <algorithm>
#include <cmath>

namespace xl::detail {
namespace {
bool bounded(const Clip &clip) {
    return std::isfinite(clip.left) || std::isfinite(clip.top) || std::isfinite(clip.right) ||
           std::isfinite(clip.bottom) || clip.curves;
}
std::pair<float, float> shift(const std::vector<xl_node_id> &ids, const ScrollOffsets &offsets) {
    double x = 0, y = 0;
    for (const auto id : ids)
        if (const auto found = offsets.find(id); found != offsets.end()) {
            x -= found->second.first;
            y -= found->second.second;
        }
    return {static_cast<float>(x), static_cast<float>(y)};
}
void unite(xl_node_bounds &value, const xl_node_bounds &next) {
    if (!nonempty(next))
        return;
    if (!nonempty(value)) {
        value = next;
        return;
    }
    const auto right = std::max(value.x + value.width, next.x + next.width),
               bottom = std::max(value.y + value.height, next.y + next.height);
    value.x = std::min(value.x, next.x);
    value.y = std::min(value.y, next.y);
    value.width = right - value.x;
    value.height = bottom - value.y;
}
} // namespace
FrameProjection::Anchor::Anchor(xl_node_bounds bounds, const Clip &value) : rect(bounds) { clip(value); }
void FrameProjection::Anchor::move(float x, float y) {
    rect.x += x;
    rect.y += y;
    for (auto &layer : clips)
        layer.clip = layer.clip.moved(x, y);
}
void FrameProjection::Anchor::scroll(xl_node_id id) {
    scrolls.push_back(id);
    for (auto &layer : clips)
        layer.scrolls.push_back(id);
}
void FrameProjection::Anchor::clip(const Clip &value) {
    if (bounded(value))
        clips.push_back({value, {}});
}
std::pair<xl_node_bounds, Clip> FrameProjection::Anchor::project(const ScrollOffsets &offsets) const {
    auto box = rect;
    const auto [x, y] = shift(scrolls, offsets);
    box.x += x;
    box.y += y;
    Clip result;
    for (const auto &layer : clips) {
        const auto [dx, dy] = shift(layer.scrolls, offsets);
        result = result.intersect(layer.clip.moved(dx, dy));
    }
    return {box, result};
}
template <class Function> void FrameProjection::each(Function &&fn) {
    for (auto &anchor : paint_)
        fn(anchor);
    for (auto &anchor : texts_)
        fn(anchor);
    for (auto &item : outlines_)
        for (auto &anchor : item)
            fn(anchor);
    for (auto &[id, items] : boxes_) {
        (void)id;
        for (auto &anchor : items)
            fn(anchor);
    }
    for (auto &[id, items] : shapes_) {
        (void)id;
        for (auto &anchor : items)
            fn(anchor);
    }
    for (auto &[id, anchor] : legends_) {
        (void)id;
        fn(anchor);
    }
    for (auto &[id, anchor] : scrolls_) {
        (void)id;
        fn(anchor);
    }
}
void FrameProjection::capture(const DocumentFrame &frame) {
    for (size_t i = paint_.size(); i < frame.paint.size(); ++i) {
        const auto &p = frame.paint[i];
        paint_.emplace_back(xl_node_bounds{p.rect.x, p.rect.y, p.rect.width, p.rect.height}, p.clip);
    }
    for (size_t i = texts_.size(); i < frame.texts.size(); ++i) {
        const auto &p = frame.texts[i];
        texts_.emplace_back(xl_node_bounds{p.x, p.y, 0, 0}, p.clip);
    }
    outlines_.resize(frame.outlines.size());
    for (size_t i = 0; i < frame.outlines.size(); ++i) {
        const auto &p = frame.outlines[i];
        auto &items = outlines_[i];
        for (size_t j = items.size(); j < p.fragments.size(); ++j)
            items.emplace_back(p.fragments[j], p.clip);
    }
    for (const auto &[id, box] : frame.boxes)
        if (!boxes_.contains(id)) {
            const auto clip = frame.box_clips.find(id);
            boxes_[id].emplace_back(box, clip == frame.box_clips.end() ? Clip{} : clip->second);
        }
    for (const auto &[id, items] : frame.hit_shapes) {
        auto &stored = shapes_[id];
        for (size_t i = stored.size(); i < items.size(); ++i)
            stored.emplace_back(items[i].rect);
    }
    for (const auto &[id, box] : frame.disclosure_legends)
        if (!legends_.contains(id))
            legends_.emplace(id, Anchor{box});
    for (const auto &[id, scroll] : frame.scrolls)
        if (!scrolls_.contains(id))
            scrolls_.emplace(id, Anchor{scroll.port, scroll.clip});
}
void FrameProjection::include(xl_node_id id, xl_node_bounds bounds, const Clip &clip) {
    boxes_[id].emplace_back(bounds, clip);
}
void FrameProjection::move(float x, float y) {
    each([&](auto &anchor) { anchor.move(x, y); });
}
void FrameProjection::scroll(xl_node_id id) {
    each([&](auto &anchor) { anchor.scroll(id); });
}
void FrameProjection::clip(const Clip &value) {
    each([&](auto &anchor) { anchor.clip(value); });
}
void FrameProjection::merge(const FrameProjection &child, bool embedded) {
    if (!embedded) {
        paint_.insert(paint_.end(), child.paint_.begin(), child.paint_.end());
        outlines_.insert(outlines_.end(), child.outlines_.begin(), child.outlines_.end());
    }
    texts_.insert(texts_.end(), child.texts_.begin(), child.texts_.end());
    for (const auto &[id, items] : child.boxes_) {
        auto &target = boxes_[id];
        target.insert(target.end(), items.begin(), items.end());
    }
    for (const auto &[id, items] : child.shapes_) {
        auto &target = shapes_[id];
        target.insert(target.end(), items.begin(), items.end());
    }
    legends_.insert(child.legends_.begin(), child.legends_.end());
    scrolls_.insert(child.scrolls_.begin(), child.scrolls_.end());
}
void FrameProjection::fix_paint(size_t index, const BoxPaint &paint) {
    paint_.at(index) = Anchor{{paint.rect.x, paint.rect.y, paint.rect.width, paint.rect.height}, paint.clip};
}
void FrameProjection::project(DocumentFrame &frame, const ScrollOffsets &requested) const {
    ScrollOffsets offsets;
    for (auto &[id, scroll] : frame.scrolls) {
        const auto found = requested.find(id);
        const auto next = found == requested.end() ? std::pair{0.f, 0.f} : found->second;
        scroll.info.x = std::clamp(next.first, scroll.info.min_x, scroll.info.max_x);
        scroll.info.y = std::clamp(next.second, scroll.info.min_y, scroll.info.max_y);
        if (scroll.info.x || scroll.info.y)
            offsets.emplace(id, std::pair{scroll.info.x, scroll.info.y});
    }
    for (size_t i = 0; i < frame.paint.size(); ++i) {
        auto &p = frame.paint[i];
        auto [box, clip] = paint_.at(i).project(offsets);
        p.rect.x = box.x;
        p.rect.y = box.y;
        p.clip = std::move(clip);
    }
    for (size_t i = 0; i < frame.texts.size(); ++i) {
        auto &p = frame.texts[i];
        auto [box, clip] = texts_.at(i).project(offsets);
        p.x = box.x;
        p.y = box.y;
        p.clip = std::move(clip);
        p.scenes = p.paragraph->scroll_scenes(offsets,p.scenes);
    }
    for (size_t i = 0; i < frame.outlines.size(); ++i) {
        auto &p = frame.outlines[i];
        for (size_t j = 0; j < p.fragments.size(); ++j) {
            auto [box, clip] = outlines_.at(i).at(j).project(offsets);
            p.fragments[j] = box;
            p.clip = std::move(clip);
        }
    }
    for (auto &[id, box] : frame.boxes) {
        const auto &anchors = boxes_.at(id);
        bool first = true;
        for (const auto &anchor : anchors) {
            auto [next, clip] = anchor.project(offsets);
            if (first) {
                box = next;
                frame.box_clips[id] = std::move(clip);
                first = false;
            } else
                unite(box, next);
        }
    }
    for (auto &[id, items] : frame.hit_shapes)
        for (size_t i = 0; i < items.size(); ++i)
            items[i].rect = shapes_.at(id).at(i).project(offsets).first;
    for (auto &[id, box] : frame.disclosure_legends)
        box = legends_.at(id).project(offsets).first;
    for (auto &[id, scroll] : frame.scrolls) {
        auto [box, clip] = scrolls_.at(id).project(offsets);
        scroll.port = box;
        scroll.clip = std::move(clip);
    }
    for(auto& paint:frame.paint)if(paint.background) {
        if(const auto scroll=frame.scrolls.find(paint.background_node);scroll!=frame.scrolls.end())
            paint.background_local={scroll->second.info.x,scroll->second.info.y,scroll->second.info.scroll_width,scroll->second.info.scroll_height};
        if(paint.background_canvas_node)if(const auto reference=frame.boxes.find(paint.background_canvas_node);reference!=frame.boxes.end())paint.background_reference=reference->second;
    }
    for (auto &entry : frame.accessibility) {
        auto &ax = entry.node;
        if (ax.role == XL_AX_DOCUMENT && ax.node == frame.identity)
            continue;
        auto visible = frame.visible_bounds(ax.node);
        if (const auto legend = frame.disclosure_legends.find(ax.node); legend != frame.disclosure_legends.end())
            visible = frame.box_clips[ax.node].bounds(legend->second);
        ax.bounds = visible;
        ax.states = (ax.states & ~XL_AX_OFFSCREEN) | (nonempty(visible) ? 0u : XL_AX_OFFSCREEN);
    }
}
} // namespace xl::detail
