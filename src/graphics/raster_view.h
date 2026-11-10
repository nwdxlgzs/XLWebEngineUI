#pragma once
#include <XLWebEngineUI/xl.h>
#include <memory>
#include <span>
#include "text/paragraph.h"
#include "box_paint.h"
#include "outline_paint.h"
#include "display_list.h"

namespace xl::detail {
struct Damage {
    int left = 0, top = 0, right = 0, bottom = 0;
    bool empty() const noexcept { return right <= left || bottom <= top; }
};

// UI-owned retained scene. No platform window, event loop or resource loading.
class RasterView final {
public:
    RasterView(int width, int height, uint32_t background);
    ~RasterView();
    void resize(int width, int height);
    void background(uint32_t color);
    void rects(std::span<const xl_paint_rect> rectangles);
    void boxes(std::span<const BoxPaint> boxes);
    void texts(std::span<const TextItem> text);
    void outlines(std::span<const OutlinePaint> outlines);
    void order(std::span<const PaintCommand> commands);
    void overlays(std::span<const xl_paint_rect> rectangles);
    void invalidate(int x, int y, int width, int height);
    bool dirty() const noexcept;
    bool ready() const noexcept;
    int width() const noexcept;
    int height() const noexcept;
    Damage paint();
    bool copy_rgba(void* data, size_t capacity, size_t row_bytes) const;
    bool read_region(Damage region, void* data, size_t row_bytes) const;
    uint64_t frames() const noexcept;
    uint64_t painted_pixels() const noexcept;
    void graphics_backend(uint32_t backend);
    xl_graphics_info graphics_info() const;
    void abandon_graphics();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
