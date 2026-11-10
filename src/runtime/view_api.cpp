#include "runtime_internal.h"
#include "document_internal.h"
#include "graphics/raster_view.h"
#include "platform/desktop.h"
#include "text/selection.h"
#include "text/edit_model.h"
#include "accessibility/updates.h"
#include "accessibility/native.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <vector>
#include <utility>
#include <unicode/utf8.h>

using namespace xl::detail;
struct xl_view {
    xl_runtime* runtime = nullptr;
    std::unique_ptr<DesktopWindow> window;
    bool owns_window = false;
    std::unique_ptr<RasterView> raster;
    std::vector<unsigned char> staging;
    bool active = false, present_pending = false;
    uint64_t presents = 0, skipped = 0, uploaded = 0;
    xl_document* document = nullptr;
    std::unique_ptr<DocumentFrame> document_frame;
    AXUpdates ax_updates;
    AXUpdates native_updates;
    std::unique_ptr<NativeAX> native_ax;
    uint64_t ax_generation = 0;
    std::optional<xl_text_selection> selection;
    ScrollOffsets scrolls;
    bool scroll_dirty = false;
    xl_node_id space_target = 0, reveal_target = 0;
    struct KeyScroll { xl_node_id target; std::string key; bool shift; };
    std::vector<KeyScroll> key_scrolls;
    xl_node_id input_identity = 0;
    xl_node_id focus_snapshot = 0;
    uint64_t space_epoch = 0;
    std::vector<xl_node_id> pointer_path;
    float pointer_x = 0, pointer_y = 0;
    bool pointer_dragged = false, pointer_selecting = false;
    uint64_t edit_snapshot = 0;
    bool reveal_edit = false;
    std::optional<xl_clipboard_handler> clipboard;
    ~xl_view() { if (document) --document->views; }
};

