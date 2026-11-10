#include "dom_internal.h"
#include "style/media_query.h"
#include <algorithm>

namespace xl::detail {
namespace {
bool rel_contains(std::string text,std::string_view token) {
    for(auto& c:text)if(c>='A' && c<='Z')c=char(c-'A'+'a');
    size_t first=0;
    while(first<text.size()) {
        const auto end=text.find_first_of(" \t\r\n\f",first);
        if(std::string_view(text).substr(first,end==std::string::npos?text.size()-first:end-first)==token)return true;
        if(end==std::string::npos)break;
        first=end+1;
    }
    return false;
}
}
std::string Document::Impl::base_uri() {
    const auto base=query("base[href]");
    if(!base)return uri;
    try { return resolve_ui_uri(uri,attribute(base,"href",false)); }
    catch(const UriFailure&) { return uri; }
}
void Document::Impl::sync_linked_styles(const std::vector<lxb_dom_element_t*>& elements) {
    link_media.clear();
    auto* sheets=dom->dom_document.css->stylesheets;
    // All element cascade references have already been removed. Rebuild the
    // sheet order from DOM order so linked and inline sheets interleave.
    std::unordered_map<lxb_dom_node_t*,lxb_css_stylesheet_t*> inline_sheets;
    std::vector<lxb_css_stylesheet_t*> other;
    std::vector<lxb_css_stylesheet_t*> previous;
    for(size_t index=0;index<lexbor_array_length(sheets);++index)
        previous.push_back(static_cast<lxb_css_stylesheet_t*>(lexbor_array_get(sheets,index)));
    lexbor_array_clean(sheets);
    for(auto* sheet:previous) {
        if(linked_sheets.contains(sheet))lxb_css_stylesheet_destroy(sheet,false);
        else if(sheet->element)inline_sheets[lxb_dom_interface_node(sheet->element)]=sheet;
        else other.push_back(sheet);
    }
    linked_sheets.clear();
    for(auto* sheet:other)check_lxb(lxb_dom_document_stylesheet_add(&dom->dom_document,sheet));
    const auto base=base_uri();
    for(auto* target:elements) {
        auto* node=lxb_dom_interface_node(target);
        if(!connected(node))continue;
        const auto existing=inline_sheets.find(node);
        if(existing!=inline_sheets.end()) {
            check_lxb(lxb_dom_document_stylesheet_add(&dom->dom_document,existing->second));
            inline_sheets.erase(existing);continue;
        }
        if(!mounts || node->local_name!=LXB_TAG_LINK || lxb_dom_element_has_attribute(target,chars("disabled"),8))continue;
        const auto id=identify(node);
        const auto rel=attribute(id,"rel",false);
        if(!rel_contains(rel,"stylesheet") || rel_contains(rel,"alternate"))continue;
        auto query=std::make_shared<MediaQuery>(attribute(id,"media",false));const auto matches=query->matches(environment);link_media.emplace_back(std::move(query),matches);
        if(!matches)continue;
        const auto type=attribute(id,"type",false);
        if(!type.empty() && !ui_mime_is(type,"text/css"))continue;
        std::shared_ptr<const UiContent> content;
        try { content=mounts->open(resolve_ui_uri(base,attribute(id,"href",false))); }
        catch(const UriFailure&){continue;}
        if(!ui_mime_is(content->mime,"text/css"))continue;
        if(content->bytes.size()>16*1024*1024)continue;
        auto* sheet=lxb_css_stylesheet_create(dom->dom_document.css->memory);
        if(!sheet)throw std::bad_alloc();
        auto* parser=lxb_css_parser_create();
        if(!parser){lxb_css_stylesheet_destroy(sheet,false);throw std::bad_alloc();}
        struct Cleanup {lxb_css_parser_t* p;~Cleanup(){lxb_css_parser_destroy(p,true);}} cleanup{parser};
        try {
            check_lxb(lxb_css_parser_init(parser,nullptr));
            const auto* bytes=content->bytes.empty()?chars(""):reinterpret_cast<const lxb_char_t*>(content->bytes.data());
            check_lxb(lxb_css_stylesheet_parse(sheet,parser,bytes,content->bytes.size()));
            sheet->element=target;
            linked_sheets.emplace(sheet,content);
            check_lxb(lxb_dom_document_stylesheet_add(&dom->dom_document,sheet));
        } catch(...) {
            linked_sheets.erase(sheet);lxb_css_stylesheet_destroy(sheet,false);throw;
        }
    }
    // Keep detached sheets allocated; refresh_styles skips applying them.
    for(const auto& [node,sheet]:inline_sheets) {
        (void)node;check_lxb(lxb_dom_document_stylesheet_add(&dom->dom_document,sheet));
    }
}
}
