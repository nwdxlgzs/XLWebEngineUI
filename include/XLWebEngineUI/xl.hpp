#ifndef XLWEBENGINEUI_XL_HPP
#define XLWEBENGINEUI_XL_HPP
#include <XLWebEngineUI/xl.h>
#include <exception>
#include <stdexcept>
#include <utility>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace xl {
class Content final {
public:
    explicit Content(xl_content* handle=nullptr) noexcept:handle_(handle){}
    explicit Content(std::span<const std::byte> bytes,const char* mime=nullptr) {
        check(xl_content_create(bytes.data(),bytes.size(),mime,&handle_));
    }
    explicit Content(std::string_view text,const char* mime="text/plain;charset=utf-8") {
        check(xl_content_create(text.data(),text.size(),mime,&handle_));
    }
    ~Content(){xl_content_release(handle_);}
    Content(const Content& other) {if(other.handle_)check(xl_content_clone(other.handle_,&handle_));}
    Content& operator=(const Content& other){if(this!=&other){Content next(other);std::swap(handle_,next.handle_);}return *this;}
    Content(Content&& other) noexcept:handle_(std::exchange(other.handle_,nullptr)){}
    Content& operator=(Content&& other) noexcept {if(this!=&other){xl_content_release(handle_);handle_=std::exchange(other.handle_,nullptr);}return *this;}
    xl_content* get() const noexcept{return handle_;}
    std::span<const std::byte> data() const {
        const void* bytes;size_t size;const char* type;check(xl_content_data(handle_,&bytes,&size,&type));
        return {static_cast<const std::byte*>(bytes),size};
    }
    std::string_view mime() const {
        const void* bytes;size_t size;const char* type;check(xl_content_data(handle_,&bytes,&size,&type));return type;
    }
private:
    static void check(xl_status status){if(status!=XL_OK)throw std::runtime_error(xl_status_string(status));}
    xl_content* handle_=nullptr;
};
inline std::string resolve_ui_uri(const char* base,const char* reference) {
    size_t size;const auto status=xl_resolve_ui_uri(base,reference,nullptr,0,&size);
    if(status!=XL_OK && status!=XL_BUFFER_TOO_SMALL)throw std::runtime_error(xl_status_string(status));
    std::string output(size,'\0');
    if(xl_resolve_ui_uri(base,reference,output.data(),output.size(),&size)!=XL_OK)throw std::runtime_error("URI resolution failed");
    return output;
}
class ResourceLease final {
public:
    explicit ResourceLease(xl_resource_lease* handle = nullptr) noexcept : handle_(handle) {}
    ~ResourceLease() { xl_resource_lease_release(handle_); }
    ResourceLease(const ResourceLease&) = delete;
    ResourceLease& operator=(const ResourceLease&) = delete;
    ResourceLease(ResourceLease&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    ResourceLease& operator=(ResourceLease&& other) noexcept {
        if (this != &other) { xl_resource_lease_release(handle_); handle_ = std::exchange(other.handle_, nullptr); }
        return *this;
    }
    xl_resource_lease* get() const noexcept { return handle_; }
    const void* data() const {
        const void* bytes = nullptr;
        size_t size = 0;
        const auto status = xl_resource_lease_data(handle_, &bytes, &size);
        if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
        return bytes;
    }
    size_t size() const {
        const void* bytes = nullptr;
        size_t size = 0;
        const auto status = xl_resource_lease_data(handle_, &bytes, &size);
        if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
        return size;
    }
private:
    xl_resource_lease* handle_;
};

class ResourceRequest final {
public:
    explicit ResourceRequest(xl_resource_request* handle = nullptr) noexcept : handle_(handle) {}
    ~ResourceRequest() { xl_resource_request_release(handle_); }
    ResourceRequest(const ResourceRequest&) = delete;
    ResourceRequest& operator=(const ResourceRequest&) = delete;
    ResourceRequest(ResourceRequest&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    ResourceRequest& operator=(ResourceRequest&& other) noexcept {
        if (this != &other) { xl_resource_request_release(handle_); handle_ = std::exchange(other.handle_, nullptr); }
        return *this;
    }
    xl_status poll(ResourceLease& lease) const {
        xl_resource_lease* result = nullptr;
        const auto status = xl_resource_request_poll(handle_, &result);
        if (status == XL_OK) lease = ResourceLease(result);
        return status;
    }
    xl_status cancel() { return xl_resource_request_cancel(handle_); }
private:
    xl_resource_request* handle_;
};

class Runtime final {
public:
    explicit Runtime(const xl_runtime_options* options = nullptr) {
        const auto status = xl_runtime_create(options, &handle_);
        if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
    }
    ~Runtime() noexcept {
        // Destroying on another thread is a contract violation, never silently leak.
        if (handle_ && xl_runtime_destroy(handle_) != XL_OK) std::terminate();
    }
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    Runtime& operator=(Runtime&&) = delete;
    xl_runtime* get() const noexcept { return handle_; }
    void mount(const char* uri,const Content& content) {
        const auto status=xl_runtime_mount_content(handle_,uri,content.get());
        if(status!=XL_OK)throw std::runtime_error(xl_status_string(status));
    }
    void unmount(const char* uri) {
        const auto status=xl_runtime_unmount_content(handle_,uri);
        if(status!=XL_OK)throw std::runtime_error(xl_status_string(status));
    }
    Content content(const char* uri) {
        xl_content* result=nullptr;const auto status=xl_runtime_open_content(handle_,uri,&result);
        if(status!=XL_OK)throw std::runtime_error(xl_status_string(status));return Content(result);
    }
    void content_loader(xl_content_loader_fn loader,void* context=nullptr) {
        const auto status=xl_runtime_set_content_loader(handle_,loader,context);
        if(status!=XL_OK)throw std::runtime_error(xl_status_string(status));
    }
    ResourceRequest resource(uint32_t id) {
        xl_resource_request* request = nullptr;
        const auto status = xl_runtime_request_resource(handle_, id, &request);
        if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
        return ResourceRequest(request);
    }
private:
    xl_runtime* handle_ = nullptr;
};

class Document final {
public:
    explicit Document(Runtime& runtime) {
        check(xl_document_create(runtime.get(), &handle_));
    }
    ~Document() noexcept { if (handle_ && xl_document_destroy(handle_) != XL_OK) std::terminate(); }
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;
    Document(Document&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    Document& operator=(Document&&) = delete;
    xl_document* get() const noexcept { return handle_; }
    void html(std::string_view text) { check(xl_document_load_html(handle_, text.data(), text.size())); }
    void html(std::string_view text,const char* uri){check(xl_document_load_html_at(handle_,text.data(),text.size(),uri));}
    void load(const char* uri){check(xl_document_load_uri(handle_,uri));}
    std::string uri() const {
        size_t size;const auto status=xl_document_get_uri(handle_,nullptr,0,&size);
        if(status!=XL_BUFFER_TOO_SMALL)check(status);
        std::string result(size,'\0');check(xl_document_get_uri(handle_,result.data(),result.size(),&size));return result;
    }
    xl_node_id query(std::string_view selector) {
        xl_node_id node = 0;
        const auto status = xl_document_query_selector(handle_, selector.data(), selector.size(), &node);
        if (status != XL_NOT_FOUND) check(status);
        return node;
    }
    void attribute(xl_node_id node, const char* name, std::string_view value) {
        check(xl_document_set_attribute(handle_, node, name, value.data(), value.size()));
    }
    void text(xl_node_id node, std::string_view value) { check(xl_document_set_text(handle_, node, value.data(), value.size())); }
    bool matches_media(std::string_view query) const {
        uint32_t matches=0;check(xl_document_matches_media(handle_,query.data(),query.size(),&matches));return matches!=0;
    }
    void media_preferences(const xl_media_preferences* preferences) {
        check(xl_document_set_media_preferences(handle_,preferences,preferences?sizeof(*preferences):0));
    }
    std::string pseudo_style(xl_node_id node, const char* pseudo, const char* property) const {
        size_t size = 0; const auto status = xl_document_get_pseudo_style(handle_, node, pseudo, property, nullptr, 0, &size);
        if (status != XL_BUFFER_TOO_SMALL) check(status);
        std::string result(size, '\0'); check(xl_document_get_pseudo_style(handle_, node, pseudo, property, result.data(), result.size(), &size)); return result;
    }
    std::string value(xl_node_id node) const {
        size_t size = 0; const auto status = xl_document_get_value(handle_, node, nullptr, 0, &size);
        if (status != XL_BUFFER_TOO_SMALL) check(status);
        std::string result(size, '\0'); check(xl_document_get_value(handle_, node, result.data(), result.size(), &size)); return result;
    }
    void value(xl_node_id node, std::string_view value) { check(xl_document_set_value(handle_, node, value.data(), value.size())); }
    xl_edit_state edit_state(xl_node_id node) const {
        xl_edit_state state{}; check(xl_document_get_edit_state(handle_, node, &state, sizeof(state))); return state;
    }
    void edit_selection(xl_node_id node, uint32_t anchor, uint32_t focus) { check(xl_document_set_edit_selection(handle_, node, anchor, focus)); }
    xl_node_id create(std::string_view tag) {
        xl_node_id node = 0;
        check(xl_document_create_element(handle_, tag.data(), tag.size(), &node));
        return node;
    }
    void append(xl_node_id parent, xl_node_id child) { check(xl_document_append_child(handle_, parent, child)); }
    void remove(xl_node_id node) { check(xl_document_remove_node(handle_, node)); }
    xl_status evaluate(std::string_view code, uint32_t budget_ms = 0) { return xl_document_eval(handle_, code.data(), code.size(), budget_ms); }
    std::string result() const {
        size_t required = 0;
        auto status = xl_document_script_result(handle_, nullptr, 0, &required);
        if (status != XL_BUFFER_TOO_SMALL) check(status);
        std::string value(required, '\0');
        check(xl_document_script_result(handle_, value.data(), value.size(), &required));
        return value;
    }
    xl_document_stats stats() const {
        xl_document_stats result{};
        check(xl_document_get_stats(handle_, &result, sizeof(result)));
        return result;
    }
private:
    static void check(xl_status status) { if (status != XL_OK) throw std::runtime_error(xl_status_string(status)); }
    xl_document* handle_ = nullptr;
};

class AXUpdate final {
public:
    explicit AXUpdate(xl_ax_update* handle = nullptr) noexcept : handle_(handle) {}
    ~AXUpdate() { xl_ax_update_release(handle_); }
    AXUpdate(const AXUpdate& other) { if (other.handle_) check(xl_ax_update_clone(other.handle_, &handle_)); }
    AXUpdate& operator=(const AXUpdate& other) { if (this != &other) { AXUpdate copy(other); std::swap(handle_, copy.handle_); } return *this; }
    AXUpdate(AXUpdate&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    AXUpdate& operator=(AXUpdate&& other) noexcept {
        if (this != &other) { xl_ax_update_release(handle_); handle_ = std::exchange(other.handle_, nullptr); } return *this;
    }
    xl_ax_update* get() const noexcept { return handle_; }
    explicit operator bool() const noexcept { return handle_ != nullptr; }
    xl_ax_update_info info() const { xl_ax_update_info value{}; check(xl_ax_update_get_info(handle_, &value, sizeof(value))); return value; }
    xl_ax_node node(size_t index) const { xl_ax_node value{}; check(xl_ax_update_get_node(handle_, index, &value, sizeof(value))); return value; }
    xl_ax_event event(size_t index) const { xl_ax_event value{}; check(xl_ax_update_get_event(handle_, index, &value, sizeof(value))); return value; }
    std::string name(size_t index) const { return text(index, xl_ax_update_name); }
    std::string value(size_t index) const { return text(index, xl_ax_update_value); }
    std::string event_text(size_t index) const { return text(index, xl_ax_update_event_text); }
private:
    static void check(xl_status status) { if (status != XL_OK) throw std::runtime_error(xl_status_string(status)); }
    std::string text(size_t index, xl_status (*read)(const xl_ax_update*, size_t, char*, size_t, size_t*)) const {
        size_t size = 0; const auto status = read(handle_, index, nullptr, 0, &size);
        if (status != XL_BUFFER_TOO_SMALL) check(status);
        std::string result(size, '\0'); check(read(handle_, index, result.data(), size, &size)); return result;
    }
    xl_ax_update* handle_ = nullptr;
};

class View final {
public:
    explicit View(Runtime& runtime, const xl_view_options* options = nullptr) {
        const auto status = xl_view_create(runtime.get(), options, &handle_);
        if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
    }
    ~View() noexcept { if (handle_ && xl_view_destroy(handle_) != XL_OK) std::terminate(); }
    View(const View&) = delete;
    View& operator=(const View&) = delete;
    View(View&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    View& operator=(View&&) = delete;
    xl_view* get() const noexcept { return handle_; }
    void ax_updates(bool enabled) { check(xl_view_set_ax_updates(handle_, enabled ? 1 : 0)); }
    void native_accessibility(bool enabled) { check(xl_view_set_native_accessibility(handle_, enabled ? 1 : 0)); }
    bool take_ax_update(AXUpdate& update) {
        xl_ax_update* result = nullptr; const auto status = xl_view_take_ax_update(handle_, &result);
        if (status == XL_NOT_FOUND) return false;
        check(status); update = AXUpdate(result); return true;
    }
    void document(Document* value) {
        const auto status = xl_view_set_document(handle_, value ? value->get() : nullptr);
        if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
    }
    uint32_t render() {
        uint32_t flags = 0;
        const auto status = xl_view_render(handle_, &flags);
        if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
        return flags;
    }
    void rects(std::span<const xl_paint_rect> values) {
        const auto status = xl_view_set_rects(handle_, values.data(), values.size());
        if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
    }
    std::vector<xl_node_bounds> text_rects(xl_node_id node, uint32_t start, uint32_t end, bool dom_offsets = true) const {
        const auto query = dom_offsets ? xl_view_dom_text_rects : xl_view_text_rects;
        size_t count = 0;
        auto status = query(handle_, node, start, end, nullptr, 0, &count);
        if (status != XL_OK && status != XL_BUFFER_TOO_SMALL) throw std::runtime_error(xl_status_string(status));
        std::vector<xl_node_bounds> result(count);
        status = query(handle_, node, start, end, result.data(), result.size(), &count);
        if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
        return result;
    }
    xl_text_position hit_text(float x, float y) const {
        xl_text_position result{};
        const auto status = xl_view_hit_test_text(handle_, x, y, &result, sizeof(result));
        if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
        return result;
    }
    xl_view_stats stats() const {
        xl_view_stats stats{};
        const auto status = xl_view_get_stats(handle_, &stats, sizeof(stats));
        if (status != XL_OK) throw std::runtime_error(xl_status_string(status));
        return stats;
    }
    int64_t next_wakeup_ms() const {
        int64_t value = -1;
        check(xl_view_next_wakeup_ms(handle_, &value));
        return value;
    }
    void graphics_backend(xl_graphics_backend backend) { check(xl_view_set_graphics_backend(handle_, backend)); }
    xl_graphics_info graphics_info() const {
        xl_graphics_info value{};
        check(xl_view_get_graphics_info(handle_, &value, sizeof(value)));
        return value;
    }
    void abandon_graphics_backend() { check(xl_view_abandon_graphics_backend(handle_)); }
    void selection(const xl_text_position& anchor,const xl_text_position& focus) {
        const xl_text_selection value{sizeof(xl_text_selection),0,anchor,focus};
        check(xl_view_set_selection(handle_,&value,sizeof(value)));
    }
    void clear_selection() {check(xl_view_set_selection(handle_,nullptr,0));}
    void select_at(float x,float y,bool extend=false) {
        check(xl_view_select_at(handle_,x,y,extend?XL_SELECTION_EXTEND:0));
    }
    void selection_input(bool enabled) {check(xl_view_set_selection_input(handle_,enabled?1:0));}
    xl_scroll_info scroll(xl_node_id node = 0) const {
        xl_scroll_info value{}; check(xl_view_get_scroll(handle_, node, &value, sizeof(value))); return value;
    }
    void scroll_to(xl_node_id node, float x, float y) { check(xl_view_set_scroll(handle_, node, x, y)); }
    void scroll_at(float x, float y, float dx, float dy) { check(xl_view_scroll_at(handle_, x, y, dx, dy)); }
    void scroll_input(bool enabled) { check(xl_view_set_scroll_input(handle_, enabled ? 1:0)); }
    bool key(const xl_key_event& event) {
        uint32_t handled = 0; check(xl_view_key(handle_, &event, sizeof(event), &handled)); return handled != 0;
    }
    bool pointer(const xl_pointer_event& event) {
        uint32_t handled = 0; check(xl_view_pointer(handle_, &event, sizeof(event), &handled)); return handled != 0;
    }
    void keyboard_input(bool enabled) { check(xl_view_set_keyboard_input(handle_, enabled ? 1:0)); }
    void clipboard_handler(const xl_clipboard_handler* handler) {
        check(xl_view_set_clipboard_handler(handle_, handler, handler ? sizeof(*handler) : 0));
    }
    bool clipboard(xl_clipboard_command command) {
        uint32_t handled = 0; check(xl_view_clipboard(handle_, command, &handled)); return handled != 0;
    }
    bool text_input(std::string_view text) {
        uint32_t handled = 0; check(xl_view_text_input(handle_, text.data(), text.size(), &handled)); return handled != 0;
    }
    bool composition(const xl_composition& value) {
        uint32_t handled = 0; check(xl_view_set_composition(handle_, &value, sizeof(value), &handled)); return handled != 0;
    }
    xl_text_selection selection() const {
        xl_text_selection value{};check(xl_view_get_selection(handle_,&value,sizeof(value)));return value;
    }
    std::string selected_text() const {
        size_t size=0;const auto status=xl_view_selected_text(handle_,nullptr,0,&size);
        if(status!=XL_BUFFER_TOO_SMALL)check(status);
        std::string value(size,'\0');check(xl_view_selected_text(handle_,value.data(),size,&size));return value;
    }
    xl_node_bounds caret(const xl_text_position& position) const {
        xl_node_bounds value{};check(xl_view_caret_rect(handle_,&position,&value));return value;
    }
    std::vector<xl_node_bounds> selection_rects() const {
        size_t size=0;const auto status=xl_view_selection_rects(handle_,nullptr,0,&size);
        if(status!=XL_BUFFER_TOO_SMALL)check(status);
        std::vector<xl_node_bounds> value(size);
        check(xl_view_selection_rects(handle_,value.data(),size,&size));return value;
    }
private:
    static void check(xl_status status) {if(status!=XL_OK)throw std::runtime_error(xl_status_string(status));}
    xl_view* handle_ = nullptr;
};
}
#endif
