#pragma once
#include "document/dom_internal.h"

namespace xl::detail::generated {
using Type = lxb_css_syntax_token_type_t;
struct Token { Type type; std::string text; size_t end = 0; bool space_before = false; };
using Tokens = std::vector<Token>;
std::optional<Tokens> tokenize(std::string_view value);
std::string lower(std::string value);
std::string keyword(const Tokens& tokens);
bool counter_name(std::string_view name);
bool managed_property(std::string_view name);
bool valid_property(std::string_view name, std::string_view value);
std::string declaration(lxb_dom_node_t* node, std::string_view name);
std::string property(Document::Impl& document, lxb_dom_node_t* node, std::string_view name);
std::string quote_string(std::string_view value);

struct Change { std::string name; int32_t value = 0; bool reversed = false, automatic = false; };
std::optional<std::vector<Change>> changes(std::string_view value, std::string_view property);
struct Quotes {
    bool automatic = true;
    std::vector<std::pair<std::string, std::string>> pairs;
};
std::optional<Quotes> quotes(std::string_view value);
Quotes auto_quotes(Document::Impl& document, lxb_dom_node_t* node);
bool replaced(lxb_dom_node_t* node);

class Counters {
    struct Counter {
        std::string name;
        lxb_dom_node_t* creator;
        int32_t value;
        bool reversed, automatic, stopped = false;
        int64_t total = 0, last = 0;
    };
public:
    using Set = std::vector<Counter*>;
    explicit Counters(Document::Impl& document) : document_(document) {}
    void begin_pass(bool preview);
    void finish_preview();
    void apply(Set& state, lxb_dom_node_t* node, lxb_dom_node_t* parent);
    std::string output(Set& state, lxb_dom_node_t* node, lxb_dom_node_t* parent,
                       std::string_view name, const std::optional<std::string>& separator, std::string_view style);
private:
    Document::Impl& document_;
    bool preview_ = false;
    std::vector<std::unique_ptr<Counter>> owned_;
    std::unordered_map<lxb_dom_node_t*, std::unordered_map<std::string, int32_t>> initial_;
    Counter* instantiate(Set& state, lxb_dom_node_t* node, lxb_dom_node_t* parent, const Change& change);
    Counter* find(Set& state, std::string_view name);
};
}
