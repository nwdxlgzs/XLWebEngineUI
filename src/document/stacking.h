#pragma once
#include "document.h"

namespace xl::detail {
void build_display(Document::Impl &document, DocumentFrame &frame,
                   xl_node_id root);
}
