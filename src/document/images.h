#pragma once
#include "graphics/image_content.h"
#include "resources/ui_content.h"
#include <deque>
namespace xl::detail {
class DocumentImages final {
  struct Entry {
    std::shared_ptr<const UiContent> content;
    std::string source, base;
    std::shared_ptr<ImageContent> value;
    size_t bytes;
  };
  std::deque<Entry> entries_;
  size_t bytes_ = 0;
  uint64_t revision_ = 0, mount_revision_ = 0;
  std::unordered_map<xl_node_id, std::shared_ptr<ImageContent>> nodes_;

public:
  void trim();
  std::shared_ptr<ImageContent> open(std::shared_ptr<const UiContent>,
                                     std::string source, std::string base,
                                     UiMounts *);
  std::shared_ptr<ImageContent>
  node(xl_node_id id, uint64_t revision, uint64_t mounts,
       const std::function<std::shared_ptr<ImageContent>()> &load);
};
} // namespace xl::detail
