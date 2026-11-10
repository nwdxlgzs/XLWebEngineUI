#include <XLWebEngineUI/xl.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
namespace {
struct Text { xl_node_id node; std::string name; };
std::vector<Text> text_nodes(xl::View& view) {
    size_t count;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    std::vector<Text> result;
    for (size_t index = 0; index < count; ++index) {
        xl_ax_node node;
        CHECK(xl_view_ax_get(view.get(), index, &node, sizeof(node)) == XL_OK);
        if (node.role != XL_AX_TEXT) continue;
        size_t length;
        const auto status = xl_view_ax_name(view.get(), index, nullptr, 0, &length);
        CHECK(status == XL_OK || status == XL_BUFFER_TOO_SMALL);
        std::string name(length, '\0');
        CHECK(xl_view_ax_name(view.get(), index, name.data(), name.size(), &length) == XL_OK);
        result.push_back({node.node, std::move(name)});
    }
    return result;
}
xl_node_bounds bounds(xl::View& view, xl_node_id node) {
    xl_node_bounds result;
    CHECK(xl_view_node_bounds(view.get(), node, &result) == XL_OK);
    return result;
}
void render(xl::View& view) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    uint32_t flags;
    xl_status status;
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    CHECK(status == XL_OK);
}
std::vector<unsigned char> pixels(xl::View& view) {
    auto stats = view.stats();
    std::vector<unsigned char> result(size_t(stats.pixel_width)*stats.pixel_height*4);
    CHECK(xl_view_copy_rgba(view.get(), result.data(), result.size(), size_t(stats.pixel_width)*4) == XL_OK);
    return result;
}
bool close(float a, float b) { return std::abs(a-b) < .1f; }
}
int main() {
    xl::Runtime runtime;
    xl::Document document(runtime);
    std::ifstream input(XL_TEST_FONT, std::ios::binary);
    std::vector<char> font{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind=XL_VIEW_OFFSCREEN; options.width=360; options.height=260;
    xl::View view(runtime, &options); view.document(&document);
    const std::string base="<style>body,p,pre,div{margin:0}p,pre,div{font-size:20px;width:100px}</style>";
    const auto page=[&](const std::string& content) { document.html(base+content); render(view); };
    document.html(base+"<p style='white-space:pre'>   </p>");
    xl_resource_stats resources;
    CHECK(xl_runtime_resource_stats(runtime.get(), &resources, sizeof(resources)) == XL_OK && resources.decompressions == 0);
    render(view);
    CHECK(xl_runtime_resource_stats(runtime.get(), &resources, sizeof(resources)) == XL_OK && resources.decompressions == 1);
    auto nodes=text_nodes(view);
    CHECK(nodes.size()==1 && nodes[0].name=="   ");
    CHECK(!view.text_rects(nodes[0].node,0,3).empty());

    page("<p>A</p>");
    const auto line=bounds(view,document.query("p")).height;
    const auto glyph_height=bounds(view,text_nodes(view)[0].node).height;
    CHECK(line>0);
    page("<p style='white-space:normal'>  A \t  B\n  C  </p>");
    nodes=text_nodes(view);
    CHECK(nodes.size()==1 && nodes[0].name=="A B C");
    CHECK(close(bounds(view,document.query("p")).height,line));
    page("<p style='white-space:pre'>  A  B\n C  </p>");
    nodes=text_nodes(view);
    CHECK(nodes.size()==1 && nodes[0].name=="  A  B\n C  ");
    CHECK(close(bounds(view,document.query("p")).height,line*2));
    CHECK(view.text_rects(nodes[0].node,0,2)[0].width>0);
    CHECK(view.text_rects(nodes[0].node,6,7).empty());
    page("<pre>A\nB\n</pre>");
    CHECK(close(bounds(view,document.query("pre")).height,line*2));
    page("<p style='white-space:pre-line'>  A  \n \n  B </p>");
    nodes=text_nodes(view);
    CHECK(nodes.size()==1 && nodes[0].name=="A\n\nB");
    CHECK(close(bounds(view,document.query("p")).height,line*3));

    const std::string words="one two three four five six";
    page("<p>"+words+"</p>");
    CHECK(bounds(view,document.query("p")).height>line);
    page("<p style='white-space:nowrap'>  "+words+"  </p>");
    nodes=text_nodes(view);
    CHECK(nodes[0].name==words && close(bounds(view,document.query("p")).height,line));
    CHECK(bounds(view,nodes[0].node).width>100);
    page("<p>before <span id=n style='white-space:nowrap'>one two three four</span> after</p>");
    CHECK(close(bounds(view,document.query("#n")).height,glyph_height));
    page("<p>averyveryveryveryveryverylongword</p>");
    CHECK(close(bounds(view,document.query("p")).height,line)); // Normal overflow is not emergency wrapping.
    page("<p style='width:0'>Word</p>");
    CHECK(close(bounds(view,document.query("p")).height,line) && bounds(view,text_nodes(view)[0].node).width>0);
    page("<p style='white-space:pre-wrap;width:40px'>A          </p>");
    CHECK(close(bounds(view,document.query("p")).height,line));
    page("<p style='white-space:break-spaces;width:40px'>A          </p>");
    CHECK(bounds(view,document.query("p")).height>line);
    page("<p style='white-space:pre-wrap'>  one two three four\n B  </p>");
    nodes=text_nodes(view);
    CHECK(nodes[0].name=="  one two three four\n B  ");
    CHECK(bounds(view,document.query("p")).height>=line*3);

    // The same tab keeps one DOM UTF-16 unit while advancing to a measured stop.
    page("<pre style='tab-size:4'>A\tB</pre>");
    nodes=text_nodes(view);
    CHECK(nodes.size()==1 && nodes[0].name=="A\tB");
    const auto a=view.text_rects(nodes[0].node,0,1)[0], tab=view.text_rects(nodes[0].node,1,2)[0], b=view.text_rects(nodes[0].node,2,3)[0];
    CHECK(tab.width>0 && close(tab.x,a.x+a.width) && close(b.x,tab.x+tab.width));
    auto hit=view.hit_text(tab.x+tab.width*.1f,tab.y+tab.height*.5f);
    CHECK(hit.node==nodes[0].node && hit.offset_utf16==1);
    page("<pre style='tab-size:40px'>A\tB</pre>");
    nodes=text_nodes(view);
    CHECK(close(view.text_rects(nodes[0].node,2,3)[0].x,40));
    page("<pre style='tab-size:0'>A\tB</pre>");
    nodes=text_nodes(view);
    CHECK(close(view.text_rects(nodes[0].node,2,3)[0].x,view.text_rects(nodes[0].node,0,1)[0].width));
    page("<pre style='tab-size:40px'><span>A</span><span>\t</span><span>B</span></pre>");
    nodes=text_nodes(view);
    CHECK(nodes.size()==3 && close(view.text_rects(nodes[2].node,0,1)[0].x,40));

    page("<p style='line-height:2'>A<br>B</p>");
    CHECK(close(bounds(view,document.query("p")).height,80));
    page("<p style='line-height:40px'>A</p>");
    CHECK(close(bounds(view,document.query("p")).height,40));
    page("<p style='line-height:40px'><span style='font-size:30px'>A</span></p>");
    // Parent strut and differently sized inline share a baseline: their union
    // can exceed 40px. Chromium with this locked font measures 43px; Skia rounds
    // its font metrics to 44px. Check the inherited value and baseline union.
    const auto mixed_length=bounds(view,document.query("p")).height;
    CHECK(mixed_length>=40 && mixed_length<=44);
    page("<p style='line-height:2'><span style='font-size:30px'>A</span></p>");
    CHECK(close(bounds(view,document.query("p")).height,60));
    page("<p style='line-height:200%'><span style='font-size:30px'>A</span></p>");
    CHECK(close(bounds(view,document.query("p")).height,mixed_length));
    page("<p style='line-height:0'>A<br>B</p>");
    CHECK(close(bounds(view,document.query("p")).height,0));

    page("<p id=p style='white-space:pre'>A</p>");
    document.text(document.query("#p"),"A\r\nB"); render(view);
    nodes=text_nodes(view);
    CHECK(nodes.size()==1 && nodes[0].name=="A\nB");
    CHECK(close(bounds(view,document.query("p")).height,line*2));
    CHECK(view.text_rects(nodes[0].node,1,3).empty());
    CHECK(view.text_rects(nodes[0].node,3,4)[0].y>view.text_rects(nodes[0].node,0,1)[0].y);
    document.attribute(document.query("#p"),"style","white-space:normal;line-height:40px");
    render(view);
    CHECK(text_nodes(view)[0].name=="A B" && close(bounds(view,document.query("p")).height,40));
    {
        xl::View complete(runtime,&options);complete.document(&document);render(complete);
        CHECK(pixels(view)==pixels(complete));
    }
    CHECK(view.render()==0);
    std::cout<<"CSS whitespace preservation/wrapping, tab stops, inherited line height, source maps and AX passed\n";
}
