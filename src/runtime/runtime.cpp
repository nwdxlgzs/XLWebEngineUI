#include <XLWebEngineUI/xl.h>
#include "runtime_internal.h"
#include <new>
#include <limits>

extern "C" {
const char* xl_version_string(void) { return "0.1.0-web-core"; }
const char* xl_status_string(xl_status status) {
    switch (status) {
    case XL_OK: return "ok";
    case XL_INVALID_ARGUMENT: return "invalid argument";
    case XL_ABI_MISMATCH: return "ABI mismatch";
    case XL_OUT_OF_MEMORY: return "out of memory";
    case XL_WRONG_THREAD: return "wrong thread";
    case XL_BUSY: return "busy";
    case XL_INTERNAL_ERROR: return "internal error";
    case XL_UNSUPPORTED: return "unsupported";
    case XL_CANCELED: return "canceled";
    case XL_CLOSED: return "closed";
    case XL_NOT_FOUND: return "not found";
    case XL_CORRUPT_DATA: return "corrupt data";
    case XL_BUFFER_TOO_SMALL: return "buffer too small";
    case XL_SCRIPT_ERROR: return "script error";
    case XL_TIMEOUT: return "timeout";
    default: return "unknown status";
    }
}
xl_status xl_runtime_options_init(xl_runtime_options* options, size_t options_size) {
    if (!options) return XL_INVALID_ARGUMENT;
    if (options_size < sizeof(xl_runtime_options)) return XL_ABI_MISMATCH;
    *options = {};
    options->struct_size = sizeof(xl_runtime_options);
    options->abi_version = XL_ABI_VERSION;
    options->worker_threads = 4;
    options->resource_cache_budget_bytes = 64ull * 1024 * 1024;
    return XL_OK;
}
xl_status xl_runtime_create(const xl_runtime_options* options, xl_runtime** out_runtime) {
    if (!out_runtime) return XL_INVALID_ARGUMENT;
    *out_runtime = nullptr;
    xl_runtime_options actual;
    xl_runtime_options_init(&actual, sizeof(actual));
    if (options) {
        if (options->struct_size < sizeof(xl_runtime_options)) return XL_ABI_MISMATCH;
        if (options->abi_version != XL_ABI_VERSION) return XL_ABI_MISMATCH;
        if (options->flags != 0 || options->worker_threads > 64
                || options->resource_cache_budget_bytes > std::numeric_limits<std::size_t>::max()) return XL_INVALID_ARGUMENT;
        actual = *options;
        if (actual.worker_threads == 0) actual.worker_threads = 4;
    }
    try {
        auto runtime = std::make_unique<xl_runtime>();
        runtime->options = actual;
        *out_runtime = runtime.release();
        return XL_OK;
    } catch (const std::bad_alloc&) { return XL_OUT_OF_MEMORY; }
    catch (...) { return XL_INTERNAL_ERROR; }
}
xl_status xl_runtime_check_thread(const xl_runtime* runtime) {
    if (!runtime) return XL_INVALID_ARGUMENT;
    if(runtime->owner != std::this_thread::get_id())return XL_WRONG_THREAD;
    return runtime->content.loading()?XL_BUSY:XL_OK;
}
xl_status xl_runtime_destroy(xl_runtime* runtime) {
    if (!runtime) return XL_OK;
    const auto status = xl_runtime_check_thread(runtime);
    if (status != XL_OK) return status;
    if (runtime->view_count || runtime->view_calls || runtime->document_count) return XL_BUSY;
    try {
        runtime->text_session.reset();
        runtime->text_request = {};
        runtime->text_resources.reset();
        runtime->resources.reset();
        delete runtime;
        return XL_OK;
    } catch (...) { return XL_INTERNAL_ERROR; }
}
}
