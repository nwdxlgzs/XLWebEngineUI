#pragma once
#include <XLWebEngineUI/xl.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "graphics/box_paint.h"

class SkCanvas;
class SkFontMgr;
template<class T> class sk_sp;
namespace xl::detail {
class PaintScene;
struct TextObject {
    float width = 0, height = 0, baseline = 0;
    std::shared_ptr<const PaintScene> scene;
    bool flattened=false;
    bool operator==(const TextObject&) const;
};
enum class WhiteSpace { normal, nowrap, pre, pre_wrap, pre_line, break_spaces };
inline bool collapses_spaces(WhiteSpace value) {
    return value == WhiteSpace::normal || value == WhiteSpace::nowrap || value == WhiteSpace::pre_line;
}
inline bool preserves_breaks(WhiteSpace value) {
    return value != WhiteSpace::normal && value != WhiteSpace::nowrap;
}
inline bool wraps_text(WhiteSpace value) {
    return value != WhiteSpace::nowrap && value != WhiteSpace::pre;
}
struct TextFormat {
    float size = 16;
    uint32_t color = 0xff000000;
    int weight = 400;
    bool italic = false;
    std::vector<std::string> families;
    WhiteSpace whitespace = WhiteSpace::normal;
    float line_height = -1;
    float tab_size = 8;
    bool tab_is_length = false;
    bool oblique = false;
    int stretch = 5;
    float baseline_shift = 0; // Positive moves down, relative to the block strut.
    uint64_t alignment_group = 0; // Independent top/bottom aligned subtree.
    float visual_x = 0, visual_y = 0; // Post-layout inline relative positioning.
    bool operator==(const TextFormat&) const = default;
};
struct TextRun {
    std::string text;
    TextFormat format;
    int edge = 0; // +1 opening, -1 closing; synthetic and never DOM/AX text
    float edge_width = 0;
    std::shared_ptr<const TextObject> object = {};
    xl_node_id owner=0;
    bool operator==(const TextRun&) const;
};
struct TextInlineBox {
    uint32_t start = 0, end = 0;
    TextFormat format;
    float padding_top = 0, padding_bottom = 0, margin_start = 0, margin_end = 0;
    std::array<BorderSide, 4> borders{};
    RadiusStyle radii{};
    uint32_t background = 0;
    std::shared_ptr<const cssbackground::Paint> background_paint = {};
    bool rtl = false, opening = false, closing = false, begins = false, ends = false;
    uint8_t line_alignment = 0; // 1 top, 2 bottom; otherwise baseline-relative.
    uint64_t alignment_parent = 0;
    float alignment_offset = 0;
    bool atomic = false;
    xl_node_id owner=0;
    bool operator==(const TextInlineBox&) const = default;
};
struct TextSource {
    static constexpr uint32_t invalid_byte=0xffffffffu;
    xl_node_id node = 0;
    // One paragraph UTF-16 offset per DOM UTF-16 boundary. Repeated offsets
    // represent collapsed whitespace, including whitespace across elements.
    std::vector<uint32_t> offsets;
    std::string rendered;
    std::string original;
    std::vector<uint32_t> byte_offsets;
    size_t order = 0;
    std::vector<xl_node_bounds> fragments = {}; // Paragraph-local hit geometry, prepared once per layout.
    bool generated = false;
};
using TextScenes=std::unordered_map<const TextObject*,std::shared_ptr<const PaintScene>>;
using TextScrollOffsets=std::unordered_map<xl_node_id,std::pair<float,float>>;
using ImageUpdates=std::unordered_map<xl_node_id,std::shared_ptr<const class ImageDrawing>>;
struct TextFontMetrics { float ascent = 0, descent = 0, leading = 0, x_height = 0; };
class TextParagraph final {
  public:
    struct Part {
        enum Kind { decoration, glyph, object } kind=glyph;
        xl_node_id owner=0;
        size_t line=0,index=0,section=0;
        xl_node_bounds bounds{};
        bool flattened=false;
    };
    const std::vector<Part>& parts() const;
    void paint_part(SkCanvas* canvas,float x,float y,size_t index,const TextScenes* scenes=nullptr) const;
    struct Impl;
    explicit TextParagraph(std::unique_ptr<Impl> impl);
    ~TextParagraph();
    float height() const;
    float width() const;
    float last_baseline() const;
    float first_baseline() const;
    std::vector<std::pair<uint32_t, xl_node_bounds>> objects() const;
    xl_node_bounds ink_bounds() const;
    bool curved() const;
    xl_node_bounds curve_bounds() const;
    void paint(SkCanvas* canvas, float x, float y,const TextScenes* scenes=nullptr) const;
    std::shared_ptr<const TextScenes> scroll_scenes(const TextScrollOffsets& offsets,std::shared_ptr<const TextScenes> previous={}) const;
    std::shared_ptr<const TextScenes> image_scenes(const ImageUpdates&,std::shared_ptr<const TextScenes> previous={}) const;
    std::vector<xl_node_bounds> ranges(uint32_t start, uint32_t end) const;
    std::vector<xl_node_bounds> inline_ranges(size_t index) const;
    const std::vector<RoundedRect>& inline_shapes(size_t index) const;
    std::pair<uint32_t, bool> hit(float x, float y) const;
    xl_node_bounds caret(uint32_t offset, bool downstream) const;

  private:
    std::unique_ptr<Impl> impl_;
};
class TextEngine final {
  public:
    TextEngine();
    ~TextEngine();
    void add_font(const char* family, const void* data, size_t size);
    float normal_line_height(const TextFormat& format) const;
    TextFontMetrics font_metrics(const TextFormat& format) const;
    sk_sp<SkFontMgr> svg_fonts() const;
    uint64_t font_revision() const;
    std::shared_ptr<TextParagraph> shape(const std::vector<TextRun>& runs, const std::vector<TextInlineBox>& boxes,
                                         float width, bool rtl, uint32_t align, const TextFormat& base,
                                         std::shared_ptr<void> session);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
struct TextItem {
    float x = 0, y = 0;
    std::shared_ptr<TextParagraph> paragraph;
    size_t paint_index = 0;
    float layout_width = 0;
    std::vector<TextSource> sources;
    bool embedded = false; // Painted by an ancestor's atomic scene.
    Clip clip{};
    bool control = false;
    std::shared_ptr<const TextScenes> scenes = {};
    xl_node_id owner=0;
};
struct TextPaint {
    float x = 0, y = 0;
    std::shared_ptr<TextParagraph> paragraph;
    size_t paint_index = 0;
    Clip clip{};
    std::shared_ptr<const TextScenes> scenes = {};
    bool operator==(const TextPaint&) const = default;
};
} // namespace xl::detail
