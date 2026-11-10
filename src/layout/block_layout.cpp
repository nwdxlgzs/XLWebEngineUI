#include "style/computed.h"
#include "graphics/paint_scene.h"
#include "style/pseudo_elements.h"
#include "document/stacking.h"
#include "graphics/image_content.h"
#include "graphics/canvas_content.h"
#include "include/core/SkFontMgr.h"
#include <algorithm>
#include <charconv>
#include <limits>
#include <optional>
#include <stdexcept>
#include <unicode/utf8.h>

namespace xl::detail {
namespace {
bool skipped(lxb_dom_node_t* node) {
    return node->type != LXB_DOM_NODE_TYPE_ELEMENT || node->local_name == LXB_TAG_HEAD ||
           node->local_name == LXB_TAG_SCRIPT || node->local_name == LXB_TAG_STYLE || node->local_name == LXB_TAG_TITLE;
}
bool out_of_flow(const BoxStyle& style) {
    return style.position.kind==cssposition::Kind::absolute||style.position.kind==cssposition::Kind::fixed;
}
void include(DocumentFrame& frame, xl_node_id node, xl_node_bounds next,bool record=true) {
    if(record)frame.include_projection(node,next);
    const auto [entry, added] = frame.boxes.emplace(node, next);
    if (added) return;
    auto& box = entry->second;
    // Empty inline fragments still have a position, but a nonempty fragment
    // determines the bounding box when one exists (CSSOM client geometry).
    if (next.width == 0 || next.height == 0) return;
    if (box.width == 0 || box.height == 0) {
        box = next;
        return;
    }
    const auto right = std::max(box.x + box.width, next.x + next.width);
    const auto bottom = std::max(box.y + box.height, next.y + next.height);
    box.x = std::min(box.x, next.x);
    box.y = std::min(box.y, next.y);
    box.width = right - box.x;
    box.height = bottom - box.y;
}
TextFormat format(const BoxStyle& style) {
    return {style.font_size,
            style.color,
            style.weight,
            style.italic,
            style.families,
            style.whitespace,
            style.line_height < 0      ? -1
            : style.line_height_factor ? style.line_height * style.font_size
                                       : style.line_height,
            style.tab_size,
            style.tab_is_length,
            style.oblique,
            style.stretch,
            style.baseline_shift,
            style.alignment_group, style.inline_x, style.inline_y};
}
struct InlineBuffer {
    struct Source {
        TextSource value;
        size_t byte_start, byte_end;
    };
    struct Span {
        xl_node_id node;
        TextInlineBox value;
        OutlineStyle outline;
        bool positioned = false;
    };
    std::string text;
    std::vector<TextRun> runs;
    std::vector<Source> sources;
    std::vector<Span> spans;
    std::unordered_map<xl_node_id, size_t> span_ids;
    struct Atomic { uint32_t offset; DocumentFrame frame; };
    std::vector<Atomic> atomics;
    struct Anchor { uint32_t offset; PositionedBox box; bool inline_level; };
    std::vector<Anchor> anchors;
    uint32_t length = 0;
    bool trailing_space = false, line_start = true, previous_cr = false;
    uint32_t space_offset = 0;
    size_t space_byte = 0;

