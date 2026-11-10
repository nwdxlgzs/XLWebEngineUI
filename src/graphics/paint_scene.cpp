#include "paint_scene.h"
#include "document/document.h"
#include "include/core/SkCanvas.h"
#include <algorithm>

namespace xl::detail {
PaintScene::PaintScene(std::span<const BoxPaint> boxes, std::span<const TextItem> texts, std::span<const OutlinePaint> outlines)
    : boxes_(boxes.begin(), boxes.end()), outlines_(outlines.begin(), outlines.end()) {
    SkRect ink = SkRect::MakeEmpty();
    SkRect curves=SkRect::MakeEmpty();
    const auto add_curve=[&](xl_node_bounds r){if(nonempty(r)){curved_=true;curves.join(SkRect::MakeXYWH(r.x,r.y,r.width,r.height));}};
    const auto clips=[&](const Clip& clip){if(clip.curves)for(const auto& curve:*clip.curves)add_curve(curve.rect);};
    for(const auto& box:boxes_) {
        clips(box.clip);
        if(box.image||RoundedRect{{},box.radii}.curved())add_curve({box.rect.x,box.rect.y,box.rect.width,box.rect.height});
    }
    for (const auto& box : boxes_)
        if (box.rect.argb || box.background || box.image || std::any_of(box.borders.begin(), box.borders.end(),
                                       [](const auto& side) { return side.width > 0; }))
        {
            const auto visible = box.clip.bounds({box.rect.x, box.rect.y, box.rect.width, box.rect.height});
            ink.join(SkRect::MakeXYWH(visible.x, visible.y, visible.width, visible.height));
        }
    for (const auto& text : texts) {
        if (text.embedded) continue;
        clips(text.clip);
        if(text.paragraph->curved()){auto curve=text.paragraph->curve_bounds();curve.x+=text.x;curve.y+=text.y;add_curve(curve);}
        texts_.push_back({text.x, text.y, text.paragraph, text.paint_index, text.clip,text.scenes});
        const auto bounds = text.paragraph->ink_bounds();
        const auto visible = text.clip.bounds({text.x + bounds.x, text.y + bounds.y, bounds.width, bounds.height});
        ink.join(SkRect::MakeXYWH(visible.x, visible.y, visible.width, visible.height));
    }
    for (const auto& outline : outlines_) {
        auto shape = std::make_shared<OutlineShape>(outline);
        const auto bounds = shape->ink_bounds();
        clips(outline.clip);
        if(std::any_of(outline.radii.begin(),outline.radii.end(),[](const auto& r){return RoundedRect{{},r}.curved();}))add_curve(bounds);
        ink.join(SkRect::MakeXYWH(bounds.x, bounds.y, bounds.width, bounds.height));
        outline_shapes_.push_back(std::move(shape));
    }
    ink_ = {ink.x(), ink.y(), ink.width(), ink.height()};
    curve_ink_={curves.x(),curves.y(),curves.width(),curves.height()};
}
PaintScene::PaintScene(const DocumentFrame& source):PaintScene(source.paint,source.texts,source.outlines) {
    order_=source.display;
    auto frame=std::make_shared<DocumentFrame>();
    frame->display=source.display;
    frame->projection=source.projection;frame->paint=source.paint;frame->outlines=source.outlines;frame->scrolls=source.scrolls;
    for(const auto& item:source.texts){TextItem copy;copy.x=item.x;copy.y=item.y;copy.paragraph=item.paragraph;copy.paint_index=item.paint_index;copy.embedded=item.embedded;copy.clip=item.clip;copy.scenes=item.scenes;frame->texts.push_back(std::move(copy));}
    projection_=std::move(frame);
}
bool PaintScene::scrollable() const {return projection_&&!projection_->scrolls.empty();}
std::shared_ptr<const PaintScene> PaintScene::images(const ImageUpdates& images) const {
    std::shared_ptr<PaintScene> next;
    const auto copy=[&]()->PaintScene&{if(!next)next=std::make_shared<PaintScene>(*this);return *next;};
    for(size_t i=0;i<boxes_.size();++i){const auto& box=boxes_[i];if(const auto found=images.find(box.owner);found!=images.end()&&box.image!=found->second)copy().boxes_[i].image=found->second;}
    for(size_t i=0;i<texts_.size();++i){const auto& text=texts_[i];auto scenes=text.paragraph->image_scenes(images,text.scenes);if(scenes!=text.scenes)copy().texts_[i].scenes=std::move(scenes);}
    if(!next)return {};
    if(projection_){auto frame=std::make_shared<DocumentFrame>(*projection_);
        for(auto& box:frame->paint)if(const auto found=images.find(box.owner);found!=images.end())box.image=found->second;
        size_t index=0;
        // These are the same paragraphs already refreshed above. Re-entering
        // their object scenes here doubles the work at every nesting level.
        for(auto& text:frame->texts)if(!text.embedded)text.scenes=next->texts_.at(index++).scenes;
        next->projection_=std::move(frame);
    }
    return next;
}
std::shared_ptr<const PaintScene> PaintScene::project(const TextScrollOffsets& offsets) const {
    if(!scrollable())return {};
    bool changed=false;
    for(const auto& [id,scroll]:projection_->scrolls) {
        const auto found=offsets.find(id);const auto [x,y]=found==offsets.end()?std::pair{0.f,0.f}:found->second;
        if(std::clamp(x,scroll.info.min_x,scroll.info.max_x)!=scroll.info.x||std::clamp(y,scroll.info.min_y,scroll.info.max_y)!=scroll.info.y){changed=true;break;}
    }
    if(!changed)return {};
    return std::make_shared<PaintScene>(projection_->project(offsets));
}
void PaintScene::paint(SkCanvas* canvas, float x, float y) const {
    canvas->save();
    canvas->translate(x, y);
    if(!order_.empty())for(const auto& command:order_) {
        if(command.kind==PaintCommand::box)paint_box(canvas,boxes_.at(command.index));
        else if(command.kind==PaintCommand::outline)outline_shapes_.at(command.index)->paint(canvas);
        else {
            const auto& item=texts_.at(command.index);SkAutoCanvasRestore restore(canvas,true);apply_clip(canvas,item.clip);
            item.paragraph->paint_part(canvas,item.x,item.y,command.part,item.scenes.get());
        }
    }else {
    size_t text = 0;
    for (size_t box = 0; box <= boxes_.size(); ++box) {
        while (text < texts_.size() && texts_[text].paint_index <= box) {
            const auto& item = texts_[text++];
            SkAutoCanvasRestore restore(canvas, true);
            apply_clip(canvas, item.clip);
            item.paragraph->paint(canvas, item.x, item.y,item.scenes.get());
        }
        if (box < boxes_.size()) paint_box(canvas, boxes_[box]);
    }
    for (const auto& outline : outline_shapes_) outline->paint(canvas);
    }
    canvas->restore();
}
xl_node_bounds PaintScene::ink_bounds() const { return ink_; }
bool PaintScene::operator==(const PaintScene& other) const {
    // Identical pixels can belong to different scroll nodes, and paragraphs
    // retain their atomic scenes. Never reuse another scene's projection
    // bindings solely because its current appearance happens to match.
    if((scrollable()||other.scrollable())&&projection_!=other.projection_)return false;
    if (order_!=other.order_||texts_ != other.texts_ || boxes_.size() != other.boxes_.size() || outlines_.size() != other.outlines_.size()) return false;
    for (size_t i = 0; i < outlines_.size(); ++i)
        if (!outline_equal(outlines_[i], other.outlines_[i])) return false;
    for (size_t i = 0; i < boxes_.size(); ++i) {
        const auto& a = boxes_[i];
        const auto& b = other.boxes_[i];
        if (a.rect.x != b.rect.x || a.rect.y != b.rect.y || a.rect.width != b.rect.width ||
            a.rect.height != b.rect.height || a.rect.argb != b.rect.argb || a.borders != b.borders || a.clip != b.clip || a.disclosure != b.disclosure || a.radii!=b.radii||!background_equal(a,b)||a.image!=b.image||
            a.image_rect.x!=b.image_rect.x||a.image_rect.y!=b.image_rect.y||a.image_rect.width!=b.image_rect.width||a.image_rect.height!=b.image_rect.height) return false;
    }
    return true;
}
bool TextObject::operator==(const TextObject& other) const {
    return width == other.width && height == other.height && baseline == other.baseline &&flattened==other.flattened&&
           (scene == other.scene || (scene && other.scene && *scene == *other.scene));
}
bool TextRun::operator==(const TextRun& other) const {
    return text == other.text && format == other.format && edge == other.edge && edge_width == other.edge_width &&owner==other.owner&&
           (object == other.object || (object && other.object && *object == *other.object));
}
}
