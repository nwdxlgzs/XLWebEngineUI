#include "runtime_internal.h"
#include "catalog.h"
#include "text/icu_session.h"
#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#include <limits>
#include <new>
#include <vector>

using namespace xl::detail;
struct xl_resource_request { ResourceRequest request; };
struct xl_resource_lease { ResourceLease lease; };

namespace {
xl_status resource_status(ResourceStatus status) {
    switch (status) {
    case ResourceStatus::ready: return XL_OK;
    case ResourceStatus::not_found: return XL_NOT_FOUND;
    case ResourceStatus::busy: return XL_BUSY;
    case ResourceStatus::canceled: return XL_CANCELED;
    case ResourceStatus::closed: return XL_CLOSED;
    case ResourceStatus::corrupt: return XL_CORRUPT_DATA;
    case ResourceStatus::out_of_memory: return XL_OUT_OF_MEMORY;
    default: return XL_INTERNAL_ERROR;
    }
}
ResourceService& resources(xl_runtime* runtime) {
    if (!runtime->resources)
        runtime->resources = std::make_unique<ResourceService>(builtin_resource_catalog(), runtime->options.worker_threads,
                                64, static_cast<std::size_t>(runtime->options.resource_cache_budget_bytes), runtime->allocator);
    return *runtime->resources;
}
ResourceService& text_resources(xl_runtime* runtime) {
    if (!runtime->allocator.allocate) return resources(runtime);
    if (!runtime->text_resources)
        runtime->text_resources = std::make_unique<ResourceService>(builtin_resource_catalog(), runtime->options.worker_threads,
                                64, 0); // ICU pins its lease; a second cache would exceed the Runtime budget.
    return *runtime->text_resources;
}
}

