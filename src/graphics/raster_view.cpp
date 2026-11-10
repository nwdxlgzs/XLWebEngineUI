#include "raster_view.h"
#include "gpu_device.h"
#include "render_executor.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>

namespace xl::detail {
struct RasterView::Impl {
#if defined(__linux__)
    std::unique_ptr<RenderExecutor> executor;
#endif
    std::unique_ptr<GpuDevice> gpu;
    sk_sp<SkSurface> surface;
    xl_graphics_info graphics{sizeof(xl_graphics_info),XL_GRAPHICS_RASTER,XL_GRAPHICS_RASTER,1,0,0,1,0};
    size_t next_candidate = 0;
    static constexpr uint32_t candidates[]{XL_GRAPHICS_D3D12,XL_GRAPHICS_ANGLE_D3D11,XL_GRAPHICS_VULKAN,
        XL_GRAPHICS_OPENGL,XL_GRAPHICS_ANGLE_OPENGL,XL_GRAPHICS_DAWN,XL_GRAPHICS_DAWN_SWIFTSHADER,
        XL_GRAPHICS_SWIFTSHADER,XL_GRAPHICS_RASTER};
    void drop_surface() {
        if(gpu && !gpu->current())gpu->abandon();
        surface.reset();
        if(gpu)gpu->release();
        gpu.reset();
    }
    ~Impl() { drop_surface(); }
    void select(int width,int height) {
        for(;next_candidate<std::size(candidates);) {
            const auto candidate=candidates[next_candidate++];
            graphics.attempted_mask|=1u<<candidate;
            std::unique_ptr<GpuDevice> fresh_gpu;
            sk_sp<SkSurface> fresh_surface;
            if(candidate==XL_GRAPHICS_RASTER) fresh_surface=SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width,height));
            else {
                fresh_gpu=make_gpu_device(candidate);
                if(fresh_gpu)fresh_surface=fresh_gpu->surface(width,height);
                if(fresh_gpu)fresh_gpu->release();
            }
            if(fresh_surface) {
                drop_surface();gpu=std::move(fresh_gpu);surface=std::move(fresh_surface);
                graphics.active=candidate;++graphics.generation;damage={0,0,width,height};ready=false;return;
            }
            graphics.failed_mask|=1u<<candidate;
        }
        throw std::bad_alloc();
    }
    void recover() {
        const int width=surface->width(),height=surface->height();
        graphics.failed_mask|=1u<<graphics.active;++graphics.recoveries;
        select(width,height);
    }
    std::vector<BoxPaint> rectangles;
    std::vector<TextPaint> texts;
    struct Outline { OutlinePaint paint; std::shared_ptr<const OutlineShape> shape; };
    std::vector<Outline> outlines;
    std::vector<PaintCommand> order;
    std::vector<Damage> order_bounds;
    std::vector<xl_paint_rect> overlays;
    Damage damage;
    uint32_t background;
    uint64_t frames = 0, pixels = 0;
    bool ready = false;

    void include(Damage next) {
        if (next.empty()) return;
        if (damage.empty())
            damage = next;
        else
            damage = {std::min(damage.left, next.left), std::min(damage.top, next.top),
                      std::max(damage.right, next.right), std::max(damage.bottom, next.bottom)};
    }
    Damage bounds(const xl_paint_rect& input, const Clip& clip = {}) const {
        const auto rect = clip.bounds({input.x, input.y, input.width, input.height});
        if (!nonempty(rect)) return {};
        const double width = surface->width(), height = surface->height();
        // Expand for antialiasing; clamp before converting potentially huge floats.
        return {static_cast<int>(std::clamp(std::floor(double(rect.x)) - 1, 0.0, width)),
                static_cast<int>(std::clamp(std::floor(double(rect.y)) - 1, 0.0, height)),
                static_cast<int>(std::clamp(std::ceil(double(rect.x) + rect.width) + 1, 0.0, width)),
                static_cast<int>(std::clamp(std::ceil(double(rect.y) + rect.height) + 1, 0.0, height))};
    }
    void stabilize_curves() {
        if (damage.empty() || (damage.left == 0 && damage.top == 0 &&
                              damage.right == surface->width() && damage.bottom == surface->height())) return;
        // Skia's CPU conic coverage can change when a path is chopped to a
        // smaller tile. Keep intersecting curved primitives whole, including
        // those reached as the damage region grows; rectangular regions retain
        // their original small damage and static frames never reach this code.
        std::vector<Damage> regions;
        const auto add = [&](xl_node_bounds rect) {
            const auto region = bounds({rect.x, rect.y, rect.width, rect.height, 0, 0});
            if (!region.empty()) regions.push_back(region);
        };
        const auto clips = [&](const Clip& clip) {
            if (clip.curves) for (const auto& curve : *clip.curves) add(curve.rect);
        };
        for (const auto& box : rectangles) {
            clips(box.clip);
            if (box.image||RoundedRect{{}, box.radii}.curved()) add({box.rect.x,box.rect.y,box.rect.width,box.rect.height});
        }
        for (const auto& text : texts) {
            clips(text.clip);
            if (text.paragraph->curved()) {
                auto rect=text.paragraph->curve_bounds();rect.x+=text.x;rect.y+=text.y;add(rect);
            }
        }
        for (const auto& outline : outlines) {
            clips(outline.paint.clip);
            if (std::any_of(outline.paint.radii.begin(),outline.paint.radii.end(),
                            [](const auto& radii){return RoundedRect{{},radii}.curved();})) add(outline.shape->ink_bounds());
        }
        bool changed;
        do {
            changed=false;
            for (auto& region : regions) {
                if (region.empty() || region.right<=damage.left || region.left>=damage.right ||
                    region.bottom<=damage.top || region.top>=damage.bottom) continue;
                const auto old=damage;
                include(region);region={};
                changed |= old.left!=damage.left || old.top!=damage.top || old.right!=damage.right || old.bottom!=damage.bottom;
            }
        } while(changed);
    }
};

