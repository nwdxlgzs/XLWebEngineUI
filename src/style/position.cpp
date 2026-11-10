#include "position.h"
#include "document/dom_internal.h"
#include <algorithm>
#include <cmath>

namespace xl::detail::cssposition {
namespace {
struct Part {
  std::string raw, ident;
};
std::optional<std::vector<Part>> parts(std::string_view source) {
  if (source.size() > 65536)
    return {};
  auto *lexer = lxb_css_syntax_tokenizer_create();
  if (!lexer)
    throw std::bad_alloc();
  struct End {
    lxb_css_syntax_tokenizer_t *p;
    ~End() { lxb_css_syntax_tokenizer_destroy(p); }
  } end{lexer};
  check_lxb(lxb_css_syntax_tokenizer_init(lexer));
  lexer->with_comment = true;
  lxb_css_syntax_tokenizer_buffer_set(lexer, chars(source), source.size());
  std::vector<Part> result;
  size_t depth = 0, count = 0;
  for (auto *token = lxb_css_syntax_token(lexer);
       token && token->type != LXB_CSS_SYNTAX_TOKEN__EOF;
       lxb_css_syntax_token_consume(lexer),
            token = lxb_css_syntax_token(lexer)) {
    if (++count > 8192)
      return {};
    const auto type = token->type;
    const bool trivia = type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE ||
                        type == LXB_CSS_SYNTAX_TOKEN_COMMENT;
    if (!depth && trivia)
      continue;
    if (!depth) {
      if (result.size() == 4)
        return {};
      result.push_back({});
      if (type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
        const auto *id = lxb_css_syntax_token_ident(token);
        result.back().ident.assign(reinterpret_cast<const char *>(id->data),
                                   id->length);
        for (auto &c : result.back().ident)
          if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
      }
    }
    const auto *raw = lxb_css_syntax_token_base(token);
    result.back().raw.append(reinterpret_cast<const char *>(raw->begin),
                             raw->length);
    if (type == LXB_CSS_SYNTAX_TOKEN_FUNCTION ||
        type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS) {
      if (++depth > 64)
        return {};
    } else if (type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS) {
      if (!depth)
        return {};
      --depth;
    }
  }
  check_lxb(lexer->status);
  return depth || result.empty() ? std::nullopt
                                 : std::optional(std::move(result));
}
} // namespace
bool property(std::string_view name) {
  return name == "inset" ||
         std::any_of(std::begin(properties), std::end(properties),
                     [&](auto n) { return name == n; });
}
std::shared_ptr<const Declaration> parse(std::string_view name,
                                         std::string_view source) {
  if (!property(name))
    return {};
  const auto tokens = parts(source);
  if (!tokens)
    return {};
  auto result = std::make_shared<Declaration>();
  const auto &first = tokens->front().ident;
  if (tokens->size() == 1 &&
      (first == "inherit" || first == "initial" || first == "unset" ||
       first == "revert" || first == "revert-layer")) {
    result->wide = first;
    return result;
  }
  if (name == "position") {
    if (tokens->size() != 1)
      return {};
    if (first == "static")
      result->style.kind = Kind::normal;
    else if (first == "relative")
      result->style.kind = Kind::relative;
    else if (first == "absolute")
      result->style.kind = Kind::absolute;
    else if (first == "fixed")
      result->style.kind = Kind::fixed;
    else if (first == "sticky")
      result->style.kind = Kind::sticky;
    else
      return {};
    result->values[0] = first;
    return result;
  }
  if(name=="isolation") {
    if(tokens->size()!=1||(first!="auto"&&first!="isolate"))return {};
    result->style.isolate=first=="isolate";result->values[6]=first;return result;
  }
  if(name=="z-index") {
    if(tokens->size()!=1)return {};
    if(first=="auto")return result;
    auto raw=std::string_view(tokens->front().raw);bool negative=false;
    if(!raw.empty()&&(raw.front()=='+'||raw.front()=='-')){negative=raw.front()=='-';raw.remove_prefix(1);}
    if(raw.empty())return {};
    uint64_t value=0;const uint64_t limit=negative?2147483648ull:2147483647ull;
    for(const auto c:raw){if(c<'0'||c>'9')return {};value=std::min(limit,value*10+unsigned(c-'0'));}
    result->style.z=static_cast<int32_t>(negative?-static_cast<int64_t>(value):static_cast<int64_t>(value));
    result->values[5]=std::to_string(*result->style.z);return result;
  }
  if (name != "inset" && tokens->size() != 1)
    return {};
  std::array<std::optional<cssbackground::Length>, 4> lengths;
  std::array<bool, 4> percentages{};
  for (size_t i = 0; i < tokens->size(); ++i) {
    if ((*tokens)[i].ident == "auto")
      continue;
    lengths[i] = cssbackground::parse_length((*tokens)[i].raw, &percentages[i]);
    if (!lengths[i])
      return {};
  }
  for (size_t i = 0; i < 4; ++i) {
    if (name != "inset" && name != properties[i + 1])
      continue;
    const auto source_index = name != "inset"                 ? 0
                              : i < tokens->size()            ? i
                              : i == 3 && tokens->size() >= 2 ? 1
                                                              : 0;
    result->style.inset[i] = lengths[source_index];
    result->style.percentages[i] = percentages[source_index];
    result->values[i + 1] = (*tokens)[source_index].ident == "auto"
                                ? "auto"
                                : (*tokens)[source_index].raw;
  }
  return result;
}
std::pair<float, float> relative(const Style &style, float width, float height,
                                 bool rtl) {
  if (style.kind != Kind::relative)
    return {};
  const auto used = [&](size_t i, float basis) -> std::optional<float> {
    const auto &value = style.inset[i];
    if (!value || (!std::isfinite(basis) && style.percentages[i]))
      return {};
    const auto resolved =
        value->px + (std::isfinite(basis) ? value->percent * basis : 0);
    return static_cast<float>(std::clamp(resolved, -1e20, 1e20));
  };
  const auto top = used(0, height), right = used(1, width),
             bottom = used(2, height), left = used(3, width);
  return {left && (!right || !rtl) ? *left
          : right                  ? -*right
                                   : 0,
          top      ? *top
          : bottom ? -*bottom
                   : 0};
}
} // namespace xl::detail::cssposition
