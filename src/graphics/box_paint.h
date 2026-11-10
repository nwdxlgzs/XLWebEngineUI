#pragma once
#include <XLWebEngineUI/xl.h>
#include <array>
#include "clip.h"
#include "style/background.h"

class SkCanvas;
namespace xl::detail {
enum class BorderStyle { none, hidden, solid, dotted, dashed, double_line, groove, ridge, inset, outset };
struct BorderSide {
    float width = 0;
    uint32_t color = 0xff000000;
    BorderStyle style = BorderStyle::none;
    float specified_width = 3;
    bool operator==(const BorderSide&) const = default;
};
struct BoxPaint {
    xl_paint_rect rect{};
    std::array<BorderSide, 4> borders{}; // top, right, bottom, left
    Clip clip{};
    uint8_t disclosure = 0; // Native triangle: 1 right, 2 down, 3 left.
    CornerRadii radii{};
    std::shared_ptr<const cssbackground::Paint> background = {};
    xl_node_id background_node = 0;
    // x/y are this box's own scroll; width/height are its scrollable padding area.
    xl_node_bounds background_local{};
    xl_node_id background_canvas_node = 0;
    xl_node_bounds background_reference{};
    xl_node_id owner=0;
    std::shared_ptr<const class ImageDrawing> image = {};
    xl_node_bounds image_rect{};
};
void paint_background(SkCanvas* canvas, const BoxPaint& box);
bool background_equal(const BoxPaint& a, const BoxPaint& b);
void paint_box(SkCanvas* canvas, const BoxPaint& box);
} // namespace xl::detail
