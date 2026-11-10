#include <XLWebEngineUI/xl.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <thread>
#include <vector>
#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
int main() {
    xl::Runtime runtime; xl::Document document(runtime);
    const auto page = [&](const std::string& html) { document.html("<style>body{margin:0;font:16px/20px test}</style>" + html); };
    const auto expect = [&](const char* selector, const char* pseudo, const std::string& expected, const std::source_location& at = std::source_location::current()) {
        const auto actual = document.pseudo_style(document.query(selector),pseudo,"content");
        if (actual != expected) { std::cerr << at.line() << ' ' << selector << pseudo << " expected " << expected << " got " << actual << '\n'; std::exit(1); }
    };
    const auto script = [&](const char* source, const char* expected) { CHECK(document.evaluate(source) == XL_OK); if(document.result()!=expected) {std::cerr << source << " got " << document.result() << '\n';std::exit(1);} };
    page("<style>body{counter-reset:n 4}.item{counter-increment:n}.item::before{content:counter(n) '. '}</style><div class=item id=a>A</div><div class=item id=b>B</div>");
    expect("#a","::before","\"5. \""); expect("#b","::before","\"6. \"");
    xl_resource_stats resources{}; CHECK(xl_runtime_resource_stats(runtime.get(),&resources,sizeof(resources)) == XL_OK); CHECK(resources.decompressions == 0 && resources.worker_threads == 0);
    script("document.getElementById('a').textContent+':'+document.getElementById('a').childNodes.length+':'+document.querySelectorAll('span').length", "A:1:0");
    script("getComputedStyle(document.getElementById('a'),'::before').getPropertyValue('counter-reset')", "none");

    page("<style>body{counter-reset:section}h1{counter-increment:section;counter-reset:sub}h2{counter-increment:sub}h1::before{content:counter(section)}h2::before{content:counter(section) '.' counter(sub)}</style><h1 id=a>A</h1><h2 id=b>B</h2><h2 id=c>C</h2><h1 id=d>D</h1><h2 id=e>E</h2>");
    expect("#a","::before","\"1\"");expect("#b","::before","\"1.1\"");expect("#c","::before","\"1.2\"");expect("#d","::before","\"2\"");expect("#e","::before","\"2.1\"");
    page("<style>ol{counter-reset:chapter}li{counter-increment:chapter}li::before{content:counters(chapter,'.')}</style><ol><li id=a>A<ol><li id=b>B</li><li id=c>C</li></ol></li><li id=d>D</li></ol>");
    expect("#a","::before","\"1\"");expect("#b","::before","\"1.1\"");expect("#c","::before","\"1.2\"");expect("#d","::before","\"2\"");
    page("<style>body{counter-reset:n}.inc{counter-increment:n}#b{counter-set:n 8}#c{counter-increment:n 1 n 2}div::before{content:counters(n,'.')}</style><section><i class=inc></i></section><div id=a></div><div id=b></div><div id=c></div>");
    expect("#a","::before","\"1\"");expect("#b","::before","\"8\"");expect("#c","::before","\"11\"");
    page("<style>body{counter-reset:n 3 n 8}#x{counter-set:n 4 n 9;counter-increment:n 5}#x::before{content:counter(n)}</style><div id=x></div>"); expect("#x","::before","\"9\"");
    page("<style>#host{counter-reset:n;display:flex}.item{counter-increment:n}.item::before{content:counter(n)}#b{order:-1}#a::after{content:counter(n);counter-increment:n}</style><div id=host><div class=item id=a>A</div><div class=item id=b>B</div></div>");
    expect("#a","::before","\"1\"");expect("#a","::after","\"2\"");expect("#b","::before","\"3\"");
    page("<style>body{counter-reset:n}.inc{counter-increment:n}.inc::before{content:counter(n)}#a{display:none}#b::before{counter-increment:n;display:none}</style><div class=inc id=a></div><div id=b></div><div hidden class=inc></div><details><summary>S</summary><div class=inc></div></details><div class=inc id=c aria-hidden=true></div><div class=inc id=d></div>");
    expect("#c","::before","\"1\"");expect("#d","::before","\"2\"");
    page("<style>body{counter-reset:reversed(n)}.item{counter-increment:n -1}.item::before{content:counter(n)}</style><div class=item id=a></div><div class=item id=b></div><div class=item id=c></div>");
    expect("#a","::before","\"3\"");expect("#b","::before","\"2\"");expect("#c","::before","\"1\"");
    script("document.getElementById('b').style.setProperty('display','none');'hidden'","hidden");expect("#a","::before","\"2\"");expect("#c","::before","\"1\"");
    page("<style>body{counter-reset:reversed(n) 10}.item{counter-increment:n}.item::before{content:counter(n)}</style><div class=item id=a></div><div class=item id=b></div>");
    expect("#a","::before","\"11\"");expect("#b","::before","\"12\"");
    page("<style>body{counter-reset:reversed(n)}#a{counter-increment:n -2}#b{counter-increment:n -1}#c{counter-increment:n -3}div::before{content:counter(n)}</style><div id=a></div><div id=b></div><div id=c></div>");
    expect("#a","::before","\"7\"");expect("#b","::before","\"6\"");expect("#c","::before","\"3\"");
    page("<style>li::before{content:counters(list-item,'.')}</style><ol start=4><li id=a></li><li id=b value=9><ol><li id=c></li></ol></li><li id=d></li></ol>");
    expect("#a","::before","\"4\"");expect("#b","::before","\"9\"");expect("#c","::before","\"9.1\"");expect("#d","::before","\"10\"");
    page("<style>li::before{content:counter(list-item)}</style><ol reversed><li id=a></li><li id=b></li><li id=c></li></ol><ol reversed start=8><li id=d></li><li id=e value=4></li><li id=f></li></ol>");
    expect("#a","::before","\"3\"");expect("#b","::before","\"2\"");expect("#c","::before","\"1\"");expect("#d","::before","\"8\"");expect("#e","::before","\"4\"");expect("#f","::before","\"3\"");
    page("<style>body{counter-reset:n 27 N 4}#x::before{content:counter(n,lower-alpha) ' ' counter(n,upper-latin) ' ' counter(n,upper-roman) ' ' counter(N,decimal-leading-zero)}#x::after{content:counter(n,none) counter(missing)}</style><div id=x></div>");
    expect("#x","::before","\"aa AA XXVII 04\"");expect("#x","::after","\"0\"");
    page("<style>body{counter-reset:n -2147483648}#x::before{content:counter(n,lower-roman)}#x::after{content:counter(n);counter-increment:n -1}</style><div id=x></div>");
    expect("#x","::before","\"-2147483648\"");expect("#x","::after","\"-2147483648\"");
    page("<style>body{counter-reset:n 1}#x{counter-increment:n 2147483647 n -1}#x::before{content:counter(n)}</style><div id=x></div>");expect("#x","::before","\"2147483647\"");
    page("<style>body{counter-reset:n 12}#x::before{content:counter(n,arabic-indic)}#x::after{content:counter(n,cjk-decimal)}</style><div id=x></div>");
    expect("#x","::before","\"\xd9\xa1\xd9\xa2\"");expect("#x","::after","\"\xe4\xb8\x80\xe4\xba\x8c\"");

    page("<style>body{counter-reset:n 5}#a{counter-increment:n;counter-increment:n 1.5}#b{counter-increment:n;counter-increment:var(--bad,none extra 1.5)}div::before{content:counter(n)}#c::before{content:'kept';content:counter()}#d::before{content:'kept';content:counters(n,2)}</style><div id=a></div><div id=b></div><div id=c></div><div id=d></div>");
    expect("#a","::before","\"6\"");expect("#b","::before","\"6\"");expect("#c","::before","\"kept\"");expect("#d","::before","\"kept\"");
    page("<style>body{--step:2;counter-reset:n}#x{counter-increment:n var(--step)}#x::before{content:counter(n) / 'Number ' counter(n)}#x::after{content:attr(data-ok,open-quote) open-quote;quotes:'[' ']'}</style><button id=x data-ok=Y>Button</button>");
    expect("#x","::before","\"2\" / \"Number 2\"");expect("#x","::after","\"Y[\"");
    script("document.body.style.setProperty('--step','3');'changed'","changed");expect("#x","::before","\"3\" / \"Number 3\"");

    page("<style>body{quotes:'[' ']' '{' '}'}q::before{color:red}</style><q id=a>outer<q id=b>inner<q id=c>deep</q></q></q><q id=d>next</q>");
    expect("#a","::before","\"[\"");expect("#b","::before","\"{\"");expect("#c","::before","\"{\"");expect("#c","::after","\"}\"");expect("#a","::after","\"]\"");expect("#d","::before","\"[\"");
    script("document.querySelectorAll('q').length+':'+document.getElementById('b').childNodes.length", "4:2");
    page("<style>body{quotes:'[' ']' '{' '}'}#a::before{content:close-quote no-open-quote}#b::before{content:open-quote}#b::after{content:close-quote no-close-quote}#c::before{content:open-quote}</style><div id=a></div><div id=b></div><div id=c></div>");
    expect("#a","::before","\"\"");expect("#b","::before","\"{\"");expect("#b","::after","\"}\"");expect("#c","::before","\"[\"");
    page("<style>body{quotes:'[' ']' '{' '}'}#a::before{content:open-quote;quotes:none}#c{display:none}#c::before{content:open-quote}</style><div id=a></div><q id=b>B</q><div id=c></div><q id=d>D</q>");
    expect("#a","::before","\"\"");expect("#b","::before","\"{\"");expect("#d","::before","\"{\"");
    page("<style>body{quotes:'[' ']'}q{quotes:'bad'}#b{quotes:var(--invalid,'bad')}#c{quotes:initial}#d::before{content:none}</style><q id=a>A</q><q id=b>B</q><q id=c lang=de>C</q><q id=d>D</q>");
    expect("#a","::before","\"[\"");expect("#b","::before","\"[\"");expect("#c","::before","\"\xe2\x80\x9e\"");expect("#d","::before","none");
    page("<style>body{quotes:'[' ']'}#a::before{content:inherit}#b::before{content:none;content:revert}</style><q id=a>A</q><q id=b>B</q>");expect("#a","::before","none");expect("#b","::before","\"[\"");
    page("<q id=a lang=fr>A</q><q id=b lang=ja>B</q><q id=c lang=zh-Hant>C</q><q id=d lang=de-DE>D</q><q id=e lang=unknown>E</q>");
    expect("#a","::before","\"\xc2\xab\"");expect("#a","::after","\"\xc2\xbb\"");expect("#b","::before","\"\xe3\x80\x8c\"");expect("#c","::before","\"\xe3\x80\x8c\"");expect("#d","::before","\"\xe2\x80\x9e\"");expect("#e","::before","\"\xe2\x80\x9c\"");
    document.attribute(document.query("#c"),"lang","zh-TW");expect("#c","::before","\"\xe3\x80\x8c\"");
    CHECK(xl_runtime_resource_stats(runtime.get(),&resources,sizeof(resources)) == XL_OK); CHECK(resources.decompressions == 0 && resources.worker_threads == 0);

    std::ifstream input(XL_TEST_FONT,std::ios::binary); const std::vector<char> font{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()}; CHECK(!font.empty());
    CHECK(xl_document_add_font(document.get(),"test",font.data(),font.size()) == XL_OK);
    xl_view_options options;CHECK(xl_view_options_init(&options,sizeof(options)) == XL_OK);options.kind=XL_VIEW_OFFSCREEN;options.width=240;options.height=100;
    xl::View view(runtime,&options);view.document(&document);view.ax_updates(true);
    const auto render = [&] { const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10); uint32_t flags;xl_status status;while((status=xl_view_render(view.get(),&flags)) == XL_BUSY){CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}CHECK(status==XL_OK);return flags; };
    const auto names = [&] {size_t count=0;CHECK(xl_view_ax_count(view.get(),&count)==XL_OK);std::vector<std::pair<xl_ax_node,std::string>> result;for(size_t i=0;i<count;++i){xl_ax_node node{};CHECK(xl_view_ax_get(view.get(),i,&node,sizeof(node))==XL_OK);size_t size=0;const auto status=xl_view_ax_name(view.get(),i,nullptr,0,&size);CHECK(status==XL_OK||status==XL_BUFFER_TOO_SMALL);std::string name(size,'\0');CHECK(xl_view_ax_name(view.get(),i,name.data(),name.size(),&size)==XL_OK);result.emplace_back(node,name);}return result;};
    const auto find_name = [&](const std::string& name,uint32_t role=XL_AX_TEXT){for(const auto& [node,text]:names())if(text==name&&node.role==role)return node;std::cerr<<"missing AX "<<name<<'\n';std::exit(1);};
    const auto pixels = [&] {std::vector<unsigned char> bytes(240*100*4);CHECK(xl_view_copy_rgba(view.get(),bytes.data(),bytes.size(),240*4)==XL_OK);return bytes;};
    page("<style>body{counter-reset:n 8}button{counter-increment:n}button::before{content:counter(n) '. ';color:red}</style><button id=x>Body</button>");render();
    const auto number=find_name("9. ");find_name("9. Body",XL_AX_BUTTON);const auto old_pixels=pixels();CHECK(number.bounds.width>0);CHECK(view.render()==0);
    xl::AXUpdate old,batch;while(view.take_ax_update(batch))old=std::move(batch);CHECK(old);
    script("document.body.style.setProperty('counter-reset','n 9');'changed'","changed");render();CHECK(find_name("10. ").node==number.node);find_name("10. Body",XL_AX_BUTTON);CHECK(old_pixels!=pixels());CHECK(view.render()==0);
    bool retained=false;for(size_t i=0;i<old.info().node_count;++i)retained|=old.name(i)=="9. ";CHECK(retained);
    page("<style>body{counter-reset:n 2}button::before{content:counter(n) / 'Read ' counter(n)}</style><button id=x>Body</button>");render();find_name("Read 2Body",XL_AX_BUTTON);
    page("<style>body{quotes:'[' ']'}q::before{color:red}q::after{color:blue}</style><q id=x>Body</q>");render();const auto open=find_name("["),body=find_name("Body"),close=find_name("]");CHECK(open.bounds.x<body.bounds.x&&body.bounds.x<close.bounds.x);
    {xl::Runtime another;xl::Document isolated(another);isolated.html("<q id=x>X</q><div id=y style='counter-increment:n'></div><style>#y::before{content:counter(n)}</style>");CHECK(isolated.pseudo_style(isolated.query("#x"),"::before","content")=="\"\xe2\x80\x9c\"");CHECK(isolated.pseudo_style(isolated.query("#y"),"::before","content")=="\"1\"");}
    script("document.getElementById('x').remove();'removed'","removed");render();for(const auto& [node,name]:names()){(void)node;CHECK(name!="Body");}
    std::cout<<"generated counters, scope, reversed/list-item, quotes/locale, lazy data, pixels and AX passed\n";
}
