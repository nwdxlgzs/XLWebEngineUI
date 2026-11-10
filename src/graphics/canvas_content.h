#pragma once
#include "image_content.h"
#include "text/paragraph.h"
#include <array>
#include <span>
namespace xl::detail {
struct CanvasGradient {
  enum Kind { linear, radial, conic } kind = linear;
  std::array<float, 6> points{};
  std::array<float, 6> matrix{1, 0, 0, 1, 0, 0};
  std::vector<std::pair<float, uint32_t>> stops;
};
class CanvasContent final {
public:
  struct Impl;
  CanvasContent(uint32_t width, uint32_t height);
  ~CanvasContent();
  void reset(uint32_t width, uint32_t height);
  uint32_t width() const;
  uint32_t height() const;
  bool needs_text() const;
  bool pending() const;
  size_t retained_bytes() const;
  uint64_t generation() const { return generation_; }
  void command(std::string_view, std::span<const double>);
  void style(std::string_view, std::string_view);
  void gradient(bool stroke, const CanvasGradient &);
  void text(std::string text, float x, float y, float max_width, bool stroke);
  struct Metrics {
    float width = 0, left = 0, right = 0, ascent = 0, descent = 0,
          font_ascent = 0, font_descent = 0;
  };
  Metrics measure(std::string text, TextEngine *, std::shared_ptr<void>);
  std::vector<uint8_t> pixels(int x, int y, uint32_t width,
                              uint32_t height) const;
  void put_pixels(std::span<const uint8_t>, uint32_t width, uint32_t height,
                  int x, int y, xl_node_bounds dirty);
  void image(std::shared_ptr<const ImageDrawing>, xl_node_bounds source,
             xl_node_bounds destination, bool raw = false);
  std::string data_url() const;
  std::array<float, 6> matrix() const;
  void render(TextEngine *, std::shared_ptr<void>);
  std::shared_ptr<const ImageDrawing> drawing();

private:
  std::unique_ptr<Impl> impl_;
  uint64_t generation_ = 0;
};
} // namespace xl::detail
