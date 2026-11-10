#include <XLWebEngineUI/xl.hpp>
#include "webtest_font.h"
#include <zstd.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string_view>
#include <thread>

namespace fs = std::filesystem;
namespace {
std::string utf8(const fs::path& path) {
    const auto value=path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()),value.size()};
}
fs::path path_from_utf8(std::string_view text) {return fs::path(std::u8string(text.begin(),text.end()));}
void check(xl_status status) {
    if(status!=XL_OK)throw std::runtime_error(xl_status_string(status));
}
std::string lower(std::string value) {
    for(auto& c:value)if(c>='A' && c<='Z')c=char(c-'A'+'a');
    return value;
}
std::vector<std::byte> read_file(const fs::path& path,size_t limit) {
    const auto size=fs::file_size(path);
    if(size>limit)throw std::runtime_error("file exceeds memory limit: "+utf8(path.filename()));
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("cannot open file: "+utf8(path.filename()));
    std::vector<std::byte> data(static_cast<size_t>(size));
    if(size && !stream.read(reinterpret_cast<char*>(data.data()),static_cast<std::streamsize>(size)))
        throw std::runtime_error("cannot read file: "+utf8(path.filename()));
    return data;
}
std::string encode_path(std::string_view value) {
    constexpr char hex[]="0123456789ABCDEF";
    std::string result;
    for(unsigned char c:value) {
        if((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') ||
           c=='-' || c=='_' || c=='.' || c=='~' || c=='/')result+=char(c);
        else {result+='%';result+=hex[c>>4];result+=hex[c&15];}
    }
    return result;
}
int hex(char c) {return c>='0'&&c<='9'?c-'0':c>='A'&&c<='F'?c-'A'+10:c>='a'&&c<='f'?c-'a'+10:-1;}
std::string decode_path(std::string_view value) {
    std::string result;
    for(size_t i=0;i<value.size();++i) {
        unsigned char c=value[i];
        if(c=='%') {
            if(i+2>=value.size() || hex(value[i+1])<0 || hex(value[i+2])<0)throw std::runtime_error("invalid URI escape");
            c=static_cast<unsigned char>(hex(value[i+1])*16+hex(value[i+2]));i+=2;
            if(c=='/' || c=='\\')throw std::runtime_error("encoded path separator");
        }
        if(c==0 || c<0x20 || c=='\\' || c==':')throw std::runtime_error("invalid local resource path");
        result+=char(c);
    }
    return result;
}
const char* mime(const fs::path& path) {
    const auto ext=lower(utf8(path.extension()));
    if(ext==".html" || ext==".htm")return "text/html;charset=utf-8";
    if(ext==".css")return "text/css;charset=utf-8";
    if(ext==".js" || ext==".mjs")return "text/javascript;charset=utf-8";
    if(ext==".json")return "application/json";
    if(ext==".svg")return "image/svg+xml";
    if(ext==".png")return "image/png";
    if(ext==".jpg" || ext==".jpeg")return "image/jpeg";
    if(ext==".webp")return "image/webp";
    if(ext==".gif")return "image/gif";
    if(ext==".bmp")return "image/bmp";
    if(ext==".ico")return "image/x-icon";
    if(ext==".webm")return "video/webm";
    if(ext==".ttf")return "font/ttf";
    if(ext==".otf")return "font/otf";
    return "application/octet-stream";
}
struct Files {
    fs::path root;
    std::map<fs::path,xl::Content> cached;
    size_t bytes=0, missing=0;
    static constexpr size_t budget=256u*1024u*1024u;
    fs::path checked(const fs::path& relative) const {
        if(relative.is_absolute())throw std::runtime_error("absolute dependency path is outside the UI root");
        const auto path=fs::canonical(root/relative);
        const auto common=std::mismatch(root.begin(),root.end(),path.begin(),path.end());
        if(common.first!=root.end() || !fs::is_regular_file(path))throw std::runtime_error("resource is outside the UI root or not a regular file");
        return path;
    }
    xl::Content open(const char* uri) {
        constexpr std::string_view prefix="xl-webengine-ui://app/";
        const std::string_view text(uri);
        if(!text.starts_with(prefix))throw std::runtime_error("only the local app authority is mounted");
        const auto path=checked(path_from_utf8(decode_path(text.substr(prefix.size()))));
        if(const auto found=cached.find(path);found!=cached.end())return found->second;
        const auto data=read_file(path,budget-bytes);
        xl::Content content(std::span<const std::byte>(data),mime(path));
        bytes+=data.size();
        cached.emplace(path,content);
        std::cout<<"Loaded "<<uri<<" ("<<data.size()<<" bytes)\n";
        return content;
    }
    static xl_status load(void* context,const char* uri,xl_content** output) noexcept {
        *output=nullptr;
        auto& files=*static_cast<Files*>(context);
        try {auto content=files.open(uri);return xl_content_clone(content.get(),output);}
        catch(const std::bad_alloc&){return XL_OUT_OF_MEMORY;}
        catch(const std::exception& error) {++files.missing;std::cerr<<"Resource "<<uri<<": "<<error.what()<<'\n';return XL_NOT_FOUND;}
    }
};
struct Options {
    fs::path root=fs::current_path(),page="index.html",snapshot;
    std::vector<fs::path> fonts;
    int width=960,height=640,frames=0;
    bool headless=false,scripts=true;
    uint32_t graphics=UINT32_MAX;
};
int number(std::string_view value,int maximum) {
    int result=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),result);
    if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size() || result<1 || result>maximum)
        throw std::runtime_error("invalid positive numeric option");
    return result;
}
Options options(const std::vector<std::string>& args) {
    Options value;
    bool page=false;
    for(size_t i=1;i<args.size();++i) {
        const auto& arg=args[i];
        auto next=[&]() -> const std::string& {if(++i==args.size())throw std::runtime_error("missing value for "+arg);return args[i];};
        if(arg=="--root")value.root=path_from_utf8(next());
        else if(arg=="--font")value.fonts.push_back(path_from_utf8(next()));
        else if(arg=="--snapshot")value.snapshot=path_from_utf8(next());
        else if(arg=="--width")value.width=number(next(),8192);
        else if(arg=="--height")value.height=number(next(),8192);
        else if(arg=="--frames")value.frames=number(next(),1000000);
        else if(arg=="--graphics") {
            const auto& backend=next();
            const std::map<std::string,uint32_t> names{{"auto",XL_GRAPHICS_AUTO},{"raster",XL_GRAPHICS_RASTER},
                {"d3d12",XL_GRAPHICS_D3D12},{"angle-d3d11",XL_GRAPHICS_ANGLE_D3D11},{"vulkan",XL_GRAPHICS_VULKAN},
                {"opengl",XL_GRAPHICS_OPENGL},{"angle-gl",XL_GRAPHICS_ANGLE_OPENGL},{"swiftshader",XL_GRAPHICS_SWIFTSHADER},
                {"dawn",XL_GRAPHICS_DAWN},{"dawn-swiftshader",XL_GRAPHICS_DAWN_SWIFTSHADER}};
            const auto found=names.find(backend);
            if(found==names.end())throw std::runtime_error("unknown graphics backend: "+backend);
            value.graphics=found->second;
        }
        else if(arg=="--check")value.headless=true;
        else if(arg=="--no-scripts")value.scripts=false;
        else if(!arg.starts_with("-") && !page){value.page=path_from_utf8(arg);page=true;}
        else throw std::runtime_error("unknown option: "+arg);
    }
    return value;
}
std::string evaluate(xl::Document& doc,std::string_view script) {
    const auto status=doc.evaluate(script,1000);
    if(status!=XL_OK)throw std::runtime_error("Script: "+doc.result());
    return doc.result();
}
void scripts(xl::Document& doc,xl::Runtime& runtime) {
    evaluate(doc,"globalThis.__webTestLogs=[];globalThis.console=Object.freeze(Object.fromEntries(['log','info','warn','error','debug'].map(level=>[level,(...args)=>{if(__webTestLogs.length<128)__webTestLogs.push(level+': '+args.map(String).join(' ').slice(0,4096))}])));'console ready'");
    const auto count=number(evaluate(doc,"globalThis.__webTestScripts=document.querySelectorAll('script').map(s=>({src:s.getAttribute('src'),type:(s.getAttribute('type')||'').toLowerCase(),text:s.textContent}));String(__webTestScripts.length+1)"),4097)-1;
    for(int i=0;i<count;++i) {
        const auto item="__webTestScripts["+std::to_string(i)+"]";
        const auto type=evaluate(doc,item+".type");
        if(!type.empty() && type!="text/javascript" && type!="application/javascript") {
            if(type=="module")std::cerr<<"Script modules are not supported yet\n";
            continue;
        }
        const auto external=evaluate(doc,item+".src===null?'no':'yes'")=="yes";
        auto code=evaluate(doc,item+".text");
        if(external) {
            const auto uri=evaluate(doc,"xl.resolveURI("+item+".src)");
            const auto content=runtime.content(uri.c_str());
            if(content.data().size()>16*1024*1024)throw std::runtime_error("script exceeds 16 MiB");
            const auto data=content.data();
            code.assign(data.empty()?"":reinterpret_cast<const char*>(data.data()),data.size());
        }
        evaluate(doc,code);
        const auto messages=evaluate(doc,"__webTestLogs.splice(0).join('\\n')");
        if(!messages.empty())std::cout<<messages<<'\n';
    }
    evaluate(doc,"delete globalThis.__webTestScripts;'scripts complete'");
}
void add_font(xl::Document& doc,const fs::path& path) {
    const auto data=read_file(path,64*1024*1024);
    const auto family=utf8(path.stem());
    check(xl_document_add_font(doc.get(),family.c_str(),data.data(),data.size()));
    std::cout<<"Font "<<utf8(path.filename())<<'\n';
}
void fallback_font(xl::Document& doc) {
    std::vector<std::byte> data(webtest::font_size);
    const auto size=ZSTD_decompress(data.data(),data.size(),webtest::font_data,sizeof(webtest::font_data));
    if(ZSTD_isError(size) || size!=data.size())throw std::runtime_error("corrupt bundled fallback font");
    check(xl_document_add_font(doc.get(),"Roboto",data.data(),data.size()));
    std::cout<<"Loaded bundled Roboto fallback in memory\n";
}
void local_fonts(xl::Document& doc,const Files& files,const Options& opt) {
    size_t count=0;
    size_t bytes=0;
    std::set<fs::path> loaded;
    auto add=[&](const fs::path& path) {
        const auto checked=files.checked(path);
        if(!loaded.insert(checked).second)return;
        if(++count>128)throw std::runtime_error("too many local fonts (limit 128)");
        const auto size=fs::file_size(checked);
        if(size>64u*1024u*1024u-bytes)throw std::runtime_error("local fonts exceed 64 MiB memory limit");
        bytes+=static_cast<size_t>(size);
        add_font(doc,checked);
    };
    for(const auto& path:opt.fonts)add(path);
    for(const auto& entry:fs::directory_iterator(files.root)) {
        const auto ext=lower(utf8(entry.path().extension()));
        if(entry.is_regular_file() && !entry.is_symlink() && (ext==".ttf" || ext==".otf"))add(entry.path().filename());
    }
    const auto folder=files.root/"fonts";
    if(fs::is_directory(folder) && !fs::is_symlink(folder)) {
        for(const auto& entry:fs::directory_iterator(folder)) {
            const auto ext=lower(utf8(entry.path().extension()));
            if(entry.is_regular_file() && !entry.is_symlink() && (ext==".ttf" || ext==".otf"))add(fs::path("fonts")/entry.path().filename());
        }
    }
}
void snapshot(xl::View& view,const fs::path& path) {
    const auto stats=view.stats();
    std::vector<unsigned char> rgba(size_t(stats.pixel_width)*stats.pixel_height*4);
    check(xl_view_copy_rgba(view.get(),rgba.data(),rgba.size(),size_t(stats.pixel_width)*4));
    std::ofstream output(path,std::ios::binary);
    if(!output)throw std::runtime_error("cannot write snapshot");
    output<<"P6\n"<<stats.pixel_width<<' '<<stats.pixel_height<<"\n255\n";
    for(size_t i=0;i<rgba.size();i+=4)output.write(reinterpret_cast<const char*>(rgba.data()+i),3);
    if(!output)throw std::runtime_error("snapshot write failed");
}
int run(const std::vector<std::string>& args) {
    if(std::find(args.begin(),args.end(),"--help")!=args.end()) {
        std::cout<<"WebTest - XLWebEngineUI local UI preview\n"
                   "Usage: WebTest [index.html] [--root DIR] [--check] [--snapshot image.ppm]\n"
                   "       [--font relative.ttf] [--width 960] [--height 640] [--frames N] [--no-scripts]\n"
                   "       [--graphics auto|raster|d3d12|angle-d3d11|vulkan|opengl|angle-gl|swiftshader|dawn|dawn-swiftshader]\n"
                   "Default: render the current directory's index.html in a window.\n"
                   "Local dependencies resolve inside the chosen root, on demand. No HTTP.\n";
        return 0;
    }
    const auto opt=options(args);
    Files files;files.root=fs::canonical(opt.root);
    const auto logical=opt.page.lexically_normal();
    if(logical.empty() || *logical.begin()=="..")throw std::runtime_error("page must be inside the UI root");
    files.checked(logical);
    const auto uri="xl-webengine-ui://app/"+encode_path(utf8(logical));
    std::cout<<"WebTest / "<<xl_version_string()<<"\nRoot: "<<utf8(files.root)<<'\n';
    xl::Runtime runtime;
    runtime.content_loader(Files::load,&files);
    xl::Document document(runtime);
    document.load(uri.c_str());
    xl_view_options view_options;
    check(xl_view_options_init(&view_options,sizeof(view_options)));
    view_options.width=opt.width;view_options.height=opt.height;
    view_options.title_utf8="WebTest - XLWebEngineUI";
    view_options.flags=XL_VIEW_RESIZABLE;
    if(opt.headless){view_options.kind=XL_VIEW_OFFSCREEN;view_options.flags=0;}
    xl::View view(runtime,&view_options);view.document(&document);
    check(xl_view_set_graphics_backend(view.get(),opt.graphics==UINT32_MAX?
        (opt.headless?XL_GRAPHICS_RASTER:XL_GRAPHICS_AUTO):opt.graphics));
    if(opt.scripts)scripts(document,runtime);
    if(!opt.headless) { view.selection_input(true); view.scroll_input(true); view.keyboard_input(true); }
#ifdef _WIN32
    if(!opt.headless) view.native_accessibility(true);
#endif
    bool local=false,fallback=false,first=true;
    int frames=0;
    auto pending=std::chrono::steady_clock::now();
    while(!view.stats().close_requested) {
        if(!opt.headless)check(xl_runtime_pump_events(runtime.get(),XL_PUMP_NATIVE_EVENTS));
        uint32_t flags=0;
        const auto status=xl_view_render(view.get(),&flags);
        if((status==XL_BUSY || status==XL_NOT_FOUND) && !local) {local=true;local_fonts(document,files,opt);continue;}
        if(status==XL_NOT_FOUND && !fallback) {fallback=true;fallback_font(document);continue;}
        if(status==XL_BUSY) {
            if(std::chrono::steady_clock::now()-pending>std::chrono::seconds(20))throw std::runtime_error("render preparation timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;
        }
        check(status);pending=std::chrono::steady_clock::now();
        if(first) {
            first=false;
            const auto stats=document.stats();
            std::cout<<"Rendered "<<view.stats().pixel_width<<'x'<<view.stats().pixel_height
                     <<"; resources="<<files.cached.size()<<"; unsupported_layout_nodes="<<stats.unsupported_layout_nodes<<'\n';
            if(!opt.snapshot.empty())snapshot(view,opt.snapshot);
            const auto graphics=view.graphics_info();
            std::cout<<"Graphics requested="<<graphics.requested<<" active="<<graphics.active
                     <<" shader_compiler="<<graphics.shader_compiler
                     <<" failed_mask="<<graphics.failed_mask<<" recoveries="<<graphics.recoveries<<'\n';
        }
        if(opt.headless || (opt.frames && ++frames>=opt.frames))break;
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
    return files.missing?2:0;
}
int entry(const std::vector<std::string>& args) {
    try {return run(args);}
    catch(const std::exception& error){std::cerr<<"WebTest: "<<error.what()<<'\n';return 1;}
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    std::vector<std::string> args;
    for(int i=0;i<argc;++i)args.push_back(utf8(fs::path(argv[i])));
    return entry(args);
}
#else
int main(int argc,char** argv) {return entry({argv,argv+argc});}
#endif
