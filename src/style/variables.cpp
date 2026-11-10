#include "variables.h"
#include "font_shorthand.h"
#include "border_shorthand.h"
#include "grid_style.h"
#include "layout_shorthand.h"
#include "pseudo_elements.h"
#include "generated_content.h"
#include "active_rules.h"
#include "radius_style.h"
#include "nesting.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace xl::detail {
namespace {
constexpr size_t no_index = std::numeric_limits<size_t>::max();
constexpr size_t max_tokens = 8192, max_bytes = 65536, max_depth = 128;
constexpr size_t generation_budget = 8 * 1024 * 1024;
using Type = lxb_css_syntax_token_type_t;
struct Token {
    Type type;
    std::string raw, ident;
    size_t end = no_index, fallback = no_index;
    bool variable = false;
};
using Tokens = std::vector<Token>;
using Value = std::optional<Tokens>;
bool trivia(const Token& token) {
    return token.type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE || token.type == LXB_CSS_SYNTAX_TOKEN_COMMENT;
}
bool custom_name(std::string_view name) { return name.size() > 2 && name.starts_with("--"); }
std::string lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return value;
}
std::string keyword(const Tokens& tokens) {
    const Token* found = nullptr;
    for (const auto& token : tokens) if (!trivia(token)) {
        if (found || token.type != LXB_CSS_SYNTAX_TOKEN_IDENT) return {};
        found = &token;
    }
    return found ? lower(found->ident) : std::string{};
}
struct Stream { Tokens tokens; bool valid = true, variable = false, oversized = false; };
Stream tokenize(std::string_view value) {
    auto* parser = lxb_css_syntax_tokenizer_create();
    if (!parser) throw std::bad_alloc();
    struct Cleanup { lxb_css_syntax_tokenizer_t* p; ~Cleanup() { lxb_css_syntax_tokenizer_destroy(p); } } cleanup{parser};
    check_lxb(lxb_css_syntax_tokenizer_init(parser));
    parser->with_comment = true;
    lxb_css_syntax_tokenizer_buffer_set(parser, chars(value), value.size());
    Stream result;
    std::vector<size_t> blocks;
    const auto close = [&](size_t opening, size_t index) {
        auto& begin = result.tokens[opening];
        const bool matches = result.tokens[index].type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS
            ? begin.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION || begin.type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS
            : result.tokens[index].type == LXB_CSS_SYNTAX_TOKEN_RS_BRACKET
                ? begin.type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET : begin.type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET;
        if (!matches) { result.valid = false; return; }
        begin.end = index;
        if (!begin.variable) return;
        size_t argument = opening + 1;
        while (argument < index && trivia(result.tokens[argument])) ++argument;
        if (argument == index || result.tokens[argument].type != LXB_CSS_SYNTAX_TOKEN_IDENT ||
            !custom_name(result.tokens[argument].ident)) { result.valid = false; return; }
        begin.ident = result.tokens[argument++].ident;
        while (argument < index && trivia(result.tokens[argument])) ++argument;
        if (argument < index) {
            if (result.tokens[argument].type != LXB_CSS_SYNTAX_TOKEN_COMMA) result.valid = false;
            else begin.fallback = argument + 1;
        }
    };
    while (auto* token = lxb_css_syntax_token(parser)) {
        if (token->type == LXB_CSS_SYNTAX_TOKEN__EOF) break;
        const auto* raw = lxb_css_syntax_token_base(token);
        Token next{token->type, {reinterpret_cast<const char*>(raw->begin), raw->length}, {}};
        if (token->type == LXB_CSS_SYNTAX_TOKEN_IDENT || token->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION) {
            const auto* ident = lxb_css_syntax_token_ident(token);
            next.ident.assign(reinterpret_cast<const char*>(ident->data), ident->length);
        }
        next.variable = token->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && lower(next.ident) == "var";
        result.variable |= next.variable;
        const size_t index = result.tokens.size();
        const bool opening = next.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION ||
            next.type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS || next.type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET ||
            next.type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET;
        const bool closing = next.type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS ||
            next.type == LXB_CSS_SYNTAX_TOKEN_RS_BRACKET || next.type == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET;
        if (next.type == LXB_CSS_SYNTAX_TOKEN_BAD_STRING || next.type == LXB_CSS_SYNTAX_TOKEN_BAD_URL ||
            (blocks.empty() && (next.type == LXB_CSS_SYNTAX_TOKEN_SEMICOLON ||
             (next.type == LXB_CSS_SYNTAX_TOKEN_DELIM && next.raw == "!")))) result.valid = false;
        result.tokens.push_back(std::move(next));
        if (result.tokens.size() >= max_tokens || value.size() > max_bytes) {
            result.oversized = true;
            return result;
        }
        if (opening) blocks.push_back(index);
        if (closing) {
            if (blocks.empty()) { result.valid = false; break; }
            close(blocks.back(), index);
            blocks.pop_back();
        }
        lxb_css_syntax_token_consume(parser);
    }
    check_lxb(parser->status);
    // CSS component values implicitly close open blocks/functions at EOF.
    while (result.valid && !blocks.empty()) {
        const auto opening = blocks.back(); blocks.pop_back();
        const auto type = result.tokens[opening].type;
        const auto closing = type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET ? LXB_CSS_SYNTAX_TOKEN_RS_BRACKET :
            type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET ? LXB_CSS_SYNTAX_TOKEN_RC_BRACKET : LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS;
        result.tokens.push_back({closing, closing == LXB_CSS_SYNTAX_TOKEN_RS_BRACKET ? "]" :
            closing == LXB_CSS_SYNTAX_TOKEN_RC_BRACKET ? "}" : ")", {}});
        close(opening, result.tokens.size() - 1);
    }
    return result;
}
std::string serialize(const Tokens& tokens) {
    std::string result;
    for (const auto& token : tokens) result += token.raw;
    return result;
}
size_t storage_cost(const Tokens& tokens) {
    size_t size = tokens.size() * sizeof(Token);
    for (const auto& token : tokens) size += token.raw.size() + token.ident.size();
    return size;
}
bool append(Tokens& output, const Tokens& input, size_t& bytes) {
    // Keep token boundaries across substitution: var(--number)px must not
    // become a dimension token when --number contains just a number.
    if (!input.empty() && !output.empty() && !trivia(output.back()) && !trivia(input.front())) {
        output.push_back({LXB_CSS_SYNTAX_TOKEN_COMMENT, "/**/", {}}); bytes += 4;
    }
    for (const auto& token : input) {
        bytes += token.raw.size();
        if (bytes > max_bytes || output.size() >= max_tokens) return false;
        output.push_back(token);
    }
    return true;
}
Value substitute(const Tokens& input, size_t start, size_t end,
                 const std::function<const Value*(std::string_view)>& lookup, size_t depth = 0) {
    if (depth > max_depth) return {};
    Tokens result;
    size_t bytes = 0;
    bool boundary = false;
    for (size_t index = start; index < end; ++index) {
        const auto& token = input[index];
        if (token.variable) {
            if (token.end == no_index || token.end >= end) return {};
            const Value* value = lookup(token.ident);
            Value fallback;
            if (!value || !*value) {
                if (token.fallback == no_index) return {};
                fallback = substitute(input, token.fallback, token.end, lookup, depth + 1);
                if (!fallback) return {};
                value = &fallback;
            }
            if (!append(result, **value, bytes)) return {};
            boundary = true;
            index = token.end;
        } else {
            if (boundary && !result.empty() && !trivia(result.back()) && !trivia(token)) {
                result.push_back({LXB_CSS_SYNTAX_TOKEN_COMMENT, "/**/", {}}); bytes += 4;
            }
            boundary = false;
            bytes += token.raw.size();
            if (bytes > max_bytes || result.size() >= max_tokens) return {};
            result.push_back(token);
        }
    }
    return result;
}
struct Declaration {
    lxb_css_rule_declaration_t* original;
    std::string name;
    Stream value;
    size_t order;
    std::shared_ptr<const LayerOrder> layer;
    std::string rollback;
    std::shared_ptr<ComputedStyleCache> cache;
};
struct Reference { Declaration* declaration; lxb_css_selector_specificity_t specificity; };
using References = std::vector<Reference>;
struct Scope {
    Scope* parent = nullptr;
    std::unordered_map<std::string, Declaration*> definitions;
    std::unordered_map<std::string, Value> values;
    std::unordered_set<std::string> cyclic;
    const Value* find(std::string_view name) const {
        const std::string key(name);
        for (auto* scope = this; scope; scope = scope->parent) {
            const auto value = scope->values.find(key);
            if (value != scope->values.end()) return &value->second;
        }
        return nullptr;
    }
    void cycles() {
        std::unordered_map<std::string, unsigned> colors;
        std::vector<std::string> stack;
        std::function<void(const std::string&)> visit = [&](const auto& name) {
            if (!definitions.count(name)) return;
            if (colors[name] == 2) return;
            if (colors[name] == 1) {
                const auto first = std::find(stack.begin(), stack.end(), name);
                cyclic.insert(first, stack.end()); return;
            }
            if (stack.size() >= max_depth) { cyclic.insert(name); return; }
            colors[name] = 1; stack.push_back(name);
            // Include references in unused fallbacks, as required for cycles.
            if (!definitions.at(name)->value.oversized)
                for (const auto& token : definitions.at(name)->value.tokens)
                    if (token.variable) visit(token.ident);
            stack.pop_back(); colors[name] = 2;
        };
        for (const auto& [name, declaration] : definitions) { (void)declaration; visit(name); }
    }
    const Value* resolve(std::string_view requested, size_t& budget, size_t depth = 0) {
        const std::string name(requested);
        if (values.count(name)) return &values.at(name);
        const auto declaration = definitions.find(name);
        if (declaration == definitions.end()) return parent ? parent->find(name) : nullptr;
        if (depth > max_depth || cyclic.count(name) || declaration->second->value.oversized)
            return &values.emplace(name, Value{}).first->second;
        const auto& tokens = declaration->second->value.tokens;
        const auto global = keyword(tokens);
        Value computed;
        if (global == "inherit" || global == "unset" || global == "revert" || global == "revert-layer") {
            if (const auto* inherited = parent ? parent->find(name) : nullptr) computed = *inherited;
        } else if (global != "initial") {
            computed = substitute(tokens, 0, tokens.size(), [&](auto reference) {
                return resolve(reference, budget, depth + 1);
            });
        }
        if (computed) {
            const auto bytes = storage_cost(*computed);
            if (bytes > budget) computed.reset(); else budget -= bytes;
        }
        return &values.emplace(name, std::move(computed)).first->second;
    }
};
}

