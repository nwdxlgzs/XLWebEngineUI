#include <XLWebEngineUI/xl.hpp>
#include <iostream>
int main() {
    xl::Runtime runtime;
    std::cout << "XLWebEngineUI " << xl_version_string() << "\n";
    return xl_runtime_check_thread(runtime.get()) == XL_OK ? 0 : 1;
}
