#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
int main(int argc, char** argv) {
    xl::Runtime runtime; xl::Document document(runtime);
    const auto script = [&](const char* code, const char* expected) {
        CHECK(document.evaluate(code) == XL_OK);
        const auto actual = document.result();
        if (actual != expected) { std::cerr << code << "\nexpected " << expected << "\nactual " << actual << '\n'; std::exit(1); }
    };
    document.html("<style>body{margin:0}label,input{display:none}#a:checked~#panel{background-color:lime}#panel{background-color:red;width:20px;height:20px}</style>"
        "<input id=a type=radio name=tab checked><input id=b type=radio name=tab><div id=panel></div>"
        "<label id=label for=b><span id=span>Tab B</span></label>"
        "<input id=c type=checkbox style='display:none'><label id=cl for=c>Theme</label>"
        "<label id=implicit><input id=d type=checkbox><span id=ds>Toggle</span><a id=link href='#x'>Link</a></label>"
        "<form id=f><input id=foreign type=radio name=tab checked></form><input id=external type=radio name=tab form=f>");
    xl_view_options options; CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 32; options.height = 32;
    xl::View view(runtime, &options); view.document(&document);
    const auto pixel = [&] {
        uint32_t flags = 0; xl_status status;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) { CHECK(std::chrono::steady_clock::now() < end); std::this_thread::yield(); }
        CHECK(status == XL_OK);
        std::vector<unsigned char> bytes(32 * 32 * 4);
        CHECK(xl_view_copy_rgba(view.get(), bytes.data(), bytes.size(), 32 * 4) == XL_OK);
        CHECK(view.render() == 0);
        return std::vector<unsigned char>(bytes.begin(), bytes.begin() + 4);
    };
    script("let a=document.getElementById('a'),b=document.getElementById('b'),c=document.getElementById('c'),d=document.getElementById('d');"
           "document.querySelectorAll(':checked').map(x=>x.id).join(',')", "a,foreign");
    CHECK(pixel() == std::vector<unsigned char>({0,255,0,255}));
    script("document.getElementById('span').click();[a.checked,b.checked,document.getElementById('foreign').checked].join(',')", "false,true,true");
    script("getComputedStyle(document.getElementById('panel')).getPropertyValue('background-color')", "red");
    CHECK(pixel() == std::vector<unsigned char>({255,0,0,255}));
    script("a.checked=true;getComputedStyle(document.getElementById('panel')).getPropertyValue('background-color')", "lime");
    script("b.onclick=e=>e.preventDefault();b.click();[a.checked,b.checked].join(',')", "true,false");
    script("b.onclick=null;globalThis.events=[];b.oninput=()=>events.push('input');b.onchange=()=>events.push('change');b.click();b.click();events.join(',')", "input,change");
    const auto revision = document.stats().revision;
    script("b.click();events.join(',')", "input,change");
    CHECK(document.stats().revision == revision);
    script("document.getElementById('external').checked=true;[b.checked,document.getElementById('foreign').checked].join(',')", "true,false");
    script("document.getElementById('cl').click();c.checked+':'+c.hasAttribute('checked')+':'+document.querySelector('#c:checked').id", "true:false:c");
    script("document.getElementById('cl').onclick=e=>e.preventDefault();document.getElementById('cl').click();c.checked", "true");
    script("document.getElementById('ds').click();d.checked", "true");
    script("document.getElementById('link').click();d.checked", "true");
    script("d.onclick=()=>d.click();d.click();d.checked", "false");
    script("c.disabled=true;document.getElementById('cl').onclick=null;document.getElementById('cl').click();c.checked", "true");
    script("document.querySelector('input:not(:checked):is(:not(:checked))').id", "a");
    script("let detached=document.createElement('section'),x=document.createElement('input');x.setAttribute('type','checkbox');detached.appendChild(x);x.checked=true;detached.querySelectorAll(':checked').length", "1");
    // The real user page is read from its original path. Do not rewrite or
    // inject a script into this file; execute checks against the loaded DOM.
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        const std::string html{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        CHECK(!html.empty()); document.html(html);
        script("document.querySelector('#tab-1:checked~.panels>.panel:nth-child(1)')!==null", "true");
        script("document.querySelector('label[for=tab-2]').click();[document.getElementById('tab-1').checked,document.getElementById('tab-2').checked,document.getElementById('tab-3').checked].join(',')", "false,true,false");
        script("getComputedStyle(document.querySelector('.panel:nth-child(1)')).getPropertyValue('display')+':'+getComputedStyle(document.querySelector('.panel:nth-child(2)')).getPropertyValue('display')", "none:block");
        script("document.querySelector('.theme-btn').click();document.getElementById('theme').checked", "true");
        script("document.querySelector('.theme-btn').click();document.getElementById('theme').checked", "false");
    }
}