namespace {
struct RuntimeCall {
    explicit RuntimeCall(xl_runtime* runtime) : runtime(runtime) { ++runtime->view_calls; }
    ~RuntimeCall() { --runtime->view_calls; }
    xl_runtime* runtime;
};
template<class Function> xl_status boundary(Function&& function) {
    try { return function(); }
    catch (const DesktopFailure& error) { return error.status; }
    catch (const DocumentFailure& error) { return error.status; }
    catch (const std::bad_alloc&) { return XL_OUT_OF_MEMORY; }
    catch (const std::invalid_argument&) { return XL_INVALID_ARGUMENT; }
    catch (...) { return XL_INTERNAL_ERROR; }
}
template<class Function> xl_status view_call(const xl_view* constant, Function&& function) {
    if (!constant) return XL_INVALID_ARGUMENT;
    auto* view = const_cast<xl_view*>(constant);
    const auto status = xl_runtime_check_thread(view->runtime);
    if (status != XL_OK) return status;
    if (view->active || view->runtime->view_calls || (view->window && DesktopWindow::busy())) return XL_BUSY;
    return boundary([&] {
        RuntimeCall call(view->runtime);
        struct Active {
            explicit Active(bool& value) : value(value) { value = true; }
            ~Active() { value = false; }
            bool& value;
        } active(view->active);
        return function(*view);
    });
}
bool valid_size(int width, int height) {
    return width > 0 && height > 0 && width <= std::numeric_limits<int>::max() / 4;
}
bool matching_document_layout(const xl_view& view) {
    return view.document_frame && view.document_frame->revision == view.document->core->revision()
        && view.document_frame->width == view.raster->width() && view.document_frame->height == view.raster->height()
        && view.raster->ready();
}
bool current_document_layout(const xl_view& view) { return !view.scroll_dirty && matching_document_layout(view); }
bool current_document_frame(const xl_view& view) {
    return current_document_layout(view) && view.document_frame->canvas_revision==view.document->core->canvas_revision() && !view.raster->dirty();
}
void publish_accessibility(xl_view& view) {
    view.ax_updates.publish(view.document_frame.get(), view.selection, view.ax_generation);
    if (!view.native_ax) return;
    view.native_updates.publish(view.document_frame.get(), view.selection, view.ax_generation);
    const auto status = view.native_updates.pending();
    if (status == XL_OK) {
        if (!view.native_ax->publish(view.native_updates.take(), view.raster->width(), view.raster->height())) {
            view.native_updates.enable(false); view.native_updates.enable(true);
        }
    } else if (status != XL_NOT_FOUND) {
        // Do not expose a stale tree after the native stream rejects a frame.
        view.native_ax->publish({}, view.raster->width(), view.raster->height());
    }
    view.native_ax->refresh_focus();
}
struct NativeRequests {
    std::vector<std::shared_ptr<NativeAXRequest>> requests;
    ~NativeRequests() {
        for (const auto& request : requests) {
            std::lock_guard lock(request->mutex); request->done = true; request->changed.notify_all();
        }
    }
};
void native_requests(xl_view& view, NativeRequests& batch) {
    if (!view.native_ax) return;
    batch.requests = view.native_ax->take_requests();
    for (const auto& request : batch.requests) {
        { std::lock_guard lock(request->mutex); if (request->cancelled) continue; request->started = true; }
        const auto result = boundary([&] {
            if (!view.document || view.document->core->identity() != request->document) return XL_NOT_FOUND;
            if (view.document->active) return XL_BUSY;
            if (request->action == NativeAXAction::focus && !request->node) return view.native_ax->focus_window();
            if (request->action == NativeAXAction::value) view.document->core->accessibility_value(request->node, request->text);
            else if (request->action == NativeAXAction::expand || request->action == NativeAXAction::collapse)
                view.document->core->action(request->node, request->action == NativeAXAction::expand ? XL_AX_ACTION_EXPAND : XL_AX_ACTION_COLLAPSE);
            else view.document->core->action(request->node, request->action == NativeAXAction::focus ? XL_AX_ACTION_FOCUS : XL_AX_ACTION_ACTIVATE);
            if (request->action == NativeAXAction::focus) return view.native_ax->focus_window();
            return XL_OK;
        });
        std::lock_guard lock(request->mutex); request->status = result;
    }
}
void refresh_selection(xl_view& view) {
    std::vector<xl_paint_rect> overlays;
    if(view.selection && view.document_frame) {
        try {
            validate_selection(*view.document_frame,*view.selection);
            const auto& anchor=view.selection->anchor;
            const auto& focus=view.selection->focus;
            if(anchor.node==focus.node && anchor.offset_utf16==focus.offset_utf16) {
                const auto caret=caret_bounds(*view.document_frame,focus);
                if (nonempty(caret)) overlays.push_back({caret.x,caret.y,caret.width,caret.height,0xff173858,0});
            } else {
                for(const auto rect:selection_ranges(*view.document_frame,*view.selection))
                    overlays.push_back({rect.x,rect.y,rect.width,rect.height,0x663377ff,0});
            }
        } catch(const DocumentFailure& error) {
            if(error.status!=XL_NOT_FOUND && error.status!=XL_INVALID_ARGUMENT)throw;
            view.selection.reset();
        }
    }
    if (view.document_frame && view.document_frame->edit) {
        const auto& edit = *view.document_frame->edit;
        const xl_text_position anchor{sizeof(xl_text_position), edit.anchor_utf16, edit.node, XL_TEXT_DOWNSTREAM, 0};
        const xl_text_position focus{sizeof(xl_text_position), edit.focus_utf16, edit.node, XL_TEXT_DOWNSTREAM, 0};
        if (edit.anchor_utf16 != edit.focus_utf16) {
            const xl_text_selection range{sizeof(xl_text_selection), 0, anchor, focus};
            for (const auto& rect : selection_ranges(*view.document_frame, range))
                overlays.push_back({rect.x, rect.y, rect.width, rect.height, 0x663377ff, 0});
        } else {
            const auto caret = caret_bounds(*view.document_frame, focus);
            if (nonempty(caret)) overlays.push_back({caret.x, caret.y, caret.width, caret.height, 0xff173858, 0});
        }
        if (edit.flags & XL_EDIT_COMPOSING) {
            auto first = anchor, last = focus;
            first.offset_utf16 = edit.composition_start_utf16; last.offset_utf16 = edit.composition_end_utf16;
            const xl_text_selection range{sizeof(xl_text_selection), 0, first, last};
            for (const auto& rect : selection_ranges(*view.document_frame, range))
                overlays.push_back({rect.x, rect.y + std::max(0.0f, rect.height - 1), rect.width, 1, 0xff175bc2, 0});
        }
    }
    view.raster->overlays(overlays);
    ++view.ax_generation;
}
void select_at(xl_view& view,float x,float y,bool extend) {
    const auto focus=hit_text(*view.document_frame,x,y);
    const auto& text = view.document_frame->texts[view.document_frame->text_nodes.at(focus.node).first];
    view.document->core->pointer_input();
    if (text.control) {
        const auto state = view.document->core->edit_state(focus.node);
        view.document->core->action(focus.node, XL_AX_ACTION_FOCUS);
        view.document->core->edit_selection(focus.node, extend ? state.anchor_utf16 : focus.offset_utf16, focus.offset_utf16);
        view.selection.reset(); return;
    }
    const auto anchor=extend && view.selection?view.selection->anchor:focus;
    const auto previous=view.selection;
    view.selection=xl_text_selection{sizeof(xl_text_selection),0,anchor,focus};
    try {refresh_selection(view);}
    catch(...) {view.selection=previous;throw;}
}
bool scroll_offset(xl_view& view, xl_node_id node, const xl_scroll_info& info, double x, double y) {
    const std::pair<float, float> next{float(std::clamp(x, double(info.min_x), double(info.max_x))),
                                      float(std::clamp(y, double(info.min_y), double(info.max_y)))};
    const auto found = view.scrolls.find(node);
    const auto previous = found == view.scrolls.end() ? std::pair{0.0f, 0.0f} : found->second;
    if (previous == next) return false;
    if (!next.first && !next.second) view.scrolls.erase(node);
    else view.scrolls[node] = next;
    view.scroll_dirty = true;
    return true;
}
void scroll_at(xl_view& view, float x, float y, float delta_x, float delta_y) {
    const auto& frame = *view.document_frame;
    xl_node_id chosen = 0;
    bool found_port = false;
    const auto target=view.document->core->pointer_target(frame,x,y);
    auto chain=target?view.document->core->ancestors(target):std::vector<xl_node_id>{};chain.push_back(0);
    for(const auto id:chain) {
        if(!frame.scrolls.contains(id))continue;
        const auto& scroll = frame.scrolls.at(id);
        const auto visible = scroll.clip.bounds(scroll.port);
        if (!scroll.clip.contains(x,y)||x < visible.x || x >= visible.x + visible.width || y < visible.y || y >= visible.y + visible.height) continue;
        chosen=id;found_port=true;break;
    }
    if (!found_port) return;
    double dx = delta_x, dy = delta_y;
    for (;;) {
        const auto& scroll = frame.scrolls.at(chosen);
        const auto& info = scroll.info;
        const auto found = view.scrolls.find(chosen);
        const auto offset = found == view.scrolls.end() ? std::pair{0.0f, 0.0f} : found->second;
        const auto nx = info.flags & XL_SCROLL_USER_X ? std::clamp(offset.first + dx, double(info.min_x), double(info.max_x)) : offset.first;
        const auto ny = info.flags & XL_SCROLL_USER_Y ? std::clamp(offset.second + dy, double(info.min_y), double(info.max_y)) : offset.second;
        scroll_offset(view, chosen, info, nx, ny);
        dx -= nx - offset.first; dy -= ny - offset.second;
        if (!chosen || (!dx && !dy)) break;
        chosen = scroll.parent;
    }
}
bool key_scroll(xl_view& view, xl_node_id target, std::string_view key, bool shift) {
    auto chain = view.document->core->ancestors(target);
    chain.push_back(0);
    for (const auto id : chain) {
        const auto found = view.document_frame->scrolls.find(id);
        if (found == view.document_frame->scrolls.end()) continue;
        const auto& info = found->second.info;
        const auto existing = view.scrolls.find(id);
        const auto [x, y] = existing == view.scrolls.end() ? std::pair{0.0f, 0.0f} : existing->second;
        double nx = x, ny = y;
        const auto page = std::max(1.0f, info.client_height * .875f);
        if (info.flags & XL_SCROLL_USER_X) {
            if (key == "ArrowLeft") nx -= 40;
            if (key == "ArrowRight") nx += 40;
        }
        if (info.flags & XL_SCROLL_USER_Y) {
            if (key == "ArrowUp") ny -= 40;
            if (key == "ArrowDown") ny += 40;
            if (key == "PageUp" || (key == " " && shift)) ny -= page;
            if (key == "PageDown" || (key == " " && !shift)) ny += page;
            if (key == "Home") ny = info.min_y;
            if (key == "End") ny = info.max_y;
        }
        if (scroll_offset(view, id, info, nx, ny)) return true;
    }
    return false;
}
void reveal_bounds(xl_view& view, xl_node_id target, xl_node_bounds bounds, bool own) {
    const auto& frame = *view.document_frame;
    auto chain = view.document->core->ancestors(target);
    chain.push_back(0);
    const auto nearest = [](float start, float length, float port, float extent) {
        if (start < port && start + length > port + extent) return 0.0f;
        if (start < port) return start - port;
        if (start + length > port + extent) return start + length - port - extent;
        return 0.0f;
    };
    for (const auto id : chain) {
        if (id == target && !own) continue;
        const auto scroll = frame.scrolls.find(id);
        if (scroll == frame.scrolls.end()) continue;
        const auto& info = scroll->second.info;
        const auto& port = scroll->second.port;
        const auto offset = view.scrolls.find(id);
        const auto [x, y] = offset == view.scrolls.end() ? std::pair{0.0f, 0.0f} : offset->second;
        const auto nx = std::clamp(x + nearest(bounds.x, bounds.width, port.x, port.width), info.min_x, info.max_x);
        const auto ny = std::clamp(y + nearest(bounds.y, bounds.height, port.y, port.height), info.min_y, info.max_y);
        scroll_offset(view, id, info, nx, ny);
        bounds.x -= nx - x; bounds.y -= ny - y;
    }
}
void reveal_focus(xl_view& view, xl_node_id target) {
    const auto found = view.document_frame->boxes.find(target);
    if (found != view.document_frame->boxes.end()) reveal_bounds(view, target, found->second, false);
}
xl_node_bounds edit_caret(const DocumentFrame& frame, bool visible) {
    if (!frame.edit) return {};
    const auto& edit = *frame.edit;
    const auto [item, source] = frame.text_nodes.at(edit.node);
    const auto& text = frame.texts[item];
    auto rect = text.paragraph->caret(text.sources[source].offsets[edit.focus_utf16], true);
    rect.x += text.x; rect.y += text.y;
    return visible ? text.clip.bounds(rect) : rect;
}
void navigation_input(xl_view& view) {
    const auto identity = view.document->core->identity();
    if (view.input_identity == identity) return;
    view.scrolls.clear(); view.key_scrolls.clear(); view.reveal_target = view.space_target = 0;
    view.input_identity = identity;
    view.pointer_path.clear();
}
bool pointer_input(xl_view& view, const xl_pointer_event& event) {
    navigation_input(view);
    auto& doc = *view.document->core;
    if (event.type == XL_POINTER_CANCEL) { view.pointer_path.clear(); return true; }
    if (!current_document_layout(view)) throw DocumentFailure{XL_BUSY};
    const auto target = doc.pointer_target(*view.document_frame, event.x, event.y);
    if (event.type == XL_POINTER_DOWN) {
        view.pointer_path = target ? doc.ancestors(target) : std::vector<xl_node_id>{};
        view.pointer_x = event.x; view.pointer_y = event.y; view.pointer_dragged = false;
        view.pointer_selecting = false;
        if (!(event.modifiers & XL_MOD_SHIFT) && view.selection) { view.selection.reset(); refresh_selection(view); }
        if (target) try {
            const auto position = hit_text(*view.document_frame, event.x, event.y);
            const auto text_path = doc.ancestors(position.node);
            view.pointer_selecting = std::find(text_path.begin(), text_path.end(), target) != text_path.end();
        } catch (const DocumentFailure& error) { if (error.status != XL_NOT_FOUND) throw; }
        doc.pointer_focus(target);
    } else if (!view.pointer_path.empty()) {
        const auto dx = event.x - view.pointer_x, dy = event.y - view.pointer_y;
        view.pointer_dragged = view.pointer_dragged || dx * dx + dy * dy > 16;
    }
    // A focus listener can invalidate layout. The pending press remains, but
    // selection waits for a fresh frame rather than consuming old geometry.
    if (current_document_layout(view) && view.pointer_selecting && (event.type == XL_POINTER_DOWN || !view.pointer_path.empty()))
        try { select_at(view, event.x, event.y, event.type != XL_POINTER_DOWN || (event.modifiers & XL_MOD_SHIFT)); }
        catch (const DocumentFailure& error) { if (error.status != XL_NOT_FOUND && error.status != XL_UNSUPPORTED) throw; }
    if (event.type == XL_POINTER_UP) {
        auto pressed = std::move(view.pointer_path); view.pointer_path.clear();
        if (!view.pointer_dragged && target)
            for (const auto id : doc.ancestors(target))
                if (std::find(pressed.begin(), pressed.end(), id) != pressed.end()) { doc.pointer_click(id); break; }
    }
    return target != 0;
}
bool clipboard_input(xl_view& view, uint32_t command) {
    navigation_input(view);
    auto& doc = *view.document->core;
    std::string selected;
    xl_node_id node = 0;
    if (command != XL_CLIPBOARD_PASTE && !doc.editable_field(doc.focused()) && view.selection) {
        if (!current_document_layout(view)) throw DocumentFailure{XL_BUSY};
        selected = selection_text(*view.document_frame, *view.selection, true);
        node = view.selection->anchor.node;
    }
    const auto read = [&]() -> std::string {
        std::string text;
        if (view.clipboard) {
            if (!view.clipboard->read) throw DocumentFailure{XL_UNSUPPORTED};
            xl_content* content = nullptr;
            const auto status = view.clipboard->read(view.clipboard->context, &content);
            const std::unique_ptr<xl_content> owner(content);
            if (status != XL_OK) throw DocumentFailure{status};
            if (!content || !ui_mime_is(content->value->mime, "text/plain") || content->value->bytes.size() > 4 * 1024 * 1024)
                throw DocumentFailure{XL_INVALID_ARGUMENT};
            const auto& bytes = content->value->bytes;
            if (!bytes.empty()) text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        } else {
            if (!view.window) throw DocumentFailure{XL_UNSUPPORTED};
            text = view.window->clipboard_text();
        }
        (void)TextIndex(text); return text;
    };
    const auto write = [&](std::string_view text) {
        (void)TextIndex(text);
        if (view.clipboard) {
            if (!view.clipboard->write) throw DocumentFailure{XL_UNSUPPORTED};
            const auto status = view.clipboard->write(view.clipboard->context, text.data(), text.size());
            if (status != XL_OK) throw DocumentFailure{status};
        } else {
            if (!view.window) throw DocumentFailure{XL_UNSUPPORTED};
            view.window->clipboard_text(text);
        }
    };
    return doc.clipboard(command, read, write, selected, node);
}
bool key_input(xl_view& view, const xl_key_event& event) try {
    navigation_input(view);
    auto& doc = *view.document->core;
    const std::string_view key(event.key_utf8);
    const auto result = doc.key(event);
    if (!result.allowed) { if (key == " ") view.space_target = 0; return true; }
    if (event.type == XL_KEY_DOWN && !(event.modifiers & XL_MOD_ALT)) {
        const bool control = event.modifiers & (XL_MOD_CONTROL | XL_MOD_META), shift = event.modifiers & XL_MOD_SHIFT;
        uint32_t command = 0;
        if (control && (key == "c" || key == "C" || key == "Insert")) command = XL_CLIPBOARD_COPY;
        else if (control && (key == "x" || key == "X")) command = XL_CLIPBOARD_CUT;
        else if (control && (key == "v" || key == "V")) command = XL_CLIPBOARD_PASTE;
        else if (!control && shift && key == "Insert") command = XL_CLIPBOARD_PASTE;
        else if (!control && shift && key == "Delete") command = XL_CLIPBOARD_CUT;
        const bool composing = doc.editable_field(doc.focused()) && (doc.edit_state(doc.focused()).flags & XL_EDIT_COMPOSING);
        if (command && !composing) return clipboard_input(view, command);
    }
    if (event.type == XL_KEY_DOWN && key != "Tab" && doc.edit_key(event, matching_document_layout(view) ? view.document_frame.get() : nullptr)) return true;
    const bool shortcut = event.modifiers & (XL_MOD_CONTROL | XL_MOD_ALT | XL_MOD_META);
    const bool shift = event.modifiers & XL_MOD_SHIFT;
    if (key == " " && event.type == XL_KEY_UP) {
        const auto target = std::exchange(view.space_target, 0);
        return target && target == result.target && target == doc.focused() && view.space_epoch == doc.focus_epoch() &&
            doc.keyboard_activate(target, true, true);
    }
    if (event.type != XL_KEY_DOWN || shortcut) return false;
    if (key == "Tab") {
        view.space_target = 0; view.reveal_target = doc.focus_next(shift); return true;
    }
    if (key == "Enter" && doc.keyboard_activate(result.target, false, true)) return true;
    if (key == " " && doc.keyboard_activate(result.target, true, false)) {
        if (!event.repeat) { view.space_target = result.target; view.space_epoch = doc.focus_epoch(); }
        return true;
    }
    if (doc.text_control(result.target)) return false;
    if (key == "ArrowLeft" || key == "ArrowRight" || key == "ArrowUp" || key == "ArrowDown" ||
        key == "PageUp" || key == "PageDown" || key == "Home" || key == "End" || key == " ") {
        if (matching_document_layout(view)) return key_scroll(view, result.target, key, shift);
        if (view.key_scrolls.size() >= 64) view.key_scrolls.erase(view.key_scrolls.begin());
        view.key_scrolls.push_back({result.target, std::string(key), shift}); return true;
    }
    return false;
} catch (...) {
    view.space_target = 0; throw;
}
bool valid_key_string(const char* text, bool empty) {
    if (!text) return false;
    size_t length = 0;
    while (length <= 128 && text[length]) ++length;
    if (length > 128 || (!empty && !length)) return false;
    for (int32_t index = 0; index < int32_t(length);) {
        UChar32 scalar; U8_NEXT(text, index, int32_t(length), scalar);
        if (scalar < 0 || scalar < 0x20 || scalar == 0x7f) return false;
    }
    return true;
}
xl_status prepare_edit(xl_view& view) {
    if (!view.document) return XL_NOT_FOUND;
    if (view.document->active) return XL_BUSY;
    const auto target = view.document->core->focused();
    if (!target || !view.document->core->text_control(target)) return XL_OK;
    const auto prepared = xl_runtime_prepare_text(view.runtime);
    if (prepared == XL_OK) view.document->core->text_session(view.runtime->text_session);
    return prepared;
}
bool cancel_composition(xl_view& view) {
    const xl_composition cancel{sizeof(xl_composition), XL_COMPOSITION_CANCEL, 0, 0, nullptr, 0};
    return view.document && view.document->core->composition(cancel);
}
void native_composition(xl_view& view, const WindowInput& input) {
    const TextIndex index(input.text);
    const auto scalar_offset = [&](int64_t count) {
        if (count < 0) return index.length();
        uint32_t result = 0;
        for (size_t i = 1; count > 0 && i < index.bytes.size(); ++i) if (index.bytes[i] != TextIndex::invalid) { result = uint32_t(i); --count; }
        return result;
    };
    const auto first = scalar_offset(input.start), end = input.length < 0 ? first : scalar_offset(int64_t(std::max(0, input.start)) + input.length);
    const xl_composition update{sizeof(xl_composition), XL_COMPOSITION_UPDATE, first, std::max(first, end), input.text.data(), input.text.size()};
    view.document->core->composition(update);
}
xl_status text_rects(const xl_view* view, xl_node_id node, uint32_t start, uint32_t end,
                     xl_node_bounds* output, size_t capacity, size_t* required, bool dom_offsets) {
    if (!required || (!output && capacity) || start > end) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (!current_document_frame(value)) return XL_BUSY;
        const auto found = value.document_frame->text_nodes.find(node);
        if (found == value.document_frame->text_nodes.end()) return XL_NOT_FOUND;
        const auto& text = value.document_frame->texts[found->second.first];
        const auto& source = text.sources[found->second.second];
        const auto length = dom_offsets ? source.offsets.size() - 1 : source.offsets.back() - source.offsets.front();
        if (end > length) return XL_INVALID_ARGUMENT;
        const auto begin = dom_offsets ? source.offsets[start] : source.offsets.front() + start;
        const auto finish = dom_offsets ? source.offsets[end] : source.offsets.front() + end;
        auto ranges = text.paragraph->ranges(begin, finish);
        for (auto& rect : ranges) {
            rect.x += text.x; rect.y += text.y; rect = text.clip.bounds(rect);
        }
        std::erase_if(ranges, [](const auto& rect) { return !nonempty(rect); });
        *required = ranges.size();
        if (capacity < ranges.size()) return XL_BUFFER_TOO_SMALL;
        for (size_t index = 0; index < ranges.size(); ++index) {
            output[index] = ranges[index];
        }
        return XL_OK;
    });
}
}

