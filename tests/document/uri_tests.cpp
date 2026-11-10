#include <XLWebEngineUI/xl.hpp>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>

#define CHECK(value) do {if(!(value)){std::cerr<<__LINE__<<": " #value "\n";std::exit(1);}}while(false)
namespace {
std::string style(xl::Document& doc,xl_node_id node,const char* property) {
    size_t size;const auto status=xl_document_get_style(doc.get(),node,property,nullptr,0,&size);
    CHECK(status==XL_OK || status==XL_BUFFER_TOO_SMALL);
    std::string result(size,'\0');CHECK(xl_document_get_style(doc.get(),node,property,result.data(),result.size(),&size)==XL_OK);return result;
}
std::string evaluate(xl::Document& doc,const char* script) {CHECK(doc.evaluate(script)==XL_OK);return doc.result();}
}
int main() {
    const char* base="xl-webengine-ui://app/a/b/page.html?old#fragment";
    for(const auto& [reference,expected]:std::vector<std::pair<const char*,const char*>>{
        {"../theme.css","xl-webengine-ui://app/a/theme.css"},
        {"./x/../theme.css","xl-webengine-ui://app/a/b/theme.css"},
        {"/root.txt","xl-webengine-ui://app/root.txt"},
        {"//SHARED/root.txt","xl-webengine-ui://shared/root.txt"},
        {"?new#id","xl-webengine-ui://app/a/b/page.html?new#id"},
        {"#next","xl-webengine-ui://app/a/b/page.html?old#next"},
        {"","xl-webengine-ui://app/a/b/page.html?old"},
        {"../../../file","xl-webengine-ui://app/file"},
        {"%2e%2E/%74heme.css","xl-webengine-ui://app/a/theme.css"},
        {"a%20b.txt","xl-webengine-ui://app/a/b/a%20b.txt"},
        {"XL-WEBENGINE-UI://APP/x","xl-webengine-ui://app/x"}})
        CHECK(xl::resolve_ui_uri(base,reference)==expected);
    for(const char* uri:{"http://host/a","https://host/a","file:///C:/secret","data:text/html,A","javascript:evil()"}) {
        size_t size;CHECK(xl_resolve_ui_uri(base,uri,nullptr,0,&size)==XL_UNSUPPORTED);
    }
    for(const char* uri:{"xl-webengine-ui://","xl-webengine-ui://a:123/a","xl-webengine-ui://user@a/a",
            "xl-webengine-ui://a/a%2fb","xl-webengine-ui://a/a%5Cb","xl-webengine-ui://a/%00","xl-webengine-ui://a/%zz",
            "xl-webengine-ui://a/a\\b","xl-webengine-ui:/a","xl-webengine-ui://a/a b"}) {
        size_t size;CHECK(xl_resolve_ui_uri(base,uri,nullptr,0,&size)==XL_INVALID_ARGUMENT);
    }
    char sentinel='Z';size_t size;
    CHECK(xl_resolve_ui_uri(base,"theme.css",&sentinel,1,&size)==XL_BUFFER_TOO_SMALL && sentinel=='Z');
    xl::Content survivor;
    {
        xl::Runtime first,second;
        xl::Content shared("<!doctype html><link rel=stylesheet href=theme.css><p id=p>Shared</p><script>globalThis.accidental=1</script>","Text/Html; charset=utf-8");
        xl::Content css_a("p{width:42px}","text/css"),css_b("p{width:64px}","Text/CSS; charset=utf-8");
        first.mount("xl-webengine-ui://app/one/page.html",shared);
        first.mount("xl-webengine-ui://app/two/page.html",shared);
        second.mount("xl-webengine-ui://app/one/page.html",shared);
        first.mount("xl-webengine-ui://app/one/theme.css",css_a);
        first.mount("xl-webengine-ui://app/two/theme.css",css_b);
        second.mount("xl-webengine-ui://app/one/theme.css",css_b);
        auto one=first.content("xl-webengine-ui://app/one/page.html?v=1#title");
        auto two=first.content("xl-webengine-ui://app/two/page.html");
        auto other=second.content("xl-webengine-ui://app/one/page.html");
        CHECK(shared.data().data()==one.data().data() && one.data().data()==two.data().data() && two.data().data()==other.data().data());
        CHECK(one.mime()=="Text/Html; charset=utf-8");
        survivor=one;
        xl::Document a(first),b(first),c(second);
        a.load("xl-webengine-ui://app/one/page.html?v=1#title");
        b.load("xl-webengine-ui://app/two/page.html");
        c.load("xl-webengine-ui://app/one/page.html");
        CHECK(style(a,a.query("p"),"width")=="42px" && style(b,b.query("p"),"width")=="64px" && style(c,c.query("p"),"width")=="64px");
        CHECK(a.uri()=="xl-webengine-ui://app/one/page.html?v=1#title");
        CHECK(evaluate(a,"document.URL") == a.uri());
        CHECK(evaluate(a,"document.body.baseURI") == a.uri());
        CHECK(evaluate(a,"typeof globalThis.accidental") == "undefined");
        CHECK(evaluate(a,"xl.resolveURI('../two/theme.css')") == "xl-webengine-ui://app/two/theme.css");
        CHECK(evaluate(a,"xl.readText('theme.css')") == "p{width:42px}");
        CHECK(evaluate(a,"document.querySelector('link').href") == "xl-webengine-ui://app/one/theme.css");
        const auto old_node=a.query("p");const auto old_uri=a.uri();
        CHECK(xl_document_load_uri(a.get(),"xl-webengine-ui://app/missing")==XL_NOT_FOUND);
        CHECK(xl_document_load_uri(a.get(),"http://host")==XL_UNSUPPORTED);
        CHECK(a.query("p")==old_node && a.uri()==old_uri);
        CHECK(xl_document_load_html_at(a.get(),"X",1,"file:///etc/passwd")==XL_UNSUPPORTED);
        CHECK(a.query("p")==old_node);
        first.mount("xl-webengine-ui://app/one/theme.css",css_b);
        CHECK(style(a,old_node,"width")=="64px");
        CHECK(style(c,c.query("p"),"width")=="64px");
        first.unmount("xl-webengine-ui://app/one/theme.css");
        CHECK(evaluate(a,"(()=>{try{xl.readText('theme.css');return 'wrong'}catch(e){return 'missing'}})()") == "missing");
        CHECK(style(b,b.query("p"),"width")=="64px");
        CHECK(style(c,c.query("p"),"width")=="64px");
        a.html("<style>p{width:8px}</style><link rel=stylesheet href=theme.css><p>A</p>","xl-webengine-ui://app/two/index.html");
        CHECK(style(a,a.query("p"),"width")=="64px");
        a.html("<link rel=stylesheet href=theme.css><style>p{width:8px}</style><p>A</p>","xl-webengine-ui://app/two/index.html");
        CHECK(style(a,a.query("p"),"width")=="8px");
        a.html("<style>p{width:8px}</style><link id=sheet rel=stylesheet href=theme.css media=print><p>A</p>","xl-webengine-ui://app/two/index.html");
        CHECK(style(a,a.query("p"),"width")=="8px");
        CHECK(evaluate(a,"document.querySelector('link').setAttribute('media','screen');'ok'")=="ok");
        CHECK(style(a,a.query("p"),"width")=="64px");
        CHECK(evaluate(a,"document.querySelector('link').setAttribute('disabled','');'ok'")=="ok");
        CHECK(style(a,a.query("p"),"width")=="8px");
        a.html("<style>p{width:8px}</style><link rel='alternate stylesheet' href=theme.css><p>A</p>","xl-webengine-ui://app/two/index.html");
        CHECK(style(a,a.query("p"),"width")=="8px");
        a.html("<base href='../two/'><link rel=stylesheet href=theme.css><p>A</p>","xl-webengine-ui://app/one/index.html");
        CHECK(style(a,a.query("p"),"width")=="64px");
        CHECK(evaluate(a,"document.baseURI")=="xl-webengine-ui://app/two/");
        a.html("<p>A</p>","xl-webengine-ui://app/synthetic/a.html");
        CHECK(a.uri()=="xl-webengine-ui://app/synthetic/a.html");
        a.html("<p>A</p>");CHECK(a.uri()=="xl-webengine-ui://app/");
        CHECK(evaluate(a,"document.querySelectorAll('p, p').length")=="1");
        struct Loader {xl_runtime* runtime;const xl::Content* content;int calls=0;} loader{first.get(),&shared};
        first.content_loader([](void* context,const char* uri,xl_content** result)->xl_status {
            auto& value=*static_cast<Loader*>(context);++value.calls;
            CHECK(xl_runtime_check_thread(value.runtime)==XL_BUSY);
            CHECK(xl_runtime_set_content_loader(value.runtime,nullptr,nullptr)==XL_BUSY);
            CHECK(xl_runtime_destroy(value.runtime)==XL_BUSY);
            if(std::strcmp(uri,"xl-webengine-ui://app/dynamic/page.html")!=0)return XL_NOT_FOUND;
            return xl_content_clone(value.content->get(),result);
        },&loader);
        auto lazy=first.content("xl-webengine-ui://app/dynamic/page.html?test");
        CHECK(loader.calls==1 && lazy.data().data()==shared.data().data());
        CHECK(first.content("xl-webengine-ui://app/dynamic/page.html").data().data()==lazy.data().data() && loader.calls==1);
        xl_content* absent=nullptr;
        CHECK(xl_runtime_open_content(second.get(),"xl-webengine-ui://app/dynamic/page.html",&absent)==XL_NOT_FOUND);
        first.unmount("xl-webengine-ui://app/dynamic/page.html");
        first.content_loader(nullptr);
        CHECK(xl_runtime_open_content(first.get(),"xl-webengine-ui://app/dynamic/page.html",&absent)==XL_NOT_FOUND);
        CHECK(lazy.data().data()==shared.data().data());
        std::atomic<int> reads=0;
        std::vector<std::thread> workers;
        for(int i=0;i<4;++i)workers.emplace_back([&]{for(int j=0;j<50;++j){xl::Content copy(shared);CHECK(copy.data().data()==shared.data().data());++reads;}});
        for(auto& worker:workers)worker.join();CHECK(reads==200);
        std::thread wrong([&]{CHECK(xl_runtime_mount_content(first.get(),"xl-webengine-ui://app/wrong",shared.get())==XL_WRONG_THREAD);});wrong.join();
        first.unmount("xl-webengine-ui://app/one/page.html");CHECK(one.data().size()==shared.data().size());
        xl_content* missing=nullptr;CHECK(xl_runtime_open_content(first.get(),"xl-webengine-ui://app/one/page.html",&missing)==XL_NOT_FOUND && !missing);
    }
    CHECK(!survivor.data().empty() && survivor.mime()=="Text/Html; charset=utf-8");
    xl_content* empty=nullptr;CHECK(xl_content_create(nullptr,0,nullptr,&empty)==XL_OK);xl_content_release(empty);
    std::cout<<"Internal URI resolution, isolated mounts, shared immutable storage, source locations, linked CSS and JS passed\n";
}
