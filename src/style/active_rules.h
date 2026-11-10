#pragma once
#include "document/dom_internal.h"

namespace xl::detail {
class MediaQuery;
class NestedSheet;
using LayerOrder = std::vector<uint32_t>;
struct ActiveStyle {
    lxb_css_rule_style_t *rule;
    std::shared_ptr<const std::string> source;
    std::shared_ptr<const LayerOrder> layer;
    std::shared_ptr<NestedSheet> compiled;
};
struct ActiveStyles {
    std::vector<ActiveStyle> rules;
    bool manual = false, layers = false;
    std::vector<std::pair<std::shared_ptr<MediaQuery>, bool>> conditions;
    explicit ActiveStyles(Document::Impl &document);
};
} // namespace xl::detail
