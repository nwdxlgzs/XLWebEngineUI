#pragma once
#include "document/dom_internal.h"

namespace xl::detail {
class ComputedStyleCache;
// Private compiled copies preserve authored DOM text and source offsets within
// their own normalized input. Clear element cascades before retiring a cache.
class NestedSheet final {
  public:
    explicit NestedSheet(std::shared_ptr<const std::string> authored, const void *owner = nullptr);
    ~NestedSheet();
    NestedSheet(const NestedSheet &) = delete;
    NestedSheet &operator=(const NestedSheet &) = delete;
    const std::shared_ptr<const std::string> authored;
    const void *const owner;
    std::shared_ptr<const std::string> source;
    lxb_css_stylesheet_t *sheet = nullptr;
    std::unordered_set<lxb_css_rule_style_t *> expanded;
    std::shared_ptr<ComputedStyleCache> values;
};
class NestedStyles final {
  public:
    void begin();
    std::shared_ptr<NestedSheet> get(std::shared_ptr<const std::string> source, const void *owner = nullptr);
    void finish();

  private:
    std::vector<std::shared_ptr<NestedSheet>> cached_, next_;
    size_t bytes_ = 0;
};
} // namespace xl::detail