extern "C" {
xl_status xl_runtime_set_allocator(xl_runtime* runtime, void* user, xl_allocate_fn allocate, xl_deallocate_fn deallocate) {
    const auto status = xl_runtime_check_thread(runtime);
    if (status != XL_OK) return status;
    if (bool(allocate) != bool(deallocate)) return XL_INVALID_ARGUMENT;
    if (runtime->resources || runtime->text_resources || runtime->text_session) return XL_BUSY;
    runtime->allocator = {user, allocate, deallocate};
    return XL_OK;
}
xl_status xl_runtime_request_resource(xl_runtime* runtime, uint32_t id, xl_resource_request** out_request) {
    if (!out_request) return XL_INVALID_ARGUMENT;
    *out_request = nullptr;
    const auto status = xl_runtime_check_thread(runtime);
    if (status != XL_OK) return status;
    try {
        auto request = std::make_unique<xl_resource_request>();
        request->request = resources(runtime).request(id);
        *out_request = request.release();
        return XL_OK;
    } catch (const std::bad_alloc&) { return XL_OUT_OF_MEMORY; }
    catch (...) { return XL_INTERNAL_ERROR; }
}
xl_status xl_runtime_prewarm_resource(xl_runtime* runtime, uint32_t id, xl_resource_request** out_request) {
    return xl_runtime_request_resource(runtime, id, out_request);
}
xl_status xl_runtime_resource_stats(const xl_runtime* runtime, xl_resource_stats* stats, size_t stats_size) {
    const auto status = xl_runtime_check_thread(runtime);
    if (status != XL_OK) return status;
    if (!stats) return XL_INVALID_ARGUMENT;
    if (stats_size < sizeof(*stats)) return XL_ABI_MISMATCH;
    try {
        ResourceStats snapshot;
        if (runtime->resources) snapshot = runtime->resources->stats();
        *stats = {};
        stats->struct_size = sizeof(*stats);
        stats->accepting = snapshot.accepting;
        stats->decompressions = snapshot.decompressions;
        stats->coalesced = snapshot.coalesced;
        stats->hits = snapshot.hits;
        stats->failures = snapshot.failures;
        stats->retained_bytes = snapshot.cache_bytes + snapshot.pool.cached_bytes;
        stats->live_bytes = snapshot.pool.live_bytes;
        stats->worker_threads = static_cast<uint32_t>(snapshot.executor.workers);
        if (runtime->text_resources) {
            const auto text = runtime->text_resources->stats();
            stats->decompressions += text.decompressions;
            stats->coalesced += text.coalesced;
            stats->hits += text.hits;
            stats->failures += text.failures;
            stats->retained_bytes += text.cache_bytes + text.pool.cached_bytes;
            stats->live_bytes += text.pool.live_bytes;
            stats->worker_threads += static_cast<uint32_t>(text.executor.workers);
        }
        if (runtime->text_session) stats->shared_text_data_bytes = icu_data_size();
        return XL_OK;
    } catch (...) { return XL_INTERNAL_ERROR; }
}
xl_status xl_runtime_trim_resources(xl_runtime* runtime) {
    const auto status = xl_runtime_check_thread(runtime);
    if (status != XL_OK) return status;
    try {
        if (runtime->resources) runtime->resources->trim();
        if (runtime->text_resources) runtime->text_resources->trim();
        return XL_OK;
    } catch (const std::bad_alloc&) { return XL_OUT_OF_MEMORY; }
    catch (...) { return XL_INTERNAL_ERROR; }
}
xl_status xl_resource_request_poll(const xl_resource_request* request, xl_resource_lease** out_lease) {
    if (!out_lease) return XL_INVALID_ARGUMENT;
    *out_lease = nullptr;
    if (!request) return XL_INVALID_ARGUMENT;
    try {
        if (!request->request.ready()) return XL_BUSY;
        auto result = request->request.get();
        const auto status = resource_status(result.status);
        if (status != XL_OK) return status;
        auto lease = std::make_unique<xl_resource_lease>();
        lease->lease = std::move(result.lease);
        *out_lease = lease.release();
        return XL_OK;
    } catch (const std::bad_alloc&) { return XL_OUT_OF_MEMORY; }
    catch (...) { return XL_INTERNAL_ERROR; }
}
xl_status xl_resource_request_cancel(xl_resource_request* request) {
    if (!request) return XL_INVALID_ARGUMENT;
    try { return request->request.cancel() ? XL_OK : XL_BUSY; }
    catch (...) { return XL_INTERNAL_ERROR; }
}
void xl_resource_request_release(xl_resource_request* request) {
    if (!request) return;
    try { request->request.cancel(); } catch (...) {}
    delete request;
}
xl_status xl_resource_lease_data(const xl_resource_lease* lease, const void** data, size_t* size) {
    if (!data || !size) return XL_INVALID_ARGUMENT;
    *data = nullptr;
    *size = 0;
    if (!lease) return XL_INVALID_ARGUMENT;
    const auto bytes = lease->lease.bytes();
    *data = bytes.data();
    *size = bytes.size();
    return XL_OK;
}
void xl_resource_lease_release(xl_resource_lease* lease) { delete lease; }
xl_status xl_runtime_prepare_text(xl_runtime* runtime) {
    const auto status = xl_runtime_check_thread(runtime);
    if (status != XL_OK) return status;
    if (runtime->text_session) return XL_OK;
    try {
        if (!runtime->text_request.valid()) {
            runtime->text_session = try_acquire_icu();
            if (runtime->text_session) return XL_OK;
            runtime->text_request = text_resources(runtime).prewarm(XL_RESOURCE_ICU_DATA);
        }
        if (!runtime->text_request.ready()) return XL_BUSY;
        auto result = runtime->text_request.get();
        const auto loaded = resource_status(result.status);
        if (loaded != XL_OK) { runtime->text_request = {}; return loaded; }
        runtime->text_session = acquire_icu(std::move(result.lease));
        runtime->text_request = {};
        return XL_OK;
    } catch (const std::bad_alloc&) { return XL_OUT_OF_MEMORY; }
    catch (...) { return XL_INTERNAL_ERROR; }
}
xl_status xl_runtime_normalize_utf8(xl_runtime* runtime, const char* input, size_t input_size,
                                  char* output, size_t output_capacity, size_t* required_size) {
    if (!required_size || (!input && input_size) || (!output && output_capacity)
            || input_size >= INT32_MAX) return XL_INVALID_ARGUMENT;
    *required_size = 0;
    const auto prepared = xl_runtime_prepare_text(runtime);
    if (prepared != XL_OK) return prepared;
    try {
        UErrorCode error = U_ZERO_ERROR;
        std::vector<UChar> utf16(input_size + 1);
        int32_t length = 0;
        u_strFromUTF8(utf16.data(), static_cast<int32_t>(utf16.size()), &length, input ? input : "", static_cast<int32_t>(input_size), &error);
        if (U_FAILURE(error)) return XL_INVALID_ARGUMENT;
        const auto* nfc = unorm2_getNFCInstance(&error);
        const auto count = unorm2_normalize(nfc, utf16.data(), length, nullptr, 0, &error);
        if (error != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(error)) return XL_INTERNAL_ERROR;
        error = U_ZERO_ERROR;
        std::vector<UChar> normalized(static_cast<std::size_t>(count) + 1);
        unorm2_normalize(nfc, utf16.data(), length, normalized.data(), static_cast<int32_t>(normalized.size()), &error);
        int32_t bytes = 0;
        u_strToUTF8(nullptr, 0, &bytes, normalized.data(), count, &error);
        if (error != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(error)) return XL_INTERNAL_ERROR;
        *required_size = static_cast<std::size_t>(bytes);
        if (output_capacity < static_cast<std::size_t>(bytes)) return XL_BUFFER_TOO_SMALL;
        if (!bytes) return XL_OK;
        error = U_ZERO_ERROR;
        u_strToUTF8(output, bytes, nullptr, normalized.data(), count, &error);
        return U_SUCCESS(error) ? XL_OK : XL_INTERNAL_ERROR;
    } catch (const std::bad_alloc&) { return XL_OUT_OF_MEMORY; }
    catch (...) { return XL_INTERNAL_ERROR; }
}
}