RasterView::RasterView(int width, int height, uint32_t background) : impl_(std::make_unique<Impl>()) {
    impl_->background = background;
    impl_->next_candidate=std::size(Impl::candidates)-1;
    resize(width, height);
}
RasterView::~RasterView() {
#if defined(__linux__)
    if (impl_->executor) impl_->executor->invoke([this] { impl_->drop_surface(); });
#endif
}
void RasterView::resize(int width, int height) {
#if defined(__linux__)
    if (impl_->executor && !impl_->executor->current())
        return impl_->executor->invoke([=, this] { resize(width, height); });
#endif
    if (width <= 0 || height <= 0) throw std::invalid_argument("positive drawable dimensions required");
    if (impl_->surface && width == this->width() && height == this->height()) return;
    auto surface = impl_->gpu ? impl_->gpu->surface(width,height) : SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width,height));
    if(!surface && impl_->gpu) {
        impl_->gpu->release();
        impl_->graphics.failed_mask|=1u<<impl_->graphics.active;++impl_->graphics.recoveries;
        impl_->select(width,height);return;
    }
    if (!surface) throw std::bad_alloc();
    impl_->surface = std::move(surface);
    if(impl_->gpu)impl_->gpu->release();
    impl_->damage = {0, 0, width, height};
    impl_->ready = false;
}
void RasterView::background(uint32_t color) {
    if (impl_->background == color) return;
    impl_->background = color;
    impl_->damage = {0, 0, width(), height()};
}
void RasterView::rects(std::span<const xl_paint_rect> rectangles) {
    for (const auto& rect : rectangles) {
        if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
            !std::isfinite(rect.height) || rect.width < 0 || rect.height < 0 || rect.reserved)
            throw std::invalid_argument("invalid paint rectangle");
    }
    std::vector<BoxPaint> next;
    for (const auto& rectangle : rectangles)
        next.push_back({rectangle, {}});
    boxes(next);
    order({});
}
void RasterView::boxes(std::span<const BoxPaint> boxes) {
    for (const auto& box : boxes) {
        const auto& rect = box.rect;
        if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
            !std::isfinite(rect.height) || rect.width < 0 || rect.height < 0 || rect.reserved)
            throw std::invalid_argument("invalid paint box");
        for (const auto& side : box.borders)
            if (!std::isfinite(side.width) || side.width < 0) throw std::invalid_argument("invalid border width");
    }
    std::vector<BoxPaint> next(boxes.begin(), boxes.end());
    const auto count = std::max(next.size(), impl_->rectangles.size());
    for (size_t index = 0; index < count; ++index) {
        const auto* old = index < impl_->rectangles.size() ? &impl_->rectangles[index] : nullptr;
        const auto* fresh = index < next.size() ? &next[index] : nullptr;
        if (old && fresh && old->rect.x == fresh->rect.x && old->rect.y == fresh->rect.y &&
            old->rect.width == fresh->rect.width && old->rect.height == fresh->rect.height &&
            old->rect.argb == fresh->rect.argb && old->borders == fresh->borders && old->clip == fresh->clip && old->disclosure == fresh->disclosure && old->radii==fresh->radii&&background_equal(*old,*fresh)&&old->image==fresh->image&&
            old->image_rect.x==fresh->image_rect.x&&old->image_rect.y==fresh->image_rect.y&&old->image_rect.width==fresh->image_rect.width&&old->image_rect.height==fresh->image_rect.height)
            continue;
        if (old) impl_->include(impl_->bounds(old->rect, old->clip));
        if (fresh) impl_->include(impl_->bounds(fresh->rect, fresh->clip));
    }
    impl_->rectangles = std::move(next);
}
void RasterView::invalidate(int x, int y, int width, int height) {
    if (width < 0 || height < 0) throw std::invalid_argument("negative damage dimensions");
    impl_->include({std::clamp(x, 0, this->width()), std::clamp(y, 0, this->height()),
                    static_cast<int>(std::clamp(int64_t(x) + width, int64_t(0), int64_t(this->width()))),
                    static_cast<int>(std::clamp(int64_t(y) + height, int64_t(0), int64_t(this->height())))});
}
void RasterView::texts(std::span<const TextItem> items) {
    std::vector<TextPaint> next;
    next.reserve(items.size());
    for (const auto& item : items)
        if (!item.embedded) next.push_back({item.x, item.y, item.paragraph, item.paint_index, item.clip,item.scenes});
    const auto damage = [&](const TextPaint& item) {
        const auto ink = item.paragraph->ink_bounds();
        impl_->include(impl_->bounds({item.x + ink.x - 1, item.y + ink.y - 1, ink.width + 2, ink.height + 2, 0, 0}, item.clip));
    };
    for (size_t index = 0; index < std::max(next.size(), impl_->texts.size()); ++index) {
        const auto* old = index < impl_->texts.size() ? &impl_->texts[index] : nullptr;
        const auto* fresh = index < next.size() ? &next[index] : nullptr;
        if (old && fresh && old->x == fresh->x && old->y == fresh->y && old->paint_index == fresh->paint_index &&
            old->paragraph == fresh->paragraph && old->clip == fresh->clip&&old->scenes==fresh->scenes)
            continue;
        if (old) damage(*old);
        if (fresh) damage(*fresh);
    }
    impl_->texts = std::move(next);
}
void RasterView::overlays(std::span<const xl_paint_rect> rectangles) {
    std::vector<xl_paint_rect> next(rectangles.begin(),rectangles.end());
    const auto equal=[](const xl_paint_rect& a,const xl_paint_rect& b) {
        return a.x==b.x && a.y==b.y && a.width==b.width && a.height==b.height && a.argb==b.argb;
    };
    if(next.size()==impl_->overlays.size() && std::equal(next.begin(),next.end(),impl_->overlays.begin(),equal))return;
    for(const auto& rect:impl_->overlays)impl_->include(impl_->bounds(rect));
    for(const auto& rect:next)impl_->include(impl_->bounds(rect));
    impl_->overlays=std::move(next);
}
void RasterView::outlines(std::span<const OutlinePaint> outlines) {
    std::vector<Impl::Outline> next;
    next.reserve(outlines.size());
    const auto damage = [&](const OutlineShape& shape) {
        const auto ink = shape.ink_bounds();
        impl_->include(impl_->bounds({ink.x, ink.y, ink.width, ink.height, 0, 0}));
    };
    for (size_t i = 0; i < std::max(outlines.size(), impl_->outlines.size()); ++i) {
        const auto* old = i < impl_->outlines.size() ? &impl_->outlines[i] : nullptr;
        if (i < outlines.size()) {
            if (old && outline_equal(old->paint, outlines[i])) { next.push_back(*old); continue; }
            auto shape = std::make_shared<OutlineShape>(outlines[i]);
            damage(*shape); next.push_back({outlines[i], std::move(shape)});
        }
        if (old) damage(*old->shape);
    }
    impl_->outlines = std::move(next);
}
void RasterView::order(std::span<const PaintCommand> commands) {
    std::vector<PaintCommand> next(commands.begin(),commands.end());
    std::vector<Damage> regions;regions.reserve(next.size());
    for(const auto& command:next) {
        if(command.kind==PaintCommand::box) {
            const auto& box=impl_->rectangles.at(command.index);regions.push_back(impl_->bounds(box.rect,box.clip));
        }else if(command.kind==PaintCommand::outline) {
            const auto b=impl_->outlines.at(command.index).shape->ink_bounds();regions.push_back(impl_->bounds({b.x,b.y,b.width,b.height,0,0}));
        }else {
            const auto& text=impl_->texts.at(command.index);const auto& b=text.paragraph->parts().at(command.part).bounds;
            regions.push_back(impl_->bounds({text.x+b.x,text.y+b.y,b.width,b.height,0,0},text.clip));
        }
    }
    if(next!=impl_->order) {
        size_t first=0;while(first<std::min(next.size(),impl_->order.size())&&next[first]==impl_->order[first])++first;
        auto old_end=impl_->order.size(),new_end=next.size();
        while(old_end>first&&new_end>first&&impl_->order[old_end-1]==next[new_end-1]){--old_end;--new_end;}
        for(size_t i=first;i<old_end;++i)impl_->include(impl_->order_bounds.at(i));
        for(size_t i=first;i<new_end;++i)impl_->include(regions.at(i));
    }
    impl_->order=std::move(next);impl_->order_bounds=std::move(regions);
}
bool RasterView::dirty() const noexcept {
    return !impl_->damage.empty();
}
bool RasterView::ready() const noexcept {
    return impl_->ready;
}
int RasterView::width() const noexcept {
    return impl_->surface->width();
}
int RasterView::height() const noexcept {
    return impl_->surface->height();
}
uint64_t RasterView::frames() const noexcept {
    return impl_->frames;
}
uint64_t RasterView::painted_pixels() const noexcept {
    return impl_->pixels;
}
void RasterView::graphics_backend(uint32_t backend) {
#if defined(__linux__)
    if (!impl_->executor && backend != XL_GRAPHICS_RASTER)
        impl_->executor = std::make_unique<RenderExecutor>();
    if (impl_->executor && !impl_->executor->current())
        return impl_->executor->invoke([=, this] { graphics_backend(backend); });
#endif
    const auto width=this->width(),height=this->height();
    impl_->graphics.requested=backend;impl_->graphics.attempted_mask=0;impl_->graphics.failed_mask=0;
    const auto begin=std::begin(Impl::candidates),end=std::end(Impl::candidates);
    impl_->next_candidate=backend==XL_GRAPHICS_AUTO?0:size_t(std::find(begin,end,backend)-begin);
    impl_->select(width,height);
}
xl_graphics_info RasterView::graphics_info() const {
    auto value=impl_->graphics;
    value.shader_compiler=impl_->gpu?impl_->gpu->shader_compiler():XL_SHADER_COMPILER_NONE;
    return value;
}
void RasterView::abandon_graphics() {
#if defined(__linux__)
    if (impl_->executor && !impl_->executor->current())
        return impl_->executor->invoke([this] { abandon_graphics(); });
#endif
    if(impl_->gpu)impl_->gpu->abandon();
    impl_->ready=false;impl_->damage={0,0,width(),height()};
}
Damage RasterView::paint() {
#if defined(__linux__)
    if (impl_->executor && !impl_->executor->current())
        return impl_->executor->invoke([this] { return paint(); });
#endif
    for(;;) {
    impl_->stabilize_curves();
    const auto damage = impl_->damage;
    if (damage.empty()) return damage;
    if(impl_->gpu && !impl_->gpu->current()) {impl_->recover();continue;}
    struct Release {GpuDevice* device;~Release(){if(device)device->release();}} release{impl_->gpu.get()};
    auto* canvas = impl_->surface->getCanvas();
    canvas->save();
    canvas->clipIRect(SkIRect::MakeLTRB(damage.left, damage.top, damage.right, damage.bottom));
    canvas->drawColor(impl_->background, SkBlendMode::kSrc);
    if(!impl_->order.empty())for(size_t command_index=0;command_index<impl_->order.size();++command_index) {
        const auto& bounds=impl_->order_bounds.at(command_index);
        if(bounds.empty()||bounds.right<=damage.left||bounds.left>=damage.right||bounds.bottom<=damage.top||bounds.top>=damage.bottom)continue;
        const auto& command=impl_->order[command_index];
        if(command.kind==PaintCommand::box)paint_box(canvas,impl_->rectangles.at(command.index));
        else if(command.kind==PaintCommand::outline)impl_->outlines.at(command.index).shape->paint(canvas);
        else {
            const auto& item=impl_->texts.at(command.index);SkAutoCanvasRestore restore(canvas,true);apply_clip(canvas,item.clip);
            item.paragraph->paint_part(canvas,item.x,item.y,command.part,item.scenes.get());
        }
    }else {
    size_t text_index = 0;
    for (size_t index = 0; index <= impl_->rectangles.size(); ++index) {
        while (text_index < impl_->texts.size() && impl_->texts[text_index].paint_index <= index) {
            const auto& item = impl_->texts[text_index++];
            SkAutoCanvasRestore restore(canvas, true);
            apply_clip(canvas, item.clip);
            item.paragraph->paint(canvas, item.x, item.y,item.scenes.get());
        }
        if (index == impl_->rectangles.size()) break;
        paint_box(canvas, impl_->rectangles[index]);
    }
    for (const auto& outline : impl_->outlines) outline.shape->paint(canvas);
    }
    SkPaint overlay;
    for(const auto& rect:impl_->overlays) {
        overlay.setColor(rect.argb);
        canvas->drawRect(SkRect::MakeXYWH(rect.x,rect.y,rect.width,rect.height),overlay);
    }
    canvas->restore();
    if(impl_->gpu && !impl_->gpu->finish()) {
        impl_->gpu->release();release.device=nullptr;impl_->recover();continue;
    }
    impl_->pixels += uint64_t(damage.right - damage.left) * uint64_t(damage.bottom - damage.top);
    ++impl_->frames;
    impl_->damage = {};
    impl_->ready = true;
    return damage;
    }
}
bool RasterView::read_region(Damage region, void* data, size_t row_bytes) const {
#if defined(__linux__)
    if (impl_->executor && !impl_->executor->current())
        return impl_->executor->invoke([=, this] { return read_region(region, data, row_bytes); });
#endif
    if (!ready() || region.empty() || region.left < 0 || region.top < 0 || region.right > width() ||
        region.bottom > height())
        return false;
    const auto info = SkImageInfo::Make(region.right - region.left, region.bottom - region.top, kRGBA_8888_SkColorType,
                                        kUnpremul_SkAlphaType);
    for(;;) {
        bool success=false;
        if(!impl_->gpu || impl_->gpu->current()) {
            success=impl_->gpu ? impl_->gpu->read_pixels(impl_->surface.get(), info, data, row_bytes, region.left, region.top)
                              : impl_->surface->readPixels(info, data, row_bytes, region.left, region.top);
            if(impl_->gpu)impl_->gpu->release();
        }
        if(success)return true;
        if(!impl_->gpu)return false;
        // Readback can expose a loss after flush succeeded. Repaint the whole
        // retained scene on the next candidate before returning any pixels.
        impl_->recover();const_cast<RasterView*>(this)->paint();
    }
}
bool RasterView::copy_rgba(void* data, size_t capacity, size_t row_bytes) const {
    const auto minimum = size_t(width()) * 4;
    if (!data || row_bytes < minimum ||
        size_t(height() - 1) > (std::numeric_limits<size_t>::max() - minimum) / row_bytes ||
        capacity < size_t(height() - 1) * row_bytes + minimum)
        return false;
    return read_region({0, 0, width(), height()}, data, row_bytes);
}
} // namespace xl::detail
