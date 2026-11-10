// Build-time ICU/CLDR extraction only; never linked into the SDK.
#include <unicode/udata.h>
#include <unicode/ures.h>
#include <unicode/ulocdata.h>
#include <unicode/ustring.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<char> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (bytes.empty()) return 3;
    UErrorCode status = U_ZERO_ERROR;
    udata_setCommonData(bytes.data(), &status);
    udata_setFileAccess(UDATA_NO_FILES, &status);
    auto* locales = ures_openAvailableLocales(nullptr, &status);
    if (U_FAILURE(status)) return 4;
    int32_t size;
    std::vector<std::string> names{"root"};
    while (const auto* locale = uenum_next(locales, &size, &status)) names.emplace_back(locale);
    uenum_close(locales);
    for (const auto& name : names) {
        const auto* locale = name.c_str();
        status = U_ZERO_ERROR;
        auto* bundle = ulocdata_open(locale, &status);
        if (U_FAILURE(status)) { ulocdata_close(bundle); continue; }
        std::cout << locale;
        for (int delimiter = 0; delimiter < ULOCDATA_DELIMITER_COUNT; ++delimiter) {
            UChar value[32];
            const auto length = ulocdata_getDelimiter(bundle, static_cast<ULocaleDataDelimiterType>(delimiter), value, 32, &status);
            char utf8[128]; int32_t count;
            u_strToUTF8(utf8, sizeof(utf8), &count, value, length, &status);
            if (U_FAILURE(status)) return 5;
            std::cout << '\t';
            constexpr char hex[] = "0123456789abcdef";
            for (int32_t i = 0; i < count; ++i) { const auto b = static_cast<unsigned char>(utf8[i]); std::cout << hex[b >> 4] << hex[b & 15]; }
        }
        std::cout << '\n';
        ulocdata_close(bundle);
    }
    return U_FAILURE(status) ? 6 : 0;
}
