#include "images.h"
#include "dom_internal.h"
#include "style/computed.h"
#include <cstdio>
#include <cstring>
#include <map>

namespace xl::detail {
namespace {
constexpr size_t cache_limit = 64 * 1024 * 1024, xml_limit = 16 * 1024 * 1024;
std::shared_ptr<const UiContent> data_image(std::string_view uri) {
  if (!uri.starts_with("data:"))
    return {};
  const auto comma = uri.find(',');
  if (comma == uri.npos || uri.size() > 24 * 1024 * 1024)
    return {};
  auto header = uri.substr(5, comma - 5);
  const bool base64 = header.ends_with(";base64");
  if (base64)
    header.remove_suffix(7);
  const auto mime = header.substr(0, header.find(';'));
  if (!mime.starts_with("image/"))
    return {};
  auto content = std::make_shared<UiContent>();
  content->mime = mime;
  const auto hex = [](char c) {
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
                                  : -1;
  };
  std::string decoded;
  decoded.reserve(uri.size() - comma - 1);
  for (size_t i = comma + 1; i < uri.size(); ++i) {
    auto c = uri[i];
    if (c == '%') {
      if (i + 2 >= uri.size() || hex(uri[i + 1]) < 0 || hex(uri[i + 2]) < 0)
        return {};
      c = char(hex(uri[i + 1]) * 16 + hex(uri[i + 2]));
      i += 2;
    }
    decoded += c;
  }
  if (base64) {
    uint32_t bits = 0;
    unsigned count = 0, padding = 0;
    bool ended = false;
    for (unsigned char c : decoded) {
      if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f')
        continue;
      if (c == '=') {
        ended = true;
        if (++padding > 2)
          return {};
        continue;
      }
      if (ended)
        return {};
      const auto value = c >= 'A' && c <= 'Z'   ? c - 'A'
                         : c >= 'a' && c <= 'z' ? c - 'a' + 26
                         : c >= '0' && c <= '9' ? c - '0' + 52
                         : c == '+'             ? 62
                         : c == '/'             ? 63
                                                : -1;
      if (value < 0)
        return {};
      bits = (bits << 6) | unsigned(value);
      count += 6;
      if (count >= 8) {
        count -= 8;
        content->bytes.push_back(std::byte((bits >> count) & 255));
      }
    }
    if (count == 6 || content->bytes.size() > xml_limit)
      return {};
  } else {
    if (decoded.size() > xml_limit)
      return {};
    content->bytes.resize(decoded.size());
    std::memcpy(content->bytes.data(), decoded.data(), decoded.size());
  }
  return content;
}
std::string xml_escape(std::string_view text) {
  std::string result;
  for (char c : text) {
    if (c == '&')
      result += "&amp;";
    else if (c == '<')
      result += "&lt;";
    else if (c == '>')
      result += "&gt;";
    else if (c == '\"')
      result += "&quot;";
    else
      result += c;
  }
  return result;
}
std::string name(const lxb_char_t *text, size_t size) {
  return {reinterpret_cast<const char *>(text), size};
}
std::string canonical(std::string value) {
  // Lexbor's HTML parser normalizes SVG tag/attribute case. XML is
  // case-sensitive.
  static constexpr const char *names[]{"viewBox",
                                       "preserveAspectRatio",
                                       "gradientUnits",
                                       "gradientTransform",
                                       "spreadMethod",
                                       "clipPathUnits",
                                       "patternUnits",
                                       "patternContentUnits",
                                       "patternTransform",
                                       "markerWidth",
                                       "markerHeight",
                                       "markerUnits",
                                       "refX",
                                       "refY",
                                       "filterUnits",
                                       "primitiveUnits",
                                       "stdDeviation",
                                       "baseFrequency",
                                       "numOctaves",
                                       "kernelUnitLength",
                                       "surfaceScale",
                                       "diffuseConstant",
                                       "specularConstant",
                                       "specularExponent",
                                       "limitingConeAngle",
                                       "pointsAtX",
                                       "pointsAtY",
                                       "pointsAtZ",
                                       "xChannelSelector",
                                       "yChannelSelector",
                                       "tableValues",
                                       "keyPoints",
                                       "keyTimes",
                                       "keySplines",
                                       "attributeName",
                                       "textLength",
                                       "lengthAdjust",
                                       "linearGradient",
                                       "radialGradient",
                                       "clipPath",
                                       "textPath",
                                       "foreignObject",
                                       "feGaussianBlur",
                                       "feColorMatrix",
                                       "feBlend",
                                       "feComposite",
                                       "feOffset",
                                       "feFlood",
                                       "feMerge",
                                       "feMergeNode",
                                       "feMorphology",
                                       "feTurbulence",
                                       "feDisplacementMap",
                                       "feComponentTransfer",
                                       "feFuncR",
                                       "feFuncG",
                                       "feFuncB",
                                       "feFuncA",
                                       "feDiffuseLighting",
                                       "feSpecularLighting",
                                       "feDistantLight",
                                       "fePointLight",
                                       "feSpotLight"};
  for (const auto *candidate : names)
    if (value.size() == std::strlen(candidate) &&
        std::equal(value.begin(), value.end(), candidate, [](char a, char b) {
          return a == b || a == (b >= 'A' && b <= 'Z' ? b - 'A' + 'a' : b);
        }))
      return candidate;
  return value;
}
std::optional<std::string> declared(Document::Impl &doc, lxb_dom_node_t *node,
                                    const char *property) {
  const auto *declaration = lxb_dom_element_style_by_name(
      element(node), chars(property), std::strlen(property));
  if (!declaration)
    return {};
  if (declaration->type == LXB_CSS_PROPERTY__CUSTOM) {
    const auto &value = declaration->u.custom->value;
    return name(value.data, value.length);
  }
  try {
    return doc.style(doc.identify(node), property);
  } catch (const DocumentFailure &e) {
    if (e.status == XL_NOT_FOUND)
      return {};
    throw;
  }
}
std::string snapshot(Document::Impl &doc, lxb_dom_node_t *root) {
  std::string result;
  result.reserve(2048);
  size_t count = 0;
  const auto append = [&](std::string_view value) {
    if (value.size() > xml_limit - result.size())
      throw DocumentFailure{XL_UNSUPPORTED};
    result += value;
  };
  BoxStyle inherited;
  std::vector<lxb_dom_node_t *> ancestors;
  for (auto *node = root->parent;
       node && node->type == LXB_DOM_NODE_TYPE_ELEMENT; node = node->parent)
    ancestors.push_back(node);
  for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
    inherited = box_style(doc, *it, float(doc.viewport_width),
                          doc.viewport_width, doc.viewport_height, inherited);
  const auto walk = [&](auto &&self, lxb_dom_node_t *node, size_t depth,
                        const BoxStyle &parent) -> void {
    if (depth > 256 || ++count > 65536)
      throw DocumentFailure{XL_UNSUPPORTED};
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
      const auto &data = lxb_dom_interface_text(node)->char_data.data;
      append(
          xml_escape({reinterpret_cast<const char *>(data.data), data.length}));
      return;
    }
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT || node->ns != LXB_NS_SVG)
      return;
    size_t size = 0;
    const auto *raw = lxb_dom_element_local_name(element(node), &size);
    const auto tag = canonical(name(raw, size));
    if (tag == "script" || tag == "style" || tag == "foreignObject")
      return;
    const auto box = box_style(doc, node, float(doc.viewport_width),
                               doc.viewport_width, doc.viewport_height, parent);
    if (box.hidden)
      return;
    std::map<std::string, std::string> attributes;
    for (auto *attr = lxb_dom_element_first_attribute(element(node)); attr;
         attr = attr->next) {
      raw = lxb_dom_attr_qualified_name(attr, &size);
      auto key = canonical(name(raw, size));
      if (key.starts_with("on") || key == "style" || key == "xmlns" ||
          key == "xmlns:xlink")
        continue;
      raw = lxb_dom_attr_value(attr, &size);
      attributes[std::move(key)] = name(raw, size);
    }
    if (const auto it = attributes.find("href"); it != attributes.end()) {
      attributes["xlink:href"] = it->second;
      attributes.erase(it);
    }
    // Snapshot only presentation declarations consumed by SkSVG. The
    // authored DOM/stylesheet and public style strings remain unchanged.
    for (const auto *property : {"fill",
                                 "fill-opacity",
                                 "fill-rule",
                                 "stroke",
                                 "stroke-opacity",
                                 "stroke-width",
                                 "stroke-linecap",
                                 "stroke-linejoin",
                                 "stroke-miterlimit",
                                 "stroke-dasharray",
                                 "stroke-dashoffset",
                                 "clip-path",
                                 "clip-rule",
                                 "opacity",
                                 "visibility",
                                 "display",
                                 "stop-color",
                                 "stop-opacity",
                                 "flood-color",
                                 "flood-opacity",
                                 "color-interpolation",
                                 "color-interpolation-filters",
                                 "font-family",
                                 "font-size",
                                 "font-weight",
                                 "font-style",
                                 "text-anchor",
                                 "dominant-baseline",
                                 "transform",
                                 "x",
                                 "y",
                                 "cx",
                                 "cy",
                                 "r",
                                 "rx",
                                 "ry",
                                 "width",
                                 "height"})
      if (node != root || (std::string_view(property) != "width" &&
                           std::string_view(property) != "height"))
        if (auto value = declared(doc, node, property);
            value && *value != "inherit" && *value != "initial" &&
            *value != "unset")
          attributes[property] = std::move(*value);
    // SkSVG's parser handles legacy SVG colors; feed it the already
    // resolved CSS currentColor even when inherited from HTML ancestors.
    const auto color_text = [](uint32_t value) {
      char color[80]{};
      std::snprintf(color, sizeof(color), "rgba(%u,%u,%u,%.8f)",
                    (value >> 16) & 255, (value >> 8) & 255, value & 255,
                    double(value >> 24) / 255);
      return std::string(color);
    };
    attributes["color"] = color_text(box.color);
    for (const auto *property : {"fill", "stroke", "stop-color", "flood-color"})
      if (const auto found = attributes.find(property);
          found != attributes.end())
        if (auto color = csscolor::parse(found->second))
          found->second = color_text(csscolor::argb(
              csscolor::resolve(color, {box.foreground, box.dark})));
    if (node == root) {
      char size[40]{};
      std::snprintf(size, sizeof(size), "%.6g", double(box.font_size));
      attributes["font-size"] = size;
      if (!box.families.empty()) {
        std::string families;
        for (const auto &family : box.families) {
          if (!families.empty())
            families += ',';
          families += family;
        }
        attributes["font-family"] = std::move(families);
      }
    }
    append("<");
    append(tag);
    if (node == root)
      append(" xmlns=\"http://www.w3.org/2000/svg\" "
             "xmlns:xlink=\"http://www.w3.org/1999/xlink\"");
    for (const auto &[key, value] : attributes) {
      append(" ");
      append(key);
      append("=\"");
      append(xml_escape(value));
      append("\"");
    }
    append(">");
    for (auto *child = node->first_child; child; child = child->next)
      self(self, child, depth + 1, box);
    append("</");
    append(tag);
    append(">");
  };
  walk(walk, root, 0, inherited);
  return result;
}
} // namespace
std::shared_ptr<ImageContent> DocumentImages::node(
    xl_node_id id, uint64_t revision, uint64_t mounts,
    const std::function<std::shared_ptr<ImageContent>()> &load) {
  if (revision_ != revision || mount_revision_ != mounts) {
    nodes_.clear();
    if (mount_revision_ != mounts) {
      entries_.clear();
      bytes_ = 0;
    }
    trim();
    revision_ = revision;
    mount_revision_ = mounts;
  }
  if (const auto found = nodes_.find(id); found != nodes_.end())
    return found->second;
  auto value = load();
  nodes_.emplace(id, value);
  return value;
}
std::shared_ptr<ImageContent>
DocumentImages::open(std::shared_ptr<const UiContent> content,
                     std::string source, std::string base, UiMounts *mounts) {
  for (const auto &entry : entries_)
    if ((entry.content == content || base.starts_with("data:")) &&
        entry.source == source && entry.base == base)
      return entry.value;
  if (!source.empty()) {
    auto inline_content = std::make_shared<UiContent>();
    inline_content->mime = "image/svg+xml";
    inline_content->bytes.resize(source.size());
    std::memcpy(inline_content->bytes.data(), source.data(), source.size());
    content = std::move(inline_content);
  }
  const auto size =
      (content ? content->bytes.size() : 0) + source.size() + base.size();
  auto value = ImageContent::make(
      content, base,
      mounts ? std::function<std::shared_ptr<const UiContent>(
                   std::string_view)>{[mounts](auto uri) {
        return mounts->open(uri);
      }}
             : nullptr);
  while (!entries_.empty() &&
         (entries_.size() >= 64 || bytes_ + size > cache_limit)) {
    bytes_ -= entries_.front().bytes;
    entries_.pop_front();
  }
  if (size <= cache_limit) {
    entries_.push_back({source.empty() ? content : nullptr, std::move(source),
                        std::move(base), value, size});
    bytes_ += size;
  }
  return value;
}
void DocumentImages::trim() {
  bytes_ = 0;
  for (auto &entry : entries_) {
    entry.bytes = entry.source.size() + entry.base.size() +
                  (entry.value     ? entry.value->retained_bytes()
                   : entry.content ? entry.content->bytes.size()
                                   : 0);
    bytes_ += entry.bytes;
  }
  while (!entries_.empty() && (entries_.size() > 64 || bytes_ > cache_limit)) {
    bytes_ -= entries_.front().bytes;
    entries_.pop_front();
  }
}
bool Document::Impl::image_node(lxb_dom_node_t *node) const {
  return node->type == LXB_DOM_NODE_TYPE_ELEMENT &&
         (node->local_name == LXB_TAG_IMG ||
          (node->local_name == LXB_TAG_SVG && node->ns == LXB_NS_SVG));
}
std::shared_ptr<ImageContent>
Document::Impl::image_content(lxb_dom_node_t *node) {
  if (!image_node(node))
    return {};
  if (!images)
    images = std::make_unique<DocumentImages>();
  return images->node(
      identify(node), revision, mounts ? mounts->revision() : 0, [&] {
        const auto base = base_uri();
        if (node->local_name == LXB_TAG_SVG)
          return images->open({}, snapshot(*this, node), base, mounts);
        const auto src = attribute(identify(node), "src", false);
        if (src.empty())
          return std::shared_ptr<ImageContent>{};
        if (src.starts_with("data:")) {
          // Embedded bytes are not a network protocol. Opaque data images
          // have no base for resolving external relative resources.
          auto content = data_image(src);
          return content ? images->open(content, {}, src, nullptr)
                         : std::shared_ptr<ImageContent>{};
        }
        if (!mounts)
          return std::shared_ptr<ImageContent>{};
        try {
          const auto uri = resolve_ui_uri(base, src);
          return images->open(mounts->open(uri), {}, uri, mounts);
        } catch (const UriFailure &error) {
          if (error.status == XL_NOT_FOUND ||
              error.status == XL_INVALID_ARGUMENT ||
              error.status == XL_UNSUPPORTED)
            return std::shared_ptr<ImageContent>{};
          throw;
        }
      });
}
} // namespace xl::detail
