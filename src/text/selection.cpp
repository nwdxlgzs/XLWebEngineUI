#include "selection.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unicode/utf8.h>

namespace xl::detail {
namespace {
size_t byte_offset(std::string_view text,uint32_t target) {
    uint32_t offset=0;
    for(int32_t index=0;index<static_cast<int32_t>(text.size());) {
        if(offset==target)return static_cast<size_t>(index);
        UChar32 scalar;
        U8_NEXT(text.data(),index,static_cast<int32_t>(text.size()),scalar);
        if(scalar<0)throw DocumentFailure{XL_INVALID_ARGUMENT};
        offset+=scalar>0xffff?2u:1u;
        if(offset>target)throw DocumentFailure{XL_INVALID_ARGUMENT}; // No split surrogate pairs.
    }
    if(offset!=target)throw DocumentFailure{XL_INVALID_ARGUMENT};
    return text.size();
}
using Order=std::pair<size_t,size_t>;
Order find(const DocumentFrame& frame,const xl_text_position& position) {
    if(position.struct_size<sizeof(position))throw DocumentFailure{XL_ABI_MISMATCH};
    if(position.reserved || position.affinity>XL_TEXT_DOWNSTREAM)throw DocumentFailure{XL_INVALID_ARGUMENT};
    const auto found=frame.text_nodes.find(position.node);
    if(found==frame.text_nodes.end())throw DocumentFailure{XL_NOT_FOUND};
    const auto& source=frame.texts[found->second.first].sources[found->second.second];
    if(source.generated)throw DocumentFailure{XL_UNSUPPORTED};
    if(position.offset_utf16>=source.offsets.size())throw DocumentFailure{XL_INVALID_ARGUMENT};
    // The layout stores invalid_byte at a split surrogate boundary. Drag
    // updates validate it without rescanning the original UTF-8 prefix.
    const auto& boundaries=source.byte_offsets;
    if(!boundaries.empty() && boundaries[position.offset_utf16]==TextSource::invalid_byte)
        throw DocumentFailure{XL_INVALID_ARGUMENT};
    return found->second;
}
struct Range {size_t first,last;uint32_t start,end;};
Range ordered(const DocumentFrame& frame,const xl_text_selection& selection) {
    const auto a=find(frame,selection.anchor),f=find(frame,selection.focus);
    const auto anchor=frame.texts[a.first].sources[a.second].order;
    const auto focus=frame.texts[f.first].sources[f.second].order;
    if(anchor<focus || (anchor==focus && selection.anchor.offset_utf16<=selection.focus.offset_utf16))
        return {anchor,focus,selection.anchor.offset_utf16,selection.focus.offset_utf16};
    return {focus,anchor,selection.focus.offset_utf16,selection.anchor.offset_utf16};
}
template<class Function> void visit(const DocumentFrame& frame,const Range& range,Function&& function) {
    for(size_t order=range.first;order<=range.last;++order) {
        const auto [text_index,index]=frame.text_order.at(order);
        const auto& text=frame.texts[text_index];
        const auto& source=text.sources[index];
        const auto start=order==range.first?range.start:0;
        const auto end=order==range.last?range.end:static_cast<uint32_t>(source.offsets.size()-1);
        function(text,source,start,end);
    }
}
}
xl_text_position hit_text(const DocumentFrame& frame,float x,float y) {
        const TextItem* nearest = nullptr;
        const TextSource* chosen = nullptr;
        float hit_x = x, hit_y = y;
        double distance = std::numeric_limits<double>::infinity();
        size_t rank=0;
        const auto top=frame.hit_paint(x,y);
        const auto descendant=[&](xl_node_id id,xl_node_id parent){
            for(size_t depth=0;id&&depth<=256;++depth){if(id==parent)return true;const auto found=frame.paint_nodes.find(id);if(found==frame.paint_nodes.end())break;id=found->second.parent;}return false;
        };
        for (const auto& text : frame.texts) {
            if (!text.clip.contains(x, y)) continue;
            if (!text.control && std::none_of(text.sources.begin(), text.sources.end(), [](const TextSource& source) {
                    return source.offsets.front() != source.offsets.back(); })) continue;
            // A containing paragraph also covers the space occupied by atomic
            // children. Hit actual source fragments so it cannot hide them.
            for (const auto& source : text.sources) {
                if (source.generated) continue;
                const auto consider = [&](const xl_node_bounds& rect) {
                    const auto visible = text.clip.bounds({text.x + rect.x, text.y + rect.y, rect.width, rect.height});
                    if (!nonempty(visible)) return;
                    const auto hx = std::clamp(x, visible.x, visible.x + visible.width);
                    const auto hy = std::clamp(y, visible.y, visible.y + visible.height);
                    const double dx = x - hx, dy = y - hy, next = dx * dx + dy * dy;
                    if(next==0&&top&&!descendant(source.node,top)&&!descendant(top,source.node))return;
                    const auto found=frame.paint_ranks.find(source.node);const auto next_rank=found==frame.paint_ranks.end()?0:found->second;
                    if (next < distance||(next==distance&&next_rank>=rank)) {
                        distance = next; nearest = &text; chosen = &source; hit_x = hx; hit_y = hy;
                        rank=next_rank;
                    }
                };
                for (const auto& rect : source.fragments) consider(rect);
                if (text.control && source.fragments.empty()) consider({0, 0, text.layout_width, text.paragraph->height()});
            }
        }
        if (!nearest) throw DocumentFailure{XL_NOT_FOUND};
        const auto [offset, downstream] = nearest->paragraph->hit(hit_x - nearest->x, hit_y - nearest->y);
        if (!chosen) throw DocumentFailure{XL_NOT_FOUND};
        const auto mapped = std::clamp(offset, chosen->offsets.front(), chosen->offsets.back());
        auto boundary = downstream ? std::upper_bound(chosen->offsets.begin(), chosen->offsets.end(), mapped)
                                   : std::lower_bound(chosen->offsets.begin(), chosen->offsets.end(), mapped);
        if (downstream && boundary != chosen->offsets.begin()) --boundary;
        auto dom_offset = uint32_t(boundary - chosen->offsets.begin());
        while (!chosen->byte_offsets.empty() && chosen->byte_offsets[dom_offset] == TextSource::invalid_byte) {
            if (downstream && dom_offset + 1 < chosen->byte_offsets.size()) ++dom_offset;
            else --dom_offset;
        }
        return {sizeof(xl_text_position), dom_offset, chosen->node,
                     static_cast<uint32_t>(downstream ? XL_TEXT_DOWNSTREAM : XL_TEXT_UPSTREAM), 0};
}
void validate_selection(const DocumentFrame& frame,const xl_text_selection& selection) {
    if(selection.struct_size<sizeof(selection))throw DocumentFailure{XL_ABI_MISMATCH};
    if(selection.reserved)throw DocumentFailure{XL_INVALID_ARGUMENT};
    (void)ordered(frame,selection);
}
std::vector<xl_node_bounds> selection_ranges(const DocumentFrame& frame,const xl_text_selection& selection) {
    std::vector<xl_node_bounds> result;
    visit(frame,ordered(frame,selection),[&](const TextItem& text,const TextSource& source,uint32_t start,uint32_t end) {
        for(auto rect:text.paragraph->ranges(source.offsets[start],source.offsets[end])) {
            rect.x+=text.x;rect.y+=text.y;
            rect = text.clip.bounds(rect);
            if (nonempty(rect)) result.push_back(rect);
        }
    });
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b) {
        if(a.y!=b.y)return a.y<b.y;
        if(a.height!=b.height)return a.height<b.height;
        return a.x<b.x;
    });
    std::vector<xl_node_bounds> merged;
    for(const auto& rect:result) {
        if(!merged.empty() && std::abs(rect.y-merged.back().y)<.01f && std::abs(rect.height-merged.back().height)<.01f &&
           rect.x<=merged.back().x+merged.back().width+.01f)
            merged.back().width=std::max(merged.back().x+merged.back().width,rect.x+rect.width)-merged.back().x;
        else merged.push_back(rect);
    }
    return merged;
}
xl_node_bounds caret_bounds(const DocumentFrame& frame,const xl_text_position& position) {
    const auto order=find(frame,position);
    const auto& item=frame.texts[order.first];
    auto bounds=item.paragraph->caret(item.sources[order.second].offsets[position.offset_utf16],position.affinity==XL_TEXT_DOWNSTREAM);
    bounds.x+=item.x;bounds.y+=item.y;
    return item.clip.bounds(bounds);
}
std::string selection_text(const DocumentFrame& frame,const xl_text_selection& selection,bool skip_controls) {
    std::string result;
    visit(frame,ordered(frame,selection),[&](const TextItem& text,const TextSource& source,uint32_t start,uint32_t end) {
        // Reading selections must not export control values (including a
        // blurred password); focused editor copies use their own selection.
        if (skip_controls && text.control) return;
        const auto first=source.byte_offsets.empty()?byte_offset(source.original,start):source.byte_offsets[start];
        const auto last=source.byte_offsets.empty()?byte_offset(source.original,end):source.byte_offsets[end];
        result.append(source.original,first,last-first);
    });
    return result;
}
}
