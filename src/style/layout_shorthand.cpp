#include "layout_shorthand.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <lexbor/css/unit.h>

namespace xl::detail {
namespace {
lxb_status_t append(const lxb_char_t *data, size_t size, void *ctx) {
    try {
        static_cast<std::string *>(ctx)->append(reinterpret_cast<const char *>(data), size);
        return LXB_STATUS_OK;
    } catch (...) {
        return LXB_STATUS_ERROR_MEMORY_ALLOCATION;
    }
}
std::vector<std::string> gap_tokens(std::string_view value) {
    auto *parser = lxb_css_syntax_tokenizer_create();
    if (!parser)
        throw std::bad_alloc();
    struct Cleanup {
        lxb_css_syntax_tokenizer_t *p;
        ~Cleanup() { lxb_css_syntax_tokenizer_destroy(p); }
    } cleanup{parser};
    check_lxb(lxb_css_syntax_tokenizer_init(parser));
    parser->with_comment = true;
    lxb_css_syntax_tokenizer_buffer_set(parser, chars(value), value.size());
    std::vector<std::string> result;
    while (auto *token = lxb_css_syntax_token(parser)) {
        if (token->type == LXB_CSS_SYNTAX_TOKEN__EOF)
            break;
        if (token->type != LXB_CSS_SYNTAX_TOKEN_WHITESPACE && token->type != LXB_CSS_SYNTAX_TOKEN_COMMENT) {
            if (token->type != LXB_CSS_SYNTAX_TOKEN_IDENT && token->type != LXB_CSS_SYNTAX_TOKEN_DIMENSION &&
                token->type != LXB_CSS_SYNTAX_TOKEN_PERCENTAGE && token->type != LXB_CSS_SYNTAX_TOKEN_NUMBER)
                return {};
            std::string text;
            if (token->type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
                const auto *ident = lxb_css_syntax_token_ident(token);
                text.assign(reinterpret_cast<const char *>(ident->data), ident->length);
            } else {
                const auto *base = lxb_css_syntax_token_base(token);
                text.assign(reinterpret_cast<const char *>(base->begin), base->length);
            }
            for (auto &c : text)
                if (c >= 'A' && c <= 'Z')
                    c = char(c - 'A' + 'a');
            result.push_back(std::move(text));
        }
        lxb_css_syntax_token_consume(parser);
    }
    check_lxb(parser->status);
    return result;
}
bool valid_gap(std::string value) {
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    if (value == "normal" || value == "inherit" || value == "initial" || value == "unset")
        return true;
    double number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc{} || !std::isfinite(number) || number < 0)
        return false;
    const std::string_view suffix(parsed.ptr, size_t(value.data() + value.size() - parsed.ptr));
    return suffix == "%" || (suffix.empty() && number == 0) ||
           lxb_css_unit_absolute_relative_by_name(chars(suffix), suffix.size());
}
} // namespace
void expand_layout_shorthands(lxb_css_rule_declaration_list_t *list) {
    if (!list)
        return;
    for (auto *rule = list->first; rule;) {
        auto *next = rule->next;
        if (rule->type != LXB_CSS_RULE_DECLARATION) {
            rule = next;
            continue;
        }
        auto *declaration = lxb_css_rule_declaration(rule);
        if (declaration->type == LXB_CSS_PROPERTY__UNDEF) {
            const auto &invalid = *declaration->u.undef;
            const bool justify = invalid.type == LXB_CSS_PROPERTY_JUSTIFY_CONTENT;
            const bool content = invalid.type == LXB_CSS_PROPERTY_ALIGN_CONTENT;
            const bool alignment = justify || content || invalid.type == LXB_CSS_PROPERTY_ALIGN_ITEMS ||
                                   invalid.type == LXB_CSS_PROPERTY_ALIGN_SELF;
            const bool dimension = invalid.type == LXB_CSS_PROPERTY_WIDTH || invalid.type == LXB_CSS_PROPERTY_HEIGHT ||
                invalid.type == LXB_CSS_PROPERTY_MIN_WIDTH || invalid.type == LXB_CSS_PROPERTY_MAX_WIDTH ||
                invalid.type == LXB_CSS_PROPERTY_MIN_HEIGHT || invalid.type == LXB_CSS_PROPERTY_MAX_HEIGHT;
            if (dimension) {
                const auto words = gap_tokens({reinterpret_cast<const char*>(invalid.value.data), invalid.value.length});
                if (words.size() == 1 && (words[0] == "fit-content" || words[0] == "min-content" || words[0] == "max-content")) {
                    const auto property = invalid.type;
                    auto* replacement = static_cast<lxb_css_property_width_t*>(lxb_css_property_width_create(rule->memory));
                    if (!replacement) throw std::bad_alloc();
                    replacement->type = words[0] == "fit-content" ? css_fit_content : words[0] == "min-content" ? LXB_CSS_VALUE_MIN_CONTENT : LXB_CSS_VALUE_MAX_CONTENT;
                    lxb_css_property__undef_destroy(rule->memory, declaration->u.undef, true);
                    declaration->type = property; declaration->u.user = replacement;
                }
            }
            else if (alignment) {
                const auto words =
                    gap_tokens({reinterpret_cast<const char *>(invalid.value.data), invalid.value.length});
                if (words.size() == 1) {
                    auto value = words[0];
                    for (auto &c : value)
                        if (c >= 'A' && c <= 'Z')
                            c = char(c - 'A' + 'a');
                    lxb_css_value_type_t type =
                        value == "start"    ? LXB_CSS_VALUE_START
                        : value == "end"    ? LXB_CSS_VALUE_END
                        : value == "normal" ? (justify ? LXB_CSS_VALUE_FLEX_START : LXB_CSS_VALUE_STRETCH)
                        : value == "space-evenly" && (justify || content) ? css_space_evenly
                                                                          : 0;
                    if (type) {
                        const auto property = invalid.type;
                        auto *replacement = static_cast<lxb_css_property_align_items_t *>(
                            lxb_css_property_align_items_create(rule->memory));
                        if (!replacement)
                            throw std::bad_alloc();
                        replacement->type = type;
                        lxb_css_property__undef_destroy(rule->memory, declaration->u.undef, true);
                        declaration->type = property;
                        declaration->u.user = replacement;
                    }
                }
            }
        }
        std::string source;
        bool replace = false;
        const auto add = [&](const char *name, const std::string &value) {
            source += std::string(name) + ':' + value + (declaration->important ? "!important;" : ";");
        };
        const auto keyword = [&](lxb_css_value_type_t type) {
            std::string text;
            check_lxb(lxb_css_value_serialize(type, append, &text));
            return text;
        };
        if (declaration->type == LXB_CSS_PROPERTY_FLEX) {
            replace = true;
            const auto &flex = *declaration->u.flex;
            if (flex.type == LXB_CSS_VALUE_REVERT) {
                rule = next;
                continue;
            }
            std::array<std::string, 3> values{"1", "1", "0%"};
            if (flex.type == LXB_CSS_VALUE_INHERIT || flex.type == LXB_CSS_VALUE_INITIAL ||
                flex.type == LXB_CSS_VALUE_UNSET)
                values.fill(keyword(flex.type));
            else if (flex.type == LXB_CSS_VALUE_NONE)
                values = {"0", "0", "auto"};
            else {
                if (flex.grow.type != LXB_CSS_VALUE__UNDEF) {
                    values[0].clear();
                    check_lxb(lxb_css_value_number_type_sr(&flex.grow, append, &values[0]));
                }
                if (flex.shrink.type != LXB_CSS_VALUE__UNDEF) {
                    values[1].clear();
                    check_lxb(lxb_css_value_number_type_sr(&flex.shrink, append, &values[1]));
                }
                if (flex.basis.type != LXB_CSS_VALUE__UNDEF) {
                    values[2].clear();
                    check_lxb(lxb_css_value_length_percentage_sr(&flex.basis, append, &values[2]));
                }
            }
            const bool valid = flex.grow.number.num >= 0 && flex.shrink.number.num >= 0;
            if (valid) {
                add("flex-grow", values[0]);
                add("flex-shrink", values[1]);
                add("flex-basis", values[2]);
            }
        } else if (declaration->type == LXB_CSS_PROPERTY_FLEX_FLOW) {
            replace = true;
            const auto &flow = *declaration->u.flex_flow;
            if (flow.type_direction == LXB_CSS_VALUE_REVERT) {
                rule = next;
                continue;
            }
            const auto direction = flow.type_direction ? flow.type_direction : LXB_CSS_VALUE_ROW;
            const auto wrap = flow.type_direction == LXB_CSS_VALUE_INHERIT ||
                                      flow.type_direction == LXB_CSS_VALUE_INITIAL ||
                                      flow.type_direction == LXB_CSS_VALUE_UNSET
                                  ? flow.type_direction
                              : flow.wrap ? flow.wrap
                                          : LXB_CSS_VALUE_NOWRAP;
            add("flex-direction", keyword(direction));
            add("flex-wrap", keyword(wrap));
        } else if (declaration->type == LXB_CSS_PROPERTY__CUSTOM) {
            const auto &custom = *declaration->u.custom;
            const std::string name(reinterpret_cast<const char *>(custom.name.data), custom.name.length);
            if (name == "overflow") {
                const auto tokens = gap_tokens({reinterpret_cast<const char*>(custom.value.data), custom.value.length});
                const auto global = [](const auto& value) { return value == "inherit" || value == "initial" || value == "unset"; };
                const auto valid = [&](const auto& value) {
                    return value == "visible" || value == "hidden" || value == "scroll" || value == "auto" || value == "clip" || global(value);
                };
                if (!tokens.empty() && tokens.size() <= 2 && std::all_of(tokens.begin(), tokens.end(), valid) &&
                    (tokens.size() == 1 || (!global(tokens.front()) && !global(tokens.back())))) {
                    replace = true; add("overflow-x", tokens.front()); add("overflow-y", tokens.back());
                }
            }
            else if (name == "gap" || name == "row-gap" || name == "column-gap") {
                const auto tokens =
                    gap_tokens({reinterpret_cast<const char *>(custom.value.data), custom.value.length});
                const bool valid =
                    !tokens.empty() && tokens.size() <= (name == "gap" ? 2u : 1u) &&
                    std::all_of(tokens.begin(), tokens.end(), valid_gap) &&
                    (tokens.size() == 1 ||
                     ((tokens[0] != "inherit" && tokens[0] != "initial" && tokens[0] != "unset") &&
                      (tokens[1] != "inherit" && tokens[1] != "initial" && tokens[1] != "unset")));
                if (valid && name == "gap") {
                    replace = true;
                    add("row-gap", tokens[0]);
                    add("column-gap", tokens.back());
                } else if (valid && name != "gap") {
                    const std::string original(reinterpret_cast<const char *>(custom.value.data),
                                               custom.value.length);
                    if (original != tokens[0]) {
                        replace = true;
                        add(name.c_str(), tokens[0]);
                    }
                } else if (!valid && !tokens.empty() && tokens[0] != "revert" && tokens[0] != "revert-layer")
                    replace = true; // Invalid values cannot mask a previous declaration.
            }
        }
        if (!replace) {
            rule = next;
            continue;
        }
        auto *parser = lxb_css_parser_create();
        if (!parser)
            throw std::bad_alloc();
        struct Cleanup {
            lxb_css_parser_t *p;
            ~Cleanup() { lxb_css_parser_destroy(p, true); }
        } cleanup{parser};
        check_lxb(lxb_css_parser_init(parser, nullptr));
        parser->memory = rule->memory;
        auto *replacement = lxb_css_declaration_list_parse(parser, chars(source), source.size());
        if (!replacement) {
            check_lxb(parser->status);
            throw std::bad_alloc();
        }
        auto *before = rule->prev;
        for (auto *item = replacement->first; item;) {
            auto *after = item->next;
            if (item->type == LXB_CSS_RULE_DECLARATION)
                lxb_css_rule_declaration(item)->offset = {};
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
} // namespace xl::detail
