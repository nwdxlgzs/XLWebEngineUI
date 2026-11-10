#include "document_internal.h"
#include <cstring>

using namespace xl::detail;
namespace {
template<class Function> xl_status call(const xl_document* constant, Function&& function) {
    if (!constant) return XL_INVALID_ARGUMENT;
    auto* document = const_cast<xl_document*>(constant);
    const auto status = xl_runtime_check_thread(document->runtime);
    if (status != XL_OK) return status;
    if (document->active || document->runtime->view_calls) return XL_BUSY;
    struct Active { bool& flag; Active(bool& value) : flag(value) { flag = true; } ~Active() { flag = false; } } active{document->active};
    try { return function(*document->core); }
    catch (const DocumentFailure& error) { return error.status; }
    catch (const UriFailure& error) { return error.status; }
    catch (const std::bad_alloc&) { return XL_OUT_OF_MEMORY; }
    catch (...) { return XL_INTERNAL_ERROR; }
}
bool valid(const char* text, size_t size) { return (text || !size) && size <= 16 * 1024 * 1024; }
std::string_view input(const char* text, size_t size) { return {text ? text : "", size}; }
}
extern "C" {
xl_status xl_document_set_media_preferences(xl_document* document,const xl_media_preferences* preferences,size_t size) {
    if(preferences) {
        if(size<sizeof(*preferences)||preferences->struct_size!=sizeof(*preferences))return XL_ABI_MISMATCH;
        if(preferences->color_scheme>2||preferences->reduced_motion>2||preferences->contrast>4||preferences->forced_colors>2||preferences->reserved)return XL_INVALID_ARGUMENT;
    } else if(size)return XL_INVALID_ARGUMENT;
    return call(document,[&](Document& core){core.media_preferences(preferences);return XL_OK;});
}
xl_status xl_document_matches_media(const xl_document* document,const char* query,size_t size,uint32_t* matches) {
    if(!matches||(!query&&size)||size>65536)return XL_INVALID_ARGUMENT;
    *matches=0;
    return call(document,[&](Document& core){*matches=core.matches_media(input(query,size));return XL_OK;});
}
xl_status xl_document_create(xl_runtime* runtime, xl_document** result) {
    if (!result) return XL_INVALID_ARGUMENT;
    *result = nullptr;
    const auto status = xl_runtime_check_thread(runtime);
    if (status != XL_OK) return status;
    if (runtime->view_calls) return XL_BUSY;
    try {
        auto document = std::make_unique<xl_document>();
        document->runtime = runtime;
        document->core = std::make_unique<Document>(&runtime->content,[runtime]{
            auto prepared=xl_runtime_prepare_text(runtime);
            // A synchronous Canvas read/measurement needs a synchronous answer.
            // Decoding remains on the existing bounded resource worker; no UI callback is needed.
            if(prepared==XL_BUSY&&runtime->text_request.valid()){(void)runtime->text_request.get();prepared=xl_runtime_prepare_text(runtime);}
            if(prepared!=XL_OK)throw DocumentFailure{prepared};return runtime->text_session;
        });
        ++runtime->document_count;
        *result = document.release();
        return XL_OK;
    } catch (const DocumentFailure& error) { return error.status; }
    catch (const std::bad_alloc&) { return XL_OUT_OF_MEMORY; }
    catch (...) { return XL_INTERNAL_ERROR; }
}
xl_status xl_document_destroy(xl_document* document) {
    if (!document) return XL_OK;
    const auto status = xl_runtime_check_thread(document->runtime);
    if (status != XL_OK) return status;
    if (document->views || document->active || document->runtime->view_calls) return XL_BUSY;
    auto* runtime = document->runtime;
    delete document;
    --runtime->document_count;
    return XL_OK;
}
xl_status xl_document_load_html(xl_document* document, const char* text, size_t size) {
    if (!valid(text, size)) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { core.load(input(text, size)); return XL_OK; });
}
xl_status xl_document_load_html_at(xl_document* document,const char* text,size_t size,const char* uri) {
    if(!uri || !valid(text,size))return XL_INVALID_ARGUMENT;
    return call(document,[&](Document& core){core.load(input(text,size),uri);return XL_OK;});
}
xl_status xl_document_load_uri(xl_document* document,const char* uri) {
    if(!uri)return XL_INVALID_ARGUMENT;
    return call(document,[&](Document& core){core.load_uri(uri);return XL_OK;});
}
xl_status xl_document_get_uri(const xl_document* document,char* output,size_t capacity,size_t* required) {
    if(!required || (!output && capacity))return XL_INVALID_ARGUMENT;
    return call(document,[&](Document& core){return copy_string(core.uri(),output,capacity,required);});
}
xl_status xl_document_query_selector(xl_document* document, const char* selector, size_t size, xl_node_id* result) {
    if (!result || !valid(selector, size)) return XL_INVALID_ARGUMENT;
    *result = 0;
    return call(document, [&](Document& core) { *result = core.query(input(selector, size)); return *result ? XL_OK : XL_NOT_FOUND; });
}
xl_status xl_document_add_font(xl_document* document, const char* family, const void* data, size_t size) {
    if (!family || !*family || !data || size == 0 || size > 64 * 1024 * 1024) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { core.add_font(family, data, size); return XL_OK; });
}
xl_status xl_document_create_element(xl_document* document, const char* tag, size_t size, xl_node_id* result) {
    if (!result || !valid(tag, size)) return XL_INVALID_ARGUMENT;
    *result = 0;
    return call(document, [&](Document& core) { *result = core.create(input(tag, size)); return XL_OK; });
}
xl_status xl_document_append_child(xl_document* document, xl_node_id parent, xl_node_id child) {
    return call(document, [&](Document& core) { core.append(parent, child); return XL_OK; });
}
xl_status xl_document_remove_node(xl_document* document, xl_node_id node) {
    return call(document, [&](Document& core) { core.remove(node); return XL_OK; });
}
xl_status xl_document_set_attribute(xl_document* document, xl_node_id node, const char* name, const char* value, size_t size) {
    if (!name || !valid(value, size)) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { core.attribute(node, name, input(value, size)); return XL_OK; });
}
xl_status xl_document_get_attribute(const xl_document* document, xl_node_id node, const char* name, char* output, size_t capacity, size_t* required) {
    if (!name || !required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { return copy_string(core.attribute(node, name), output, capacity, required); });
}
xl_status xl_document_set_text(xl_document* document, xl_node_id node, const char* text, size_t size) {
    if (!valid(text, size)) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { core.text(node, input(text, size)); return XL_OK; });
}
xl_status xl_document_get_text(const xl_document* document, xl_node_id node, char* output, size_t capacity, size_t* required) {
    if (!required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { return copy_string(core.text(node), output, capacity, required); });
}
xl_status xl_document_get_style(const xl_document* document, xl_node_id node, const char* name, char* output, size_t capacity, size_t* required) {
    if (!name || !required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { return copy_string(core.style(node, name), output, capacity, required); });
}
xl_status xl_document_get_pseudo_style(const xl_document* document, xl_node_id node, const char* pseudo, const char* name,
                                      char* output, size_t capacity, size_t* required) {
    if (!pseudo || !name || !required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { return copy_string(core.pseudo_style(node, pseudo, name), output, capacity, required); });
}
xl_status xl_document_eval(xl_document* document, const char* script, size_t size, uint32_t budget) {
    if (!valid(script, size) || budget > 10000) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { return core.evaluate(input(script, size), budget); });
}
xl_status xl_document_script_result(const xl_document* document, char* output, size_t capacity, size_t* required) {
    if (!required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { return copy_string(core.result(), output, capacity, required); });
}
xl_status xl_document_get_value(const xl_document* document, xl_node_id node, char* output, size_t capacity, size_t* required) {
    if (!required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { return copy_string(core.value(node), output, capacity, required); });
}
xl_status xl_document_set_value(xl_document* document, xl_node_id node, const char* text, size_t size) {
    if ((!text && size) || size > 4 * 1024 * 1024) return XL_INVALID_ARGUMENT;
    return call(document, [&](Document& core) { core.value(node, input(text, size)); return XL_OK; });
}
xl_status xl_document_get_edit_state(const xl_document* document, xl_node_id node, xl_edit_state* state, size_t size) {
    if (!state) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*state)) return XL_ABI_MISMATCH;
    return call(document, [&](Document& core) { *state = core.edit_state(node); return XL_OK; });
}
xl_status xl_document_set_edit_selection(xl_document* document, xl_node_id node, uint32_t anchor, uint32_t focus) {
    return call(document, [&](Document& core) { core.edit_selection(node, anchor, focus); return XL_OK; });
}
xl_status xl_document_get_stats(const xl_document* document, xl_document_stats* stats, size_t size) {
    if (!stats) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*stats)) return XL_ABI_MISMATCH;
    return call(document, [&](Document& core) { *stats = core.stats(); return XL_OK; });
}
xl_status xl_document_ax_action(xl_document* document, xl_node_id node, uint32_t action) {
    return call(document, [&](Document& core) { core.action(node, action); return XL_OK; });
}
}
