#include "border_shorthand.h"
#include "color.h"
#include <array>
#include <algorithm>

namespace xl::detail {
namespace {
std::string lower(std::string value) {
    for (auto& c : value)
        if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return value;
}
lxb_status_t append(const lxb_char_t* data, size_t size, void* ctx) {
    try {
        static_cast<std::string*>(ctx)->append(reinterpret_cast<const char*>(data), size);
        return LXB_STATUS_OK;
    } catch (...) {
        return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
    }
}
// Split CSS components, retaining an entire color function and its original escapes.
std::vector<std::string> components(std::string_view value) {
    auto* tokenizer = lxb_css_syntax_tokenizer_create();
    if (!tokenizer) throw std::bad_alloc();
    struct Cleanup {
        lxb_css_syntax_tokenizer_t* p;
        ~Cleanup() { lxb_css_syntax_tokenizer_destroy(p); }
    } cleanup{tokenizer};
    check_lxb(lxb_css_syntax_tokenizer_init(tokenizer));
    tokenizer->with_comment = true;
    lxb_css_syntax_tokenizer_buffer_set(tokenizer, chars(value), value.size());
    std::vector<std::string> result;
    int depth = 0;
    while (auto* token = lxb_css_syntax_token(tokenizer)) {
        if (token->type == LXB_CSS_SYNTAX_TOKEN__EOF) break;
        if (token->type != LXB_CSS_SYNTAX_TOKEN_COMMENT) {
            const auto* base = lxb_css_syntax_token_base(token);
            const std::string bytes(reinterpret_cast<const char*>(base->begin), base->length);
            if (depth)
                result.back() += bytes;
            else if (token->type != LXB_CSS_SYNTAX_TOKEN_WHITESPACE)
                result.push_back(bytes);
            if (token->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || token->type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS)
                ++depth;
            if (token->type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS) --depth;
            if (depth < 0) return {};
        }
        lxb_css_syntax_token_consume(tokenizer);
    }
    check_lxb(tokenizer->status);
    return depth ? std::vector<std::string>{} : result;
}
struct Validator {
    lxb_css_memory_t* memory = nullptr;
    lxb_css_parser_t* parser = nullptr;
    Validator() {
        memory = lxb_css_memory_create();
        if (!memory) throw std::bad_alloc();
        try {
            check_lxb(lxb_css_memory_init(memory, 64));
            parser = lxb_css_parser_create();
            if (!parser) throw std::bad_alloc();
            check_lxb(lxb_css_parser_init(parser, nullptr));
            parser->memory = memory;
        } catch (...) {
            lxb_css_parser_destroy(parser, true);
            lxb_css_memory_destroy(memory, true);
            throw;
        }
    }
    ~Validator() {
        lxb_css_parser_destroy(parser, true);
        lxb_css_memory_destroy(memory, true);
    }
    const lxb_css_rule_declaration_t* parse(std::string_view name, std::string_view value, uintptr_t type) {
        lxb_css_memory_clean(memory);
        const auto source = std::string(name) + ':' + std::string(value);
        auto* list = lxb_css_declaration_list_parse(parser, chars(source), source.size());
        if (!list) {
            check_lxb(parser->status);
            return nullptr;
        }
        if (list->count != 1 || !list->first || list->first->type != LXB_CSS_RULE_DECLARATION) return nullptr;
        auto* declaration = lxb_css_rule_declaration(list->first);
        return declaration->type == type ? declaration : nullptr;
    }
};
bool global(std::string_view value) {
    return value == "inherit" || value == "initial" || value == "unset";
}
} // namespace
void expand_border_shorthands(lxb_css_rule_declaration_list_t* list, std::string_view authored) {
    if (!list) return;
    constexpr std::array<const char*, 4> sides{"top", "right", "bottom", "left"};
    constexpr std::array<const char*, 3> fields{"width", "style", "color"};
    for (auto* rule = list->first; rule;) {
        auto* next = rule->next;
        if (rule->type != LXB_CSS_RULE_DECLARATION) {
            rule = next;
            continue;
        }
        auto* declaration = lxb_css_rule_declaration(rule);
        std::string name;
        if (declaration->type == LXB_CSS_PROPERTY__CUSTOM)
            name = lower(
                {reinterpret_cast<const char*>(declaration->u.custom->name.data), declaration->u.custom->name.length});
        else {
            const auto* entry = lxb_css_property_by_id(declaration->type==LXB_CSS_PROPERTY__UNDEF?declaration->u.undef->type:declaration->type);
            if (entry) name.assign(reinterpret_cast<const char*>(entry->name), entry->length);
        }
        const bool whole = name == "border";
        int side = -1, field = -1;
        for (int i = 0; i < 4; ++i)
            if (name == std::string("border-") + sides[i]) side = i;
        for (int i = 0; i < 3; ++i)
            if (name == std::string("border-") + fields[i]) field = i;
        int long_side = -1, long_field = -1;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 2; ++j)
                if (name == std::string("border-") + sides[i] + '-' + fields[j]) {
                    long_side = i;
                    long_field = j;
                }
        if (!whole && side < 0 && field < 0 && long_side < 0) {
            rule = next;
            continue;
        }
        std::string value;
        if (declaration->type == LXB_CSS_PROPERTY__CUSTOM) {
            const auto& raw = declaration->u.custom->value;
            value.assign(reinterpret_cast<const char*>(raw.data), raw.length);
        } else
            check_lxb(lxb_css_rule_declaration_serialize(declaration, append, &value));
        // Known-property serialization includes its name; use authored values
        // whenever possible. Generated canonical longhands have zero offsets.
        if (declaration->type != LXB_CSS_PROPERTY__CUSTOM) {
            const auto colon = value.find(':');
            if (colon != std::string::npos) value.erase(0, colon + 1);
            const auto important = value.find("!important");
            if (important != std::string::npos) value.erase(important);
            if (!value.empty() && value.back() == ';') value.pop_back();
        }
        const auto& offset = declaration->offset;
        if (offset.value_begin < offset.value_end && offset.value_end <= authored.size())
            value = authored.substr(offset.value_begin, offset.value_end - offset.value_begin);
        auto parts = components(value);
        if (parts.size() == 1 && (lower(parts[0]) == "revert" || lower(parts[0]) == "revert-layer")) {
            rule = next;
            continue;
        }
        // Variable resolution is deferred; preserve the authored declaration.
        if (lower(value).find("var(") != std::string::npos) {
            rule = next;
            continue;
        }
        Validator validator;
        std::string source;
        const auto add = [&](int s, int f, const std::string& text) {
            source += std::string("border-") + sides[s] + '-' + fields[f] + ':' + text +
                      (declaration->important ? "!important;" : ";");
        };
        if (whole || side >= 0) {
            std::array<std::string,3> values{"medium","none","currentcolor"};std::array<bool,3> used{};
            bool valid=!parts.empty()&&parts.size()<=3;
            if(global(parts.size()==1?lower(parts[0]):""))values.fill(lower(parts[0]));
            else for(const auto& part:parts) {
                if(global(lower(part))){valid=false;break;}
                if(!used[2]&&csscolor::parse(part)){values[2]=part;used[2]=true;continue;}
                bool matched=false;
                for(int field=0;field<2;++field) {
                    if(used[field])continue;
                    const auto* parsed=validator.parse("border-top",field==0?part+" solid":"1px "+part,LXB_CSS_PROPERTY_BORDER_TOP);
                    if(!parsed)continue;
                    if(field==0&&(parsed->u.border->width.type==LXB_CSS_VALUE__UNDEF||(parsed->u.border->width.type==LXB_CSS_VALUE__LENGTH&&parsed->u.border->width.length.num<0)))continue;
                    if(field==1&&parsed->u.border->style==LXB_CSS_VALUE__UNDEF)continue;
                    values[field].clear();
                    if(field==0)check_lxb(lxb_css_value_length_type_sr(&parsed->u.border->width,append,&values[field]));
                    else check_lxb(lxb_css_value_serialize(parsed->u.border->style,append,&values[field]));
                    used[field]=true;matched=true;break;
                }
                if(!matched){valid=false;break;}
            }
            if(valid) {
                for (int s = 0; s < 4; ++s)
                    if (whole || side == s)
                        for (int f = 0; f < 3; ++f)
                            add(s, f, values[f]);
                // border also resets border-image, even before image borders land.
                if (whole)
                    source += std::string("border-image:") +
                              (global(parts.size() == 1 ? lower(parts[0]) : "") ? lower(parts[0]) : "initial") +
                              (declaration->important ? "!important;" : ";");
            }
        } else {
            const auto f = field >= 0 ? field : long_field;
            bool valid = !parts.empty() && parts.size() <= size_t(field >= 0 ? 4 : 1);
            std::vector<std::string> values;
            for (const auto& part : parts) {
                const auto normalized = lower(part);
                if (global(normalized)) {
                    valid = valid && parts.size() == 1;
                    values.push_back(normalized);
                    continue;
                }
                const lxb_css_rule_declaration_t* parsed = nullptr;
                if (f == 2)
                    {if(!csscolor::parse(part)){valid=false;break;}values.push_back(part);continue;}
                else
                    parsed = validator.parse("border-top", f == 0 ? part + " solid" : "1px " + part,
                                             LXB_CSS_PROPERTY_BORDER_TOP);
                if (!parsed ||
                    (f == 0 && (parsed->u.border->width.type == LXB_CSS_VALUE__UNDEF ||
                                (parsed->u.border->width.type == LXB_CSS_VALUE__LENGTH &&
                                 parsed->u.border->width.length.num < 0))) ||
                    (f == 1 && parsed->u.border->style == LXB_CSS_VALUE__UNDEF)) {
                    valid = false;
                    break;
                }
                std::string canonical;
                if (f == 0)
                    check_lxb(lxb_css_value_length_type_sr(&parsed->u.border->width, append, &canonical));
                else if (f == 1)
                    check_lxb(lxb_css_value_serialize(parsed->u.border->style, append, &canonical));
                else
                    check_lxb(lxb_css_value_color_serialize(parsed->u.border_top_color, append, &canonical));
                values.push_back(std::move(canonical));
            }
            if (valid)
                for (int s = 0; s < 4; ++s)
                    if (field >= 0 || s == long_side) {
                        const auto index = values.size() == 1 ? 0
                                           : s == 3           ? (values.size() == 4 ? 3 : 1)
                                           : s == 2           ? (values.size() >= 3 ? 2 : 0)
                                                              : s;
                        add(s, f, values[index]);
                    }
        }
        // Canonical width/style longhands are custom properties to Lexbor.
        // Replacing them with identical nodes on every refresh is unnecessary.
        if (long_side >= 0 && source == name + ':' + value + (declaration->important ? "!important;" : ";")) {
            rule = next;
            continue;
        }
        auto* parser = lxb_css_parser_create();
        if (!parser) throw std::bad_alloc();
        struct Cleanup {
            lxb_css_parser_t* p;
            ~Cleanup() { lxb_css_parser_destroy(p, true); }
        } cleanup{parser};
        check_lxb(lxb_css_parser_init(parser, nullptr));
        parser->memory = rule->memory;
        auto* replacement = lxb_css_declaration_list_parse(parser, chars(source), source.size());
        if (!replacement) {
            check_lxb(parser->status);
            throw std::bad_alloc();
        }
        auto* before = rule->prev;
        for (auto* item = replacement->first; item;) {
            auto* after = item->next;
            if (item->type == LXB_CSS_RULE_DECLARATION) lxb_css_rule_declaration(item)->offset = {};
            item->parent = &list->rule;
            item->prev = before;
            if (before)
                before->next = item;
            else
                list->first = item;
            before = item;
            item = after;
        }
        if (before)
            before->next = next;
        else
            list->first = next;
        if (next)
            next->prev = before;
        else
            list->last = before;
        list->count = list->count - 1 + replacement->count;
        replacement->first = replacement->last = nullptr;
        lxb_css_rule_declaration_list_destroy(replacement, true);
        rule->prev = rule->next = rule->parent = nullptr;
        lxb_css_rule_destroy(rule, true);
        rule = next;
    }
}
void expand_outline_shorthands(lxb_css_rule_declaration_list_t* list, std::string_view authored) {
    if (!list) return;
    constexpr const char* fields[]{"outline-width", "outline-style", "outline-color", "outline-offset"};
    for (auto* rule = list->first; rule;) {
        auto* next = rule->next;
        if (rule->type != LXB_CSS_RULE_DECLARATION) { rule = next; continue; }
        auto* declaration = lxb_css_rule_declaration(rule);
        if (declaration->type != LXB_CSS_PROPERTY__CUSTOM) { rule = next; continue; }
        const auto* custom = declaration->u.custom;
        const auto name = lower({reinterpret_cast<const char*>(custom->name.data), custom->name.length});
        int field = -1;
        for (int i = 0; i < 4; ++i) if (name == fields[i]) field = i;
        if (name != "outline" && field < 0) { rule = next; continue; }
        std::string value(reinterpret_cast<const char*>(custom->value.data), custom->value.length);
        const auto& offset = declaration->offset;
        if (offset.value_begin < offset.value_end && offset.value_end <= authored.size())
            value = authored.substr(offset.value_begin, offset.value_end - offset.value_begin);
        const auto parts = components(value);
        if (lower(value).find("var(") != std::string::npos ||
            (parts.size() == 1 && (lower(parts[0]) == "revert" || lower(parts[0]) == "revert-layer"))) {
            rule = next; continue;
        }
        Validator validator;
        const auto normalize = [&](int index, const std::string& part, std::string& output) {
            const auto word = lower(part);
            if (global(word)) { output = word; return true; }
            if (index == 1 && word == "auto") { output = word; return true; }
            if (index == 1 && word == "hidden") return false;
            if (index == 2) {
                if(!csscolor::parse(part))return false;
                output=part;
            } else if (index == 3) {
                const auto* parsed = validator.parse("margin-top", part, LXB_CSS_PROPERTY_MARGIN_TOP);
                if (!parsed) return false;
                const auto* length = static_cast<const lxb_css_value_length_percentage_t*>(parsed->u.user);
                if (length->type != LXB_CSS_VALUE__LENGTH) return false;
                check_lxb(lxb_css_value_length_percentage_sr(length, append, &output));
            } else {
                const auto* parsed = validator.parse("border-top", index == 0 ? part + " solid" : "1px " + part,
                                                     LXB_CSS_PROPERTY_BORDER_TOP);
                if (!parsed) return false;
                if (index == 0) {
                    const auto& width = parsed->u.border->width;
                    if (width.type == LXB_CSS_VALUE__UNDEF || (width.type == LXB_CSS_VALUE__LENGTH && width.length.num < 0)) return false;
                    check_lxb(lxb_css_value_length_type_sr(&width, append, &output));
                } else {
                    if (parsed->u.border->style == LXB_CSS_VALUE__UNDEF) return false;
                    check_lxb(lxb_css_value_serialize(parsed->u.border->style, append, &output));
                }
            }
            return true;
        };
        bool valid = !parts.empty();
        std::array<std::string, 4> values{"medium", "none", "currentcolor", "0px"};
        if (field >= 0) valid = parts.size() == 1 && normalize(field, parts[0], values[field] = "");
        else if (parts.size() == 1 && global(lower(parts[0]))) {
            for (int i = 0; i < 3; ++i) values[i] = lower(parts[0]);
        } else {
            std::array<bool, 3> used{};
            for (const auto& part : parts) {
                bool matched = false;
                if (global(lower(part))) { valid = false; break; }
                for (int i = 0; i < 3; ++i) {
                    std::string normalized;
                    if (used[i] || !normalize(i, part, normalized)) continue;
                    values[i] = std::move(normalized); used[i] = true; matched = true; break;
                }
                if (!matched) { valid = false; break; }
            }
        }
        std::string source;
        if (valid)
            for (int i = 0; i < 4; ++i)
                if (field == i || (field < 0 && i < 3))
                    source += std::string(fields[i]) + ':' + values[i] + (declaration->important ? "!important;" : ";");
        const std::string canonical = name + ':' + value + (declaration->important ? "!important;" : ";");
        if (valid && field >= 0 && source == canonical) { rule = next; continue; }
        auto* parser = lxb_css_parser_create();
        if (!parser) throw std::bad_alloc();
        struct Cleanup { lxb_css_parser_t* p; ~Cleanup() { lxb_css_parser_destroy(p, true); } } cleanup{parser};
        check_lxb(lxb_css_parser_init(parser, nullptr)); parser->memory = rule->memory;
        auto* replacement = lxb_css_declaration_list_parse(parser, chars(source), source.size());
        if (!replacement) { check_lxb(parser->status); throw std::bad_alloc(); }
        auto* before = rule->prev;
        for (auto* item = replacement->first; item;) {
            auto* after = item->next;
            if (item->type == LXB_CSS_RULE_DECLARATION) lxb_css_rule_declaration(item)->offset = {};
            item->parent = &list->rule; item->prev = before;
            if (before) before->next = item; else list->first = item;
            before = item; item = after;
        }
        if (before) before->next = next; else list->first = next;
        if (next) next->prev = before; else list->last = before;
        list->count = list->count - 1 + replacement->count;
        replacement->first = replacement->last = nullptr;
        lxb_css_rule_declaration_list_destroy(replacement, true);
        rule->prev = rule->next = rule->parent = nullptr;
        lxb_css_rule_destroy(rule, true); rule = next;
    }
}
} // namespace xl::detail
