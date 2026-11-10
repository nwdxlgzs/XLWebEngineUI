#include "catalog.h"
#include <unicode/ubrk.h>
#include <unicode/uchar.h>
#include <unicode/uclean.h>
#include <unicode/udata.h>
#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#include <unicode/ubidi.h>
#include <unicode/ucol.h>
#include <unicode/ucnv.h>
#include <cstdio>
#include <cstdlib>

using namespace xl::detail;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #condition); std::abort(); } } while (false)

int main() {
    ResourceService resources(builtin_resource_catalog(), 2, 8, 64 * 1024 * 1024);
    CHECK(resources.stats().decompressions == 0 && resources.stats().pool.system_allocations == 0);
    auto result = resources.request(1).get();
    CHECK(result.status == ResourceStatus::ready && result.lease.bytes().size() > 1024 * 1024
          && result.lease.bytes().size() < 16 * 1024 * 1024);
    CHECK(resources.stats().decompressions == 1);
    UErrorCode error = U_ZERO_ERROR;
    udata_setFileAccess(UDATA_NO_FILES, &error);
    udata_setCommonData(result.lease.bytes().data(), &error);
    u_init(&error);
    CHECK(U_SUCCESS(error));
    CHECK(u_hasBinaryProperty(0x1F600, UCHAR_EMOJI));
    const UChar lower[] = {0x0069};
    UChar upper[8]{};
    CHECK(u_strToUpper(upper, 8, lower, 1, "tr", &error) == 1 && upper[0] == 0x0130);
    const auto* normalizer = unorm2_getNFCInstance(&error);
    const UChar decomposed[] = {0x0065, 0x0301};
    UChar composed[8]{};
    CHECK(unorm2_normalize(normalizer, decomposed, 2, composed, 8, &error) == 1 && composed[0] == 0x00E9);
    const UChar text[] = {0x0065, 0x0301, 0x4E2D, 0xD83D, 0xDE00};
    auto* breaks = ubrk_open(UBRK_CHARACTER, "zh", text, 5, &error);
    CHECK(U_SUCCESS(error) && breaks);
    CHECK(ubrk_first(breaks) == 0 && ubrk_next(breaks) == 2 && ubrk_next(breaks) == 3 && ubrk_next(breaks) == 5);
    CHECK(ubrk_next(breaks) == UBRK_DONE);
    ubrk_close(breaks);
    // Exercise actual locale data and dictionary consumers after Chromium filtering.
    for (const char* locale : {"en", "zh", "zh_Hant@lb=strict", "ja@lb=strict", "ko@lb=loose",
                               "ar", "he", "th", "lo", "km", "my", "hi", "tr"}) {
        for (auto kind : {UBRK_CHARACTER, UBRK_WORD, UBRK_LINE, UBRK_SENTENCE}) {
            error = U_ZERO_ERROR;
            auto* iterator = ubrk_open(kind, locale, text, 5, &error);
            CHECK(U_SUCCESS(error) && iterator);
            CHECK(ubrk_first(iterator) == 0);
            int32_t last = 0;
            for (int32_t next; (next = ubrk_next(iterator)) != UBRK_DONE;) {
                CHECK(next > last && next <= 5);
                last = next;
            }
            CHECK(last == 5);
            ubrk_close(iterator);
        }
    }
    const UChar thai[] = {0x0e20,0x0e32,0x0e29,0x0e32,0x0e44,0x0e17,0x0e22,
                         0x0e20,0x0e32,0x0e29,0x0e32,0x0e44,0x0e17,0x0e22};
    error = U_ZERO_ERROR;
    breaks = ubrk_open(UBRK_WORD, "th", thai, 14, &error);
    CHECK(U_SUCCESS(error) && breaks);
    CHECK(ubrk_first(breaks) == 0 && ubrk_next(breaks) == 4 && ubrk_next(breaks) == 7
          && ubrk_next(breaks) == 11 && ubrk_next(breaks) == 14 && ubrk_next(breaks) == UBRK_DONE);
    ubrk_close(breaks);
    const UChar mixed[] = {0x0061,0x0020,0x05d0,0x05d1};
    error = U_ZERO_ERROR;
    auto* bidi = ubidi_open();
    CHECK(bidi);
    ubidi_setPara(bidi, mixed, 4, 0, nullptr, &error);
    CHECK(U_SUCCESS(error) && ubidi_getDirection(bidi) == UBIDI_MIXED && (ubidi_getLevelAt(bidi, 2) & 1));
    ubidi_close(bidi);
    error = U_ZERO_ERROR;
    auto* collation = ucol_open("fr", &error);
    CHECK(U_SUCCESS(error) && collation);
    const UChar a[] = {0x0061}, z[] = {0x007a};
    CHECK(ucol_strcoll(collation, a, 1, z, 1) == UCOL_LESS);
    ucol_close(collation);
    for (const char* encoding : {"UTF-8", "big5-html", "shift_jis-html", "windows-1252-html"}) {
        error = U_ZERO_ERROR;
        auto* converter = ucnv_open(encoding, &error);
        CHECK(U_SUCCESS(error) && converter);
        UChar decoded[8]{};
        CHECK(ucnv_toUChars(converter, decoded, 8, "ABC", 3, &error) == 3 && U_SUCCESS(error) && decoded[0] == 0x41);
        ucnv_close(converter);
    }
    // ICU retains pointers into the lease; cleanup precedes the last release.
    resources.trim();
    CHECK(u_hasBinaryProperty(0x1F600, UCHAR_EMOJI));
    CHECK(resources.stats().decompressions == 1);
    u_cleanup();
    result.lease = {};
    CHECK(resources.shutdown());
    std::puts("Embedded compressed ICU: lazy memory registration, Unicode data, locale, normalization and grapheme access passed");
}
