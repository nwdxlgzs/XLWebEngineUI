#include "document/dom_internal.h"
#include "style/pseudo_elements.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <utility>

namespace { thread_local int failure_after = -1; long live_blocks = 0; }
void* operator new(std::size_t size) {
    if (failure_after >= 0 && failure_after-- == 0) throw std::bad_alloc();
    if (auto* value = std::malloc(size ? size : 1)) { ++live_blocks; return value; }
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { if (value) { --live_blocks; std::free(value); } }
void operator delete(void* value, std::size_t) noexcept { ::operator delete(value); }
#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
int main() {
    using namespace xl::detail;
    // Both potentially throwing pseudo allocations precede raw DOM acquisition.
    // Inject each failure and check that the successfully allocated C++ owner
    // also unwinds. The subsequent real DOM cycle must remain usable.
    for (const auto point : {0,1}) {
        const auto baseline = live_blocks; bool failed = false;
        failure_after = point;
        try { Document::Impl document; } catch (const std::bad_alloc&) { failed = true; }
        failure_after = -1;
        CHECK(failed && live_blocks == baseline);
    }
    int injected = 0;
    for (int point = 0; point < 32; ++point) {
        Document::Impl probe; const auto origin = probe.create("div");
        bool failed = false; failure_after = point;
        try { probe.pseudos->ensure(probe.resolve(origin),PseudoKind::before); } catch (const std::bad_alloc&) { failed = true; }
        failure_after = -1;
        if (!failed) break;
        ++injected;
        CHECK(probe.nodes.size() == 1 && probe.identities.size() == 1 && probe.pseudos->elements().empty());
        auto* recovered = probe.pseudos->ensure(probe.resolve(origin),PseudoKind::before);
        CHECK(probe.pseudos->generated(recovered) && probe.pseudos->generated(recovered->first_child));
        CHECK(probe.nodes.size() == 3 && probe.identities.size() == 3);
    }
    CHECK(injected >= 8);
    Document::Impl document;
    const auto id = document.create("div"); auto* origin = document.resolve(id);
    auto* pseudo = document.pseudos->ensure(origin,PseudoKind::before);
    auto* text = pseudo->first_child; const auto stable = document.identify(text);
    for (int cycle = 0; cycle < 64; ++cycle) {
        document.set_text(id,std::string(60000,char('a' + cycle % 26)));
        auto& source = lxb_dom_interface_character_data(origin->first_child)->data;
        auto& held = lxb_dom_interface_character_data(text)->data;
        std::swap(source,held); CHECK(held.length == 60000 && held.data);
        document.pseudos->reset();
        CHECK(held.data == nullptr && held.length == 0);
        CHECK(document.pseudos->ensure(origin,PseudoKind::before) == pseudo && document.identify(text) == stable);
    }
    std::cout << "pseudo construction, " << injected << " transactional creation failures and 64 inactive text releases passed\n";
}
