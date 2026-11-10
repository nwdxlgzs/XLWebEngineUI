#include "radius_style.h"
#include "generated_content.h"
#include <charconv>
#include <cmath>

namespace xl::detail {
namespace {
struct Context {
    double em = 16, rem = 16, vw = 100, vh = 100;
};
struct Math {
    double length = 0, percent = 0, number = 0;
    bool dimension = false, calculated = false;
};
using Tokens = generated::Tokens;
bool wide(std::string_view key) {
    return key == "inherit" || key == "initial" || key == "unset" || key == "revert" || key == "revert-layer";
}
std::optional<Math> scalar(const generated::Token &token, const Context &context) {
    if (token.type != LXB_CSS_SYNTAX_TOKEN_DIMENSION && token.type != LXB_CSS_SYNTAX_TOKEN_PERCENTAGE &&
        token.type != LXB_CSS_SYNTAX_TOKEN_NUMBER)
        return {};
    const auto raw = generated::lower(token.text);
    auto input = std::string_view(raw);
    if (input.starts_with('+'))
        input.remove_prefix(1);
    double value;
    const auto parsed = std::from_chars(input.data(), input.data() + input.size(), value);
    if (parsed.ec != std::errc{} || !std::isfinite(value))
        return {};
    const auto unit = std::string_view(parsed.ptr, size_t(input.data() + input.size() - parsed.ptr));
    if (token.type == LXB_CSS_SYNTAX_TOKEN_NUMBER)
        return Math{0, 0, value, false, false};
    if (unit == "%")
        return Math{0, value / 100, 0, true, false};
    double factor;
    if (unit == "px")
        factor = 1;
    else if (unit == "em")
        factor = context.em;
    else if (unit == "rem")
        factor = context.rem;
    else if (unit == "in")
        factor = 96;
    else if (unit == "cm")
        factor = 96 / 2.54;
    else if (unit == "mm")
        factor = 96 / 25.4;
    else if (unit == "q")
        factor = 96 / 101.6;
    else if (unit == "pt")
        factor = 96 / 72.;
    else if (unit == "pc")
        factor = 16;
    else if (unit == "vw" || unit == "svw" || unit == "lvw" || unit == "dvw")
        factor = context.vw / 100;
    else if (unit == "vh" || unit == "svh" || unit == "lvh" || unit == "dvh")
        factor = context.vh / 100;
    else if (unit == "vmin")
        factor = std::min(context.vw, context.vh) / 100;
    else if (unit == "vmax")
        factor = std::max(context.vw, context.vh) / 100;
    else
        return {};
    return Math{value * factor, 0, 0, true, false};
}
class Parser {
    const Tokens &tokens;
    Context context;
    std::optional<Math> atom(size_t &index, size_t end, unsigned depth) const {
        if (index == end || depth > 32)
            return {};
        const auto &token = tokens[index];
        if (token.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || token.type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS) {
            if (token.end >= end ||
                (token.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && generated::lower(token.text) != "calc"))
                return {};
            const auto close = token.end;
            ++index;
            auto value = sum(index, close, depth + 1);
            if (!value || index != close)
                return {};
            index = close + 1;
            value->calculated = true;
            return value;
        }
        ++index;
        return scalar(token, context);
    }
    std::optional<Math> product(size_t &index, size_t end, unsigned depth) const {
        auto value = atom(index, end, depth);
        if (!value)
            return {};
        while (index < end && tokens[index].type == LXB_CSS_SYNTAX_TOKEN_DELIM &&
               (tokens[index].text == "*" || tokens[index].text == "/")) {
            const auto divide = tokens[index++].text == "/";
            auto right = atom(index, end, depth);
            if (!right)
                return {};
            if (divide) {
                if (right->dimension || right->number == 0)
                    return {};
                const auto scale = 1 / right->number;
                value->length *= scale;
                value->percent *= scale;
                value->number *= scale;
            } else {
                if (value->dimension && right->dimension)
                    return {};
                if (!value->dimension && right->dimension)
                    std::swap(value, right);
                const auto scale = right->number;
                value->length *= scale;
                value->percent *= scale;
                value->number *= scale;
            }
        }
        return value;
    }
    std::optional<Math> sum(size_t &index, size_t end, unsigned depth) const {
        auto value = product(index, end, depth);
        if (!value)
            return {};
        while (index < end && tokens[index].type == LXB_CSS_SYNTAX_TOKEN_DELIM &&
               (tokens[index].text == "+" || tokens[index].text == "-")) {
            if (!tokens[index].space_before || index + 1 >= end || !tokens[index + 1].space_before)
                return {};
            const auto sign = tokens[index++].text == "-" ? -1. : 1.;
            const auto right = product(index, end, depth);
            if (!right || value->dimension != right->dimension)
                return {};
            value->length += sign * right->length;
            value->percent += sign * right->percent;
            value->number += sign * right->number;
        }
        return value;
    }

