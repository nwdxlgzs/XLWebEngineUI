#include <XLWebEngineUI/xl.hpp>
#include "text/selection.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#define CHECK(x) do {if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}}while(false)
namespace {
void render(xl::View& view) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    uint32_t flags=0;
    xl_status status;
    while((status=xl_view_render(view.get(),&flags))==XL_BUSY) {
        CHECK(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();
    }
    CHECK(status==XL_OK);
}
struct Text {xl_node_id node;std::string name;};
std::vector<Text> texts(xl::View& view) {
    size_t count=0;CHECK(xl_view_ax_count(view.get(),&count)==XL_OK);
    std::vector<Text> result;
    for(size_t i=0;i<count;++i) {
        xl_ax_node node{};CHECK(xl_view_ax_get(view.get(),i,&node,sizeof(node))==XL_OK);
        if(node.role!=XL_AX_TEXT)continue;
        size_t size=0;const auto status=xl_view_ax_name(view.get(),i,nullptr,0,&size);
        CHECK(status==XL_OK || status==XL_BUFFER_TOO_SMALL);
        std::string name(size,'\0');CHECK(xl_view_ax_name(view.get(),i,name.data(),size,&size)==XL_OK);
        result.push_back({node.node,std::move(name)});
    }
    return result;
}
xl_text_position position(xl_node_id node,uint32_t offset,uint32_t affinity=XL_TEXT_DOWNSTREAM) {
    return {sizeof(xl_text_position),offset,node,affinity,0};
}
std::vector<unsigned char> pixels(xl::View& view) {
    const auto stats=view.stats();std::vector<unsigned char> result(size_t(stats.pixel_width)*stats.pixel_height*4);
    CHECK(xl_view_copy_rgba(view.get(),result.data(),result.size(),size_t(stats.pixel_width)*4)==XL_OK);return result;
}
bool close_to(float a,float b){return std::abs(a-b)<1;}
}
int main() {
    // Supplementary scalar offsets are validated independently of platform
    // font coverage; no missing-glyph fallback can hide a split surrogate.
    {
        xl::detail::DocumentFrame frame;
        xl::detail::TextItem item;
        item.sources.push_back({123,{0,1,2,3},{},"A\xf0\x90\x90\x80",{0,1,xl::detail::TextSource::invalid_byte,5}});
        frame.texts.push_back(std::move(item));frame.text_nodes[123]={0,0};frame.text_order.push_back({0,0});
        xl_text_selection scalar{sizeof(xl_text_selection),0,position(123,1),position(123,3)};
        xl::detail::validate_selection(frame,scalar);
        CHECK(xl::detail::selection_text(frame,scalar)=="\xf0\x90\x90\x80");
        scalar.focus.offset_utf16=2;
        bool rejected=false;
        try {xl::detail::validate_selection(frame,scalar);}
        catch(const xl::detail::DocumentFailure& error){rejected=error.status==XL_INVALID_ARGUMENT;}
        CHECK(rejected);
    }
    xl::Runtime runtime;
    xl::Document document(runtime);
    std::ifstream stream(XL_TEST_FONT,std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
    CHECK(xl_document_add_font(document.get(),"test",font.data(),font.size())==XL_OK);
    xl_view_options options;CHECK(xl_view_options_init(&options,sizeof(options))==XL_OK);
    options.kind=XL_VIEW_OFFSCREEN;options.width=320;options.height=200;
    xl::View view(runtime,&options);view.document(&document);
    const std::string base="<!doctype html><style>body,p{margin:0}p{font:20px/26px test;width:180px}</style>";
    const auto page=[&](const std::string& content){document.html(base+content);render(view);};
    page("<p>  A  <span style='padding:2px;border:1px solid blue'>B</span> C</p><p>D e\xcc\x81</p>");
    auto nodes=texts(view);CHECK(nodes.size()==4);
    const auto plain=pixels(view);
    const auto layouts=document.stats().layout_frames;
    view.selection(position(nodes[0].node,2),position(nodes[2].node,2));
    CHECK(view.selected_text()=="A  B C");
    const auto selected=view.selection();
    CHECK(selected.anchor.node==nodes[0].node && selected.focus.node==nodes[2].node);
    xl_node_bounds sentinel{99,99,99,99};size_t count=0;
    CHECK(xl_view_selection_rects(view.get(),nullptr,0,&count)==XL_BUFFER_TOO_SMALL && count>=3);
    CHECK(xl_view_selection_rects(view.get(),&sentinel,1,&count)==XL_BUFFER_TOO_SMALL && sentinel.x==99);
    std::vector<xl_node_bounds> boxes(count);
    CHECK(xl_view_selection_rects(view.get(),boxes.data(),boxes.size(),&count)==XL_OK);
    char byte='Z';size_t required=0;
    CHECK(xl_view_selected_text(view.get(),&byte,1,&required)==XL_BUFFER_TOO_SMALL && byte=='Z');
    const auto before=view.stats().rasterized_pixels;
    render(view);CHECK(pixels(view)!=plain && document.stats().layout_frames==layouts);
    CHECK(view.stats().rasterized_pixels-before<320u*200u && view.render()==0);
    CHECK(texts(view).size()==nodes.size());
    {
        xl::View full(runtime,&options);full.document(&document);render(full);
        full.selection(selected.anchor,selected.focus);render(full);CHECK(pixels(full)==pixels(view));
    }
    view.selection(selected.focus,selected.anchor);render(view);
    CHECK(view.selected_text()=="A  B C" && view.selection().anchor.node==selected.focus.node);
    const auto beginning=view.caret(position(nodes[0].node,2));
    const auto ending=view.caret(position(nodes[2].node,2,XL_TEXT_UPSTREAM));
    view.select_at(beginning.x+.1f,beginning.y+beginning.height*.5f);
    view.select_at(ending.x-.1f,ending.y+ending.height*.5f,true); // No render needed between drag updates.
    CHECK(view.selected_text()=="A  B C");render(view);
    CHECK(xl_view_set_selection_input(view.get(),1)==XL_UNSUPPORTED);
    view.selection(position(nodes[0].node,2),position(nodes[3].node,4));
    CHECK(view.selected_text()=="A  B CD e\xcc\x81");
    render(view);view.clear_selection();render(view);CHECK(pixels(view)==plain);
    xl_text_selection absent{};
    CHECK(xl_view_get_selection(view.get(),&absent,sizeof(absent))==XL_NOT_FOUND);
    const auto start=position(nodes[0].node,2);
    const auto caret_layouts=document.stats().layout_frames;
    auto caret=view.caret(start);CHECK(caret.width==1 && caret.height>20);
    const auto hit=view.hit_text(caret.x+.2f,caret.y+caret.height*.5f);
    CHECK(hit.node==nodes[0].node && hit.offset_utf16>=2 && hit.offset_utf16<=3);
    view.selection(start,start);CHECK(view.selected_text().empty());
    CHECK(xl_view_selection_rects(view.get(),nullptr,0,&count)==XL_OK && count==0);
    render(view);CHECK(pixels(view)!=plain && document.stats().layout_frames==caret_layouts);
    CHECK(view.render()==0);
    auto invalid=start;invalid.offset_utf16=999;
    xl_text_selection invalid_selection{sizeof(xl_text_selection),0,invalid,start};
    CHECK(xl_view_set_selection(view.get(),&invalid_selection,sizeof(invalid_selection))==XL_INVALID_ARGUMENT);
    CHECK(view.selection().anchor.offset_utf16==start.offset_utf16);
    CHECK(xl_view_set_selection(view.get(),&invalid_selection,1)==XL_ABI_MISMATCH);
    std::thread wrong([&]{CHECK(xl_view_set_selection(view.get(),nullptr,0)==XL_WRONG_THREAD);});wrong.join();
    // Style and resize retain DOM offsets while refreshing selection geometry.
    document.attribute(document.query("p"),"style","font-size:28px;width:90px");
    CHECK(xl_view_get_selection(view.get(),&absent,sizeof(absent))==XL_BUSY);
    render(view);CHECK(view.selection().anchor.node==start.node);
    CHECK(xl_view_resize(view.get(),240,160)==XL_OK);render(view);
    CHECK(view.selection().anchor.node==start.node);
    document.text(document.query("p"),"Replacement");render(view);
    CHECK(xl_view_get_selection(view.get(),&absent,sizeof(absent))==XL_NOT_FOUND);
    page("<p style='width:40px'>A B C D E F</p>");
    nodes=texts(view);CHECK(nodes.size()==1);
    bool wrapped=false;
    for(uint32_t i=1;i<nodes[0].name.size();++i) {
        const auto upstream=view.caret(position(nodes[0].node,i,XL_TEXT_UPSTREAM));
        const auto downstream=view.caret(position(nodes[0].node,i));
        if(downstream.y>upstream.y+1){wrapped=true;break;}
    }
    CHECK(wrapped);
    page("<p dir=rtl>abc</p>");nodes=texts(view);
    caret=view.caret(position(nodes[0].node,0));CHECK(caret.x>100);
    page("<p style='white-space:pre;tab-size:40px'>A\tB\r\nC</p>");nodes=texts(view);
    view.selection(position(nodes[0].node,1),position(nodes[0].node,5));
    CHECK(view.selected_text()=="\tB\nC"); // HTML parser normalizes source CRLF.
    render(view);const auto tab=view.text_rects(nodes[0].node,1,2).front();
    CHECK(close_to(view.caret(position(nodes[0].node,1)).x,tab.x));
    CHECK(close_to(view.caret(position(nodes[0].node,2,XL_TEXT_UPSTREAM)).x,tab.x+tab.width));
    const auto pre_node=nodes[0].node;
    CHECK(document.evaluate("document.querySelector('p').textContent='A\\tB\\r\\nC'")==XL_OK);render(view);
    nodes=texts(view);CHECK(nodes[0].node!=pre_node);
    view.selection(position(nodes[0].node,1),position(nodes[0].node,6));
    CHECK(view.selected_text()=="\tB\r\nC");render(view);
    page("<p>e\xcc\x81</p>");nodes=texts(view);
    const auto combined=view.caret(position(nodes[0].node,1));
    CHECK(close_to(combined.x,view.caret(position(nodes[0].node,0)).x));
    view.selection(position(nodes[0].node,0),position(nodes[0].node,2));render(view);
    document.html(base+"<p>New navigation</p>");render(view);
    CHECK(xl_view_get_selection(view.get(),&absent,sizeof(absent))==XL_NOT_FOUND);
    view.document(nullptr);CHECK(xl_view_set_selection(view.get(),nullptr,0)==XL_OK);
#ifdef _WIN32
    // Win32 messages pass through SDL's real window event adapter, with no
    // dependency on private SDL symbols or the desktop cursor location.
    options.kind=XL_VIEW_WINDOW;options.flags=XL_VIEW_HIDDEN;
    xl::View native(runtime,&options);native.document(&document);render(native);
    auto native_texts=texts(native);CHECK(!native_texts.empty());
    auto first=native.caret(position(native_texts[0].node,0));
    auto last=native.caret(position(native_texts[0].node,5));
    uint32_t kind=0;uintptr_t handle=0;
    CHECK(xl_view_native_handle(native.get(),&kind,&handle)==XL_OK && kind==XL_NATIVE_WIN32);
    native.selection_input(true);
    const auto hwnd=reinterpret_cast<HWND>(handle);
    int logical_width=0,logical_height=0;
    RECT client{};CHECK(GetClientRect(hwnd,&client));logical_width=client.right;logical_height=client.bottom;
    const auto coordinates=[&](xl_node_bounds bounds) {
        const int x=static_cast<int>((bounds.x+.1f)*logical_width/native.stats().pixel_width);
        const int y=static_cast<int>((bounds.y+bounds.height*.5f)*logical_height/native.stats().pixel_height);
        return MAKELPARAM(x,y);
    };
    CHECK(PostMessageW(hwnd,WM_MOUSEMOVE,0,coordinates(first)));
    CHECK(PostMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,coordinates(first)));
    CHECK(PostMessageW(hwnd,WM_MOUSEMOVE,MK_LBUTTON,coordinates(last)));
    CHECK(PostMessageW(hwnd,WM_LBUTTONUP,0,coordinates(last)));
    const auto native_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    bool native_selected=false;
    do {
        CHECK(xl_runtime_pump_events(runtime.get(),XL_PUMP_NATIVE_EVENTS)==XL_OK);render(native);
        xl_text_selection selection{};
        if(xl_view_get_selection(native.get(),&selection,sizeof(selection))==XL_OK && !native.selected_text().empty()) {
            native_selected=true;break;
        }
    } while(std::chrono::steady_clock::now()<native_deadline);
    if(!native_selected) {
        xl_text_selection state{};
        const auto status=xl_view_get_selection(native.get(),&state,sizeof(state));
        std::cerr<<"Native selection status="<<status<<" anchor="<<state.anchor.offset_utf16
                 <<" focus="<<state.focus.offset_utf16<<" first="<<first.x<<","<<first.y
                 <<" last="<<last.x<<","<<last.y<<'\n';
    }
    CHECK(native_selected && native.selected_text().find("New")!=std::string::npos);
#endif
    std::cout<<"View selection, caret affinity, DOM offsets, original text, AX and retained overlays passed\n";
}
