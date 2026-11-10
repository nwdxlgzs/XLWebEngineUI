// Host tools reuse the locked no-file-I/O ICU libraries with explicit memory data.
// This adapter is never linked into the SDK or loaded by an application.
#include <unicode/uclean.h>
#include <unicode/udata.h>
#include <unicode/ucnv.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>
#include "tool_main.h"

int main(int argc, char** argv) {
#ifdef _WIN32
    auto* location = _wgetenv(L"XL_ICU_TOOL_DATA");
#else
    auto* location = std::getenv("XL_ICU_TOOL_DATA");
#endif
    if (!location) return 100;
    std::ifstream input(std::filesystem::path(location), std::ios::binary | std::ios::ate);
    if (!input) return 101;
    const auto count = input.tellg();
    if (count <= 0) return 102;
    std::vector<std::max_align_t> data((size_t(count) + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(data.data()), count)) return 103;
    UErrorCode error = U_ZERO_ERROR;
    udata_setFileAccess(UDATA_NO_FILES, &error);
    udata_setCommonData(data.data(), &error);
    if (U_FAILURE(error)) return 104;
    ucnv_setDefaultName("UTF-8");
    const int result = xl_icu_data_tool_main(argc, argv);
    u_cleanup();
    return result;
}