// Owned by an immutable compiled sheet. Each key remains tied to its authored
// declaration, so sharing never changes source-order metadata between rules.
class ComputedStyleCache {
public:
    lxb_css_memory_t* memory=nullptr;
    lxb_css_parser_t* parser=nullptr;
    std::unordered_map<const lxb_css_rule_declaration_t*,std::unordered_map<std::string,lxb_css_rule_declaration_list_t*>> values;
    std::unordered_map<const lxb_css_rule_declaration_t*,bool> constants;
    std::unordered_map<const lxb_css_rule_declaration_t*,std::unordered_map<std::string,std::shared_ptr<const ColorDeclaration>>> colors;
    std::unordered_map<const lxb_css_rule_declaration_t*,std::unordered_map<std::string,std::shared_ptr<const cssbackground::Declaration>>> backgrounds;
    std::unordered_map<const lxb_css_rule_declaration_t*,std::unordered_map<std::string,std::shared_ptr<const cssposition::Declaration>>> positions;
    size_t entries=0,bytes=0;
    bool full() const{return entries>=2048||bytes>=2*1024*1024;}
    ComputedStyleCache() {
        memory=lxb_css_memory_create();if(!memory)throw std::bad_alloc();
        try{check_lxb(lxb_css_memory_init(memory,64));parser=lxb_css_parser_create();if(!parser)throw std::bad_alloc();check_lxb(lxb_css_parser_init(parser,nullptr));parser->memory=memory;}
        catch(...){lxb_css_parser_destroy(parser,true);lxb_css_memory_destroy(memory,true);throw;}
    }
    ~ComputedStyleCache(){lxb_css_parser_destroy(parser,true);lxb_css_memory_destroy(memory,true);}
};

