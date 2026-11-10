#include "icu_session.h"
#include <unicode/uclean.h>
#include <unicode/udata.h>
#include <mutex>
#include <stdexcept>
#include <utility>
extern "C" void xl_skia_purge_icu_cache();

namespace xl::detail {
namespace {
std::mutex icu_mutex;
std::size_t users = 0;
ResourceLease registered_data;

struct Session {
    ~Session() {
        if (!active) return;
        ResourceLease released;
        {
            std::lock_guard lock(icu_mutex);
            if (--users == 0) {
                xl_skia_purge_icu_cache();
                u_cleanup();
                released = std::move(registered_data);
            }
        }
    }
    bool active = false;
};
}

std::shared_ptr<void> try_acquire_icu() {
    auto session = std::make_shared<Session>();
    std::lock_guard lock(icu_mutex);
    if (users == 0) return {};
    ++users;
    session->active = true;
    return session;
}

std::size_t icu_data_size() {
    std::lock_guard lock(icu_mutex);
    return registered_data.bytes().size();
}

std::shared_ptr<void> acquire_icu(ResourceLease data) {
    // Allocate bookkeeping before touching global ICU state. Failure leaves
    // neither registered data nor a partially published session behind.
    auto session = std::make_shared<Session>();
    std::lock_guard lock(icu_mutex);
    if (users == 0) {
        UErrorCode error = U_ZERO_ERROR;
        udata_setFileAccess(UDATA_NO_FILES, &error);
        udata_setCommonData(data.bytes().data(), &error);
        u_init(&error);
        if (U_FAILURE(error)) {
            u_cleanup();
            throw std::runtime_error("private ICU data registration failed");
        }
        registered_data = std::move(data);
    }
    ++users;
    session->active = true;
    return session;
}
}