    void emit(std::string_view bytes, uint32_t units, const TextFormat& style,xl_node_id owner=0) {
        if (length > std::numeric_limits<uint32_t>::max() - units) throw DocumentFailure{XL_UNSUPPORTED};
        if (runs.empty() || runs.back().edge || runs.back().object || runs.back().format != style||runs.back().owner!=owner) {runs.push_back({{}, style});runs.back().owner=owner;}
        runs.back().text.append(bytes);
        text.append(bytes);
        length += units;
    }
    void trim_space(Source* current = nullptr) {
        if (!trailing_space) return;
        --length;
        text.erase(space_byte, 1);
        size_t byte = 0;
        for (auto run = runs.begin(); run != runs.end(); ++run) {
            if (space_byte < byte + run->text.size()) {
                run->text.erase(space_byte - byte, 1);
                if (run->text.empty()) runs.erase(run);
                break;
            }
            byte += run->text.size();
        }
        const auto adjust = [&](Source& source) {
            for (auto& boundary : source.value.offsets)
                if (boundary > space_offset) --boundary;
            if (source.byte_start > space_byte) --source.byte_start;
            if (source.byte_end > space_byte) --source.byte_end;
        };
        for (auto& source : sources)
            adjust(source);
        for (auto& span : spans) {
            if (span.value.start > space_offset) --span.value.start;
            if (span.value.end > space_offset) --span.value.end;
        }
        for (auto& atomic : atomics)
            if (atomic.offset > space_offset) --atomic.offset;
        for(auto& anchor:anchors)if(anchor.offset>space_offset)--anchor.offset;
        if (current) {
            adjust(*current);
        }
        trailing_space = false;
    }
    void append(xl_node_id node, std::string_view original, const TextFormat& style) {
        if (original.size() > size_t(std::numeric_limits<int32_t>::max())) throw DocumentFailure{XL_UNSUPPORTED};
        Source source{{node, {length}, {}, std::string(original), {0}}, text.size(), 0};
        for (int32_t offset = 0, count = static_cast<int32_t>(original.size()); offset < count;) {
            const int32_t begin = offset;
            UChar32 scalar;
            U8_NEXT(original.data(), offset, count, scalar);
            if (scalar < 0) throw DocumentFailure{XL_INVALID_ARGUMENT};
            if(scalar>0xffff)source.value.byte_offsets.push_back(TextSource::invalid_byte);
            source.value.byte_offsets.push_back(static_cast<uint32_t>(offset));
            if (scalar == '\n' && previous_cr) {
                previous_cr = false;
                source.value.offsets.push_back(length);
                continue;
            }
            previous_cr = scalar == '\r';
            const bool newline = scalar == '\n' || scalar == '\r' || scalar == '\f';
            const bool whitespace =
                scalar == ' ' || scalar == '\t' || scalar == '\n' || scalar == '\r' || scalar == '\f';
            if (newline && preserves_breaks(style.whitespace)) {
                trim_space(&source);
                emit("\n", 1, style,node);
                line_start = true;
                trailing_space = false;
            } else if (whitespace && collapses_spaces(style.whitespace)) {
                if (!line_start && !trailing_space) {
                    space_offset = length;
                    space_byte = text.size();
                    emit(" ", 1, style,node);
                    trailing_space = true;
                }
            } else {
                const auto before = length;
                emit(original.substr(static_cast<size_t>(begin), static_cast<size_t>(offset - begin)),
                     scalar > 0xffff ? 2u : 1u, style,node);
                if (scalar > 0xffff) source.value.offsets.push_back(before + 1);
                trailing_space = false;
                line_start = false;
            }
            source.value.offsets.push_back(length);
        }
        source.byte_end = text.size();
        sources.push_back(std::move(source));
    }
    void hard_break(const TextFormat& style) {
        trim_space();
        emit("\n", 1, style);
        line_start = true;
        previous_cr = false;
    }
    void edge(const TextFormat& style, float width, bool opening,xl_node_id owner) {
        if (length == std::numeric_limits<uint32_t>::max()) throw DocumentFailure{XL_UNSUPPORTED};
        runs.push_back({"\xef\xbf\xbc", style, opening ? 1 : -1, width});
        runs.back().owner=owner;
        text += "\xef\xbf\xbc";
        ++length;
    }
    void object(const TextFormat& style, std::shared_ptr<const TextObject> value,xl_node_id owner) {
        if (length == std::numeric_limits<uint32_t>::max()) throw DocumentFailure{XL_UNSUPPORTED};
        runs.push_back({"\xef\xbf\xbc", style, 0, 0, std::move(value)});
        runs.back().owner=owner;
        text += "\xef\xbf\xbc";
        ++length;
        trailing_space = line_start = previous_cr = false;
    }
    bool finish() {
        trim_space();
        return !text.empty() && text.back() == '\n';
    }
};

struct Layout;
enum class Measure { none, minimum, maximum };
struct IntrinsicCache : std::unordered_map<xl_node_id, std::pair<float, float>> {
    const ScrollOffsets& scrolls;
    lxb_dom_node_t* propagated = nullptr;
    xl_node_id canvas_background = 0;
    explicit IntrinsicCache(const ScrollOffsets& offsets) : scrolls(offsets) {}
};

void overflow_content(DocumentFrame& frame, xl_node_id id, xl_node_bounds port, const BoxStyle& style,
                      const ScrollOffsets& offsets, float natural_height = 0) {
    if (style.overflow_x == Overflow::visible && style.overflow_y == Overflow::visible) return;
    float left = port.x, top = port.y, right = port.x + port.width, bottom = port.y + port.height;
    for(const auto& bounds:frame.flow_bounds)if(nonempty(bounds)) {
        left=std::min(left,bounds.x);top=std::min(top,bounds.y);
        right=std::max(right,bounds.x+bounds.width+style.padding_right);
        bottom=std::max(bottom,bounds.y+bounds.height+style.padding_bottom);
    }
    for (const auto& [node, bounds] : frame.boxes) {
        const auto visible = frame.visible_bounds(node);
        if (!nonempty(visible)) continue;
        left = std::min(left, visible.x); top = std::min(top, visible.y);
        right = std::max(right, visible.x + visible.width + style.padding_right);
        bottom = std::max(bottom, visible.y + visible.height + style.padding_bottom);
    }
    for (const auto& text : frame.texts) if (text.control && !text.sources.front().original.empty()) {
        const auto visible = text.clip.bounds({text.x, text.y, text.paragraph->width(), text.paragraph->height()});
        if (!nonempty(visible)) continue;
        left = std::min(left, visible.x); top = std::min(top, visible.y);
        right = std::max(right, visible.x + visible.width + style.padding_right);
        bottom = std::max(bottom, visible.y + visible.height + style.padding_bottom);
    }
    bottom = std::max(bottom, port.y + style.padding_top + natural_height + style.padding_bottom);
    FrameScroll scroll;
    scroll.port = port;
    auto& info = scroll.info;
    info.struct_size = sizeof(info);
    info.client_width = port.width; info.client_height = port.height;
    const bool origin_right = style.rtl != (style.flex_container && !style.flex.column && style.flex.reverse);
    const bool origin_bottom = style.flex_container && style.flex.column && style.flex.reverse;
    if (scrollable(style.overflow_x)) {
        info.flags |= XL_SCROLL_X;
        info.min_x = origin_right ? std::min(0.0f, left - port.x) : 0;
        info.max_x = origin_right ? 0 : std::max(0.0f, right - port.x - port.width);
    }
    if (scrollable(style.overflow_y)) {
        info.flags |= XL_SCROLL_Y;
        info.min_y = origin_bottom ? std::min(0.0f, top - port.y) : 0;
        info.max_y = origin_bottom ? 0 : std::max(0.0f, bottom - port.y - port.height);
    }
    if (user_scrollable(style.overflow_x)) info.flags |= XL_SCROLL_USER_X;
    if (user_scrollable(style.overflow_y)) info.flags |= XL_SCROLL_USER_Y;
    info.scroll_width = port.width + info.max_x - info.min_x;
    info.scroll_height = port.height + info.max_y - info.min_y;
    if (const auto found = offsets.find(id); found != offsets.end()) {
        info.x = std::clamp(found->second.first, info.min_x, info.max_x);
        info.y = std::clamp(found->second.second, info.min_y, info.max_y);
    }
    frame.scroll(id,info.x,info.y);
    Clip clip;
    if (style.overflow_x != Overflow::visible) { clip.left = port.x; clip.right = port.x + port.width; }
    if (style.overflow_y != Overflow::visible) { clip.top = port.y; clip.bottom = port.y + port.height; }
    if(style.overflow_x!=Overflow::visible&&style.overflow_y!=Overflow::visible) {
        const auto left_border=style.borders[3].width,right_border=style.borders[1].width,top_border=style.borders[0].width,bottom_border=style.borders[2].width;
        const xl_node_bounds border_box{port.x-left_border,port.y-top_border,port.width+left_border+right_border,port.height+top_border+bottom_border};
        const RoundedRect outer{border_box,resolve_radii(style.radii,border_box.width,border_box.height)};
        const auto inner=outer.inset(top_border,right_border,bottom_border,left_border);
        if(inner.curved())clip=clip.rounded(inner);
        scroll.clip=clip;
    }
    frame.clip(clip);
    if (info.flags & (XL_SCROLL_X | XL_SCROLL_Y)) {
        for (auto& [node, child] : frame.scrolls) if (!child.parent) child.parent = id;
        frame.scrolls.emplace(id, scroll);
        frame.scroll_order.push_back(id);
    }
}
struct Flow {
    Layout& layout;
    float x, y, width;
    const BoxStyle& block_style;
    InlineBuffer buffer;
    struct Inline {
        xl_node_id node;
        BoxStyle style;
    };
    std::vector<Inline> active;
    float intrinsic_width = 0;
    float containing_height = NAN;
    void touch();
    void flush();
    void walk(lxb_dom_node_t* node, const BoxStyle& inherited, size_t depth);
    void contents(lxb_dom_node_t* node, const BoxStyle& style, size_t depth);
    void marker(lxb_dom_node_t* owner, const BoxStyle& style);
    void legend(lxb_dom_node_t* owner, const BoxStyle& style);
};
struct Layout {
    Document::Impl& doc;
    DocumentFrame frame;
    Measure measure;
    IntrinsicCache& intrinsic_cache;
    std::optional<float> last_baseline = {};
    std::optional<float> first_baseline = {};
    struct Block { float width, height; };
    void record(lxb_dom_node_t* node,const BoxStyle& style) {
        if(measure!=Measure::none)return;
        auto& info=frame.paint_nodes[doc.identify(node)];
        info.positioned=style.position.kind!=cssposition::Kind::normal;
        info.inline_level=style.inline_flow;info.atomic=style.inline_atomic;
        info.out_of_flow=out_of_flow(style);
        info.flex=style.flex_container;info.grid=style.grid_container;
        info.z=style.position.z;info.isolate=style.position.isolate||style.position.kind==cssposition::Kind::fixed;info.order=style.order;
    }
    PositionedBox defer(lxb_dom_node_t* node,const BoxStyle& style,const BoxStyle& inherited,size_t depth,xl_node_bounds anchor) {
        PositionedBox box;
        box.node=doc.identify(node);box.fixed=style.position.kind==cssposition::Kind::fixed;
        box.containing=box.fixed?0:inherited.absolute_container;
        box.inherited=std::make_shared<BoxStyle>(inherited);box.depth=depth;box.anchor=anchor;box.rtl=inherited.rtl;
        return box;
    }
    void resolve_positioned(xl_node_id containing,xl_node_bounds port,bool rtl,bool fixed=false);
    void resolve_inlines();
    float control_dimension(lxb_dom_node_t* node, const BoxStyle& style, bool vertical) {
        if (!doc.text_engine) doc.text_engine = std::make_unique<TextEngine>();
        auto base = format(style); base.baseline_shift = 0; base.alignment_group = 0;
        base.whitespace = WhiteSpace::pre;
        const auto paragraph = doc.text_engine->shape({{"0", base}}, {}, 100000, false, 1, base, doc.text_session);
        const auto id = doc.identify(node);
        const auto raw = doc.attribute(id, vertical ? "rows" : node->local_name == LXB_TAG_TEXTAREA ? "cols" : "size", false);
        int count = vertical ? (node->local_name == LXB_TAG_TEXTAREA ? 2 : 1) : 20;
        int requested = 0; const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), requested);
        if (parsed.ec == std::errc{} && parsed.ptr == raw.data() + raw.size() && requested > 0 && requested <= 10000) count = requested;
        return count * (vertical ? paragraph->height() : paragraph->width());
    }
    void control_content(lxb_dom_node_t* node, float x, float y, float width, const BoxStyle& style) {
        const auto id = doc.identify(node);
        const auto value = doc.editor(id).value;
        const TextIndex index(value);
        std::string display = value;
        std::vector<uint32_t> offsets(index.length() + 1);
        const bool password = doc.edit_state(id).flags & XL_EDIT_PASSWORD;
        if (password) {
            display.clear(); uint32_t position = 0;
            for (uint32_t i = 0; i <= index.length(); ++i) {
                offsets[i] = position;
                if (i < index.length() && index.bytes[i + 1] != TextIndex::invalid) { display += "\xe2\x80\xa2"; ++position; }
            }
        } else for (uint32_t i = 0; i <= index.length(); ++i) offsets[i] = i;
        const auto rendered = display;
        auto base = format(style); base.baseline_shift = 0; base.alignment_group = 0;
        if (node->local_name != LXB_TAG_TEXTAREA) base.whitespace = WhiteSpace::pre;
        if (value.empty()) {
            display = doc.attribute(id, "placeholder", false);
            if (!display.empty()) base.color = 0xff757575;
        }
        if (display.empty() || display.back() == '\n') display += ' ';
        TextRun run{display,base};run.owner=id;
        auto paragraph = doc.text_engine->shape({std::move(run)}, {}, width, style.rtl, style.text_align, base, doc.text_session);
        TextSource source{id, std::move(offsets), rendered, value, index.bytes};
        source.fragments = paragraph->ranges(0, source.offsets.back());
        TextItem item{x, y, paragraph, frame.paint.size(), width, {std::move(source)}};
        item.control = true;
        item.owner=style.paint_owner;
        frame.text_nodes.emplace(id, std::pair{frame.texts.size(), size_t(0)});
        frame.texts.push_back(std::move(item));
        first_baseline = y + paragraph->first_baseline(); last_baseline = y + paragraph->last_baseline();
    }
    float container_content(lxb_dom_node_t* node, float x, float y, float width, float height, const BoxStyle& style, size_t depth);
    float intrinsic(lxb_dom_node_t* node, const BoxStyle& style, size_t depth, Measure mode) {
        if(style.replaced) {
            if(node->local_name==LXB_TAG_CANVAS)return float(doc.canvas_size(doc.identify(node)).first);
            const auto content=style.image?style.image:doc.image_content(node);
            if(!content)return 300;
            const auto& metrics=content->metrics();
            return metrics.width.value_or(metrics.height&&metrics.ratio?*metrics.height * *metrics.ratio:metrics.ratio?std::min(300.0f,150 * *metrics.ratio):300);
        }
        if (style.text_control) return control_dimension(node, style, false);
        const auto id = doc.identify(node);
        const auto [entry, added] = intrinsic_cache.try_emplace(id, NAN, NAN);
        auto cached = mode == Measure::minimum ? entry->second.first : entry->second.second;
        if (!std::isnan(cached)) return cached;
        Layout probe{doc, {}, mode, intrinsic_cache};
        probe.frame.width = frame.width;
        probe.frame.height = frame.height;
        auto inner = style;
        inner.baseline_shift = inner.vertical_shift = inner.alignment_offset = 0;
        inner.alignment_group = inner.alignment_parent = inner.vertical_keyword = 0;
        Flow flow{probe, 0, 0, 0, inner, {}, {}};
        if (inner.flex_container || inner.grid_container)
            cached = probe.container_content(node, 0, 0, 0, NAN, inner, depth);
        else {
            flow.contents(node, inner, depth);
            flow.flush(); cached = flow.intrinsic_width;
        }
        // Recursive insertions may rehash the cache.
        auto& result = intrinsic_cache.at(id);
        (mode == Measure::minimum ? result.first : result.second) = cached;
        return cached;
    }
    void width_values(lxb_dom_node_t* node, BoxStyle& style, float available, size_t depth) {
        const auto decoration = style.padding_left + style.padding_right + style.borders[1].width + style.borders[3].width;
        if(style.replaced) {
            const bool canvas=node->local_name==LXB_TAG_CANVAS;
            if(!canvas&&!style.image)style.image=doc.image_content(node);
            auto metrics=style.image?style.image->metrics():ImageMetrics{};
            if(canvas){const auto [w,h]=doc.canvas_size(doc.identify(node));metrics.width=float(w);metrics.height=float(h);if(w&&h)metrics.ratio=float(w)/float(h);}
            const auto vertical=style.padding_top+style.padding_bottom+style.borders[0].width+style.borders[2].width;
            const bool auto_width=!std::isfinite(style.width),auto_height=!std::isfinite(style.height);
            auto width=auto_width?metrics.width.value_or(NAN):std::max(0.0f,style.width-(style.border_box?decoration:0));
            auto height=auto_height?metrics.height.value_or(NAN):std::max(0.0f,style.height-(style.border_box?vertical:0));
            const auto ratio=metrics.ratio;
            if(ratio) {
                if(auto_width&&!auto_height)width=height * *ratio;
                else if(!auto_width&&auto_height)height=width / *ratio;
                else if(!std::isfinite(width)&&std::isfinite(height))width=height * *ratio;
                else if(std::isfinite(width)&&!std::isfinite(height))height=width / *ratio;
                else if(!std::isfinite(width)&&!std::isfinite(height)){width=std::min(300.0f,150 * *ratio);height=width / *ratio;}
            }
            if(!std::isfinite(width))width=300;if(!std::isfinite(height))height=150;
            const auto [min_width,max_width]=content_limits(style.min_width,style.max_width,decoration,style.border_box);
            const auto [min_height,max_height]=content_limits(style.min_height,style.max_height,vertical,style.border_box);
            const auto clamped=std::clamp(width,min_width,max_width);
            if(auto_height&&ratio)height=clamped / *ratio;
            width=clamped;
            const auto clamped_height=std::clamp(height,min_height,max_height);
            if(auto_width&&auto_height&&ratio)width=std::clamp(clamped_height * *ratio,min_width,max_width);
            style.width=width+(style.border_box?decoration:0);style.height=clamped_height+(style.border_box?vertical:0);style.height_definite=true;
            return;
        }
        if (style.text_control && std::isnan(style.width)) style.width = control_dimension(node, style, false) + (style.border_box ? decoration : 0);
        const auto value = [&](SizeProperty property, float current) {
            const auto& specified = style.sizes[property];
            if (measure != Measure::none && specified.kind == SizeKind::percentage)
                return property == size_max_width ? INFINITY : NAN;
            if (!specified.intrinsic() || (property == size_width && std::isfinite(current))) return current;
            float size;
            if (specified.kind == SizeKind::min_content) size = intrinsic(node, style, depth, Measure::minimum);
            else if (specified.kind == SizeKind::max_content) size = intrinsic(node, style, depth, Measure::maximum);
            else {
                const auto minimum = intrinsic(node, style, depth, Measure::minimum);
                const auto maximum = std::max(minimum, intrinsic(node, style, depth, Measure::maximum));
                size = std::min(std::max(minimum, available - decoration), maximum);
            }
            return size + (style.border_box ? decoration : 0);
        };
        style.width = value(size_width, style.width);
        style.min_width = value(size_min_width, style.min_width);
        style.max_width = value(size_max_width, style.max_width);
    }
    void merge(DocumentFrame&& child, float x, float y) {
        child.move(x, y);
        frame.merge_projection(child);
        const auto paint_offset = frame.paint.size();
        for (auto paint : child.paint) frame.paint.push_back(paint);
        for (const auto& [node, index] : child.outline_nodes) frame.outline_nodes.emplace(node, frame.outlines.size() + index);
        frame.outlines.insert(frame.outlines.end(), std::make_move_iterator(child.outlines.begin()), std::make_move_iterator(child.outlines.end()));
        for (auto& text : child.texts) {
            text.paint_index += paint_offset;
            for (size_t source = 0; source < text.sources.size(); ++source)
                frame.text_nodes.emplace(text.sources[source].node, std::pair{frame.texts.size(), source});
            frame.texts.push_back(std::move(text));
        }
        for (const auto& [id, bounds] : child.boxes) include(frame, id, bounds,false);
        frame.box_clips.merge(child.box_clips);
        frame.hit_shapes.merge(child.hit_shapes);
        frame.disclosure_legends.merge(child.disclosure_legends);
        frame.scrolls.merge(child.scrolls);
        frame.scroll_order.insert(frame.scroll_order.end(), child.scroll_order.begin(), child.scroll_order.end());
        frame.flow_bounds.insert(frame.flow_bounds.end(),child.flow_bounds.begin(),child.flow_bounds.end());
        frame.paint_nodes.merge(child.paint_nodes);frame.paint_parents.merge(child.paint_parents);
        frame.positioned.insert(frame.positioned.end(),std::make_move_iterator(child.positioned.begin()),std::make_move_iterator(child.positioned.end()));
        for(const auto& [id,box]:child.inline_containing) {
            const auto [entry,added]=frame.inline_containing.emplace(id,box);
            if(!added)entry->second.last=box.last;
        }
        frame.unsupported += child.unsupported;
    }
    Block block(lxb_dom_node_t* node, float x, float y, float available, const BoxStyle& input, size_t depth) {
        auto style = input;
        record(node,style);
        width_values(node, style, available, depth);
        const auto id = doc.identify(node);
        const auto margin_left = std::isnan(style.left) ? 0.0f : style.left;
        const auto margin_right = std::isnan(style.right) ? 0.0f : style.right;
        const auto horizontal = style.padding_left + style.padding_right + style.borders[1].width + style.borders[3].width;
        const auto vertical = style.padding_top + style.padding_bottom + style.borders[0].width + style.borders[2].width;
        if (style.text_control && std::isnan(style.height)) {
            style.height = control_dimension(node, style, true) + (style.border_box ? vertical : 0);
            style.height_definite = true;
        }
        const auto [min_width, max_width] = content_limits(style.min_width, style.max_width, horizontal, style.border_box);
        const auto constrained_width = [&](float value) { return std::max(min_width, std::min(max_width, value)); };
        if (measure != Measure::none) {
            const auto content = std::isnan(style.width) || style.width_percentage ? intrinsic(node, style, depth, measure)
                : std::max(0.0f, style.width - (style.border_box ? horizontal : 0));
            return {std::max(0.0f, constrained_width(content) + horizontal + margin_left + margin_right), 0};
        }
        const auto content_width = constrained_width(std::isfinite(style.width)
            ? std::max(0.0f, style.width - (style.border_box ? horizontal : 0))
            : std::max(0.0f, available - margin_left - margin_right - horizontal));
        const auto remaining = available - content_width - horizontal - margin_left - margin_right;
        if (!style.inline_atomic && std::isnan(style.left))
            x += std::max(0.0f, remaining) * (std::isnan(style.right) ? .5f : 1.0f);
        else x += margin_left;
        const float margin_top = std::isnan(style.top) ? 0 : style.top;
        const float margin_bottom = std::isnan(style.bottom) ? 0 : style.bottom;
        y += margin_top;
        auto [minimum_height, maximum_height] = content_limits(style.min_height, style.max_height, vertical, style.border_box);
        const auto height = std::isfinite(style.height)
            ? std::max(minimum_height, std::min(maximum_height, std::max(0.0f, style.height - (style.border_box ? vertical : 0)))) : NAN;
        const auto layout_content = [&](float allocated_height) {
            Layout nested{doc, {}, Measure::none, intrinsic_cache};
            nested.frame.width = frame.width; nested.frame.height = frame.height;
            auto content_style=style;content_style.inline_x=content_style.inline_y=0;
            if(style.position.kind!=cssposition::Kind::normal)content_style.absolute_container=id;
            Flow flow{nested, style.padding_left + style.borders[3].width, style.padding_top + style.borders[0].width,
                      content_width, content_style, {}, {}};
            flow.containing_height = style.height_definite ? height : NAN;
            float natural = 0;
            if(style.replaced) {
                natural=std::max(0.0f,style.height-(style.border_box?vertical:0));
            }else if (style.text_control) {
                nested.control_content(node, flow.x, flow.y, content_width, content_style);
                natural = control_dimension(node, style, true);
            } else if (style.flex_container || style.grid_container)
                natural = nested.container_content(node, flow.x, flow.y, content_width, allocated_height, content_style, depth);
            else {
                flow.contents(node, content_style, depth);
                flow.flush(); natural = std::max(0.0f, flow.y - style.padding_top - style.borders[0].width);
            }
            return std::pair{std::move(nested), natural};
        };
        auto [nested, natural_height] = layout_content(height);
        if (style.sizes[size_min_height].intrinsic()) minimum_height = natural_height;
        if (style.sizes[size_max_height].intrinsic()) maximum_height = std::max(minimum_height, natural_height);
        const auto content_height = std::max(minimum_height, std::min(maximum_height,
            std::isfinite(height) ? height : natural_height));
        if (!std::isfinite(height) && content_height != natural_height && (style.flex_container || style.grid_container)) {
            auto relaid = layout_content(content_height);
            nested.frame = std::move(relaid.first.frame);
            nested.first_baseline = relaid.first.first_baseline; nested.last_baseline = relaid.first.last_baseline;
            natural_height = relaid.second;
        }
        nested.resolve_inlines();
        if(style.position.kind!=cssposition::Kind::normal)
            nested.resolve_positioned(id,{style.borders[3].width,style.borders[0].width,
                content_width+style.padding_left+style.padding_right,content_height+style.padding_top+style.padding_bottom},style.rtl);
        if (node != intrinsic_cache.propagated)
            overflow_content(nested.frame, id, {style.borders[3].width, style.borders[0].width,
                content_width + style.padding_left + style.padding_right,
                content_height + style.padding_top + style.padding_bottom}, style, intrinsic_cache.scrolls, natural_height);
        const auto baseline_y=y;
        if(style.relative_x||style.relative_y)frame.flow_bounds.push_back({x+style.inline_x,y+style.inline_y,content_width+horizontal,content_height+vertical});
        x+=style.relative_x+style.inline_x;y+=style.relative_y+style.inline_y;
        frame.paint.push_back({{x, y, content_width + horizontal, content_height + vertical, style.background, 0}, style.borders});
        frame.paint.back().owner=id;
        if(style.replaced) {
            if(node->local_name==LXB_TAG_CANVAS)frame.paint.back().image=doc.canvas(id).drawing();
            if(style.image&&style.image->metrics().text&&!doc.text_engine)doc.text_engine=std::make_unique<TextEngine>();
            if(style.image)frame.paint.back().image=style.image->drawing(content_width,content_height,
                doc.text_engine?doc.text_engine->svg_fonts():sk_sp<SkFontMgr>{},doc.text_session,doc.text_engine?doc.text_engine->font_revision():0);
            if(!frame.paint.back().image)++frame.unsupported;
            frame.paint.back().image_rect={style.padding_left+style.borders[3].width,style.padding_top+style.borders[0].width,content_width,content_height};
            if(!nested.first_baseline)nested.first_baseline=content_height+vertical;
            if(!nested.last_baseline)nested.last_baseline=content_height+vertical;
        }
        const auto radii=resolve_radii(style.radii,content_width+horizontal,content_height+vertical);frame.paint.back().radii=radii;
        if(id!=intrinsic_cache.canvas_background) {
            auto& paint=frame.paint.back();paint.background=style.background_paint;paint.background_node=id;
            if(const auto scroll=nested.frame.scrolls.find(id);scroll!=nested.frame.scrolls.end())paint.background_local={scroll->second.info.x,scroll->second.info.y,scroll->second.info.scroll_width,scroll->second.info.scroll_height};
        }else frame.paint.back().rect.argb=0;
        if (!first_baseline && nested.first_baseline) first_baseline = baseline_y + *nested.first_baseline;
        if (nested.last_baseline) last_baseline = baseline_y + *nested.last_baseline;
        merge(std::move(nested.frame), x, y);
        include(frame, id, {x, y, content_width + horizontal, content_height + vertical});
        const RoundedRect hit{{x,y,content_width+horizontal,content_height+vertical},radii};
        if(hit.curved())frame.hit_shapes[id].push_back(hit);
        frame.outline(id, {x, y, content_width + horizontal, content_height + vertical}, style.outline,radii);
        return {content_width + horizontal + margin_left + margin_right, content_height + vertical + margin_top + margin_bottom};
    }
    void atomic(Flow& flow, lxb_dom_node_t* node, BoxStyle style, const BoxStyle& inherited, size_t depth) {
        width_values(node, style, flow.width, depth);
        const auto extra = style.padding_left + style.padding_right + style.borders[1].width + style.borders[3].width;
        const auto left = std::isnan(style.left) ? 0 : style.left;
        const auto right = std::isnan(style.right) ? 0 : style.right;
        auto object = std::make_shared<TextObject>();
        DocumentFrame child;
        if (measure != Measure::none) {
            const auto content = std::isnan(style.width) || style.width_percentage ? intrinsic(node, style, depth, measure)
                                 : std::max(0.0f, style.width - (style.border_box ? extra : 0));
            const auto [minimum, maximum] = content_limits(style.min_width, style.max_width, extra, style.border_box);
            object->width = std::max(minimum, std::min(maximum, content)) + extra + left + right;
        } else {
            if (std::isnan(style.width)) {
                const auto minimum = intrinsic(node, style, depth, Measure::minimum);
                const auto maximum = std::max(minimum, intrinsic(node, style, depth, Measure::maximum));
                const auto content = std::min(std::max(minimum, flow.width - extra - left - right), maximum);
                style.width = content + (style.border_box ? extra : 0);
            }
            // The outer alignment does not propagate into the independent
            // formatting context inside this atomic box.
            auto inner = style;
            inner.inline_x = inner.inline_y = 0;
            inner.baseline_shift = inner.vertical_shift = inner.alignment_offset = 0;
            inner.alignment_group = inner.alignment_parent = inner.vertical_keyword = 0;
            Layout nested{doc, {}, Measure::none, intrinsic_cache};
            nested.frame.width = frame.width;
            nested.frame.height = frame.height;
            const auto size = nested.block(node, 0, 0, flow.width, inner, depth);
            object->width = size.width;
            object->height = size.height;
            object->baseline = ((style.flex_container || style.grid_container) ? nested.first_baseline : nested.last_baseline).value_or(size.height);
            if (!style.flex_container && !style.grid_container && scrollable(style.overflow_y))
                object->baseline = size.height;
            nested.frame.capture_projection();
            build_display(doc,nested.frame,doc.identify(node));
            object->scene = std::make_shared<PaintScene>(nested.frame);
            object->flattened=true;
            child = std::move(nested.frame);
            frame.unsupported += child.unsupported;
            if (style.vertical_keyword == LXB_CSS_VALUE_MIDDLE || style.vertical_keyword == LXB_CSS_VALUE_TEXT_TOP ||
                style.vertical_keyword == LXB_CSS_VALUE_TEXT_BOTTOM) {
                const auto parent = doc.text_engine->font_metrics(format(inherited));
                style.vertical_shift = style.vertical_keyword == LXB_CSS_VALUE_MIDDLE
                    ? object->baseline - (object->height + parent.x_height) * .5f
                    : style.vertical_keyword == LXB_CSS_VALUE_TEXT_TOP ? object->baseline - parent.ascent
                    : object->baseline + parent.descent - object->height;
                style.baseline_shift = inherited.baseline_shift + style.vertical_shift;
            }
        }
        const auto id = doc.identify(node);
        const auto offset = flow.buffer.length;
        auto external = format(style);
        // The atomic box's white-space controls its contents; wrapping around
        // the box belongs to the containing inline formatting context.
        external.whitespace = inherited.whitespace;
        TextInlineBox box;
        box.start = offset;
        box.end = offset + 1;
        box.format = external;
        box.atomic = true;
        box.line_alignment = style.vertical_keyword == LXB_CSS_VALUE_TOP ? 1
                             : style.vertical_keyword == LXB_CSS_VALUE_BOTTOM ? 2 : 0;
        box.alignment_parent = style.alignment_parent;
        box.alignment_offset = style.alignment_offset;
        flow.buffer.spans.push_back({id, std::move(box), {}});
        flow.buffer.object(external, std::move(object),id);
        if (measure == Measure::none) flow.buffer.atomics.push_back({offset, std::move(child)});
    }
};
void Layout::resolve_positioned(xl_node_id containing,xl_node_bounds port,bool rtl,bool fixed) {
    for(size_t index=0;index<frame.positioned.size();) {
        if(frame.positioned[index].containing!=containing||frame.positioned[index].fixed!=fixed){++index;continue;}
        auto pending=std::move(frame.positioned[index]);frame.positioned.erase(frame.positioned.begin()+index);
        const auto containing_port=port;
        if(pending.grid_area) {
            const auto& edges=pending.grid_edges;
            const float left=std::isfinite(edges[0])?edges[0]:port.x,right=std::isfinite(edges[2])?edges[2]:port.x+port.width;
            const float top=std::isfinite(edges[1])?edges[1]:port.y,bottom=std::isfinite(edges[3])?edges[3]:port.y+port.height;
            port={left,top,std::max(0.0f,right-left),std::max(0.0f,bottom-top)};
        }
        auto* node=doc.resolve(pending.node);
        auto style=box_style(doc,node,port.width,frame.width,frame.height,*pending.inherited,port.height);
        style.inline_flow=style.inline_atomic=false;
        style.inline_x=style.inline_y=style.relative_x=style.relative_y=0;
        style.baseline_shift=style.vertical_shift=style.alignment_offset=0;
        style.alignment_group=style.alignment_parent=style.vertical_keyword=0;
        width_values(node,style,port.width,pending.depth);
        const float horizontal=style.padding_left+style.padding_right+style.borders[1].width+style.borders[3].width;
        const float vertical=style.padding_top+style.padding_bottom+style.borders[0].width+style.borders[2].width;
        const auto inset=[&](size_t side,float basis)->std::optional<float> {
            if(!style.position.inset[side])return {};
            const auto& value=*style.position.inset[side];return float(value.px+value.percent*basis);
        };
        const auto left=inset(3,port.width),right=inset(1,port.width),top=inset(0,port.height),bottom=inset(2,port.height);
        const bool auto_width=std::isnan(style.width),auto_height=std::isnan(style.height);
        const float ml=std::isnan(style.left)?0:style.left,mr=std::isnan(style.right)?0:style.right;
        const float mt=std::isnan(style.top)?0:style.top,mb=std::isnan(style.bottom)?0:style.bottom;
        if(auto_width) {
            float available=port.width-left.value_or(0)-right.value_or(0)-ml-mr-horizontal;
            if(!left&&!right)available=rtl?pending.anchor.x+pending.anchor.width-port.x-mr-horizontal:
                port.x+port.width-pending.anchor.x-ml-horizontal;
            const auto content=left&&right?std::max(0.0f,available):
                std::min(std::max(intrinsic(node,style,pending.depth,Measure::minimum),available),
                         std::max(intrinsic(node,style,pending.depth,Measure::minimum),intrinsic(node,style,pending.depth,Measure::maximum)));
            style.width=content+(style.border_box?horizontal:0);
        }
        if(auto_height&&top&&bottom) {
            style.height=std::max(0.0f,port.height-*top-*bottom-mt-mb-vertical)+(style.border_box?vertical:0);
            style.height_definite=true;
        }
        const float margins[]{style.top,style.right,style.bottom,style.left};
        style.top=style.right=style.bottom=style.left=0;
        Layout nested{doc,{},Measure::none,intrinsic_cache};nested.frame.width=frame.width;nested.frame.height=frame.height;
        const auto size=nested.block(node,0,0,port.width,style,pending.depth);
        const auto coordinate=[&](bool horizontal_axis) {
            const auto start=horizontal_axis?left:top,end=horizontal_axis?right:bottom;
            const float origin=horizontal_axis?port.x:port.y,extent=horizontal_axis?port.width:port.height;
            const float length=horizontal_axis?size.width:size.height;
            const float before=margins[horizontal_axis?3:0],after=margins[horizontal_axis?1:2];
            float first=std::isnan(before)?0:before,last=std::isnan(after)?0:after;
            const bool specified=horizontal_axis?!auto_width:!auto_height;
            if(start&&end&&specified) {
                const auto free=extent-*start-*end-length-first-last;
                if(std::isnan(before)&&std::isnan(after)) {
                    if(horizontal_axis&&free<0){if(rtl)first=free;else last=free;}
                    else first=last=free*.5f;
                }else if(std::isnan(before))first=free;
                else if(std::isnan(after))last=free;
            }
            if(start&&end&&horizontal_axis&&rtl)return origin+extent-*end-last-length;
            if(start)return origin+*start+first;
            if(end)return origin+extent-*end-last-length;
            const float anchor=horizontal_axis?pending.anchor.x:pending.anchor.y;
            const float area=horizontal_axis?pending.anchor.width:pending.anchor.height;
            if(pending.area)return anchor+first+(area-length-first-last)*(horizontal_axis?pending.align_x:pending.align_y);
            return horizontal_axis&&pending.rtl?anchor+area-last-length:anchor+first;
        };
        const auto dx=coordinate(true),dy=coordinate(false);
        if(fixed)nested.frame.clip({-dx,-dy,float(frame.width)-dx,float(frame.height)-dy});
        merge(std::move(nested.frame),dx,dy);
        port=containing_port;
    }
}
void Layout::resolve_inlines() {
    const auto boxes=frame.inline_containing;
    for(const auto& [id,box]:boxes) {
        const auto left=box.rtl?box.last.x:box.first.x;
        const auto right=box.rtl?box.first.x+box.first.width:box.last.x+box.last.width;
        resolve_positioned(id,{left,box.first.y,std::max(0.0f,right-left),
            std::max(0.0f,box.last.y+box.last.height-box.first.y)},box.rtl);
    }
}
float Layout::container_content(lxb_dom_node_t* node, float x, float y, float width, float height,
                           const BoxStyle& style, size_t depth) {
    if (depth > 256) throw DocumentFailure{XL_UNSUPPORTED};
    struct Child {
        lxb_dom_node_t* node = nullptr;
        bool legend = false;
        BoxStyle style;
        std::vector<lxb_dom_node_t*> text;
        DocumentFrame frame;
        float measured_width = NAN, measured_height = NAN, width = 0, height = 0, baseline = 0;
        bool measured_definite = false;
    };
    std::vector<Child> children;
    std::vector<PositionedBox> deferred;
    const auto align=[](FlexAlign value)->float {
        if(value==FlexAlign::center||value==FlexAlign::around||value==FlexAlign::evenly)return .5f;
        return value==FlexAlign::end||value==FlexAlign::flow_end?1.0f:0.0f;
    };
    const auto place_deferred=[&](float final_height,const GridResult* grid=nullptr) {
        for(auto& box:deferred) {
            const auto computed=box_style(doc,doc.resolve(box.node),width,frame.width,frame.height,style,final_height);
            box.anchor={x,y,width,final_height};box.area=true;
            if(style.flex_container) {
                const auto cross=computed.align_self==FlexAlign::automatic?style.flex.items:computed.align_self;
                float main=align(style.flex.justify),other=align(cross);
                const bool reverse=style.flex.reverse!=(!style.flex.column&&style.rtl);
                const bool cross_reverse=(style.flex.wrap==2)!=(style.flex.column&&style.rtl);
                if(style.flex.justify==FlexAlign::flow_start||style.flex.justify==FlexAlign::flow_end) {
                    if(style.flex.reverse)main=1-main;
                }
                if(cross==FlexAlign::flow_start||cross==FlexAlign::flow_end) {
                    if(style.flex.wrap==2)other=1-other;
                }
                if(reverse)main=1-main;if(cross_reverse)other=1-other;
                box.align_x=style.flex.column?other:main;box.align_y=style.flex.column?main:other;
            } else if(grid&&box.containing==doc.identify(node)) {
                const auto region=grid_positioned_area(*grid,computed.grid_columns,computed.grid_rows,width,final_height,style.rtl);
                box.grid_edges={x+region[0],y+region[1],x+region[2],y+region[3]};
                const float left=std::isfinite(region[0])?region[0]:0,right=std::isfinite(region[2])?region[2]:width;
                const float top=std::isfinite(region[1])?region[1]:0,bottom=std::isfinite(region[3])?region[3]:final_height;
                box.anchor={x+left,y+top,std::max(0.0f,right-left),std::max(0.0f,bottom-top)};
                // Grid lines replace padding edges only for explicitly resolved lines.
                box.area=true;
                box.align_x=align(computed.justify_self==FlexAlign::automatic?style.grid.justify_items:computed.justify_self);
                if(style.rtl)box.align_x=1-box.align_x;
                box.align_y=align(computed.align_self==FlexAlign::automatic?style.flex.items:computed.align_self);
                box.grid_area=true;
            }
            frame.positioned.push_back(std::move(box));
        }
    };
    auto* summary = doc.details_summary(node);
    if (node->local_name == LXB_TAG_DETAILS && !summary) {
        Child legend; legend.style = style; legend.legend = true; children.push_back(std::move(legend));
    }
    const auto content = doc.render_children(node);
    for (auto* child : content) {
        if (doc.details_hidden(child)) continue;
        if (child->type == LXB_DOM_NODE_TYPE_TEXT) {
            if (children.empty() || children.back().node || children.back().legend) {
                Child anonymous; anonymous.style = style;
                anonymous.style.flex_container = anonymous.style.grid_container = anonymous.style.inline_atomic = anonymous.style.inline_flow = false;
                anonymous.style.grid_columns = anonymous.style.grid_rows = {};
                anonymous.style.justify_self = FlexAlign::automatic;
                anonymous.style.width = anonymous.style.height = NAN;
                anonymous.style.sizes = {}; anonymous.style.height_definite = false;
                anonymous.style.top = anonymous.style.right = anonymous.style.bottom = anonymous.style.left = 0;
                anonymous.style.padding_top = anonymous.style.padding_right = anonymous.style.padding_bottom = anonymous.style.padding_left = 0;
                anonymous.style.borders = {}; anonymous.style.background = 0;anonymous.style.background_paint.reset();anonymous.style.background_style.reset();
                anonymous.style.min_width = anonymous.style.min_height = anonymous.style.basis = NAN;
                anonymous.style.max_width = anonymous.style.max_height = INFINITY;
                anonymous.style.align_self = FlexAlign::automatic; anonymous.style.order = 0;
                children.push_back(std::move(anonymous));
            }
            children.back().text.push_back(child);
        } else if (!skipped(child)) {
            const auto percentage_height = style.height_definite ? height : NAN;
            auto computed = box_style(doc, child, width, frame.width, frame.height, style, percentage_height);
            if (computed.hidden) continue;
            if(out_of_flow(computed)) {
                if(measure==Measure::none)deferred.push_back(defer(child,computed,style,depth+1,{x,y,width,0}));
                continue;
            }
            width_values(child, computed, width, depth + 1);
            frame.unsupported += computed.unsupported;
            if (std::isfinite(computed.height_ratio)) computed.height = std::isfinite(percentage_height) ? percentage_height * computed.height_ratio : NAN;
            computed.inline_flow = computed.inline_atomic = false; // Flex item blockification.
            computed.baseline_shift = computed.vertical_shift = computed.alignment_offset = 0;
            computed.alignment_group = computed.alignment_parent = computed.vertical_keyword = 0;
            Child item; item.node = child; item.style = std::move(computed);
            children.push_back(std::move(item));
        }
    }
    const auto text_intrinsic = [&](const Child& child, Measure mode) {
        Layout probe{doc, {}, mode, intrinsic_cache}; probe.frame.width = frame.width; probe.frame.height = frame.height;
        Flow flow{probe, 0, 0, 0, child.style, {}, {}};
        if (child.legend) flow.legend(node, child.style);
        for (auto* text : child.text) flow.walk(text, child.style, depth + 1);
        flow.flush(); return flow.intrinsic_width;
    };
    children.erase(std::remove_if(children.begin(), children.end(), [&](const Child& child) {
        if (child.node || child.legend) return false;
        // Anonymous all-collapsible whitespace does not create a flex item.
        for (auto* text : child.text) {
            const auto& data = lxb_dom_interface_text(text)->char_data.data;
            if (data.length && !collapses_spaces(child.style.whitespace)) return false;
            for (size_t i = 0; i < data.length; ++i)
                if (data.data[i] != ' ' && data.data[i] != '\t' && data.data[i] != '\n' && data.data[i] != '\r' && data.data[i] != '\f') return false;
        }
        return true;
    }), children.end());
    std::stable_sort(children.begin(), children.end(), [](const Child& a, const Child& b) {
        return (a.node ? a.style.order : 0) < (b.node ? b.style.order : 0);
    });
    const auto extra = [](const BoxStyle& box, bool vertical) {
        return vertical ? box.padding_top + box.padding_bottom + box.borders[0].width + box.borders[2].width
                        : box.padding_left + box.padding_right + box.borders[1].width + box.borders[3].width;
    };
    const auto intrinsic_child = [&](const Child& child, Measure mode) {
        return child.node ? intrinsic(child.node, child.style, depth + 1, mode) : text_intrinsic(child, mode);
    };
    const auto grid_item = [&](const Child& child) {
        GridItem item;
        item.columns = child.style.grid_columns; item.rows = child.style.grid_rows;
        const auto& computed = child.style;
        const auto decoration = child.node ? extra(computed, false) : 0;
        const auto margins = (std::isnan(computed.left) ? 0 : computed.left) + (std::isnan(computed.right) ? 0 : computed.right);
        const auto minimum = std::isfinite(computed.min_width) ? std::max(decoration, computed.min_width + (computed.border_box ? 0 : decoration)) : 0;
        const auto maximum = std::max(minimum, computed.max_width + (computed.border_box ? 0 : decoration));
        item.min_content = std::max(minimum, std::min(maximum, intrinsic_child(child, Measure::minimum) + decoration));
        item.max_content = std::max(minimum, std::min(maximum, intrinsic_child(child, Measure::maximum) + decoration));
        if (std::isfinite(computed.width) && !computed.width_percentage)
            item.min_content = item.max_content = std::max(minimum, std::min(maximum, computed.width + (computed.border_box ? 0 : decoration)));
        item.minimum = (std::isfinite(computed.min_width) ? minimum
            : scrollable(computed.overflow_x) ? decoration : item.min_content) + margins;
        item.automatic_minimum = !std::isfinite(computed.min_width);
        item.min_content += margins; item.max_content += margins;
        item.baseline = (computed.align_self == FlexAlign::automatic ? style.flex.items : computed.align_self) == FlexAlign::baseline;
        return item;
    };
    if (measure != Measure::none) {
        if (style.grid_container) {
            std::vector<GridItem> items; for (const auto& child : children) items.push_back(grid_item(child));
            try { return grid_layout(style.grid, items, NAN, NAN, style.flex.column_gap, style.flex.row_gap, NAN,
                                     FlexAlign::start, FlexAlign::start, measure == Measure::minimum ? 1 : 2).width; }
            catch (const std::length_error&) { throw DocumentFailure{XL_UNSUPPORTED}; }
        }
        float result = 0;
        const bool sum = !style.flex.column && (measure == Measure::maximum || !style.flex.wrap);
        for (const auto& child : children) {
            auto contribution = child.node && std::isfinite(child.style.width) && !child.style.width_percentage
                ? child.style.width - (child.style.border_box ? extra(child.style, false) : 0)
                : intrinsic_child(child, measure);
            contribution += child.node ? extra(child.style, false) +
                (std::isnan(child.style.left) ? 0 : child.style.left) + (std::isnan(child.style.right) ? 0 : child.style.right) : 0;
            result = sum ? result + contribution : std::max(result, contribution);
        }
        if (sum && !children.empty()) result += style.flex.column_gap * float(children.size() - 1);
        return result;
    }
    const auto render_child = [&](Child& child, float assigned_width, float assigned_height, bool assigned_definite = true) {
        if ((assigned_width == child.measured_width || (std::isnan(assigned_width) && std::isnan(child.measured_width))) &&
            (assigned_height == child.measured_height || (std::isnan(assigned_height) && std::isnan(child.measured_height))) &&
            child.frame.revision && child.measured_definite == assigned_definite) return;
        Layout nested{doc, {}, Measure::none, intrinsic_cache};
        nested.frame.width = frame.width; nested.frame.height = frame.height; nested.frame.revision = 1;
        auto computed = child.style;
        computed.top = computed.right = computed.bottom = computed.left = 0;
        computed.inline_atomic = true; // Resolved auto margins are outside this layout.
        const auto border_width = std::isfinite(assigned_width) ? assigned_width
            : child.node && std::isfinite(computed.width) ? computed.width + (computed.border_box ? 0 : extra(computed, false))
            : std::min(std::max(intrinsic_child(child, Measure::minimum), width), intrinsic_child(child, Measure::maximum)) + (child.node ? extra(computed, false) : 0);
        computed.width = std::max(0.0f, border_width - (computed.border_box ? 0 : extra(computed, false)));
        if (std::isfinite(assigned_height)) {
            computed.height = std::max(0.0f, assigned_height - (computed.border_box ? 0 : extra(computed, true)));
            computed.height_definite = assigned_definite;
        }
        if (child.node) {
            const auto size = nested.block(child.node, 0, 0, border_width, computed, depth + 1);
            child.width = size.width; child.height = size.height;
        } else {
            if(!child.text.empty()) {
                const auto owner=doc.identify(child.text.front());computed.paint_owner=owner;
                nested.frame.paint_nodes[owner].atomic=true;
                for(size_t i=1;i<child.text.size();++i)nested.frame.paint_parents[doc.identify(child.text[i])]=owner;
            }
            Flow flow{nested, 0, 0, border_width, computed, {}, {}};
            if (child.legend) flow.legend(node, computed);
            for (auto* text : child.text) flow.walk(text, computed, depth + 1);
            flow.flush(); child.width = border_width; child.height = flow.y;
        }
        child.baseline = nested.first_baseline.value_or(child.height);
        child.frame = std::move(nested.frame);
        child.measured_width = assigned_width; child.measured_height = assigned_height;
        child.measured_definite = assigned_definite;
    };
    if (style.grid_container) {
        const auto align_item = [&](const Child& child, bool horizontal) {
            const auto own = horizontal ? child.style.justify_self : child.style.align_self;
            const auto inherited = horizontal ? style.grid.justify_items : style.flex.items;
            return own == FlexAlign::automatic ? inherited : own;
        };
        const auto layout_item = [&](Child& child, float area_width, float area_height) {
            if (child.node) {
                child.style = box_style(doc, child.node, area_width, frame.width, frame.height, style, area_height);
                width_values(child.node, child.style, area_width, depth + 1);
                child.style.inline_flow = child.style.inline_atomic = false;
                child.style.baseline_shift = child.style.vertical_shift = child.style.alignment_offset = 0;
                child.style.alignment_group = child.style.alignment_parent = child.style.vertical_keyword = 0;
                if (std::isfinite(child.style.height_ratio)) child.style.height = std::isfinite(area_height) ? area_height * child.style.height_ratio : NAN;
                child.frame.revision = 0; // Area-relative percentages may have changed.
            }
            const auto& computed = child.style;
            const auto margin_x = (std::isnan(computed.left) ? 0 : computed.left) + (std::isnan(computed.right) ? 0 : computed.right);
            const auto margin_y = (std::isnan(computed.top) ? 0 : computed.top) + (std::isnan(computed.bottom) ? 0 : computed.bottom);
            const auto dimension = [&](bool horizontal, float area, float margins) {
                const auto decoration = child.node ? extra(computed, !horizontal) : 0;
                const auto specified = horizontal ? computed.width : computed.height;
                const auto min = horizontal ? computed.min_width : computed.min_height;
                const auto max = horizontal ? computed.max_width : computed.max_height;
                const auto minimum = std::isfinite(min) ? std::max(decoration, min + (computed.border_box ? 0 : decoration)) : decoration;
                const auto maximum = std::max(minimum, max + (computed.border_box ? 0 : decoration));
                float size = NAN;
                if (std::isfinite(specified)) size = specified + (computed.border_box ? 0 : decoration);
                else if (align_item(child, horizontal) == FlexAlign::stretch && std::isfinite(area) &&
                         !std::isnan(horizontal ? computed.left : computed.top) && !std::isnan(horizontal ? computed.right : computed.bottom))
                    size = std::max(0.0f, area - margins);
                else if (horizontal) size = std::min(std::max(intrinsic_child(child, Measure::minimum) + decoration, area - margins),
                                                     intrinsic_child(child, Measure::maximum) + decoration);
                return std::isfinite(size) ? std::max(minimum, std::min(maximum, size)) : size;
            };
            const auto assigned_width = dimension(true, area_width, margin_x), assigned_height = dimension(false, area_height, margin_y);
            render_child(child, assigned_width, assigned_height);
            if (!std::isfinite(assigned_height)) {
                const auto decoration = child.node ? extra(computed, true) : 0;
                const auto minimum = std::isfinite(computed.min_height) ? std::max(decoration, computed.min_height + (computed.border_box ? 0 : decoration)) : decoration;
                const auto maximum = std::max(minimum, computed.max_height + (computed.border_box ? 0 : decoration));
                const auto adjusted = std::max(minimum, std::min(maximum, child.height));
                if (adjusted != child.height) render_child(child, assigned_width, adjusted);
            }
            return GridMeasure{child.height + margin_y, child.baseline + (std::isnan(computed.top) ? 0 : computed.top)};
        };
        std::vector<GridItem> items;
        for (const auto& child : children) {
            auto item = grid_item(child);
            item.measure = [&, index = items.size()](float area) { return layout_item(children[index], area, NAN); };
            items.push_back(std::move(item));
        }
        const auto column_gap = std::isfinite(style.column_gap_ratio) ? width * style.column_gap_ratio : style.flex.column_gap;
        const auto row_gap = std::isfinite(style.row_gap_ratio) && std::isfinite(height) ? height * style.row_gap_ratio : style.flex.row_gap;
        GridResult result;
        try { result = grid_layout(style.grid, items, width, height, column_gap, row_gap, style.row_gap_ratio,
                                   style.flex.justify, style.flex.content); }
        catch (const std::length_error&) { throw DocumentFailure{XL_UNSUPPORTED}; }
        std::vector<float> baselines(grid_track_limit, 0);
        for (size_t i = 0; i < items.size(); ++i) {
            const auto measured = layout_item(children[i], items[i].width, items[i].height);
            if (items[i].baseline) baselines[items[i].row] = std::max(baselines[items[i].row], measured.baseline);
        }
        for (size_t i = 0; i < items.size(); ++i) {
            auto& child = children[i]; const auto& item = items[i];
            const auto offset = [&](bool horizontal) {
                const auto before = horizontal ? (style.rtl ? child.style.right : child.style.left) : child.style.top;
                const auto after = horizontal ? (style.rtl ? child.style.left : child.style.right) : child.style.bottom;
                const auto start = std::isnan(before) ? 0 : before, end = std::isnan(after) ? 0 : after;
                const auto free = (horizontal ? item.width - child.width : item.height - child.height) - start - end;
                if (std::isnan(before) || std::isnan(after)) return start + (free > 0 && std::isnan(before) ? free / float(int(std::isnan(before)) + int(std::isnan(after))) : 0);
                const auto align = align_item(child, horizontal);
                if (!horizontal && align == FlexAlign::baseline) return baselines[item.row] - child.baseline;
                if (align == FlexAlign::center) return start + free * .5f;
                if (align == FlexAlign::end || align == FlexAlign::flow_end) return start + free;
                return start;
            };
            const auto dx = x + (style.rtl ? width - item.x - offset(true) - child.width : item.x + offset(true));
            const auto dy = y + item.y + offset(false);
            merge(std::move(child.frame), dx, dy);
            if (!first_baseline) first_baseline = dy + child.baseline;
            last_baseline = dy + child.baseline;
        }
        place_deferred(result.height,&result);
        return result.height;
    }
    auto flex_style = style.flex;
    if (std::isfinite(style.row_gap_ratio)) flex_style.row_gap = std::isfinite(height) ? height * style.row_gap_ratio : 0;
    if (std::isfinite(style.column_gap_ratio)) flex_style.column_gap = width * style.column_gap_ratio;
    const bool column = flex_style.column;
    const bool main_reverse = flex_style.reverse != (!column && style.rtl);
    const bool cross_reverse = (flex_style.wrap == 2) != (column && style.rtl);
    const auto logical_alignment = [](FlexAlign align, bool reverse) {
        if (align == FlexAlign::flow_start) return reverse ? FlexAlign::end : FlexAlign::start;
        if (align == FlexAlign::flow_end) return reverse ? FlexAlign::start : FlexAlign::end;
        return align;
    };
    flex_style.justify = logical_alignment(flex_style.justify, flex_style.reverse);
    flex_style.items = logical_alignment(flex_style.items, flex_style.wrap == 2);
    flex_style.content = logical_alignment(flex_style.content, flex_style.wrap == 2);
    std::vector<FlexItem> items;
    for (auto& child : children) {
        auto& computed = child.style;
        FlexItem item;
        const auto main_extra = child.node ? extra(computed, column) : 0;
        const auto cross_extra = child.node ? extra(computed, !column) : 0;
        const auto specified = column ? computed.height : computed.width;
        const auto cross_specified = column ? computed.width : computed.height;
        const auto limit = [&](float value, float fallback, float decoration) {
            return std::isnan(value) ? fallback : std::max(decoration, value + (computed.border_box ? 0 : decoration));
        };
        if (column) {
            const auto align = computed.align_self == FlexAlign::automatic ? flex_style.items : computed.align_self;
            const auto stretch_width = !child.node || (align == FlexAlign::stretch && !std::isfinite(computed.width)) ? width -
                (std::isnan(computed.left) ? 0 : computed.left) - (std::isnan(computed.right) ? 0 : computed.right) : NAN;
            render_child(child, stretch_width, NAN);
        }
        const auto natural = column ? child.height : intrinsic_child(child, Measure::maximum) + main_extra;
        const auto minimum_content = column ? child.height : intrinsic_child(child, Measure::minimum) + main_extra;
        const auto specified_border = std::isfinite(specified) ? std::max(main_extra, specified + (computed.border_box ? 0 : main_extra)) : INFINITY;
        const auto main_available = column ? height : width;
        const bool use_basis = std::isfinite(computed.basis) && (!computed.basis_percentage ||
            (std::isfinite(main_available) && (!column || style.height_definite)));
        item.base = use_basis ? std::max(main_extra, computed.basis * (computed.basis_percentage ? main_available : 1) +
                                            (computed.border_box ? 0 : main_extra))
                    : !computed.basis_content && std::isfinite(specified_border) ? specified_border : natural;
        const auto maximum = limit(column ? computed.max_height : computed.max_width, INFINITY, main_extra);
        item.minimum = limit(column ? computed.min_height : computed.min_width,
                             scrollable(column ? computed.overflow_y : computed.overflow_x) ? main_extra
                                 : std::min({minimum_content, specified_border, maximum}), main_extra);
        item.maximum = std::max(item.minimum, maximum);
        item.cross_min = limit(column ? computed.min_width : computed.min_height, cross_extra, cross_extra);
        item.cross_max = std::max(item.cross_min, limit(column ? computed.max_width : computed.max_height, INFINITY, cross_extra));
        item.grow = child.node ? computed.grow : 0; item.shrink = child.node ? computed.shrink : 1;
        const float margins[]{computed.top, computed.right, computed.bottom, computed.left};
        const auto before = column ? 0 : 3, after = column ? 2 : 1;
        const auto cross_before = column ? 3 : 0, cross_after = column ? 1 : 2;
        item.before = margins[main_reverse ? after : before]; item.after = margins[main_reverse ? before : after];
        item.cross_before = margins[cross_reverse ? cross_after : cross_before]; item.cross_after = margins[cross_reverse ? cross_before : cross_after];
        item.cross_auto = !std::isfinite(cross_specified) && computed.sizes[column ? size_width : size_height].kind == SizeKind::automatic;
        item.align = child.node ? computed.align_self : FlexAlign::automatic;
        item.align = logical_alignment(item.align, flex_style.wrap == 2);
        item.measure = [&, index = items.size(), definite_main = style.height_definite || use_basis || computed.height_definite](float main, float cross) {
            auto& measured = children[index];
            render_child(measured, column ? cross : main, column ? main : cross, column ? definite_main : std::isfinite(cross));
            return FlexMeasure{column ? measured.width : measured.height, column ? measured.width : measured.baseline};
        };
        items.push_back(std::move(item));
    }
    const auto result = flex_layout(flex_style, items, column ? height : width, column ? width : height);
    for (size_t i = 0; i < items.size(); ++i) {
        auto& child = children[i]; const auto& item = items[i];
        const auto main = main_reverse ? result.main - item.main_position - item.main : item.main_position;
        const auto cross = cross_reverse ? result.cross - item.cross_position - item.cross : item.cross_position;
        const auto dx = x + (column ? cross : main), dy = y + (column ? main : cross);
        merge(std::move(child.frame), dx, dy);
        if (!first_baseline) first_baseline = dy + child.baseline;
        last_baseline = dy + child.baseline;
    }
    const auto final_height=column ? result.main : result.cross;
    place_deferred(final_height);
    return final_height;
}
void Flow::touch() {
    for (const auto& ancestor : active) {
        const auto [entry, added] = buffer.span_ids.emplace(ancestor.node, buffer.spans.size());
        if (added) {
            TextInlineBox box;
            box.owner=ancestor.node;
            box.start = box.end = buffer.length;
            box.format = format(ancestor.style);
            box.padding_top = ancestor.style.padding_top;
            box.padding_bottom = ancestor.style.padding_bottom;
            box.background = ancestor.style.background;box.background_paint=ancestor.style.background_paint;
            box.borders = ancestor.style.borders;
            box.radii = ancestor.style.radii;
            box.rtl = ancestor.style.rtl;
            box.line_alignment = ancestor.style.vertical_keyword == LXB_CSS_VALUE_TOP      ? 1
                                 : ancestor.style.vertical_keyword == LXB_CSS_VALUE_BOTTOM ? 2
                                                                                          : 0;
            box.alignment_parent = ancestor.style.alignment_parent;
            box.alignment_offset = ancestor.style.alignment_offset;
            box.margin_start = std::isnan(box.rtl ? ancestor.style.right : ancestor.style.left) ? 0
                               : box.rtl                                                        ? ancestor.style.right
                                                                                                : ancestor.style.left;
            box.margin_end = std::isnan(box.rtl ? ancestor.style.left : ancestor.style.right) ? 0
                             : box.rtl                                                        ? ancestor.style.left
                                                                                              : ancestor.style.right;
            buffer.spans.push_back({ancestor.node, std::move(box), ancestor.style.outline,ancestor.style.position.kind!=cssposition::Kind::normal});
        } else
            buffer.spans[entry->second].value.end = buffer.length;
    }
}
void Flow::flush() {
    const bool forced_line = buffer.finish();
    if (buffer.text.empty() && !forced_line) {
        if(layout.measure==Measure::none)for(const auto& span:buffer.spans)if(span.positioned) {
            const xl_node_bounds empty{x+span.value.format.visual_x,y+span.value.format.visual_y,0,0};
            layout.frame.inline_containing.try_emplace(span.node,InlineContainingBox{empty,empty,span.value.rtl});
            include(layout.frame,span.node,empty);
        }
        for(auto& anchor:buffer.anchors)layout.frame.positioned.push_back(std::move(anchor.box));
        buffer = {};
        return;
    }
    auto& doc = layout.doc;
    auto& frame = layout.frame;
    if (!doc.text_engine) doc.text_engine = std::make_unique<TextEngine>();
    std::vector<TextInlineBox> boxes;
    for (const auto& span : buffer.spans)
        boxes.push_back(span.value);
    const auto shaping_width = layout.measure == Measure::minimum ? 0.0f
                               : layout.measure == Measure::maximum ? std::numeric_limits<float>::max() * .5f : width;
    auto paragraph = doc.text_engine->shape(buffer.runs, boxes, shaping_width, block_style.rtl,
                                            layout.measure == Measure::none ? block_style.text_align : 1,
                                            format(block_style), doc.text_session);
    intrinsic_width = std::max(intrinsic_width, paragraph->width());
    if (layout.measure != Measure::none) { buffer = {}; return; }
    for(auto& anchor:buffer.anchors) {
        if(anchor.inline_level) {
            const auto caret=paragraph->caret(anchor.offset,true);
            anchor.box.anchor.x=x+caret.x;anchor.box.anchor.y=y+caret.y;
            anchor.box.anchor.width=0;
        }else anchor.box.anchor.y=y+paragraph->height();
        frame.positioned.push_back(std::move(anchor.box));
    }
    for (size_t i = 0; i < buffer.spans.size(); ++i)
        if (!buffer.spans[i].value.atomic)
        for (const auto& local : paragraph->inline_shapes(i)) {
            const auto shape=local.moved(x,y);const auto bounds=shape.rect;
            if(buffer.spans[i].value.format.visual_x||buffer.spans[i].value.format.visual_y)
                frame.flow_bounds.push_back({bounds.x-buffer.spans[i].value.format.visual_x,bounds.y-buffer.spans[i].value.format.visual_y,bounds.width,bounds.height});
            include(frame, buffer.spans[i].node, bounds);
            frame.hit_shapes[buffer.spans[i].node].push_back(shape);
            frame.outline(buffer.spans[i].node,bounds,buffer.spans[i].outline,shape.radii);
            const auto& span=buffer.spans[i];
            if(span.positioned) {
                auto padding=bounds;
                padding.x+=span.value.borders[3].width;padding.y+=span.value.borders[0].width;
                padding.width=std::max(0.0f,padding.width-span.value.borders[3].width-span.value.borders[1].width);
                padding.height=std::max(0.0f,padding.height-span.value.borders[0].width-span.value.borders[2].width);
                const auto [entry,added]=frame.inline_containing.emplace(span.node,InlineContainingBox{padding,padding,span.value.rtl});
                if(!added)entry->second.last=padding;
            }
        }
    const auto positions = paragraph->objects();
    for (auto& atomic : buffer.atomics) {
        const auto found = std::find_if(positions.begin(), positions.end(), [&](const auto& entry) { return entry.first == atomic.offset; });
        if (found == positions.end()) throw DocumentFailure{XL_INTERNAL_ERROR};
        const auto dx = x + found->second.x, dy = y + found->second.y;
        layout.merge(std::move(atomic.frame),dx,dy);
    }
    TextItem item{x, y, paragraph, frame.paint.size(), width, {}};
    item.owner=block_style.paint_owner;
    for (auto& source : buffer.sources) {
        source.value.generated = doc.pseudos->generated(doc.resolve(source.value.node));
        source.value.fragments = paragraph->ranges(source.value.offsets.front(), source.value.offsets.back());
        for (const auto& bounds : source.value.fragments)
            include(frame, source.value.node, {bounds.x + x, bounds.y + y, bounds.width, bounds.height});
        source.value.rendered = buffer.text.substr(source.byte_start, source.byte_end - source.byte_start);
        frame.text_nodes.emplace(source.value.node, std::pair{frame.texts.size(), item.sources.size()});
        item.sources.push_back(std::move(source.value));
    }
    frame.texts.push_back(std::move(item));
    if(std::any_of(buffer.runs.begin(),buffer.runs.end(),[](const auto& run){return run.format.visual_x||run.format.visual_y;}))
        frame.flow_bounds.push_back({x,y,paragraph->width(),paragraph->height()});
    layout.last_baseline = y + paragraph->last_baseline();
    if (!layout.first_baseline) layout.first_baseline = y + paragraph->first_baseline();
    y += paragraph->height();
    buffer = {};
}
void Flow::marker(lxb_dom_node_t* owner, const BoxStyle& style) {
    if (!style.disclosure_marker) return;
    auto object = std::make_shared<TextObject>();
    object->width = style.font_size * .9f; object->height = style.font_size; object->baseline = style.font_size * .8f;
    const BoxPaint triangle{{0, 0, object->width, object->height, style.color, 0}, {}, {},
        uint8_t(layout.doc.details_open(owner) ? 2 : style.rtl ? 3 : 1)};
    object->scene = std::make_shared<PaintScene>(std::span(&triangle, 1), std::span<const TextItem>{});
    buffer.object(format(style), std::move(object),style.paint_owner?style.paint_owner:layout.doc.identify(owner));
}
void Flow::legend(lxb_dom_node_t* owner, const BoxStyle& style) {
    flush();
    const auto top = y;
    marker(owner, style);
    // Native content has no DOM text source or selection offset.
    buffer.emit("Details", 7, format(style),style.paint_owner?style.paint_owner:layout.doc.identify(owner));
    buffer.line_start = buffer.trailing_space = false;
    flush();
    if (layout.measure == Measure::none)
        layout.frame.disclosure_legends.insert_or_assign(layout.doc.identify(owner), xl_node_bounds{x, top, width, y - top});
}
void Flow::contents(lxb_dom_node_t* node, const BoxStyle& style, size_t depth) {
    auto* summary = layout.doc.details_summary(node);
    if (node->local_name == LXB_TAG_DETAILS) {
        if (!summary) legend(node, style);
    } else if (auto* owner = layout.doc.disclosure_owner(node)) marker(owner, style);
    for (auto* child : layout.doc.render_children(node)) walk(child, style, depth + 1);
}
void Flow::walk(lxb_dom_node_t* node, const BoxStyle& inherited, size_t depth) {
    if (depth > 256) throw DocumentFailure{XL_UNSUPPORTED};
    if (layout.doc.details_hidden(node)) return;
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        const auto& data = lxb_dom_interface_text(node)->char_data.data;
        touch();
        buffer.append(layout.doc.identify(node), {reinterpret_cast<const char*>(data.data), data.length},
                      format(inherited));
        touch();
        return;
    }
    if (skipped(node)) return;
    auto style = box_style(layout.doc, node, width, layout.frame.width, layout.frame.height, inherited, containing_height);
    layout.frame.unsupported += style.unsupported;
    if (style.hidden) return;
    if(out_of_flow(style)) {
        if(layout.measure==Measure::none) {
            auto box=layout.defer(node,style,inherited,depth,{x,y,width,0});
            buffer.anchors.push_back({buffer.length,std::move(box),style.inline_flow});
        }
        return;
    }
    layout.record(node,style);
    style.inline_x=inherited.inline_x;style.inline_y=inherited.inline_y;
    if(style.inline_flow&&!style.inline_atomic){style.inline_x+=style.relative_x;style.inline_y+=style.relative_y;}
    style.baseline_shift = style.inline_flow ? inherited.baseline_shift + style.vertical_shift : 0;
    style.alignment_group = style.inline_flow ? inherited.alignment_group : 0;
    if (style.inline_flow &&
        (style.vertical_keyword == LXB_CSS_VALUE_TOP || style.vertical_keyword == LXB_CSS_VALUE_BOTTOM)) {
        style.alignment_group = layout.doc.identify(node);
        style.alignment_parent = inherited.alignment_group;
        style.alignment_offset = inherited.baseline_shift;
        style.baseline_shift = 0;
    }
    if (node->local_name == LXB_TAG_BR) {
        touch();
        buffer.hard_break(format(inherited));
        touch();
        return;
    }
    if (style.inline_atomic) {
        touch();
        if (!layout.doc.text_engine) layout.doc.text_engine = std::make_unique<TextEngine>();
        layout.atomic(*this, node, style, inherited, depth);
        touch();
        return;
    }
    if (!style.inline_flow) {
        flush();
        const auto block = layout.block(node, x, y, width, style, depth);
        y += block.height;
        intrinsic_width = std::max(intrinsic_width, block.width);
        for (const auto& ancestor : active) {
            const auto box = layout.frame.boxes.find(layout.doc.identify(node));
            if (box != layout.frame.boxes.end()) {
                include(layout.frame, ancestor.node, box->second);
                layout.frame.outline(ancestor.node, box->second, ancestor.style.outline);
            }
        }
        return;
    }
    if (style.rtl != inherited.rtl) ++layout.frame.unsupported;
    const auto id = layout.doc.identify(node);
    if(style.position.kind!=cssposition::Kind::normal)style.absolute_container=id;
    active.push_back({id, style});
    touch();
    const auto leading =
        (style.rtl ? style.padding_right + style.borders[1].width : style.padding_left + style.borders[3].width);
    const auto trailing =
        (style.rtl ? style.padding_left + style.borders[3].width : style.padding_right + style.borders[1].width);
    auto& initial = buffer.spans[buffer.span_ids.at(id)].value;
    initial.begins = true;
    const auto opening_width = leading + initial.margin_start, closing_width = trailing + initial.margin_end;
    const bool decorated = opening_width || closing_width ||
                           (!node->first_child && (style.alignment_group || style.padding_top || style.padding_bottom ||
                                                   style.borders[0].width || style.borders[2].width));
    if (decorated) {
        initial.opening = true;
        buffer.edge(format(style), opening_width, true,id);
        touch();
    }
    contents(node, style, depth);
    touch();
    if (decorated) {
        buffer.edge(format(style), closing_width, false,id);
        touch();
        buffer.spans[buffer.span_ids.at(id)].value.closing = true;
    }
    buffer.spans[buffer.span_ids.at(id)].value.ends = true;
    touch();
    active.pop_back();
}
} // namespace