struct VariableStyles::Impl {
    lxb_css_memory_t* memory = nullptr;
    lxb_css_parser_t* parser = nullptr;
    std::unordered_map<lxb_dom_element_t*, Scope> scopes;
    std::unordered_map<lxb_dom_element_t*,std::unordered_map<std::string,std::shared_ptr<const ColorDeclaration>>> colors;
    std::unordered_map<const Declaration*,std::unordered_map<std::string,std::shared_ptr<const ColorDeclaration>>> color_values;
    std::unordered_map<lxb_dom_element_t*,std::unordered_map<std::string,std::shared_ptr<const cssbackground::Declaration>>> backgrounds;
    std::unordered_map<const Declaration*,std::unordered_map<std::string,std::shared_ptr<const cssbackground::Declaration>>> background_values;
    std::unordered_map<lxb_dom_element_t*,std::unordered_map<std::string,std::shared_ptr<const cssposition::Declaration>>> positions;
    std::unordered_map<const Declaration*,std::unordered_map<std::string,std::shared_ptr<const cssposition::Declaration>>> position_values;
    std::vector<std::unique_ptr<Declaration>> declarations;
    struct Rule { lxb_css_rule_style_t* original; References declarations; };
    std::vector<Rule> rules;
    std::unordered_map<lxb_dom_element_t*, References> references;
    std::unordered_map<const Declaration*,std::unordered_map<std::string,lxb_css_rule_declaration_list_t*>> parsed_values;
    std::unordered_map<lxb_css_rule_declaration_t*,std::vector<lxb_css_rule_declaration_t*>> expanded_boxes;
    std::unordered_map<const Declaration*,std::unordered_map<std::string,bool>> validated_values;
    struct Substitution {bool valid=false;std::string text,rollback;};
    std::unordered_map<const Declaration*,std::unordered_map<const Scope*,Substitution>> substitutions;
    bool enabled = false, layered = false;
    std::unordered_set<std::string> property_names;
    std::string_view property_name(std::string_view name) {return *property_names.emplace(name).first;}
    size_t budget = generation_budget, sequence = 0;
    Impl() {
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
    ~Impl() { lxb_css_parser_destroy(parser, true); lxb_css_memory_destroy(memory, true); }
    bool valid(const std::string& name,std::string_view text) const {
        if(name=="color-scheme")return csscolor::valid_scheme(text);
        if(cssbackground::property(name))return bool(cssbackground::parse(name,text));
        if(cssposition::property(name))return bool(cssposition::parse(name,text));
        if(csscolor::property(name))return csscolor::valid(text);
        if(name=="content")return PseudoElements::valid_content(text);
        if(generated::managed_property(name))return generated::valid_property(name,text);
        if(radius_property(name))return valid_radius(text,name=="border-radius");
        return true;
    }
    bool valid(const Declaration& declaration,const std::string& text) {
        const auto& name=declaration.name;
        if(name!="content"&&!generated::managed_property(name)&&!radius_property(name)&&!csscolor::property(name)&&!cssbackground::property(name)&&!cssposition::property(name)&&name!="color-scheme")return true;
        auto& values=validated_values[&declaration];
        const auto found=values.find(text);if(found!=values.end())return found->second;
        const auto result=valid(name,text);values.emplace(text,result);return result;
    }
    References read(lxb_css_rule_declaration_list_t* list, std::string_view authored,
                    lxb_css_selector_specificity_t specificity = 0, std::shared_ptr<const LayerOrder> layer = {},
                    std::shared_ptr<ComputedStyleCache> cache = {}) {
        References result;
        if (!list) return result;
        for (auto* rule = list->first; rule; rule = rule->next) {
            if (rule->type != LXB_CSS_RULE_DECLARATION) continue;
            auto* original = lxb_css_rule_declaration(rule);
            std::string name, value;
            if (original->type == LXB_CSS_PROPERTY__CUSTOM) {
                const auto& custom = *original->u.custom;
                name.assign(reinterpret_cast<const char*>(custom.name.data), custom.name.length);
                value.assign(reinterpret_cast<const char*>(custom.value.data), custom.value.length);
            } else {
                const auto id = original->type == LXB_CSS_PROPERTY__UNDEF ? original->u.undef->type : original->type;
                const auto* entry = lxb_css_property_by_id(id);
                if (!entry) continue;
                name.assign(reinterpret_cast<const char*>(entry->name), entry->length);
                if (original->type == LXB_CSS_PROPERTY__UNDEF)
                    value.assign(reinterpret_cast<const char*>(original->u.undef->value.data), original->u.undef->value.length);
            }
            const auto& offset = original->offset;
            if (offset.value_begin && offset.value_begin <= offset.value_end && offset.value_end <= authored.size())
                value = std::string(authored.substr(offset.value_begin, offset.value_end - offset.value_begin));
            if (!custom_name(name)) name = lower(std::move(name));
            if(value.empty()&&original->type!=LXB_CSS_PROPERTY__CUSTOM&&original->type!=LXB_CSS_PROPERTY__UNDEF)
                check_lxb(lxb_css_property_serialize(original->u.user,static_cast<lxb_css_property_type_t>(original->type),
                    [](const lxb_char_t* bytes,size_t count,void* opaque)->lxb_status_t{try{static_cast<std::string*>(opaque)->append(reinterpret_cast<const char*>(bytes),count);return LXB_STATUS_OK;}catch(...){return LXB_STATUS_ERROR_MEMORY_ALLOCATION;}},&value));
            // Successfully typed declarations cannot contain pending var().
            // Reuse those AST nodes without building duplicate token buffers.
            Stream tokens;
            const bool color=csscolor::property(name)||cssbackground::property(name)||cssposition::property(name),scheme=name=="color-scheme";
            if (original->type == LXB_CSS_PROPERTY__CUSTOM || original->type == LXB_CSS_PROPERTY__UNDEF || name == "content" || color || scheme || lower(value).find("revert")!=std::string::npos || value.find('\\')!=std::string::npos)
                tokens = tokenize(value);
            auto rollback=keyword(tokens.tokens);if(rollback!="revert"&&rollback!="revert-layer")rollback.clear();
            layered|=!rollback.empty();enabled|=!rollback.empty();
            layered|=color||scheme;enabled|=color||scheme;
            const bool color_shorthand=name=="border"||name=="border-color"||name=="border-top"||name=="border-right"||name=="border-bottom"||name=="border-left"||name=="outline";
            layered|=color_shorthand;enabled|=color_shorthand;
            enabled |= custom_name(name) || tokens.variable || name == "background" || generated::managed_property(name) || radius_property(name);
            if (!tokens.valid || name == "--") continue;
            if(!tokens.variable&&(name=="content"||generated::managed_property(name)||radius_property(name)||color||scheme)) {
                bool accepted;
                if(cache) {
                    const auto found=cache->constants.find(original);
                    if(found!=cache->constants.end())accepted=found->second;
                    else {accepted=valid(name,serialize(tokens.tokens));cache->constants.emplace(original,accepted);}
                } else accepted=valid(name,serialize(tokens.tokens));
                if(!accepted)continue;
            }
            auto declaration = std::make_unique<Declaration>(Declaration{original, std::move(name), std::move(tokens), sequence++,layer,std::move(rollback),cache});
            result.push_back({declaration.get(), specificity}); declarations.push_back(std::move(declaration));
        }
        return result;
    }
    lxb_css_rule_declaration_list_t* parse(const Declaration& declaration, std::string_view value) {
        if(declaration.cache) {
            const auto found=declaration.cache->values.find(declaration.original);
            if(found!=declaration.cache->values.end())if(const auto value_found=found->second.find(std::string(value));value_found!=found->second.end())return value_found->second;
        }
        auto& values=parsed_values[&declaration];if(const auto found=values.find(std::string(value));found!=values.end())return found->second;
        const bool persistent=declaration.cache&&!declaration.cache->full()&&value.size()<=2*1024*1024-declaration.cache->bytes;
        auto* parse_with=persistent?declaration.cache->parser:parser;
        // Background colors use this serializer; the complete background
        // shorthand is parsed and expanded by the private background adapter.
        const auto name = declaration.name == "background" ? "background-color" : declaration.name;
        const auto source = name + ':' + std::string(value) + (declaration.original->important ? "!important;" : ";");
        auto* result = lxb_css_declaration_list_parse(parse_with, chars(source), source.size());
        if (!result) { check_lxb(parse_with->status); throw std::bad_alloc(); }
        expand_font_shorthands(result, source);
        expand_border_shorthands(result, source);
        expand_outline_shorthands(result, source);
        expand_radius_shorthands(result,source);
        expand_grid_shorthands(result);
        expand_layout_shorthands(result);
        if(accepted(declaration,result)) {
            if(persistent){declaration.cache->values[declaration.original].emplace(value,result);++declaration.cache->entries;declaration.cache->bytes+=value.size();}
            else values.emplace(value,result);
        }
        return result;
    }
    bool accepted(const Declaration& declaration, lxb_css_rule_declaration_list_t* list) {
        if (!list || !list->count) return false;
        static const std::unordered_set<std::string> shorthands{
            "font", "border", "border-width", "border-style", "border-color", "border-top", "border-right", "border-bottom", "border-left",
            "outline", "flex", "flex-flow", "gap", "overflow", "grid", "grid-template", "grid-row", "grid-column", "grid-area",
            "place-items", "place-self", "place-content", "border-radius"};
        for (auto* rule = list->first; rule; rule = rule->next) {
            if (rule->type != LXB_CSS_RULE_DECLARATION) return false;
            auto* item = lxb_css_rule_declaration(rule);
            if (item->type == LXB_CSS_PROPERTY__UNDEF) {
                const auto* entry=lxb_css_property_by_id(item->u.undef->type);
                if(!entry||!csscolor::property({reinterpret_cast<const char*>(entry->name),entry->length})||
                    !csscolor::valid({reinterpret_cast<const char*>(item->u.undef->value.data),item->u.undef->value.length}))return false;
            }
            if (item->type == LXB_CSS_PROPERTY__CUSTOM && shorthands.count(declaration.name)) {
                const auto& name = item->u.custom->name;
                if (std::string_view(reinterpret_cast<const char*>(name.data), name.length) == declaration.name) return false;
            }
        }
        return true;
    }
    std::shared_ptr<const ColorDeclaration> compile_color(const Declaration& declaration,const std::string& text) {
        if(declaration.cache) {
            const auto found=declaration.cache->colors.find(declaration.original);
            if(found!=declaration.cache->colors.end())if(const auto item=found->second.find(text);item!=found->second.end())return item->second;
        }
        auto& local=color_values[&declaration];if(const auto found=local.find(text);found!=local.end())return found->second;
        auto result=std::make_shared<ColorDeclaration>();result->value=text;result->expression=csscolor::parse(text);
        if(!result->expression)result->value=keyword(tokenize(text).tokens);
        // Preserve the existing declaration-serialization query contract when
        // Lexbor understands the spelling. New color forms keep their tokens.
        auto* list=parse(declaration,text);
        if(list&&list->count==1&&list->first->type==LXB_CSS_RULE_DECLARATION) {
            const auto* item=lxb_css_rule_declaration(list->first);
            if(item->type!=LXB_CSS_PROPERTY__UNDEF&&item->type!=LXB_CSS_PROPERTY__CUSTOM) {
                result->value.clear();
                check_lxb(lxb_css_property_serialize(item->u.user,static_cast<lxb_css_property_type_t>(item->type),
                    [](const lxb_char_t* data,size_t size,void* context)->lxb_status_t{try{static_cast<std::string*>(context)->append(reinterpret_cast<const char*>(data),size);return LXB_STATUS_OK;}catch(...){return LXB_STATUS_ERROR_MEMORY_ALLOCATION;}},&result->value));
            }
        }
        if(declaration.cache&&!declaration.cache->full()&&text.size()<=2*1024*1024-declaration.cache->bytes) {
            declaration.cache->colors[declaration.original].emplace(text,result);++declaration.cache->entries;declaration.cache->bytes+=text.size();
        } else local.emplace(text,result);
        return result;
    }
    std::shared_ptr<const cssbackground::Declaration> compile_background(const Declaration& declaration,const std::string& text) {
        if(declaration.cache)if(const auto found=declaration.cache->backgrounds.find(declaration.original);found!=declaration.cache->backgrounds.end())
            if(const auto item=found->second.find(text);item!=found->second.end())return item->second;
        auto& local=background_values[&declaration];if(const auto found=local.find(text);found!=local.end())return found->second;
        auto result=cssbackground::parse(declaration.name,text);if(!result)throw DocumentFailure{XL_INTERNAL_ERROR};
        if(declaration.cache&&!declaration.cache->full()&&text.size()<=2*1024*1024-declaration.cache->bytes) {
            declaration.cache->backgrounds[declaration.original].emplace(text,result);++declaration.cache->entries;declaration.cache->bytes+=text.size();
        }else local.emplace(text,result);
        return result;
    }
    std::shared_ptr<const cssposition::Declaration> compile_position(const Declaration& declaration,const std::string& text) {
        if(declaration.cache)if(const auto found=declaration.cache->positions.find(declaration.original);found!=declaration.cache->positions.end())
            if(const auto item=found->second.find(text);item!=found->second.end())return item->second;
        auto& local=position_values[&declaration];if(const auto found=local.find(text);found!=local.end())return found->second;
        auto result=cssposition::parse(declaration.name,text);if(!result)throw DocumentFailure{XL_INTERNAL_ERROR};
        if(declaration.cache&&!declaration.cache->full()&&text.size()<=2*1024*1024-declaration.cache->bytes) {
            declaration.cache->positions[declaration.original].emplace(text,result);++declaration.cache->entries;declaration.cache->bytes+=text.size();
        }else local.emplace(text,result);
        return result;
    }
    void apply_element(Document::Impl& document, lxb_dom_element_t* element, Scope* parent) {
        auto& scope = scopes[element]; scope.parent = parent;
        std::unordered_map<std::string, std::pair<lxb_css_selector_specificity_t, size_t>> winners;
        auto& found = references[element];
        std::vector<bool> attached;attached.reserve(found.size());
        static const LayerOrder unlayered{UINT32_MAX};
        if(layered) {
            const auto order=[](const Reference& a,const Reference& b) {
                const bool important=a.declaration->original->important;
                if(important!=b.declaration->original->important)return !important;
                const auto attached_a=lxb_css_selector_sp_s(a.specificity),attached_b=lxb_css_selector_sp_s(b.specificity);
                if(attached_a!=attached_b)return attached_a<attached_b;
                const auto& al=a.declaration->layer?*a.declaration->layer:unlayered;
                const auto& bl=b.declaration->layer?*b.declaration->layer:unlayered;
                if(al!=bl)return important?al>bl:al<bl;
                if(a.specificity!=b.specificity)return a.specificity<b.specificity;
                return a.declaration->order<b.declaration->order;
            };
            std::stable_sort(found.begin(),found.end(),order);
            if(found.size()>=1u<<27)throw DocumentFailure{XL_UNSUPPORTED};
            for(size_t i=0;i<found.size();++i){attached.push_back(lxb_css_selector_sp_s(found[i].specificity)!=0);found[i].specificity=static_cast<lxb_css_selector_specificity_t>(i+1);}
        }
        // Reversion removes the current layer and the interval between its
        // normal/important positions. Inline rollback only removes inline
        // declarations, preserving stylesheet important declarations.
        const auto rolled_back=[&](const Reference& a,const Reference& b) -> bool {
            const auto ai=static_cast<size_t>(&a-found.data()),bi=static_cast<size_t>(&b-found.data());
            const auto& al=a.declaration->layer?*a.declaration->layer:unlayered;const auto& bl=b.declaration->layer?*b.declaration->layer:unlayered;
            if(attached[bi])return bool(attached[ai]);
            return !attached[ai]&&al>=bl;
        };
        std::unordered_set<std::string_view> custom_done;std::unordered_map<std::string_view,std::vector<const Reference*>> custom_blocked;
        if(layered)for(auto it=found.rbegin();it!=found.rend();++it) {
            const auto& declaration=*it->declaration;if(!custom_name(declaration.name)||custom_done.contains(declaration.name))continue;
            const auto blocks=custom_blocked.find(declaration.name);if(blocks!=custom_blocked.end()&&std::any_of(blocks->second.begin(),blocks->second.end(),[&](const auto* item){return rolled_back(*it,*item);}))continue;
            if(declaration.rollback=="revert-layer"){custom_blocked[declaration.name].push_back(&*it);continue;}
            custom_done.insert(declaration.name);if(declaration.rollback.empty())scope.definitions[declaration.name]=it->declaration;
        }
        for (const auto& reference : found) {
            auto& declaration = *reference.declaration;
            if (!custom_name(declaration.name)) continue;
            if(layered)continue;
            auto specificity = reference.specificity;
            lxb_css_selector_sp_set_i(specificity, declaration.original->important);
            const auto priority = std::pair{specificity, declaration.order};
            if (!winners.count(declaration.name) || priority >= winners.at(declaration.name)) {
                winners[declaration.name] = priority; scope.definitions[declaration.name] = &declaration;
            }
        }
        scope.cycles();
        for (const auto& [name, declaration] : scope.definitions) { (void)declaration; scope.resolve(name, budget); }
        struct Candidate {lxb_css_rule_declaration_t* item;std::string_view name,rollback;const Reference* reference;size_t position;std::shared_ptr<const ColorDeclaration> color={};std::shared_ptr<const cssbackground::Declaration> background={};std::shared_ptr<const cssposition::Declaration> positioning={};};
        std::vector<Candidate> candidates;
        candidates.reserve(found.size()*2);
        const auto names=[](std::string_view name) {
            std::vector<std::string> result;
            if(name=="border"||name=="border-color"||name=="border-width"||name=="border-style") {
                for(const auto* side:{"top","right","bottom","left"})for(const auto* kind:{"color","width","style"})if(name=="border"||name==std::string("border-")+kind)result.push_back(std::string("border-")+side+'-'+kind);
            } else if(name=="border-top"||name=="border-right"||name=="border-bottom"||name=="border-left")for(const auto* kind:{"color","width","style"})result.push_back(std::string(name)+'-'+kind);
            else if(name=="margin"||name=="padding")for(const auto* side:{"top","right","bottom","left"})result.push_back(std::string(name)+'-'+side);
            else if(name=="background"){result.push_back("background-color");for(const auto* field:cssbackground::properties)result.emplace_back(field);}
            else if(name=="inset")result={"top","right","bottom","left"};
            else if(name=="border-radius")for(const auto* corner:radius_names)result.emplace_back(corner);
            else if(name=="font")result={"font-style","font-weight","font-stretch","font-size","line-height","font-family","font-variant","font-size-adjust","font-kerning","font-optical-sizing","font-feature-settings","font-variation-settings","font-variant-ligatures","font-variant-caps","font-variant-numeric","font-variant-east-asian","font-variant-position","font-language-override","font-synthesis","font-variant-alternates"};
            else if(name=="outline")result={"outline-width","outline-style","outline-color"};
            else if(name=="flex")result={"flex-grow","flex-shrink","flex-basis"};
            else if(name=="flex-flow")result={"flex-direction","flex-wrap"};
            else if(name=="gap")result={"row-gap","column-gap"};
            else if(name=="overflow")result={"overflow-x","overflow-y"};
            else if(name=="grid"||name=="grid-template") {result={"grid-template-rows","grid-template-columns","grid-template-areas"};if(name=="grid")result.insert(result.end(),{"grid-auto-rows","grid-auto-columns","grid-auto-flow"});}
            else if(name=="grid-row"||name=="grid-column")result={std::string(name)+"-start",std::string(name)+"-end"};
            else if(name=="grid-area")result={"grid-row-start","grid-column-start","grid-row-end","grid-column-end"};
            else if(name=="place-items")result={"align-items","justify-items"};
            else if(name=="place-content")result={"align-content","justify-content"};
            else if(name=="place-self")result={"align-self","justify-self"};
            else result.emplace_back(name);
            return result;
        };
        const auto append=[&](lxb_css_rule_declaration_t* item,const Reference& reference,size_t position) {
            std::string_view name;
            if(item->type==LXB_CSS_PROPERTY__CUSTOM)name={reinterpret_cast<const char*>(item->u.custom->name.data),item->u.custom->name.length};
            else {const auto* entry=lxb_css_property_by_id(item->type==LXB_CSS_PROPERTY__UNDEF?item->u.undef->type:item->type);if(!entry)return;name={reinterpret_cast<const char*>(entry->name),entry->length};}
            if(csscolor::property(name)) {
                std::string value;
                if(item->type==LXB_CSS_PROPERTY__UNDEF)value.assign(reinterpret_cast<const char*>(item->u.undef->value.data),item->u.undef->value.length);
                else if(item->type==LXB_CSS_PROPERTY__CUSTOM)value.assign(reinterpret_cast<const char*>(item->u.custom->value.data),item->u.custom->value.length);
                else check_lxb(lxb_css_property_serialize(item->u.user,static_cast<lxb_css_property_type_t>(item->type),
                    [](const lxb_char_t* data,size_t size,void* context)->lxb_status_t{try{static_cast<std::string*>(context)->append(reinterpret_cast<const char*>(data),size);return LXB_STATUS_OK;}catch(...){return LXB_STATUS_ERROR_MEMORY_ALLOCATION;}},&value));
                candidates.push_back({item,name,"",&reference,position,compile_color(*reference.declaration,value)});return;
            }
            if(!layered){document.style_order[&item->rule]={reference.declaration->order,position};check_lxb(lxb_dom_element_style_append(element,item,reference.specificity));return;}
            if(item->type==LXB_CSS_PROPERTY_MARGIN||item->type==LXB_CSS_PROPERTY_PADDING) {
                if(const auto found=expanded_boxes.find(item);found!=expanded_boxes.end()) {
                    const auto longhands=names(name);for(size_t i=0;i<found->second.size();++i)candidates.push_back({found->second[i],property_name(longhands[i]),"",&reference,position*4+i});return;
                }
                const bool margin=item->type==LXB_CSS_PROPERTY_MARGIN;
                const lxb_css_value_length_percentage_t* values[4];
                if(margin){values[0]=&item->u.margin->top;values[1]=&item->u.margin->right;values[2]=&item->u.margin->bottom;values[3]=&item->u.margin->left;}
                else {values[0]=&item->u.padding->top;values[1]=&item->u.padding->right;values[2]=&item->u.padding->bottom;values[3]=&item->u.padding->left;}
                if(values[1]->type==LXB_CSS_VALUE__UNDEF)values[1]=values[0];
                if(values[2]->type==LXB_CSS_VALUE__UNDEF)values[2]=values[0];
                if(values[3]->type==LXB_CSS_VALUE__UNDEF)values[3]=values[1];
                const auto longhands=names(name);
                std::vector<lxb_css_rule_declaration_t*> expanded;
                for(size_t i=0;i<4;++i) {
                    std::string value;const auto type=static_cast<lxb_css_property_type_t>(margin?LXB_CSS_PROPERTY_MARGIN_TOP:LXB_CSS_PROPERTY_PADDING_TOP);
                    check_lxb(lxb_css_property_serialize(values[i],type,[](const lxb_char_t* bytes,size_t count,void* opaque)->lxb_status_t{try{static_cast<std::string*>(opaque)->append(reinterpret_cast<const char*>(bytes),count);return LXB_STATUS_OK;}catch(...){return LXB_STATUS_ERROR_MEMORY_ALLOCATION;}},&value));
                    const auto source=longhands[i]+':'+value+(item->important?"!important;":";");auto* list=lxb_css_declaration_list_parse(parser,chars(source),source.size());if(!list)throw std::bad_alloc();
                    if(!list->first)throw DocumentFailure{XL_INTERNAL_ERROR};auto* scalar=lxb_css_rule_declaration(list->first);
                    expanded.push_back(scalar);candidates.push_back({scalar,property_name(longhands[i]),"",&reference,position*4+i});
                }
                expanded_boxes.emplace(item,std::move(expanded));
            } else candidates.push_back({item,std::move(name),"",&reference,position});
        };
        for (const auto& reference : found) {
            const auto& declaration = *reference.declaration;
            if (custom_name(declaration.name)) continue;
            if(layered&&!declaration.rollback.empty()) {
                for(auto& name:names(declaration.name))candidates.push_back({declaration.original,property_name(name),declaration.rollback,&reference,0});
                continue;
            }
            const bool color=csscolor::property(declaration.name),background=cssbackground::property(declaration.name),positioning=cssposition::property(declaration.name);
            if (!declaration.value.variable && !color && !background && !positioning) {
                if (declaration.original->type == LXB_CSS_PROPERTY__UNDEF) continue;
                append(declaration.original,reference,0);
                continue;
            }
            // Nodes without local custom properties see the identical inherited
            // binding environment. Resolve each declaration once for that
            // environment; still charge every use against the generation limit.
            const Scope* bindings=&scope;
            while(bindings->definitions.empty()&&bindings->parent)bindings=bindings->parent;
            if(!declaration.value.variable)bindings=nullptr;
            auto& cached=substitutions[&declaration];
            auto found_value=cached.find(bindings);
            if(found_value==cached.end()) {
                Value substituted;
                if(!declaration.value.oversized)substituted=substitute(declaration.value.tokens,0,
                    declaration.value.tokens.size(),[&](auto name){return scope.find(name);});
                Substitution next;next.valid=bool(substituted);
                if(substituted){next.text=serialize(*substituted);next.rollback=keyword(*substituted);next.valid=valid(declaration,next.text);}
                found_value=cached.emplace(bindings,std::move(next)).first;
            }
            const auto& computed=found_value->second;const auto& text=computed.text;
            bool substituted=computed.valid;
            if(text.size()>budget)substituted=false;else budget-=text.size();
            if(layered&&substituted) {
                const auto& rollback=computed.rollback;
                if(rollback=="revert"||rollback=="revert-layer") {for(auto& name:names(declaration.name))candidates.push_back({declaration.original,property_name(name),rollback,&reference,0});continue;}
            }
            if(positioning) {
                const auto parsed=compile_position(declaration,substituted?text:"unset");
                for(const auto& name:names(declaration.name))candidates.push_back({declaration.original,property_name(name),"",&reference,0,{},{},parsed});
                continue;
            }
            if(background) {
                const auto parsed=compile_background(declaration,substituted?text:"unset");
                if(declaration.name=="background")candidates.push_back({declaration.original,"background-color","",&reference,0,compile_color(declaration,parsed->wide.empty()?parsed->color:parsed->wide)});
                for(const auto* name:cssbackground::properties)if(declaration.name=="background"||declaration.name==name)
                    candidates.push_back({declaration.original,name,"",&reference,0,{},parsed});
                continue;
            }
            if(color) {
                candidates.push_back({declaration.original,declaration.name,"",&reference,0,compile_color(declaration,substituted?text:"unset")});
                continue;
            }
            auto* parsed = parse(declaration, substituted ? text : "unset");
            if (!accepted(declaration, parsed)) {
                lxb_css_rule_declaration_list_destroy(parsed, true);
                if (!declaration.value.variable) continue;
                parsed = parse(declaration, "unset");
            }
            size_t position = 0;
            for (auto* rule = parsed->first; rule; rule = rule->next) {
                if (rule->type != LXB_CSS_RULE_DECLARATION) continue;
                auto* item = lxb_css_rule_declaration(rule);
                append(item,reference,position++);
            }
        }
        if(layered) {
            std::unordered_set<std::string_view> done;std::unordered_map<std::string_view,std::vector<const Reference*>> blocked;
            done.reserve(candidates.size());
            for(auto it=candidates.rbegin();it!=candidates.rend();++it) {
                if(done.contains(it->name))continue;const auto blocks=blocked.find(it->name);
                if(blocks!=blocked.end()&&std::any_of(blocks->second.begin(),blocks->second.end(),[&](const auto* item){return rolled_back(*it->reference,*item);}))continue;
                if(it->rollback=="revert-layer"){blocked[it->name].push_back(it->reference);continue;}
                done.insert(it->name);if(!it->rollback.empty())continue;
                if(it->color){colors[element][std::string(it->name)]=it->color;continue;}
                if(it->background){backgrounds[element][std::string(it->name)]=it->background;continue;}
                if(it->positioning){positions[element][std::string(it->name)]=it->positioning;continue;}
                document.style_order[&it->item->rule]={it->reference->declaration->order,it->position};
                check_lxb(lxb_dom_element_style_append(element,it->item,it->reference->specificity));
            }
        }
    }
};

VariableStyles::VariableStyles() : impl_(std::make_unique<Impl>()) {}
VariableStyles::~VariableStyles() = default;
bool VariableStyles::apply(Document::Impl& document, const std::vector<lxb_dom_element_t*>& elements, const ActiveStyles& active) {
    auto& data = *impl_;
    data.enabled=active.manual;data.layered=active.layers;
    std::unordered_set<NestedSheet*> prepared;
    for(const auto& rule:active.rules) {
        if(rule.compiled&&prepared.insert(rule.compiled.get()).second) {
            if(!rule.compiled->values||rule.compiled->values->full())rule.compiled->values=std::make_shared<ComputedStyleCache>();
        }
        data.enabled|=PseudoElements::supported_selector(rule.rule->selector);
        data.rules.push_back({rule.rule,data.read(rule.rule->declarations,*rule.source,0,rule.layer,rule.compiled?rule.compiled->values:nullptr)});
    }
    for (auto* element : elements) {
        if (element->node.local_name == LXB_TAG_Q) {
            document.pseudos->ensure(&element->node,PseudoKind::before);
            document.pseudos->ensure(&element->node,PseudoKind::after);
            data.enabled = true;
        }
        if (element->list) data.references[element] = data.read(static_cast<lxb_css_rule_declaration_list_t*>(element->list),
            document.attribute(document.identify(lxb_dom_interface_node(element)), "style", false), lxb_css_selector_sp_up_s(0));
    }
    if (!data.enabled) return false;
    const SelectorCandidates candidates(document,elements);
    for (auto& rule : data.rules) {
        PseudoElements::match(document, rule.original->selector, [&](lxb_dom_node_t* node, PseudoKind kind, auto specificity) {
            auto* target = kind == PseudoKind::none ? node : document.pseudos->ensure(node, kind);
            auto& found = data.references[element(target)];
            for (const auto& declaration : rule.declarations) found.push_back({declaration.declaration, specificity});
        },&candidates);
    }
    for (auto* element : elements) {
        const auto* node = lxb_dom_interface_node(element);
        data.apply_element(document, element, node->parent && node->parent->type == LXB_DOM_NODE_TYPE_ELEMENT ?
            &data.scopes[lxb_dom_interface_element(node->parent)] : nullptr);
    }
    for (auto* target : document.pseudos->elements())
        data.apply_element(document, target, &data.scopes[element(document.pseudos->origin(lxb_dom_interface_node(target)))]);
    return true;
}
std::optional<std::string> VariableStyles::custom(lxb_dom_element_t* element, std::string_view name) const {
    const auto scope = impl_->scopes.find(element);
    if (scope == impl_->scopes.end()) return {};
    const auto* value = scope->second.find(name);
    if (!value) return {};
    return *value ? serialize(**value) : std::string{};
}
const ColorDeclaration* VariableStyles::color(lxb_dom_element_t* element,std::string_view name) const {
    const auto found=impl_->colors.find(element);if(found==impl_->colors.end())return nullptr;
    const auto value=found->second.find(std::string(name));return value==found->second.end()?nullptr:value->second.get();
}
std::shared_ptr<const cssbackground::Declaration> VariableStyles::background(lxb_dom_element_t* element,std::string_view name) const {
    const auto found=impl_->backgrounds.find(element);if(found==impl_->backgrounds.end())return {};
    const auto item=found->second.find(std::string(name));return item==found->second.end()?nullptr:item->second;
}
std::shared_ptr<const cssposition::Declaration> VariableStyles::position(lxb_dom_element_t* element,std::string_view name) const {
    const auto found=impl_->positions.find(element);if(found==impl_->positions.end())return {};
    const auto item=found->second.find(std::string(name));return item==found->second.end()?nullptr:item->second;
}

}
