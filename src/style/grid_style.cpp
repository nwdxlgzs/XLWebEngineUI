#include "grid_style.h"
#include "document/dom_internal.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <map>

namespace xl::detail {
namespace {
struct Token {
    lxb_css_syntax_token_type_t type;
    std::string raw, text;
};
std::vector<Token> tokens(std::string_view input) {
    auto *tokenizer = lxb_css_syntax_tokenizer_create();
    if (!tokenizer)
        throw std::bad_alloc();
    struct Cleanup {
        lxb_css_syntax_tokenizer_t *p;
        ~Cleanup() { lxb_css_syntax_tokenizer_destroy(p); }
    } cleanup{tokenizer};
    check_lxb(lxb_css_syntax_tokenizer_init(tokenizer));
    tokenizer->with_comment = true;
    lxb_css_syntax_tokenizer_buffer_set(tokenizer, chars(input), input.size());
    std::vector<Token> result;
    while (auto *token = lxb_css_syntax_token(tokenizer)) {
        if (token->type == LXB_CSS_SYNTAX_TOKEN__EOF)
            break;
        if (token->type != LXB_CSS_SYNTAX_TOKEN_WHITESPACE && token->type != LXB_CSS_SYNTAX_TOKEN_COMMENT) {
            const auto *base = lxb_css_syntax_token_base(token);
            Token next{token->type, {reinterpret_cast<const char *>(base->begin), base->length}, {}};
            next.text = next.raw;
            if (token->type == LXB_CSS_SYNTAX_TOKEN_IDENT || token->type == LXB_CSS_SYNTAX_TOKEN_STRING ||
                token->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION) {
                const auto *text = lxb_css_syntax_token_ident(token);
                next.text.assign(reinterpret_cast<const char *>(text->data), text->length);
            }
            result.push_back(std::move(next));
        }
        lxb_css_syntax_token_consume(tokenizer);
    }
    check_lxb(tokenizer->status);
    return result;
}
std::string lower(std::string value) {
    for (auto &c : value)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return value;
}
bool custom_ident(const Token &token) {
    if (token.type != LXB_CSS_SYNTAX_TOKEN_IDENT)
        return false;
    const auto word = lower(token.text);
    return word != "auto" && word != "span" && word != "none" && word != "initial" && word != "inherit" &&
           word != "unset" && word != "revert" && word != "revert-layer" && word != "default";
}
std::optional<GridBreadth> breadth(const Token &token, const GridUnits &units) {
    const auto word = lower(token.text);
    if (token.type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
        if (word == "auto")
            return GridBreadth{};
        if (word == "min-content")
            return GridBreadth{GridBreadthKind::min_content};
        if (word == "max-content")
            return GridBreadth{GridBreadthKind::max_content};
        return {};
    }
    double number = 0;
    auto raw = lower(token.raw);
    if (!raw.empty() && raw[0] == '+')
        raw.erase(0, 1);
    const auto result = std::from_chars(raw.data(), raw.data() + raw.size(), number);
    if (result.ec != std::errc{} || !std::isfinite(number) || number < 0 || number > 1e9)
        return {};
    const std::string_view suffix(result.ptr, size_t(raw.data() + raw.size() - result.ptr));
    if (suffix == "fr")
        return GridBreadth{GridBreadthKind::fraction, float(number)};
    if (suffix == "%")
        return GridBreadth{GridBreadthKind::percentage, float(number / 100)};
    float scale = 0;
    if (suffix == "px" || (suffix.empty() && number == 0))
        scale = 1;
    else if (suffix == "em")
        scale = units.font;
    else if (suffix == "rem")
        scale = units.root;
    else if (suffix == "vw")
        scale = units.width / 100;
    else if (suffix == "vh")
        scale = units.height / 100;
    else if (suffix == "pt")
        scale = 96.0f / 72;
    else if (suffix == "pc")
        scale = 16;
    else if (suffix == "in")
        scale = 96;
    else if (suffix == "cm")
        scale = 96 / 2.54f;
    else if (suffix == "mm")
        scale = 96 / 25.4f;
    else
        return {};
    return GridBreadth{GridBreadthKind::fixed, float(number) * scale};
}
struct Parser {
    const std::vector<Token> &input;
    const GridUnits &units;
    size_t position = 0;
    bool implicit = false;
    bool accept(lxb_css_syntax_token_type_t type) {
        if (position == input.size() || input[position].type != type)
            return false;
        ++position;
        return true;
    }
    std::optional<GridTrack> track() {
        if (position == input.size())
            return {};
        if (input[position].type != LXB_CSS_SYNTAX_TOKEN_FUNCTION) {
            const auto value = breadth(input[position++], units);
            if (!value)
                return {};
            return value->kind == GridBreadthKind::fraction ? GridTrack{{}, *value}
                                                            : GridTrack{*value, *value};
        }
        const auto name = lower(input[position++].text);
        if (position == input.size())
            return {};
        const auto first = breadth(input[position++], units);
        if (!first)
            return {};
        if (name == "fit-content") {
            if (first->kind != GridBreadthKind::fixed && first->kind != GridBreadthKind::percentage)
                return {};
            if (!accept(LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS))
                return {};
            return GridTrack{{}, *first, true};
        }
        if (name != "minmax" || first->kind == GridBreadthKind::fraction ||
            !accept(LXB_CSS_SYNTAX_TOKEN_COMMA) || position == input.size())
            return {};
        const auto second = breadth(input[position++], units);
        if (!second || !accept(LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS))
            return {};
        return GridTrack{*first, *second};
    }
    bool list(GridAxis &axis, bool nested = false) {
        while (position < input.size() && input[position].type != LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS) {
            if (accept(LXB_CSS_SYNTAX_TOKEN_LS_BRACKET)) {
                while (position < input.size() && input[position].type != LXB_CSS_SYNTAX_TOKEN_RS_BRACKET) {
                    if (!custom_ident(input[position]))
                        return false;
                    axis.names.back().push_back(input[position++].text);
                }
                if (!accept(LXB_CSS_SYNTAX_TOKEN_RS_BRACKET))
                    return false;
                continue;
            }
            if (input[position].type == LXB_CSS_SYNTAX_TOKEN_FUNCTION &&
                lower(input[position].text) == "repeat") {
                if (nested || implicit || ++position == input.size())
                    return false;
                int count = 0;
                bool automatic = false, fit = false;
                const auto word = lower(input[position].text);
                if (word == "auto-fill" || word == "auto-fit") {
                    automatic = true;
                    fit = word == "auto-fit";
                    count = 1;
                } else {
                    const auto &raw = input[position].raw;
                    const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), count);
                    if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size() || count < 1 ||
                        count > grid_track_limit)
                        return false;
                }
                ++position;
                if (!accept(LXB_CSS_SYNTAX_TOKEN_COMMA))
                    return false;
                GridAxis pattern;
                if (!list(pattern, true) || pattern.tracks.empty() ||
                    !accept(LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS))
                    return false;
                if (automatic) {
                    if (axis.repeat_start >= 0)
                        return false;
                    for (const auto &track : pattern.tracks)
                        if (track.minimum.kind != GridBreadthKind::fixed &&
                            track.minimum.kind != GridBreadthKind::percentage &&
                            track.maximum.kind != GridBreadthKind::fixed &&
                            track.maximum.kind != GridBreadthKind::percentage)
                            return false;
                    axis.repeat_start = int(axis.tracks.size());
                    axis.repeat_count = int(pattern.tracks.size());
                    axis.auto_fit = fit;
                }
                for (int i = 0; i < count; ++i) {
                    axis.names.back().insert(axis.names.back().end(), pattern.names[0].begin(),
                                             pattern.names[0].end());
                    for (size_t j = 0; j < pattern.tracks.size(); ++j) {
                        axis.tracks.push_back(pattern.tracks[j]);
                        axis.names.push_back(pattern.names[j + 1]);
                    }
                }
            } else {
                const auto value = track();
                if (!value)
                    return false;
                axis.tracks.push_back(*value);
                axis.names.emplace_back();
            }
            if (axis.tracks.size() > grid_track_limit)
                return false;
        }
        return true;
    }
};
} // namespace
std::optional<GridAxis> grid_tracks(std::string_view value, const GridUnits &units, bool implicit) {
    const auto input = tokens(value);
    if (input.size() == 1 && lower(input[0].text) == "none" && !implicit)
        return GridAxis{};
    if (input.empty())
        return {};
    Parser parser{input, units, 0, implicit};
    GridAxis axis;
    if (!parser.list(axis) || parser.position != input.size() || axis.tracks.empty())
        return {};
    if (implicit && (axis.repeat_start >= 0 || std::any_of(axis.names.begin(), axis.names.end(),
                                                           [](const auto &names) { return !names.empty(); })))
        return {};
    return axis;
}
std::optional<GridLine> grid_line(std::string_view value) {
    const auto input = tokens(value);
    if (input.size() == 1 && lower(input[0].text) == "auto")
        return GridLine{};
    if (input.empty() || input.size() > 3)
        return {};
    GridLine line;
    for (const auto &token : input) {
        if (lower(token.text) == "span") {
            if (line.span)
                return {};
            line.span = true;
        } else if (custom_ident(token)) {
            if (!line.name.empty())
                return {};
            line.name = token.text;
        } else {
            if (line.number)
                return {};
            auto raw = token.raw;
            if (!raw.empty() && raw[0] == '+')
                raw.erase(0, 1);
            const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), line.number);
            if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size() || line.number == 0 ||
                line.number < -grid_track_limit || line.number > grid_track_limit)
                return {};
        }
    }
    if (line.span && (line.number < 0 || (!line.number && line.name.empty())))
        return {};
    return line;
}
std::optional<std::vector<std::vector<std::string>>> grid_areas(std::string_view value) {
    const auto input = tokens(value);
    if (input.size() == 1 && lower(input[0].text) == "none")
        return std::vector<std::vector<std::string>>{};
    if (input.empty())
        return {};
    std::vector<std::vector<std::string>> rows;
    std::map<std::string, std::array<int, 4>> areas;
    for (const auto &token : input) {
        if (token.type != LXB_CSS_SYNTAX_TOKEN_STRING)
            return {};
        std::vector<std::string> row;
        for (size_t i = 0; i < token.text.size();) {
            if (token.text[i] == ' ' || token.text[i] == '\t' || token.text[i] == '\n' ||
                token.text[i] == '\r' || token.text[i] == '\f') {
                ++i;
                continue;
            }
            const auto begin = i;
            const bool dot = token.text[i] == '.';
            while (i < token.text.size() &&
                   (dot ? token.text[i] == '.'
                        : token.text[i] != '.' && token.text[i] != ' ' && token.text[i] != '\t' &&
                              token.text[i] != '\n' && token.text[i] != '\r' && token.text[i] != '\f'))
                ++i;
            row.push_back(dot ? "" : token.text.substr(begin, i - begin));
        }
        if (row.empty() || row.size() > grid_track_limit || (!rows.empty() && row.size() != rows[0].size()))
            return {};
        for (size_t c = 0; c < row.size(); ++c)
            if (!row[c].empty()) {
                const auto [found, added] = areas.try_emplace(
                    row[c], std::array<int, 4>{int(rows.size()), int(c), int(rows.size()) + 1, int(c) + 1});
                if (!added) {
                    auto &box = found->second;
                    box[1] = std::min(box[1], int(c));
                    box[2] = int(rows.size()) + 1;
                    box[3] = std::max(box[3], int(c) + 1);
                }
            }
        rows.push_back(std::move(row));
        if (rows.size() > grid_track_limit)
            return {};
    }
    for (const auto &[name, area] : areas)
        for (int r = area[0]; r < area[2]; ++r)
            for (int c = area[1]; c < area[3]; ++c)
                if (rows[r][c] != name)
                    return {};
    return rows;
}
std::optional<FlexAlign> grid_alignment(std::string_view value) {
    const auto input = tokens(value);
    if (input.size() != 1)
        return {};
    const auto word = lower(input[0].text);
    if (word == "auto")
        return FlexAlign::automatic;
    if (word == "normal" || word == "stretch")
        return FlexAlign::stretch;
    if (word == "start" || word == "flex-start")
        return FlexAlign::start;
    if (word == "end" || word == "flex-end")
        return FlexAlign::end;
    if (word == "center")
        return FlexAlign::center;
    if (word == "baseline")
        return FlexAlign::baseline;
    return {};
}
std::vector<std::string> grid_components(std::string_view value, char delimiter) {
    const auto input = tokens(value);
    std::vector<std::string> result;
    int depth = 0;
    std::string current;
    for (const auto &token : input) {
        const bool split = !depth && (delimiter ? token.raw == std::string(1, delimiter) : !current.empty());
        if (split) {
            result.push_back(current);
            current.clear();
            if (delimiter)
                continue;
        }
        if (!current.empty())
            current += ' ';
        current += token.raw;
        if (token.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || token.type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET)
            ++depth;
        if (token.type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS || token.type == LXB_CSS_SYNTAX_TOKEN_RS_BRACKET)
            --depth;
    }
    if (!current.empty())
        result.push_back(std::move(current));
    return result;
}
void expand_grid_shorthands(lxb_css_rule_declaration_list_t *list) {
    if (!list)
        return;
    for (auto *rule = list->first; rule;) {
        auto *next = rule->next;
        if (rule->type != LXB_CSS_RULE_DECLARATION ||
            lxb_css_rule_declaration(rule)->type != LXB_CSS_PROPERTY__CUSTOM) {
            rule = next;
            continue;
        }
        auto *declaration = lxb_css_rule_declaration(rule);
        const auto &custom = *declaration->u.custom;
        const auto name = lower({reinterpret_cast<const char *>(custom.name.data), custom.name.length});
        const std::string value(reinterpret_cast<const char *>(custom.value.data), custom.value.length);
        const auto words = tokens(value);
        const bool global =
            words.size() == 1 && (lower(words[0].text) == "inherit" || lower(words[0].text) == "initial" ||
                                  lower(words[0].text) == "unset");
        const auto keyword = global ? lower(words[0].text) : "";
        if (value.find("var(") != std::string::npos || value.find("calc(") != std::string::npos ||
            value.find("subgrid") != std::string::npos || value.find("masonry") != std::string::npos ||
            value.find("revert") != std::string::npos) {
            rule = next;
            continue;
        }
        std::string source;
        bool replace = false;
        const auto add = [&](const char *property, const std::string &content) {
            source += std::string(property) + ':' + content + (declaration->important ? "!important;" : ";");
        };
        const auto named_line = [&](const std::string &text) {
            const auto line = grid_line(text);
            return line && !line->span && !line->name.empty() && !line->number;
        };
        if (name == "grid-column" || name == "grid-row" || name == "grid-area") {
            replace = true;
            const std::array<const char *, 4> properties =
                name == "grid-area" ? std::array<const char *, 4>{"grid-row-start", "grid-column-start",
                                                                  "grid-row-end", "grid-column-end"}
                : name == "grid-column"
                    ? std::array<const char *, 4>{"grid-column-start", "grid-column-end", "", ""}
                    : std::array<const char *, 4>{"grid-row-start", "grid-row-end", "", ""};
            const auto parts = grid_components(value, '/');
            const auto count = name == "grid-area" ? 4u : 2u;
            bool valid = !parts.empty() && parts.size() <= count;
            for (const auto &part : parts)
                if (!global && !grid_line(part))
                    valid = false;
            if (valid) {
                std::array<std::string, 4> values{"auto", "auto", "auto", "auto"};
                if (global)
                    values.fill(keyword);
                else {
                    for (size_t i = 0; i < parts.size(); ++i)
                        values[i] = parts[i];
                    if (count == 2 && parts.size() == 1 && named_line(values[0]))
                        values[1] = values[0];
                    if (count == 4) {
                        if (parts.size() < 2 && named_line(values[0]))
                            values[1] = values[0];
                        if (parts.size() < 3 && named_line(values[0]))
                            values[2] = values[0];
                        if (parts.size() < 4 && named_line(values[1]))
                            values[3] = values[1];
                    }
                }
                for (size_t i = 0; i < count; ++i)
                    add(properties[i], values[i]);
            }
        } else if (name == "grid-template" || name == "grid") {
            replace = true;
            const auto parts = grid_components(value, '/');
            std::string rows = "none", columns = "none", areas = "none", auto_rows = "auto",
                        auto_columns = "auto", flow = "row";
            bool valid = false;
            if (global || (words.size() == 1 && lower(words[0].text) == "none")) {
                valid = true;
                if (global)
                    rows = columns = areas = auto_rows = auto_columns = flow = keyword;
            } else if (parts.size() == 2) {
                const auto row_parts = grid_components(parts[0]);
                if (std::any_of(row_parts.begin(), row_parts.end(), [](const auto &part) {
                        const auto values = tokens(part);
                        return values.size() == 1 && values[0].type == LXB_CSS_SYNTAX_TOKEN_STRING;
                    })) {
                    rows.clear();
                    areas.clear();
                    for (size_t i = 0; i < row_parts.size();) {
                        const auto current = tokens(row_parts[i]);
                        if (current.size() != 1 || current[0].type != LXB_CSS_SYNTAX_TOKEN_STRING) {
                            valid = false;
                            break;
                        }
                        areas += row_parts[i++] + ' ';
                        if (i < row_parts.size() &&
                            tokens(row_parts[i])[0].type != LXB_CSS_SYNTAX_TOKEN_STRING)
                            rows += row_parts[i++] + ' ';
                        else
                            rows += "auto ";
                    }
                    columns = parts[1];
                    valid = bool(grid_tracks(rows)) && bool(grid_tracks(columns)) && bool(grid_areas(areas));
                } else if (name == "grid" && (parts[0].find("auto-flow") != std::string::npos ||
                                              parts[1].find("auto-flow") != std::string::npos)) {
                    const bool column = parts[1].find("auto-flow") != std::string::npos;
                    const auto sequence = grid_components(column ? parts[1] : parts[0]);
                    std::string sizes;
                    flow = column ? "column" : "row";
                    for (const auto &part : sequence) {
                        if (part == "dense")
                            flow += " dense";
                        else if (part != "auto-flow")
                            sizes += part + ' ';
                    }
                    if (sizes.empty())
                        sizes = "auto";
                    if (column) {
                        auto_columns = sizes;
                        rows = parts[0];
                    } else {
                        auto_rows = sizes;
                        columns = parts[1];
                    }
                    valid = bool(grid_tracks(rows)) && bool(grid_tracks(columns)) &&
                            bool(grid_tracks(sizes, {}, true));
                } else {
                    rows = parts[0];
                    columns = parts[1];
                    valid = bool(grid_tracks(rows)) && bool(grid_tracks(columns));
                }
            }
            if (valid) {
                add("grid-template-rows", rows);
                add("grid-template-columns", columns);
                add("grid-template-areas", areas);
                if (name == "grid") {
                    add("grid-auto-rows", auto_rows);
                    add("grid-auto-columns", auto_columns);
                    add("grid-auto-flow", flow);
                }
            }
        } else if (name == "place-items" || name == "place-self" || name == "place-content") {
            const auto parts = grid_components(value);
            replace = true;
            if (!parts.empty() && parts.size() <= 2) {
                const auto *a = name == "place-items"  ? "align-items"
                                : name == "place-self" ? "align-self"
                                                       : "align-content";
                const auto *b = name == "place-items"  ? "justify-items"
                                : name == "place-self" ? "justify-self"
                                                       : "justify-content";
                add(a, parts[0]);
                add(b, parts.back());
            }
        } else {
            bool valid = true, recognized = true;
            if (name == "grid-template-columns" || name == "grid-template-rows")
                valid = global || bool(grid_tracks(value));
            else if (name == "grid-auto-columns" || name == "grid-auto-rows")
                valid = global || bool(grid_tracks(value, {}, true));
            else if (name == "grid-template-areas")
                valid = global || bool(grid_areas(value));
            else if (name == "grid-column-start" || name == "grid-column-end" || name == "grid-row-start" ||
                     name == "grid-row-end")
                valid = global || bool(grid_line(value));
            else if (name == "justify-items" || name == "justify-self")
                valid = global || bool(grid_alignment(value));
            else if (name == "grid-auto-flow") {
                const auto parts = grid_components(value);
                bool direction = false, dense = false;
                valid = global || (!parts.empty() && parts.size() <= 2);
                if (!global)
                    for (const auto &part : parts) {
                        const auto word = lower(part);
                        if (word == "row" || word == "column") {
                            if (direction)
                                valid = false;
                            direction = true;
                        } else if (word == "dense") {
                            if (dense)
                                valid = false;
                            dense = true;
                        } else
                            valid = false;
                    }
            } else
                recognized = false;
            replace = recognized && !valid;
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
