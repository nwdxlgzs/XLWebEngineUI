#include <zstd.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) return 3;
    std::vector<char> plain((std::istreambuf_iterator<char>(input)), {});
    if (!input.eof() && input.fail()) return 3;
    auto* context = ZSTD_createCCtx();
    if (!context) return 4;
    if (ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_compressionLevel, 9))
            || ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_windowLog, 23))
            || ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_checksumFlag, 1))) {
        ZSTD_freeCCtx(context);
        return 5;
    }
    std::vector<char> compressed(ZSTD_compressBound(plain.size()));
    const auto size = ZSTD_compress2(context, compressed.data(), compressed.size(), plain.data(), plain.size());
    ZSTD_freeCCtx(context);
    if (ZSTD_isError(size) || size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) return 6;
    std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
    output.write(compressed.data(), static_cast<std::streamsize>(size));
    output.close();
    if (!output) return 7;
    std::printf("%zu -> %zu compressed bytes; checksum and bounded window enabled\n", plain.size(), size);
}
