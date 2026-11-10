#include "document.h"
#include "frame_projection.h"

namespace xl::detail {
void DocumentFrame::capture_projection() {
    if(!projection)projection=std::make_shared<FrameProjection>();
    else if(projection.use_count()!=1)projection=std::make_shared<FrameProjection>(*projection);
    projection->capture(*this);
}
void DocumentFrame::include_projection(xl_node_id id,xl_node_bounds bounds) {
    if(projection){if(projection.use_count()!=1)projection=std::make_shared<FrameProjection>(*projection);const auto clip=box_clips.find(id);projection->include(id,bounds,clip==box_clips.end()?Clip{}:clip->second);}
}
void DocumentFrame::merge_projection(DocumentFrame& child,bool embedded) {capture_projection();child.capture_projection();projection->merge(*child.projection,embedded);}
void DocumentFrame::scroll(xl_node_id id,float x,float y) {capture_projection();projection->scroll(id);move(-x,-y,false);}
void DocumentFrame::fix_paint(size_t index) {capture_projection();projection->fix_paint(index,paint.at(index));}
DocumentFrame DocumentFrame::project(const ScrollOffsets& offsets) const {if(!projection)throw DocumentFailure{XL_INTERNAL_ERROR};auto result=*this;projection->project(result,offsets);return result;}
void DocumentFrame::move(float x, float y,bool record) {
    if(!x&&!y)return;
    if(record) {
        for(auto& item:positioned){item.anchor.x+=x;item.anchor.y+=y;if(item.grid_area){item.grid_edges[0]+=x;item.grid_edges[2]+=x;item.grid_edges[1]+=y;item.grid_edges[3]+=y;}}
        for(auto& [id,item]:inline_containing){item.first.x+=x;item.first.y+=y;item.last.x+=x;item.last.y+=y;}
    }
    if(record){capture_projection();projection->move(x,y);}
    for (auto& box : paint) { box.rect.x += x; box.rect.y += y; box.clip = box.clip.moved(x, y); }
    for (auto& text : texts) { text.x += x; text.y += y; text.clip = text.clip.moved(x, y); }
    for (auto& outline : outlines) {
        for (auto& fragment : outline.fragments) { fragment.x += x; fragment.y += y; }
        outline.clip = outline.clip.moved(x, y);
    }
    for (auto& [id, bounds] : boxes) { bounds.x += x; bounds.y += y; }
    for(auto& bounds:flow_bounds){bounds.x+=x;bounds.y+=y;}
    for (auto& [id, bounds] : disclosure_legends) { bounds.x += x; bounds.y += y; }
    for (auto& [id, clip] : box_clips) clip = clip.moved(x, y);
    for(auto& [id,shapes]:hit_shapes){(void)id;for(auto& shape:shapes)shape=shape.moved(x,y);}
    for (auto& [id, scroll] : scrolls) {
        scroll.port.x += x; scroll.port.y += y; scroll.clip = scroll.clip.moved(x, y);
    }
}
void DocumentFrame::clip(const Clip& clip) {
    capture_projection();projection->clip(clip);
    for (auto& box : paint) box.clip = box.clip.intersect(clip);
    for (auto& text : texts) text.clip = text.clip.intersect(clip);
    for (auto& outline : outlines) outline.clip = outline.clip.intersect(clip);
    for (const auto& [id, bounds] : boxes) {
        auto& current = box_clips[id]; current = current.intersect(clip);
    }
    for (auto& [id, scroll] : scrolls) scroll.clip = scroll.clip.intersect(clip);
    for(auto& bounds:flow_bounds)bounds=clip.bounds(bounds);
}
void DocumentFrame::outline(xl_node_id node, xl_node_bounds fragment, const OutlineStyle& style,CornerRadii radii) {
    if (!style.visible() || !nonempty(fragment)) return;
    const auto [entry, added] = outline_nodes.emplace(node, outlines.size());
    if (added) outlines.push_back({style, {}});
    auto& paint = outlines[entry->second];
    if (paint.fragments.size() >= 4096) throw DocumentFailure{XL_UNSUPPORTED};
    paint.fragments.push_back(fragment);
    paint.radii.push_back(radii);
}
xl_node_bounds DocumentFrame::visible_bounds(xl_node_id id) const {
    const auto found = boxes.find(id);
    if (found == boxes.end()) return {};
    const auto clip = box_clips.find(id);
    return clip == box_clips.end() ? found->second : clip->second.bounds(found->second);
}
xl_node_id DocumentFrame::hit_paint(float x,float y,const std::function<bool(xl_node_id)>& accept) const {
    if(x<0||y<0||x>=width||y>=height)return 0;
    for(auto it=display.rbegin();it!=display.rend();++it) {
        if(!it->owner||it->kind==PaintCommand::outline)continue;
        xl_node_bounds bounds{};const Clip* clip=nullptr;
        if(it->kind==PaintCommand::box) {
            const auto& box=paint.at(it->index);bounds={box.rect.x,box.rect.y,box.rect.width,box.rect.height};clip=&box.clip;
            if(!RoundedRect{bounds,box.radii}.contains(x,y))continue;
        }else {
            const auto& item=texts.at(it->index);const auto& part=item.paragraph->parts().at(it->part);
            bounds=part.bounds;bounds.x+=item.x;bounds.y+=item.y;clip=&item.clip;
            if(part.kind==TextParagraph::Part::glyph)if(const auto source=text_nodes.find(it->owner);source!=text_nodes.end()) {
                const auto& text=texts.at(source->second.first);const auto& fragments=text.sources.at(source->second.second).fragments;
                if(!std::any_of(fragments.begin(),fragments.end(),[&](const auto& fragment){return x>=text.x+fragment.x&&x<text.x+fragment.x+fragment.width&&y>=text.y+fragment.y&&y<text.y+fragment.y+fragment.height;}))continue;
            }
            if(part.kind==TextParagraph::Part::decoration) {
                const auto shapes=hit_shapes.find(it->owner);
                if(shapes!=hit_shapes.end()&&!std::any_of(shapes->second.begin(),shapes->second.end(),[&](const auto& shape){return shape.contains(x,y);}))continue;
            }
        }
        if(!clip->contains(x,y)||!nonempty(bounds)||x<bounds.x||x>=bounds.x+bounds.width||y<bounds.y||y>=bounds.y+bounds.height)continue;
        if(accept&&!accept(it->owner))continue;
        return it->owner;
    }
    return 0;
}
} // namespace xl::detail
