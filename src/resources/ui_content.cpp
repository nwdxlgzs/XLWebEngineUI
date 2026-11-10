#include "ui_content.h"
#include <algorithm>
#include <optional>

namespace xl::detail {
namespace {
constexpr std::string_view prefix="xl-webengine-ui://";
bool alpha(char c) { return (c>='a' && c<='z') || (c>='A' && c<='Z'); }
bool digit(char c) { return c>='0' && c<='9'; }
bool unreserved(char c) { return alpha(c) || digit(c) || c=='-' || c=='_' || c=='.' || c=='~'; }
int hex(char c) { return digit(c)?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1; }
char lower(char c) { return c>='A'&&c<='Z'?char(c-'A'+'a'):c; }
std::string normalize(std::string_view input,bool path) {
    std::string output;
    constexpr char digits[]="0123456789ABCDEF";
    for(size_t i=0;i<input.size();++i) {
        auto c=static_cast<unsigned char>(input[i]);
        if(c<=0x20 || c>=0x7f || c=='\\')throw UriFailure{XL_INVALID_ARGUMENT};
        if(c=='%') {
            if(i+2>=input.size() || hex(input[i+1])<0 || hex(input[i+2])<0)throw UriFailure{XL_INVALID_ARGUMENT};
            c=static_cast<unsigned char>(hex(input[i+1])*16+hex(input[i+2]));i+=2;
            if(c==0 || c<0x20 || c==0x7f || c=='\\' || (path && c=='/'))throw UriFailure{XL_INVALID_ARGUMENT};
            if(unreserved(char(c)))output+=char(c);
            else {output+='%';output+=digits[c>>4];output+=digits[c&15];}
        } else if(unreserved(char(c)) || std::string_view("!$&'()*+,;=:@/").find(char(c))!=std::string_view::npos || (!path && c=='?'))output+=char(c);
        else throw UriFailure{XL_INVALID_ARGUMENT};
    }
    return output;
}
struct Parts {
    std::string authority,path;
    std::optional<std::string> query,fragment;
    bool absolute=false,has_authority=false;
};
Parts parse(std::string_view text) {
    if(text.size()>16384 || text.find('\0')!=std::string_view::npos)throw UriFailure{XL_INVALID_ARGUMENT};
    Parts result;
    const auto hash=text.find('#');
    if(hash!=std::string_view::npos){result.fragment=normalize(text.substr(hash+1),false);text=text.substr(0,hash);}
    const auto question=text.find('?');
    if(question!=std::string_view::npos){result.query=normalize(text.substr(question+1),false);text=text.substr(0,question);}
    const auto colon=text.find(':');
    if(colon!=std::string_view::npos && colon<text.find('/')) {
        auto scheme=std::string(text.substr(0,colon));
        std::transform(scheme.begin(),scheme.end(),scheme.begin(),lower);
        if(scheme!="xl-webengine-ui")throw UriFailure{XL_UNSUPPORTED};
        result.absolute=true;text.remove_prefix(colon+1);
        if(!text.starts_with("//"))throw UriFailure{XL_INVALID_ARGUMENT};
    }
    if(text.starts_with("//")) {
        result.has_authority=true;text.remove_prefix(2);
        const auto slash=text.find('/');
        result.authority=std::string(text.substr(0,slash));
        if(result.authority.empty() || result.authority.size()>128)throw UriFailure{XL_INVALID_ARGUMENT};
        for(auto& c:result.authority) {
            if(!alpha(c) && !digit(c) && c!='-' && c!='_' && c!='.')throw UriFailure{XL_INVALID_ARGUMENT};
            c=lower(c);
        }
        text=slash==std::string_view::npos?std::string_view{}:text.substr(slash);
    }
    result.path=normalize(text,true);
    return result;
}
std::string path_normalized(const std::string& path) {
    std::vector<std::string_view> segments;
    const bool trailing=path.ends_with('/') || path.ends_with("/.") || path.ends_with("/..");
    for(size_t offset=1;offset<=path.size();) {
        const auto slash=path.find('/',offset);
        const auto segment=std::string_view(path).substr(offset,slash==std::string::npos?path.size()-offset:slash-offset);
        if(segment=="..") { if(!segments.empty())segments.pop_back(); }
        else if(segment!=".")segments.push_back(segment);
        if(slash==std::string::npos)break;
        offset=slash+1;
    }
    std::string result="/";
    for(size_t i=0;i<segments.size();++i) {if(i)result+='/';result+=segments[i];}
    if(trailing && !result.ends_with('/'))result+='/';
    return result;
}
}
std::string resolve_ui_uri(std::string_view base,std::string_view reference) {
    auto next=parse(reference);
    if(!next.absolute) {
        auto parent=parse(base);
        if(!parent.absolute || !parent.has_authority)throw UriFailure{XL_INVALID_ARGUMENT};
        if(!next.has_authority) {
            next.authority=std::move(parent.authority);
            if(next.path.empty()) {
                next.path=parent.path;
                if(!next.query)next.query=std::move(parent.query);
            } else if(!next.path.starts_with('/')) {
                const auto slash=parent.path.rfind('/');
                next.path=(slash==std::string::npos?"/":parent.path.substr(0,slash+1))+next.path;
            }
        }
    }
    if(next.authority.empty())throw UriFailure{XL_INVALID_ARGUMENT};
    auto result=std::string(prefix)+next.authority+path_normalized(next.path.empty()?"/":next.path);
    if(next.query)result+='?'+*next.query;
    if(next.fragment)result+='#'+*next.fragment;
    return result;
}
std::string ui_resource_key(std::string_view canonical) {
    return std::string(canonical.substr(0,canonical.find_first_of("?#")));
}
bool ui_mime_is(std::string_view mime,std::string_view expected) {
    mime=mime.substr(0,mime.find(';'));
    while(!mime.empty() && mime.front()==' ')mime.remove_prefix(1);
    while(!mime.empty() && mime.back()==' ')mime.remove_suffix(1);
    return mime.size()==expected.size() && std::equal(mime.begin(),mime.end(),expected.begin(),
        [](char a,char b){return lower(a)==lower(b);});
}
void UiMounts::mount(std::string_view uri,std::shared_ptr<const UiContent> content) {
    entries_.insert_or_assign(ui_resource_key(resolve_ui_uri({},uri)),std::move(content));++revision_;
}
bool UiMounts::unmount(std::string_view uri) {
    const auto erased=entries_.erase(ui_resource_key(resolve_ui_uri({},uri)));if(erased)++revision_;return erased!=0;
}
std::shared_ptr<const UiContent> UiMounts::open(std::string_view uri) {
    const auto key=ui_resource_key(resolve_ui_uri({},uri));
    const auto found=entries_.find(key);
    if(found!=entries_.end())return found->second;
    if(!loader_)throw UriFailure{XL_NOT_FOUND};
    if(loading_)throw UriFailure{XL_BUSY};
    struct Active { bool& flag; Active(bool& f):flag(f){flag=true;} ~Active(){flag=false;} } active{loading_};
    xl_content* handle=nullptr;
    const auto status=loader_(context_,key.c_str(),&handle);
    std::unique_ptr<xl_content> owner(handle);
    if(status!=XL_OK)throw UriFailure{status};
    if(!owner || !owner->value)throw UriFailure{XL_INTERNAL_ERROR};
    auto value=owner->value;
    entries_.emplace(key,value);++revision_;
    return value;
}
}
