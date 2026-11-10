#include "computed.h"
#include "layout_shorthand.h"
#include "grid_style.h"
#include "pseudo_elements.h"
#include "radius_style.h"
#include "variables.h"
#include "generated_content.h"
#include "include/utils/SkParse.h"
#include <algorithm>
#include <charconv>
#include <climits>
#include <lexbor/css/unit.h>

namespace xl::detail {
namespace {
bool default_inline(uintptr_t tag) {
    switch (tag) {
    case LXB_TAG_HTML:
    case LXB_TAG_BODY:
    case LXB_TAG_DIV:
    case LXB_TAG_DETAILS:
    case LXB_TAG_SUMMARY:
    case LXB_TAG_P:
    case LXB_TAG_H1:
    case LXB_TAG_H2:
    case LXB_TAG_H3:
    case LXB_TAG_H4:
    case LXB_TAG_H5:
    case LXB_TAG_H6:
    case LXB_TAG_UL:
    case LXB_TAG_OL:
    case LXB_TAG_LI:
    case LXB_TAG_DL:
    case LXB_TAG_DT:
    case LXB_TAG_DD:
    case LXB_TAG_FORM:
    case LXB_TAG_FIELDSET:
    case LXB_TAG_PRE:
    case LXB_TAG_BLOCKQUOTE:
    case LXB_TAG_ARTICLE:
    case LXB_TAG_ASIDE:
    case LXB_TAG_HEADER:
    case LXB_TAG_FOOTER:
    case LXB_TAG_MAIN:
    case LXB_TAG_NAV:
    case LXB_TAG_SECTION:
    case LXB_TAG_FIGURE:
    case LXB_TAG_FIGCAPTION:
    case LXB_TAG_TABLE:
    case LXB_TAG_HR:
    case LXB_TAG_BUTTON:
    case LXB_TAG_INPUT:
    case LXB_TAG_TEXTAREA:
        return false;
    default:
        return true;
    }
}
const lxb_css_rule_declaration_t* declaration(lxb_dom_node_t* node, uintptr_t property) {
    return lxb_dom_element_style_by_id(element(node), property);
}
template <class Type> const Type* property(lxb_dom_node_t* node, uintptr_t name) {
    const auto* value = declaration(node, name);
    return value ? static_cast<const Type*>(value->u.user) : nullptr;
}
std::vector<size_t> order(Document::Impl& doc, const lxb_css_rule_t* rule) {
    if (const auto entry = doc.style_order.find(rule); entry != doc.style_order.end())
        return {entry->second[0], entry->second[1]};
    std::vector<size_t> indices;
    const lxb_css_rule_t* root = rule;
    for (auto* current = rule; current; current = current->parent) {
        root = current;
        size_t index = 0;
        for (auto* previous = current->prev; previous; previous = previous->prev)
            ++index;
        indices.push_back(index);
    }
    auto* stylesheets = doc.dom->dom_document.css->stylesheets;
    size_t sheet = lexbor_array_length(stylesheets);
    for (size_t i = 0; i < sheet; ++i) {
        auto* item = static_cast<lxb_css_stylesheet_t*>(lexbor_array_get(stylesheets, i));
        if (item->root == root) {
            sheet = i;
            break;
        }
    }
    indices.push_back(sheet);
    std::reverse(indices.begin(), indices.end());
    return indices;
}
bool longhand_wins(Document::Impl& doc, lxb_dom_node_t* node, uintptr_t longhand, uintptr_t shorthand) {
    const auto* a = lxb_dom_element_style_node_by_id(element(node), longhand);
    const auto* b = lxb_dom_element_style_node_by_id(element(node), shorthand);
    if (!a) return false;
    if (!b) return true;
    if (a->sp != b->sp) return a->sp > b->sp;
    return order(doc, &static_cast<lxb_css_rule_declaration_t*>(a->entry.value)->rule) >
           order(doc, &static_cast<lxb_css_rule_declaration_t*>(b->entry.value)->rule);
}
float length(const lxb_css_value_length_percentage_t* value, float basis, float em, float root_em, int vw, int vh,
             float fallback, uint64_t& unsupported) {
    if (!value) return fallback;
    if (value->type == LXB_CSS_VALUE__NUMBER && value->u.length.num == 0) return 0;
    if (value->type == LXB_CSS_VALUE__PERCENTAGE) return static_cast<float>(value->u.percentage.num * basis / 100);
    if (value->type == LXB_CSS_VALUE_AUTO) return NAN;
    if (value->type == LXB_CSS_VALUE_INITIAL || value->type == LXB_CSS_VALUE_UNSET) return fallback;
    if (value->type != LXB_CSS_VALUE__LENGTH) {
        ++unsupported;
        return fallback;
    }
    const float number = static_cast<float>(value->u.length.num);
    switch (static_cast<int>(value->u.length.unit)) {
    case LXB_CSS_UNIT_PX:
        return number;
    case LXB_CSS_UNIT_EM:
        return number * em;
    case LXB_CSS_UNIT_REM:
        return number * root_em;
    case LXB_CSS_UNIT_VW:
        return number * float(vw) / 100;
    case LXB_CSS_UNIT_VH:
        return number * float(vh) / 100;
    case LXB_CSS_UNIT_PT:
        return number * 96 / 72;
    case LXB_CSS_UNIT_PC:
        return number * 16;
    case LXB_CSS_UNIT_IN:
        return number * 96;
    case LXB_CSS_UNIT_CM:
        return number * 96 / 2.54f;
    case LXB_CSS_UNIT_MM:
        return number * 96 / 25.4f;
    default:
        ++unsupported;
        return fallback;
    }
}
csscolor::Value selected_color(Document::Impl& doc,lxb_dom_node_t* node,std::string_view name,
    csscolor::Value initial,csscolor::Value parent,csscolor::Value current,bool dark,bool inherited=false) {
    if(doc.variables)if(const auto* declaration=doc.variables->color(element(node),name)) {
        if(declaration->expression)return csscolor::resolve(declaration->expression,{current,dark});
        const auto key=generated::lower(csscolor::trim(declaration->value));
        if(key=="inherit"||(key=="unset"&&inherited))return parent;
        return initial;
    }
    return inherited?parent:initial;
}
} // namespace

WhiteSpace white_space(lxb_dom_node_t* node, WhiteSpace inherited) {
    auto value = node->local_name == LXB_TAG_PRE ? WhiteSpace::pre : inherited;
    if (const auto* whitespace = property<lxb_css_property_white_space_t>(node, LXB_CSS_PROPERTY_WHITE_SPACE)) {
        switch (whitespace->type) {
        case LXB_CSS_VALUE_NORMAL:
        case LXB_CSS_VALUE_INITIAL:
            value = WhiteSpace::normal;
            break;
        case LXB_CSS_VALUE_NOWRAP:
            value = WhiteSpace::nowrap;
            break;
        case LXB_CSS_VALUE_PRE:
            value = WhiteSpace::pre;
            break;
        case LXB_CSS_VALUE_PRE_WRAP:
            value = WhiteSpace::pre_wrap;
            break;
        case LXB_CSS_VALUE_PRE_LINE:
            value = WhiteSpace::pre_line;
            break;
        case LXB_CSS_VALUE_BREAK_SPACES:
            value = WhiteSpace::break_spaces;
            break;
        case LXB_CSS_VALUE_INHERIT:
        case LXB_CSS_VALUE_UNSET:
            value = inherited;
            break;
        default:
            break;
        }
    }
    return value;
}
BoxStyle box_style(Document::Impl& doc, lxb_dom_node_t* node, float width, int vw, int vh, const BoxStyle& inherited, float containing_height) {
    BoxStyle result;
    result.paint_owner=doc.identify(node);
    result.absolute_container=inherited.absolute_container;
    result.dark=csscolor::dark_scheme(generated::declaration(node,"color-scheme"),inherited.dark,doc.environment.color_scheme==XL_MEDIA_COLOR_DARK-1);
    if (!doc.pseudos->generated(node)) result.unsupported += doc.pseudos->unsupported(node);
    result.font_size = inherited.font_size;
    result.root_font_size = inherited.root_font_size;
    result.color = inherited.color;
    result.weight = inherited.weight;
    result.italic = inherited.italic;
    result.oblique = inherited.oblique;
    result.stretch = inherited.stretch;
    result.families = inherited.families;
    result.rtl = inherited.rtl;
    result.text_align = inherited.text_align;
    result.whitespace = white_space(node, inherited.whitespace);
    result.line_height = inherited.line_height;
    result.line_height_factor = inherited.line_height_factor;
    result.tab_size = inherited.tab_size;
    result.tab_is_length = inherited.tab_is_length;
    result.inline_flow = default_inline(node->local_name);
    result.text_control = doc.text_field(node);
    if (result.text_control) result.inline_flow = result.inline_atomic = true;
    if (result.text_control && !property<lxb_css_property_white_space_t>(node, LXB_CSS_PROPERTY_WHITE_SPACE))
        result.whitespace = node->local_name == LXB_TAG_TEXTAREA ? WhiteSpace::pre_wrap : WhiteSpace::pre;
    if (node->local_name == LXB_TAG_B || node->local_name == LXB_TAG_STRONG) result.weight = 700;
    if (node->local_name == LXB_TAG_I || node->local_name == LXB_TAG_EM) {
        result.italic = true;
        result.oblique = false;
    }
    const auto id = doc.identify(node);
    result.hidden = doc.details_hidden(node) || doc.attribute(id, "hidden", false).size() != 0 ||
                    lxb_dom_element_has_attribute(element(node), chars("hidden"), 6);
    if (node->local_name == LXB_TAG_INPUT && doc.attribute(id, "type", false) == "hidden") result.hidden = true;
    const auto* display = property<lxb_css_property_display_t>(node, LXB_CSS_PROPERTY_DISPLAY);
    result.disclosure_marker = doc.disclosure_owner(node) && (node->local_name == LXB_TAG_DETAILS || !display || display->a == LXB_CSS_VALUE_LIST_ITEM);
    if (display) {
        if (result.text_control) result.inline_atomic = false;
        result.hidden = result.hidden || display->a == LXB_CSS_VALUE_NONE;
        if (display->a == LXB_CSS_VALUE_INLINE &&
            (display->b == LXB_CSS_VALUE__UNDEF || display->b == LXB_CSS_VALUE_FLOW))
            result.inline_flow = true;
        else if (display->a == LXB_CSS_VALUE_INLINE_BLOCK ||
                 (display->a == LXB_CSS_VALUE_INLINE && display->b == LXB_CSS_VALUE_FLOW_ROOT)) {
            result.inline_flow = result.inline_atomic = true;
        } else if (display->a == LXB_CSS_VALUE_FLEX || display->a == LXB_CSS_VALUE_INLINE_FLEX ||
                   ((display->a == LXB_CSS_VALUE_BLOCK || display->a == LXB_CSS_VALUE_INLINE) &&
                     display->b == LXB_CSS_VALUE_FLEX)) {
            result.flex_container = true;
            result.inline_flow = result.inline_atomic = display->a == LXB_CSS_VALUE_INLINE_FLEX || display->a == LXB_CSS_VALUE_INLINE;
        } else if (display->a == LXB_CSS_VALUE_GRID || display->a == LXB_CSS_VALUE_INLINE_GRID ||
                   ((display->a == LXB_CSS_VALUE_BLOCK || display->a == LXB_CSS_VALUE_INLINE) && display->b == LXB_CSS_VALUE_GRID)) {
            result.grid_container = true;
            result.inline_flow = result.inline_atomic = display->a == LXB_CSS_VALUE_INLINE_GRID || display->a == LXB_CSS_VALUE_INLINE;
        } else if (display->a == LXB_CSS_VALUE_BLOCK || display->a == LXB_CSS_VALUE_NONE || display->a == LXB_CSS_VALUE_LIST_ITEM) {
            result.inline_flow = false;
            if (display->b != LXB_CSS_VALUE__UNDEF && display->b != LXB_CSS_VALUE_FLOW) ++result.unsupported;
        } else {
            result.inline_flow = false;
            ++result.unsupported;
        }
    }
    if (result.disclosure_marker) {
        for (const auto* name : {"list-style", "list-style-type"}) {
            const auto* item = lxb_dom_element_style_by_name(element(node), chars(name), std::char_traits<char>::length(name));
            if (!item || item->type != LXB_CSS_PROPERTY__CUSTOM) continue;
            std::string value(reinterpret_cast<const char*>(item->u.custom->value.data), item->u.custom->value.length);
            const auto first = value.find_first_not_of(" \t\r\n\f"), last = value.find_last_not_of(" \t\r\n\f");
            if (first != std::string::npos) value = value.substr(first, last - first + 1);
            for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (value == "none") result.disclosure_marker = false;
        }
    }
    if (const auto* font = property<lxb_css_property_font_size_t>(node, LXB_CSS_PROPERTY_FONT_SIZE)) {
        switch (font->type) {
        case LXB_CSS_VALUE__LENGTH:
            result.font_size = length(&font->length, inherited.font_size, inherited.font_size, inherited.root_font_size,
                                      vw, vh, inherited.font_size, result.unsupported);
            break;
        case LXB_CSS_VALUE_INITIAL:
        case LXB_CSS_VALUE_MEDIUM:
            result.font_size = 16;
            break;
        case LXB_CSS_VALUE_INHERIT:
        case LXB_CSS_VALUE_UNSET:
            break;
        case LXB_CSS_VALUE_XX_SMALL:
            result.font_size = 9;
            break;
        case LXB_CSS_VALUE_X_SMALL:
            result.font_size = 10;
            break;
        case LXB_CSS_VALUE_SMALL:
            result.font_size = 13;
            break;
        case LXB_CSS_VALUE_LARGE:
            result.font_size = 18;
            break;
        case LXB_CSS_VALUE_X_LARGE:
            result.font_size = 24;
            break;
        case LXB_CSS_VALUE_XX_LARGE:
            result.font_size = 32;
            break;
        case LXB_CSS_VALUE_XXX_LARGE:
            result.font_size = 48;
            break;
        case LXB_CSS_VALUE_LARGER:
            result.font_size = inherited.font_size * 1.2f;
            break;
        case LXB_CSS_VALUE_SMALLER:
            result.font_size = inherited.font_size / 1.2f;
            break;
        default:
            ++result.unsupported;
            break;
        }
    }
    if (!std::isfinite(result.font_size) || result.font_size < 0) {
        result.font_size = inherited.font_size;
        ++result.unsupported;
    }
    if (node->local_name == LXB_TAG_HTML) result.root_font_size = result.font_size;
    const auto initial_color=csscolor::from_argb(result.dark?0xffffffff:0xff000000);
    auto parent_color=inherited.foreground;
    if(node->local_name==LXB_TAG_HTML)parent_color=initial_color;
    result.foreground=selected_color(doc,node,"color",initial_color,parent_color,parent_color,result.dark,true);
    result.color=csscolor::argb(result.foreground);
    if (const auto* weight = property<lxb_css_property_font_weight_t>(node, LXB_CSS_PROPERTY_FONT_WEIGHT)) {
        if (weight->type == LXB_CSS_VALUE__NUMBER)
            result.weight = static_cast<int>(std::clamp(weight->number.num, 1.0, 1000.0));
        else if (weight->type == LXB_CSS_VALUE_NORMAL || weight->type == LXB_CSS_VALUE_INITIAL)
            result.weight = 400;
        else if (weight->type == LXB_CSS_VALUE_BOLD)
            result.weight = 700;
        else if (weight->type == LXB_CSS_VALUE_BOLDER)
            result.weight = inherited.weight < 350 ? 400 : inherited.weight < 550 ? 700 : 900;
        else if (weight->type == LXB_CSS_VALUE_LIGHTER)
            result.weight = inherited.weight <= 550 ? 100 : inherited.weight <= 750 ? 400 : 700;
        else if (weight->type == LXB_CSS_VALUE_INHERIT || weight->type == LXB_CSS_VALUE_UNSET)
            result.weight = inherited.weight;
        else
            ++result.unsupported;
    }
    if (const auto* slant = property<lxb_css_property_font_style_t>(node, LXB_CSS_PROPERTY_FONT_STYLE)) {
        if (slant->type == LXB_CSS_VALUE_ITALIC || slant->type == LXB_CSS_VALUE_OBLIQUE) {
            result.italic = true;
            result.oblique = slant->type == LXB_CSS_VALUE_OBLIQUE;
            if (result.oblique && slant->angle.num != 0)
                ++result.unsupported; // Arbitrary synthetic oblique angles remain pending.
        } else if (slant->type == LXB_CSS_VALUE_NORMAL || slant->type == LXB_CSS_VALUE_INITIAL) {
            result.italic = false;
            result.oblique = false;
        } else if (slant->type == LXB_CSS_VALUE_INHERIT || slant->type == LXB_CSS_VALUE_UNSET) {
            result.italic = inherited.italic;
            result.oblique = inherited.oblique;
        } else
            ++result.unsupported;
    }
    if (const auto* stretch = property<lxb_css_property_font_stretch_t>(node, LXB_CSS_PROPERTY_FONT_STRETCH)) {
        constexpr uintptr_t keywords[] = {
            LXB_CSS_VALUE_ULTRA_CONDENSED, LXB_CSS_VALUE_EXTRA_CONDENSED, LXB_CSS_VALUE_CONDENSED,
            LXB_CSS_VALUE_SEMI_CONDENSED,  LXB_CSS_VALUE_NORMAL,          LXB_CSS_VALUE_SEMI_EXPANDED,
            LXB_CSS_VALUE_EXPANDED,        LXB_CSS_VALUE_EXTRA_EXPANDED,  LXB_CSS_VALUE_ULTRA_EXPANDED};
        if (stretch->type == LXB_CSS_VALUE_INITIAL)
            result.stretch = 5;
        else if (stretch->type == LXB_CSS_VALUE__PERCENTAGE) {
            constexpr double percentages[] = {50, 62.5, 75, 87.5, 100, 112.5, 125, 150, 200};
            result.stretch = 1;
            for (int i = 1; i < 9; ++i)
                if (std::abs(percentages[i] - stretch->percentage.num) <
                    std::abs(percentages[result.stretch - 1] - stretch->percentage.num))
                    result.stretch = i + 1;
            if (percentages[result.stretch - 1] != stretch->percentage.num) ++result.unsupported;
        } else if (stretch->type != LXB_CSS_VALUE_INHERIT && stretch->type != LXB_CSS_VALUE_UNSET) {
            const auto* found = std::find(std::begin(keywords), std::end(keywords), stretch->type);
            if (found != std::end(keywords))
                result.stretch = int(found - keywords) + 1;
            else
                ++result.unsupported;
        }
    }
    // Lexbor stores unsupported font properties as custom declarations. Keep
    // diagnostics for supported syntax whose rendering/variable resolution is
    // not implemented, including small caps; do not claim full Fonts coverage.
    for (const auto* name : {"font", "font-variant", "font-variant-caps", "font-feature-settings",
                             "font-variation-settings", "font-size-adjust"}) {
        const auto* value =
            lxb_dom_element_style_by_name(element(node), chars(name), std::char_traits<char>::length(name));
        if (!value || value->type != LXB_CSS_PROPERTY__CUSTOM) continue;
        const auto& text = value->u.custom->value;
        const std::string_view raw{reinterpret_cast<const char*>(text.data), text.length};
        if (raw != "normal" && raw != "initial" && raw != "inherit" && raw != "unset") ++result.unsupported;
    }
    if (const auto* family = property<lxb_css_property_font_family_t>(node, LXB_CSS_PROPERTY_FONT_FAMILY)) {
        const auto* first = family->first;
        if (!(first && first->generic &&
              (first->u.type == LXB_CSS_VALUE_INHERIT || first->u.type == LXB_CSS_VALUE_UNSET))) {
            result.families.clear();
            for (auto* name = first; name; name = name->next) {
                if (name->generic) {
                    const auto* value = lxb_css_value_by_id(name->u.type);
                    if (value && name->u.type != LXB_CSS_VALUE_INITIAL)
                        result.families.emplace_back(reinterpret_cast<const char*>(value->name), value->length);
                } else
                    result.families.emplace_back(reinterpret_cast<const char*>(name->u.str.data), name->u.str.length);
            }
        }
    }
    if (const auto* height = property<lxb_css_property_line_height_t>(node, LXB_CSS_PROPERTY_LINE_HEIGHT)) {
        if (height->type == LXB_CSS_VALUE_NORMAL || height->type == LXB_CSS_VALUE_INITIAL)
            result.line_height = -1;
        else if (height->type == LXB_CSS_VALUE__NUMBER) {
            result.line_height = static_cast<float>(height->u.number.num);
            result.line_height_factor = true;
        } else if (height->type == LXB_CSS_VALUE__LENGTH || height->type == LXB_CSS_VALUE__PERCENTAGE) {
            lxb_css_value_length_percentage_t value{};
            value.type = height->type;
            if (height->type == LXB_CSS_VALUE__LENGTH)
                value.u.length = height->u.length;
            else
                value.u.percentage = height->u.percentage;
            result.line_height = length(&value, result.font_size, result.font_size, result.root_font_size, vw, vh, -1,
                                        result.unsupported);
            result.line_height_factor = false;
        }
    }
    if (const auto* tab = property<lxb_css_property_tab_size_t>(node, LXB_CSS_PROPERTY_TAB_SIZE)) {
        if (tab->type == LXB_CSS_VALUE_INITIAL) {
            result.tab_size = 8;
            result.tab_is_length = false;
        } else if (tab->type == LXB_CSS_VALUE__NUMBER) {
            result.tab_size = static_cast<float>(tab->u.number.num);
            result.tab_is_length = false;
        } else if (tab->type == LXB_CSS_VALUE__LENGTH) {
            lxb_css_value_length_percentage_t value{};
            value.type = tab->type;
            value.u.length = tab->u.length;
            result.tab_size = length(&value, result.font_size, result.font_size, result.root_font_size, vw, vh, 8,
                                     result.unsupported);
            result.tab_is_length = true;
        }
    }
    if (const auto* vertical = property<lxb_css_property_vertical_align_t>(node, LXB_CSS_PROPERTY_VERTICAL_ALIGN)) {
        if (vertical->type == LXB_CSS_VALUE_INHERIT) {
            result.vertical_shift = inherited.vertical_shift;
            result.vertical_percentage = inherited.vertical_percentage;
            result.vertical_keyword = inherited.vertical_keyword;
        } else if (vertical->type != LXB_CSS_VALUE_INITIAL && vertical->type != LXB_CSS_VALUE_UNSET) {
            if (vertical->type != LXB_CSS_VALUE__UNDEF) ++result.unsupported;
            if (vertical->alignment.type != LXB_CSS_VALUE__UNDEF &&
                vertical->alignment.type != LXB_CSS_VALUE_BASELINE) {
                switch(vertical->alignment.type) {
                case LXB_CSS_VALUE_MIDDLE:case LXB_CSS_VALUE_TEXT_TOP:case LXB_CSS_VALUE_TEXT_BOTTOM:
                    result.vertical_keyword=vertical->alignment.type;break;
                default:++result.unsupported;break;
                }
            }
            const auto& shift = vertical->shift;
            if (shift.type == LXB_CSS_VALUE__LENGTH || shift.type == LXB_CSS_VALUE__PERCENTAGE ||
                shift.type == LXB_CSS_VALUE__NUMBER) {
                if(shift.type == LXB_CSS_VALUE__PERCENTAGE)result.vertical_percentage=static_cast<float>(shift.u.percentage.num);
                else result.vertical_shift = -length(&shift, 0, result.font_size, result.root_font_size,
                                                    vw, vh, 0, result.unsupported);
            } else if (shift.type == LXB_CSS_VALUE_SUB || shift.type == LXB_CSS_VALUE_SUPER)
                result.vertical_keyword=shift.type;
            else if (shift.type == LXB_CSS_VALUE_TOP || shift.type == LXB_CSS_VALUE_BOTTOM)
                result.vertical_keyword=shift.type;
            else if (shift.type != LXB_CSS_VALUE__UNDEF) ++result.unsupported;
        }
    }
    if(std::isfinite(result.vertical_percentage) && result.inline_flow && !result.hidden) {
        float height=result.line_height_factor?result.line_height*result.font_size:result.line_height;
        if(height<0) {
            if(!doc.text_engine)doc.text_engine=std::make_unique<TextEngine>();
            TextFormat font;
            font.size=result.font_size;font.weight=result.weight;font.italic=result.italic;
            font.oblique=result.oblique;font.stretch=result.stretch;font.families=result.families;
            height=doc.text_engine->normal_line_height(font);
        }
        result.vertical_shift=-height*result.vertical_percentage/100;
    }
    if(result.vertical_keyword && result.vertical_keyword != LXB_CSS_VALUE_TOP &&
       result.vertical_keyword != LXB_CSS_VALUE_BOTTOM && result.inline_flow && !result.hidden) {
        auto text_format=[](const BoxStyle& style) {
            TextFormat font;
            font.size=style.font_size;font.weight=style.weight;font.italic=style.italic;
            font.oblique=style.oblique;font.stretch=style.stretch;font.families=style.families;
            return font;
        };
        if(!doc.text_engine)doc.text_engine=std::make_unique<TextEngine>();
        const auto own=doc.text_engine->font_metrics(text_format(result));
        const auto parent=doc.text_engine->font_metrics(text_format(inherited));
        const float height=result.line_height<0 ? own.ascent+own.descent+own.leading
            : result.line_height_factor ? result.line_height*result.font_size : result.line_height;
        const auto leading=(height-own.ascent-own.descent)*.5f;
        switch(result.vertical_keyword) {
        case LXB_CSS_VALUE_MIDDLE:
            result.vertical_shift=(own.ascent-own.descent-parent.x_height)*.5f;break;
        case LXB_CSS_VALUE_TEXT_TOP:result.vertical_shift=own.ascent+leading-parent.ascent;break;
        case LXB_CSS_VALUE_TEXT_BOTTOM:result.vertical_shift=parent.descent-own.descent-leading;break;
        case LXB_CSS_VALUE_SUB:result.vertical_shift=inherited.font_size*.2f;break;
        case LXB_CSS_VALUE_SUPER:result.vertical_shift=-inherited.font_size/3;break;
        default:break;
        }
    }
    const auto direction = doc.attribute(id, "dir", false);
    if (direction == "rtl" || direction == "ltr") result.rtl = direction == "rtl";
    if (const auto* dir = property<lxb_css_property_direction_t>(node, LXB_CSS_PROPERTY_DIRECTION)) {
        if (dir->type == LXB_CSS_VALUE_RTL || dir->type == LXB_CSS_VALUE_LTR)
            result.rtl = dir->type == LXB_CSS_VALUE_RTL;
    }
    if (const auto* align = property<lxb_css_property_text_align_t>(node, LXB_CSS_PROPERTY_TEXT_ALIGN)) {
        switch (align->type) {
        case LXB_CSS_VALUE_START:
            result.text_align = 0;
            break;
        case LXB_CSS_VALUE_LEFT:
            result.text_align = 1;
            break;
        case LXB_CSS_VALUE_RIGHT:
            result.text_align = 2;
            break;
        case LXB_CSS_VALUE_CENTER:
            result.text_align = 3;
            break;
        case LXB_CSS_VALUE_END:
            result.text_align = 4;
            break;
        case LXB_CSS_VALUE_INHERIT:
        case LXB_CSS_VALUE_UNSET:
            break;
        default:
            ++result.unsupported;
            break;
        }
    }
    result.background_color=selected_color(doc,node,"background-color",csscolor::from_argb(0),
        inherited.background_color,result.foreground,result.dark);
    if(result.text_control&&(!doc.variables||!doc.variables->color(element(node),"background-color")))result.background_color=csscolor::from_argb(result.dark?0xff202020:0xffffffff);
    result.background=csscolor::argb(result.background_color);
    constexpr uintptr_t dimension_properties[]{LXB_CSS_PROPERTY_WIDTH, LXB_CSS_PROPERTY_HEIGHT,
        LXB_CSS_PROPERTY_MIN_WIDTH, LXB_CSS_PROPERTY_MAX_WIDTH, LXB_CSS_PROPERTY_MIN_HEIGHT, LXB_CSS_PROPERTY_MAX_HEIGHT};
    for (size_t i = 0; i < result.sizes.size(); ++i) {
        auto& size = result.sizes[i];
        const auto initial = i == size_max_width || i == size_max_height ? SizeKind::none : SizeKind::automatic;
        size.kind = initial;
        const auto* value = property<lxb_css_value_length_percentage_t>(node, dimension_properties[i]);
        if (!value || value->type == LXB_CSS_VALUE_INITIAL || value->type == LXB_CSS_VALUE_UNSET) continue;
        if (value->type == LXB_CSS_VALUE_INHERIT) { size = inherited.sizes[i]; continue; }
        if (value->type == LXB_CSS_VALUE_AUTO) size.kind = SizeKind::automatic;
        else if (value->type == LXB_CSS_VALUE_NONE) size.kind = SizeKind::none;
        else if (value->type == LXB_CSS_VALUE_MIN_CONTENT) size.kind = SizeKind::min_content;
        else if (value->type == LXB_CSS_VALUE_MAX_CONTENT) size.kind = SizeKind::max_content;
        else if (value->type == css_fit_content) size.kind = SizeKind::fit_content;
        else if (value->type == LXB_CSS_VALUE__PERCENTAGE) { size.kind = SizeKind::percentage; size.value = float(value->u.percentage.num / 100); }
        else {
            size.kind = SizeKind::length;
            size.value = length(value, 0, result.font_size, result.root_font_size, vw, vh, NAN, result.unsupported);
            if (!std::isfinite(size.value)) size.kind = initial;
        }
    }
    result.width = result.sizes[size_width].resolve(width, NAN);
    result.height = result.sizes[size_height].resolve(containing_height, NAN);
    result.height_definite = std::isfinite(result.height);
    result.min_width = result.sizes[size_min_width].resolve(width, NAN);
    result.max_width = result.sizes[size_max_width].resolve(width, INFINITY);
    result.min_height = result.sizes[size_min_height].resolve(containing_height, NAN);
    result.max_height = result.sizes[size_max_height].resolve(containing_height, INFINITY);
    result.width_percentage = result.sizes[size_width].kind == SizeKind::percentage;
    result.height_ratio = result.sizes[size_height].kind == SizeKind::percentage ? result.sizes[size_height].value : NAN;
    const auto* flow = property<lxb_css_property_flex_flow_t>(node, LXB_CSS_PROPERTY_FLEX_FLOW);
    lxb_css_value_type_t flex_direction = flow ? flow->type_direction : LXB_CSS_VALUE_ROW;
    lxb_css_value_type_t wrapping = flow ? flow->wrap : LXB_CSS_VALUE_NOWRAP;
    if (const auto* value = property<lxb_css_property_flex_direction_t>(node, LXB_CSS_PROPERTY_FLEX_DIRECTION))
        if (longhand_wins(doc, node, LXB_CSS_PROPERTY_FLEX_DIRECTION, LXB_CSS_PROPERTY_FLEX_FLOW)) flex_direction = value->type;
    if (const auto* value = property<lxb_css_property_flex_wrap_t>(node, LXB_CSS_PROPERTY_FLEX_WRAP))
        if (longhand_wins(doc, node, LXB_CSS_PROPERTY_FLEX_WRAP, LXB_CSS_PROPERTY_FLEX_FLOW)) wrapping = value->type;
    result.flex.column = flex_direction == LXB_CSS_VALUE_COLUMN || flex_direction == LXB_CSS_VALUE_COLUMN_REVERSE;
    result.flex.reverse = flex_direction == LXB_CSS_VALUE_ROW_REVERSE || flex_direction == LXB_CSS_VALUE_COLUMN_REVERSE;
    if (flex_direction == LXB_CSS_VALUE_INHERIT) { result.flex.column = inherited.flex.column; result.flex.reverse = inherited.flex.reverse; }
    result.flex.wrap = wrapping == LXB_CSS_VALUE_WRAP ? 1 : wrapping == LXB_CSS_VALUE_WRAP_REVERSE ? 2 : 0;
    if (wrapping == LXB_CSS_VALUE_INHERIT || (flow && flow->type_direction == LXB_CSS_VALUE_INHERIT)) result.flex.wrap = inherited.flex.wrap;
    const auto alignment = [&](uintptr_t name, FlexAlign initial, FlexAlign parent) {
        const auto* value = property<lxb_css_property_align_items_t>(node, name);
        if (!value) return initial;
        switch (value->type) {
        case LXB_CSS_VALUE_INHERIT: return parent;
        case LXB_CSS_VALUE_INITIAL: case LXB_CSS_VALUE_UNSET: return initial;
        case LXB_CSS_VALUE_AUTO: return FlexAlign::automatic;
        case LXB_CSS_VALUE_FLEX_START: return FlexAlign::start;
        case LXB_CSS_VALUE_FLEX_END: return FlexAlign::end;
        case LXB_CSS_VALUE_START: return FlexAlign::flow_start;
        case LXB_CSS_VALUE_END: return FlexAlign::flow_end;
        case css_space_evenly: return FlexAlign::evenly;
        case LXB_CSS_VALUE_CENTER: return FlexAlign::center;
        case LXB_CSS_VALUE_SPACE_BETWEEN: return FlexAlign::between;
        case LXB_CSS_VALUE_SPACE_AROUND: return FlexAlign::around;
        case LXB_CSS_VALUE_STRETCH: return FlexAlign::stretch;
        case LXB_CSS_VALUE_BASELINE: return FlexAlign::baseline;
        default: ++result.unsupported; return initial;
        }
    };
    result.flex.justify = alignment(LXB_CSS_PROPERTY_JUSTIFY_CONTENT, FlexAlign::start, inherited.flex.justify);
    result.flex.items = alignment(LXB_CSS_PROPERTY_ALIGN_ITEMS, FlexAlign::stretch, inherited.flex.items);
    result.flex.content = alignment(LXB_CSS_PROPERTY_ALIGN_CONTENT, FlexAlign::stretch, inherited.flex.content);
    result.align_self = alignment(LXB_CSS_PROPERTY_ALIGN_SELF, FlexAlign::automatic, inherited.align_self);
    if (const auto* value = property<lxb_css_property_order_t>(node, LXB_CSS_PROPERTY_ORDER)) {
        if (value->type == LXB_CSS_VALUE_INHERIT) result.order = inherited.order;
        else if (value->type == LXB_CSS_VALUE__INTEGER) result.order = int(std::clamp(double(value->integer.num), double(INT_MIN), double(INT_MAX)));
    }
    const auto* flex = property<lxb_css_property_flex_t>(node, LXB_CSS_PROPERTY_FLEX);
    if (flex) {
        if (flex->type == LXB_CSS_VALUE_INHERIT) {
            result.grow = inherited.grow; result.shrink = inherited.shrink; result.basis = inherited.basis;
            result.basis_percentage = inherited.basis_percentage; result.basis_content = inherited.basis_content;
        } else if (flex->type == LXB_CSS_VALUE_NONE) result.shrink = 0;
        else if (flex->type != LXB_CSS_VALUE_INITIAL && flex->type != LXB_CSS_VALUE_UNSET) {
            result.grow = flex->grow.type == LXB_CSS_VALUE__UNDEF ? 1 : float(flex->grow.number.num);
            result.shrink = flex->shrink.type == LXB_CSS_VALUE__UNDEF ? 1 : float(flex->shrink.number.num);
        }
    }
    const auto factor = [&](uintptr_t name, float current, float initial, float parent) {
        const auto* value = property<lxb_css_property_flex_grow_t>(node, name);
        if (!value || !longhand_wins(doc, node, name, LXB_CSS_PROPERTY_FLEX)) return current;
        if (value->type == LXB_CSS_VALUE_INHERIT) return parent;
        if (value->type == LXB_CSS_VALUE_INITIAL || value->type == LXB_CSS_VALUE_UNSET) return initial;
        return std::max(0.0f, float(value->number.num));
    };
    result.grow = factor(LXB_CSS_PROPERTY_FLEX_GROW, result.grow, 0, inherited.grow);
    result.shrink = factor(LXB_CSS_PROPERTY_FLEX_SHRINK, result.shrink, 1, inherited.shrink);
    const auto* basis = property<lxb_css_property_flex_basis_t>(node, LXB_CSS_PROPERTY_FLEX_BASIS);
    if (!basis || !longhand_wins(doc, node, LXB_CSS_PROPERTY_FLEX_BASIS, LXB_CSS_PROPERTY_FLEX)) {
        basis = nullptr;
        if (flex && flex->type != LXB_CSS_VALUE_INHERIT && flex->type != LXB_CSS_VALUE_INITIAL &&
            flex->type != LXB_CSS_VALUE_UNSET && flex->type != LXB_CSS_VALUE_NONE) {
            basis = &flex->basis;
            if (basis->type == LXB_CSS_VALUE__UNDEF) { result.basis = 0; result.basis_percentage = true; }
        }
    }
    if (basis) {
        if (basis->type == LXB_CSS_VALUE_INHERIT) {
            result.basis = inherited.basis; result.basis_percentage = inherited.basis_percentage; result.basis_content = inherited.basis_content;
        } else if (basis->type == LXB_CSS_VALUE_CONTENT) result.basis_content = true;
        else if (basis->type == LXB_CSS_VALUE__PERCENTAGE) {
            result.basis = float(basis->u.percentage.num / 100); result.basis_percentage = true;
        } else if (basis->type != LXB_CSS_VALUE__UNDEF)
            result.basis = length(basis, width, result.font_size, result.root_font_size, vw, vh, NAN, result.unsupported);
    }
    const auto gap = [&](const char* name, float parent, float parent_ratio, float& ratio) {
        const auto* declaration = lxb_dom_element_style_by_name(element(node), chars(name), std::char_traits<char>::length(name));
        if (!declaration || declaration->type != LXB_CSS_PROPERTY__CUSTOM) return 0.0f;
        const auto& data = declaration->u.custom->value;
        std::string raw(reinterpret_cast<const char*>(data.data), data.length);
        const auto begin = raw.find_first_not_of(" \t\r\n\f"), end = raw.find_last_not_of(" \t\r\n\f");
        raw = begin == std::string::npos ? "" : raw.substr(begin, end - begin + 1);
        if (raw == "inherit") { ratio = parent_ratio; return parent; }
        if (raw == "initial" || raw == "unset" || raw == "normal") return 0.0f;
        double number = 0;
        const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), number);
        if (parsed.ec != std::errc{} || !std::isfinite(number) || number < 0) { ++result.unsupported; return 0.0f; }
        const std::string_view suffix(parsed.ptr, size_t(raw.data() + raw.size() - parsed.ptr));
        if (suffix == "%") { ratio = float(number / 100); return 0.0f; }
        const auto* unit = lxb_css_unit_absolute_relative_by_name(chars(suffix), suffix.size());
        if (!unit && (number || !suffix.empty())) { ++result.unsupported; return 0.0f; }
        lxb_css_value_length_percentage_t value{};
        value.type = LXB_CSS_VALUE__LENGTH; value.u.length.num = number;
        value.u.length.unit = static_cast<lxb_css_unit_t>(unit ? unit->unique : LXB_CSS_UNIT_PX);
        return length(&value, width, result.font_size, result.root_font_size, vw, vh, 0, result.unsupported);
    };
    result.flex.row_gap = gap("row-gap", inherited.flex.row_gap, inherited.row_gap_ratio, result.row_gap_ratio);
    result.flex.column_gap = gap("column-gap", inherited.flex.column_gap, inherited.column_gap_ratio, result.column_gap_ratio);
    const GridUnits units{result.font_size, result.root_font_size, float(vw), float(vh)};
    const auto grid_value = [&](const char* name) -> std::optional<std::string> {
        const auto* entry = lxb_dom_element_style_by_name(element(node), chars(name), std::char_traits<char>::length(name));
        if (!entry || entry->type != LXB_CSS_PROPERTY__CUSTOM) return {};
        const auto& raw = entry->u.custom->value;
        std::string text(reinterpret_cast<const char*>(raw.data), raw.length);
        const auto first = text.find_first_not_of(" \n\r\t\f"), last = text.find_last_not_of(" \n\r\t\f");
        return first == std::string::npos ? "" : text.substr(first, last - first + 1);
    };
    const auto track_list = [&](const char* name, const GridAxis& parent) {
        if (const auto value = grid_value(name)) {
            if (*value == "inherit") return parent;
            if (*value == "initial" || *value == "unset") return GridAxis{};
            if (const auto parsed = grid_tracks(*value, units)) return *parsed;
            ++result.unsupported;
        }
        return GridAxis{};
    };
    result.grid.columns = track_list("grid-template-columns", inherited.grid.columns);
    result.grid.rows = track_list("grid-template-rows", inherited.grid.rows);
    const auto implicit = [&](const char* name, const std::vector<GridTrack>& parent) {
        if (const auto value = grid_value(name)) {
            if (*value == "inherit") return parent;
            if (*value != "initial" && *value != "unset") {
                if (const auto parsed = grid_tracks(*value, units, true)) return parsed->tracks;
                ++result.unsupported;
            }
        }
        return std::vector<GridTrack>{{}};
    };
    result.grid.auto_columns = implicit("grid-auto-columns", inherited.grid.auto_columns);
    result.grid.auto_rows = implicit("grid-auto-rows", inherited.grid.auto_rows);
    const auto place = [&](const char* start_name, const char* end_name, const GridPlacement& parent) {
        GridPlacement placement;
        for (int side = 0; side < 2; ++side)
            if (const auto value = grid_value(side ? end_name : start_name)) {
                auto& line = side ? placement.end : placement.start;
                if (*value == "inherit") line = side ? parent.end : parent.start;
                else if (*value != "initial" && *value != "unset") {
                    if (const auto parsed = grid_line(*value)) line = *parsed; else ++result.unsupported;
                }
            }
        return placement;
    };
    result.grid_columns = place("grid-column-start", "grid-column-end", inherited.grid_columns);
    result.grid_rows = place("grid-row-start", "grid-row-end", inherited.grid_rows);
    const auto align = [&](const char* name, FlexAlign initial, FlexAlign parent) {
        if (const auto value = grid_value(name)) {
            if (*value == "inherit") return parent;
            if (*value == "initial" || *value == "unset") return initial;
            if (const auto parsed = grid_alignment(*value)) return *parsed;
            ++result.unsupported;
        }
        return initial;
    };
    result.grid.justify_items = align("justify-items", FlexAlign::stretch, inherited.grid.justify_items);
    result.justify_self = align("justify-self", FlexAlign::automatic, inherited.justify_self);
    if (const auto value = grid_value("grid-auto-flow")) {
        if (*value == "inherit") { result.grid.column_flow = inherited.grid.column_flow; result.grid.dense = inherited.grid.dense; }
        else for (const auto& word : grid_components(*value)) {
            if (word == "column") result.grid.column_flow = true;
            if (word == "dense") result.grid.dense = true;
        }
    }
    if (const auto value = grid_value("grid-template-areas")) {
        if (*value == "inherit") result.grid.areas = inherited.grid.areas;
        else if (const auto parsed = grid_areas(*value)) result.grid.areas = *parsed;
        else if (*value != "initial" && *value != "unset") ++result.unsupported;
    }
    if (!result.grid.areas.empty()) {
        const auto* areas = &result.grid.areas;
                auto& columns = result.grid.columns; auto& rows = result.grid.rows;
                columns.tracks.resize(std::max(columns.tracks.size(), areas->front().size())); columns.names.resize(columns.tracks.size() + 1);
                rows.tracks.resize(std::max(rows.tracks.size(), areas->size())); rows.names.resize(rows.tracks.size() + 1);
                std::unordered_set<std::string> names;
                for (const auto& row : *areas) for (const auto& name : row) if (!name.empty()) names.insert(name);
                for (const auto& name : names) {
                    size_t r0 = areas->size(), r1 = 0, c0 = areas->front().size(), c1 = 0;
                    for (size_t r = 0; r < areas->size(); ++r) for (size_t c = 0; c < (*areas)[r].size(); ++c)
                        if ((*areas)[r][c] == name) { r0 = std::min(r0, r); r1 = std::max(r1, r + 1); c0 = std::min(c0, c); c1 = std::max(c1, c + 1); }
                    columns.names[c0].push_back(name + "-start"); columns.names[c1].push_back(name + "-end");
                    rows.names[r0].push_back(name + "-start"); rows.names[r1].push_back(name + "-end");
                }
    }
    if (result.grid_container && !declaration(node, LXB_CSS_PROPERTY_JUSTIFY_CONTENT)) result.flex.justify = FlexAlign::stretch;
    for (const auto* name : {"flex", "flex-flow", "gap", "grid", "grid-template", "grid-area", "grid-row", "grid-column", "place-items", "place-self", "place-content"})
        if (lxb_dom_element_style_by_name(element(node), chars(name), std::char_traits<char>::length(name)))
            ++result.unsupported; // Unexpanded variables/calc/cascade rollback remain explicit gaps.
    if (const auto* sizing = property<lxb_css_property_box_sizing_t>(node, LXB_CSS_PROPERTY_BOX_SIZING))
        result.border_box = sizing->type == LXB_CSS_VALUE_BORDER_BOX;
    const auto* margin = property<lxb_css_property_margin_t>(node, LXB_CSS_PROPERTY_MARGIN);
    const auto* padding = property<lxb_css_property_padding_t>(node, LXB_CSS_PROPERTY_PADDING);
    const lxb_css_value_length_percentage_t* margin_sides[4]{};
    const lxb_css_value_length_percentage_t* padding_sides[4]{};
    const auto expand = [](const auto* shorthand, auto& sides) {
        if (!shorthand) return;
        sides[0] = &shorthand->top;
        sides[1] = shorthand->right.type == LXB_CSS_VALUE__UNDEF ? sides[0] : &shorthand->right;
        sides[2] = shorthand->bottom.type == LXB_CSS_VALUE__UNDEF ? sides[0] : &shorthand->bottom;
        sides[3] = shorthand->left.type == LXB_CSS_VALUE__UNDEF ? sides[1] : &shorthand->left;
    };
    expand(margin, margin_sides);
    expand(padding, padding_sides);
    const uintptr_t margin_ids[] = {LXB_CSS_PROPERTY_MARGIN_TOP, LXB_CSS_PROPERTY_MARGIN_RIGHT,
                                    LXB_CSS_PROPERTY_MARGIN_BOTTOM, LXB_CSS_PROPERTY_MARGIN_LEFT};
    const uintptr_t padding_ids[] = {LXB_CSS_PROPERTY_PADDING_TOP, LXB_CSS_PROPERTY_PADDING_RIGHT,
                                     LXB_CSS_PROPERTY_PADDING_BOTTOM, LXB_CSS_PROPERTY_PADDING_LEFT};
    float* margins[] = {&result.top, &result.right, &result.bottom, &result.left};
    float* paddings[] = {&result.padding_top, &result.padding_right, &result.padding_bottom, &result.padding_left};
    const float inherited_margins[]{inherited.top, inherited.right, inherited.bottom, inherited.left};
    const float inherited_paddings[]{inherited.padding_top, inherited.padding_right, inherited.padding_bottom,
                                     inherited.padding_left};
    for (size_t index = 0; index < 4; ++index) {
        const auto* m = longhand_wins(doc, node, margin_ids[index], LXB_CSS_PROPERTY_MARGIN)
                            ? property<lxb_css_value_length_percentage_t>(node, margin_ids[index])
                            : margin_sides[index];
        const auto* p = longhand_wins(doc, node, padding_ids[index], LXB_CSS_PROPERTY_PADDING)
                            ? property<lxb_css_value_length_percentage_t>(node, padding_ids[index])
                            : padding_sides[index];
        *margins[index] =
            length(m && m->type != LXB_CSS_VALUE_INHERIT ? m : nullptr, width, result.font_size, result.root_font_size,
                   vw, vh, node->local_name == LXB_TAG_BODY ? 8.0f : 0.0f, result.unsupported);
        *paddings[index] =
            std::max(0.0f, length(p && p->type != LXB_CSS_VALUE_INHERIT ? p : nullptr, width, result.font_size,
                                  result.root_font_size, vw, vh, result.text_control && !p ? 2.0f : 0.0f, result.unsupported));
        if (m && m->type == LXB_CSS_VALUE_INHERIT) *margins[index] = inherited_margins[index];
        if (p && p->type == LXB_CSS_VALUE_INHERIT) *paddings[index] = inherited_paddings[index];
    }
    constexpr const char* sides[]{"top", "right", "bottom", "left"};
    constexpr std::pair<const char*, BorderStyle> styles[]{
        {"none", BorderStyle::none},     {"hidden", BorderStyle::hidden}, {"solid", BorderStyle::solid},
        {"dotted", BorderStyle::dotted}, {"dashed", BorderStyle::dashed}, {"double", BorderStyle::double_line},
        {"groove", BorderStyle::groove}, {"ridge", BorderStyle::ridge},   {"inset", BorderStyle::inset},
        {"outset", BorderStyle::outset}};
    for (size_t index = 0; index < 4; ++index) {
        auto& border = result.borders[index];
        if (result.text_control) { border.style = BorderStyle::solid; border.specified_width = 1; }
        const auto custom = [&](const char* field) {
            const auto name = std::string("border-") + sides[index] + '-' + field;
            const auto* declaration = lxb_dom_element_style_by_name(element(node), chars(name), name.size());
            if (!declaration || declaration->type != LXB_CSS_PROPERTY__CUSTOM) return std::string{};
            const auto& value = declaration->u.custom->value;
            return std::string(reinterpret_cast<const char*>(value.data), value.length);
        };
        const auto style = custom("style"), border_width = custom("width");
        if (style == "initial" || style == "unset") border.style = BorderStyle::none;
        if (style == "inherit")
            border.style = inherited.borders[index].style;
        else if (!style.empty() && style != "initial" && style != "unset") {
            const auto found =
                std::find_if(std::begin(styles), std::end(styles), [&](auto& entry) { return style == entry.first; });
            if (found != std::end(styles))
                border.style = found->second;
            else
                ++result.unsupported;
        }
        if (border_width == "inherit")
            border.specified_width = inherited.borders[index].width;
        else if (border_width == "thin")
            border.specified_width = 1;
        else if (border_width == "thick")
            border.specified_width = 5;
        else if (!border_width.empty() && border_width != "medium" && border_width != "initial" &&
                 border_width != "unset") {
            double number = 0;
            const auto parsed = std::from_chars(border_width.data(), border_width.data() + border_width.size(), number);
            const auto suffix =
                std::string_view(parsed.ptr, size_t(border_width.data() + border_width.size() - parsed.ptr));
            const auto* unit = lxb_css_unit_absolute_relative_by_name(chars(suffix), suffix.size());
            if (parsed.ec != std::errc{} || !std::isfinite(number) || number < 0 ||
                (!unit && (number != 0 || !suffix.empty())))
                ++result.unsupported;
            else {
                lxb_css_value_length_percentage_t value{};
                value.type = LXB_CSS_VALUE__LENGTH;
                value.u.length.num = number;
                value.u.length.unit = static_cast<lxb_css_unit_t>(unit ? unit->unique : LXB_CSS_UNIT_PX);
                border.specified_width =
                    length(&value, width, result.font_size, result.root_font_size, vw, vh, 3, result.unsupported);
                if (!std::isfinite(border.specified_width)) {
                    border.specified_width = 3;
                    ++result.unsupported;
                }
            }
        }
        result.border_colors[index]=selected_color(doc,node,std::string("border-")+sides[index]+"-color",
            result.foreground,inherited.border_colors[index],result.foreground,result.dark);
        border.color=csscolor::argb(result.border_colors[index]);
        border.width =
            border.style == BorderStyle::none || border.style == BorderStyle::hidden ? 0 : border.specified_width;
    }
    result.outline_color=result.foreground;
    result.outline.color = result.color;
    if (doc.focus_visible && doc.focused == doc.identify(node)) {
        result.outline.automatic = true;
        result.outline.color = 0xff175bc2;
        result.outline_color=csscolor::from_argb(result.outline.color);
    }
    const auto outline_value = [&](const char* field) {
        const auto* item = lxb_dom_element_style_by_name(element(node), chars(field), std::char_traits<char>::length(field));
        if (!item || item->type != LXB_CSS_PROPERTY__CUSTOM) return std::string{};
        const auto& value = item->u.custom->value;
        return std::string(reinterpret_cast<const char*>(value.data), value.length);
    };
    const auto outline_style = outline_value("outline-style");
    if (!outline_style.empty()) {
        result.outline.automatic = false;
        if (outline_style == "inherit") {
            result.outline.style = inherited.outline.style; result.outline.automatic = inherited.outline.automatic;
        } else if (outline_style == "initial" || outline_style == "unset") result.outline.style = BorderStyle::none;
        else if (outline_style == "auto") result.outline.automatic = true;
        else {
            const auto found = std::find_if(std::begin(styles), std::end(styles), [&](auto& item) { return outline_style == item.first; });
            if (found != std::end(styles)) result.outline.style = found->second;
            else ++result.unsupported;
        }
    }
    const auto outline_length = [&](const std::string& text, float initial, float parent) {
        if (text.empty() || text == "initial" || text == "unset" || text == "medium") return initial;
        if (text == "inherit") return parent;
        if (text == "thin") return 1.0f;
        if (text == "thick") return 5.0f;
        double number = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), number);
        const auto suffix = std::string_view(parsed.ptr, size_t(text.data() + text.size() - parsed.ptr));
        const auto* unit = lxb_css_unit_absolute_relative_by_name(chars(suffix), suffix.size());
        if (parsed.ec != std::errc{} || !std::isfinite(number) || (!unit && (number != 0 || !suffix.empty()))) {
            ++result.unsupported; return initial;
        }
        lxb_css_value_length_percentage_t value{};
        value.type = LXB_CSS_VALUE__LENGTH; value.u.length.num = number;
        value.u.length.unit = static_cast<lxb_css_unit_t>(unit ? unit->unique : LXB_CSS_UNIT_PX);
        const float computed = length(&value, width, result.font_size, result.root_font_size, vw, vh, initial, result.unsupported);
        if (!std::isfinite(computed)) { ++result.unsupported; return initial; }
        return computed;
    };
    result.outline.width = outline_length(outline_value("outline-width"), 3, inherited.outline.width);
    result.outline.offset = outline_length(outline_value("outline-offset"), 0, inherited.outline.offset);
    if(doc.variables&&doc.variables->color(element(node),"outline-color")) {
        result.outline_color=selected_color(doc,node,"outline-color",result.foreground,
            inherited.outline_color,result.foreground,result.dark);
        result.outline.color=csscolor::argb(result.outline_color);
    }
    if (!result.outline.automatic && result.outline.style == BorderStyle::none) result.outline.width = 0;
    const auto overflow = [&](uintptr_t name, Overflow parent) {
        const auto* value = property<lxb_css_property_overflow_x_t>(node, name);
        if (!value) return Overflow::visible;
        if (value->type == LXB_CSS_VALUE_INHERIT) return parent;
        if (value->type == LXB_CSS_VALUE_HIDDEN) return Overflow::hidden;
        if (value->type == LXB_CSS_VALUE_SCROLL) return Overflow::scroll;
        if (value->type == LXB_CSS_VALUE_AUTO) return Overflow::automatic;
        if (value->type == LXB_CSS_VALUE_CLIP) return Overflow::clip;
        return Overflow::visible;
    };
    result.overflow_x = overflow(LXB_CSS_PROPERTY_OVERFLOW_X, inherited.overflow_x);
    result.overflow_y = overflow(LXB_CSS_PROPERTY_OVERFLOW_Y, inherited.overflow_y);
    if (result.text_control) result.overflow_x = result.overflow_y = Overflow::hidden;
    if (scrollable(result.overflow_x) || scrollable(result.overflow_y)) {
        const auto coupled = [](Overflow value) {
            return value == Overflow::visible ? Overflow::automatic : value == Overflow::clip ? Overflow::hidden : value;
        };
        result.overflow_x = coupled(result.overflow_x); result.overflow_y = coupled(result.overflow_y);
    }
    // Ordinary shorthands have already been expanded. Remaining declarations
    // here require features such as variables or cascade rollback.
    for (const auto* name : {"border", "border-top", "border-right", "border-bottom", "border-left", "border-width",
                             "border-style", "border-color", "outline"})
        if (lxb_dom_element_style_by_name(element(node), chars(name), std::char_traits<char>::length(name)))
            ++result.unsupported;
    if(doc.variables) {
        cssbackground::Style background;
        static const cssbackground::Style defaults;
        for(size_t i=0;i<std::size(cssbackground::properties);++i)if(auto value=doc.variables->background(element(node),cssbackground::properties[i])) {
            const auto& source=value->wide=="inherit"&&inherited.background_style?*inherited.background_style:
                value->wide.empty()?value->style:defaults;
            cssbackground::assign(background,source,i);
        }
        if(!cssbackground::initial(background)) {
            result.background_style=std::make_shared<cssbackground::Style>(std::move(background));
            if(std::any_of(result.background_style->images.begin(),result.background_style->images.end(),[](const auto& image){return bool(image);})||
                    result.background_style->clips.back()!=cssbackground::Box::border) {
                auto paint=std::make_shared<cssbackground::Paint>();paint->style=*result.background_style;
                paint->environment={result.font_size,result.root_font_size,double(vw),double(vh),{result.foreground,result.dark}};
                paint->padding={result.padding_top,result.padding_right,result.padding_bottom,result.padding_left};
                paint->border={result.borders[0].width,result.borders[1].width,result.borders[2].width,result.borders[3].width};result.background_paint=std::move(paint);
            }
        }
    }
    if(doc.variables) {
        for(size_t i=0;i<std::size(cssposition::properties);++i)if(auto value=doc.variables->position(element(node),cssposition::properties[i])) {
            const auto& source=value->wide=="inherit"?inherited.position:value->wide.empty()?value->style:cssposition::Style{};
            if(i==0)result.position.kind=source.kind;
            else if(i<=4) {
                result.position.inset[i-1]=source.inset[i-1];
                result.position.percentages[i-1]=source.percentages[i-1];
                if(value->wide.empty()&&result.position.inset[i-1]) {
                    auto& inset=*result.position.inset[i-1];
                    const auto percent=inset.percent;inset.percent=0;
                    inset=cssbackground::Length{cssbackground::resolve(inset,0,{result.font_size,result.root_font_size,double(vw),double(vh)}),percent};
                }
            }else if(i==5)result.position.z=source.z;
            else result.position.isolate=source.isolate;
        }
        const auto shift=cssposition::relative(result.position,width,containing_height,inherited.rtl);
        result.relative_x=shift.first;result.relative_y=shift.second;
        if(result.position.kind==cssposition::Kind::sticky)++result.unsupported;
    }
    result.replaced=doc.image_node(node)||node->local_name==LXB_TAG_CANVAS;
    if(result.replaced)result.inline_atomic=result.inline_flow;
    result.radii=radius_style(node,inherited.radii,result.font_size,result.root_font_size,vw,vh,result.unsupported);
    for (const auto* name : {"box-decoration-break", "border-image"}) {
        const auto* declaration =
            lxb_dom_element_style_by_name(element(node), chars(name), std::char_traits<char>::length(name));
        if (!declaration || declaration->type != LXB_CSS_PROPERTY__CUSTOM) continue;
        const auto& value = declaration->u.custom->value;
        const std::string_view raw(reinterpret_cast<const char*>(value.data), value.length);
        if (raw != "initial" && raw != "unset" && raw != "none" && raw != "slice" && raw != "0" && raw != "0px")
            ++result.unsupported;
    }
    if(result.replaced&&node->local_name!=LXB_TAG_CANVAS)for(size_t axis=0;axis<2;++axis) {
        if(lxb_dom_element_style_by_id(element(node),dimension_properties[axis]))continue;
        const auto raw=doc.attribute(doc.identify(node),axis?"height":"width",false);if(raw.empty())continue;
        auto parsed=cssbackground::parse_length(raw);
        if(!parsed) {
            float value=0;const auto number=std::from_chars(raw.data(),raw.data()+raw.size(),value);
            if(number.ec==std::errc{}&&number.ptr==raw.data()+raw.size()&&value>=0)parsed=cssbackground::Length{value};
        }
        if(parsed&&parsed->px>=0&&parsed->percent>=0) {
            const auto resolved=float(cssbackground::resolve(*parsed,axis?containing_height:width,{result.font_size,result.root_font_size,double(vw),double(vh)}));
            result.sizes[axis]={parsed->percent?SizeKind::percentage:SizeKind::length,parsed->percent?float(parsed->percent):resolved};
            if(axis){result.height=resolved;result.height_definite=std::isfinite(resolved);}
            else result.width=resolved;
        }
    }
    return result;
}
} // namespace xl::detail
