#include "active_rules.h"
#include "generated_content.h"
#include "media_query.h"
#include "nesting.h"
#include <limits>

namespace xl::detail {
namespace {
struct Layer {
    LayerOrder path;
    std::unordered_map<std::string, Layer *> names;
    std::vector<std::unique_ptr<Layer>> children;
};
std::optional<std::vector<std::vector<std::string>>> layer_names(std::string_view value) {
    const auto tokens = generated::tokenize(value);
    if (!tokens)
        return {};
    std::vector<std::vector<std::string>> names;
    if (tokens->empty())
        return names;
    size_t i = 0;
    while (i < tokens->size()) {
        std::vector<std::string> path;
        while (true) {
            if (i == tokens->size() || (*tokens)[i].type != LXB_CSS_SYNTAX_TOKEN_IDENT)
                return {};
            const auto name = (*tokens)[i++].text;
            const auto key = generated::lower(name);
            if (key == "initial" || key == "inherit" || key == "unset" || key == "revert" || key == "revert-layer" ||
                key == "default")
                return {};
            path.push_back(name);
            if (i == tokens->size() || (*tokens)[i].type != LXB_CSS_SYNTAX_TOKEN_DELIM || (*tokens)[i].text != ".")
                break;
            ++i;
        }
        names.push_back(std::move(path));
        if (i == tokens->size())
            break;
        if ((*tokens)[i++].type != LXB_CSS_SYNTAX_TOKEN_COMMA || i == tokens->size())
            return {};
    }
    return names;
}
Layer *add(Layer &parent, const std::string &name, bool anonymous = false) {
    if (!anonymous)
        if (const auto found = parent.names.find(name); found != parent.names.end())
            return found->second;
    if (parent.children.size() >= 4096)
        throw DocumentFailure{XL_UNSUPPORTED};
    auto child = std::make_unique<Layer>();
    child->path = parent.path;
    child->path.push_back(static_cast<uint32_t>(parent.children.size()));
    auto *result = child.get();
    parent.children.push_back(std::move(child));
    if (!anonymous)
        parent.names.emplace(name, result);
    return result;
}
} // namespace
ActiveStyles::ActiveStyles(Document::Impl &document) {
    conditions = document.link_media;
    Layer root;
    size_t visited = 0;
    auto *sheets = document.dom->dom_document.css->stylesheets;
    for (size_t index = 0; index < lexbor_array_length(sheets); ++index) {
        auto *sheet = static_cast<lxb_css_stylesheet_t *>(lexbor_array_get(sheets, index));
        if (sheet->element && !document.connected(lxb_dom_interface_node(sheet->element)))
            continue;
        if (sheet->element) {
            auto *node = lxb_dom_interface_node(sheet->element);
            const auto id = document.identify(node);
            const auto media = document.attribute(id, "media", false);
            manual |= !media.empty();
            auto query = std::make_shared<MediaQuery>(media);
            const auto matches = query->matches(document.environment);
            conditions.emplace_back(std::move(query), matches);
            if (lxb_dom_element_has_attribute(element(node), chars("disabled"), 8) || !matches) {
                manual = true;
                continue;
            }
            const auto type = document.attribute(id, "type", false);
            if (!type.empty() && !ui_mime_is(type, "text/css")) {
                manual = true;
                continue;
            }
        }
        const auto linked = document.linked_sheets.find(sheet);
        auto source = std::make_shared<const std::string>(
            linked != document.linked_sheets.end()
                ? std::string(reinterpret_cast<const char *>(linked->second->bytes.data()),
                              linked->second->bytes.size())
            : sheet->element ? document.text(document.identify(lxb_dom_interface_node(sheet->element)))
                             : std::string{});
        const auto nested=document.nested_styles->get(source,sheet->element?sheet->element:sheet);
        auto* working=sheet;
        if(nested->sheet){working=nested->sheet;source=nested->source;manual=true;}
        if(!working->root||working->root->type!=LXB_CSS_RULE_LIST)continue;
        std::function<void(lxb_css_rule_list_t *, Layer &, unsigned)> collect;
        collect = [&](lxb_css_rule_list_t *list, Layer &layer, unsigned depth) {
            if (!list)
                return;
            if (depth > 64)
                throw DocumentFailure{XL_UNSUPPORTED};
            for (auto *rule = list->first; rule; rule = rule->next) {
                if (++visited > 65536)
                    throw DocumentFailure{XL_UNSUPPORTED};
                if (rule->type == LXB_CSS_RULE_STYLE) {
                    auto order = std::make_shared<LayerOrder>(layer.path);
                    order->push_back(UINT32_MAX);
                    rules.push_back({lxb_css_rule_style(rule), source, std::move(order),nested->sheet?nested:nullptr});
                    continue;
                }
                if (rule->type != LXB_CSS_RULE_AT_RULE)
                    continue;
                auto *at = lxb_css_rule_at(rule);
                if (at->type == LXB_CSS_AT_RULE_MEDIA) {
                    manual = true;
                    if (at->prelude_begin <= at->prelude_end && at->prelude_end <= source->size()) {
                        auto query = std::make_shared<MediaQuery>(
                            std::string_view(*source).substr(at->prelude_begin, at->prelude_end - at->prelude_begin));
                        const auto matches = query->matches(document.environment);
                        conditions.emplace_back(std::move(query), matches);
                        if (matches)
                            collect(at->u.media->block, layer, depth + 1);
                    }
                } else if (at->type == LXB_CSS_AT_RULE__CUSTOM) {
                    const auto &name = at->u.custom->name;
                    if (generated::lower({reinterpret_cast<const char *>(name.data), name.length}) != "layer")
                        continue;
                    manual = layers = true;
                    const auto &prelude = at->u.custom->prelude;
                    const auto names = layer_names({reinterpret_cast<const char *>(prelude.data), prelude.length});
                    if (!names || (at->u.custom->block && names->size() > 1))
                        continue;
                    if (names->empty()) {
                        if (at->u.custom->block)
                            collect(at->u.custom->block, *add(layer, "", true), depth + 1);
                        continue;
                    }
                    for (const auto &path : *names) {
                        auto *current = &layer;
                        for (const auto &part : path)
                            current = add(*current, part);
                        if (at->u.custom->block)
                            collect(at->u.custom->block, *current, depth + 1);
                    }
                }
            }
        };
        collect(lxb_css_rule_list(working->root), root, 0);
    }
}
} // namespace xl::detail