  public:
    Parser(const Tokens &value, Context env) : tokens(value), context(env) {}
    std::optional<RadiusLength> value(size_t &index, size_t end) const {
        if (index == end || tokens[index].type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS)
            return {};
        auto value = atom(index, end, 0);
        if (!value || (!value->dimension && (value->calculated || value->number != 0)))
            return {};
        if (!value->calculated && (value->length < 0 || value->percent < 0))
            return {};
        if (!std::isfinite(value->length) || !std::isfinite(value->percent) || std::abs(value->length) > 1e30 ||
            std::abs(value->percent) > 1e30)
            return {};
        return RadiusLength{static_cast<float>(value->length), static_cast<float>(value->percent)};
    }
};
std::string serialize(const Tokens &tokens, size_t first, size_t last) {
    std::string result;
    for (size_t i = first; i < last; ++i) {
        if (!result.empty())
            result += ' ';
        result += tokens[i].text;
        if (tokens[i].type == LXB_CSS_SYNTAX_TOKEN_FUNCTION)
            result += '(';
    }
    return result;
}
struct Parsed {
    RadiusStyle style;
    std::array<std::string, 4> values;
};
std::optional<Parsed> parse(std::string_view input, bool shorthand, Context context = {}) {
    const auto tokens = generated::tokenize(input);
    if (!tokens || tokens->empty())
        return {};
    const auto key = generated::keyword(*tokens);
    if (wide(key)) {
        Parsed result;
        result.values.fill(key);
        return result;
    }
    Parser parser(*tokens, context);
    std::vector<RadiusLength> x, y;
    std::vector<std::string> raw_x, raw_y;
    size_t index = 0;
    while (index < tokens->size()) {
        const auto start = index;
        const auto value = parser.value(index, tokens->size());
        if (!value)
            return {};
        x.push_back(*value);
        raw_x.push_back(serialize(*tokens, start, index));
        if (index < tokens->size() && (*tokens)[index].type == LXB_CSS_SYNTAX_TOKEN_DELIM &&
            (*tokens)[index].text == "/")
            break;
        if (x.size() > size_t(shorthand ? 4 : 2))
            return {};
    }
    if (x.empty() || x.size() > size_t(shorthand ? 4 : 2))
        return {};
    if (!shorthand) {
        if (index != tokens->size())
            return {};
        Parsed result;
        result.style[0] = {x[0], x.size() == 2 ? x[1] : x[0]};
        return result;
    }
    if (index < tokens->size()) {
        ++index;
        while (index < tokens->size()) {
            const auto start = index;
            const auto value = parser.value(index, tokens->size());
            if (!value)
                return {};
            y.push_back(*value);
            raw_y.push_back(serialize(*tokens, start, index));
            if (y.size() > 4)
                return {};
        }
        if (y.empty())
            return {};
    } else {
        y = x;
        raw_y = raw_x;
    }
    const auto expand = [](auto &values) {
        const auto size = values.size();
        if (size == 1)
            values.push_back(values[0]);
        if (size < 3)
            values.push_back(values[0]);
        if (size < 4)
            values.push_back(values[1]);
    };
    expand(x);
    expand(y);
    expand(raw_x);
    expand(raw_y);
    Parsed result;
    for (size_t i = 0; i < 4; ++i) {
        result.style[i] = {x[i], y[i]};
        result.values[i] = raw_x[i] + " " + raw_y[i];
    }
    return result;
}
} // namespace
bool radius_property(std::string_view name) {
    return name == "border-radius" || std::any_of(std::begin(radius_names), std::end(radius_names),
                                                  [&](const auto *value) { return name == value; });
}
bool valid_radius(std::string_view value, bool shorthand) { return parse(value, shorthand).has_value(); }
void expand_radius_shorthands(lxb_css_rule_declaration_list_t *list, std::string_view authored) {
    if (!list)
        return;
    for (auto *rule = list->first; rule;) {
        auto *next = rule->next;
        if (rule->type != LXB_CSS_RULE_DECLARATION) {
            rule = next;
            continue;
        }
        auto *item = lxb_css_rule_declaration(rule);
        if (item->type != LXB_CSS_PROPERTY__CUSTOM ||
            std::string_view(reinterpret_cast<const char *>(item->u.custom->name.data), item->u.custom->name.length) !=
                "border-radius") {
            rule = next;
            continue;
        }
        std::string_view raw(reinterpret_cast<const char *>(item->u.custom->value.data), item->u.custom->value.length);
        const auto offset = item->offset;
        if (offset.value_begin < offset.value_end && offset.value_end <= authored.size())
            raw = authored.substr(offset.value_begin, offset.value_end - offset.value_begin);
        const auto parsed = parse(raw, true);
        if (!parsed) {
            rule = next;
            continue;
        }
        std::string source;
        for (size_t i = 0; i < 4; ++i)
            source += std::string(radius_names[i]) + ':' + parsed->values[i] + (item->important ? "!important;" : ";");
        auto *parser = lxb_css_parser_create();
        if (!parser)
            throw std::bad_alloc();
        struct End {
            lxb_css_parser_t *value;
            ~End() { lxb_css_parser_destroy(value, true); }
        } end{parser};
        check_lxb(lxb_css_parser_init(parser, nullptr));
        parser->memory = rule->memory;
        auto *replacement = lxb_css_declaration_list_parse(parser, chars(source), source.size());
        if (!replacement)
            throw std::bad_alloc();
        auto *before = rule->prev;
        for (auto *node = replacement->first; node;) {
            auto *after = node->next;
            if (node->type == LXB_CSS_RULE_DECLARATION)
                lxb_css_rule_declaration(node)->offset = {};
            node->parent = &list->rule;
            node->prev = before;
            if (before)
                before->next = node;
            else
                list->first = node;
            before = node;
            node = after;
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
RadiusStyle radius_style(lxb_dom_node_t *node, const RadiusStyle &inherited, float em, float rem, int vw, int vh,
                         uint64_t &unsupported) {
    RadiusStyle result;
    for (size_t i = 0; i < 4; ++i) {
        const auto value = generated::declaration(node, radius_names[i]);
        if (value.empty())
            continue;
        const auto tokens = generated::tokenize(value);
        const auto key = tokens ? generated::keyword(*tokens) : std::string{};
        if (key == "inherit") {
            result[i] = inherited[i];
            continue;
        }
        if (wide(key))
            continue;
        const auto parsed = parse(value, false, {em, rem, double(vw), double(vh)});
        if (parsed)
            result[i] = parsed->style[0];
        else
            ++unsupported;
    }
    return result;
}
} // namespace xl::detail
