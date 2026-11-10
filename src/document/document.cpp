#include "dom_internal.h"
#include "style/computed.h"
#include "style/font_shorthand.h"
#include "style/border_shorthand.h"
#include "style/layout_shorthand.h"
#include "style/grid_style.h"
#include "style/focus_selectors.h"
#include "style/variables.h"
#include "style/pseudo_elements.h"
#include "style/generated_content.h"
#include "style/active_rules.h"
#include "style/media_query.h"
#include "style/radius_style.h"
#include "style/nesting.h"
#include "images.h"
#include "graphics/canvas_content.h"
#include <atomic>
#include <algorithm>
#include <cstring>
#include <limits>
#include <exception>

namespace xl::detail {
namespace {
std::atomic<uint64_t> next_node{1};
lxb_status_t serialize(const lxb_char_t* bytes, size_t size, void* context) {
    try { static_cast<std::string*>(context)->append(reinterpret_cast<const char*>(bytes), size); return LXB_STATUS_OK; }
    catch (...) { return LXB_STATUS_ERROR_MEMORY_ALLOCATION; }
}
}

xl_status copy_string(std::string_view text, char* output, size_t capacity, size_t* required) {
    if (!required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    *required = text.size();
    if (capacity < text.size()) return XL_BUFFER_TOO_SMALL;
    if (!text.empty()) std::memcpy(output, text.data(), text.size());
    return XL_OK;
}
Document::Impl::Impl() : pseudos(std::make_unique<PseudoElements>(*this)) {
    // Complete throwing C++ allocations before acquiring the raw styled DOM.
    // PseudoElements stores only this reference until the first style refresh.
    dom = lxb_html_document_create();
    if (!dom) throw std::bad_alloc();
    const auto status = lxb_style_init(dom);
    if (status != LXB_STATUS_OK) { lxb_html_document_destroy(dom); dom = nullptr; check_lxb(status); }
}
Document::Impl::~Impl() {
    close_script();
    pseudos.reset(); // Drop render cascades before their computed declaration arena and DOM.
    for (auto it = detached.begin(); it != detached.end(); ) {
        if ((*it)->parent) it = detached.erase(it);
        else ++it;
    }
    for (auto* node : detached) lxb_dom_node_destroy_deep(node);
    if (dom) { lxb_style_destroy(dom); lxb_html_document_destroy(dom); }
}
xl_node_id Document::Impl::identify(lxb_dom_node_t* node) {
    if (!node) return 0;
    const auto found = identities.find(node);
    if (found != identities.end()) return found->second;
    const auto id = next_node.fetch_add(1, std::memory_order_relaxed);
    if (id == 0 || id == std::numeric_limits<uint64_t>::max()) std::terminate();
    nodes.emplace(id, node);
    try { identities.emplace(node, id); }
    catch (...) { nodes.erase(id); throw; }
    return id;
}
lxb_dom_node_t* Document::Impl::resolve(xl_node_id id) const {
    const auto found = nodes.find(id);
    if (found == nodes.end()) throw DocumentFailure{XL_NOT_FOUND};
    return found->second;
}
xl_node_id Document::Impl::query(std::string_view selector, lxb_dom_node_t* root) {
    const auto matches=query_all(selector,root);
    return matches.empty()?0:matches.front();
}
std::vector<xl_node_id> Document::Impl::query_all(std::string_view selector, lxb_dom_node_t* root) {
    if (selector.empty()) throw DocumentFailure{XL_INVALID_ARGUMENT};
    auto* parser = lxb_css_parser_create();
    if (!parser) throw std::bad_alloc();
    struct Cleanup {
        lxb_css_parser_t* parser;
        lxb_css_selector_list_t* list = nullptr;
        lxb_selectors_t* selectors = nullptr;
        ~Cleanup() {
            if (selectors) lxb_selectors_destroy(selectors, true);
            if (list) lxb_css_selector_list_destroy_memory(list);
            lxb_css_parser_destroy(parser, true);
        }
    } cleanup{parser};
    check_lxb(lxb_css_parser_init(parser, nullptr));
    cleanup.list = lxb_css_selectors_parse(parser, chars(selector), selector.size());
    if (!cleanup.list || parser->status != LXB_STATUS_OK) throw DocumentFailure{XL_INVALID_ARGUMENT};
    cleanup.selectors = lxb_selectors_create();
    if (!cleanup.selectors) throw std::bad_alloc();
    check_lxb(lxb_selectors_init(cleanup.selectors));
    FocusSelectors focus(*this, root);
    focus.apply(cleanup.list);
    std::vector<lxb_dom_node_t*> found;
    check_lxb(lxb_selectors_find(cleanup.selectors, root ? root : lxb_dom_interface_node(dom), cleanup.list,
        [](lxb_dom_node_t* node, lxb_css_selector_specificity_t, void* data) -> lxb_status_t {
            auto& result = *static_cast<std::vector<lxb_dom_node_t*>*>(data);
            try { if(std::find(result.begin(),result.end(),node)==result.end())result.push_back(node); }
            catch(...) {return LXB_STATUS_ERROR_MEMORY_ALLOCATION;}
            return LXB_STATUS_OK;
        }, &found));
    std::vector<xl_node_id> result;
    result.reserve(found.size());
    for(auto* node:found)result.push_back(identify(node));
    return result;
}
xl_node_id Document::Impl::by_id(std::string_view name) {
    return identify(lxb_dom_interface_node(lxb_dom_node_by_id(lxb_dom_interface_node(dom), chars(name), name.size())));
}
void Document::Impl::set_attribute(xl_node_id id, std::string_view name, std::string_view value) {
    if (pseudos->generated(resolve(id))) throw DocumentFailure{XL_INVALID_ARGUMENT};
    if (name.empty() || name.find('\0') != std::string_view::npos || name.find_first_of(" \t\r\n\f\"'>/=") != std::string_view::npos) throw DocumentFailure{XL_INVALID_ARGUMENT};
    auto* target = element(resolve(id));
    std::string canonical(name);
    for (auto& c : canonical) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    const bool canvas_dimension=target->node.local_name==LXB_TAG_CANVAS&&(canonical=="width"||canonical=="height");
    if (!canvas_dimension&&attribute(id, name, false) == value && lxb_dom_element_has_attribute(target, chars(name), name.size())) return;
    const bool was_open = details_open(resolve(id));
    ++revision; // A failed mutation must still invalidate a previously published snapshot.
    if (!lxb_dom_element_set_attribute(target, chars(name), name.size(), chars(value), value.size())) throw std::bad_alloc();
    details_attribute(resolve(id), canonical, was_open);
    if(resolve(id)->local_name==LXB_TAG_CANVAS&&(canonical=="width"||canonical=="height"))canvas_reset(id);
}
void Document::Impl::remove_attribute(xl_node_id id, std::string_view name) {
    if (pseudos->generated(resolve(id))) throw DocumentFailure{XL_INVALID_ARGUMENT};
    auto* node = resolve(id);
    if (!lxb_dom_element_has_attribute(element(node), chars(name), name.size())) return;
    const bool was_open = details_open(node);
    ++revision;
    check_lxb(lxb_dom_element_remove_attribute(element(node), chars(name), name.size()));
    std::string canonical(name);
    for (auto& c : canonical) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    details_attribute(node, canonical, was_open);
    if(node->local_name==LXB_TAG_CANVAS&&(canonical=="width"||canonical=="height"))canvas_reset(id);
}
std::string Document::Impl::attribute(xl_node_id id, std::string_view name, bool required) const {
    size_t size = 0;
    const auto* value = lxb_dom_element_get_attribute(element(resolve(id)), chars(name), name.size(), &size);
    if (!value) {
        // A parsed boolean attribute can be present without a value buffer.
        if (required && !lxb_dom_element_has_attribute(element(resolve(id)), chars(name), name.size()))
            throw DocumentFailure{XL_NOT_FOUND};
        return {};
    }
    return {reinterpret_cast<const char*>(value), size};
}
void Document::Impl::set_text(xl_node_id id, std::string_view value) {
    auto* node = resolve(id);
    if (pseudos->generated(node)) throw DocumentFailure{XL_INVALID_ARGUMENT};
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT && node->type != LXB_DOM_NODE_TYPE_TEXT) throw DocumentFailure{XL_INVALID_ARGUMENT};
    if (text(id) == value) return;
    ++revision;
    if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
        check_lxb(lxb_dom_node_text_content_set(node, chars(value), value.size()));
        return;
    }
    // Keep removed descendants alive: C++ IDs and JS references survive detach.
    auto* replacement = value.empty() ? nullptr : lxb_dom_document_create_text_node(&dom->dom_document, chars(value), value.size());
    if (!replacement && !value.empty()) throw std::bad_alloc();
    while (auto* child = node->first_child) {
        detached.insert(child);
        lxb_dom_node_remove(child);
    }
    if (replacement) {
        if (lxb_dom_node_append_child(node, lxb_dom_interface_node(replacement)) != LXB_DOM_EXCEPTION_OK) throw DocumentFailure{XL_INTERNAL_ERROR};
    }
}
std::string Document::Impl::text(xl_node_id id) const {
    auto* node = resolve(id);
    size_t size = 0;
    auto* bytes = lxb_dom_node_text_content(node, &size);
    if (!bytes) { if (!size) return {}; throw std::bad_alloc(); }
    struct Cleanup { lxb_dom_document_t* dom; lxb_char_t* data; ~Cleanup() { lxb_dom_document_destroy_text(dom, data); } } cleanup{&dom->dom_document, bytes};
    return {reinterpret_cast<const char*>(bytes), size};
}
std::string Document::Impl::style(xl_node_id id, std::string_view name) const {
    const_cast<Impl*>(this)->refresh_styles();
    if(variables&&csscolor::property(name))if(const auto* color=variables->color(element(resolve(id)),name))return color->value;
    if(variables&&cssposition::property(name)&&name!="inset")if(auto value=variables->position(element(resolve(id)),name)) {
        if(!value->wide.empty())return value->wide;
        for(size_t i=0;i<std::size(cssposition::properties);++i)if(name==cssposition::properties[i])return value->values[i];
    }
    if(variables&&cssbackground::property(name)&&name!="background")if(auto value=variables->background(element(resolve(id)),name)) {
        if(!value->wide.empty())return value->wide;
        for(size_t i=0;i<std::size(cssbackground::properties);++i)if(name==cssbackground::properties[i])return value->style.values[i];
    }
    if (generated::managed_property(name)) return generated::property(*const_cast<Impl*>(this),resolve(id),name);
    if (variables && name.starts_with("--")) {
        const auto value = variables->custom(element(resolve(id)), name);
        if (!value) throw DocumentFailure{XL_NOT_FOUND};
        return *value;
    }
    const auto* declaration = lxb_dom_element_style_by_name(element(resolve(id)), chars(name), name.size());
    if (!declaration) throw DocumentFailure{XL_NOT_FOUND};
    if ((declaration->type == LXB_CSS_PROPERTY_WIDTH || declaration->type == LXB_CSS_PROPERTY_HEIGHT ||
         declaration->type == LXB_CSS_PROPERTY_MIN_WIDTH || declaration->type == LXB_CSS_PROPERTY_MAX_WIDTH ||
         declaration->type == LXB_CSS_PROPERTY_MIN_HEIGHT || declaration->type == LXB_CSS_PROPERTY_MAX_HEIGHT) &&
        static_cast<const lxb_css_property_width_t*>(declaration->u.user)->type == css_fit_content) return "fit-content";
    if ((declaration->type == LXB_CSS_PROPERTY_JUSTIFY_CONTENT || declaration->type == LXB_CSS_PROPERTY_ALIGN_CONTENT) &&
        static_cast<const lxb_css_property_align_items_t*>(declaration->u.user)->type == css_space_evenly)
        return "space-evenly";
    std::string output;
    check_lxb(lxb_css_property_serialize(declaration->u.user, static_cast<lxb_css_property_type_t>(declaration->type), serialize, &output));
    return output;
}
xl_node_id Document::Impl::create(std::string_view tag) {
    if (tag.empty() || tag.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string_view::npos)
        throw DocumentFailure{XL_INVALID_ARGUMENT};
    auto* created = lxb_html_document_create_element(dom, chars(tag), tag.size(), nullptr);
    if (!created) throw std::bad_alloc();
    auto* node = lxb_dom_interface_node(created);
    detached.insert(node);
    return identify(node);
}
void Document::Impl::append(xl_node_id parent, xl_node_id child) {
    auto* p = resolve(parent);
    auto* c = resolve(child);
    if (pseudos->generated(p) || pseudos->generated(c)) throw DocumentFailure{XL_INVALID_ARGUMENT};
    if ((p->type != LXB_DOM_NODE_TYPE_ELEMENT && p->type != LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) || c->type == LXB_DOM_NODE_TYPE_DOCUMENT) throw DocumentFailure{XL_INVALID_ARGUMENT};
    ++revision;
    std::vector<lxb_dom_node_t*> inserted;
    if (c->type == LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)
        for (auto* item = c->first_child; item; item = item->next) inserted.push_back(item);
    else inserted.push_back(c);
    if (lxb_dom_node_append_child(p, c) != LXB_DOM_EXCEPTION_OK) throw DocumentFailure{XL_INVALID_ARGUMENT};
    if (c->type != LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT) detached.erase(c);
    for (auto* item : inserted) details_inserted(item);
}
void Document::Impl::remove(xl_node_id child) {
    auto* node = resolve(child);
    if (pseudos->generated(node)) throw DocumentFailure{XL_INVALID_ARGUMENT};
    if (!node->parent) return;
    if (node->type == LXB_DOM_NODE_TYPE_DOCUMENT) throw DocumentFailure{XL_INVALID_ARGUMENT};
    detached.insert(node);
    ++revision;
    lxb_dom_node_remove(node);
}
void Document::Impl::insert_before(xl_node_id parent,xl_node_id child,xl_node_id reference) {
    if(!reference){append(parent,child);return;}
    auto* p=resolve(parent);auto* c=resolve(child);auto* ref=resolve(reference);
    if(pseudos->generated(p)||pseudos->generated(c)||pseudos->generated(ref)||ref->parent!=p||
        (p->type!=LXB_DOM_NODE_TYPE_ELEMENT&&p->type!=LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)||c->type==LXB_DOM_NODE_TYPE_DOCUMENT)
        throw DocumentFailure{XL_INVALID_ARGUMENT};
    if(c==ref)return;
    std::vector<lxb_dom_node_t*> inserted;
    if(c->type==LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)for(auto* item=c->first_child;item;item=item->next)inserted.push_back(item);
    else inserted.push_back(c);
    if(lxb_dom_node_insert_before_spec(p,c,ref)!=LXB_DOM_EXCEPTION_OK)throw DocumentFailure{XL_INVALID_ARGUMENT};
    ++revision;if(c->type!=LXB_DOM_NODE_TYPE_DOCUMENT_FRAGMENT)detached.erase(c);
    for(auto* item:inserted)details_inserted(item);
}
bool Document::Impl::connected(lxb_dom_node_t* node) const {
    while (node && node->parent) node = node->parent;
    return node == lxb_dom_interface_node(dom);
}
void Document::Impl::refresh_styles() {
    if(mounts && content_revision!=mounts->revision()) {
        content_revision=mounts->revision();++revision;
    }
    if (styled_revision == revision) return;
    // Clear every cascade reference before rewriting any shared declarations.
    // Detached trees retain live DOM handles, including their inline styles.
    std::vector<lxb_dom_node_t*> pending{lxb_dom_interface_node(dom)};
    for (auto* root : detached) if (!root->parent) pending.push_back(root);
    std::vector<lxb_dom_element_t*> elements;
    while (!pending.empty()) {
        auto* node = pending.back(); pending.pop_back();
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            auto* target = element(node);
            while (target->style) lxb_dom_element_style_remove_all(target, reinterpret_cast<lxb_style_node_t*>(target->style));
            elements.push_back(target);
        }
        for (auto* child = node->last_child; child; child = child->prev) pending.push_back(child);
    }
    pseudos->reset();
    variables.reset();
    style_order.clear();
    sync_linked_styles(elements);
    if(!nested_styles)nested_styles=std::make_unique<NestedStyles>();
    nested_styles->begin();
    const ActiveStyles active(*this);
    nested_styles->finish();
    auto* sheets = dom->dom_document.css->stylesheets;
    for(const auto& rule:active.rules) {
        if(rule.compiled&&rule.compiled->expanded.contains(rule.rule))continue;
        expand_font_shorthands(rule.rule->declarations,*rule.source);
        expand_border_shorthands(rule.rule->declarations,*rule.source);
        expand_outline_shorthands(rule.rule->declarations,*rule.source);
        expand_radius_shorthands(rule.rule->declarations,*rule.source);
        expand_grid_shorthands(rule.rule->declarations);
        expand_layout_shorthands(rule.rule->declarations);
        if(rule.compiled)rule.compiled->expanded.insert(rule.rule);
    }
    for (auto* target : elements) {
        if (!target->list) continue;
        auto* list = static_cast<lxb_css_rule_declaration_list_t*>(target->list);
        const auto authored = attribute(identify(lxb_dom_interface_node(target)), "style", false);
        expand_font_shorthands(list, authored);
        expand_border_shorthands(list, authored);
        expand_outline_shorthands(list, authored);
        expand_radius_shorthands(list, authored);
        expand_grid_shorthands(list);
        expand_layout_shorthands(list);
    }
    // Class/ancestor/sibling and :has invalidation belongs to the framework.
    FocusSelectors focus(*this);
    for(const auto& rule:active.rules)focus.apply(rule.rule->selector);
    variables = std::make_unique<VariableStyles>();
    if (!variables->apply(*this, elements,active)) {
        variables.reset();
        for (auto* target : elements) if (target->list)
            check_lxb(lxb_dom_element_style_list_append(target,
                static_cast<lxb_css_rule_declaration_list_t*>(target->list), lxb_css_selector_sp_up_s(0)));
        for (size_t index = 0; index < lexbor_array_length(sheets); ++index) {
            auto* sheet = static_cast<lxb_css_stylesheet_t*>(lexbor_array_get(sheets, index));
            if (!sheet->element || connected(lxb_dom_interface_node(sheet->element)))
                check_lxb(lxb_dom_document_stylesheet_apply(&dom->dom_document, sheet));
        }
    }
    pseudos->compute();
    style_media=active.conditions;
    styled_revision = revision;
}
Document::Document(UiMounts* mounts, std::function<std::shared_ptr<void>()> prepare) : impl_(std::make_unique<Impl>()) {
    impl_->mounts=mounts;
    impl_->text_preparer=std::move(prepare);
    check_lxb(lxb_html_document_parse(impl_->dom, chars(""), 0));
}
Document::~Document() = default;
void Document::load(std::string_view html,std::string_view uri) {
    auto next = std::make_unique<Impl>();
    next->uri=resolve_ui_uri({},uri);
    next->mounts=impl_->mounts;
    next->text_preparer=impl_->text_preparer;
    next->environment=impl_->environment;next->native_environment=impl_->native_environment;next->media_preferences=impl_->media_preferences;
    check_lxb(lxb_html_document_parse(next->dom, chars(html), html.size()));
    next->details_inserted(lxb_dom_interface_node(next->dom), true);
    next->revision = impl_->revision + 1;
    next->text_engine = std::move(impl_->text_engine);
    next->text_session = std::move(impl_->text_session);
    impl_ = std::move(next);
}
void Document::load_uri(std::string_view uri) {
    const auto location=resolve_ui_uri({},uri);
    if(!impl_->mounts)throw UriFailure{XL_NOT_FOUND};
    const auto content=impl_->mounts->open(location);
    if(!ui_mime_is(content->mime,"text/html"))throw UriFailure{XL_UNSUPPORTED};
    if(content->bytes.size()>16*1024*1024)throw UriFailure{XL_INVALID_ARGUMENT};
    load({content->bytes.empty()?"":reinterpret_cast<const char*>(content->bytes.data()),content->bytes.size()},location);
}
const std::string& Document::uri() const { return impl_->uri; }
void Document::add_font(const char* family, const void* data, size_t size) {
    if (!impl_->text_engine) impl_->text_engine = std::make_unique<TextEngine>();
    impl_->text_engine->add_font(family, data, size);
    ++impl_->revision;
}
void Document::text_session(std::shared_ptr<void> session) { impl_->text_session = std::move(session); }
bool Document::has_text() const {
    for(const auto& [id,canvas]:impl_->canvases){(void)id;if(canvas->needs_text())return true;}
    impl_->refresh_styles();
    std::vector<std::pair<lxb_dom_node_t*, WhiteSpace>> pending;
    if (impl_->dom->body) {
        auto* body = lxb_dom_interface_node(impl_->dom->body);
        pending.emplace_back(body, body->parent ? white_space(body->parent, WhiteSpace::normal) : WhiteSpace::normal);
    }
    while (!pending.empty()) {
        auto [node, mode] = pending.back(); pending.pop_back();
        if (impl_->details_hidden(node)) continue;
        if (node->local_name == LXB_TAG_SCRIPT || node->local_name == LXB_TAG_STYLE) continue;
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            mode = white_space(node, mode);
            const auto id = const_cast<Impl*>(impl_.get())->identify(node);
            const auto* display = lxb_dom_element_style_by_id(element(node), LXB_CSS_PROPERTY_DISPLAY);
            if (lxb_dom_element_has_attribute(element(node), chars("hidden"), 6)
                    || (display && display->u.display->a == LXB_CSS_VALUE_NONE)
                    || (node->local_name == LXB_TAG_INPUT && impl_->attribute(id, "type", false) == "hidden")) continue;
            if(node->local_name==LXB_TAG_CANVAS){
                const auto found=impl_->canvases.find(id);
                if(found!=impl_->canvases.end()&&found->second->needs_text())return true;
                if(box_style(*impl_,node,100,100,100,BoxStyle{}).inline_flow)return true;
                continue;
            }
            if(impl_->image_node(node)) {
                const auto image=const_cast<Impl*>(impl_.get())->image_content(node);
                if(image&&image->metrics().text)return true;
                // Inline replaced layout still needs its surrounding line's
                // font strut; a block containing only shapes needs no ICU.
                const auto image_style=box_style(*impl_,node,100,100,100,BoxStyle{});
                if(image_style.inline_flow)return true;
                continue;
            }
            // A forced line break also needs paragraph/font layout even when
            // there are no non-whitespace text nodes in this document.
            if (node->local_name == LXB_TAG_BR) return true;
            if (impl_->disclosure_owner(node)) return true;
            if (impl_->text_field(node)) return true;
            const auto box=box_style(*impl_,node,100,100,100,BoxStyle{});
            if(box.inline_atomic) return true;
            if(box.inline_flow && (box.padding_left || box.padding_right || box.padding_top || box.padding_bottom
                    || box.left || box.right || std::any_of(box.borders.begin(),box.borders.end(),[](const auto& border){return border.width>0;}))) return true;
        }
        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            const auto& data = lxb_dom_interface_text(node)->char_data.data;
            for (size_t i = 0; i < data.length; ++i) {
                const auto c = data.data[i];
                if (!collapses_spaces(mode) && data.length) return true;
                if (preserves_breaks(mode) && (c == '\n' || c == '\r' || c == '\f')) return true;
                if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\f') return true;
            }
        }
        for (auto* child : impl_->render_children(node)) pending.emplace_back(child, mode);
    }
    return false;
}
xl_node_id Document::query(std::string_view selector) { return impl_->query(selector); }
xl_node_id Document::create(std::string_view tag) { return impl_->create(tag); }
void Document::append(xl_node_id parent, xl_node_id child) { impl_->append(parent, child); }
void Document::remove(xl_node_id node) { impl_->remove(node); }
void Document::attribute(xl_node_id id, std::string_view name, std::string_view value) { impl_->set_attribute(id, name, value); }
std::string Document::attribute(xl_node_id id, std::string_view name) const { return impl_->attribute(id, name); }
void Document::text(xl_node_id id, std::string_view value) { impl_->set_text(id, value); }
std::string Document::text(xl_node_id id) const { return impl_->text(id); }
std::string Document::style(xl_node_id id, std::string_view name) const { return impl_->style(id, name); }
xl_status Document::evaluate(std::string_view code, uint32_t budget) { return impl_->evaluate(code, budget); }
const std::string& Document::result() const { return impl_->script_result; }
void Document::run_tasks() { impl_->run_tasks(); }
bool Document::tasks_ready() const { return impl_->tasks_ready(); }
int64_t Document::next_task_delay() const {
    if (!impl_->details_toggles.empty()) return 0;
    if(impl_->resize_pending||impl_->visibility_pending||std::any_of(impl_->media_watches.begin(),impl_->media_watches.end(),[](const auto& item){return item.second.pending;}))return 0;
    if (impl_->timers.empty()) return -1;
    auto due = std::chrono::steady_clock::time_point::max();
    for (const auto& [id, timer] : impl_->timers) { (void)id;if(timer.animation&&impl_->environment.hidden)continue; due = std::min(due, timer.due); }
    if(due==std::chrono::steady_clock::time_point::max())return -1;
    return std::max<int64_t>(0, std::chrono::ceil<std::chrono::milliseconds>(due - std::chrono::steady_clock::now()).count());
}
void Document::Impl::update_environment(ViewEnvironment value) {
    native_environment=value;
    if(media_preferences) {
        const auto& prefs=*media_preferences;
        if(prefs.color_scheme)value.color_scheme=prefs.color_scheme-1;
        if(prefs.reduced_motion)value.reduced_motion=prefs.reduced_motion-1;
        if(prefs.contrast)value.contrast=prefs.contrast-1;
        if(prefs.forced_colors)value.forced_colors=prefs.forced_colors-1;
    }
    if(environment==value)return;
    const bool resized=environment.width!=value.width||environment.height!=value.height;
    const bool visibility_changed=environment.hidden!=value.hidden;
    environment=value;viewport_width=value.width;viewport_height=value.height;
    ++revision;
    bool changed=false;
    for(const auto& [query,previous]:style_media)if(query->matches(value)!=previous){changed=true;break;}
    if(!changed&&styled_revision==revision-1)styled_revision=revision;
    for(auto& [id,watch]:media_watches) {(void)id;if(watch.query->matches(value)!=watch.last&&!watch.pending){watch.pending=true;watch.order=next_task_order++;}}
    if(context&&resize_observed&&resized&&!resize_pending){resize_pending=true;resize_order=next_task_order++;}
    if(context&&visibility_changed&&!visibility_pending){visibility_pending=true;visibility_order=next_task_order++;}
}
void Document::environment(ViewEnvironment value) {impl_->update_environment(value);}
void Document::media_preferences(const xl_media_preferences* preferences) {
    if(preferences)impl_->media_preferences=*preferences;else impl_->media_preferences.reset();
    impl_->update_environment(impl_->native_environment);
}
bool Document::matches_media(std::string_view query) const {return MediaQuery(query).matches(impl_->environment);}
uint64_t Document::revision() const {
    if(impl_->mounts && impl_->content_revision!=impl_->mounts->revision()) {
        impl_->content_revision=impl_->mounts->revision();++impl_->revision;
    }
    return impl_->revision;
}
xl_document_stats Document::stats() const {
    return {sizeof(xl_document_stats), 0, impl_->revision, impl_->nodes.size(), impl_->layouts, impl_->unsupported};
}
DocumentFrame Document::layout(int width, int height, const ScrollOffsets& scrolls) {
    auto environment=impl_->native_environment;environment.width=width;environment.height=height;impl_->update_environment(environment);
    impl_->viewport_width = width; impl_->viewport_height = height;
    (void)focused();
    impl_->refresh_styles();
    impl_->render_canvases();
    auto frame = [&] {
        try { return block_layout(*impl_, width, height, scrolls); }
        catch (const std::length_error&) { throw DocumentFailure{XL_UNSUPPORTED}; }
    }();
    frame.canvas_revision=impl_->canvas_revision;
    frame.focused = impl_->focused;
    frame.focus_visible = impl_->focus_visible;
    if (frame.focused && impl_->text_field(impl_->resolve(frame.focused))) frame.edit = impl_->edit_state(frame.focused);
    build_accessibility(*impl_, frame);
    if(impl_->images)impl_->images->trim();
    ++impl_->layouts;
    impl_->unsupported = frame.unsupported;
    return frame;
}
xl_node_id Document::identity() const { return impl_->identify(lxb_dom_interface_node(impl_->dom)); }
void Document::action(xl_node_id node, uint32_t action) {
    auto* value = impl_->resolve(node);
    if (!impl_->connected(value)) throw DocumentFailure{XL_NOT_FOUND};
    if (impl_->disabled(value) || impl_->inert(value) || impl_->details_hidden(value) ||
        (action != XL_AX_ACTION_FOCUS && accessibility_disabled(*impl_, value)))
        throw DocumentFailure{XL_UNSUPPORTED};
    const auto role = accessibility_role(*impl_, value);
    const bool activatable = role == XL_AX_BUTTON || role == XL_AX_LINK || role == XL_AX_CHECKBOX || role == XL_AX_RADIO;
    const bool focusable = impl_->focusable(value);
    if (action == XL_AX_ACTION_EXPAND || action == XL_AX_ACTION_COLLAPSE) {
        auto* owner = impl_->disclosure_owner(value);
        if (!owner || !focusable) throw DocumentFailure{XL_UNSUPPORTED};
        const auto id = impl_->identify(owner);
        if (action == XL_AX_ACTION_EXPAND) impl_->set_attribute(id, "open", "");
        else impl_->remove_attribute(id, "open");
        return;
    }
    if ((action == XL_AX_ACTION_FOCUS && !focusable) || (action == XL_AX_ACTION_ACTIVATE && !activatable))
        throw DocumentFailure{XL_UNSUPPORTED};
    if (action == XL_AX_ACTION_FOCUS) impl_->set_focus(node);
    else if (action == XL_AX_ACTION_ACTIVATE) impl_->activate(node, true);
    else throw DocumentFailure{XL_UNSUPPORTED};
}
}
