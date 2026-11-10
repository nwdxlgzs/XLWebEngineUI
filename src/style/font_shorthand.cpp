#include "font_shorthand.h"
#include <array>
#include <algorithm>
#include <optional>
#include <charconv>
#include <cmath>

namespace xl::detail {
namespace {
std::string lower(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return text;
}
struct Token { lxb_css_syntax_token_type_t type; std::string text, ident, name; };
std::vector<Token> tokenize(std::string_view value) {
    auto* tokenizer = lxb_css_syntax_tokenizer_create();
    if (!tokenizer) throw std::bad_alloc();
    struct Cleanup { lxb_css_syntax_tokenizer_t* value; ~Cleanup() { lxb_css_syntax_tokenizer_destroy(value); } } cleanup{tokenizer};
    check_lxb(lxb_css_syntax_tokenizer_init(tokenizer));
    tokenizer->with_comment = true;
    lxb_css_syntax_tokenizer_buffer_set(tokenizer, chars(value), value.size());
    std::vector<Token> result;
    while (true) {
        auto* token = lxb_css_syntax_token(tokenizer);
        if (!token) { check_lxb(tokenizer->status); throw DocumentFailure{XL_INTERNAL_ERROR}; }
        if (token->type == LXB_CSS_SYNTAX_TOKEN__EOF) break;
        if (token->type != LXB_CSS_SYNTAX_TOKEN_WHITESPACE && token->type != LXB_CSS_SYNTAX_TOKEN_COMMENT) {
            Token next{token->type, {}, {}, {}};
            // Preserve original escapes: serializing a decoded identifier can
            // turn an escaped comma or space into new CSS syntax on reparse.
            const auto* base = lxb_css_syntax_token_base(token);
            next.text.assign(reinterpret_cast<const char*>(base->begin), base->length);
            if (token->type == LXB_CSS_SYNTAX_TOKEN_IDENT || token->type == LXB_CSS_SYNTAX_TOKEN_STRING) {
                const auto* text = lxb_css_syntax_token_ident(token);
                next.name.assign(reinterpret_cast<const char*>(text->data), text->length);
                if (token->type == LXB_CSS_SYNTAX_TOKEN_IDENT) next.ident = lower(next.name);
            } else if (token->type == LXB_CSS_SYNTAX_TOKEN_DIMENSION) {
                const auto* unit = lxb_css_syntax_token_dimension_string(token);
                next.ident = lower({reinterpret_cast<const char*>(unit->data), unit->length});
            }
            result.push_back(std::move(next));
        }
        lxb_css_syntax_token_consume(tokenizer);
    }
    check_lxb(tokenizer->status);
    return result;
}
struct Parser {
    lxb_css_memory_t* memory = nullptr;
    lxb_css_parser_t* parser = nullptr;
    Parser() {
        memory = lxb_css_memory_create();
        if (!memory) throw std::bad_alloc();
        try {
            check_lxb(lxb_css_memory_init(memory, 64));
            parser = lxb_css_parser_create();
            if (!parser) throw std::bad_alloc();
            check_lxb(lxb_css_parser_init(parser, nullptr));
            parser->memory = memory;
        } catch (...) { lxb_css_parser_destroy(parser, true); lxb_css_memory_destroy(memory, true); throw; }
    }
    ~Parser() { lxb_css_parser_destroy(parser, true); lxb_css_memory_destroy(memory, true); }
    bool accepts(uintptr_t type, std::string_view property, std::string_view value) {
        lxb_css_memory_clean(memory);
        const auto input = std::string(property) + ':' + std::string(value);
        auto* list = lxb_css_declaration_list_parse(parser, chars(input), input.size());
        if (!list) { check_lxb(parser->status); return false; }
        return list->count == 1 && list->first && list->first->type == LXB_CSS_RULE_DECLARATION
            && lxb_css_rule_declaration(list->first)->type == type;
    }
};
constexpr std::array<const char*, 7> names{"font-style", "font-weight", "font-stretch", "font-size", "line-height", "font-family", "font-variant"};
// Reset-only members are represented even before their rendering support lands,
// so adding that support cannot inherit values across an earlier font reset.
constexpr std::array<const char*, 13> resets{"font-size-adjust", "font-kerning", "font-optical-sizing", "font-feature-settings",
    "font-variation-settings", "font-language-override", "font-variant-ligatures", "font-variant-caps", "font-variant-numeric",
    "font-variant-east-asian", "font-variant-position", "font-variant-alternates", "font-variant-emoji"};
struct Expanded { std::array<std::string, 7> values{"normal", "normal", "normal", "medium", "normal", "serif", "normal"}; };
bool global(std::string_view id) {
    return id == "inherit" || id == "initial" || id == "unset" || id == "revert" || id == "revert-layer" || id == "default";
}
bool generic(std::string_view name) {
    for (const auto* word : {"serif", "sans-serif", "monospace", "cursive", "fantasy", "system-ui", "ui-serif", "ui-sans-serif",
                             "ui-monospace", "ui-rounded", "emoji", "math", "fangsong"}) if (name == word) return true;
    return false;
}
std::string css_quote(std::string_view name) {
    std::string result = "\"";
    for (const auto c : name) {
        if (c == '\\' || c == '"') result += '\\';
        if (c == '\n') result += "\\a ";
        else if (c == '\r') result += "\\d ";
        else if (c == '\f') result += "\\c ";
        else result += c;
    }
    return result + '"';
}
std::optional<std::string> family_value(const std::vector<Token>& tokens, size_t index) {
    if (index >= tokens.size()) return {};
    std::string result;
    while (index < tokens.size()) {
        const auto& token = tokens[index++];
        if (token.type != LXB_CSS_SYNTAX_TOKEN_IDENT && token.type != LXB_CSS_SYNTAX_TOKEN_STRING) return {};
        if (global(token.ident)) return {};
        auto name = token.name;
        const auto is_generic = generic(token.ident);
        if (token.type == LXB_CSS_SYNTAX_TOKEN_IDENT && !is_generic) {
            while (index < tokens.size() && tokens[index].type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
                if (global(tokens[index].ident) || generic(tokens[index].ident)) return {};
                name += ' ' + tokens[index++].name;
            }
        }
        if (!result.empty()) result += ',';
        result += is_generic ? token.ident : css_quote(name);
        if (index == tokens.size()) return result;
        if (tokens[index++].type != LXB_CSS_SYNTAX_TOKEN_COMMA || index == tokens.size()) return {};
    }
    return {};
}
std::optional<Expanded> parse_font(std::string_view value) {
    const auto tokens = tokenize(value);
    if (tokens.empty()) return {};
    Expanded result;
    if (tokens.size() == 1 && (tokens[0].ident == "inherit" || tokens[0].ident == "initial" || tokens[0].ident == "unset")) {
        result.values.fill(tokens[0].ident); return result;
    }
    Parser validator;
    bool slant = false, weight = false, stretch = false, variant = false;
    size_t index = 0, normal = 0;
    for (; index < tokens.size(); ++index) {
        const auto& token = tokens[index];
        const auto& id = token.ident;
        if (id == "normal") { ++normal; continue; }
        if ((id == "italic" || id == "oblique") && !slant) {
            result.values[0] = token.text; slant = true;
            if (id == "oblique" && index + 1 < tokens.size()
                    && (tokens[index+1].ident == "deg" || tokens[index+1].ident == "grad" || tokens[index+1].ident == "rad" || tokens[index+1].ident == "turn")
                    && validator.accepts(LXB_CSS_PROPERTY_FONT_STYLE, "font-style", token.text + ' ' + tokens[index+1].text))
                result.values[0] += ' ' + tokens[++index].text;
        } else if (id == "small-caps" && !variant) { result.values[6] = "small-caps"; variant = true; }
        else if (!weight && (id == "bold" || id == "bolder" || id == "lighter" || token.type == LXB_CSS_SYNTAX_TOKEN_NUMBER)
                 && validator.accepts(LXB_CSS_PROPERTY_FONT_WEIGHT, "font-weight", token.text)) {
            result.values[1] = token.text; weight = true;
        } else {
            constexpr std::array<const char*, 9> widths{"ultra-condensed", "extra-condensed", "condensed", "semi-condensed", "normal", "semi-expanded", "expanded", "extra-expanded", "ultra-expanded"};
            constexpr std::array<const char*, 9> percentages{"50%", "62.5%", "75%", "87.5%", "100%", "112.5%", "125%", "150%", "200%"};
            const auto width = std::find(widths.begin(), widths.end(), id);
            if (width != widths.end() && !stretch) { result.values[2] = percentages[static_cast<size_t>(width-widths.begin())]; stretch = true; }
            else if (validator.accepts(LXB_CSS_PROPERTY_FONT_SIZE, "font-size", token.text)
                    && id != "inherit" && id != "initial" && id != "unset" && id != "revert" && id != "revert-layer") break;
            else return {};
        }
    }
    if (index >= tokens.size() || normal + size_t(slant) + size_t(weight) + size_t(stretch) + size_t(variant) > 4) return {};
    result.values[3] = tokens[index++].text;
    if (index < tokens.size() && tokens[index].text == "/") {
        if (++index >= tokens.size() || !validator.accepts(LXB_CSS_PROPERTY_LINE_HEIGHT, "line-height", tokens[index].text)
                || tokens[index].ident == "inherit" || tokens[index].ident == "initial" || tokens[index].ident == "unset") return {};
        result.values[4] = tokens[index++].text;
    }
    auto family = family_value(tokens, index);
    if (!family || !validator.accepts(LXB_CSS_PROPERTY_FONT_FAMILY, "font-family", *family)) return {};
    result.values[5] = std::move(*family);
    return result;
}
}
std::optional<TextFormat> canvas_font(std::string_view source) {
    const auto font=parse_font(source);if(!font)return {};
    const auto size=cssbackground::parse_length(font->values[3]);if(!size)return {};
    TextFormat result;result.size=float(cssbackground::resolve(*size,10,{10,16,640,480}));
    if(!std::isfinite(result.size)||result.size<=0)return {};
    result.italic=font->values[0]=="italic";result.oblique=font->values[0].starts_with("oblique");
    if(font->values[1]=="bold"||font->values[1]=="bolder")result.weight=700;
    else if(font->values[1]!="normal"&&font->values[1]!="lighter") {int weight=400;const auto& value=font->values[1];std::from_chars(value.data(),value.data()+value.size(),weight);result.weight=weight;}
    const auto tokens=tokenize(font->values[5]);std::string family;
    for(const auto& t:tokens){if(t.type==LXB_CSS_SYNTAX_TOKEN_COMMA){if(!family.empty())result.families.push_back(std::move(family));family.clear();}else {if(!family.empty())family+=' ';family+=t.name;}}
    if(!family.empty())result.families.push_back(std::move(family));
    result.whitespace=WhiteSpace::pre;return result;
}
void expand_font_shorthands(lxb_css_rule_declaration_list_t* list, std::string_view authored) {
    if (!list) return;
    for (auto* rule = list->first; rule;) {
        auto* next = rule->next;
        if (rule->type != LXB_CSS_RULE_DECLARATION) { rule = next; continue; }
        auto* declaration = lxb_css_rule_declaration(rule);
        const auto font = declaration->type == LXB_CSS_PROPERTY__CUSTOM
            && lower({reinterpret_cast<const char*>(declaration->u.custom->name.data), declaration->u.custom->name.length}) == "font";
        const auto family = declaration->type == LXB_CSS_PROPERTY__UNDEF && declaration->u.undef->type == LXB_CSS_PROPERTY_FONT_FAMILY;
        if (!font && !family) { rule = next; continue; }
        const auto& stored = font ? declaration->u.custom->value : declaration->u.undef->value;
        std::string_view value{reinterpret_cast<const char*>(stored.data), stored.length};
        // Lexbor's unknown-property serializer decodes identifier escapes.
        // Recover authored tokens from its source offsets before interpreting
        // the font grammar, while the original attribute/sheet remains intact.
        const auto& offset = declaration->offset;
        if (offset.value_begin < offset.value_end && offset.value_end <= authored.size())
            value = authored.substr(offset.value_begin, offset.value_end-offset.value_begin);
        const auto tokens = tokenize(value);
        const auto system = tokens.size() == 1 && (tokens[0].ident == "caption" || tokens[0].ident == "icon"
            || tokens[0].ident == "menu" || tokens[0].ident == "message-box" || tokens[0].ident == "small-caption"
            || tokens[0].ident == "status-bar" || tokens[0].ident == "revert" || tokens[0].ident == "revert-layer");
        // Keep recognized unsupported features visible to style diagnostics;
        // they are not silently counted as invalid declarations or implemented.
        if (system || std::any_of(tokens.begin(), tokens.end(), [](const auto& t) { return t.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION; })) {
            rule = next; continue;
        }
        auto expanded = font ? parse_font(value) : std::optional<Expanded>{};
        std::string source;
        if (family) {
            const auto canonical = family_value(tokens, 0);
            if (canonical) source = "font-family:" + *canonical + (declaration->important ? "!important;" : ";");
        }
        if (expanded) {
            for (size_t i = 0; i < names.size(); ++i) source += std::string(names[i]) + ':' + expanded->values[i] + (declaration->important ? "!important;" : ";");
            const auto global = expanded->values[3] == "inherit" || expanded->values[3] == "initial" || expanded->values[3] == "unset";
            for (const auto* name : resets) source += std::string(name) + ':' + (global ? expanded->values[3] : "initial") + (declaration->important ? "!important;" : ";");
        }
        // Parse generated longhands into the same allocator; no raw CSS input
        // is reinterpreted as HTML and no regex splits quoted or escaped names.
        auto* parser = lxb_css_parser_create();
        if (!parser) throw std::bad_alloc();
        struct Cleanup { lxb_css_parser_t* value; ~Cleanup() { lxb_css_parser_destroy(value, true); } } cleanup{parser};
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
        lxb_css_rule_destroy(rule, true);
        rule = next;
    }
}
}
