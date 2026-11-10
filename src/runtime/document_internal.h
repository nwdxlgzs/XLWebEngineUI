#pragma once
#include "runtime_internal.h"
#include "document/document.h"

struct xl_document {
    xl_runtime* runtime = nullptr;
    std::unique_ptr<xl::detail::Document> core;
    size_t views = 0;
    bool active = false;
};
