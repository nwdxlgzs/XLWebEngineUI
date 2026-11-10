#include <XLWebEngineUI/xl.hpp>
#include "accessibility/updates.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <thread>

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
namespace {
void render(xl::View& view, const std::source_location& at = std::source_location::current()) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    uint32_t flags; xl_status status;
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < until); std::this_thread::yield();
    }
    if (status != XL_OK) std::cerr << "render at " << at.line() << ": " << xl_status_string(status) << '\n';
    CHECK(status == XL_OK);
}
xl::AXUpdate take(xl::View& view, const std::source_location& at = std::source_location::current()) {
    xl::AXUpdate result;
    if (!view.take_ax_update(result)) { std::cerr << "missing AX batch at " << at.line() << '\n'; std::exit(1); }
    return result;
}
void empty(xl::View& view) { xl::AXUpdate result; CHECK(!view.take_ax_update(result)); }
size_t index(const xl::AXUpdate& batch, xl_node_id id) {
    for (size_t i = 0; i < batch.info().node_count; ++i) if (batch.node(i).node == id) return i;
    std::cerr << "AX node missing: " << id << '\n'; std::exit(1);
}
bool has(const xl::AXUpdate& batch, uint32_t type, xl_node_id id = 0, uint32_t properties = 0) {
    for (size_t i = 0; i < batch.info().event_count; ++i) {
        const auto event = batch.event(i);
        if (event.type == type && (!id || event.node == id) && (event.properties & properties) == properties) return true;
    }
    return false;
}
std::vector<std::string> announcements(const xl::AXUpdate& batch) {
    std::vector<std::string> result;
    for (size_t i = 0; i < batch.info().event_count; ++i)
        if (batch.event(i).type == XL_AX_EVENT_LIVE_REGION) result.push_back(batch.event_text(i));
    return result;
}
void pressure() {
    // Exercise recovery with actual retained immutable batches, independently
    // of raster cost. No giant HTML fixture or on-disk scratch is necessary.
    using namespace xl::detail;
    DocumentFrame frame; frame.identity = 1; frame.revision = 1;
    frame.accessibility.push_back({{sizeof(xl_ax_node), XL_AX_DOCUMENT, 1, 0, {}, 0, 0}, "document", {}});
    AXUpdates updates; updates.enable(true);
    for (uint64_t generation = 0; generation < 9; ++generation) {
        frame.accessibility[0].name = std::to_string(generation);
        updates.publish(&frame, {}, generation); CHECK(updates.pending() == XL_OK);
    }
    auto reset = updates.take();
    CHECK(reset->info.sequence == 9 && reset->nodes[0].name == "8");
    CHECK(reset->info.flags == (XL_AX_UPDATE_RESET | XL_AX_UPDATE_OVERFLOW));
    CHECK(reset->events.size() == 1 && reset->events[0].event.type == XL_AX_EVENT_RESET);
    CHECK(updates.pending() == XL_NOT_FOUND);
    frame.accessibility[0].name.assign(64 * 1024 * 1024, 'x');
    updates.publish(&frame, {}, 10); CHECK(updates.pending() == XL_UNSUPPORTED);
    frame.accessibility[0].name = "recovered";
    updates.publish(&frame, {}, 11); CHECK(updates.pending() == XL_OK);
    CHECK(updates.take()->info.flags == XL_AX_UPDATE_RESET);
}
}
int main() {
    pressure();
    xl::AXUpdate retained;
    {
        xl::Runtime runtime; xl::Document document(runtime);
        std::ifstream stream(XL_TEST_FONT, std::ios::binary);
        const std::vector<char> font{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
        const std::string font_path = XL_TEST_FONT;
        std::ifstream fallback(font_path.substr(0, font_path.find_last_of("/\\") + 1) + "colr.ttf", std::ios::binary);
        const std::vector<char> emoji{std::istreambuf_iterator<char>(fallback), std::istreambuf_iterator<char>()};
        CHECK(!emoji.empty() && xl_document_add_font(document.get(), "emoji", emoji.data(), emoji.size()) == XL_OK);
        xl_view_options options; CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
        options.kind = XL_VIEW_OFFSCREEN; options.width = 320; options.height = 200;
        xl::View view(runtime, &options); view.document(&document);
        CHECK(xl_view_set_native_accessibility(view.get(), 1) == XL_UNSUPPORTED);
        CHECK(xl_view_set_native_accessibility(view.get(), 2) == XL_INVALID_ARGUMENT);
        uint32_t native_kind = 123; uintptr_t native_provider = 456;
        CHECK(xl_view_get_native_ax_provider(view.get(), &native_kind, &native_provider) == XL_NOT_FOUND);
        CHECK(native_kind == 123 && native_provider == 456);
        uint64_t native_count = 123; CHECK(xl_native_ax_get_object_count(&native_count) == XL_OK && native_count == 0);
        CHECK(xl_native_ax_get_object_count(nullptr) == XL_INVALID_ARGUMENT);
        const std::string base = "<!doctype html><style>body{margin:0;font:20px/24px test}input,textarea{display:block;width:100px;height:30px}</style>";
        const auto page = [&](std::string_view html) { document.html(base + std::string(html)); render(view); };
        const auto script = [&](const char* code) { CHECK(document.evaluate(code) == XL_OK); };
        page("<button id=b>Go</button><p id=p>Read</p>");
        empty(view); CHECK(!view.stats().needs_frame);
        view.ax_updates(true); CHECK(view.stats().needs_frame); render(view);
        auto batch = take(view); const auto first = batch.info();
        CHECK(first.sequence == 1 && first.document && first.flags == XL_AX_UPDATE_RESET);
        CHECK(first.revision == document.stats().revision && first.node_count > 3 && first.event_count == 1);
        const auto button = document.query("#b"), paragraph = document.query("#p");
        CHECK(batch.name(index(batch, button)) == "Go"); retained = batch;
        render(view); empty(view); CHECK(!view.stats().needs_frame);
        document.attribute(button, "data-noop", "x"); render(view); empty(view);
        const auto raster = view.stats().raster_frames;
        document.attribute(button, "aria-label", "Start"); render(view); batch = take(view);
        CHECK(batch.info().sequence == 2 && batch.info().document == first.document && !batch.info().flags);
        CHECK(batch.name(index(batch, button)) == "Start" && retained.name(index(retained, button)) == "Go");
        CHECK(has(batch, XL_AX_EVENT_NODE_CHANGED, button, XL_AX_CHANGE_NAME));
        CHECK(view.stats().raster_frames == raster);
        document.attribute(button, "aria-expanded", "false"); document.attribute(button, "aria-selected", "true");
        document.attribute(button, "aria-invalid", "grammar"); document.attribute(button, "aria-required", "true");
        render(view); batch = take(view);
        const auto states = batch.node(index(batch, button)).states;
        CHECK((states & (XL_AX_COLLAPSED | XL_AX_SELECTED | XL_AX_INVALID | XL_AX_REQUIRED)) ==
              (XL_AX_COLLAPSED | XL_AX_SELECTED | XL_AX_INVALID | XL_AX_REQUIRED));
        CHECK(has(batch, XL_AX_EVENT_NODE_CHANGED, button, XL_AX_CHANGE_STATES));
        CHECK(xl_document_ax_action(document.get(), button, XL_AX_ACTION_FOCUS) == XL_OK); render(view); batch = take(view);
        CHECK(has(batch, XL_AX_EVENT_FOCUS_CHANGED, button));

        auto child = document.create("button"); document.text(child, "Child"); document.append(paragraph, child);
        render(view); batch = take(view);
        CHECK(has(batch, XL_AX_EVENT_NODE_ADDED, child) && has(batch, XL_AX_EVENT_CHILDREN_CHANGED, paragraph));
        document.append(button, child); render(view); batch = take(view);
        CHECK(has(batch, XL_AX_EVENT_NODE_CHANGED, child, XL_AX_CHANGE_PARENT));
        CHECK(has(batch, XL_AX_EVENT_CHILDREN_CHANGED, button) && has(batch, XL_AX_EVENT_CHILDREN_CHANGED, paragraph));
        const auto child_text = batch.node(index(batch, child) + 1).node;
        document.remove(child); render(view); batch = take(view);
        CHECK(has(batch, XL_AX_EVENT_NODE_REMOVED, child) && has(batch, XL_AX_EVENT_NODE_REMOVED, child_text));
        size_t parent_removed = SIZE_MAX, text_removed = SIZE_MAX;
        for (size_t i = 0; i < batch.info().event_count; ++i) {
            if (batch.event(i).type != XL_AX_EVENT_NODE_REMOVED) continue;
            if (batch.event(i).node == child) parent_removed = i;
            if (batch.event(i).node == child_text) text_removed = i;
        }
        CHECK(text_removed < parent_removed);
        // DOM ordering, including reordering within one parent, remains visible
        // even where the parent's own properties and geometry are unchanged.
        const auto left = document.create("span"), right = document.create("span");
        document.attribute(left, "aria-label", "Left"); document.attribute(right, "aria-label", "Right");
        document.text(left, "Left"); document.text(right, "Right");
        document.append(paragraph, left); document.append(paragraph, right); render(view); (void)take(view);
        document.append(paragraph, left); render(view); batch = take(view);
        CHECK(index(batch, right) < index(batch, left) && has(batch, XL_AX_EVENT_CHILDREN_CHANGED, paragraph));

        page("<label for=c>Accept</label><input id=c type=checkbox checked required><label>Message<textarea id=t readonly>Text</textarea></label>"
             "<div aria-disabled=true><button id=d>Disabled semantics</button><button id=e aria-disabled=false>Enabled</button></div>"
             "<div id=m role='unknown CHECKBOX' aria-checked=mixed></div><div aria-hidden=true><button id=hidden>Hidden</button></div>");
        batch = take(view); CHECK(batch.info().flags == XL_AX_UPDATE_RESET);
        const auto checkbox = batch.node(index(batch, document.query("#c")));
        CHECK(checkbox.role == XL_AX_CHECKBOX && (checkbox.states & (XL_AX_CHECKABLE | XL_AX_CHECKED | XL_AX_REQUIRED)) ==
              (XL_AX_CHECKABLE | XL_AX_CHECKED | XL_AX_REQUIRED));
        CHECK(batch.name(index(batch, checkbox.node)) == "Accept");
        const auto textarea = index(batch, document.query("#t"));
        CHECK(batch.name(textarea).find("Message") != std::string::npos);
        CHECK((batch.node(textarea).states & (XL_AX_MULTILINE | XL_AX_READ_ONLY)) == (XL_AX_MULTILINE | XL_AX_READ_ONLY));
        CHECK(batch.node(index(batch, document.query("#d"))).states & XL_AX_DISABLED);
        CHECK(!(batch.node(index(batch, document.query("#d"))).actions & XL_AX_ACTION_ACTIVATE));
        CHECK(!(batch.node(index(batch, document.query("#e"))).states & XL_AX_DISABLED));
        CHECK(batch.node(index(batch, document.query("#m"))).states & XL_AX_MIXED);
        CHECK(xl_document_ax_action(document.get(), document.query("#d"), XL_AX_ACTION_ACTIVATE) == XL_UNSUPPORTED);
        CHECK(xl_document_ax_action(document.get(), document.query("#e"), XL_AX_ACTION_ACTIVATE) == XL_OK);
        CHECK(xl_document_ax_action(document.get(), document.query("#m"), XL_AX_ACTION_ACTIVATE) == XL_OK);
        for (size_t i = 0; i < batch.info().node_count; ++i) CHECK(batch.node(i).node != document.query("#hidden"));

        page("<div id=region aria-live=polite><input id=password type=password value=secret><input id=input value='A&#x1f600;B'></div><p id=reading>Read here</p>");
        batch = take(view); CHECK(announcements(batch).empty());
        const auto password = document.query("#password"), input = document.query("#input");
        CHECK(batch.node(index(batch, password)).states & XL_AX_PROTECTED);
        CHECK(batch.value(index(batch, password)).find("secret") == std::string::npos);
        document.value(password, "different-secret"); render(view); batch = take(view);
        CHECK(has(batch, XL_AX_EVENT_NODE_CHANGED, password, XL_AX_CHANGE_VALUE));
        for (const auto& text : announcements(batch)) CHECK(text.find("secret") == std::string::npos);
        CHECK(xl_document_ax_action(document.get(), input, XL_AX_ACTION_FOCUS) == XL_OK);
        document.edit_selection(input, 1, 3); render(view); batch = take(view);
        CHECK(has(batch, XL_AX_EVENT_FOCUS_CHANGED, input) && has(batch, XL_AX_EVENT_SELECTION_CHANGED, input));
        for (size_t i = 0; i < batch.info().event_count; ++i) if (batch.event(i).type == XL_AX_EVENT_SELECTION_CHANGED) {
            CHECK(batch.event(i).selection.anchor.offset_utf16 == 1 && batch.event(i).selection.focus.offset_utf16 == 3);
        }
        script("document.getElementById('input').blur()"); render(view); batch = take(view);
        CHECK(has(batch, XL_AX_EVENT_FOCUS_CHANGED) && has(batch, XL_AX_EVENT_SELECTION_CHANGED));
        const auto reading = document.query("#reading"); xl_node_id text_node = 0;
        for (size_t i = 0; i < batch.info().node_count; ++i)
            if (batch.node(i).role == XL_AX_TEXT && batch.node(i).parent == reading) text_node = batch.node(i).node;
        CHECK(text_node);
        xl_text_position anchor{sizeof(xl_text_position), 0, text_node, XL_TEXT_DOWNSTREAM, 0}, focus = anchor; focus.offset_utf16 = 4;
        view.selection(anchor, focus); render(view); batch = take(view);
        CHECK(has(batch, XL_AX_EVENT_SELECTION_CHANGED, text_node));
        view.clear_selection(); render(view); CHECK(has(take(view), XL_AX_EVENT_SELECTION_CHANGED));

        page("<div id=port style='height:40px;overflow:auto'><button id=top>Top</button><div style='height:80px'></div><button id=bottom>Bottom</button></div>");
        (void)take(view); const auto top = document.query("#top");
        view.scroll_to(document.query("#port"), 0, 50); render(view); batch = take(view);
        CHECK(has(batch, XL_AX_EVENT_NODE_CHANGED, top, XL_AX_CHANGE_BOUNDS));
        CHECK(batch.node(index(batch, top)).states & XL_AX_OFFSCREEN);
        CHECK(xl_view_resize(view.get(), 360, 220) == XL_OK); render(view); batch = take(view);
        CHECK(has(batch, XL_AX_EVENT_NODE_CHANGED, batch.info().document, XL_AX_CHANGE_BOUNDS));

        page("<div id=live aria-live=polite><span id=message>Old</span></div><div role=alert>Warning</div>");
        batch = take(view); CHECK(announcements(batch) == std::vector<std::string>{"Warning"});
        auto message = document.query("#message"); document.text(message, "New"); render(view); batch = take(view);
        CHECK(announcements(batch) == std::vector<std::string>{"New"});
        for (size_t i = 0; i < batch.info().event_count; ++i) if (batch.event(i).type == XL_AX_EVENT_LIVE_REGION)
            CHECK(batch.event(i).node == document.query("#live") && batch.event(i).flags == XL_AX_LIVE_POLITE);
        document.attribute(document.query("#live"), "aria-atomic", "true"); render(view); empty(view);
        const auto extra = document.create("span"); document.text(extra, "Together"); document.append(document.query("#live"), extra);
        render(view); CHECK(announcements(take(view)) == std::vector<std::string>{"New Together"});
        document.attribute(message, "aria-busy", "true"); document.text(extra, "Pending"); render(view); batch = take(view);
        CHECK(announcements(batch).empty());
        document.text(message, "Accumulated"); render(view); CHECK(announcements(take(view)).empty());
        document.attribute(message, "aria-busy", "false"); render(view);
        CHECK(announcements(take(view)) == std::vector<std::string>{"Accumulated Pending"});
        document.attribute(extra, "aria-live", "off"); render(view); empty(view);
        document.text(extra, "Silent"); render(view); CHECK(announcements(take(view)).empty());

        page("<div id=live aria-live=polite aria-relevant=removals><span id=message>Remove me</span></div>");
        CHECK(announcements(take(view)).empty()); message = document.query("#message");
        document.remove(message); render(view); CHECK(announcements(take(view)) == std::vector<std::string>{"Remove me"});
        page("<div aria-live=polite><span id=live role=status aria-atomic=false><span id=a>One</span><span id=b>Two</span></span></div>");
        (void)take(view); document.text(document.query("#b"), "Three"); render(view);
        CHECK(announcements(take(view)) == std::vector<std::string>{"Three"});

        page("<div id=live aria-live=polite aria-relevant=text><span id=message>Original</span></div>");
        batch = take(view); message = document.query("#message"); xl_node_id live_text = 0;
        for (size_t i = 0; i < batch.info().node_count; ++i)
            if (batch.node(i).role == XL_AX_TEXT && batch.node(i).parent == message) live_text = batch.node(i).node;
        CHECK(live_text); document.text(live_text, "Same node changed"); render(view);
        CHECK(announcements(take(view)) == std::vector<std::string>{"Same node changed"});
        document.text(message, "Replacement node"); render(view); CHECK(announcements(take(view)).empty());

        page("<div aria-live=polite><span id=a>One</span><span id=b>Two</span></div>"); (void)take(view);

        // A pending batch remains readable while DOM changes have not rendered.
        document.attribute(document.query("#a"), "aria-label", "Renamed"); render(view);
        document.attribute(document.query("#a"), "aria-label", "Later"); batch = take(view);
        CHECK(batch.name(index(batch, document.query("#a"))) == "Renamed");
        render(view); (void)take(view);
        for (unsigned i = 0; i < 9; ++i) { document.attribute(document.query("#a"), "aria-label", std::to_string(i)); render(view); }
        batch = take(view); CHECK(batch.info().flags == (XL_AX_UPDATE_RESET | XL_AX_UPDATE_OVERFLOW)); empty(view);
        CHECK(batch.name(index(batch, document.query("#a"))) == "8");
        const auto sequence = batch.info().sequence;
        view.ax_updates(false); render(view); empty(view);
        view.ax_updates(true); render(view); batch = take(view);
        CHECK(batch.info().sequence == sequence + 1 && batch.info().flags == XL_AX_UPDATE_RESET);
        view.document(nullptr); CHECK(view.stats().needs_frame); render(view); batch = take(view);
        CHECK(batch.info().flags == XL_AX_UPDATE_RESET && !batch.info().document && !batch.info().node_count);

        xl_ax_update* untouched = retained.get();
        CHECK(xl_view_take_ax_update(view.get(), &untouched) == XL_NOT_FOUND && untouched == retained.get());
        CHECK(xl_view_set_ax_updates(view.get(), 2) == XL_INVALID_ARGUMENT);
        CHECK(xl_view_take_ax_update(view.get(), nullptr) == XL_INVALID_ARGUMENT);
        xl_ax_update_info info{}; info.sequence = 123;
        CHECK(xl_ax_update_get_info(retained.get(), &info, sizeof(info) - 1) == XL_ABI_MISMATCH && info.sequence == 123);
        xl_ax_event event{}; event.node = 123;
        CHECK(xl_ax_update_get_event(retained.get(), SIZE_MAX, &event, sizeof(event)) == XL_NOT_FOUND && event.node == 123);
        char buffer = 'x'; size_t required = 123;
        CHECK(xl_ax_update_name(retained.get(), index(retained, button), &buffer, 1, &required) == XL_BUFFER_TOO_SMALL);
        CHECK(buffer == 'x' && required == 2);
        CHECK(xl_ax_update_clone(nullptr, &untouched) == XL_INVALID_ARGUMENT && untouched == retained.get());
        xl_status wrong_enable = XL_OK, wrong_take = XL_OK;
        std::thread wrong([&] {
            wrong_enable = xl_view_set_ax_updates(view.get(), 1);
            wrong_take = xl_view_take_ax_update(view.get(), &untouched);
            xl::AXUpdate copy = retained; CHECK(copy.info().sequence == first.sequence);
        }); wrong.join();
        CHECK(wrong_enable == XL_WRONG_THREAD && wrong_take == XL_WRONG_THREAD && untouched == retained.get());
    }
    // The last Runtime and all DOM/layout/ICU objects have gone away.
    std::thread reader([copy = retained]() mutable {
        CHECK(copy.info().sequence == 1 && copy.info().node_count > 3);
        xl::AXUpdate moved = std::move(copy); CHECK(!copy && moved);
        CHECK(moved.info().event_count == 1);
    }); reader.join();
    xl_ax_update_release(nullptr);
    std::cout << "AX updates, live regions, immutable lifetime and queue recovery passed\n";
}
