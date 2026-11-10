#include "runtime_internal.h"
#include "document/document.h"
#include <algorithm>
#include <cstring>

using namespace xl::detail;
namespace {
template<class Function> xl_status content_call(Function&& function) {
    try {return function();}
    catch(const UriFailure& error){return error.status;}
    catch(const std::bad_alloc&){return XL_OUT_OF_MEMORY;}
    catch(...){return XL_INTERNAL_ERROR;}
}
}
extern "C" {
xl_status xl_content_create(const void* bytes,size_t size,const char* mime,xl_content** result) {
    if(!result)return XL_INVALID_ARGUMENT;*result=nullptr;
    if((!bytes && size) || size>256u*1024u*1024u)return XL_INVALID_ARGUMENT;
    const std::string_view type=mime?mime:"application/octet-stream";
    if(type.empty() || type.size()>256 || std::any_of(type.begin(),type.end(),[](unsigned char c){return c<0x20 || c>=0x7f;}))return XL_INVALID_ARGUMENT;
    return content_call([&]{
        auto content=std::make_shared<UiContent>();content->mime=type;
        content->bytes.resize(size);if(size)std::memcpy(content->bytes.data(),bytes,size);
        *result=new xl_content{std::move(content)};return XL_OK;
    });
}
xl_status xl_content_clone(const xl_content* content,xl_content** result) {
    if(!result)return XL_INVALID_ARGUMENT;*result=nullptr;if(!content)return XL_INVALID_ARGUMENT;
    return content_call([&]{*result=new xl_content{content->value};return XL_OK;});
}
xl_status xl_content_data(const xl_content* content,const void** bytes,size_t* size,const char** mime) {
    if(!content || !bytes || !size || !mime)return XL_INVALID_ARGUMENT;
    *bytes=content->value->bytes.data();*size=content->value->bytes.size();*mime=content->value->mime.c_str();return XL_OK;
}
void xl_content_release(xl_content* content){delete content;}
xl_status xl_runtime_mount_content(xl_runtime* runtime,const char* uri,const xl_content* content) {
    if(!uri || !content)return XL_INVALID_ARGUMENT;
    const auto status=xl_runtime_check_thread(runtime);if(status!=XL_OK)return status;
    if(runtime->view_calls)return XL_BUSY;
    return content_call([&]{runtime->content.mount(uri,content->value);return XL_OK;});
}
xl_status xl_runtime_unmount_content(xl_runtime* runtime,const char* uri) {
    if(!uri)return XL_INVALID_ARGUMENT;
    const auto status=xl_runtime_check_thread(runtime);if(status!=XL_OK)return status;
    if(runtime->view_calls)return XL_BUSY;
    return content_call([&]{return runtime->content.unmount(uri)?XL_OK:XL_NOT_FOUND;});
}
xl_status xl_runtime_open_content(xl_runtime* runtime,const char* uri,xl_content** result) {
    if(!result)return XL_INVALID_ARGUMENT;*result=nullptr;if(!uri)return XL_INVALID_ARGUMENT;
    const auto status=xl_runtime_check_thread(runtime);if(status!=XL_OK)return status;
    if(runtime->view_calls)return XL_BUSY;
    return content_call([&]{*result=new xl_content{runtime->content.open(uri)};return XL_OK;});
}
xl_status xl_resolve_ui_uri(const char* base,const char* reference,char* output,size_t capacity,size_t* required) {
    if(!reference || !required || (!output && capacity))return XL_INVALID_ARGUMENT;
    return content_call([&]{return copy_string(resolve_ui_uri(base?base:"",reference),output,capacity,required);});
}
xl_status xl_runtime_set_content_loader(xl_runtime* runtime,xl_content_loader_fn loader,void* context) {
    const auto status=xl_runtime_check_thread(runtime);if(status!=XL_OK)return status;
    if(runtime->view_calls)return XL_BUSY;
    runtime->content.loader(loader,context);return XL_OK;
}
}
