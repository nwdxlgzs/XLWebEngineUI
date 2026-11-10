#include "style/nesting.h"
#include <cstdlib>
#include <iostream>
#define CHECK(value)                                                                                                   \
    do {                                                                                                               \
        if (!(value)) {                                                                                                \
            std::cerr << __LINE__ << ": " #value "\n";                                                                 \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)
int main() {
    using namespace xl::detail;
    NestedStyles cache;
    cache.begin();
    auto plain = cache.get(std::make_shared<const std::string>("#x{color:red}"));
    CHECK(!plain->sheet);
    const auto input = std::make_shared<const std::string>(
        ".parent{span{color:blue}> p{color:green}+ .peer{color:lime}~ .later{color:red}}");
    auto nested = cache.get(input);
    CHECK(nested->sheet);
    std::cout << *nested->source << '\n';
    CHECK(nested->source->find(":is(.parent) ~ .later") != std::string::npos);
    cache.finish();
    cache.begin();
    CHECK(cache.get(std::make_shared<const std::string>(*input)) == nested);
    cache.finish();
    cache.begin();
    CHECK(cache.get(input, reinterpret_cast<const void *>(1)) != nested);
    cache.finish();
    CHECK(*nested->authored == *input);
    NestedSheet complex(std::make_shared<const std::string>(".parent{.child{&.active{color:blue}}}"));
    auto *rule = lxb_css_rule_style(lxb_css_rule_list(complex.sheet->root)->first);
    CHECK(lxb_css_selector_sp_a(rule->selector->specificity) == 0);
    CHECK(lxb_css_selector_sp_b(rule->selector->specificity) == 3);
    CHECK(lxb_css_selector_sp_c(rule->selector->specificity) == 0);
    try {
        NestedSheet oversized(std::make_shared<const std::string>(16 * 1024 * 1024 + 1, ' '));
        CHECK(false);
    } catch (const DocumentFailure &error) {
        CHECK(error.status == XL_UNSUPPORTED);
    }
    std::string deep;
    for (int i = 0; i < 66; ++i)
        deep += ".x{";
    deep += "color:red";
    deep.append(66, '}');
    try {
        NestedSheet limited(std::make_shared<const std::string>(deep));
        CHECK(false);
    } catch (const DocumentFailure &error) {
        CHECK(error.status == XL_UNSUPPORTED);
    }
    std::cout << "nested source/cache identity and explicit depth/byte limits passed\n";
}
