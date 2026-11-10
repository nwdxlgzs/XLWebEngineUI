#include <XLWebEngineUI/xl.hpp>
#include <cstring>
#include <iostream>
#include <thread>

#define CHECK(condition) do { if (!(condition)) { std::cerr << "line " << __LINE__ << ": " #condition "\n"; return 1; } } while (false)

int main() {
    CHECK(std::strstr(xl_version_string(), "web-core") != nullptr);
    xl_runtime_options options;
    std::memset(&options, 0x5a, sizeof(options));
    const auto unchanged = options;
    CHECK(xl_runtime_options_init(&options, sizeof(options) - 1) == XL_ABI_MISMATCH);
    CHECK(std::memcmp(&options, &unchanged, sizeof(options)) == 0);
    CHECK(xl_runtime_options_init(&options, sizeof(options)) == XL_OK);
    xl_runtime* runtime = nullptr;
    options.abi_version++;
    CHECK(xl_runtime_create(&options, &runtime) == XL_ABI_MISMATCH);
    CHECK(runtime == nullptr);
    CHECK(xl_runtime_options_init(&options, sizeof(options)) == XL_OK);
    CHECK(xl_runtime_create(&options, &runtime) == XL_OK);
    CHECK(xl_runtime_check_thread(runtime) == XL_OK);
    xl_status foreign_check = XL_OK, foreign_destroy = XL_OK;
    std::thread foreign([&] {
        foreign_check = xl_runtime_check_thread(runtime);
        foreign_destroy = xl_runtime_destroy(runtime);
    });
    foreign.join();
    CHECK(foreign_check == XL_WRONG_THREAD);
    CHECK(foreign_destroy == XL_WRONG_THREAD);
    CHECK(xl_runtime_destroy(runtime) == XL_OK);
    CHECK(xl_runtime_destroy(nullptr) == XL_OK);
    xl::Runtime wrapper;
    CHECK(xl_runtime_check_thread(wrapper.get()) == XL_OK);
    std::cout << "runtime ABI and UI-thread lifetime contract passed\n";
    return 0;
}
