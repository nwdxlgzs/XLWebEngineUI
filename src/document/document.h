#pragma once
#include "base/view_environment.h"
#include <XLWebEngineUI/xl.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <optional>
#include <functional>
#include "text/paragraph.h"
#include "graphics/box_paint.h"
#include "graphics/outline_paint.h"
#include "graphics/display_list.h"
#include "resources/ui_content.h"

namespace xl::detail {
struct DocumentFailure { xl_status status; };
struct AXEntry { xl_ax_node node{}; std::string name; std::string value{}; };
struct AXLivePart {
    xl_node_id node = 0, atomic = 0;
    uint32_t relevant = 3; // additions=1, text=2, removals=4
    bool busy = false;
    std::string text;
};
struct AXLiveRegion {
    xl_node_id node = 0;
    uint32_t politeness = 0;
    bool busy = false, alert = false;
    std::vector<AXLivePart> parts;
};
using ScrollOffsets = std::unordered_map<xl_node_id, std::pair<float, float>>;
struct FrameScroll {
    xl_scroll_info info{};
    xl_node_bounds port{};
    Clip clip{};
    xl_node_id parent = 0;
};
class FrameProjection;
struct BoxStyle;
struct PositionedBox {
    xl_node_id node = 0, containing = 0;
    std::shared_ptr<const BoxStyle> inherited;
    xl_node_bounds anchor{};
    size_t depth = 0;
    bool fixed = false, rtl = false, area = false, grid_area = false;
    float align_x = 0, align_y = 0;
    std::array<float,4> grid_edges{};
};
struct InlineContainingBox {
    xl_node_bounds first{}, last{};
    bool rtl = false;
};
struct KeyResult { xl_node_id target = 0; bool allowed = true; };
struct DocumentFrame {
    xl_node_id identity = 0;
    xl_node_id focused = 0;
    bool focus_visible = false;
    std::optional<xl_edit_state> edit;
    uint64_t revision = 0;
    uint64_t canvas_revision = 0;
    int width = 0, height = 0;
    std::vector<BoxPaint> paint;
    std::vector<TextItem> texts;
    std::vector<OutlinePaint> outlines;
    std::vector<PaintCommand> display;
    std::unordered_map<xl_node_id,PaintNode> paint_nodes;
    std::unordered_map<xl_node_id,xl_node_id> paint_parents;
    std::unordered_map<xl_node_id,size_t> paint_ranks;
    std::unordered_map<xl_node_id, size_t> outline_nodes;
    std::unordered_map<xl_node_id, std::pair<size_t, size_t>> text_nodes;
    std::vector<std::pair<size_t, size_t>> text_order; // DOM order, independent of paragraph/paint storage.
    std::unordered_map<xl_node_id, xl_node_bounds> boxes;
    std::unordered_map<xl_node_id, xl_node_bounds> disclosure_legends;
    std::unordered_map<xl_node_id, Clip> box_clips;
    std::unordered_map<xl_node_id,std::vector<RoundedRect>> hit_shapes;
    std::unordered_map<xl_node_id, FrameScroll> scrolls;
    std::vector<xl_node_id> scroll_order;
    // Layout-only normal-flow extents retained when relative positioning moves
    // visible geometry. Consumed by the nearest clipping/scrolling boundary.
    std::vector<xl_node_bounds> flow_bounds;
    // Deferred layout only: never capture intermediate ancestor scroll/clip.
    std::vector<PositionedBox> positioned;
    std::unordered_map<xl_node_id, InlineContainingBox> inline_containing;
    std::vector<AXEntry> accessibility;
    std::vector<AXLiveRegion> live_regions;
    uint64_t unsupported = 0;
    std::shared_ptr<FrameProjection> projection;
    void capture_projection();
    void include_projection(xl_node_id id,xl_node_bounds bounds);
    void merge_projection(DocumentFrame& child,bool embedded=false);
    void scroll(xl_node_id id,float x,float y);
    void fix_paint(size_t index);
    DocumentFrame project(const ScrollOffsets& offsets) const;
    void move(float x, float y,bool record=true);
    void clip(const Clip& clip);
    xl_node_bounds visible_bounds(xl_node_id id) const;
    xl_node_id hit_paint(float x,float y,const std::function<bool(xl_node_id)>& accept={}) const;
    void outline(xl_node_id node, xl_node_bounds fragment, const OutlineStyle& style,CornerRadii radii={});
};
class Document final {
public:
    struct Impl;
    explicit Document(UiMounts* mounts = nullptr, std::function<std::shared_ptr<void>()> prepare_text = {});
    ~Document();
    void load(std::string_view html, std::string_view uri = "xl-webengine-ui://app/");
    void load_uri(std::string_view uri);
    const std::string& uri() const;
    void add_font(const char* family, const void* data, size_t size);
    void text_session(std::shared_ptr<void> session);
    bool has_text() const;
    xl_node_id query(std::string_view selector);
    xl_node_id create(std::string_view tag);
    void append(xl_node_id parent, xl_node_id child);
    void remove(xl_node_id node);
    void attribute(xl_node_id node, std::string_view name, std::string_view value);
    std::string attribute(xl_node_id node, std::string_view name) const;
    void text(xl_node_id node, std::string_view value);
    std::string text(xl_node_id node) const;
    std::string style(xl_node_id node, std::string_view name) const;
    std::string pseudo_style(xl_node_id node, std::string_view pseudo, std::string_view name);
    xl_status evaluate(std::string_view code, uint32_t budget_ms);
    const std::string& result() const;
    void run_tasks();
    bool tasks_ready() const;
    int64_t next_task_delay() const;
    void environment(ViewEnvironment value);
    void media_preferences(const xl_media_preferences* preferences);
    bool matches_media(std::string_view query) const;
    void action(xl_node_id node, uint32_t action);
    void accessibility_value(xl_node_id node, std::string_view value);
    uint64_t revision() const;
    uint64_t canvas_revision() const;
    bool canvas_has_text() const;
    void refresh_canvases(DocumentFrame& frame);
    xl_node_id identity() const;
    xl_node_id focused() const;
    uint64_t focus_epoch() const;
    void pointer_input();
    xl_node_id pointer_target(const DocumentFrame& frame, float x, float y) const;
    void pointer_focus(xl_node_id node);
    void pointer_click(xl_node_id node);
    xl_node_id focus_next(bool reverse);
    KeyResult key(const xl_key_event& event);
    bool keyboard_activate(xl_node_id node, bool space, bool invoke);
    bool text_control(xl_node_id node) const;
    bool editable_field(xl_node_id node) const;
    std::vector<xl_node_id> ancestors(xl_node_id node) const;
    std::string value(xl_node_id node) const;
    void value(xl_node_id node, std::string_view value);
    xl_edit_state edit_state(xl_node_id node) const;
    void edit_selection(xl_node_id node, uint32_t anchor, uint32_t focus);
    bool edit_key(const xl_key_event& event, const DocumentFrame* frame);
    bool text_input(std::string_view text);
    bool composition(const xl_composition& composition);
    bool clipboard(uint32_t command, const std::function<std::string()>& read,
                   const std::function<void(std::string_view)>& write,
                   std::string_view selection = {}, xl_node_id selection_node = 0);
    xl_document_stats stats() const;
    DocumentFrame layout(int width, int height, const ScrollOffsets& scrolls);
private:
    std::unique_ptr<Impl> impl_;
};
xl_status copy_string(std::string_view text, char* output, size_t capacity, size_t* required);
}
