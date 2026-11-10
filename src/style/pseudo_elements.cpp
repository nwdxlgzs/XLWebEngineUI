#include "pseudo_elements.h"
#include "generated_content.h"
#include <algorithm>
#include <exception>

namespace xl::detail {
namespace {
using generated::lower;
using generated::declaration;
struct ContentContext {
    std::function<std::string(std::string_view,const std::optional<std::string>&,std::string_view)> counter;
    std::function<std::string(std::string_view)> quote;
};
struct Content { bool valid = true, supported = true, active = true; std::string text; std::optional<std::string> alternative; };
Content parse_content(std::string_view value, Document::Impl* document = nullptr, lxb_dom_node_t* origin = nullptr, const ContentContext* context = nullptr) {
    Content result;
    auto parsed_tokens = generated::tokenize(value);
    if (!parsed_tokens) { result.valid = false; return result; }
    const auto& tokens = *parsed_tokens;
    if (tokens.size() == 1 && tokens[0].type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
        const auto key = lower(tokens[0].text);
        if (key == "none" || key == "normal" || key == "initial" || key == "unset" || key == "inherit" || key == "revert" || key == "revert-layer") {
            result.active = false; return result;
        }
    }
    std::function<bool(size_t, size_t, std::string&, unsigned, bool, bool)> expression;
    expression = [&](size_t first, size_t last, std::string& output, unsigned depth, bool evaluate, bool alternative) {
        if (first == last || depth > 32) return false;
        for (size_t i = first; i < last; ++i) {
            const auto& token = tokens[i];
            if (token.type == LXB_CSS_SYNTAX_TOKEN_STRING) { if (evaluate) output += token.text; }
            else if (token.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && token.end > i && token.end < last) {
                const auto name = lower(token.text);
                if (name == "attr") {
                    const auto close = token.end;
                    if (i + 1 == close || tokens[i + 1].type != LXB_CSS_SYNTAX_TOKEN_IDENT) return false;
                    const auto attr = tokens[i + 1].text;
                    bool present = false;
                    if (evaluate && document && origin) {
                        present = lxb_dom_element_has_attribute(element(origin), chars(attr), attr.size());
                        if (present) output += document->attribute(document->identify(origin), attr, false);
                    }
                    if (i + 2 < close) {
                        if (tokens[i + 2].type != LXB_CSS_SYNTAX_TOKEN_COMMA) result.supported = false; // Typed/namespaced attr is separate work.
                        else {
                            std::string fallback;
                            if (i + 3 != close && !expression(i + 3, close, fallback, depth + 1, evaluate && !present, alternative)) return false;
                            if (!present) output += fallback;
                        }
                    }
                } else if (name == "counter" || name == "counters") {
                    size_t index = i + 1; const auto close = token.end;
                    if (index == close || tokens[index].type != LXB_CSS_SYNTAX_TOKEN_IDENT || !generated::counter_name(tokens[index].text)) return false;
                    const auto& counter = tokens[index++].text;
                    std::optional<std::string> separator;
                    if (name == "counters") {
                        if (index + 1 >= close || tokens[index].type != LXB_CSS_SYNTAX_TOKEN_COMMA || tokens[index + 1].type != LXB_CSS_SYNTAX_TOKEN_STRING) return false;
                        separator = tokens[index + 1].text; index += 2;
                    }
                    std::string style = "decimal";
                    if (index < close) {
                        if (tokens[index++].type != LXB_CSS_SYNTAX_TOKEN_COMMA || index == close || tokens[index].type != LXB_CSS_SYNTAX_TOKEN_IDENT) return false;
                        style = tokens[index++].text;
                        if (!generated::counter_name(style) && lower(style) != "none") return false;
                    }
                    if (index != close) return false;
                    if (evaluate && context) output += context->counter(counter,separator,style);
                } else if (name == "url" || name == "image" ||
                    name == "image-set" || name == "leader" || name == "target-counter" || name == "target-counters" || name == "target-text" || name == "var") result.supported = false;
                else return false;
                i = token.end;
            } else if (token.type == LXB_CSS_SYNTAX_TOKEN_URL) result.supported = false;
            else if (token.type == LXB_CSS_SYNTAX_TOKEN_IDENT && (lower(token.text) == "open-quote" || lower(token.text) == "close-quote" ||
                lower(token.text) == "no-open-quote" || lower(token.text) == "no-close-quote")) {
                if (alternative) return false;
                if (evaluate && context) output += context->quote(lower(token.text));
            }
            else return false;
            if (output.size() > 65536) return false;
        }
        return true;
    };
    size_t slash = tokens.size();
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (tokens[i].type == LXB_CSS_SYNTAX_TOKEN_FUNCTION) { i = tokens[i].end; continue; }
        if (tokens[i].type == LXB_CSS_SYNTAX_TOKEN_DELIM && tokens[i].text == "/") {
            if (slash != tokens.size()) { result.valid = false; return result; }
            slash = i;
        }
    }
    result.valid = expression(0, slash, result.text, 0, true, false);
    if (slash < tokens.size()) {
        result.alternative.emplace();
        result.valid = result.valid && expression(slash + 1, tokens.size(), *result.alternative, 0, true, true);
    }
    return result;
}
}
struct PseudoElements::Impl {
    struct Entry {
        lxb_dom_node_t *origin = nullptr, *node = nullptr, *text = nullptr;
        bool active = false; uint64_t unsupported = 0; std::optional<std::string> alternative;
        ~Entry() { if (node) lxb_dom_node_destroy_deep(node); }
    };
    Document::Impl& document;
    std::unordered_map<lxb_dom_node_t*, std::array<std::unique_ptr<Entry>, 2>> entries;
    std::unordered_map<lxb_dom_node_t*, Entry*> owned;
    explicit Impl(Document::Impl& doc) : document(doc) {}
};
PseudoElements::PseudoElements(Document::Impl& doc) : impl_(std::make_unique<Impl>(doc)) {}
PseudoElements::~PseudoElements() = default;
lxb_dom_node_t* PseudoElements::ensure(lxb_dom_node_t* origin, PseudoKind kind) {
    if (kind == PseudoKind::none || generated(origin)) throw DocumentFailure{XL_INVALID_ARGUMENT};
    auto& pointer = impl_->entries[origin][unsigned(kind) - 1];
    if (pointer) return pointer->node;
    if (impl_->owned.size() >= 65536) throw DocumentFailure{XL_UNSUPPORTED};
    auto entry = std::make_unique<Impl::Entry>(); entry->origin = origin;
    entry->node = lxb_dom_interface_node(lxb_html_document_create_element(impl_->document.dom, chars("span"), 4, nullptr));
    if (!entry->node) throw std::bad_alloc();
    entry->text = lxb_dom_interface_node(lxb_dom_document_create_text_node(&impl_->document.dom->dom_document, chars(""), 0));
    if (!entry->text) throw std::bad_alloc();
    if (lxb_dom_node_append_child(entry->node, entry->text) != LXB_DOM_EXCEPTION_OK) {
        lxb_dom_node_destroy(entry->text); throw DocumentFailure{XL_INTERNAL_ERROR};
    }
    try {
        impl_->owned.emplace(entry->node,entry.get()); impl_->owned.emplace(entry->text,entry.get());
        impl_->document.identify(entry->node); impl_->document.identify(entry->text);
    } catch (...) {
        for (auto* node : {entry->node,entry->text}) {
            impl_->owned.erase(node);
            const auto identity = impl_->document.identities.find(node);
            if (identity != impl_->document.identities.end()) {
                impl_->document.nodes.erase(identity->second); impl_->document.identities.erase(identity);
            }
        }
        throw;
    }
    pointer = std::move(entry);
    return pointer->node;
}
lxb_dom_node_t* PseudoElements::get(lxb_dom_node_t* origin, PseudoKind kind) const {
    const auto found = impl_->entries.find(origin);
    if (kind == PseudoKind::none || found == impl_->entries.end()) return nullptr;
    const auto& entry = found->second[unsigned(kind) - 1];
    return entry && entry->active ? entry->node : nullptr;
}
lxb_dom_node_t* PseudoElements::origin(lxb_dom_node_t* node) const {
    const auto found = impl_->owned.find(node); return found == impl_->owned.end() ? nullptr : found->second->origin;
}
bool PseudoElements::generated(lxb_dom_node_t* node) const { return impl_->owned.contains(node); }
std::vector<lxb_dom_element_t*> PseudoElements::elements() const {
    std::vector<lxb_dom_element_t*> result;
    for (const auto& [origin, pair] : impl_->entries) for (const auto& entry : pair) if (entry) result.push_back(element(entry->node));
    return result;
}
void PseudoElements::reset() {
    for (auto& [origin, pair] : impl_->entries) for (auto& entry : pair) if (entry) {
        auto* target = element(entry->node);
        while (target->style) lxb_dom_element_style_remove_all(target, reinterpret_cast<lxb_style_node_t*>(target->style));
        // Emptying textContent only changes length and retains its old capacity.
        // Return the allocation to the document arena; keep the node/AX ID.
        auto* text = lxb_dom_interface_character_data(entry->text);
        lexbor_str_destroy(&text->data, entry->text->owner_document->text, false);
        entry->active = false; entry->unsupported = 0; entry->alternative.reset();
    }
}
void PseudoElements::compute() {
    auto& doc = impl_->document;
    size_t budget = 4 * 1024 * 1024;
    std::vector<lxb_dom_node_t*> roots{lxb_dom_interface_node(doc.dom)};
    for (auto* root : doc.detached) if (!root->parent) roots.push_back(root);
    for (auto* root : roots) {
        generated::Counters counters(doc);
        for (const bool preview : {true, false}) {
            counters.begin_pass(preview);
            unsigned quote_depth = 0;
            std::function<generated::Counters::Set(lxb_dom_node_t*,lxb_dom_node_t*,const generated::Counters::Set&,unsigned)> visit;
            visit = [&](lxb_dom_node_t* node, lxb_dom_node_t* parent, const generated::Counters::Set& source, unsigned depth) {
                if (depth > 256) throw DocumentFailure{XL_UNSUPPORTED};
                auto state = source;
                const auto entry_it = impl_->owned.find(node);
                Impl::Entry* entry = entry_it == impl_->owned.end() ? nullptr : entry_it->second;
                if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
                    const auto* display = lxb_dom_element_style_by_id(element(node),LXB_CSS_PROPERTY_DISPLAY);
                    if ((display && display->u.display->a == LXB_CSS_VALUE_NONE) ||
                        (!entry && (lxb_dom_element_has_attribute(element(node),chars("hidden"),6) || doc.details_hidden(node) ||
                          node->local_name == LXB_TAG_HEAD || node->local_name == LXB_TAG_STYLE || node->local_name == LXB_TAG_SCRIPT ||
                          node->local_name == LXB_TAG_TEMPLATE ||
                          (node->local_name == LXB_TAG_INPUT && doc.attribute(doc.identify(node),"type",false) == "hidden")))) return state;
                    std::string value;
                    if (entry) {
                        value = declaration(node,"content");
                        const auto tokens = generated::tokenize(value); const auto key = tokens ? generated::keyword(*tokens) : std::string{};
                        const bool ua_default = value.empty() || key == "revert" || key == "revert-layer";
                        if (key == "inherit") value = generated::property(doc,entry->origin,"content");
                        if (ua_default && entry->origin->local_name == LXB_TAG_Q)
                            value = impl_->entries.at(entry->origin)[0].get() == entry ? "open-quote" : "close-quote";
                        if (value.empty()) return state;
                        const auto syntax = parse_content(value);
                        if (!syntax.valid || !syntax.supported) { if (!preview) ++entry->unsupported; return state; }
                        if (!syntax.active) return state;
                    }
                    // Counters are ordered by the box-generating document tree,
                    // independent of hash iteration, flex order or AX suppression.
                    if (!display || display->u.display->a != LXB_CSS_VALUE_CONTENTS) counters.apply(state,node,parent);
                    if (entry) {
                        auto quote_style = generated::quotes(generated::property(doc,node,"quotes")).value_or(generated::Quotes{});
                        if (quote_style.automatic) quote_style = generated::auto_quotes(doc,entry->origin);
                        const ContentContext context{
                            [&](auto name, const auto& separator, auto style) { return counters.output(state,node,parent,name,separator,style); },
                            [&](std::string_view key) {
                                const bool opening = key == "open-quote" || key == "no-open-quote";
                                const bool emitting = key == "open-quote" || key == "close-quote";
                                const auto level = quote_depth;
                                if (opening) { if (quote_depth >= 65536) throw DocumentFailure{XL_UNSUPPORTED}; ++quote_depth; }
                                else if (quote_depth) --quote_depth;
                                if (preview || !emitting || (!opening && !level) || quote_style.pairs.empty()) return std::string{};
                                const auto index = std::min<size_t>(opening ? level : quote_depth,quote_style.pairs.size() - 1);
                                return opening ? quote_style.pairs[index].first : quote_style.pairs[index].second;
                            }};
                        auto parsed = parse_content(value,&doc,entry->origin,&context);
                        if (!parsed.valid) throw DocumentFailure{XL_UNSUPPORTED};
                        if (!preview) {
                            const auto bytes = parsed.text.size() + (parsed.alternative ? parsed.alternative->size() : 0);
                            if (bytes > budget) throw DocumentFailure{XL_UNSUPPORTED}; budget -= bytes;
                            check_lxb(lxb_dom_node_text_content_set(entry->text,chars(parsed.text),parsed.text.size()));
                            entry->active = true; entry->alternative = std::move(parsed.alternative);
                        }
                        return state;
                    }
                }
                auto previous = state;
                if (node->type == LXB_DOM_NODE_TYPE_ELEMENT && !generated::replaced(node)) {
                    if (const auto found = impl_->entries.find(node); found != impl_->entries.end() && found->second[0])
                        previous = visit(found->second[0]->node,node,previous,depth + 1);
                }
                for (auto* child = node->first_child; child; child = child->next)
                    if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) previous = visit(child,node,previous,depth + 1);
                if (node->type == LXB_DOM_NODE_TYPE_ELEMENT && !generated::replaced(node)) {
                    if (const auto found = impl_->entries.find(node); found != impl_->entries.end() && found->second[1])
                        visit(found->second[1]->node,node,previous,depth + 1);
                }
                return state;
            };
            visit(root,nullptr,{},0);
            if (preview) counters.finish_preview();
        }
    }
}
uint64_t PseudoElements::unsupported(lxb_dom_node_t* origin) const {
    const auto found = impl_->entries.find(origin); uint64_t result = 0;
    if (found != impl_->entries.end()) for (const auto& entry : found->second) if (entry) result += entry->unsupported;
    return result;
}
std::optional<std::string> PseudoElements::alternative(lxb_dom_node_t* node) const {
    const auto found = impl_->owned.find(node); return found == impl_->owned.end() ? std::optional<std::string>{} : found->second->alternative;
}
std::string PseudoElements::content(lxb_dom_node_t* node) const {
    const auto found = impl_->owned.find(node);
    if (found == impl_->owned.end()) return "none";
    const auto& entry = *found->second;
    if (!entry.active) return entry.unsupported ? declaration(node, "content") : "none";
    auto result = generated::quote_string(impl_->document.text(impl_->document.identify(entry.text)));
    if (entry.alternative) result += " / " + generated::quote_string(*entry.alternative);
    return result;
}
bool PseudoElements::valid_content(std::string_view value) { return parse_content(value).valid; }
bool PseudoElements::supported_selector(lxb_css_selector_list_t* list) {
    for (; list; list = list->next)
        if (list->last && list->last->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT &&
            (list->last->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE || list->last->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_ELEMENT_AFTER)) return true;
    return false;
}
SelectorCandidates::SelectorCandidates(Document::Impl& document,const std::vector<lxb_dom_element_t*>& elements):document_(document) {
    for(auto* target:elements) {
        auto* node=lxb_dom_interface_node(target);if(!document.connected(node))continue;
        nodes_["t"+std::to_string(node->local_name)].push_back(node);
        const auto add=[&](char prefix,const lxb_dom_attr_t* attribute) {
            if(!attribute||!attribute->value)return;
            std::string_view value(reinterpret_cast<const char*>(attribute->value->data),attribute->value->length);
            if(prefix=='#'){nodes_["#"+lower(std::string(value))].push_back(node);return;}
            size_t first=0;
            while(first<value.size()) {
                const auto start=value.find_first_not_of(" \t\r\n\f",first);if(start==std::string_view::npos)break;
                auto end=value.find_first_of(" \t\r\n\f",start);if(end==std::string_view::npos)end=value.size();
                auto& nodes=nodes_["."+lower(std::string(value.substr(start,end-start)))];
                if(nodes.empty()||nodes.back()!=node)nodes.push_back(node);
                first=end;
            }
        };
        add('#',target->attr_id);add('.',target->attr_class);
    }
}
const std::vector<lxb_dom_node_t*>* SelectorCandidates::find(lxb_css_selector_list_t* list) const {
    const std::vector<lxb_dom_node_t*>* best=nullptr;
    for(auto* item=list->last;item;item=item->prev) {
        std::string key;
        const auto name=std::string_view(reinterpret_cast<const char*>(item->name.data),item->name.length);
        if(item->type==LXB_CSS_SELECTOR_TYPE_ID)key="#"+lower(std::string(name));
        else if(item->type==LXB_CSS_SELECTOR_TYPE_CLASS)key="."+lower(std::string(name));
        else if(item->type==LXB_CSS_SELECTOR_TYPE_ELEMENT)key="t"+std::to_string(lxb_tag_id_by_name(document_.dom->dom_document.tags,chars(name),name.size()));
        if(!key.empty()) {
            const auto found=nodes_.find(key);const auto* nodes=found==nodes_.end()?&empty_:&found->second;
            if(!best||nodes->size()<best->size())best=nodes;
        }
        if(item->combinator!=LXB_CSS_SELECTOR_COMBINATOR_CLOSE)break;
    }
    return best;
}
void PseudoElements::match(Document::Impl& doc, lxb_css_selector_list_t* list,
    const std::function<void(lxb_dom_node_t*, PseudoKind, lxb_css_selector_specificity_t)>& callback,
    const SelectorCandidates* candidates) {
    for (auto* current = list; current; current = current->next) {
        PseudoKind kind = PseudoKind::none;
        lxb_css_selector_t* pseudo = nullptr; bool invalid = false;
        for (auto* item = current->first; item; item = item->next) {
            if (item->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT_FUNCTION) { invalid = true; break; }
            if (item->type != LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT) continue;
            if (item != current->last || pseudo) { invalid = true; break; }
            pseudo = item;
            kind = item->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_ELEMENT_BEFORE ? PseudoKind::before :
                item->u.pseudo.type == LXB_CSS_SELECTOR_PSEUDO_ELEMENT_AFTER ? PseudoKind::after : PseudoKind::none;
            if (kind == PseudoKind::none) invalid = true;
        }
        if (invalid) continue;
        struct Restore {
            lxb_css_selector_list_t* list; lxb_css_selector_list_t* next; lxb_css_selector_t* pseudo;
            ~Restore() { list->next = next; if (pseudo) pseudo->type = LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT; }
        } restore{current, current->next, pseudo};
        current->next = nullptr;
        if (pseudo) pseudo->type = LXB_CSS_SELECTOR_TYPE_ANY;
        struct Context { decltype(callback) function; PseudoKind kind; std::exception_ptr error; } context{callback, kind, {}};
        const auto matched=[](lxb_dom_node_t* node, lxb_css_selector_specificity_t specificity, void* opaque) -> lxb_status_t {
                auto& context = *static_cast<Context*>(opaque);
                try { context.function(node, context.kind, specificity); return LXB_STATUS_OK; }
                catch (...) { context.error = std::current_exception(); return LXB_STATUS_ERROR; }
            };
        const auto* nodes=candidates?candidates->find(current):nullptr;
        lxb_status_t status=LXB_STATUS_OK;
        if(nodes)for(auto* node:*nodes) {
            status=lxb_selectors_match_node(doc.dom->dom_document.css->selectors,node,current,matched,&context);
            if(status!=LXB_STATUS_OK)break;
        }
        else status=lxb_selectors_find(doc.dom->dom_document.css->selectors,lxb_dom_interface_node(doc.dom),current,matched,&context);
        if (context.error) std::rethrow_exception(context.error);
        check_lxb(status);
    }
}
PseudoKind pseudo_kind(std::string_view name) {
    const auto key = lower(std::string(name));
    if (key == "::before" || key == ":before") return PseudoKind::before;
    if (key == "::after" || key == ":after") return PseudoKind::after;
    throw DocumentFailure{XL_INVALID_ARGUMENT};
}
std::vector<lxb_dom_node_t*> Document::Impl::render_children(lxb_dom_node_t* node) const {
    std::vector<lxb_dom_node_t*> result;
    if (pseudos->generated(node)) {
        for (auto* child = node->first_child; child; child = child->next) result.push_back(child);
        return result;
    }
    const bool replaced = generated::replaced(node);
    if (!replaced) if (auto* before = pseudos->get(node, PseudoKind::before)) result.push_back(before);
    auto* summary = details_summary(node);
    if (summary) result.push_back(summary);
    for (auto* child = node->first_child; child; child = child->next)
        if (child != summary && !details_hidden(child)) result.push_back(child);
    if (!replaced) if (auto* after = pseudos->get(node, PseudoKind::after)) result.push_back(after);
    return result;
}
std::string Document::Impl::accessible_text(lxb_dom_node_t* root) const {
    std::string result;
    std::vector<std::pair<lxb_dom_node_t*, unsigned>> pending{{root, 0}};
    while (!pending.empty()) {
        const auto [node, depth] = pending.back(); pending.pop_back();
        if (depth > 256) throw DocumentFailure{XL_UNSUPPORTED};
        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            result += text(const_cast<Impl*>(this)->identify(node));
            if (result.size() > 1024 * 1024) throw DocumentFailure{XL_UNSUPPORTED};
            continue;
        }
        if (node->type != LXB_DOM_NODE_TYPE_ELEMENT || node->local_name == LXB_TAG_STYLE || node->local_name == LXB_TAG_SCRIPT) continue;
        if (node != root && (lxb_dom_element_has_attribute(element(node), chars("hidden"), 6) || attribute(const_cast<Impl*>(this)->identify(node), "aria-hidden", false) == "true")) continue;
        const auto* display = lxb_dom_element_style_by_id(element(node), LXB_CSS_PROPERTY_DISPLAY);
        if (display && display->u.display->a == LXB_CSS_VALUE_NONE) continue;
        if (const auto alternative = pseudos->alternative(node)) { result += *alternative; continue; }
        const auto children = render_children(node);
        for (auto it = children.rbegin(); it != children.rend(); ++it) pending.emplace_back(*it, depth + 1);
        if (result.size() > 1024 * 1024) throw DocumentFailure{XL_UNSUPPORTED};
    }
    return result;
}
std::string Document::Impl::pseudo_style(xl_node_id id, std::string_view pseudo, std::string_view name) {
    refresh_styles();
    auto* origin = resolve(id); element(origin);
    const auto kind = pseudo_kind(pseudo);
    auto* target = pseudos->ensure(origin, kind);
    if (name == "content") {
        const auto value = pseudos->content(target);
        return value.empty() ? "none" : value;
    }
    if (name == "display") {
        const auto* value = lxb_dom_element_style_by_id(element(target), LXB_CSS_PROPERTY_DISPLAY);
        if (!value) return "inline";
    }
    const bool inherited = name.starts_with("--") || name == "color" || name == "font-family" || name == "font-size" || name == "font-weight" ||
        name == "font-style" || name == "line-height" || name == "direction" || name == "white-space";
    try {
        const auto value = style(identify(target), name);
        if (value == "inherit" || (value == "unset" && inherited)) return style(id, name);
        return value;
    }
    catch (const DocumentFailure& error) {
        if (error.status != XL_NOT_FOUND) throw;
        if (inherited) return style(id, name);
        throw;
    }
}
std::string Document::pseudo_style(xl_node_id id, std::string_view pseudo, std::string_view name) { return impl_->pseudo_style(id, pseudo, name); }
}
