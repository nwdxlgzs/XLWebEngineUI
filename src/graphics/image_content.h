#pragma once
#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"
#include <XLWebEngineUI/xl.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
class SkCanvas;
class SkFontMgr;
class SkPicture;
namespace xl::detail {
struct UiContent;
struct ImageMetrics {
  std::optional<float> width, height, ratio;
  bool text = false;
};
class ImageDrawing final {
public:
  // Declared first so it outlives all Skia objects during destruction.
  std::shared_ptr<void> text_session;
  sk_sp<SkImage> image;
  sk_sp<SkPicture> picture;
  void paint(SkCanvas *, xl_node_bounds) const;
};
class ImageContent final {
public:
  struct Impl;
  explicit ImageContent(std::unique_ptr<Impl>);
  ~ImageContent();
  const ImageMetrics &metrics() const;
  size_t retained_bytes() const;
  std::shared_ptr<const ImageDrawing> drawing(float width, float height,
                                              sk_sp<SkFontMgr>,
                                              std::shared_ptr<void>,
                                              uint64_t font_revision = 0);
  static std::shared_ptr<ImageContent> make(
      std::shared_ptr<const UiContent>, std::string base,
      std::function<std::shared_ptr<const UiContent>(std::string_view)> loader);

private:
  std::unique_ptr<Impl> impl_;
};
} // namespace xl::detail
