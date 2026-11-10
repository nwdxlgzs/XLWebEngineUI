#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
/* Simulate a host's independently linked Zstandard and ordinary ICU symbols.
   SDK resource decompression must bind to its private renamed implementation. */
unsigned long long ZSTD_getFrameContentSize(const void* data, size_t size) {
    (void)data; (void)size;
    return 0;
}
size_t ZSTD_decompress(void* destination, size_t size, const void* source, size_t count) {
    (void)destination; (void)size; (void)source; (void)count;
    return 0;
}
void u_init_79(int32_t* error) { if (error) *error = 1; }
void u_cleanup_79(void) {}
int g_debuglevel = 0;
void* POOL_create(size_t threads, size_t capacity) {
    (void)threads; (void)capacity;
    return NULL;
}
bool SDL_Init(uint32_t flags) { (void)flags; return false; }
void* JS_NewRuntime(void) { return NULL; }
void* lxb_html_document_create(void) { return NULL; }
