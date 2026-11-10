#include "generated_content.h"
#include "pseudo_elements.h"
#include <algorithm>
#include <limits>

namespace xl::detail::generated {
namespace {
int32_t clamp(int64_t value) { return static_cast<int32_t>(std::clamp<int64_t>(value, INT32_MIN, INT32_MAX)); }
bool wide(std::string_view key) {
    return key == "initial" || key == "inherit" || key == "unset" || key == "revert" || key == "revert-layer";
}
std::optional<int32_t> integer(std::string_view text, bool html = false) {
    size_t index = 0;
    if (html) while (index < text.size() && (text[index] == ' ' || text[index] == '\t' || text[index] == '\n' || text[index] == '\r' || text[index] == '\f')) ++index;
    bool negative = false;
    if (index < text.size() && (text[index] == '+' || text[index] == '-')) negative = text[index++] == '-';
    const auto start = index; int64_t value = 0;
    while (index < text.size() && text[index] >= '0' && text[index] <= '9') value = std::min<int64_t>(INT64_C(2147483648), value * 10 + text[index++] - '0');
    if (start == index || (!html && index != text.size())) return {};
    return clamp(negative ? -value : value);
}
std::string represent(int32_t value, std::string_view input) {
    const auto style = lower(std::string(input));
    if (style == "none") return {};
    if (style == "disc") return "\xe2\x80\xa2";
    if (style == "circle") return "\xe2\x97\xa6";
    if (style == "square") return "\xe2\x96\xaa";
    if (style == "disclosure-open") return "\xe2\x96\xbe";
    if (style == "disclosure-closed") return "\xe2\x96\xb8";
    if (style == "lower-alpha" || style == "upper-alpha" || style == "lower-latin" || style == "upper-latin" || style == "lower-greek") {
        if (value <= 0) return std::to_string(value);
        constexpr const char* greek[]{"\xce\xb1", "\xce\xb2", "\xce\xb3", "\xce\xb4", "\xce\xb5", "\xce\xb6", "\xce\xb7", "\xce\xb8", "\xce\xb9", "\xce\xba", "\xce\xbb", "\xce\xbc", "\xce\xbd", "\xce\xbe", "\xce\xbf", "\xcf\x80", "\xcf\x81", "\xcf\x83", "\xcf\x84", "\xcf\x85", "\xcf\x86", "\xcf\x87", "\xcf\x88", "\xcf\x89"};
        const bool is_greek = style == "lower-greek"; const auto base = is_greek ? 24 : 26;
        std::string result;
        for (int64_t rest = value; rest; rest /= base) {
            const auto digit = --rest % base;
            result = (is_greek ? greek[digit] : std::string(1, char((style.starts_with("upper") ? 'A' : 'a') + digit))) + result;
        }
        return result;
    }
    if ((style == "lower-roman" || style == "upper-roman") && value > 0 && value < 4000) {
        constexpr std::pair<int, const char*> digits[]{{1000,"M"},{900,"CM"},{500,"D"},{400,"CD"},{100,"C"},{90,"XC"},{50,"L"},{40,"XL"},{10,"X"},{9,"IX"},{5,"V"},{4,"IV"},{1,"I"}};
        std::string result;
        for (const auto& [number, letters] : digits) while (value >= number) { result += letters; value -= number; }
        return style == "lower-roman" ? lower(result) : result;
    }
    std::string decimal = std::to_string(value);
    if (style == "decimal-leading-zero" && value > -10 && value < 10) decimal.insert(value < 0 ? 1 : 0, "0");
    if (style == "cjk-decimal" || style == "arabic-indic" || style == "persian" || style == "devanagari" || style == "bengali" || style == "thai") {
        constexpr const char* cjk[]{"\xe3\x80\x87","\xe4\xb8\x80","\xe4\xba\x8c","\xe4\xb8\x89","\xe5\x9b\x9b","\xe4\xba\x94","\xe5\x85\xad","\xe4\xb8\x83","\xe5\x85\xab","\xe4\xb9\x9d"};
        const uint32_t first = style == "arabic-indic" ? 0x660 : style == "persian" ? 0x6f0 : style == "devanagari" ? 0x966 : style == "bengali" ? 0x9e6 : 0xe50;
        std::string result;
        for (const auto c : decimal) {
            if (c == '-') { result += style == "cjk-decimal" ? "\xe8\xb4\x9f" : "-"; continue; }
            if (style == "cjk-decimal") result += cjk[c - '0'];
            else {
                const auto code = first + c - '0';
                if (code < 0x800) { result += char(0xc0 | code >> 6); result += char(0x80 | (code & 63)); }
                else { result += char(0xe0 | code >> 12); result += char(0x80 | ((code >> 6) & 63)); result += char(0x80 | (code & 63)); }
            }
        }
        return result;
    }
    // Undefined @counter-style names fall back to decimal.
    return decimal;
}
}
std::string lower(std::string value) { for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A'; return value; }
std::string keyword(const Tokens& tokens) { return tokens.size() == 1 && tokens[0].type == LXB_CSS_SYNTAX_TOKEN_IDENT ? lower(tokens[0].text) : std::string{}; }
bool counter_name(std::string_view name) { const auto key = lower(std::string(name)); return !wide(key) && key != "none" && key != "default"; }
std::optional<Tokens> tokenize(std::string_view value) {
    if (value.size() > 65536) return {};
    auto* parser = lxb_css_syntax_tokenizer_create(); if (!parser) throw std::bad_alloc();
    struct End { lxb_css_syntax_tokenizer_t* p; ~End() { lxb_css_syntax_tokenizer_destroy(p); } } end{parser};
    check_lxb(lxb_css_syntax_tokenizer_init(parser)); lxb_css_syntax_tokenizer_buffer_set(parser, chars(value), value.size());
    Tokens result; std::vector<size_t> openings; bool space_before = false;
    while (auto* token = lxb_css_syntax_token(parser)) {
        if (token->type == LXB_CSS_SYNTAX_TOKEN__EOF) break;
        if (token->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE || token->type == LXB_CSS_SYNTAX_TOKEN_COMMENT) {
            space_before |= token->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE;
            lxb_css_syntax_token_consume(parser); continue;
        }
        const auto* base = lxb_css_syntax_token_base(token);
        std::string text(reinterpret_cast<const char*>(base->begin), base->length);
        if (token->type == LXB_CSS_SYNTAX_TOKEN_STRING || token->type == LXB_CSS_SYNTAX_TOKEN_IDENT || token->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION) {
            const auto* string = lxb_css_syntax_token_string(token); text.assign(reinterpret_cast<const char*>(string->data), string->length);
        }
        result.push_back({token->type, std::move(text), 0, space_before}); space_before = false; const auto index = result.size() - 1;
        if (token->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || token->type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS) openings.push_back(index);
        if (token->type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS) {
            if (openings.empty()) return {};
            result[openings.back()].end = index; openings.pop_back();
        }
        if (result.size() >= 8192) return {};
        lxb_css_syntax_token_consume(parser);
    }
    check_lxb(parser->status);
    while (!openings.empty()) { const auto index = openings.back(); openings.pop_back(); result[index].end = result.size(); result.push_back({LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS,")",0}); }
    return result;
}
std::optional<std::vector<Change>> changes(std::string_view value, std::string_view name) {
    if(value=="none")return std::vector<Change>{};
    const auto tokens = tokenize(value); if (!tokens || tokens->empty()) return {};
    const auto key = keyword(*tokens);
    if (key == "none" || wide(key)) return std::vector<Change>{};
    std::vector<Change> result;
    for (size_t i = 0; i < tokens->size(); ++i) {
        const auto& token = (*tokens)[i]; Change change;
        if (name == "counter-reset" && token.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && lower(token.text) == "reversed" && token.end == i + 2 && (*tokens)[i + 1].type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
            change.name = (*tokens)[i + 1].text; change.reversed = change.automatic = true; i = token.end;
        } else if (token.type == LXB_CSS_SYNTAX_TOKEN_IDENT) change.name = token.text;
        else return {};
        if (!counter_name(change.name)) return {};
        change.value = name == "counter-increment" ? 1 : 0;
        if (i + 1 < tokens->size() && (*tokens)[i + 1].type == LXB_CSS_SYNTAX_TOKEN_NUMBER) {
            const auto number = integer((*tokens)[++i].text); if (!number) return {};
            change.value = *number; change.automatic = false;
        }
        result.push_back(std::move(change));
    }
    return result;
}
std::optional<Quotes> quotes(std::string_view value) {
    const auto tokens = tokenize(value); if (!tokens || tokens->empty()) return {};
    const auto key = keyword(*tokens);
    if (key == "auto" || wide(key)) return Quotes{};
    if (key == "none") return Quotes{false,{}};
    if (tokens->size() % 2 || tokens->size() > 256) return {};
    Quotes result; result.automatic = false;
    for (size_t i = 0; i < tokens->size(); i += 2) {
        if ((*tokens)[i].type != LXB_CSS_SYNTAX_TOKEN_STRING || (*tokens)[i + 1].type != LXB_CSS_SYNTAX_TOKEN_STRING) return {};
        result.pairs.emplace_back((*tokens)[i].text, (*tokens)[i + 1].text);
    }
    return result;
}
bool managed_property(std::string_view name) { return name == "quotes" || name == "counter-reset" || name == "counter-increment" || name == "counter-set"; }
bool valid_property(std::string_view name, std::string_view value) { return name == "quotes" ? quotes(value).has_value() : changes(value, name).has_value(); }
std::string declaration(lxb_dom_node_t* node, std::string_view name) {
    const auto* value = lxb_dom_element_style_by_name(element(node), chars(name), name.size()); if (!value) return {};
    std::string result;
    check_lxb(lxb_css_property_serialize(value->u.user, static_cast<lxb_css_property_type_t>(value->type),
        [](const lxb_char_t* bytes, size_t size, void* opaque) -> lxb_status_t {
            try { static_cast<std::string*>(opaque)->append(reinterpret_cast<const char*>(bytes), size); return LXB_STATUS_OK; }
            catch (...) { return LXB_STATUS_ERROR_MEMORY_ALLOCATION; }
        }, &result));
    return result;
}
std::string property(Document::Impl& doc, lxb_dom_node_t* node, std::string_view name) {
    const std::string initial = name == "quotes" ? "auto" : name == "content" ? "normal" : "none";
    for (unsigned depth = 0; node && depth < 256; ++depth) {
        const auto value = declaration(node, name);
        if(value.empty()) {
            if(name!="quotes")return initial;
            auto* parent=doc.pseudos->origin(node);if(!parent)parent=node->parent;
            node=parent&&parent->type==LXB_DOM_NODE_TYPE_ELEMENT?parent:nullptr;continue;
        }
        const auto tokens = tokenize(value); const auto key = tokens ? keyword(*tokens) : std::string{};
        if (!value.empty() && !wide(key)) return value;
        if (key == "initial" || key == "revert" || key == "revert-layer" || (key == "unset" && name != "quotes")) return initial;
        if (name != "quotes" && key != "inherit") return initial;
        auto* parent = doc.pseudos->origin(node); if (!parent) parent = node->parent;
        node = parent && parent->type == LXB_DOM_NODE_TYPE_ELEMENT ? parent : nullptr;
    }
    return initial;
}
std::string quote_string(std::string_view value) {
    std::string result = "\""; constexpr char hex[] = "0123456789abcdef";
    for (const auto c : value) {
        const auto b = static_cast<unsigned char>(c);
        if (c == '\\' || c == '"') { result += '\\'; result += c; }
        else if (b < 32 || b == 127) { result += '\\'; result += hex[b >> 4]; result += hex[b & 15]; result += ' '; }
        else result += c;
    }
    return result + '"';
}
Quotes auto_quotes(Document::Impl& doc, lxb_dom_node_t* node) {
    struct Entry { std::string_view locale; std::array<const char*,4> values; };
    static constexpr Entry table[]{
#include "../../third_party/data/icu-quotes.inc"
    };
    std::string language;
    for (unsigned depth = 0; node && depth < 256; ++depth, node = node->parent) {
        if (node->type != LXB_DOM_NODE_TYPE_ELEMENT) continue;
        if (lxb_dom_element_has_attribute(element(node), chars("lang"), 4)) { language = lower(doc.attribute(doc.identify(node), "lang", false)); break; }
    }
    // ICU's available-locale list uses explicit scripts for these aliases.
    // BCP47 region-only Chinese tags still select the traditional script.
    if (language == "zh-tw" || language.starts_with("zh-tw-") || language == "zh-hk" || language.starts_with("zh-hk-") || language == "zh-mo" || language.starts_with("zh-mo-")) language = "zh-hant";
    const Entry* found = nullptr;
    for (auto key = language; !key.empty();) {
        const auto item = std::lower_bound(std::begin(table), std::end(table), key, [](const Entry& entry, const std::string& name) { return entry.locale < name; });
        if (item != std::end(table) && item->locale == key) { found = item; break; }
        const auto dash = key.rfind('-'); if (dash == std::string::npos) break; key.resize(dash);
    }
    if (!found) for (const auto& entry : table) if (entry.locale == "root") { found = &entry; break; }
    return Quotes{false,{{found->values[0],found->values[1]}, {found->values[2],found->values[3]}}};
}
bool replaced(lxb_dom_node_t* node) {
    return node->local_name == LXB_TAG_IMG || node->local_name == LXB_TAG_INPUT || node->local_name == LXB_TAG_TEXTAREA ||
        node->local_name == LXB_TAG_VIDEO || node->local_name == LXB_TAG_AUDIO || node->local_name == LXB_TAG_IFRAME || node->local_name == LXB_TAG_EMBED;
}
void Counters::begin_pass(bool preview) { owned_.clear(); preview_ = preview; }
void Counters::finish_preview() {
    for (const auto& counter : owned_) if (counter->automatic) initial_[counter->creator][counter->name] = clamp(counter->total + counter->last);
}
Counters::Counter* Counters::find(Set& state, std::string_view name) {
    for (auto it = state.rbegin(); it != state.rend(); ++it) if ((*it)->name == name) return *it;
    return nullptr;
}
Counters::Counter* Counters::instantiate(Set& state, lxb_dom_node_t* node, lxb_dom_node_t* parent, const Change& change) {
    if (auto* previous = find(state, change.name)) {
        auto* previous_parent = document_.pseudos->origin(previous->creator);
        if (!previous_parent) previous_parent = previous->creator->parent;
        if (previous->creator == node || previous_parent == parent) std::erase(state, previous);
    }
    if (state.size() >= 256 || owned_.size() >= 65536) throw DocumentFailure{XL_UNSUPPORTED};
    auto counter = std::make_unique<Counter>(Counter{change.name, node, change.value, change.reversed, change.automatic});
    if (change.automatic && !preview_) counter->value = initial_.at(node).at(change.name);
    auto* result = counter.get(); owned_.push_back(std::move(counter)); state.push_back(result); return result;
}
void Counters::apply(Set& state, lxb_dom_node_t* node, lxb_dom_node_t* parent) {
    const bool pseudo = document_.pseudos->generated(node);
    auto reset = changes(property(document_, node, "counter-reset"), "counter-reset").value_or(std::vector<Change>{});
    const auto ua_default = [&](std::string_view name) {
        const auto value = declaration(node,name); const auto tokens = tokenize(value); const auto key = tokens ? keyword(*tokens) : std::string{};
        return value.empty() || key == "revert" || key == "revert-layer";
    };
    if (!pseudo && ua_default("counter-reset") && (node->local_name == LXB_TAG_OL || node->local_name == LXB_TAG_UL)) {
        Change list{"list-item",0}; list.reversed = node->local_name == LXB_TAG_OL && lxb_dom_element_has_attribute(element(node),chars("reversed"),8);
        const auto start = integer(document_.attribute(document_.identify(node),"start",false),true);
        if (node->local_name == LXB_TAG_OL && start) list.value = clamp(int64_t(*start) + (list.reversed ? 1 : -1));
        else list.automatic = list.reversed;
        reset.push_back(std::move(list));
    }
    for (const auto& change : reset) instantiate(state, node, parent, change);
    auto increments = changes(property(document_,node,"counter-increment"),"counter-increment").value_or(std::vector<Change>{});
    const auto* display = lxb_dom_element_style_by_id(element(node),LXB_CSS_PROPERTY_DISPLAY);
    const bool list_item = display ? display->u.display->a == LXB_CSS_VALUE_LIST_ITEM || display->u.display->c == LXB_CSS_VALUE_LIST_ITEM : !pseudo && node->local_name == LXB_TAG_LI;
    if (list_item && std::none_of(increments.begin(),increments.end(),[](const Change& change) {return change.name == "list-item";})) {
        const auto* counter = find(state,"list-item"); increments.push_back({"list-item",counter && counter->reversed ? -1 : 1});
    }
    struct Event { Counter* counter; int64_t increment = 0; std::optional<int32_t> set; };
    std::vector<Event> events;
    const auto event = [&](Counter* counter) -> Event& {
        for (auto& item : events) if (item.counter == counter) return item;
        events.push_back({counter,0,{}}); return events.back();
    };
    for (const auto& change : increments) {
        auto* counter = find(state,change.name); if (!counter) counter = instantiate(state,node,parent,{change.name,0});
        event(counter).increment += change.value;
    }
    for (const auto& item : events) item.counter->value = clamp(int64_t(item.counter->value) + item.increment);
    auto sets = changes(property(document_,node,"counter-set"),"counter-set").value_or(std::vector<Change>{});
    if (!pseudo && ua_default("counter-set") && node->local_name == LXB_TAG_LI)
        if (const auto value = integer(document_.attribute(document_.identify(node),"value",false),true)) sets.push_back({"list-item",*value});
    for (const auto& change : sets) {
        auto* counter = find(state,change.name); if (!counter) counter = instantiate(state,node,parent,{change.name,0});
        counter->value = change.value; event(counter).set = change.value;
    }
    if (preview_) for (const auto& item : events) if (item.counter->automatic && !item.counter->stopped) {
        if (item.increment) item.counter->last = -item.increment;
        item.counter->total += item.set ? *item.set : -item.increment;
        item.counter->stopped = item.set.has_value();
    }
}
std::string Counters::output(Set& state, lxb_dom_node_t* node, lxb_dom_node_t* parent, std::string_view name, const std::optional<std::string>& separator, std::string_view style) {
    auto* counter = find(state,name); if (!counter) counter = instantiate(state,node,parent,{std::string(name),0});
    if (preview_) return {};
    if (!separator) return represent(counter->value,style);
    std::string result; bool first = true;
    for (const auto* item : state) if (item->name == name) {
        const auto part = represent(item->value,style);
        if (result.size() + (first ? 0 : separator->size()) + part.size() > 65536) throw DocumentFailure{XL_UNSUPPORTED};
        if (!first) result += *separator; result += part; first = false;
    }
    return result;
}
}
