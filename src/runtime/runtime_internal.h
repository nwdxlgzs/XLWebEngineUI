#pragma once
#include <XLWebEngineUI/xl.h>
#include "resources/service.h"
#include "resources/ui_content.h"
#include <memory>
#include <thread>

struct xl_runtime {
    xl_runtime_options options;
    std::thread::id owner = std::this_thread::get_id();
    xl::detail::Allocator allocator;
    std::unique_ptr<xl::detail::ResourceService> resources;
    // Private shared ICU must never retain a host allocator's context.
    std::unique_ptr<xl::detail::ResourceService> text_resources;
    xl::detail::ResourceRequest text_request;
    std::shared_ptr<void> text_session;
    std::size_t view_count = 0;
    std::size_t view_calls = 0;
    std::size_t document_count = 0;
    xl::detail::UiMounts content;
};
