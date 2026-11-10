#include "nesting.h"
#include <algorithm>
#include <limits>

namespace xl::detail {
namespace {
using Type = lxb_css_syntax_token_type_t;
constexpr size_t no_end = std::numeric_limits<size_t>::max();
constexpr size_t source_limit = 16 * 1024 * 1024, output_limit = 32 * 1024 * 1024;
struct Token {
    Type type;
    size_t first, last, end = no_end;
    std::string ident;
};
bool trivia(Type type) { return type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE || type == LXB_CSS_SYNTAX_TOKEN_COMMENT; }
bool opening(Type type) {
    return type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS ||
           type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET || type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET;
}
std::string lower(std::string value) {
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return value;
}
class Normalizer {
    std::string_view source_;
    std::vector<Token> tokens_;
    std::string output_;
    bool changed_ = false;
    size_t rules_ = 0;
    size_t skip(size_t i, size_t end) const {
        while (i < end && trivia(tokens_[i].type))
            ++i;
        return i;
    }
    size_t after(size_t i, size_t end) const {
        return opening(tokens_[i].type) ? std::min(end, tokens_[i].end + 1) : i + 1;
    }
    std::string_view raw(size_t first, size_t last) const {
        const auto start = first == tokens_.size() ? source_.size() : tokens_[first].first;
        const auto finish = last == tokens_.size() ? source_.size() : tokens_[last].first;
        return source_.substr(start, finish - start);
    }
    void write(std::string_view value) {
        if (value.size() > output_limit - output_.size())
            throw DocumentFailure{XL_UNSUPPORTED};
        output_ += value;
    }
    bool amp(size_t index) const {
        const auto &t = tokens_[index];
        return t.type == LXB_CSS_SYNTAX_TOKEN_DELIM && source_.substr(t.first, t.last - t.first) == "&";
    }
    // Parse lists strictly before they become forgiving :is() arguments. An
    // invalid parent must not accidentally acquire valid matching descendants.
    struct SelectorInfo {
        lxb_css_selector_specificity_t specificity = 0;
        bool pseudo = false;
    };
    std::optional<SelectorInfo> valid_selector(std::string_view value) const {
        auto *parser = lxb_css_parser_create();
        if (!parser)
            throw std::bad_alloc();
        struct End {
            lxb_css_parser_t *p;
            lxb_css_selector_list_t *list = nullptr;
            ~End() {
                if (list)
                    lxb_css_selector_list_destroy_memory(list);
                lxb_css_parser_destroy(p, true);
            }
        } end{parser};
        check_lxb(lxb_css_parser_init(parser, nullptr));
        end.list = lxb_css_selectors_parse(parser, chars(value), value.size());
        if (parser->status == LXB_STATUS_ERROR_MEMORY_ALLOCATION)
            throw std::bad_alloc();
        if (!end.list || parser->status != LXB_STATUS_OK)
            return {};
        SelectorInfo result;
        for (auto *list = end.list; list; list = list->next) {
            result.specificity = std::max(result.specificity, list->specificity);
            for (auto *item = list->first; item; item = item->next)
                result.pseudo |= item->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT ||
                                 item->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_ELEMENT_FUNCTION;
        }
        return result;
    }
    std::optional<std::string> selector(size_t first, size_t last, std::string_view parent) {
        std::string result;
        const auto append = [&](std::string_view value) {
            if (value.size() > 65536 - result.size())
                throw DocumentFailure{XL_UNSUPPORTED};
            result += value;
        };
        std::string reference;
        if (!parent.empty()) {
            reference = ":is(";
            reference += parent;
            if (const auto info = valid_selector(parent); info && info->pseudo) {
                // :is() cannot match pseudo-elements, but '&' still takes the
                // maximum specificity of the full parent list. A never-match
                // branch preserves that weight when its pseudo entries vanish.
                reference += ",:not(*)";
                for (unsigned i = 0; i < lxb_css_selector_sp_a(info->specificity); ++i)
                    reference += "#xl-nesting";
                for (unsigned i = 0; i < lxb_css_selector_sp_b(info->specificity); ++i)
                    reference += ".xl-nesting";
                for (unsigned i = 0; i < lxb_css_selector_sp_c(info->specificity); ++i)
                    reference += ":not(xl-nesting)";
            }
            reference += ')';
        }
        size_t part = first;
        while (part < last) {
            size_t end = part;
            for (; end < last && tokens_[end].type != LXB_CSS_SYNTAX_TOKEN_COMMA; end = after(end, last)) {
            }
            const auto start = skip(part, end);
            if (start == end)
                return {};
            bool explicit_parent = false;
            for (size_t i = start; i < end; ++i) {
                if (tokens_[i].type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET) {
                    i = std::min(end, tokens_[i].end);
                    continue;
                }
                explicit_parent |= amp(i);
            }
            if (!result.empty())
                append(",");
            if (!parent.empty() && !explicit_parent) {
                append(reference);
                append(" ");
            }
            size_t offset = tokens_[start].first;
            for (size_t i = start; i < end; ++i) {
                if (tokens_[i].type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET) {
                    i = std::min(end, tokens_[i].end);
                    continue;
                }
                if (!amp(i))
                    continue;
                append(source_.substr(offset, tokens_[i].first - offset));
                if (parent.empty()) {
                    append(":where(:root)");
                    changed_ = true;
                } else
                    append(reference);
                offset = tokens_[i].last;
            }
            const auto finish = end == tokens_.size() ? source_.size() : tokens_[end].first;
            append(source_.substr(offset, finish - offset));
            if (end == last)
                break;
            part = end + 1;
            if (part == last)
                return {};
        }
        if (result.empty() || !valid_selector(result))
            return {};
        return result;
    }
    void emit(std::string_view selector, std::string &declarations) {
        if (declarations.empty())
            return;
        if (++rules_ > 65536)
            throw DocumentFailure{XL_UNSUPPORTED};
        write(selector);
        write("{");
        write(declarations);
        write("}\n");
        declarations.clear();
    }
    void list(size_t first, size_t last, std::string_view parent, unsigned depth) {
        if (depth > 64)
            throw DocumentFailure{XL_UNSUPPORTED};
        std::string declarations;
        size_t i = first;
        while ((i = skip(i, last)) < last) {
            if (tokens_[i].type == LXB_CSS_SYNTAX_TOKEN_SEMICOLON || tokens_[i].type == LXB_CSS_SYNTAX_TOKEN_CDO ||
                tokens_[i].type == LXB_CSS_SYNTAX_TOKEN_CDC) {
                ++i;
                continue;
            }
            const auto start = i;
            if (tokens_[i].type == LXB_CSS_SYNTAX_TOKEN_AT_KEYWORD) {
                if (!parent.empty()) {
                    changed_ = true;
                    emit(parent, declarations);
                }
                const auto name = lower(tokens_[i].ident);
                size_t end = i + 1;
                for (; end < last && tokens_[end].type != LXB_CSS_SYNTAX_TOKEN_LC_BRACKET &&
                       tokens_[end].type != LXB_CSS_SYNTAX_TOKEN_SEMICOLON;
                     end = after(end, last)) {
                }
                if (end == last || tokens_[end].type == LXB_CSS_SYNTAX_TOKEN_SEMICOLON) {
                    if (parent.empty()) {
                        write(raw(start, std::min(last, end + 1)));
                        write("\n");
                    }
                    i = std::min(last, end + 1);
                    continue;
                }
                const auto close = std::min(last, tokens_[end].end);
                const bool group = name == "media" || name == "layer" || name == "supports" || name == "container" ||
                                   name == "scope" || name == "starting-style";
                if (group) {
                    write(raw(start, end));
                    write("{\n");
                    list(end + 1, close, parent, depth + 1);
                    write("}\n");
                } else if (parent.empty()) {
                    write(raw(start, std::min(last, close + 1)));
                    write("\n");
                }
                i = std::min(last, close + 1);
                continue;
            }
            // Custom property values may themselves contain brace blocks. For
            // ordinary declarations, a top-level '{' instead starts a nested
            // qualified rule, including selectors such as div:hover {...}.
            const auto colon = skip(i + 1, last);
            const bool property = !parent.empty() && tokens_[i].type == LXB_CSS_SYNTAX_TOKEN_IDENT && colon < last &&
                                  tokens_[colon].type == LXB_CSS_SYNTAX_TOKEN_COLON;
            if (property) {
                const bool custom = tokens_[i].ident.starts_with("--");
                size_t end = colon + 1;
                for (; end < last && tokens_[end].type != LXB_CSS_SYNTAX_TOKEN_SEMICOLON; end = after(end, last))
                    if (!custom && tokens_[end].type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET)
                        break;
                if (end == last || tokens_[end].type == LXB_CSS_SYNTAX_TOKEN_SEMICOLON) {
                    const auto value = raw(start, std::min(last, end + 1));
                    if (value.size() > output_limit - declarations.size())
                        throw DocumentFailure{XL_UNSUPPORTED};
                    declarations += value;
                    declarations += ';';
                    i = std::min(last, end + 1);
                    continue;
                }
            }
            size_t open = i;
            for (; open < last && tokens_[open].type != LXB_CSS_SYNTAX_TOKEN_LC_BRACKET &&
                   tokens_[open].type != LXB_CSS_SYNTAX_TOKEN_SEMICOLON;
                 open = after(open, last)) {
            }
            if (open == last)
                break;
            if (tokens_[open].type == LXB_CSS_SYNTAX_TOKEN_SEMICOLON) {
                i = open + 1;
                continue;
            }
            if (!parent.empty()) {
                changed_ = true;
                emit(parent, declarations);
            }
            const auto close = std::min(last, tokens_[open].end);
            if (const auto nested = selector(start, open, parent))
                list(open + 1, close, *nested, depth + 1);
            i = std::min(last, close + 1);
        }
        if (!parent.empty())
            emit(parent, declarations);
    }