extern "C" {
xl_status xl_view_options_init(xl_view_options* options, size_t size) {
    if (!options) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*options)) return XL_ABI_MISMATCH;
    *options = {};
    options->struct_size = sizeof(*options);
    options->abi_version = XL_ABI_VERSION;
    options->width = 640;
    options->height = 480;
    options->background_argb = 0xffffffffu;
    return XL_OK;
}
xl_status xl_view_create(xl_runtime* runtime, const xl_view_options* options, xl_view** result) {
    if (!result) return XL_INVALID_ARGUMENT;
    *result = nullptr;
    const auto status = xl_runtime_check_thread(runtime);
    if (status != XL_OK) return status;
    xl_view_options actual;
    xl_view_options_init(&actual, sizeof(actual));
    if (options) {
        if (options->struct_size < sizeof(*options) || options->abi_version != XL_ABI_VERSION) return XL_ABI_MISMATCH;
        actual = *options;
    }
    if (!valid_size(actual.width, actual.height) || actual.kind > XL_VIEW_OFFSCREEN
            || (actual.flags & ~(XL_VIEW_HIDDEN | XL_VIEW_RESIZABLE))) return XL_INVALID_ARGUMENT;
    if (actual.kind != XL_VIEW_WINDOW && actual.flags) return XL_INVALID_ARGUMENT;
    if (actual.kind == XL_VIEW_NATIVE) {
        if (!actual.native_window || actual.native_kind == XL_NATIVE_NONE) return XL_INVALID_ARGUMENT;
    } else if (actual.native_window || actual.native_kind != XL_NATIVE_NONE) return XL_INVALID_ARGUMENT;
    if (runtime->view_calls || (actual.kind != XL_VIEW_OFFSCREEN && DesktopWindow::busy())) return XL_BUSY;
    return boundary([&] {
        RuntimeCall call(runtime);
        auto view = std::make_unique<xl_view>();
        view->runtime = runtime;
        view->owns_window = actual.kind == XL_VIEW_WINDOW;
        int width = actual.width, height = actual.height;
        if (actual.kind != XL_VIEW_OFFSCREEN) {
            view->window = std::make_unique<DesktopWindow>(actual);
            view->window->size(width, height);
        }
        if (!valid_size(width, height)) return XL_INVALID_ARGUMENT;
        view->raster = std::make_unique<RasterView>(width, height, actual.background_argb);
        ++runtime->view_count;
        *result = view.release();
        return XL_OK;
    });
}
xl_status xl_view_destroy(xl_view* view) {
    if (!view) return XL_OK;
    const auto status = xl_runtime_check_thread(view->runtime);
    if (status != XL_OK) return status;
    if (view->active || view->runtime->view_calls || (view->window && DesktopWindow::busy())) return XL_BUSY;
    if (view->native_ax && view->native_ax->close_status() != XL_OK) return XL_BUSY;
    return boundary([&] {
        auto* runtime = view->runtime;
        RuntimeCall call(runtime);
        delete view;
        --runtime->view_count;
        return XL_OK;
    });
}
xl_status xl_runtime_pump_events(xl_runtime* runtime, uint32_t flags) {
    const auto status = xl_runtime_check_thread(runtime);
    if (status != XL_OK) return status;
    return boundary([&] { RuntimeCall call(runtime); return DesktopWindow::pump(flags); });
}
xl_status xl_view_set_rects(xl_view* view, const xl_paint_rect* rects, size_t count) {
    if (!rects && count) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (value.document) return XL_BUSY;
        value.raster->rects({rects, count}); return XL_OK;
    });
}
xl_status xl_view_set_background(xl_view* view, uint32_t color) {
    return view_call(view, [&](xl_view& value) { value.raster->background(color); return XL_OK; });
}
xl_status xl_view_invalidate(xl_view* view, int32_t x, int32_t y, int32_t width, int32_t height) {
    return view_call(view, [&](xl_view& value) { value.raster->invalidate(x, y, width, height); return XL_OK; });
}
xl_status xl_view_request_present(xl_view* view) {
    return view_call(view, [](xl_view& value) { value.present_pending = true; return XL_OK; });
}
xl_status xl_view_next_wakeup_ms(const xl_view* view, int64_t* milliseconds) {
    if (!milliseconds) return XL_INVALID_ARGUMENT;
    *milliseconds = -1;
    return view_call(view, [&](xl_view& value) {
        if (value.document) *milliseconds = value.document->core->next_task_delay();
        return XL_OK;
    });
}
xl_status xl_view_set_graphics_backend(xl_view* view,uint32_t backend) {
    if(backend>XL_GRAPHICS_DAWN_SWIFTSHADER)return XL_INVALID_ARGUMENT;
    return view_call(view,[&](xl_view& value){value.raster->graphics_backend(backend);value.present_pending=true;return XL_OK;});
}
xl_status xl_view_get_graphics_info(const xl_view* view,xl_graphics_info* info,size_t size) {
    if(!info)return XL_INVALID_ARGUMENT;
    if(size<sizeof(*info))return XL_ABI_MISMATCH;
    return view_call(view,[&](xl_view& value){*info=value.raster->graphics_info();return XL_OK;});
}
xl_status xl_view_abandon_graphics_backend(xl_view* view) {
    return view_call(view,[&](xl_view& value){value.raster->abandon_graphics();value.present_pending=true;return XL_OK;});
}
xl_status xl_view_render(xl_view* view, uint32_t* flags) {
    if (!flags) return XL_INVALID_ARGUMENT;
    *flags = 0;
    return view_call(view, [&](xl_view& value) {
        bool full_present = value.present_pending;
        if (value.window) {
            int width = 0, height = 0;
            value.window->size(width, height);
            if (!valid_size(width, height)) return XL_BUSY;
            value.raster->resize(width, height);
            if (value.window->exposed()) full_present = value.present_pending = true;
        }
        if (value.document) {
            if (value.document->active) return XL_BUSY;
            struct DocumentActive {
                bool& value;
                explicit DocumentActive(bool& flag) : value(flag) { value = true; }
                ~DocumentActive() { value = false; }
            } document_active(value.document->active);
            auto environment = value.window ? value.window->environment() : ViewEnvironment{};
            environment.width = value.raster->width(); environment.height = value.raster->height();
            value.document->core->environment(environment);
            value.document->core->run_tasks();
        }
        const auto sync_document = [&]() -> xl_status {
          if (value.document) {
            if (value.document->active) return XL_BUSY;
            const auto& current = value.document_frame;
            if (value.scroll_dirty || !current || current->revision != value.document->core->revision() || current->canvas_revision!=value.document->core->canvas_revision()
                    || current->width != value.raster->width() || current->height != value.raster->height()) {
                const bool projection=current&&current->revision==value.document->core->revision()
                    &&current->width==value.raster->width()&&current->height==value.raster->height();
                if ((!projection&&value.document->core->has_text())||(projection&&current->canvas_revision!=value.document->core->canvas_revision()&&value.document->core->canvas_has_text())) {
                    const auto prepared = xl_runtime_prepare_text(value.runtime);
                    if (prepared != XL_OK) return prepared;
                    value.document->core->text_session(value.runtime->text_session);
                }
                navigation_input(value);
                auto frame = std::make_unique<DocumentFrame>(projection?(value.scroll_dirty?current->project(value.scrolls):*current):value.document->core->layout(value.raster->width(), value.raster->height(), value.scrolls));
                const bool bitmap_only=projection&&!value.scroll_dirty;
                if(frame->canvas_revision!=value.document->core->canvas_revision())value.document->core->refresh_canvases(*frame);
                if (frame->focused != value.focus_snapshot) {
                    value.focus_snapshot = frame->focused; value.reveal_target = frame->focused; value.edit_snapshot = 0;
                }
                if (frame->edit && frame->edit->revision != value.edit_snapshot) {
                    value.edit_snapshot = frame->edit->revision; value.reveal_edit = true;
                }
                if (!frame->edit) value.edit_snapshot = 0;
                value.raster->boxes(frame->paint);
                value.raster->texts(frame->texts);
                value.raster->outlines(frame->outlines);
                value.raster->order(frame->display);
                value.document_frame = std::move(frame);
                if(!bitmap_only)++value.ax_generation;
                value.scrolls.clear();
                for (const auto& [id, scroll] : value.document_frame->scrolls)
                    if (scroll.info.x || scroll.info.y) value.scrolls.emplace(id, std::pair{scroll.info.x, scroll.info.y});
                value.scroll_dirty = false;
                refresh_selection(value);
            }
          }
          return XL_OK;
        };
        auto synchronized = sync_document();
        if (synchronized != XL_OK) return synchronized;
        NativeRequests native;
        native_requests(value, native);
        synchronized = sync_document();
        if (synchronized != XL_OK) return synchronized;
        if(value.window && value.document_frame) {
            WindowInput input;
            bool clear_composition = false;
            while (value.window->key_input(input)) {
                if (clear_composition && input.kind != WindowInput::Kind::text) cancel_composition(value);
                clear_composition = false;
                if (input.kind == WindowInput::Kind::reset) { value.space_target = 0; value.pointer_path.clear(); cancel_composition(value); }
                else if (input.kind == WindowInput::Kind::key) key_input(value, input.key.event());
                else if (input.kind == WindowInput::Kind::text) value.document->core->text_input(input.text);
                else if (input.kind == WindowInput::Kind::pointer) {
                    const xl_pointer_event event{sizeof(event), input.pointer_type, input.pointer_modifiers, 0, input.x, input.y};
                    pointer_input(value, event);
                }
                else if (input.text.empty()) clear_composition = true;
                else native_composition(value, input);
                synchronized = sync_document();
                if (synchronized != XL_OK) return synchronized;
            }
            if (clear_composition) cancel_composition(value);
        }
        if (value.document_frame && value.document) {
            synchronized = sync_document();
            if (synchronized != XL_OK) return synchronized;
            for (const auto& pending : value.key_scrolls) key_scroll(value, pending.target, pending.key, pending.shift);
            value.key_scrolls.clear();
            if (value.reveal_target) reveal_focus(value, std::exchange(value.reveal_target, 0));
            if (std::exchange(value.reveal_edit, false) && value.document_frame->edit)
                reveal_bounds(value, value.document_frame->edit->node, edit_caret(*value.document_frame, false), true);
            if (value.scroll_dirty) {
                synchronized = sync_document();
                if (synchronized != XL_OK) return synchronized;
            }
        }
        if(value.window && value.document_frame) {
            ScrollPointer wheel;
            while (value.window->scroll_pointer(wheel)) scroll_at(value, wheel.x, wheel.y, wheel.dx, wheel.dy);
            if (value.scroll_dirty) {
                synchronized = sync_document();
                if (synchronized != XL_OK) return synchronized;
            }
            SelectionPointer pointer;
            while(value.window->selection_pointer(pointer)) {
                try {select_at(value,pointer.x,pointer.y,pointer.extend);}
                catch(const DocumentFailure& error) {if(error.status!=XL_NOT_FOUND)throw;}
            }
            synchronized = sync_document();
            if (synchronized != XL_OK) return synchronized;
        }
        const auto damage = value.raster->paint();
        if (value.window) value.window->text_input(value.document_frame && value.document_frame->edit &&
            !(value.document_frame->edit->flags & XL_EDIT_READ_ONLY), value.document_frame ? edit_caret(*value.document_frame, true) : xl_node_bounds{});
        if (!damage.empty()) { *flags |= XL_FRAME_REDRAWN; value.present_pending = true; }
        if (!value.present_pending) {
            ++value.skipped;
            publish_accessibility(value);
            return XL_OK;
        }
        if (value.window) {
            // A failed upload retries the complete retained frame next time.
            auto region = full_present || damage.empty()
                ? Damage{0, 0, value.raster->width(), value.raster->height()} : damage;
            size_t pitch;
            int width, height;
            for (;;) {
                width = region.right - region.left; height = region.bottom - region.top;
                pitch = size_t(width) * 4;
                if (size_t(height) > std::numeric_limits<size_t>::max() / pitch) return XL_OUT_OF_MEMORY;
                value.staging.resize(pitch * height);
                const auto generation = value.raster->graphics_info().generation;
                if (!value.raster->read_region(region, value.staging.data(), pitch)) return XL_INTERNAL_ERROR;
                if (generation == value.raster->graphics_info().generation ||
                        (region.left == 0 && region.top == 0 && region.right == value.raster->width() && region.bottom == value.raster->height())) break;
                // Recovery can change antialiasing outside the original damage.
                // Present a complete frame from the new backend atomically.
                region = {0, 0, value.raster->width(), value.raster->height()};
            }
            if (!value.window->present(value.staging.data(), region.left, region.top, width, height, static_cast<int>(pitch))) return XL_INTERNAL_ERROR;
            ++value.presents;
            value.uploaded += uint64_t(width) * uint64_t(height);
            *flags |= XL_FRAME_PRESENTED;
        }
        value.present_pending = false;
        publish_accessibility(value);
        return XL_OK;
    });
}
xl_status xl_view_get_stats(const xl_view* view, xl_view_stats* stats, size_t size) {
    if (!stats) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*stats)) return XL_ABI_MISMATCH;
    return view_call(view, [&](xl_view& value) {
        *stats = {};
        stats->struct_size = sizeof(*stats);
        stats->pixel_width = value.raster->width();
        stats->pixel_height = value.raster->height();
        stats->needs_frame = value.ax_updates.needs_publish(value.ax_generation) || value.native_updates.needs_publish(value.ax_generation) ||
            (value.native_ax && value.native_ax->pending()) || value.scroll_dirty || value.raster->dirty() || value.present_pending ||
            value.reveal_target || value.reveal_edit || !value.key_scrolls.empty() ||
            (value.window && (value.window->exposed() || value.window->input_pending()));
        if (value.document && (value.document->core->tasks_ready() || !value.document_frame || value.document_frame->revision != value.document->core->revision())) stats->needs_frame = 1;
        stats->close_requested = value.window && value.window->close_requested();
        stats->raster_frames = value.raster->frames();
        stats->rasterized_pixels = value.raster->painted_pixels();
        stats->presented_frames = value.presents;
        stats->skipped_frames = value.skipped;
        stats->uploaded_pixels = value.uploaded;
        return XL_OK;
    });
}
xl_status xl_view_resize(xl_view* view, int32_t width, int32_t height) {
    if (!valid_size(width, height)) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (value.window) value.window->resize(width, height);
        else value.raster->resize(width, height);
        if(value.document) {
            auto environment=value.window?value.window->environment():ViewEnvironment{};
            if(!value.window){environment.width=width;environment.height=height;}
            value.document->core->environment(environment);
        }
        return XL_OK;
    });
}
xl_status xl_view_copy_rgba(const xl_view* view, void* pixels, size_t capacity, size_t row_bytes) {
    return view_call(view, [&](xl_view& value) {
        if (!value.raster->ready()) return XL_BUSY;
        const auto minimum = size_t(value.raster->width()) * 4;
        if (!pixels || row_bytes < minimum || size_t(value.raster->height() - 1) > (std::numeric_limits<size_t>::max() - minimum) / row_bytes) return XL_INVALID_ARGUMENT;
        if (capacity < size_t(value.raster->height() - 1) * row_bytes + minimum) return XL_BUFFER_TOO_SMALL;
        return value.raster->copy_rgba(pixels, capacity, row_bytes) ? XL_OK : XL_INTERNAL_ERROR;
    });
}
xl_status xl_view_native_handle(const xl_view* view, uint32_t* kind, uintptr_t* window) {
    if (!kind || !window) return XL_INVALID_ARGUMENT;
    *kind = XL_NATIVE_NONE; *window = 0;
    return view_call(view, [&](xl_view& value) { if (value.window) value.window->handle(*kind, *window); return XL_OK; });
}
xl_status xl_view_set_document(xl_view* view, xl_document* document) {
    return view_call(view, [&](xl_view& value) {
        if (document && document->runtime != value.runtime) return XL_INVALID_ARGUMENT;
        if (document && document->active) return XL_BUSY;
        if (value.document == document) return XL_OK;
        if (document) {
            auto environment = value.window ? value.window->environment() : ViewEnvironment{};
            environment.width = value.raster->width(); environment.height = value.raster->height();
            document->core->environment(environment);
        }
        value.raster->rects({});
        value.raster->texts({});
        value.raster->outlines({});
        value.raster->overlays({});
        value.selection.reset();
        value.scrolls.clear(); value.scroll_dirty = false;
        value.space_target = value.reveal_target = 0; value.key_scrolls.clear();
        value.input_identity = value.focus_snapshot = 0;
        value.pointer_path.clear();
        value.edit_snapshot = 0; value.reveal_edit = false;
        if(value.window) {
            SelectionPointer ignored;
            while(value.window->selection_pointer(ignored)){}
            ScrollPointer wheel;
            while(value.window->scroll_pointer(wheel)){}
            WindowInput key;
            while(value.window->key_input(key)){}
        }
        if (value.document) --value.document->views;
        value.document = document;
        if (document) ++document->views;
        value.document_frame.reset();
        ++value.ax_generation;
        return XL_OK;
    });
}
xl_status xl_view_set_ax_updates(xl_view* view, uint32_t enabled) {
    if (enabled > 1) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) { value.ax_updates.enable(enabled != 0); return XL_OK; });
}
xl_status xl_view_set_native_accessibility(xl_view* view, uint32_t enabled) {
    if (enabled > 1) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!enabled) {
            if (value.native_ax && value.native_ax->close_status() != XL_OK) return XL_BUSY;
            value.native_ax.reset(); value.native_updates.enable(false); return XL_OK;
        }
        if (value.native_ax) return XL_OK;
        if (!value.window) return XL_UNSUPPORTED;
        uint32_t kind; uintptr_t window; value.window->handle(kind, window);
        auto bridge = NativeAX::create(kind, window, value.owns_window);
        value.native_ax = std::move(bridge); value.native_updates.enable(true); return XL_OK;
    });
}
xl_status xl_view_get_native_ax_provider(xl_view* view, uint32_t* kind, uintptr_t* provider) {
    if (!kind || !provider) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.native_ax) return XL_NOT_FOUND;
        value.native_ax->provider(*kind, *provider); return XL_OK;
    });
}
xl_status xl_native_ax_get_object_count(uint64_t* count) {
    if (!count) return XL_INVALID_ARGUMENT;
    *count = native_ax_objects(); return XL_OK;
}
xl_status xl_view_take_ax_update(xl_view* view, xl_ax_update** update) {
    if (!update) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        const auto status = value.ax_updates.pending();
        if (status != XL_OK) return status;
        auto result = std::make_unique<xl_ax_update>();
        result->data = value.ax_updates.take();
        *update = result.release();
        return XL_OK;
    });
}
xl_status xl_view_node_bounds(const xl_view* view, xl_node_id node, xl_node_bounds* bounds) {
    if (!bounds) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (!current_document_frame(value)) return XL_BUSY;
        const auto found = value.document_frame->boxes.find(node);
        if (found == value.document_frame->boxes.end()) return XL_NOT_FOUND;
        *bounds = found->second;
        return XL_OK;
    });
}
xl_status xl_view_get_scroll(const xl_view* view, xl_node_id node, xl_scroll_info* info, size_t size) {
    if (!info) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*info)) return XL_ABI_MISMATCH;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (!current_document_layout(value)) return XL_BUSY;
        const auto found = value.document_frame->scrolls.find(node);
        if (found == value.document_frame->scrolls.end()) return XL_NOT_FOUND;
        *info = found->second.info; return XL_OK;
    });
}
xl_status xl_view_set_scroll(xl_view* view, xl_node_id node, float x, float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (!matching_document_layout(value)) return XL_BUSY;
        const auto found = value.document_frame->scrolls.find(node);
        if (found == value.document_frame->scrolls.end()) return XL_NOT_FOUND;
        scroll_offset(value, node, found->second.info, x, y); return XL_OK;
    });
}
xl_status xl_view_scroll_at(xl_view* view, float x, float y, float dx, float dy) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(dx) || !std::isfinite(dy)) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (!current_document_layout(value)) return XL_BUSY;
        scroll_at(value, x, y, dx, dy); return XL_OK;
    });
}
xl_status xl_view_set_scroll_input(xl_view* view, uint32_t enabled) {
    if (enabled > 1) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.window) return XL_UNSUPPORTED;
        value.window->scroll_input(enabled != 0); return XL_OK;
    });
}
xl_status xl_view_key(xl_view* view, const xl_key_event* event, size_t size, uint32_t* handled) {
    if (!event || !handled) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*event) || event->struct_size < sizeof(*event)) return XL_ABI_MISMATCH;
    if ((event->type != XL_KEY_DOWN && event->type != XL_KEY_UP) || event->location > 3 || event->repeat > 1 || event->reserved ||
        event->modifiers & ~(XL_MOD_SHIFT | XL_MOD_CONTROL | XL_MOD_ALT | XL_MOD_META | XL_MOD_CAPS_LOCK | XL_MOD_NUM_LOCK) ||
        !valid_key_string(event->key_utf8, false) || !valid_key_string(event->code_utf8, true)) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (value.document->active) return XL_BUSY;
        if (event->type == XL_KEY_DOWN) {
            const auto prepared = prepare_edit(value);
            if (prepared != XL_OK) return prepared;
        }
        const auto result = key_input(value, *event);
        *handled = result ? 1u : 0u; return XL_OK;
    });
}
xl_status xl_view_set_keyboard_input(xl_view* view, uint32_t enabled) {
    if (enabled > 1) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.window) return XL_UNSUPPORTED;
        value.window->keyboard_input(enabled != 0); value.space_target = 0; value.pointer_path.clear();
        if (!enabled) cancel_composition(value);
        if (enabled && value.document_frame) value.window->text_input(value.document_frame->edit &&
            !(value.document_frame->edit->flags & XL_EDIT_READ_ONLY), edit_caret(*value.document_frame, true));
        return XL_OK;
    });
}
xl_status xl_view_pointer(xl_view* view, const xl_pointer_event* event, size_t size, uint32_t* handled) {
    if (!event || !handled) return XL_INVALID_ARGUMENT;
    *handled = 0;
    if (size < sizeof(*event) || event->struct_size < sizeof(*event)) return XL_ABI_MISMATCH;
    if (event->type < XL_POINTER_DOWN || event->type > XL_POINTER_CANCEL || event->reserved ||
        !std::isfinite(event->x) || !std::isfinite(event->y) ||
        (event->modifiers & ~(XL_MOD_SHIFT | XL_MOD_CONTROL | XL_MOD_ALT | XL_MOD_META | XL_MOD_CAPS_LOCK | XL_MOD_NUM_LOCK))) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (value.document->active) return XL_BUSY;
        *handled = pointer_input(value, *event) ? 1u : 0u; return XL_OK;
    });
}
xl_status xl_view_set_clipboard_handler(xl_view* view, const xl_clipboard_handler* handler, size_t size) {
    if (handler && (size < sizeof(*handler) || handler->struct_size < sizeof(*handler))) return XL_ABI_MISMATCH;
    if ((handler && handler->reserved) || (!handler && size)) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (handler) value.clipboard = *handler;
        else value.clipboard.reset();
        return XL_OK;
    });
}
xl_status xl_view_clipboard(xl_view* view, uint32_t command, uint32_t* handled) {
    if (!handled || command < XL_CLIPBOARD_COPY || command > XL_CLIPBOARD_PASTE) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (value.document->active) return XL_BUSY;
        const bool result = clipboard_input(value, command);
        *handled = result ? 1u : 0u; return XL_OK;
    });
}
xl_status xl_view_text_input(xl_view* view, const char* text, size_t size, uint32_t* handled) {
    if (!handled || (!text && size) || size > 4 * 1024 * 1024) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (value.document->active) return XL_BUSY;
        navigation_input(value);
        const auto result = value.document->core->text_input({text ? text : "", size});
        *handled = result ? 1u : 0u; return XL_OK;
    });
}
xl_status xl_view_set_composition(xl_view* view, const xl_composition* composition, size_t size, uint32_t* handled) {
    if (!composition || !handled) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*composition) || composition->struct_size < sizeof(*composition)) return XL_ABI_MISMATCH;
    if (composition->type < XL_COMPOSITION_UPDATE || composition->type > XL_COMPOSITION_CANCEL ||
        (!composition->text_utf8 && composition->text_size) || composition->text_size > 4 * 1024 * 1024 ||
        (composition->type == XL_COMPOSITION_CANCEL && composition->text_size)) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (value.document->active) return XL_BUSY;
        navigation_input(value);
        const auto result = value.document->core->composition(*composition);
        *handled = result ? 1u : 0u; return XL_OK;
    });
}
xl_status xl_view_ax_count(const xl_view* view, size_t* count) {
    if (!count) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (!current_document_frame(value)) return XL_BUSY;
        *count = value.document_frame->accessibility.size();
        return XL_OK;
    });
}
xl_status xl_view_ax_get(const xl_view* view, size_t index, xl_ax_node* node, size_t size) {
    if (!node) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*node)) return XL_ABI_MISMATCH;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (!current_document_frame(value)) return XL_BUSY;
        if (index >= value.document_frame->accessibility.size()) return XL_NOT_FOUND;
        *node = value.document_frame->accessibility[index].node;
        return XL_OK;
    });
}
xl_status xl_view_ax_name(const xl_view* view, size_t index, char* output, size_t capacity, size_t* required) {
    if (!required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (!current_document_frame(value)) return XL_BUSY;
        if (index >= value.document_frame->accessibility.size()) return XL_NOT_FOUND;
        return copy_string(value.document_frame->accessibility[index].name, output, capacity, required);
    });
}
xl_status xl_view_ax_value(const xl_view* view, size_t index, char* output, size_t capacity, size_t* required) {
    if (!required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (!current_document_frame(value)) return XL_BUSY;
        if (index >= value.document_frame->accessibility.size()) return XL_NOT_FOUND;
        return copy_string(value.document_frame->accessibility[index].value, output, capacity, required);
    });
}
xl_status xl_view_text_rects(const xl_view* view, xl_node_id node, uint32_t start, uint32_t end,
                            xl_node_bounds* output, size_t capacity, size_t* required) {
    return text_rects(view, node, start, end, output, capacity, required, false);
}
xl_status xl_view_dom_text_rects(const xl_view* view, xl_node_id node, uint32_t start, uint32_t end,
                                xl_node_bounds* output, size_t capacity, size_t* required) {
    return text_rects(view, node, start, end, output, capacity, required, true);
}
xl_status xl_view_hit_test_text(const xl_view* view, float x, float y, xl_text_position* position, size_t size) {
    if (!position || !std::isfinite(x) || !std::isfinite(y)) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*position)) return XL_ABI_MISMATCH;
    return view_call(view, [&](xl_view& value) {
        if (!value.document) return XL_NOT_FOUND;
        if (!current_document_frame(value)) return XL_BUSY;
        *position=hit_text(*value.document_frame,x,y);
        return XL_OK;
    });
}
xl_status xl_view_set_selection(xl_view* view,const xl_text_selection* selection,size_t size) {
    if(selection && size<sizeof(*selection))return XL_ABI_MISMATCH;
    return view_call(view,[&](xl_view& value) {
        if(!selection) {
            value.selection.reset(); refresh_selection(value); return XL_OK;
        }
        if(!value.document)return XL_NOT_FOUND;
        if(!current_document_layout(value))return XL_BUSY;
        validate_selection(*value.document_frame,*selection);
        auto normalized=*selection;
        normalized.struct_size=sizeof(normalized);
        normalized.anchor.struct_size=normalized.focus.struct_size=sizeof(xl_text_position);
        const auto previous=value.selection;
        value.selection=normalized;
        try {refresh_selection(value);}
        catch(...) {value.selection=previous;throw;}
        return XL_OK;
    });
}
xl_status xl_view_get_selection(const xl_view* view,xl_text_selection* selection,size_t size) {
    if(!selection)return XL_INVALID_ARGUMENT;
    if(size<sizeof(*selection))return XL_ABI_MISMATCH;
    return view_call(view,[&](xl_view& value) {
        if(!value.document)return XL_NOT_FOUND;
        if(!current_document_layout(value))return XL_BUSY;
        if(!value.selection)return XL_NOT_FOUND;
        *selection=*value.selection;return XL_OK;
    });
}
xl_status xl_view_selection_rects(const xl_view* view,xl_node_bounds* output,size_t capacity,size_t* required) {
    if(!required || (!output && capacity))return XL_INVALID_ARGUMENT;
    return view_call(view,[&](xl_view& value) {
        if(!value.document)return XL_NOT_FOUND;
        if(!current_document_layout(value))return XL_BUSY;
        if(!value.selection)return XL_NOT_FOUND;
        const auto rectangles=selection_ranges(*value.document_frame,*value.selection);
        *required=rectangles.size();
        if(capacity<rectangles.size())return XL_BUFFER_TOO_SMALL;
        std::copy(rectangles.begin(),rectangles.end(),output);return XL_OK;
    });
}
xl_status xl_view_selected_text(const xl_view* view,char* output,size_t capacity,size_t* required) {
    if(!required || (!output && capacity))return XL_INVALID_ARGUMENT;
    return view_call(view,[&](xl_view& value) {
        if(!value.document)return XL_NOT_FOUND;
        if(!current_document_layout(value))return XL_BUSY;
        if(!value.selection)return XL_NOT_FOUND;
        return copy_string(selection_text(*value.document_frame,*value.selection),output,capacity,required);
    });
}
xl_status xl_view_caret_rect(const xl_view* view,const xl_text_position* position,xl_node_bounds* bounds) {
    if(!position || !bounds)return XL_INVALID_ARGUMENT;
    return view_call(view,[&](xl_view& value) {
        if(!value.document)return XL_NOT_FOUND;
        if(!current_document_layout(value))return XL_BUSY;
        *bounds=caret_bounds(*value.document_frame,*position);return XL_OK;
    });
}
xl_status xl_view_select_at(xl_view* view,float x,float y,uint32_t flags) {
    if(!std::isfinite(x) || !std::isfinite(y) || flags&~XL_SELECTION_EXTEND)return XL_INVALID_ARGUMENT;
    return view_call(view,[&](xl_view& value) {
        if(!value.document)return XL_NOT_FOUND;
        if(!current_document_layout(value))return XL_BUSY;
        select_at(value,x,y,flags&XL_SELECTION_EXTEND);return XL_OK;
    });
}
xl_status xl_view_set_selection_input(xl_view* view,uint32_t enabled) {
    if(enabled>1)return XL_INVALID_ARGUMENT;
    return view_call(view,[&](xl_view& value) {
        if(!value.window)return XL_UNSUPPORTED;
        value.window->selection_input(enabled!=0);value.pointer_path.clear();return XL_OK;
    });
}
}
