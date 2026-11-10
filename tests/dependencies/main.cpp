#include <SDL3/SDL.h>
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include <unicode/ustring.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb.h>
#include <png.h>
#include <jpeglib.h>
#include <webp/encode.h>
#include <webp/decode.h>
#include <brotli/encode.h>
#include <brotli/decode.h>
#include <zstd.h>
#include <opus/opus.h>
#include <vorbis/codec.h>
#include <ogg/ogg.h>
#include <vpx/vp8dx.h>
#include <vpx/vpx_decoder.h>
#include <mkvparser/mkvparser.h>
#include <lexbor/html/parser.h>
#include <lexbor/html/interfaces/document.h>
#include <quickjs.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); std::exit(1); } } while (false)

static void pixels_and_codecs() {
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(32, 32));
    CHECK(surface);
    surface->getCanvas()->clear(SK_ColorWHITE);
    SkPaint paint;
    paint.setColor(SK_ColorRED);
    surface->getCanvas()->drawRect(SkRect::MakeXYWH(4, 4, 12, 12), paint);
    std::array<unsigned char, 32 * 32 * 4> rgba{};
    const auto info = SkImageInfo::Make(32, 32, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    CHECK(surface->readPixels(info, rgba.data(), 32 * 4, 0, 0));
    CHECK(rgba[(8 * 32 + 8) * 4] == 255 && rgba[(8 * 32 + 8) * 4 + 1] == 0);
    CHECK(rgba[0] == 255 && rgba[1] == 255);
    auto snapshot = surface->makeImageSnapshot();
    CHECK(snapshot);

    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = image.height = 32;
    image.format = PNG_FORMAT_RGBA;
    png_alloc_size_t size = 0;
    CHECK(png_image_write_to_memory(&image, nullptr, &size, 0, rgba.data(), 0, nullptr));
    std::vector<unsigned char> compressed(size);
    CHECK(png_image_write_to_memory(&image, compressed.data(), &size, 0, rgba.data(), 0, nullptr));
    png_image decoded{};
    decoded.version = PNG_IMAGE_VERSION;
    CHECK(png_image_begin_read_from_memory(&decoded, compressed.data(), size));
    decoded.format = PNG_FORMAT_RGBA;
    std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(decoded));
    CHECK(png_image_finish_read(&decoded, nullptr, pixels.data(), 0, nullptr));
    CHECK(pixels.size() == rgba.size() && std::memcmp(pixels.data(), rgba.data(), rgba.size()) == 0);
    png_image_free(&decoded);

    unsigned char* webp = nullptr;
    const auto webp_size = WebPEncodeLosslessRGBA(rgba.data(), 32, 32, 32 * 4, &webp);
    CHECK(webp_size > 0);
    int width = 0, height = 0;
    auto* restored = WebPDecodeRGBA(webp, webp_size, &width, &height);
    CHECK(restored && width == 32 && height == 32 && std::memcmp(restored, rgba.data(), rgba.size()) == 0);
    WebPFree(restored);
    WebPFree(webp);
    jpeg_error_mgr error{};
    jpeg_decompress_struct jpeg{};
    jpeg.err = jpeg_std_error(&error);
    jpeg_create_decompress(&jpeg);
    jpeg_destroy_decompress(&jpeg);
    std::puts("Skia raster/snapshot, PNG, WebP, JPEG: passed");
}