  public:
    explicit Normalizer(std::string_view source) : source_(source) {
        if (source.size() > source_limit)
            throw DocumentFailure{XL_UNSUPPORTED};
        auto *parser = lxb_css_syntax_tokenizer_create();
        if (!parser)
            throw std::bad_alloc();
        struct End {
            lxb_css_syntax_tokenizer_t *p;
            ~End() { lxb_css_syntax_tokenizer_destroy(p); }
        } end{parser};
        check_lxb(lxb_css_syntax_tokenizer_init(parser));
        parser->with_comment = true;
        lxb_css_syntax_tokenizer_buffer_set(parser, chars(source), source.size());
        std::vector<size_t> stack;
        while (auto *token = lxb_css_syntax_token(parser)) {
            if (token->type == LXB_CSS_SYNTAX_TOKEN__EOF)
                break;
            const auto *base = lxb_css_syntax_token_base(token);
            const auto offset = size_t(reinterpret_cast<const char *>(base->begin) - source.data());
            Token value{token->type, offset, offset + base->length, no_end, {}};
            if (token->type == LXB_CSS_SYNTAX_TOKEN_IDENT || token->type == LXB_CSS_SYNTAX_TOKEN_AT_KEYWORD) {
                const auto *text = lxb_css_syntax_token_string(token);
                value.ident.assign(reinterpret_cast<const char *>(text->data), text->length);
            }
            tokens_.push_back(std::move(value));
            const auto index = tokens_.size() - 1;
            if (opening(token->type)) {
                if (stack.size() >= 128)
                    throw DocumentFailure{XL_UNSUPPORTED};
                stack.push_back(index);
            } else if (token->type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS ||
                       token->type == LXB_CSS_SYNTAX_TOKEN_RS_BRACKET ||
                       token->type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET) {
                if (!stack.empty()) {
                    const auto type = tokens_[stack.back()].type;
                    const bool match =
                        token->type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS
                            ? (type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS)
                        : token->type == LXB_CSS_SYNTAX_TOKEN_RS_BRACKET ? type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET
                                                                         : type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET;
                    if (match) {
                        tokens_[stack.back()].end = index;
                        stack.pop_back();
                    }
                }
            }
            if (tokens_.size() > 1048576)
                throw DocumentFailure{XL_UNSUPPORTED};
            lxb_css_syntax_token_consume(parser);
        }
        check_lxb(parser->status);
        for (const auto index : stack)
            tokens_[index].end = tokens_.size();
        list(0, tokens_.size(), {}, 0);
    }
    std::optional<std::string> normalized() {
        if (!changed_)
            return {};
        return std::move(output_);
    }
};
} // namespace
NestedSheet::NestedSheet(std::shared_ptr<const std::string> input, const void *identity)
    : authored(std::move(input)), owner(identity), source(authored) {
    const auto normalized = Normalizer(*authored).normalized();
    if (!normalized)
        return;
    source = std::make_shared<const std::string>(std::move(*normalized));
    auto *next = lxb_css_stylesheet_create(nullptr);
    if (!next)
        throw std::bad_alloc();
    auto *parser = lxb_css_parser_create();
    if (!parser) {
        lxb_css_stylesheet_destroy(next, true);
        throw std::bad_alloc();
    }
    struct End {
        lxb_css_parser_t *p;
        ~End() { lxb_css_parser_destroy(p, true); }
    } end{parser};
    try {
        check_lxb(lxb_css_parser_init(parser, nullptr));
        check_lxb(lxb_css_stylesheet_parse(next, parser, chars(*source), source->size()));
    } catch (...) {
        lxb_css_stylesheet_destroy(next, true);
        throw;
    }
    sheet = next;
}
NestedSheet::~NestedSheet() {
    if (sheet)
        lxb_css_stylesheet_destroy(sheet, true);
}
void NestedStyles::begin() {
    next_.clear();
    bytes_ = 0;
}
std::shared_ptr<NestedSheet> NestedStyles::get(std::shared_ptr<const std::string> source, const void *owner) {
    std::shared_ptr<NestedSheet> entry;
    for (const auto &item : next_)
        if (item->owner == owner && *item->authored == *source)
            return item;
    for (const auto &item : cached_)
        if (item->owner == owner && *item->authored == *source) {
            entry = item;
            break;
        }
    if (!entry)
        entry = std::make_shared<NestedSheet>(std::move(source), owner);
    const auto bytes = entry->authored->size() + (entry->sheet ? entry->source->size() : 0);
    if (bytes > 64 * 1024 * 1024 - bytes_)
        throw DocumentFailure{XL_UNSUPPORTED};
    bytes_ += bytes;
    next_.push_back(entry);
    return entry;
}
void NestedStyles::finish() { cached_ = std::move(next_); }
} // namespace xl::detail
