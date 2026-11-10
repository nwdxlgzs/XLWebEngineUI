#include "media_query.h"
#include "document/dom_internal.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace xl::detail {
namespace {
enum class Truth { no, yes, unknown };
Truth negate(Truth value) { return value == Truth::unknown ? value : value == Truth::yes ? Truth::no : Truth::yes; }
Truth combine(Truth a, Truth b, bool either) {
    const auto winning = either ? Truth::yes : Truth::no;
    return a == winning || b == winning                 ? winning
           : a == Truth::unknown || b == Truth::unknown ? Truth::unknown
           : either                                     ? Truth::no
                                                        : Truth::yes;
}
Truth truth(bool value) { return value ? Truth::yes : Truth::no; }
std::string lower(std::string value) {
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return value;
}
using Type = lxb_css_syntax_token_type_t;
struct Token {
    Type type;
    std::string text, raw;
    double number = 0;
    bool integer = false;
    size_t offset = 0, end = 0, close = SIZE_MAX;
};
using Tokens = std::vector<Token>;
struct Node {
    bool valid = true;
    std::string text;
    std::function<Truth(const ViewEnvironment &)> evaluate;
};
Node constant(Truth value, std::string text, bool valid = true) {
    return {valid, std::move(text), [value](const auto &) { return value; }};
}
Node invalid() { return constant(Truth::no, "not all", false); }
Node logical(Node a, Node b, bool either) {
    const auto text = a.text + (either ? " or " : " and ") + b.text;
    return {a.valid && b.valid, text, [a = std::move(a), b = std::move(b), either](const auto &env) {
                return combine(a.evaluate(env), b.evaluate(env), either);
            }};
}
bool opening(Type type) {
    return type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS || type == LXB_CSS_SYNTAX_TOKEN_FUNCTION ||
           type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET || type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET;
}
std::optional<Tokens> tokenize(std::string_view query) {
    if (query.size() > 65536)
        return {};
    auto *parser = lxb_css_syntax_tokenizer_create();
    if (!parser)
        throw std::bad_alloc();
    struct End {
        lxb_css_syntax_tokenizer_t *p;
        ~End() { lxb_css_syntax_tokenizer_destroy(p); }
    } end{parser};
    check_lxb(lxb_css_syntax_tokenizer_init(parser));
    lxb_css_syntax_tokenizer_buffer_set(parser, chars(query), query.size());
    Tokens result;
    std::vector<size_t> stack;
    while (auto *token = lxb_css_syntax_token(parser)) {
        if (token->type == LXB_CSS_SYNTAX_TOKEN__EOF)
            break;
        if (token->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE || token->type == LXB_CSS_SYNTAX_TOKEN_COMMENT) {
            lxb_css_syntax_token_consume(parser);
            continue;
        }
        const auto *base = lxb_css_syntax_token_base(token);
        std::string raw(reinterpret_cast<const char *>(base->begin), base->length);
        Token next{token->type, raw, raw, 0, false, token->offset, token->offset + base->length, SIZE_MAX};
        if (token->type == LXB_CSS_SYNTAX_TOKEN_IDENT || token->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION) {
            const auto *value = lxb_css_syntax_token_ident(token);
            next.text = lower({reinterpret_cast<const char *>(value->data), value->length});
        }
        if (token->type == LXB_CSS_SYNTAX_TOKEN_DIMENSION) {
            const auto *value = lxb_css_syntax_token_dimension(token);
            next.number = value->num.num;
            next.text = lower({reinterpret_cast<const char *>(value->str.data), value->str.length});
        } else if (token->type == LXB_CSS_SYNTAX_TOKEN_NUMBER) {
            const auto *value = lxb_css_syntax_token_number(token);
            next.number = value->num;
            next.integer = !value->is_float;
        }
        result.push_back(std::move(next));
        const auto index = result.size() - 1;
        if (opening(token->type)) {
            stack.push_back(index);
            if (stack.size() > 64)
                return {};
        } else if (token->type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS ||
                   token->type == LXB_CSS_SYNTAX_TOKEN_RS_BRACKET || token->type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
            if (!stack.empty()) {
                const auto type = result[stack.back()].type;
                const bool match =
                    token->type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS
                        ? type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS
                    : token->type == LXB_CSS_SYNTAX_TOKEN_RS_BRACKET ? type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET
                                                                     : type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET;
                if (match) {
                    result[stack.back()].close = index;
                    stack.pop_back();
                }
            }
        }
        if (result.size() >= 8192)
            return {};
        lxb_css_syntax_token_consume(parser);
    }
    check_lxb(parser->status);
    while (!stack.empty()) {
        const auto index = stack.back();
        stack.pop_back();
        const auto type = result[index].type;
        const auto close = type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET   ? LXB_CSS_SYNTAX_TOKEN_RS_BRACKET
                           : type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET ? LXB_CSS_SYNTAX_TOKEN_RC_BRACKET
                                                                     : LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS;
        result[index].close = result.size();
        const std::string raw = close == LXB_CSS_SYNTAX_TOKEN_RS_BRACKET   ? "]"
                                : close == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET ? "}"
                                                                           : ")";
        result.push_back({close, raw, raw, 0, false, query.size(), query.size(), SIZE_MAX});
    }
    return result;
}
struct Feature {
    bool known = false, discrete = false, integral = false, ratio = false, resolution = false;
    double number = 0;
    std::string value;
};
Feature feature(std::string_view name, const ViewEnvironment &env) {
    const double sw = env.screen_width > 0 ? env.screen_width : env.width,
                 sh = env.screen_height > 0 ? env.screen_height : env.height;
    Feature result;
    result.known = true;
    if (name == "width")
        result.number = env.width;
    else if (name == "height")
        result.number = env.height;
    else if (name == "device-width")
        result.number = sw;
    else if (name == "device-height")
        result.number = sh;
    else if (name == "aspect-ratio" || name == "device-aspect-ratio") {
        result.ratio = true;
        result.number = name == "aspect-ratio" ? double(env.width) / env.height : sw / sh;
    } else if (name == "resolution") {
        result.resolution = true;
        result.number = env.pixel_ratio;
    } else if (name == "color" || name == "color-index" || name == "monochrome" || name == "grid") {
        result.integral = true;
        result.number = name == "color" ? std::clamp(env.color_depth / 3, 0, 16) : 0;
    } else {
        result.discrete = true;
        if (name == "orientation")
            result.value = env.height >= env.width ? "portrait" : "landscape";
        else if (name == "scan")
            result.value = "progressive";
        else if (name == "update")
            result.value = "fast";
        else if (name == "overflow-block" || name == "overflow-inline")
            result.value = "scroll";
        else if (name == "color-gamut")
            result.value = "srgb";
        else if (name == "dynamic-range")
            result.value = "standard";
        else if (name == "prefers-color-scheme")
            result.value = env.color_scheme ? "dark" : "light";
        else if (name == "prefers-reduced-motion")
            result.value = env.reduced_motion ? "reduce" : "no-preference";
        else if (name == "prefers-contrast") {
            constexpr const char *values[]{"no-preference", "more", "less", "custom"};
            result.value = values[env.contrast % 4];
        } else if (name == "forced-colors")
            result.value = env.forced_colors ? "active" : "none";
        else if (name == "pointer") {
            constexpr const char *values[]{"none", "coarse", "fine"};
            result.value = values[env.pointer % 3];
        } else if (name == "hover")
            result.value = env.hover ? "hover" : "none";
        else if (name == "any-pointer")
            result.value = env.any_pointer ? "available" : "none";
        else if (name == "any-hover")
            result.value = env.any_hover ? "hover" : "none";
        else
            result.known = false;
    }
    return result;
}
bool discrete_value(std::string_view name, std::string_view value) {
    if (name == "orientation")
        return value == "portrait" || value == "landscape";
    if (name == "scan")
        return value == "progressive" || value == "interlace";
    if (name == "update")
        return value == "none" || value == "slow" || value == "fast";
    if (name == "overflow-block")
        return value == "none" || value == "scroll" || value == "optional-paged" || value == "paged";
    if (name == "overflow-inline")
        return value == "none" || value == "scroll";
    if (name == "color-gamut")
        return value == "srgb" || value == "p3" || value == "rec2020";
    if (name == "dynamic-range")
        return value == "standard" || value == "high";
    if (name == "prefers-color-scheme")
        return value == "light" || value == "dark";
    if (name == "prefers-reduced-motion")
        return value == "no-preference" || value == "reduce";
    if (name == "prefers-contrast")
        return value == "no-preference" || value == "more" || value == "less" || value == "custom";
    if (name == "forced-colors")
        return value == "none" || value == "active";
    if (name == "pointer" || name == "any-pointer")
        return value == "none" || value == "coarse" || value == "fine";
    if (name == "hover" || name == "any-hover")
        return value == "none" || value == "hover";
    return false;
}
enum class Compare { equal, less, less_equal, greater, greater_equal };
bool compare(double a, double b, Compare op) {
    switch (op) {
    case Compare::equal:
        return a == b;
    case Compare::less:
        return a < b;
    case Compare::less_equal:
        return a <= b;
    case Compare::greater:
        return a > b;
    case Compare::greater_equal:
        return a >= b;
    }
    return false;
}
Compare reverse(Compare value) {
    return value == Compare::less            ? Compare::greater
           : value == Compare::less_equal    ? Compare::greater_equal
           : value == Compare::greater       ? Compare::less
           : value == Compare::greater_equal ? Compare::less_equal
                                             : Compare::equal;
}
struct Input {
    bool valid = false;
    double number = 0;
    std::string unit;
    bool integer = false;
    bool ratio = false;
};
double number(const Input &input, const Feature &feature, const ViewEnvironment &env) {
    if (feature.ratio)
        return input.unit.empty() && input.number >= 0 ? input.number : std::numeric_limits<double>::quiet_NaN();
    if (feature.integral)
        return input.unit.empty() && input.integer && !input.ratio ? input.number
                                                                   : std::numeric_limits<double>::quiet_NaN();
    const auto &unit = input.unit;
    if (feature.resolution) {
        if (unit == "dppx" || unit == "x")
            return input.number;
        if (unit == "dpi")
            return input.number / 96;
        if (unit == "dpcm")
            return input.number * 2.54 / 96;
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (unit == "px" || (unit.empty() && input.number == 0 && !input.ratio))
        return input.number;
    if (unit == "em" || unit == "rem")
        return input.number * 16;
    if (unit == "in")
        return input.number * 96;
    if (unit == "cm")
        return input.number * 96 / 2.54;
    if (unit == "mm")
        return input.number * 96 / 25.4;
    if (unit == "q")
        return input.number * 96 / 101.6;
    if (unit == "pt")
        return input.number * 96 / 72;
    if (unit == "pc")
        return input.number * 16;
    if (unit == "vw" || unit == "svw" || unit == "lvw" || unit == "dvw")
        return input.number * env.width / 100;
    if (unit == "vh" || unit == "svh" || unit == "lvh" || unit == "dvh")
        return input.number * env.height / 100;
    if (unit == "vmin")
        return input.number * std::min(env.width, env.height) / 100;
    if (unit == "vmax")
        return input.number * std::max(env.width, env.height) / 100;
    return std::numeric_limits<double>::quiet_NaN();
}
class Parser {
    const Tokens &tokens;
    bool ident(size_t i, size_t end, std::string_view name) const {
        return i < end && tokens[i].type == LXB_CSS_SYNTAX_TOKEN_IDENT && tokens[i].text == name;
    }
    Input input(size_t first, size_t last) const {
        if (first == last)
            return {};
        const auto &token = tokens[first];
        if (last == first + 1 &&
            (token.type == LXB_CSS_SYNTAX_TOKEN_NUMBER || token.type == LXB_CSS_SYNTAX_TOKEN_DIMENSION) &&
            std::isfinite(token.number))
            return {true, token.number, token.type == LXB_CSS_SYNTAX_TOKEN_DIMENSION ? token.text : "", token.integer,
                    false};
        if (last == first + 3 && token.type == LXB_CSS_SYNTAX_TOKEN_NUMBER &&
            tokens[first + 1].type == LXB_CSS_SYNTAX_TOKEN_DELIM && tokens[first + 1].text == "/" &&
            tokens[first + 2].type == LXB_CSS_SYNTAX_TOKEN_NUMBER) {
            const auto a = token.number, b = tokens[first + 2].number;
            if (a < 0 || b < 0 || !std::isfinite(a) || !std::isfinite(b) || (a == 0 && b == 0))
                return {};
            return {true, b == 0 ? std::numeric_limits<double>::infinity() : a / b, "", false, true};
        }
        return {};
    }
    std::string render(size_t first, size_t last) const {
        std::string text;
        for (size_t i = first; i < last; ++i) {
            const auto &token = tokens[i];
            if (token.type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS) {
                while (!text.empty() && text.back() == ' ')
                    text.pop_back();
                text += ')';
            } else if (token.type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS) {
                if (!text.empty() && text.back() != ' ' && text.back() != '(')
                    text += ' ';
                text += '(';
            } else if (token.type == LXB_CSS_SYNTAX_TOKEN_COLON) {
                while (!text.empty() && text.back() == ' ')
                    text.pop_back();
                text += ": ";
            } else if (token.type == LXB_CSS_SYNTAX_TOKEN_COMMA) {
                while (!text.empty() && text.back() == ' ')
                    text.pop_back();
                text += ", ";
            } else {
                if (!text.empty() && text.back() != ' ' && text.back() != '(')
                    text += ' ';
                if (token.type == LXB_CSS_SYNTAX_TOKEN_IDENT)
                    text += token.text;
                else if (token.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION)
                    text += token.text + '(';
                else if (token.type == LXB_CSS_SYNTAX_TOKEN_DELIM && (token.text == "<" || token.text == ">") &&
                         i + 1 < last && tokens[i + 1].text == "=" && token.end == tokens[i + 1].offset) {
                    text += token.text + '=';
                    ++i;
                } else
                    text += lower(token.raw);
            }
        }
        return text;
    }
    Node numeric(std::string name, Input value, Compare op, std::string text) const {
        return {true, std::move(text), [name = std::move(name), value = std::move(value), op](const auto &env) {
                    const auto current = feature(name, env);
                    if (!current.known || current.discrete || !value.valid)
                        return Truth::unknown;
                    const auto desired = number(value, current, env);
                    if (std::isnan(desired))
                        return Truth::unknown;
                    if (name == "grid" && (op != Compare::equal || (desired != 0 && desired != 1)))
                        return Truth::unknown;
                    return truth(compare(current.number, desired, op));
                }};
    }
    Node media_feature(size_t first, size_t last) const {
        const auto text = '(' + render(first, last) + ')';
        if (first == last)
            return constant(Truth::unknown, text);
        if (last == first + 1 && tokens[first].type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
            const auto name = tokens[first].text;
            return {true, text, [name](const auto &env) {
                        const auto value = feature(name, env);
                        if (!value.known)
                            return Truth::unknown;
                        return truth(value.discrete ? value.value != "none" && value.value != "no-preference"
                                                    : value.number != 0);
                    }};
        }
        if (first + 1 < last && tokens[first].type == LXB_CSS_SYNTAX_TOKEN_IDENT &&
            tokens[first + 1].type == LXB_CSS_SYNTAX_TOKEN_COLON) {
            auto name = tokens[first].text;
            Compare op = Compare::equal;
            bool prefixed = false;
            if (name.starts_with("min-") || name.starts_with("max-")) {
                op = name.starts_with("min-") ? Compare::greater_equal : Compare::less_equal;
                name.erase(0, 4);
                prefixed = true;
            }
            if (last == first + 3 && tokens[first + 2].type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
                const auto desired = tokens[first + 2].text;
                return {true, text, [name, desired, prefixed](const auto &env) {
                            const auto current = feature(name, env);
                            if (prefixed || !current.known || !current.discrete || !discrete_value(name, desired))
                                return Truth::unknown;
                            if (name == "any-pointer")
                                return truth(desired == "none"
                                                 ? !env.any_pointer
                                                 : bool(env.any_pointer & (desired == "coarse" ? 1u : 2u)));
                            return truth(current.value == desired);
                        }};
            }
            return numeric(std::move(name), input(first + 2, last), op, text);
        }
        struct Operator {
            size_t first, last;
            Compare op;
        };
        std::vector<Operator> operators;
        for (size_t i = first; i < last; ++i) {
            if (opening(tokens[i].type)) {
                i = tokens[i].close;
                continue;
            }
            if (tokens[i].type != LXB_CSS_SYNTAX_TOKEN_DELIM)
                continue;
            const auto &symbol = tokens[i].text;
            if (symbol != "<" && symbol != ">" && symbol != "=")
                continue;
            const auto start = i;
            Compare op = symbol == "<" ? Compare::less : symbol == ">" ? Compare::greater : Compare::equal;
            if (i + 1 < last && tokens[i + 1].text == "=" && tokens[i].end == tokens[i + 1].offset &&
                op != Compare::equal) {
                ++i;
                op = op == Compare::less ? Compare::less_equal : Compare::greater_equal;
            }
            operators.push_back({start, i + 1, op});
        }
        if (operators.size() == 1) {
            const auto &op = operators[0];
            if (op.first == first + 1 && tokens[first].type == LXB_CSS_SYNTAX_TOKEN_IDENT)
                return numeric(tokens[first].text, input(op.last, last), op.op, text);
            if (op.last + 1 == last && tokens[op.last].type == LXB_CSS_SYNTAX_TOKEN_IDENT)
                return numeric(tokens[op.last].text, input(first, op.first), reverse(op.op), text);
        } else if (operators.size() == 2) {
            const auto &a = operators[0];
            const auto &b = operators[1];
            const bool ascending = (a.op == Compare::less || a.op == Compare::less_equal) &&
                                   (b.op == Compare::less || b.op == Compare::less_equal);
            const bool descending = (a.op == Compare::greater || a.op == Compare::greater_equal) &&
                                    (b.op == Compare::greater || b.op == Compare::greater_equal);
            if ((ascending || descending) && a.last + 1 == b.first &&
                tokens[a.last].type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
                auto left = numeric(tokens[a.last].text, input(first, a.first), reverse(a.op), text),
                     right = numeric(tokens[a.last].text, input(b.last, last), b.op, text);
                return {true, text, [left = std::move(left), right = std::move(right)](const auto &env) {
                            return combine(left.evaluate(env), right.evaluate(env), false);
                        }};
            }
        }
        return constant(Truth::unknown, text);
    }
    Node term(size_t &first, size_t last, unsigned depth) const {
        if (first == last || depth > 64)
            return invalid();
        const auto &token = tokens[first];
        if ((token.type != LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS && token.type != LXB_CSS_SYNTAX_TOKEN_FUNCTION) ||
            token.close >= last)
            return invalid();
        const auto begin = first + 1, end = token.close;
        first = end + 1;
        if (token.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION)
            return constant(Truth::unknown, render(begin - 1, end + 1));
        if (begin < end && (tokens[begin].type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS ||
                            tokens[begin].type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || ident(begin, end, "not"))) {
            auto nested = condition(begin, end, depth + 1);
            if (nested.valid) {
                nested.text = '(' + nested.text + ')';
                return nested;
            }
        }
        return media_feature(begin, end);
    }
    Node condition(size_t first, size_t last, unsigned depth, bool allow_or = true) const {
        if (ident(first, last, "not")) {
            ++first;
            auto value = term(first, last, depth + 1);
            if (first != last || !value.valid)
                return invalid();
            const auto text = "not " + value.text;
            return {true, text, [value = std::move(value)](const auto &env) { return negate(value.evaluate(env)); }};
        }
        auto value = term(first, last, depth + 1);
        if (!value.valid)
            return invalid();
        std::string operation;
        while (first < last) {
            const bool either = ident(first, last, "or"), both = ident(first, last, "and");
            if ((!either && !both) || (!allow_or && either) || (!operation.empty() && operation != tokens[first].text))
                return invalid();
            operation = tokens[first++].text;
            auto next = term(first, last, depth + 1);
            if (!next.valid)
                return invalid();
            value = logical(std::move(value), std::move(next), either);
        }
        return value;
    }

  public:
    explicit Parser(const Tokens &value) : tokens(value) {}
    Node query(size_t first, size_t last) const {
        if (first == last)
            return invalid();
        if (tokens[first].type == LXB_CSS_SYNTAX_TOKEN_IDENT && !ident(first, last, "not")) {
            bool only = ident(first, last, "only");
            if (only)
                ++first;
            if (first == last || tokens[first].type != LXB_CSS_SYNTAX_TOKEN_IDENT)
                return invalid();
            const auto name = tokens[first++].text;
            if (name == "not" || name == "only" || name == "and" || name == "or" || name == "layer")
                return invalid();
            auto result = constant(truth(name == "all" || name == "screen"), (only ? "only " : "") + name);
            if (first == last)
                return result;
            if (!ident(first, last, "and"))
                return invalid();
            ++first;
            auto tail = condition(first, last, 0, false);
            if (!tail.valid)
                return invalid();
            return logical(std::move(result), std::move(tail), false);
        }
        if (ident(first, last, "not") && first + 1 < last && tokens[first + 1].type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
            const auto name = tokens[first + 1].text;
            if (name == "not" || name == "only" || name == "and" || name == "or" || name == "layer")
                return invalid();
            auto result = constant(truth(name == "all" || name == "screen"), name);
            first += 2;
            if (first < last) {
                if (!ident(first, last, "and"))
                    return invalid();
                auto tail = condition(first + 1, last, 0, false);
                if (!tail.valid)
                    return invalid();
                result = logical(std::move(result), std::move(tail), false);
            }
            const auto text = "not " + result.text;
            return {true, text, [result = std::move(result)](const auto &env) { return negate(result.evaluate(env)); }};
        }
        return condition(first, last, 0);
    }
};
} // namespace
struct MediaQuery::Impl {
    std::vector<Node> queries;
    std::string text;
};
MediaQuery::MediaQuery(std::string_view query) : impl_(std::make_unique<Impl>()) {
    const auto tokens = tokenize(query);
    if (!tokens) {
        impl_->queries.push_back(invalid());
        impl_->text = "not all";
        return;
    }
    if (tokens->empty()) {
        impl_->queries.push_back(constant(Truth::yes, ""));
        return;
    }
    Parser parser(*tokens);
    size_t first = 0;
    for (size_t i = 0; i <= tokens->size(); ++i) {
        if (i < tokens->size() && opening((*tokens)[i].type)) {
            i = (*tokens)[i].close;
            continue;
        }
        if (i < tokens->size() && (*tokens)[i].type != LXB_CSS_SYNTAX_TOKEN_COMMA)
            continue;
        auto part = parser.query(first, i);
        if (!impl_->text.empty())
            impl_->text += ", ";
        impl_->text += part.text;
        impl_->queries.push_back(std::move(part));
        first = i + 1;
        if (impl_->queries.size() > 256) {
            impl_->queries.clear();
            impl_->queries.push_back(invalid());
            impl_->text = "not all";
            return;
        }
    }
}
MediaQuery::~MediaQuery() = default;
bool MediaQuery::matches(const ViewEnvironment &env) const {
    return std::any_of(impl_->queries.begin(), impl_->queries.end(),
                       [&](const auto &query) { return query.evaluate(env) == Truth::yes; });
}
const std::string &MediaQuery::serialized() const { return impl_->text; }
} // namespace xl::detail