DocumentFrame block_layout(Document::Impl& doc, int width, int height, const ScrollOffsets& scrolls) {
    IntrinsicCache intrinsic_cache(scrolls);
    Layout layout{doc, {}, Measure::none, intrinsic_cache};
    layout.frame.identity = doc.identify(lxb_dom_interface_node(doc.dom));
    layout.frame.revision = doc.revision;
    layout.frame.width = width;
    layout.frame.height = height;
    BoxStyle viewport;
    bool canvas_background = false;
    if (doc.dom->body) {
        auto* body = lxb_dom_interface_node(doc.dom->body);
        const auto root =
            body->parent ? box_style(doc, body->parent, float(width), width, height, BoxStyle{}) : BoxStyle{};
        const auto body_style = box_style(doc, body, float(width), width, height, root);
        viewport = root;
        intrinsic_cache.propagated = body->parent;
        if (root.overflow_x == Overflow::visible && root.overflow_y == Overflow::visible && !body_style.hidden) {
            viewport.overflow_x = body_style.overflow_x; viewport.overflow_y = body_style.overflow_y;
            intrinsic_cache.propagated = body;
        }
        const auto viewport_value = [](Overflow value) {
            return value == Overflow::visible ? Overflow::automatic : value == Overflow::clip ? Overflow::hidden : value;
        };
        viewport.overflow_x = viewport_value(viewport.overflow_x); viewport.overflow_y = viewport_value(viewport.overflow_y);
        viewport.padding_top = viewport.padding_right = viewport.padding_bottom = viewport.padding_left = 0;
        viewport.radii={};
        viewport.flex_container = viewport.grid_container = false;
        const bool root_color=root.background_color.alpha>0&&!(root.background_color.missing&8);
        const auto has_image=[](const BoxStyle& style){return style.background_style&&std::any_of(style.background_style->images.begin(),style.background_style->images.end(),[](const auto& image){return bool(image);});};
        const bool root_background=root_color||has_image(root);
        const auto& source=root_background?root:body_style;
        if(root_background||!body_style.hidden)if(source.background||source.background_paint) {
            intrinsic_cache.canvas_background=doc.identify(root_background?body->parent:body);
            layout.frame.paint.push_back({{0,0,float(width),float(height),source.background,0},{}});
            auto& paint=layout.frame.paint.back();paint.background=source.background_paint;paint.background_canvas_node=intrinsic_cache.canvas_background;
            paint.background_node=0;canvas_background=true;
        }
    }
    const BoxStyle initial;
    Flow flow{layout, 0, 0, float(width), initial, {}, {}};
    flow.containing_height = float(height);
    for (auto* node = doc.dom->dom_document.node.first_child; node; node = node->next)
        flow.walk(node, initial, 0);
    flow.flush();
    layout.resolve_inlines();
    layout.resolve_positioned(0,{0,0,float(width),float(height)},viewport.rtl);
    overflow_content(layout.frame, 0, {0, 0, float(width), float(height)}, viewport, scrolls, flow.y);
    if (canvas_background) {
        auto& paint=layout.frame.paint.front();paint.rect.x=paint.rect.y=0;
        if(const auto found=layout.frame.boxes.find(paint.background_canvas_node);found!=layout.frame.boxes.end())paint.background_reference=found->second;
        if(const auto scroll=layout.frame.scrolls.find(0);scroll!=layout.frame.scrolls.end())paint.background_local={scroll->second.info.x,scroll->second.info.y,scroll->second.info.scroll_width,scroll->second.info.scroll_height};
        layout.frame.fix_paint(0);
    }
    layout.resolve_positioned(0,{0,0,float(width),float(height)},viewport.rtl,true);
    if(!layout.frame.positioned.empty())throw DocumentFailure{XL_INTERNAL_ERROR};
    layout.frame.inline_containing.clear();
    std::vector<lxb_dom_node_t*> pending{&doc.dom->dom_document.node};
    while (!pending.empty()) {
        auto* node = pending.back(); pending.pop_back();
        if (node->type == LXB_DOM_NODE_TYPE_TEXT ||
            (node->type == LXB_DOM_NODE_TYPE_ELEMENT && doc.text_field(node))) {
            const auto found = layout.frame.text_nodes.find(doc.identify(node));
            if (found == layout.frame.text_nodes.end()) continue;
            const auto [text, source] = found->second;
            if (layout.frame.texts[text].sources[source].generated) continue;
            layout.frame.texts[text].sources[source].order = layout.frame.text_order.size();
            layout.frame.text_order.push_back(found->second);
            if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) continue;
        } else if (node->type == LXB_DOM_NODE_TYPE_ELEMENT && skipped(node)) continue;
        const auto children = doc.render_children(node);
        for (auto it = children.rbegin(); it != children.rend(); ++it) pending.push_back(*it);
    }
    layout.frame.capture_projection();
    layout.frame.flow_bounds.clear();
    build_display(doc,layout.frame,layout.frame.identity);
    return std::move(layout.frame);
}
} // namespace xl::detail
