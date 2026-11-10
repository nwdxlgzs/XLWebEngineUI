#pragma once
#include <XLWebEngineUI/xl.h>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace xl::detail {
struct UriFailure { xl_status status; };
// This scheme addresses only explicitly mounted memory. It has no network or
// filesystem fallback. Authority names are local to the selected Runtime.
std::string resolve_ui_uri(std::string_view base, std::string_view reference);
std::string ui_resource_key(std::string_view canonical);
bool ui_mime_is(std::string_view mime, std::string_view expected);
struct UiContent {
    std::vector<std::byte> bytes;
    std::string mime;
};
class UiMounts final {
public:
    void mount(std::string_view uri, std::shared_ptr<const UiContent> content);
    bool unmount(std::string_view uri);
    std::shared_ptr<const UiContent> open(std::string_view uri);
    void loader(xl_content_loader_fn callback, void* context) { loader_ = callback; context_ = context; }
    bool loading() const { return loading_; }
    uint64_t revision() const { return revision_; }
private:
    std::unordered_map<std::string,std::shared_ptr<const UiContent>> entries_;
    uint64_t revision_ = 1;
    xl_content_loader_fn loader_ = nullptr;
    void* context_ = nullptr;
    bool loading_ = false;
};
}

struct xl_content {
    std::shared_ptr<const xl::detail::UiContent> value;
};
