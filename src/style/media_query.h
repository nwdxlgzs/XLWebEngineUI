#pragma once
#include "base/view_environment.h"
#include <memory>
#include <string>
#include <string_view>

namespace xl::detail {
// A query is compiled independently of document styles, fonts and ICU data.
class MediaQuery {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    explicit MediaQuery(std::string_view query);
    ~MediaQuery();
    bool matches(const ViewEnvironment &environment) const;
    const std::string &serialized() const;
};
} // namespace xl::detail
