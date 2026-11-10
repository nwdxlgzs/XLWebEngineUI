#include "dom_internal.h"
#include "graphics/canvas_content.h"
#include "graphics/paint_scene.h"
#include <cctype>
#include <charconv>
namespace xl::detail {
std::pair<uint32_t, uint32_t> Document::Impl::canvas_size(xl_node_id id) const {
  if (resolve(id)->local_name != LXB_TAG_CANVAS)
    throw DocumentFailure{XL_INVALID_ARGUMENT};
  const auto dimension = [&](const char *name, uint32_t fallback) {
    const auto value = attribute(id, name, false);
    size_t offset = 0;
    while (offset < value.size() &&
           std::isspace(static_cast<unsigned char>(value[offset])))
      ++offset;
    if (offset < value.size() && value[offset] == '+')
      ++offset;
    uint32_t result = 0;
    const auto parsed = std::from_chars(value.data() + offset,
                                        value.data() + value.size(), result);
    return parsed.ec == std::errc{} ? result : fallback;
  };
  return {dimension("width", 300), dimension("height", 150)};
}
CanvasContent &Document::Impl::canvas(xl_node_id id) {
  const auto [w, h] = canvas_size(id);
  auto &result = canvases[id];
  if (!result)
    result = std::make_unique<CanvasContent>(w, h);
  return *result;
}
void Document::Impl::canvas_reset(xl_node_id id) {
  const auto found = canvases.find(id);
  if (found != canvases.end()) {
    const auto [w, h] = canvas_size(id);
    found->second->reset(w, h);
  }
}
void Document::Impl::render_canvases() {
  uint64_t bytes = 0;
  for (const auto &[id, item] : canvases) {
    (void)id;
    const uint64_t next = uint64_t(item->width()) * item->height() * 4;
    if (item->pending() && next > 64 * 1024 * 1024)
      throw DocumentFailure{XL_OUT_OF_MEMORY};
    bytes +=
        std::max<uint64_t>(item->retained_bytes(), item->pending() ? next : 0);
    if (bytes > 128 * 1024 * 1024)
      throw DocumentFailure{XL_OUT_OF_MEMORY};
  }
  for (auto &[id, item] : canvases) {
    (void)id;
    if (item->needs_text() && !text_engine)
      text_engine = std::make_unique<TextEngine>();
    item->render(text_engine.get(), text_session);
  }
}
void Document::Impl::ensure_canvas_text() {
  if (!text_session && text_preparer)
    text_session = text_preparer();
  if (!text_session)
    throw DocumentFailure{XL_BUSY};
  if (!text_engine)
    text_engine = std::make_unique<TextEngine>();
}
void Document::Impl::canvas_changed() { ++canvas_revision; }
uint64_t Document::canvas_revision() const { return impl_->canvas_revision; }
bool Document::canvas_has_text() const {
  for (const auto &[id, canvas] : impl_->canvases) {
    (void)id;
    if (canvas->needs_text())
      return true;
  }
  return false;
}
void Document::refresh_canvases(DocumentFrame &frame) {
  impl_->render_canvases();
  ImageUpdates updates;
  for (const auto &[id, canvas] : impl_->canvases)
    updates.emplace(id, canvas->drawing());
  for (auto &paint : frame.paint)
    if (const auto found = updates.find(paint.owner); found != updates.end())
      paint.image = found->second;
  for (auto &text : frame.texts)
    text.scenes = text.paragraph->image_scenes(updates, text.scenes);
  frame.canvas_revision = impl_->canvas_revision;
}
} // namespace xl::detail
