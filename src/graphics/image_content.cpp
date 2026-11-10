#include "image_content.h"
#include "document/document.h"
#include "include/codec/SkBmpDecoder.h"
#include "include/codec/SkCodec.h"
#include "include/codec/SkGifDecoder.h"
#include "include/codec/SkIcoDecoder.h"
#include "include/codec/SkJpegDecoder.h"
#include "include/codec/SkPngDecoder.h"
#include "include/codec/SkWbmpDecoder.h"
#include "include/codec/SkWebpDecoder.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPicture.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkStream.h"
#include "modules/skshaper/utils/FactoryHelpers.h"
#include "modules/svg/include/SkSVGDOM.h"
#include "modules/svg/include/SkSVGRenderContext.h"
#include "modules/svg/include/SkSVGSVG.h"
#include "resources/ui_content.h"
#define XML_STATIC
#include "third_party/externals/expat/expat/lib/expat.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <mutex>

namespace xl::detail {
namespace {
constexpr size_t image_limit = 64 * 1024 * 1024;
std::unique_ptr<SkCodec> codec(sk_sp<SkData> data) {
  static const SkCodecs::Decoder decoders[]{
      SkBmpDecoder::Decoder(), SkGifDecoder::Decoder(),
      SkIcoDecoder::Decoder(), SkJpegDecoder::Decoder(),
      SkPngDecoder::Decoder(), SkWbmpDecoder::Decoder(),
      SkWebpDecoder::Decoder()};
  return SkCodec::MakeFromData(std::move(data), decoders);
}
std::optional<bool> svg_text(std::string_view source) {
  struct Inspect {
    XML_Parser parser;
    size_t depth = 0, nodes = 0;
    bool text = false;
  };
  Inspect inspect{XML_ParserCreate(nullptr)};
  if (!inspect.parser)
    throw std::bad_alloc();
  struct Cleanup {
    XML_Parser p;
    ~Cleanup() { XML_ParserFree(p); }
  } cleanup{inspect.parser};
  XML_SetUserData(inspect.parser, &inspect);
  XML_SetElementHandler(
      inspect.parser,
      [](void *raw, const char *name, const char **) {
        auto &state = *static_cast<Inspect *>(raw);
        if (++state.depth > 256 || ++state.nodes > 65536) {
          XML_StopParser(state.parser, XML_FALSE);
          return;
        }
        auto tag = std::string_view(name);
        if (const auto colon = tag.rfind(':'); colon != tag.npos)
          tag.remove_prefix(colon + 1);
        if (tag == "text" || tag == "tspan" || tag == "textPath")
          state.text = true;
      },
      [](void *raw, const char *) { --static_cast<Inspect *>(raw)->depth; });
  XML_SetStartDoctypeDeclHandler(
      inspect.parser,
      [](void *raw, const char *, const char *, const char *, int) {
        XML_StopParser(static_cast<Inspect *>(raw)->parser, XML_FALSE);
      });
  if (XML_Parse(inspect.parser, source.data(), int(source.size()), XML_TRUE) !=
      XML_STATUS_OK)
    return {};
  return inspect.text;
}
class RasterAsset final : public skresources::ImageAsset {
  sk_sp<SkImage> image_;

public:
  explicit RasterAsset(sk_sp<SkImage> image) : image_(std::move(image)) {}
  bool isMultiFrame() override { return false; }
  sk_sp<SkImage> getFrame(float) override { return image_; }
};
class Provider final : public skresources::ResourceProvider {
  std::string base_;
  std::function<std::shared_ptr<const UiContent>(std::string_view)> loader_;
  mutable std::unordered_map<std::string, sk_sp<skresources::ImageAsset>>
      images_;
  mutable size_t bytes_ = 0;

public:
  size_t retained_bytes() const { return bytes_; }
  Provider(
      std::string base,
      std::function<std::shared_ptr<const UiContent>(std::string_view)> loader)
      : base_(std::move(base)), loader_(std::move(loader)) {}
  sk_sp<skresources::ImageAsset> loadImageAsset(const char *path,
                                                const char *name,
                                                const char *) const override {
    if (!loader_ || !name)
      return nullptr;
    try {
      auto reference = std::string(path ? path : "");
      if (!reference.empty() && !reference.ends_with('/'))
        reference += '/';
      reference += name;
      auto uri = resolve_ui_uri(base_, reference);
      if (const auto it = images_.find(uri); it != images_.end())
        return it->second;
      if (images_.size() >= 64)
        return nullptr;
      const auto content = loader_(uri);
      if (!content || content->bytes.size() > image_limit)
        return nullptr;
      auto decoder = codec(
          SkData::MakeWithCopy(content->bytes.data(), content->bytes.size()));
      if (!decoder)
        return nullptr;
      const auto info = decoder->getInfo();
      if (info.width() <= 0 || info.height() <= 0 ||
          uint64_t(info.width()) * info.height() > image_limit / 4)
        return nullptr;
      const auto size = size_t(info.width()) * size_t(info.height()) * 4;
      if (size > image_limit - bytes_)
        return nullptr;
      auto [image, result] = decoder->getImage(SkImageInfo::MakeN32Premul(
          info.width(), info.height(), info.refColorSpace()));
      if (!image || result != SkCodec::kSuccess)
        return nullptr;
      bytes_ += size;
      auto asset = sk_make_sp<RasterAsset>(std::move(image));
      images_.emplace(std::move(uri), asset);
      return asset;
    } catch (const UriFailure &error) {
      if (error.status == XL_NOT_FOUND || error.status == XL_INVALID_ARGUMENT ||
          error.status == XL_UNSUPPORTED)
        return nullptr;
      throw;
    }
  }
};
} // namespace
struct ImageContent::Impl {
  std::shared_ptr<const UiContent> encoded;
  sk_sp<SkSVGDOM> svg;
  sk_sp<SkData> data;
  sk_sp<Provider> provider;
  ImageMetrics metrics;
  std::shared_ptr<const ImageDrawing> raster;
  struct Recorded {
    float width, height;
    SkFontMgr *fonts;
    uint64_t font_revision;
    std::shared_ptr<const ImageDrawing> value;
    size_t bytes;
  };
  std::deque<Recorded> records;
  size_t bytes = 0;
};
ImageContent::ImageContent(std::unique_ptr<Impl> value)
    : impl_(std::move(value)) {}
ImageContent::~ImageContent() = default;
const ImageMetrics &ImageContent::metrics() const { return impl_->metrics; }
size_t ImageContent::retained_bytes() const {
  return impl_->encoded->bytes.size() * 2 + impl_->bytes +
         (impl_->provider ? impl_->provider->retained_bytes() : 0);
}
std::shared_ptr<ImageContent> ImageContent::make(
    std::shared_ptr<const UiContent> content, std::string base,
    std::function<std::shared_ptr<const UiContent>(std::string_view)> loader) {
  if (!content || content->bytes.empty() || content->bytes.size() > image_limit)
    return {};
  auto value = std::make_unique<Impl>();
  value->encoded = std::move(content);
  value->data = SkData::MakeWithCopy(value->encoded->bytes.data(),
                                     value->encoded->bytes.size());
  if (ui_mime_is(value->encoded->mime, "image/svg+xml")) {
    const std::string_view xml(
        reinterpret_cast<const char *>(value->encoded->bytes.data()),
        value->encoded->bytes.size());
    if (xml.size() > 16 * 1024 * 1024)
      return {};
    const auto text = svg_text(xml);
    if (!text)
      return {};
    value->metrics.text = *text;
    value->provider = sk_make_sp<Provider>(std::move(base), std::move(loader));
    SkMemoryStream stream(value->data);
    value->svg =
        SkSVGDOM::Builder().setResourceProvider(value->provider).make(stream);
    if (!value->svg)
      return {};
    const auto *root = value->svg->getRoot();
    const SkSVGLengthContext lengths(SkSize::Make(300, 150));
    const auto intrinsic_width = lengths.resolve(
        root->getWidth(), SkSVGLengthContext::LengthType::kHorizontal);
    const auto intrinsic_height = lengths.resolve(
        root->getHeight(), SkSVGLengthContext::LengthType::kVertical);
    if (root->getWidth().unit() != SkSVGLength::Unit::kPercentage &&
        std::isfinite(intrinsic_width) && intrinsic_width >= 0)
      value->metrics.width = intrinsic_width;
    if (root->getHeight().unit() != SkSVGLength::Unit::kPercentage &&
        std::isfinite(intrinsic_height) && intrinsic_height >= 0)
      value->metrics.height = intrinsic_height;
    if (value->metrics.width && value->metrics.height &&
        *value->metrics.width > 0 && *value->metrics.height > 0)
      value->metrics.ratio = *value->metrics.width / *value->metrics.height;
    else if (const auto &viewbox = root->getViewBox();
             viewbox && viewbox->width() > 0 && viewbox->height() > 0)
      value->metrics.ratio = viewbox->width() / viewbox->height();
  } else {
    auto decoder = codec(value->data);
    if (!decoder)
      return {};
    const auto info = decoder->getInfo();
    if (info.width() <= 0 || info.height() <= 0 ||
        uint64_t(info.width()) * info.height() > image_limit / 4)
      return {};
    value->metrics.width = float(info.width());
    value->metrics.height = float(info.height());
    value->metrics.ratio = float(info.width()) / float(info.height());
  }
  return std::make_shared<ImageContent>(std::move(value));
}
std::shared_ptr<const ImageDrawing>
ImageContent::drawing(float width, float height, sk_sp<SkFontMgr> fonts,
                      std::shared_ptr<void> session, uint64_t font_revision) {
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 ||
      height <= 0)
    return {};
  auto &value = *impl_;
  if (!value.svg) {
    if (value.raster)
      return value.raster;
    auto decoder = codec(value.data);
    if (!decoder)
      return {};
    const auto info = decoder->getInfo();
    auto [image, result] = decoder->getImage(SkImageInfo::MakeN32Premul(
        info.width(), info.height(), info.refColorSpace()));
    if (!image || result != SkCodec::kSuccess)
      return {};
    auto drawing = std::make_shared<ImageDrawing>();
    drawing->image = std::move(image);
    value.bytes =
        size_t(*value.metrics.width) * size_t(*value.metrics.height) * 4;
    value.raster = drawing;
    return drawing;
  }
  for (const auto &record : value.records)
    if (record.width == width && record.height == height &&
        record.fonts == fonts.get() && record.font_revision == font_revision)
      return record.value;
  // Parsing and recording are UI-owned; only the immutable picture crosses
  // to a render executor. Font/ICU lifetime is retained by that picture lease.
  auto drawing = std::make_shared<ImageDrawing>();
  drawing->text_session = std::move(session);
  SkMemoryStream stream(value.data);
  auto builder = SkSVGDOM::Builder().setResourceProvider(value.provider);
  if (value.metrics.text) {
    if (!drawing->text_session || !fonts)
      throw DocumentFailure{XL_BUSY};
    builder.setFontManager(fonts).setTextShapingFactory(
        SkShapers::BestAvailable());
  }
  auto dom = builder.make(stream);
  if (!dom)
    return {};
  dom->getRoot()->setWidth(SkSVGLength(100, SkSVGLength::Unit::kPercentage));
  dom->getRoot()->setHeight(SkSVGLength(100, SkSVGLength::Unit::kPercentage));
  dom->setContainerSize(SkSize::Make(width, height));
  SkPictureRecorder recorder;
  auto *canvas = recorder.beginRecording(SkRect::MakeWH(width, height));
  canvas->clipRect(SkRect::MakeWH(width, height));
  dom->render(canvas);
  drawing->picture = recorder.finishRecordingAsPicture();
  if (!drawing->picture)
    return {};
  const auto bytes = drawing->picture->approximateBytesUsed();
  while (!value.records.empty() &&
         (value.records.size() >= 4 ||
          bytes > image_limit - std::min(image_limit, value.bytes))) {
    value.bytes -= value.records.front().bytes;
    value.records.pop_front();
  }
  if (bytes <= image_limit) {
    value.records.push_back(
        {width, height, fonts.get(), font_revision, drawing, bytes});
    value.bytes += bytes;
  }
  return drawing;
}
void ImageDrawing::paint(SkCanvas *canvas, xl_node_bounds bounds) const {
  if (bounds.width <= 0 || bounds.height <= 0)
    return;
  if (image)
    canvas->drawImageRect(
        image,
        SkRect::MakeXYWH(bounds.x, bounds.y, bounds.width, bounds.height),
        SkSamplingOptions(SkFilterMode::kLinear));
  else if (picture) {
    SkAutoCanvasRestore save(canvas, true);
    canvas->translate(bounds.x, bounds.y);
    canvas->drawPicture(picture);
  }
}
} // namespace xl::detail
