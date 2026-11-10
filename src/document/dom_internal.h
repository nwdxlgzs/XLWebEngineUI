#pragma once
#include "document.h"
#include <lexbor/html/html.h>
#include <lexbor/style/style.h>
#include <lexbor/selectors/selectors.h>
#include <quickjs.h>
#include <chrono>
#include <unordered_set>
#include <array>
#include "text/edit_model.h"

namespace xl::detail {
class VariableStyles;
class PseudoElements;
class MediaQuery;
class NestedStyles;
class DocumentImages;
class CanvasContent;
enum class PseudoKind : unsigned;
struct Document::Impl {
    lxb_html_document_t* dom = nullptr;
    std::unordered_map<xl_node_id, lxb_dom_node_t*> nodes;
    std::unordered_map<lxb_dom_node_t*, xl_node_id> identities;
    std::unordered_set<lxb_dom_node_t*> detached;
    uint64_t revision = 1, layouts = 0, unsupported = 0;
    uint64_t canvas_revision=1;
    xl_node_id focused = 0;
    uint64_t focus_epoch = 0;
    bool keyboard_modality = true, focus_visible = false;
    int viewport_width = 640, viewport_height = 480;
    ViewEnvironment environment;
    ViewEnvironment native_environment;
    std::vector<std::pair<std::shared_ptr<MediaQuery>,bool>> style_media;
    std::vector<std::pair<std::shared_ptr<MediaQuery>,bool>> link_media;
    struct MediaWatch {std::shared_ptr<MediaQuery> query;JSValue callback;bool last=false,pending=false;uint64_t order=0;};
    std::unordered_map<uint32_t,MediaWatch> media_watches;
    uint32_t next_media_watch=1;
    bool resize_pending=false,resize_observed=false;
    uint64_t resize_order=0;
    bool visibility_pending=false;
    uint64_t visibility_order=0;
    std::optional<xl_media_preferences> media_preferences;
    std::unordered_map<xl_node_id, bool> checked_values;
    std::unordered_set<xl_node_id> activating;
    std::unordered_map<xl_node_id, Editor> editors;
    JSRuntime* js = nullptr;
    JSContext* context = nullptr;
    JSValue dispatcher = JS_UNDEFINED;
    std::string script_result;
    std::chrono::steady_clock::time_point deadline;
    bool timed_out = false;
    bool executing = false;
    struct ScriptTimer { JSValue callback; std::chrono::steady_clock::time_point due; uint32_t delay; bool repeat, animation; uint64_t order; };
    std::unordered_map<uint32_t, ScriptTimer> timers;
    uint32_t next_timer = 1;
    struct DetailsToggle { bool old_open, new_open; std::chrono::steady_clock::time_point due; uint64_t order; };
    std::unordered_map<xl_node_id, DetailsToggle> details_toggles;
    uint64_t next_task_order = 1;
    const std::chrono::steady_clock::time_point time_origin = std::chrono::steady_clock::now();
    uint64_t styled_revision = 0;
    std::unique_ptr<VariableStyles> variables;
    std::unique_ptr<NestedStyles> nested_styles;
    std::unique_ptr<PseudoElements> pseudos;
    std::unordered_map<const lxb_css_rule_t*, std::array<size_t, 2>> style_order;
    std::shared_ptr<void> text_session;
    std::function<std::shared_ptr<void>()> text_preparer;
    std::unique_ptr<TextEngine> text_engine;
    std::unique_ptr<DocumentImages> images;
    std::unordered_map<xl_node_id,std::unique_ptr<CanvasContent>> canvases;
    UiMounts* mounts = nullptr; // Runtime owns it and outlives this document.
    std::string uri = "xl-webengine-ui://app/";
    uint64_t content_revision = 0;
    std::unordered_map<lxb_css_stylesheet_t*, std::shared_ptr<const UiContent>> linked_sheets;
    Impl();
    ~Impl();
    xl_node_id identify(lxb_dom_node_t* node);
    lxb_dom_node_t* resolve(xl_node_id id) const;
    xl_node_id query(std::string_view selector, lxb_dom_node_t* root = nullptr);
    std::vector<xl_node_id> query_all(std::string_view selector, lxb_dom_node_t* root = nullptr);
    xl_node_id by_id(std::string_view name);
    void set_attribute(xl_node_id id, std::string_view name, std::string_view value);
    void remove_attribute(xl_node_id id, std::string_view name);
    std::string attribute(xl_node_id id, std::string_view name, bool required = true) const;
    void set_text(xl_node_id id, std::string_view text);
    std::string text(xl_node_id id) const;
    std::string style(xl_node_id id, std::string_view name) const;
    std::string pseudo_style(xl_node_id id, std::string_view pseudo, std::string_view name);
    std::vector<lxb_dom_node_t*> render_children(lxb_dom_node_t* node) const;
    std::string accessible_text(lxb_dom_node_t* node) const;
    xl_node_id create(std::string_view tag);
    void append(xl_node_id parent, xl_node_id child);
    void insert_before(xl_node_id parent,xl_node_id child,xl_node_id reference);
    void remove(xl_node_id child);
    bool connected(lxb_dom_node_t* node) const;
    void refresh_styles();
    void update_environment(ViewEnvironment value);
    uint32_t watch_media(std::shared_ptr<MediaQuery> query,JSValueConst callback);
    void unwatch_media(uint32_t id);
    void sync_linked_styles(const std::vector<lxb_dom_element_t*>& elements);
    std::string base_uri();
    bool image_node(lxb_dom_node_t*) const;
    std::shared_ptr<class ImageContent> image_content(lxb_dom_node_t*);
    CanvasContent& canvas(xl_node_id);
    std::pair<uint32_t,uint32_t> canvas_size(xl_node_id) const;
    void canvas_reset(xl_node_id);
    void render_canvases();
    void ensure_canvas_text();
    void canvas_changed();
    void initialize_script();
    void close_script();
    xl_status evaluate(std::string_view code, uint32_t budget);
    uint32_t schedule(JSValueConst callback, double delay, unsigned kind);
    void cancel_timer(uint32_t id);
    void run_tasks();
    bool tasks_ready() const;
    double time_now() const;
    bool disabled(lxb_dom_node_t* node) const;
    bool disabled_ancestor(lxb_dom_node_t* node) const;
    bool inert(lxb_dom_node_t* node) const;
    bool focusable(lxb_dom_node_t* node, bool sequential = false);
    bool set_focus(xl_node_id id);
    bool checked(xl_node_id id) const;
    void checked(xl_node_id id, bool value);
    unsigned checkable(lxb_dom_node_t* node) const;
    std::vector<lxb_dom_node_t*> radio_group(lxb_dom_node_t* node);
    lxb_dom_node_t* details_summary(lxb_dom_node_t* node) const;
    lxb_dom_node_t* disclosure_owner(lxb_dom_node_t* node) const;
    bool details_open(lxb_dom_node_t* node) const;
    bool details_hidden(lxb_dom_node_t* node) const;
    void details_attribute(lxb_dom_node_t* node, std::string_view name, bool was_open);
    void details_inserted(lxb_dom_node_t* root, bool parsed = false, bool include_root = true);
    void details_exclusive(lxb_dom_node_t* node, bool close_others);
    bool text_field(lxb_dom_node_t* node) const;
    Editor& editor(xl_node_id node);
    bool read_only(xl_node_id node) const;
    xl_edit_state edit_state(xl_node_id node);
    void edit_value(xl_node_id node, std::string_view value);
    void edit_selection(xl_node_id node, uint32_t anchor, uint32_t focus);
    bool replace_edit(xl_node_id node, std::string_view text, std::string_view input_type,
                      bool composing = false, bool history = true,
                      std::optional<std::pair<uint32_t, uint32_t>> range = {});
    void edit_blur(xl_node_id node);
    void edit_focus(xl_node_id node);
    bool dispatch(xl_node_id id, const char* type, const xl_key_event* key = nullptr,
                  xl_node_id related = 0, bool bubbles = true, bool cancelable = true, bool trusted = true,
                  const TextEvent* text = nullptr, ClipboardEvent* clipboard = nullptr,
                  const DetailsToggle* toggle = nullptr);
    void activate(xl_node_id id, bool trusted = false);
};
inline const lxb_char_t* chars(std::string_view value) {
    return reinterpret_cast<const lxb_char_t*>(value.data());
}
inline void check_lxb(lxb_status_t status) {
    if (status != LXB_STATUS_OK) throw DocumentFailure{status == LXB_STATUS_ERROR_MEMORY_ALLOCATION ? XL_OUT_OF_MEMORY : XL_INTERNAL_ERROR};
}
inline lxb_dom_element_t* element(lxb_dom_node_t* node) {
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) throw DocumentFailure{XL_INVALID_ARGUMENT};
    return lxb_dom_interface_element(node);
}
DocumentFrame block_layout(Document::Impl& doc, int width, int height, const ScrollOffsets& scrolls);
void build_accessibility(Document::Impl& doc, DocumentFrame& frame);
uint32_t accessibility_role(Document::Impl& doc, lxb_dom_node_t* node);
bool accessibility_disabled(Document::Impl& doc, lxb_dom_node_t* node);
}
