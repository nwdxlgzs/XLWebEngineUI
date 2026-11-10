#include "paragraph.h"
#include "document/document.h"
#include "graphics/paint_scene.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphPainter.h"
#include "modules/skparagraph/include/TypefaceFontProvider.h"
// The locked Skia implementation exposes shaped cluster advances. Only this
// adapter reads them; CSS wrapping and public SDK types remain framework owned.
#include "modules/skparagraph/src/ParagraphImpl.h"
#include "modules/skunicode/include/SkUnicode_icu.h"
#ifdef _WIN32
#include "include/ports/SkTypeface_win.h"
#else
#include "include/ports/SkFontMgr_empty.h"
#endif
#include <deque>
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unicode/utf8.h>
#include <unicode/ustring.h>

namespace xl::detail {
using namespace skia::textlayout;
struct TextParagraph::Impl {
    std::shared_ptr<void> session;
    struct Line {
        struct Object {
            uint32_t offset;
            SkRect rect;
            std::shared_ptr<const TextObject> value;
            xl_node_id owner=0;
        };
        struct Blob {
            sk_sp<SkTextBlob> text;
            float x, y;
            SkPaint paint;
            std::optional<SkRect> clip;
            xl_node_id owner=0;
        };
        std::unique_ptr<Paragraph> paragraph;
        uint32_t start, end;
        float x, y, width, height, paint_shift = 0;
        std::vector<std::pair<uint32_t, SkRect>> tabs;
        std::vector<std::pair<uint32_t, SkRect>> edges;
        std::vector<uint32_t> suppressed;
        std::vector<BoxPaint> decorations;
        std::vector<Object> objects = {};
        std::vector<std::vector<Blob>> blobs = {};
        std::vector<size_t> decoration_sections = {};
        struct Offset { uint32_t start, end; float x, y; };
        std::vector<Offset> offsets = {};
        std::pair<float,float> shift(uint32_t offset, bool downstream=true) const {
            for(const auto& span:offsets)
                if(downstream ? offset>=span.start&&offset<span.end : offset>span.start&&offset<=span.end)return {span.x,span.y};
            if(!offsets.empty())return offset==0?std::pair{offsets.front().x,offsets.front().y}:std::pair{offsets.back().x,offsets.back().y};
            return {};
        }
    };
    std::vector<Line> lines;
    std::vector<TextParagraph::Part> parts;
    uint32_t utf16_length = 0;
    float height = 0, width = 0;
    SkRect ink = SkRect::MakeEmpty();
    bool curved = false;
    SkRect curve_ink = SkRect::MakeEmpty();
    std::vector<std::vector<xl_node_bounds>> inline_bounds;
    std::vector<std::vector<RoundedRect>> inline_shapes;
};
namespace {
struct PaintBinding { size_t section; uint32_t color; float x=0,y=0; xl_node_id owner=0; };
// SkParagraph supplies its shaped blobs and cluster clips. Store them by
// logical section so atomic content paints between the surrounding text,
// including when visual bidi order differs from DOM order.
class SectionPainter final : public ParagraphPainter {
  public:
    using Groups = std::vector<std::vector<TextParagraph::Impl::Line::Blob>>;
    SectionPainter(const std::vector<PaintBinding>& bindings, Groups& groups) : bindings_(bindings), groups_(groups) {}
    void drawTextBlob(const sk_sp<SkTextBlob>& blob, SkScalar x, SkScalar y, const SkPaintOrID& value) override {
        if(!blob)return;
        SkPaint paint;
        size_t section = 0;
        float dx=0,dy=0;
        xl_node_id owner=0;
        if (const auto* id = std::get_if<PaintID>(&value)) {
            const auto& binding = bindings_.at(static_cast<size_t>(*id));
            section = binding.section;
            paint.setColor(binding.color);
            dx=binding.x;dy=binding.y;
            owner=binding.owner;
        } else paint = std::get<SkPaint>(value);
        auto clip=state_.clip;if(clip)clip->offset(dx,dy);
        groups_.at(section).push_back({blob, x + state_.x+dx, y + state_.y+dy, paint, clip,owner});
    }
    void clipRect(const SkRect& rect) override {
        auto next = rect.makeOffset(state_.x, state_.y);
        if (state_.clip && !next.intersect(*state_.clip)) next = SkRect::MakeEmpty();
        state_.clip = next;
    }
    void translate(SkScalar x, SkScalar y) override { state_.x += x; state_.y += y; }
    void save() override { stack_.push_back(state_); }
    void restore() override { state_ = stack_.back(); stack_.pop_back(); }
    // Current TextStyle contains only foreground/font attributes; backgrounds
    // and borders belong to framework boxes, not SkParagraph styles.
    void drawTextShadow(const sk_sp<SkTextBlob>&, SkScalar, SkScalar, SkColor, SkScalar) override { fail(); }
    void drawRect(const SkRect&, const SkPaintOrID&) override { fail(); }
    void drawFilledRect(const SkRect&, const DecorationStyle&) override { fail(); }
    void drawPath(const SkPath&, const DecorationStyle&) override { fail(); }
    void drawLine(SkScalar, SkScalar, SkScalar, SkScalar, const DecorationStyle&) override { fail(); }
  private:
    [[noreturn]] static void fail() { throw DocumentFailure{XL_INTERNAL_ERROR}; }
    struct State { float x = 0, y = 0; std::optional<SkRect> clip; } state_;
    std::vector<State> stack_;
    const std::vector<PaintBinding>& bindings_;
    Groups& groups_;
};
}
struct TextEngine::Impl {
    sk_sp<SkFontMgr> loader;
    sk_sp<TypefaceFontProvider> assets = sk_make_sp<TypefaceFontProvider>();
    sk_sp<FontCollection> fonts = sk_make_sp<FontCollection>();
    std::vector<SkString> families;
    struct Cached {
        std::vector<TextRun> runs;
        std::vector<TextInlineBox> boxes;
        float width;
        bool rtl;
        uint32_t align;
        TextFormat base;
        size_t bytes;
        std::weak_ptr<TextParagraph> paragraph;
    };
    std::deque<Cached> cache;
    size_t cache_bytes = 0;
    uint64_t font_revision = 1;
    Impl() {
#ifdef _WIN32
        loader = SkFontMgr_New_DirectWrite();
        fonts->setDefaultFontManager(loader);
#else
        loader = SkFontMgr_New_Custom_Empty();
#endif
        fonts->setAssetFontManager(assets);
        fonts->getParagraphCache()->turnOn(false);
    }
};
TextParagraph::TextParagraph(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
TextParagraph::~TextParagraph() = default;
sk_sp<SkFontMgr> TextEngine::svg_fonts() const {return impl_->assets->countFamilies()?sk_ref_sp<SkFontMgr>(impl_->assets.get()):impl_->loader;}
uint64_t TextEngine::font_revision() const {return impl_->font_revision;}
const std::vector<TextParagraph::Part>& TextParagraph::parts() const{return impl_->parts;}
void TextParagraph::paint_part(SkCanvas* canvas,float x,float y,size_t index,const TextScenes* scenes) const {
    const auto& part=impl_->parts.at(index);const auto& line=impl_->lines.at(part.line);
    SkAutoCanvasRestore restore(canvas,true);canvas->translate(x,y);
    if(part.kind==Part::decoration)paint_box(canvas,line.decorations.at(part.index));
    else if(part.kind==Part::glyph) {
        const auto& blob=line.blobs.at(part.section).at(part.index);
        if(blob.clip)canvas->clipRect(*blob.clip);
        canvas->drawTextBlob(blob.text,blob.x,blob.y,blob.paint);
    }else {
        const auto& object=line.objects.at(part.index);
        if(object.value->flattened)return;
        auto scene=object.value->scene;
        if(scenes)if(const auto found=scenes->find(object.value.get());found!=scenes->end())scene=found->second;
        if(scene)scene->paint(canvas,line.x+object.rect.x(),line.y+line.paint_shift+object.rect.y());
    }
}
TextFontMetrics TextEngine::font_metrics(const TextFormat& format) const {
    std::vector<SkString> families;
    for (const auto& family : format.families) families.emplace_back(family.c_str());
    families.insert(families.end(), impl_->families.begin(), impl_->families.end());
    const auto slant = format.oblique ? SkFontStyle::kOblique_Slant
                      : format.italic ? SkFontStyle::kItalic_Slant : SkFontStyle::kUpright_Slant;
    const auto typefaces = impl_->fonts->findTypefaces(families, SkFontStyle(format.weight, format.stretch, slant), std::nullopt);
    if (typefaces.empty()) throw DocumentFailure{XL_NOT_FOUND};
    SkFontMetrics metrics{};
    SkFont(typefaces.front(), format.size).getMetrics(&metrics);
    return {-metrics.fAscent, metrics.fDescent, metrics.fLeading, std::abs(metrics.fXHeight)};
}
float TextEngine::normal_line_height(const TextFormat& format) const {
    const auto metrics=font_metrics(format);
    return std::max(0.0f, metrics.ascent+metrics.descent+metrics.leading);
}
float TextParagraph::height() const {
    return impl_->height;
}
float TextParagraph::width() const {
    return impl_->width;
}
float TextParagraph::last_baseline() const {
    if (impl_->lines.empty()) return 0;
    const auto& line = impl_->lines.back();
    return line.y + line.paint_shift + line.paragraph->getAlphabeticBaseline();
}
float TextParagraph::first_baseline() const {
    if (impl_->lines.empty()) return 0;
    const auto& line = impl_->lines.front();
    return line.y + line.paint_shift + line.paragraph->getAlphabeticBaseline();
}
std::vector<std::pair<uint32_t, xl_node_bounds>> TextParagraph::objects() const {
    std::vector<std::pair<uint32_t, xl_node_bounds>> result;
    for (const auto& line : impl_->lines)
        for (const auto& object : line.objects)
            result.push_back({line.start + object.offset,
                              {line.x + object.rect.x(), line.y + line.paint_shift + object.rect.y(),
                               object.rect.width(), object.rect.height()}});
    return result;
}
xl_node_bounds TextParagraph::ink_bounds() const {
    return {impl_->ink.x(), impl_->ink.y(), impl_->ink.width(), impl_->ink.height()};
}
void TextParagraph::paint(SkCanvas* canvas, float x, float y,const TextScenes* scenes) const {
    canvas->save();
    canvas->translate(x, y);
    for (const auto& line : impl_->lines) {
        if (line.blobs.empty()) {
            for (const auto& box : line.decorations) paint_box(canvas, box);
            line.paragraph->paint(canvas, line.x, line.y + line.paint_shift);
        }
        else for (size_t section = 0; section < line.blobs.size(); ++section) {
            for (size_t i = 0; i < line.decorations.size(); ++i)
                if (line.decoration_sections[i] == section) paint_box(canvas, line.decorations[i]);
            for (const auto& blob : line.blobs[section]) {
                canvas->save();
                if (blob.clip) canvas->clipRect(*blob.clip);
                canvas->drawTextBlob(blob.text, blob.x, blob.y, blob.paint);
                canvas->restore();
            }
            if (section < line.objects.size()) {
                const auto& object = line.objects[section];
                auto scene=object.value->scene;
                if(scenes)if(const auto found=scenes->find(object.value.get());found!=scenes->end())scene=found->second;
                if (scene)
                    scene->paint(canvas, line.x + object.rect.x(),
                                               line.y + line.paint_shift + object.rect.y());
            }
        }
    }
    canvas->restore();
}
std::shared_ptr<const TextScenes> TextParagraph::scroll_scenes(const TextScrollOffsets& offsets,std::shared_ptr<const TextScenes> previous) const {
    std::shared_ptr<TextScenes> result;
    for(const auto& line:impl_->lines)for(const auto& object:line.objects)if(object.value->scene) {
        auto source=object.value->scene;if(previous)if(const auto found=previous->find(object.value.get());found!=previous->end())source=found->second;
        if(auto scene=source->project(offsets)) {
            if(!result)result=previous?std::make_shared<TextScenes>(*previous):std::make_shared<TextScenes>();
            (*result)[object.value.get()]=std::move(scene);
        }
    }
    return result?result:previous;
}
std::shared_ptr<const TextScenes> TextParagraph::image_scenes(const ImageUpdates& images,std::shared_ptr<const TextScenes> previous)const {
    std::shared_ptr<TextScenes> result;
    for(const auto& line:impl_->lines)for(const auto& object:line.objects)if(object.value->scene){
        auto source=object.value->scene;if(previous)if(const auto found=previous->find(object.value.get());found!=previous->end())source=found->second;
        if(auto scene=source->images(images)){if(!result)result=previous?std::make_shared<TextScenes>(*previous):std::make_shared<TextScenes>();(*result)[object.value.get()]=std::move(scene);}
    }
    return result?result:previous;
}
bool TextParagraph::curved() const {return impl_->curved;}
xl_node_bounds TextParagraph::curve_bounds() const {const auto& r=impl_->curve_ink;return {r.x(),r.y(),r.width(),r.height()};}
std::vector<xl_node_bounds> TextParagraph::inline_ranges(size_t index) const {
    return impl_->inline_bounds.at(index);
}
const std::vector<RoundedRect>& TextParagraph::inline_shapes(size_t index) const {return impl_->inline_shapes.at(index);}
std::vector<xl_node_bounds> TextParagraph::ranges(uint32_t start, uint32_t end) const {
    if (start > end || end > impl_->utf16_length) throw DocumentFailure{XL_INVALID_ARGUMENT};
    std::vector<xl_node_bounds> result;
    if (start == end) return result;
    for (const auto& line : impl_->lines) {
        if (end <= line.start || start >= line.end) continue;
        uint32_t begin = std::max(start, line.start) - line.start;
        const auto finish = std::min(end, line.end) - line.start;
        const auto add = [&](uint32_t first, uint32_t last) {
            const auto segment=[&](uint32_t a,uint32_t b,float dx,float dy) {
                if(a>=b)return;
                for(const auto& box:line.paragraph->getRectsForRange(a,b,RectHeightStyle::kTight,RectWidthStyle::kTight))
                    result.push_back({box.rect.x()+line.x+dx,box.rect.y()+line.y+line.paint_shift+dy,box.rect.width(),box.rect.height()});
            };
            if(line.offsets.empty())segment(first,last,0,0);
            else for(const auto& span:line.offsets)segment(std::max(first,span.start),std::min(last,span.end),span.x,span.y);
        };
        std::vector<std::pair<uint32_t, const SkRect*>> special;
        for (const auto& [offset, rectangle] : line.tabs)
            special.emplace_back(offset, &rectangle);
        for (const auto& [offset, rectangle] : line.edges)
            special.emplace_back(offset, nullptr);
        for (const auto& object : line.objects)
            special.emplace_back(object.offset, nullptr);
        for (const auto offset : line.suppressed)
            special.emplace_back(offset, nullptr);
        std::sort(special.begin(), special.end(), [](auto a, auto b) { return a.first < b.first; });
        for (const auto& [offset, rectangle] : special) {
            if (offset < begin || offset >= finish) continue;
            add(begin, offset);
            if (rectangle) {const auto [dx,dy]=line.shift(offset);result.push_back({rectangle->x()+line.x+dx,line.y+dy,rectangle->width(),line.height});}
            begin = offset + 1;
        }
        add(begin, finish);
    }
    return result;
}
std::pair<uint32_t, bool> TextParagraph::hit(float x, float y) const {
    if(std::any_of(impl_->lines.begin(),impl_->lines.end(),[](const auto& line){return !line.offsets.empty();})) {
        double best=std::numeric_limits<double>::infinity();
        std::pair<uint32_t,bool> result{0,true};
        for(const auto& line:impl_->lines) {
            const auto consider=[&](uint32_t start,uint32_t end,float dx,float dy) {
                for(const auto& box:line.paragraph->getRectsForRange(start,end,RectHeightStyle::kTight,RectWidthStyle::kTight)) {
                    const auto rect=box.rect.makeOffset(line.x+dx,line.y+line.paint_shift+dy);
                    const double hx=x-std::clamp(x,rect.left(),rect.right()),hy=y-std::clamp(y,rect.top(),rect.bottom());
                    const auto distance=hx*hx+hy*hy;
                    if(distance>best)continue;
                    const auto hit=line.paragraph->getGlyphPositionAtCoordinate(x-line.x-dx,y-line.y-line.paint_shift-dy);
                    result={line.start+std::clamp(uint32_t(std::max(0,hit.position)),start,end),hit.affinity==kDownstream};best=distance;
                }
            };
            if(line.offsets.empty())consider(0,line.end-line.start,0,0);
            else for(const auto& span:line.offsets)consider(span.start,span.end,span.x,span.y);
        }
        if(std::isfinite(best))return result;
    }
    const Impl::Line* nearest = nullptr;
    double best = std::numeric_limits<double>::infinity();
    for (const auto& line : impl_->lines) {
        const double dx = x - std::clamp(x, line.x, line.x + std::max(0.0f, line.width));
        const double dy = y - std::clamp(y, line.y, line.y + line.height);
        if (const auto distance = dx * dx + dy * dy; distance < best) {
            best = distance;
            nearest = &line;
        }
    }
    if (!nearest) return {0, true};
    const auto hit =
        nearest->paragraph->getGlyphPositionAtCoordinate(x - nearest->x, y - nearest->y - nearest->paint_shift);
    return {std::min(nearest->start + static_cast<uint32_t>(std::max(0, hit.position)), nearest->end),
            hit.affinity == kDownstream};
}
xl_node_bounds TextParagraph::caret(uint32_t offset, bool downstream) const {
    if(offset>impl_->utf16_length)throw DocumentFailure{XL_INVALID_ARGUMENT};
    const Impl::Line* chosen=nullptr;
    for(const auto& line:impl_->lines) {
        if(offset<line.start) {if(!chosen || downstream)chosen=&line;break;}
        chosen=&line;
        if(offset<line.end || (offset==line.end && !downstream))break;
    }
    if(!chosen)throw DocumentFailure{XL_NOT_FOUND};
    const auto local=std::clamp(offset,chosen->start,chosen->end)-chosen->start;
    const auto length=chosen->end-chosen->start;
    float x=0;
    bool special=false;
    for(const auto& [index,rect]:chosen->tabs)
        if(local==index || (local==index+1 && !downstream)) {
            x=local==index?rect.left():rect.right();special=true;break;
        }
    Paragraph::GlyphInfo glyph{};
    const auto index=local && (!downstream || local==length)?local-1:local;
    if(!special && length && chosen->paragraph->getGlyphInfoAtUTF16Offset(std::min(index,length-1),&glyph)) {
        const auto rect=glyph.fGraphemeLayoutBounds;
        const bool after=local>=glyph.fGraphemeClusterTextRange.end ||
            (local>glyph.fGraphemeClusterTextRange.start && !downstream);
        x=(after==(glyph.fDirection==TextDirection::kLtr))?rect.right():rect.left();
    }
    const auto [dx,dy]=chosen->shift(local,downstream&&local<length);
    return {chosen->x+x+dx,chosen->y+dy,1,std::max(1.0f,chosen->height)};
}
TextEngine::TextEngine() : impl_(std::make_unique<Impl>()) {}
TextEngine::~TextEngine() = default;
void TextEngine::add_font(const char* family, const void* bytes, size_t size) {
    if (!impl_->loader) throw DocumentFailure{XL_UNSUPPORTED};
    auto data = SkData::MakeWithCopy(bytes, size);
    if (!data) throw std::bad_alloc();
    auto typeface = impl_->loader->makeFromData(std::move(data));
    if (!typeface) throw DocumentFailure{XL_INVALID_ARGUMENT};
    impl_->assets->registerTypeface(typeface, SkString(family));
    ++impl_->font_revision;
    impl_->families.insert(impl_->families.begin(), SkString(family));
    impl_->fonts->clearCaches();
    impl_->cache.clear();
    impl_->cache_bytes = 0;
}
std::shared_ptr<TextParagraph> TextEngine::shape(const std::vector<TextRun>& runs,
                                                 const std::vector<TextInlineBox>& boxes, float width, bool rtl,
                                                 uint32_t align, const TextFormat& base,
                                                 std::shared_ptr<void> session) {
    if (!session) throw DocumentFailure{XL_BUSY};
    for (auto entry = impl_->cache.begin(); entry != impl_->cache.end();) {
        // Atomic keys own a scene; discard expired parent entries immediately
        // so the weak paragraph cache cannot retain old child scenes.
        if (entry->paragraph.expired()) {
            impl_->cache_bytes -= entry->bytes;
            entry = impl_->cache.erase(entry);
        } else ++entry;
    }
    for (auto& entry : impl_->cache)
        if (entry.runs == runs && entry.boxes == boxes && entry.width == width && entry.rtl == rtl &&
            entry.align == align && entry.base == base)
            if (auto paragraph = entry.paragraph.lock()) return paragraph;
#ifndef _WIN32
    if (impl_->families.empty()) throw DocumentFailure{XL_NOT_FOUND};
#endif
    const auto make_style = [&](const TextFormat& format) {
        TextStyle style;
        style.setFontSize(format.size);
        style.setColor(format.color);
        style.setBaselineShift(format.baseline_shift);
        style.setFontStyle(SkFontStyle(format.weight, format.stretch,
                                       format.oblique  ? SkFontStyle::kOblique_Slant
                                       : format.italic ? SkFontStyle::kItalic_Slant
                                                       : SkFontStyle::kUpright_Slant));
        std::vector<SkString> families;
        for (const auto& family : format.families)
            families.emplace_back(family.c_str());
        families.insert(families.end(), impl_->families.begin(), impl_->families.end());
        style.setFontFamilies(std::move(families));
        if (format.line_height >= 0 && format.size > 0) {
            style.setHeight(format.line_height / format.size);
            style.setHeightOverride(true);
            style.setHalfLeading(true);
        }
        return style;
    };
    ParagraphStyle paragraph_style;
    const auto default_style = make_style(base);
    paragraph_style.setTextStyle(default_style);
    paragraph_style.setTextDirection(rtl ? TextDirection::kRtl : TextDirection::kLtr);
    paragraph_style.setTextAlign(TextAlign::kLeft);
    paragraph_style.setFakeMissingFontStyles(true);
    paragraph_style.setApplyRoundingHack(false);
    StrutStyle strut;
    strut.setStrutEnabled(true);
    strut.setFontFamilies(default_style.getFontFamilies());
    strut.setFontStyle(default_style.getFontStyle());
    strut.setFontSize(base.size);
    if (base.line_height >= 0 && base.size > 0) {
        strut.setHeight(base.line_height / base.size);
        strut.setHeightOverride(true);
        strut.setHalfLeading(true);
    }
    paragraph_style.setStrutStyle(strut);
    const auto builder = [&] {
        auto unicode = SkUnicodes::ICU::Make();
        if (!unicode) throw DocumentFailure{XL_INTERNAL_ERROR};
        auto result = ParagraphBuilder::make(paragraph_style, impl_->fonts, std::move(unicode));
        if (!result) throw std::bad_alloc();
        return result;
    };
    const auto font_metrics = [&](const TextFormat& format) {
        SkFontMetrics metrics{};
        const auto style = make_style(format);
        const auto typefaces = impl_->fonts->findTypefaces(style.getFontFamilies(), style.getFontStyle(), std::nullopt);
        if (!typefaces.empty()) SkFont(typefaces.front(), format.size).getMetrics(&metrics);
        return metrics;
    };
    const auto edge_style = [&](const TextRun& run, const TextFormat& format) {
        const auto metrics = font_metrics(format);
        const auto height = format.line_height < 0 ? -metrics.fAscent + metrics.fDescent + metrics.fLeading
                                                   : format.line_height;
        const auto ascent = -metrics.fAscent + (height + metrics.fAscent - metrics.fDescent) * .5f;
        return PlaceholderStyle(run.edge_width, height, PlaceholderAlignment::kBaseline, TextBaseline::kAlphabetic,
                                ascent - format.baseline_shift);
    };
    struct Unit {
        uint32_t start, end;
        size_t byte;
        UChar32 scalar;
        const TextFormat* format;
        const TextRun* run;
        float advance = 0, space = 0;
        bool soft = false;
    };
    std::vector<Unit> units;
    std::u16string original;
    size_t cache_bytes = sizeof(TextRun) * runs.size() + sizeof(TextInlineBox) * boxes.size();
    auto measure_builder = builder();
    size_t byte_offset = 0;
    for (const auto& run : runs) {
        if (run.text.size() > size_t(std::numeric_limits<int32_t>::max()) - byte_offset)
            throw DocumentFailure{XL_UNSUPPORTED};
        auto utf16 = SkUnicode::convertUtf8ToUtf16(run.text.data(), static_cast<int>(run.text.size()));
        auto measurement = utf16;
        std::replace(measurement.begin(), measurement.end(), u'\t', u' ');
        measure_builder->pushStyle(make_style(run.format));
        if (run.object)
            measure_builder->addPlaceholder(PlaceholderStyle(run.object->width, run.object->height,
                PlaceholderAlignment::kBaseline, TextBaseline::kAlphabetic, run.object->baseline - run.format.baseline_shift));
        else if (run.edge)
            measure_builder->addPlaceholder(edge_style(run, run.format));
        else
            measure_builder->addText(measurement);
        measure_builder->pop();
        uint32_t position = static_cast<uint32_t>(original.size());
        for (int32_t offset = 0, count = static_cast<int32_t>(run.text.size()); offset < count;) {
            const auto begin = offset;
            UChar32 scalar;
            U8_NEXT(run.text.data(), offset, count, scalar);
            if (scalar < 0) throw DocumentFailure{XL_INVALID_ARGUMENT};
            const auto end = position + (scalar > 0xffff ? 2u : 1u);
            units.push_back({position, end, byte_offset + static_cast<size_t>(begin), scalar, &run.format, &run});
            position = end;
        }
        original += utf16;
        byte_offset += run.text.size();
        cache_bytes += run.text.size();
        for (const auto& family : run.format.families)
            cache_bytes += sizeof(std::string) + family.size();
    }
    auto measurement = measure_builder->Build();
    if (!measurement) throw std::bad_alloc();
    measurement->layout(std::numeric_limits<float>::infinity());
    // Keep missing-glyph boxes and the original text/AX ranges when a host
    // font lacks coverage. A single missing glyph must not discard the page.
    auto* measured = static_cast<ParagraphImpl*>(measurement.get());
    // Clusters are ordered by logical UTF-8 range, including RTL runs. The
    // sentinel at the text end has no source unit and is skipped.
    size_t unit = 0;
    for (auto& cluster : measured->clusters()) {
        while (unit < units.size() && units[unit].byte < cluster.textRange().start)
            ++unit;
        if (unit == units.size()) break;
        if (units[unit].byte == cluster.textRange().start) units[unit].advance += cluster.width();
        auto end = unit;
        while (end + 1 < units.size() && units[end + 1].byte < cluster.textRange().end)
            ++end;
        if (end < units.size() && cluster.canBreakLineAfter()) units[end].soft = true;
    }
    for (auto& item : units)
        if (item.scalar == '\t') {
            const auto font = measurement->getFontAtUTF16Offset(item.start);
            item.space = font.measureText(" ", 1, SkTextEncoding::kUTF8);
        }
    // Synthetic edges must not introduce U+FFFC line breaks inside words. Run
    // Unicode breaking over DOM text, then attach closing edges to that word.
    if (std::any_of(runs.begin(), runs.end(), [](const auto& run) { return run.edge != 0; })) {
        std::u16string logical;
        std::vector<uint32_t> ends;
        for (auto& item : units) {
            item.soft = false;
            if (!item.run->edge) logical.append(original, item.start, item.end - item.start);
            ends.push_back(static_cast<uint32_t>(logical.size()));
        }
        auto unicode = SkUnicodes::ICU::Make();
        auto iterator = unicode->makeBreakIterator("en", SkUnicode::BreakType::kLines);
        if (!iterator || !iterator->setText(logical.data(), logical.size())) throw DocumentFailure{XL_INTERNAL_ERROR};
        for (auto boundary = iterator->first(); !iterator->isDone(); boundary = iterator->next()) {
            auto found = std::lower_bound(ends.begin(), ends.end(), uint32_t(boundary));
            for (size_t i = size_t(found - ends.begin()); i < units.size() && ends[i] == uint32_t(boundary); ++i)
                if (!units[i].run->edge) {
                    auto last = i;
                    while (last + 1 < units.size() && units[last + 1].run->edge < 0)
                        ++last;
                    units[last].soft = true;
                    break;
                }
        }
    }
    const auto tab_width = [](const Unit& item, float x) {
        const auto stop = item.format->tab_is_length ? item.format->tab_size : item.format->tab_size * item.space;
        if (stop <= 0 || !std::isfinite(stop)) return 0.0f;
        auto advance = stop - std::fmod(x, stop);
        // CSS requires at least half a space before the next tab stop.
        if (advance < item.space * .5f) advance += stop;
        return advance;
    };
    const auto collapsible = [](const Unit& item) {
        return item.scalar == ' ' && collapses_spaces(item.format->whitespace);
    };
    const auto hanging = [](const Unit& item) {
        return (item.scalar == ' ' || item.scalar == '\t') && item.format->whitespace == WhiteSpace::pre_wrap;
    };
    const auto break_after = [&](size_t index) {
        const auto& item = units[index];
        if (!wraps_text(item.format->whitespace)) return false;
        if (index + 1 < units.size() && !wraps_text(units[index + 1].format->whitespace) &&
            item.format->whitespace == units[index + 1].format->whitespace)
            return false;
        return item.soft ||
               ((item.scalar == ' ' || item.scalar == '\t') && item.format->whitespace == WhiteSpace::break_spaces);
    };
    auto value = std::make_unique<TextParagraph::Impl>();
    value->session = std::move(session);
    value->utf16_length = static_cast<uint32_t>(original.size());
    value->inline_bounds.resize(boxes.size());
    value->inline_shapes.resize(boxes.size());
    size_t first = 0;
    while (first < units.size() || (units.empty() && value->lines.empty())) {
        size_t cursor = first, last_break = first;
        float advance = 0;
        while (cursor < units.size() && units[cursor].scalar != '\n') {
            const auto& item = units[cursor];
            const auto added = item.run->object      ? item.run->object->width
                               : item.run->edge        ? item.run->edge_width
                               : item.scalar == '\t' ? tab_width(item, advance)
                                                     : item.advance;
            if (advance + added > width + .01f && last_break > first && !hanging(item) && !collapsible(item)) {
                cursor = last_break;
                break;
            }
            advance += added;
            if (break_after(cursor)) last_break = cursor + 1;
            ++cursor;
            if (advance > width + .01f && last_break == cursor && !hanging(item)) break;
        }
        size_t terminal_break = units.size();
        if (cursor + 1 < units.size() && units[cursor].scalar == '\n' &&
            std::all_of(units.begin() + cursor + 1, units.end(), [](const Unit& item) { return item.run->edge < 0; })) {
            // A trailing br does not create another line solely to paint the
            // element's closing decoration; that edge belongs to the last line.
            terminal_break = cursor;
            cursor = units.size();
        }
        const auto consumed = cursor;
        size_t visible_end = cursor;
        while (visible_end > first && collapsible(units[visible_end - 1]))
            --visible_end;
        std::vector<bool> suppressed(visible_end - first, false);
        if (terminal_break < visible_end) suppressed[terminal_break - first] = true;
        for (auto index = visible_end; index > first;) {
            const auto& item = units[--index];
            if (item.run->edge) continue;
            if (!collapsible(item)) break;
            suppressed[index - first] = true;
        }
        for (auto index = first; index < visible_end; ++index) {
            const auto& item = units[index];
            if (item.run->edge) continue;
            if (!collapsible(item)) break;
            suppressed[index - first] = true;
        }
        const uint32_t start = first < units.size() ? units[first].start : 0;
        const uint32_t finish = visible_end > first ? units[visible_end - 1].end : start;
        const bool hard_break = consumed < units.size() && units[consumed].scalar == '\n';
        const auto metric_finish = hard_break ? units[consumed].end : finish;
        const auto needs_metrics = [](const Unit& item) {
            return item.run->object || item.run->edge || item.format->line_height == 0 || item.format->baseline_shift != 0 ||
                   item.format->alignment_group != 0;
        };
        const auto break_metrics = [&] {
            if (!hard_break) return false;
            const auto& format = *units[consumed].format;
            return needs_metrics(units[consumed]) || format.line_height != base.line_height ||
                   format.size != base.size || format.families != base.families || format.weight != base.weight ||
                   format.italic != base.italic || format.oblique != base.oblique || format.stretch != base.stretch;
        };
        const bool custom_metrics = base.line_height == 0 || break_metrics() ||
            std::any_of(units.begin() + first, units.begin() + visible_end, needs_metrics);
        struct Extents {
            float ascent = -std::numeric_limits<float>::infinity(), descent = ascent;
            uint8_t alignment = 0;
            float ancestor_shift = 0, shift = 0;
            std::vector<uint64_t> children;
        };
        std::unordered_map<uint64_t, Extents> groups;
        float line_ascent = 0, line_descent = 0;
        if (custom_metrics) {
            // Each top/bottom subtree has its own baseline. Resolve nested
            // groups before placing their containing group on this line.
            const auto metrics = [&](const SkFontMetrics& value, const TextFormat& format) {
                const auto natural_ascent = -value.fAscent + value.fLeading * .5f;
                const auto natural_descent = value.fDescent + value.fLeading * .5f;
                const auto leading =
                    format.line_height < 0 ? 0.0f : (format.line_height - natural_ascent - natural_descent) * .5f;
                auto& group = groups[format.alignment_group];
                group.ascent = std::max(group.ascent, natural_ascent + leading - format.baseline_shift);
                group.descent = std::max(group.descent, natural_descent + leading + format.baseline_shift);
            };
            metrics(font_metrics(base), base);
            for (auto index = first; index < visible_end; ++index) {
                const auto& item = units[index];
                if (suppressed[index - first] && item.scalar != '\n') continue;
                if (item.run->object) {
                    const auto& object = *item.run->object;
                    auto& group = groups[item.format->alignment_group];
                    group.ascent = std::max(group.ascent, object.baseline - item.format->baseline_shift);
                    group.descent = std::max(group.descent, object.height - object.baseline + item.format->baseline_shift);
                    continue;
                }
                SkFontMetrics font;
                if (item.run->edge || item.scalar == '\n')
                    font = font_metrics(*item.format);
                else
                    measurement->getFontAtUTF16Offset(item.start).getMetrics(&font);
                metrics(font, *item.format);
            }
            // A forced break has no glyph, but its style and enclosing struts
            // still determine the height of an otherwise empty line.
            if (hard_break) metrics(font_metrics(*units[consumed].format), *units[consumed].format);
            // An ancestor's strut remains even if all its text is in shifted
            // descendants. Vertical padding and borders are decoration only.
            for (const auto& box : boxes)
                if (box.end > start && box.start < metric_finish) {
                    if (!box.atomic) metrics(font_metrics(box.format), box.format);
                    if (box.line_alignment) {
                        auto& group = groups[box.format.alignment_group];
                        group.alignment = box.line_alignment;
                        group.ancestor_shift = box.alignment_offset;
                        groups[box.alignment_parent].children.push_back(box.format.alignment_group);
                    }
                }
            const auto resolve = [&](auto&& self, uint64_t id) -> void {
                auto& group = groups.at(id);
                if (!std::isfinite(group.ascent)) group.ascent = group.descent = 0;
                const auto ascent = group.ascent, descent = group.descent;
                auto height = ascent + descent;
                for (const auto child_id : group.children) {
                    self(self, child_id);
                    const auto& child = groups.at(child_id);
                    const auto child_height = child.ascent + child.descent;
                    if (child_height <= height) continue;
                    height = child_height;
                    // Preserve the original edge of the tallest aligned child;
                    // equal-height children preserve DOM order. CSS leaves the
                    // baseline ambiguous; this follows browser placement.
                    group.ascent = child.alignment == 1 ? ascent : height - descent;
                    group.descent = height - group.ascent;
                }
            };
            resolve(resolve, 0);
            const auto place = [&](auto&& self, uint64_t id) -> void {
                const auto& group = groups.at(id);
                for (const auto child_id : group.children) {
                    auto& child = groups.at(child_id);
                    child.shift = group.shift + child.ancestor_shift +
                                  (child.alignment == 1 ? child.ascent - group.ascent : group.descent - child.descent);
                    self(self, child_id);
                }
            };
            place(place, 0);
            line_ascent = groups.at(0).ascent;
            line_descent = groups.at(0).descent;
        }
        const auto resolved_format = [&](const TextFormat& original) {
            auto format = original;
            if (format.alignment_group) {
                const auto& group = groups.at(format.alignment_group);
                format.baseline_shift += group.shift;
            }
            return format;
        };
        auto line_builder = builder();
        std::vector<uint32_t> tabs;
        std::vector<int> edges;
        std::vector<std::shared_ptr<const TextObject>> objects;
        std::vector<xl_node_id> object_owners;
        const bool has_objects = std::any_of(units.begin() + first, units.begin() + visible_end,
                                             [](const auto& item) { return bool(item.run->object); });
        const bool has_visual = std::any_of(units.begin()+first,units.begin()+visible_end,
            [](const auto& item){return item.format->visual_x||item.format->visual_y;});
        const bool has_owners=std::any_of(units.begin()+first,units.begin()+visible_end,[](const auto& item){return item.run->owner!=0;});
        std::vector<TextParagraph::Impl::Line::Offset> offsets;
        std::vector<PaintBinding> bindings;
        size_t section = 0;
        float x = 0, hanging_width = 0;
        for (size_t index = first; index < visible_end;) {
            const auto& item = units[index];
            const auto* format = item.format;
            const auto resolved = resolved_format(*format);
            auto style = make_style(resolved);
            if (has_objects||has_visual||has_owners) {
                const auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto& binding) {
                    return binding.section == section && binding.color == resolved.color && binding.x==resolved.visual_x && binding.y==resolved.visual_y&&binding.owner==item.run->owner;
                });
                const auto id = size_t(found - bindings.begin());
                if (id >= size_t(std::numeric_limits<int>::max())) throw DocumentFailure{XL_UNSUPPORTED};
                if (found == bindings.end()) bindings.push_back({section, resolved.color,resolved.visual_x,resolved.visual_y,item.run->owner});
                style.setForegroundPaintID(static_cast<int>(id));
            }
            line_builder->pushStyle(style);
            if (suppressed[index - first]) {
                line_builder->addPlaceholder(
                    PlaceholderStyle(0, 0, PlaceholderAlignment::kBaseline, TextBaseline::kAlphabetic, 0));
                tabs.push_back(item.start - start);
                edges.push_back(2);
                ++index;
            } else if (item.run->object) {
                const auto& object = item.run->object;
                line_builder->addPlaceholder(PlaceholderStyle(object->width, object->height,
                    PlaceholderAlignment::kBaseline, TextBaseline::kAlphabetic, object->baseline - resolved.baseline_shift));
                tabs.push_back(item.start - start);
                edges.push_back(3);
                objects.push_back(object);
                object_owners.push_back(item.run->owner);
                x += object->width;
                hanging_width = 0;
                ++index;
                ++section;
            } else if (item.run->edge) {
                line_builder->addPlaceholder(edge_style(*item.run, resolved));
                tabs.push_back(item.start - start);
                edges.push_back(true);
                x += item.run->edge_width;
                ++index;
            } else if (item.scalar == '\t') {
                const auto width = tab_width(item, x);
                line_builder->addPlaceholder(
                    PlaceholderStyle(width, 0, PlaceholderAlignment::kBaseline, TextBaseline::kAlphabetic, 0));
                tabs.push_back(item.start - start);
                edges.push_back(false);
                x += width;
                hanging_width = hanging(item) ? hanging_width + width : 0;
                ++index;
            } else {
                auto end = index + 1;
                while (end < visible_end && !suppressed[end - first] && !units[end].run->edge && !units[end].run->object &&
                       units[end].scalar != '\t' && units[end].format == format)
                    ++end;
                const auto last = units[end - 1].end;
                line_builder->addText(original.substr(item.start, last - item.start));
                for (; index < end; ++index) {
                    x += units[index].advance;
                    hanging_width = hanging(units[index]) ? hanging_width + units[index].advance : 0;
                }
            }
            line_builder->pop();
            if(has_visual) {
                const auto a=item.start-start,b=units[index-1].end-start;
                if(!offsets.empty()&&offsets.back().end==a&&offsets.back().x==resolved.visual_x&&offsets.back().y==resolved.visual_y)offsets.back().end=b;
                else offsets.push_back({a,b,resolved.visual_x,resolved.visual_y});
            }
        }
        auto paragraph = line_builder->Build();
        if (!paragraph) throw std::bad_alloc();
        paragraph->layout(std::numeric_limits<float>::infinity());
        // The single-run fast path does not populate Skia's private
        // widthWithTrailingSpaces field. This unwrapped line's intrinsic
        // width includes the preserved spaces and placeholders in both paths.
        const auto used = paragraph->getMaxIntrinsicWidth();
        const auto actual_width = std::max(0.0f, used - hanging_width);
        const bool right = align == 2 || (align == 0 && rtl) || (align == 4 && !rtl);
        const auto offset = align == 3 ? (width - actual_width) * .5f : right ? width - actual_width : 0;
        TextParagraph::Impl::Line line{
            std::move(paragraph), start, finish, offset, value->height, used, 0, 0, {}, {}, {}, {}};
        line.offsets=std::move(offsets);
        line.height = line.paragraph->getHeight();
        if (custom_metrics) {
            line.height = std::max(0.0f, line_ascent + line_descent);
            line.paint_shift = line_ascent - line.paragraph->getAlphabeticBaseline();
        }
        const auto placeholders = line.paragraph->getRectsForPlaceholders();
        if (placeholders.size() != tabs.size()) throw DocumentFailure{XL_INTERNAL_ERROR};
        size_t object_index = 0;
        for (size_t i = 0; i < tabs.size(); ++i) {
            if (edges[i] == 2)
                line.suppressed.push_back(tabs[i]);
            else if (edges[i] == 3) {
                const auto object = objects.at(object_index);
                const auto owner=object_owners.at(object_index++);
                const auto [dx,dy]=line.shift(tabs[i]);
                const auto object_rect=placeholders[i].rect.makeOffset(dx,dy);
                line.objects.push_back({tabs[i], object_rect, object,owner});
                if (object->scene) {
                    if(object->scene->scrollable()) {
                        const auto full=SkRect::MakeXYWH(line.x+object_rect.x(),line.y+line.paint_shift+object_rect.y(),object->width,object->height);
                        value->ink.join(full);value->curve_ink.join(full);value->curved=true;
                    }
                    value->curved |= object->scene->curved();
                    const auto curve=object->scene->curve_bounds();
                    if(nonempty(curve))value->curve_ink.join(SkRect::MakeXYWH(line.x+object_rect.x()+curve.x,line.y+line.paint_shift+object_rect.y()+curve.y,curve.width,curve.height));
                    const auto ink = object->scene->ink_bounds();
                    value->ink.join(SkRect::MakeXYWH(line.x + object_rect.x() + ink.x,
                        line.y + line.paint_shift + object_rect.y() + ink.y, ink.width, ink.height));
                }
            } else
                (edges[i] ? line.edges : line.tabs).emplace_back(tabs[i], placeholders[i].rect);
        }
        for (size_t index = 0; index < boxes.size(); ++index) {
            const auto& box = boxes[index];
            if (box.atomic) continue;
            if (box.end <= start || box.start >= metric_finish) continue;
            const auto begin = std::min(std::max(box.start, start), finish) - start;
            const auto end = std::min(box.end, finish) - start;
            std::vector<SkRect> fragments;
            for (const auto& rect :
                 line.paragraph->getRectsForRange(begin, end, RectHeightStyle::kTight, RectWidthStyle::kTight))
                fragments.push_back(rect.rect.makeSorted());
            for (const auto& [offset, rect] : line.edges)
                if (offset >= begin && offset < end) fragments.push_back(rect.makeSorted());
            for (const auto& object : line.objects)
                if (object.offset >= begin && object.offset < end) {
                    const auto [dx,dy]=line.shift(object.offset);
                    fragments.push_back(object.rect.makeOffset(-dx,-dy).makeSorted());
                }
            if (fragments.empty() && hard_break && box.start <= units[consumed].start && box.end > units[consumed].start)
                fragments.push_back(SkRect::MakeXYWH(rtl ? 0 : used, 0, 0, 0));
            std::sort(fragments.begin(), fragments.end(), [](auto a, auto b) { return a.left() < b.left(); });
            std::vector<SkRect> merged;
            for (const auto& rect : fragments) {
                if (!merged.empty() && rect.left() <= merged.back().right() + .05f)
                    merged.back().fRight = std::max(merged.back().right(), rect.right());
                else
                    merged.push_back(rect);
            }
            const auto metrics = font_metrics(box.format);
            const auto baseline = line.paragraph->getAlphabeticBaseline() + line.paint_shift + line.y +
                                  resolved_format(box.format).baseline_shift;
            for (auto rect : merged) {
                auto border = box.borders;
                bool has_start = box.begins && !box.opening && box.start >= start,
                     has_end = box.ends && !box.closing && box.end <= finish;
                for (const auto& [offset, edge] : line.edges) {
                    if (offset + start == box.start && box.opening && edge.left() <= rect.right() + .05f &&
                        edge.right() >= rect.left() - .05f) {
                        has_start = true;
                        if (box.rtl)
                            rect.fRight = edge.right() - box.margin_start;
                        else
                            rect.fLeft = edge.left() + box.margin_start;
                    }
                    if (offset + start + 1 == box.end && box.closing && edge.left() <= rect.right() + .05f &&
                        edge.right() >= rect.left() - .05f) {
                        has_end = true;
                        if (box.rtl)
                            rect.fLeft = edge.left() + box.margin_end;
                        else
                            rect.fRight = edge.right() - box.margin_end;
                    }
                }
                if (!has_start) border[box.rtl ? 1 : 3].width = 0;
                if (!has_end) border[box.rtl ? 3 : 1].width = 0;
                const auto top = baseline + metrics.fAscent - box.padding_top - border[0].width + box.format.visual_y;
                const auto bottom = baseline + metrics.fDescent + box.padding_bottom + border[2].width + box.format.visual_y;
                BoxPaint decoration{{rect.left() + line.x + box.format.visual_x, top, std::max(0.0f, rect.width()),
                                     std::max(0.0f, bottom - top), box.background, 0},
                                    border};
                auto radius=box.radii;
                if(!has_start){radius[box.rtl?1:0]={};radius[box.rtl?2:3]={};}
                if(!has_end){radius[box.rtl?0:1]={};radius[box.rtl?3:2]={};}
                decoration.background=box.background_paint;
                decoration.owner=box.owner;
                decoration.radii=resolve_radii(radius,decoration.rect.width,decoration.rect.height);
                if(RoundedRect{{decoration.rect.x,top,decoration.rect.width,decoration.rect.height},decoration.radii}.curved()) {
                    value->curved=true;value->curve_ink.join(SkRect::MakeXYWH(decoration.rect.x,top,decoration.rect.width,decoration.rect.height));
                }
                value->inline_bounds[index].push_back(
                    {decoration.rect.x, top, decoration.rect.width, decoration.rect.height});
                value->inline_shapes[index].push_back({{decoration.rect.x,top,decoration.rect.width,decoration.rect.height},decoration.radii});
                if (box.background || box.background_paint ||
                    std::any_of(border.begin(), border.end(), [](auto& side) { return side.width > 0; })) {
                    value->ink.join(
                        SkRect::MakeXYWH(decoration.rect.x, top, decoration.rect.width, decoration.rect.height));
                }
                line.decorations.push_back(std::move(decoration));
                line.decoration_sections.push_back(size_t(std::count_if(line.objects.begin(), line.objects.end(),
                    [&](const auto& object) { return object.offset < begin; })));
            }
        }
        line.paragraph->visit([&](int, const Paragraph::VisitorInfo* info) {
            if (!info) return;
            std::vector<SkRect> bounds(static_cast<size_t>(info->count));
            info->font.getBounds({info->glyphs, bounds.size()}, {bounds.data(), bounds.size()}, nullptr);
            for (size_t index = 0; index < bounds.size(); ++index) {
                bounds[index].offset(info->origin.x() + info->positions[index].x() + line.x,
                                     info->origin.y() + info->positions[index].y() + line.y + line.paint_shift);
                value->ink.join(bounds[index]);
            }
        });
        if (has_objects||has_visual||has_owners) {
            line.blobs.resize(line.objects.size() + 1);
            SectionPainter recorder(bindings, line.blobs);
            line.paragraph->paint(&recorder, line.x, line.y + line.paint_shift);
            if(has_visual)for(const auto& section:line.blobs)for(const auto& blob:section) {
                auto bounds=blob.text->bounds().makeOffset(blob.x,blob.y);
                if(blob.clip&&!bounds.intersect(*blob.clip))continue;
                value->ink.join(bounds);
            }
        }
        const auto line_index=value->lines.size();
        for(size_t i=0;i<line.decorations.size();++i) {
            const auto& box=line.decorations[i];
            value->parts.push_back({TextParagraph::Part::decoration,box.owner,line_index,i,0,{box.rect.x,box.rect.y,box.rect.width,box.rect.height}});
        }
        for(size_t section=0;section<line.blobs.size();++section)for(size_t i=0;i<line.blobs[section].size();++i) {
            const auto& blob=line.blobs[section][i];auto bounds=blob.text->bounds().makeOffset(blob.x,blob.y);
            if(blob.clip&&!bounds.intersect(*blob.clip))bounds=SkRect::MakeEmpty();
            value->parts.push_back({TextParagraph::Part::glyph,blob.owner,line_index,i,section,{bounds.x(),bounds.y(),bounds.width(),bounds.height()}});
        }
        for(size_t i=0;i<line.objects.size();++i) {
            const auto& object=line.objects[i];
            value->parts.push_back({TextParagraph::Part::object,object.owner,line_index,i,0,
                {line.x+object.rect.x(),line.y+line.paint_shift+object.rect.y(),object.rect.width(),object.rect.height()},object.value->flattened});
        }
        value->height += line.height;
        value->width = std::max(value->width, used);
        value->lines.push_back(std::move(line));
        if (consumed == units.size()) break;
        first = consumed + (units[consumed].scalar == '\n' ? 1u : 0u);
        // Collapsed spaces at a soft line start have no visible contribution.
        while (first < units.size() && collapsible(units[first]))
            ++first;
    }
    auto result = std::make_shared<TextParagraph>(std::move(value));
    for (const auto& family : base.families)
        cache_bytes += sizeof(std::string) + family.size();
    for (const auto& box : boxes)
        for (const auto& family : box.format.families)
            cache_bytes += sizeof(std::string) + family.size();
    if (cache_bytes <= 1024 * 1024) {
        while (!impl_->cache.empty() &&
               (impl_->cache.size() >= 128 || impl_->cache_bytes + cache_bytes > 1024 * 1024)) {
            impl_->cache_bytes -= impl_->cache.front().bytes;
            impl_->cache.pop_front();
        }
        impl_->cache.push_back({runs, boxes, width, rtl, align, base, cache_bytes, result});
        impl_->cache_bytes += cache_bytes;
    }
    return result;
}
} // namespace xl::detail