static void text_and_languages() {
    const UChar input[] = {0x0041, 0x4E2D, 0xD83D, 0xDE00};
    char utf8[24]{};
    int32_t length = 0;
    UErrorCode status = U_ZERO_ERROR;
    u_strToUTF8(utf8, sizeof(utf8), &length, input, 4, &status);
    CHECK(U_SUCCESS(status) && length == 8);
    FT_Library ft = nullptr;
    CHECK(FT_Init_FreeType(&ft) == 0);
    FT_Done_FreeType(ft);
    auto* buffer = hb_buffer_create();
    hb_buffer_add_utf8(buffer, utf8, length, 0, length);
    hb_buffer_guess_segment_properties(buffer);
    CHECK(hb_buffer_get_length(buffer) == 3 && hb_buffer_get_direction(buffer) == HB_DIRECTION_LTR);
    hb_buffer_destroy(buffer);
    auto* parser = lxb_html_parser_create();
    CHECK(parser && lxb_html_parser_init(parser) == LXB_STATUS_OK);
    constexpr char html[] = "<!doctype html><title>XL</title><main><button aria-label='OK'>ok</button></main>";
    auto* document = lxb_html_parse(parser, reinterpret_cast<const lxb_char_t*>(html), sizeof(html) - 1);
    CHECK(document && lxb_html_document_body_element(document));
    lxb_html_document_destroy(document);
    lxb_html_parser_destroy(parser);
    auto* runtime = JS_NewRuntime();
    auto* context = JS_NewContext(runtime);
    CHECK(runtime && context);
    constexpr char script[] = "(() => { const values = [20, 21]; return values.map(x => x + 1)[1] * 2; })()";
    JSValue value = JS_Eval(context, script, sizeof(script) - 1, "integration.js", JS_EVAL_TYPE_GLOBAL);
    int32_t result = 0;
    CHECK(!JS_IsException(value) && JS_ToInt32(context, &result, value) == 0 && result == 44);
    JS_FreeValue(context, value);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
    std::puts("ICU memory conversion, FreeType, HarfBuzz, Lexbor DOM, QuickJS: passed");
}

static void compression_and_media() {
    constexpr char text[] = "lazy in-memory resources: compressed bytes remain embedded until first use";
    std::array<unsigned char, 256> encoded{}, decoded{};
    const auto length = ZSTD_compress(encoded.data(), encoded.size(), text, sizeof(text), 3);
    CHECK(!ZSTD_isError(length));
    CHECK(ZSTD_decompress(decoded.data(), decoded.size(), encoded.data(), length) == sizeof(text));
    CHECK(std::memcmp(decoded.data(), text, sizeof(text)) == 0);
    size_t brotli_size = encoded.size();
    CHECK(BrotliEncoderCompress(4, BROTLI_DEFAULT_WINDOW, BROTLI_MODE_GENERIC, sizeof(text),
                               reinterpret_cast<const uint8_t*>(text), &brotli_size, encoded.data()));
    size_t restored_size = decoded.size();
    CHECK(BrotliDecoderDecompress(brotli_size, encoded.data(), &restored_size, decoded.data()) == BROTLI_DECODER_RESULT_SUCCESS);
    CHECK(restored_size == sizeof(text) && std::memcmp(decoded.data(), text, sizeof(text)) == 0);
    int error = 0;
    auto* encoder = opus_encoder_create(48000, 1, OPUS_APPLICATION_AUDIO, &error);
    CHECK(encoder && error == OPUS_OK);
    auto* decoder = opus_decoder_create(48000, 1, &error);
    CHECK(decoder && error == OPUS_OK);
    std::array<opus_int16, 960> samples{}, audio{};
    const int packet = opus_encode(encoder, samples.data(), samples.size(), encoded.data(), encoded.size());
    CHECK(packet > 0 && opus_decode(decoder, encoded.data(), packet, audio.data(), audio.size(), 0) == 960);
    opus_encoder_destroy(encoder);
    opus_decoder_destroy(decoder);
    vorbis_info vorbis{};
    vorbis_info_init(&vorbis);
    vorbis_info_clear(&vorbis);
    ogg_sync_state ogg{};
    CHECK(ogg_sync_init(&ogg) == 0 && ogg_sync_clear(&ogg) == 0);
    for (auto* iface : {vpx_codec_vp8_dx(), vpx_codec_vp9_dx()}) {
        vpx_codec_ctx_t codec{};
        CHECK(vpx_codec_dec_init(&codec, iface, nullptr, 0) == VPX_CODEC_OK);
        CHECK(vpx_codec_destroy(&codec) == VPX_CODEC_OK);
    }
    int major = 0, minor = 0, build = 0, revision = 0;
    mkvparser::GetVersion(major, minor, build, revision);
    CHECK(major > 0);
    std::puts("Zstd/Brotli roundtrip, Opus encode/decode, Vorbis/Ogg, VP8/VP9 init, WebM: passed");
}

int main() {
    CHECK(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy"));
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    auto* window = SDL_CreateWindow("XL dependency integration", 32, 32, SDL_WINDOW_HIDDEN);
    CHECK(window);
    CHECK(SDL_GetWindowSurface(window));
    pixels_and_codecs();
    text_and_languages();
    compression_and_media();
    SDL_DestroyWindow(window);
    SDL_Quit();
    std::puts("All compiled dependency calls passed. This is dependency integration, not a complete webpage engine.");
}
