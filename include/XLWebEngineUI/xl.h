#ifndef XLWEBENGINEUI_XL_H
#define XLWEBENGINEUI_XL_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && !defined(XL_STATIC)
# if defined(XL_BUILDING_LIBRARY)
#  define XL_API __declspec(dllexport)
# else
#  define XL_API __declspec(dllimport)
# endif
#elif defined(__GNUC__)
# define XL_API __attribute__((visibility("default")))
#else
# define XL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define XL_ABI_VERSION 1u

typedef enum xl_status {
    XL_OK = 0,
    XL_INVALID_ARGUMENT = 1,
    XL_ABI_MISMATCH = 2,
    XL_OUT_OF_MEMORY = 3,
    XL_WRONG_THREAD = 4,
    XL_BUSY = 5,
    XL_INTERNAL_ERROR = 6,
    XL_UNSUPPORTED = 7,
    XL_CANCELED = 8,
    XL_CLOSED = 9,
    XL_NOT_FOUND = 10,
    XL_CORRUPT_DATA = 11,
    XL_BUFFER_TOO_SMALL = 12,
    XL_SCRIPT_ERROR = 13,
    XL_TIMEOUT = 14
} xl_status;

typedef struct xl_runtime xl_runtime;
typedef struct xl_resource_request xl_resource_request;
typedef struct xl_resource_lease xl_resource_lease;
typedef struct xl_view xl_view;
typedef struct xl_document xl_document;
typedef struct xl_content xl_content;
typedef struct xl_ax_update xl_ax_update;

typedef enum xl_clipboard_command {
    XL_CLIPBOARD_COPY = 1, XL_CLIPBOARD_CUT = 2, XL_CLIPBOARD_PASTE = 3
} xl_clipboard_command;
typedef xl_status (*xl_clipboard_read_fn)(void* context, xl_content** out_content);
typedef xl_status (*xl_clipboard_write_fn)(void* context, const char* utf8, size_t size);
typedef struct xl_clipboard_handler {
    uint32_t struct_size;
    uint32_t reserved;
    void* context;
    xl_clipboard_read_fn read;
    xl_clipboard_write_fn write;
} xl_clipboard_handler;
typedef uint64_t xl_node_id;
typedef enum xl_text_affinity {
    XL_TEXT_UPSTREAM = 0,
    XL_TEXT_DOWNSTREAM = 1
} xl_text_affinity;
typedef struct xl_text_position {
    uint32_t struct_size;
    uint32_t offset_utf16;
    xl_node_id node;
    uint32_t affinity;
    uint32_t reserved;
} xl_text_position;
typedef struct xl_text_selection {
    uint32_t struct_size;
    uint32_t reserved;
    xl_text_position anchor;
    xl_text_position focus;
} xl_text_selection;

typedef struct xl_node_bounds { float x, y, width, height; } xl_node_bounds;
typedef enum xl_ax_role {
    XL_AX_DOCUMENT = 0, XL_AX_GENERIC = 1, XL_AX_HEADING = 2, XL_AX_PARAGRAPH = 3,
    XL_AX_TEXT = 4, XL_AX_BUTTON = 5, XL_AX_LINK = 6, XL_AX_TEXT_FIELD = 7,
    XL_AX_CHECKBOX = 8, XL_AX_IMAGE = 9, XL_AX_STATUS = 10, XL_AX_ALERT = 11,
    XL_AX_LOG = 12, XL_AX_TIMER = 13, XL_AX_MARQUEE = 14, XL_AX_RADIO = 15
} xl_ax_role;
#define XL_AX_FOCUSED 1u
#define XL_AX_DISABLED 2u
#define XL_AX_CHECKED 4u
#define XL_AX_OFFSCREEN 8u
#define XL_AX_READ_ONLY 16u
#define XL_AX_PROTECTED 32u
#define XL_AX_CHECKABLE 64u
#define XL_AX_MIXED 128u
#define XL_AX_SELECTED 256u
#define XL_AX_EXPANDED 512u
#define XL_AX_COLLAPSED 1024u
#define XL_AX_REQUIRED 2048u
#define XL_AX_INVALID 4096u
#define XL_AX_MULTILINE 8192u
#define XL_AX_BUSY 16384u
#define XL_AX_LIVE_POLITE_REGION 32768u
#define XL_AX_LIVE_ASSERTIVE_REGION 65536u
#define XL_AX_ACTION_FOCUS 1u
#define XL_AX_ACTION_ACTIVATE 2u
#define XL_AX_ACTION_EXPAND 4u
#define XL_AX_ACTION_COLLAPSE 8u
typedef struct xl_ax_node {
    uint32_t struct_size, role;
    xl_node_id node, parent;
    xl_node_bounds bounds;
    uint32_t states, actions;
} xl_ax_node;

#define XL_AX_UPDATE_RESET 1u
#define XL_AX_UPDATE_OVERFLOW 2u
#define XL_AX_CHANGE_ROLE 1u
#define XL_AX_CHANGE_NAME 2u
#define XL_AX_CHANGE_VALUE 4u
#define XL_AX_CHANGE_STATES 8u
#define XL_AX_CHANGE_ACTIONS 16u
#define XL_AX_CHANGE_BOUNDS 32u
#define XL_AX_CHANGE_PARENT 64u
#define XL_AX_LIVE_POLITE 1u
#define XL_AX_LIVE_ASSERTIVE 2u
typedef enum xl_ax_event_type {
    XL_AX_EVENT_RESET = 0, XL_AX_EVENT_NODE_ADDED = 1, XL_AX_EVENT_NODE_REMOVED = 2,
    XL_AX_EVENT_NODE_CHANGED = 3, XL_AX_EVENT_CHILDREN_CHANGED = 4, XL_AX_EVENT_FOCUS_CHANGED = 5,
    XL_AX_EVENT_SELECTION_CHANGED = 6, XL_AX_EVENT_LIVE_REGION = 7
} xl_ax_event_type;
typedef struct xl_ax_update_info {
    uint32_t struct_size, flags;
    uint64_t sequence, document, revision, node_count, event_count;
} xl_ax_update_info;
typedef struct xl_ax_event {
    uint32_t struct_size, type, properties, flags;
    xl_node_id node, related;
    /* Selection event: node=focused text control or reading focus; cleared=0.
       Other events leave selection zeroed. Live events use flags=politeness.
       Removed nodes use related=old parent; focus uses related=previous focus. */
    xl_text_selection selection;
} xl_ax_event;
typedef struct xl_document_stats {
    uint32_t struct_size, reserved;
    uint64_t revision, nodes, layout_frames, unsupported_layout_nodes;
} xl_document_stats;

#define XL_SCROLL_X 1u
#define XL_SCROLL_Y 2u
#define XL_SCROLL_USER_X 4u
#define XL_SCROLL_USER_Y 8u
typedef struct xl_scroll_info {
    uint32_t struct_size, flags;
    float x, y;
    float client_width, client_height, scroll_width, scroll_height;
    float min_x, min_y, max_x, max_y;
} xl_scroll_info;

#define XL_KEY_DOWN 1u
#define XL_KEY_UP 2u
#define XL_MOD_SHIFT 1u
#define XL_MOD_CONTROL 2u
#define XL_MOD_ALT 4u
#define XL_MOD_META 8u
#define XL_MOD_CAPS_LOCK 16u
#define XL_MOD_NUM_LOCK 32u
typedef struct xl_key_event {
    uint32_t struct_size, type, modifiers, repeat;
    uint32_t location, reserved;
    const char* key_utf8; /* DOM key, e.g. "Tab", "Enter", "a", or " ". */
    const char* code_utf8; /* Physical DOM code, e.g. "KeyA"; may be empty. */
} xl_key_event;

#define XL_POINTER_DOWN 1u
#define XL_POINTER_UP 2u
#define XL_POINTER_MOVE 3u
#define XL_POINTER_CANCEL 4u
/* Primary pointer bridge in drawable coordinates. Supplies native focus,
   selection and click activation; full DOM PointerEvent/capture is separate. */
typedef struct xl_pointer_event {
    uint32_t struct_size, type, modifiers, reserved;
    float x, y;
} xl_pointer_event;

#define XL_EDIT_MULTILINE 1u
#define XL_EDIT_READ_ONLY 2u
#define XL_EDIT_PASSWORD 4u
#define XL_EDIT_COMPOSING 8u
typedef struct xl_edit_state {
    uint32_t struct_size, flags;
    xl_node_id node;
    uint32_t anchor_utf16, focus_utf16, composition_start_utf16, composition_end_utf16;
    uint64_t revision;
} xl_edit_state;
#define XL_COMPOSITION_UPDATE 1u
#define XL_COMPOSITION_COMMIT 2u
#define XL_COMPOSITION_CANCEL 3u
typedef struct xl_composition {
    uint32_t struct_size, type, selection_start_utf16, selection_end_utf16;
    const char* text_utf8;
    size_t text_size;
} xl_composition;

typedef enum xl_view_kind { XL_VIEW_WINDOW = 0, XL_VIEW_NATIVE = 1, XL_VIEW_OFFSCREEN = 2 } xl_view_kind;
/* Zero follows the View environment. Offscreen/unknown defaults to light,
   no reduced motion, no contrast preference and no forced colors. */
#define XL_MEDIA_AUTO 0u
#define XL_MEDIA_COLOR_LIGHT 1u
#define XL_MEDIA_COLOR_DARK 2u
#define XL_MEDIA_MOTION_NO_PREFERENCE 1u
#define XL_MEDIA_MOTION_REDUCE 2u
#define XL_MEDIA_CONTRAST_NO_PREFERENCE 1u
#define XL_MEDIA_CONTRAST_MORE 2u
#define XL_MEDIA_CONTRAST_LESS 3u
#define XL_MEDIA_CONTRAST_CUSTOM 4u
#define XL_MEDIA_FORCED_NONE 1u
#define XL_MEDIA_FORCED_ACTIVE 2u
typedef struct xl_media_preferences {
    uint32_t struct_size, color_scheme, reduced_motion, contrast, forced_colors, reserved;
} xl_media_preferences;
typedef enum xl_native_kind { XL_NATIVE_NONE = 0, XL_NATIVE_WIN32 = 1, XL_NATIVE_X11 = 2 } xl_native_kind;
#define XL_VIEW_HIDDEN 1u
#define XL_VIEW_RESIZABLE 2u
#define XL_PUMP_NATIVE_EVENTS 1u
#define XL_FRAME_REDRAWN 1u
#define XL_FRAME_PRESENTED 2u
typedef struct xl_view_options {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t kind;
    uint32_t flags;
    int32_t width;
    int32_t height;
    uint32_t background_argb;
    uint32_t native_kind;
    uintptr_t native_window;
    const char* title_utf8;
} xl_view_options;
/* Low-level retained paint coordinates are drawable pixels, not CSS units. */
typedef struct xl_paint_rect {
    float x, y, width, height;
    uint32_t argb;
    uint32_t reserved;
} xl_paint_rect;
typedef struct xl_view_stats {
    uint32_t struct_size;
    uint32_t needs_frame;
    int32_t pixel_width, pixel_height;
    uint32_t close_requested;
    uint32_t reserved;
    uint64_t raster_frames;
    uint64_t presented_frames;
    uint64_t skipped_frames;
    uint64_t rasterized_pixels;
    uint64_t uploaded_pixels;
} xl_view_stats;
typedef enum xl_graphics_backend {
    XL_GRAPHICS_RASTER = 0, XL_GRAPHICS_AUTO = 1, XL_GRAPHICS_D3D12 = 2,
    XL_GRAPHICS_ANGLE_D3D11 = 3, XL_GRAPHICS_VULKAN = 4,
    XL_GRAPHICS_ANGLE_OPENGL = 5, XL_GRAPHICS_SWIFTSHADER = 6,
    XL_GRAPHICS_OPENGL = 7,
    XL_GRAPHICS_DAWN = 8, XL_GRAPHICS_DAWN_SWIFTSHADER = 9
} xl_graphics_backend;
typedef enum xl_shader_compiler {
    XL_SHADER_COMPILER_NONE = 0, XL_SHADER_COMPILER_FXC = 1, XL_SHADER_COMPILER_DXC = 2
} xl_shader_compiler;
typedef struct xl_graphics_info {
    uint32_t struct_size, requested, active, attempted_mask, failed_mask, shader_compiler;
    uint64_t generation, recoveries;
} xl_graphics_info;

typedef void* (*xl_allocate_fn)(void* user, size_t size, size_t alignment);
typedef void (*xl_deallocate_fn)(void* user, void* data, size_t size, size_t alignment);

typedef struct xl_runtime_options {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t worker_threads;
    uint32_t flags;
    uint64_t resource_cache_budget_bytes;
} xl_runtime_options;

#define XL_RESOURCE_ICU_DATA 1u
typedef struct xl_resource_stats {
    uint32_t struct_size;
    uint32_t accepting;
    uint64_t decompressions;
    uint64_t coalesced;
    uint64_t hits;
    uint64_t failures;
    uint64_t retained_bytes;
    uint64_t live_bytes;
    uint32_t worker_threads;
    uint32_t reserved;
    /* Shared pinned ICU bytes; may overlap live_bytes and other runtimes. */
    uint64_t shared_text_data_bytes;
} xl_resource_stats;

/* Static strings are library-owned and valid until the library is unloaded. */
XL_API const char* xl_version_string(void);
XL_API const char* xl_status_string(xl_status status);
/* Size is supplied by the caller so a newer DLL cannot overwrite an older struct. */
XL_API xl_status xl_runtime_options_init(xl_runtime_options* options, size_t options_size);
/* Create binds the runtime to the calling UI thread. NULL options use defaults. */
XL_API xl_status xl_runtime_create(const xl_runtime_options* options, xl_runtime** out_runtime);
/* Runtime operations, including destroy, must execute on that UI thread. */
XL_API xl_status xl_runtime_check_thread(const xl_runtime* runtime);
XL_API xl_status xl_runtime_destroy(xl_runtime* runtime);
/* Set before the first resource request. The callbacks must not throw;
   user must outlive Runtime and all its requests/leases. Private shared text
   data uses SDK allocation and never extends the host context's lifetime. */
XL_API xl_status xl_runtime_set_allocator(xl_runtime* runtime, void* user, xl_allocate_fn allocate, xl_deallocate_fn deallocate);
/* Runtime entry points use its UI thread; a request handle may then be polled,
   canceled or released on any thread. Each handle has one external owner. */
XL_API xl_status xl_runtime_request_resource(xl_runtime* runtime, uint32_t id, xl_resource_request** out_request);
XL_API xl_status xl_runtime_prewarm_resource(xl_runtime* runtime, uint32_t id, xl_resource_request** out_request);
XL_API xl_status xl_runtime_resource_stats(const xl_runtime* runtime, xl_resource_stats* stats, size_t stats_size);
XL_API xl_status xl_runtime_trim_resources(xl_runtime* runtime);
/* Nonblocking: XL_BUSY until complete; each successful poll creates a lease.
   Cancellation is local to this request. Release cancels any pending result. */
XL_API xl_status xl_resource_request_poll(const xl_resource_request* request, xl_resource_lease** out_lease);
XL_API xl_status xl_resource_request_cancel(xl_resource_request* request);
XL_API void xl_resource_request_release(xl_resource_request* request);
/* The immutable pointer is valid until this lease is released. Leases can
   outlive Runtime; unload the SDK only after all handles have been released. */
XL_API xl_status xl_resource_lease_data(const xl_resource_lease* lease, const void** data, size_t* size);
XL_API void xl_resource_lease_release(xl_resource_lease* lease);
/* Explicit nonblocking preparation for fixed text flows. Returns XL_BUSY while
   ICU data loads. Multiple runtimes coordinate the private ICU lifecycle. */
XL_API xl_status xl_runtime_prepare_text(xl_runtime* runtime);
/* UTF-8 NFC; calls prepare_text and returns XL_BUSY until data is ready.
   required_size excludes NUL. No output is written if capacity is too small. */
XL_API xl_status xl_runtime_normalize_utf8(xl_runtime* runtime, const char* input, size_t input_size,
                                         char* output, size_t output_capacity, size_t* required_size);

/* All view operations use the Runtime's UI thread. Native views additionally
   use the host's main/video thread. Runtime destroy is XL_BUSY while views live. */
XL_API xl_status xl_view_options_init(xl_view_options* options, size_t options_size);
XL_API xl_status xl_view_create(xl_runtime* runtime, const xl_view_options* options, xl_view** out_view);
XL_API xl_status xl_view_destroy(xl_view* view);
/* The host opts into OS pumping. flags=0 drains only the private SDL queue;
   it never implicitly polls the host's event loop. No loop/thread is started. */
XL_API xl_status xl_runtime_pump_events(xl_runtime* runtime, uint32_t flags);
XL_API xl_status xl_view_set_rects(xl_view* view, const xl_paint_rect* rects, size_t count);
XL_API xl_status xl_view_set_background(xl_view* view, uint32_t argb);
XL_API xl_status xl_view_invalidate(xl_view* view, int32_t x, int32_t y, int32_t width, int32_t height);
/* Expose/lost backing buffer: present retained pixels without rerasterizing. */
XL_API xl_status xl_view_request_present(xl_view* view);
/* UI thread; milliseconds until the document's next script task. -1 means no
   scheduled task. A due task requests a frame; the host still owns waiting and
   calling xl_view_render. Device/events/resource readiness are independent. */
XL_API xl_status xl_view_next_wakeup_ms(const xl_view* view, int64_t* milliseconds);
/* UI thread. Start with this candidate then progressively fall back to Raster.
   Explicit Raster never initializes a GPU. Selection replaces the retained
   surface, preserves the scene, and requests a complete frame. */
XL_API xl_status xl_view_set_graphics_backend(xl_view* view, uint32_t backend);
XL_API xl_status xl_view_get_graphics_info(const xl_view* view, xl_graphics_info* info, size_t size);
/* Host-reported device loss: invalidate backing pixels and abandon this
   candidate. The next render rebuilds the complete scene on the next backend. */
XL_API xl_status xl_view_abandon_graphics_backend(xl_view* view);
XL_API xl_status xl_view_render(xl_view* view, uint32_t* frame_flags);
XL_API xl_status xl_view_get_stats(const xl_view* view, xl_view_stats* stats, size_t stats_size);
XL_API xl_status xl_view_resize(xl_view* view, int32_t width, int32_t height);
/* RGBA8, straight alpha. Copies the last rendered frame into caller memory;
   returns XL_BUSY before first render. Row padding is left untouched. */
XL_API xl_status xl_view_copy_rgba(const xl_view* view, void* pixels, size_t capacity, size_t row_bytes);
/* Borrowed handles stay owned by the host. Destroy the view before its window. */
XL_API xl_status xl_view_native_handle(const xl_view* view, uint32_t* kind, uintptr_t* window);
/* UI-thread synchronous plain-text clipboard. A configured handler overrides
   the native adapter; NULL restores the owned window's SDL adapter. Borrowed
   and offscreen views require a handler. Missing directions are UNSUPPORTED.
   read: XL_OK transfers one owned text/plain UTF-8 content; otherwise NULL.
   write borrows bytes only during the call; XL_OK means the write succeeded.
   Text is at most 4MiB, without NUL or invalid UTF-8. context lives until the
   handler is replaced/cleared or the View ends. No SDK locks are held across
   callbacks; same-Runtime mutation/View reentry returns BUSY. Callback errors
   propagate, do not change handled and never trigger default cut deletion.
   Commands route cancelable copy/cut/paste events and editor defaults. Password
   copy/cut and active IME compositions have no default clipboard operation. */
XL_API xl_status xl_view_set_clipboard_handler(xl_view* view, const xl_clipboard_handler* handler, size_t handler_size);
XL_API xl_status xl_view_clipboard(xl_view* view, uint32_t command, uint32_t* handled);

/* Document/DOM/JS operations use the Runtime UI thread. Runtime outlives its
   documents; destroy is busy while a view is attached. Navigation resets JS
   and invalidates old node IDs. Detached nodes remain valid until navigation. */
XL_API xl_status xl_document_create(xl_runtime* runtime, xl_document** out_document);
/* UI thread; NULL restores native/default preferences. */
XL_API xl_status xl_document_set_media_preferences(xl_document* document, const xl_media_preferences* preferences, size_t preferences_size);
XL_API xl_status xl_document_matches_media(const xl_document* document, const char* query_utf8, size_t query_size, uint32_t* matches);
XL_API xl_status xl_document_destroy(xl_document* document);
/* Memory HTML only. Scripts are evaluated explicitly, never on parsing. */
XL_API xl_status xl_document_load_html(xl_document* document, const char* utf8, size_t size);
/* Immutable memory content: create copies bytes once; clone/mount/open share
   that storage without copying. Handles may outlive runtimes and be read or
   cloned on any thread. Do not release a handle concurrently with its use;
   release all handles before unloading the SDK. MIME defaults to application/octet-stream. */
XL_API xl_status xl_content_create(const void* bytes, size_t size, const char* mime, xl_content** out_content);
XL_API xl_status xl_content_clone(const xl_content* content, xl_content** out_content);
XL_API xl_status xl_content_data(const xl_content* content, const void** bytes, size_t* size, const char** mime);
XL_API void xl_content_release(xl_content* content);
/* Per-Runtime URI mounts use its UI thread. Each mapping retains content;
   replacement/unmount never invalidates existing content handles. Only this
   internal scheme is supported; no file/network fallback. Queries/fragments
   remain in document URIs but do not select different mounted content. */
XL_API xl_status xl_runtime_mount_content(xl_runtime* runtime, const char* uri, const xl_content* content);
XL_API xl_status xl_runtime_unmount_content(xl_runtime* runtime, const char* uri);
XL_API xl_status xl_runtime_open_content(xl_runtime* runtime, const char* uri, xl_content** out_content);
/* Optional synchronous host loader, called only for an unmapped URI on the UI
   thread. URI is canonical, without query/fragment. XL_OK transfers one owned
   content handle to the SDK; other results must return NULL. Successful content
   is cached as a normal mount until unmounted/replaced. The callback may create
   or clone content, but Runtime APIs reject callback reentry with XL_BUSY.
   context stays alive until the loader is replaced/cleared or Runtime ends.
   Passing NULL clears the loader; existing mounts stay intact. No thread starts. */
typedef xl_status (*xl_content_loader_fn)(void* context, const char* uri, xl_content** out_content);
XL_API xl_status xl_runtime_set_content_loader(xl_runtime* runtime, xl_content_loader_fn loader, void* context);
/* Resolve a reference against an absolute internal base. UTF-8 non-ASCII
   URI bytes must be percent encoded. Output excludes NUL; no partial output. */
XL_API xl_status xl_resolve_ui_uri(const char* base, const char* reference, char* output, size_t capacity, size_t* required);
/* Assign a logical source location to memory HTML, or navigate to a mount.
   Both keep scripts explicit; failure leaves the old document intact.
   Memory HTML without a supplied URI uses xl-webengine-ui://app/. */
XL_API xl_status xl_document_load_html_at(xl_document* document, const char* utf8, size_t size, const char* uri);
XL_API xl_status xl_document_load_uri(xl_document* document, const char* uri);
XL_API xl_status xl_document_get_uri(const xl_document* document, char* output, size_t capacity, size_t* required);
/* Copies a font into private memory. CSS family may be an alias. Linux text
   currently needs an explicit font; Windows can fall back to system fonts. */
XL_API xl_status xl_document_add_font(xl_document* document, const char* family_utf8, const void* bytes, size_t size);
XL_API xl_status xl_document_query_selector(xl_document* document, const char* selector, size_t size, xl_node_id* out_node);
XL_API xl_status xl_document_create_element(xl_document* document, const char* tag, size_t size, xl_node_id* out_node);
XL_API xl_status xl_document_append_child(xl_document* document, xl_node_id parent, xl_node_id child);
XL_API xl_status xl_document_remove_node(xl_document* document, xl_node_id node);
XL_API xl_status xl_document_set_attribute(xl_document* document, xl_node_id node, const char* name, const char* value, size_t size);
XL_API xl_status xl_document_get_attribute(const xl_document* document, xl_node_id node, const char* name, char* output, size_t capacity, size_t* required);
XL_API xl_status xl_document_set_text(xl_document* document, xl_node_id node, const char* text, size_t size);
XL_API xl_status xl_document_get_text(const xl_document* document, xl_node_id node, char* output, size_t capacity, size_t* required);
/* Cascaded declaration serialization; does not imply complete computed CSS.
   Query expanded font longhands individually; the font shorthand may be absent. */
XL_API xl_status xl_document_get_style(const xl_document* document, xl_node_id node, const char* property, char* output, size_t capacity, size_t* required);
/* Computed generated-box declarations for ::before/::after (legacy single
   colon accepted). Origin is an author element; no generated DOM handle is exposed. */
XL_API xl_status xl_document_get_pseudo_style(const xl_document* document, xl_node_id node, const char* pseudo, const char* property,
                                           char* output, size_t capacity, size_t* required);
/* Eval runs once. Read its saved string result or diagnostic separately;
   buffer sizing never executes JS again. Budget 1..10000 ms, 0 means 100 ms. */
XL_API xl_status xl_document_eval(xl_document* document, const char* script, size_t size, uint32_t budget_ms);
XL_API xl_status xl_document_script_result(const xl_document* document, char* output, size_t capacity, size_t* required);
XL_API xl_status xl_document_get_stats(const xl_document* document, xl_document_stats* stats, size_t size);
/* NULL detaches. Paint rectangles and attached document content are exclusive. */
XL_API xl_status xl_view_set_document(xl_view* view, xl_document* document);
XL_API xl_status xl_view_node_bounds(const xl_view* view, xl_node_id node, xl_node_bounds* bounds);
/* Per-view scroll state; node 0 addresses the viewport. Coordinates are
   drawable pixels. Requires a current layout. set_scroll clamps finite offsets;
   the next render synchronizes paint, hit/selection and AX. No DOM revision or
   focus change. Non-scroll containers return NOT_FOUND. */
XL_API xl_status xl_view_get_scroll(const xl_view* view, xl_node_id node, xl_scroll_info* info, size_t size);
XL_API xl_status xl_view_set_scroll(xl_view* view, xl_node_id node, float x, float y);
/* Host wheel/pan bridge, positive deltas scroll right/down. Finds the topmost
   scrollport at the pointer and chains unused delta to its ancestors. hidden
   and clip do not accept user scrolling. Render after a change. */
XL_API xl_status xl_view_scroll_at(xl_view* view, float x, float y, float delta_x, float delta_y);
/* Owned-window wheel bridge, disabled by default. One wheel unit is 48 logical
   pixels, converted to drawable pixels. Host still pumps and renders. */
XL_API xl_status xl_view_set_scroll_input(xl_view* view, uint32_t enabled);
/* UI-thread host keyboard bridge. Strings are borrowed for this call only,
   NUL-terminated valid UTF-8, <=128 bytes each. location is 0..3. handled is
   0/1 and is untouched on failure. Dispatches cancelable DOM keyboard events;
   Tab/activation/scroll defaults run unless prevented. Changes synchronize on
   render; text insertion and IME use separate input work. */
XL_API xl_status xl_view_key(xl_view* view, const xl_key_event* event, size_t size, uint32_t* handled);
/* Owned windows only; default disabled. Host continues to pump/render. */
XL_API xl_status xl_view_set_keyboard_input(xl_view* view, uint32_t enabled);
/* Text input/textarea state, shared with JS value/selectionStart/selectionEnd.
   Programmatic setters do not dispatch input. UTF-8 is copied, <=4MiB, no NUL.
   Selection uses UTF-16 boundaries and rejects split surrogate pairs. */
XL_API xl_status xl_document_get_value(const xl_document* document, xl_node_id node, char* output, size_t capacity, size_t* required);
XL_API xl_status xl_document_set_value(xl_document* document, xl_node_id node, const char* text, size_t size);
XL_API xl_status xl_document_get_edit_state(const xl_document* document, xl_node_id node, xl_edit_state* state, size_t size);
XL_API xl_status xl_document_set_edit_selection(xl_document* document, xl_node_id node, uint32_t anchor, uint32_t focus);
/* User text and IME events target the focused writable control. handled is
   untouched on failure. UPDATE starts/updates preedit; COMMIT replaces the
   original selection as one history entry; CANCEL restores its original value.
   Preedit selection is relative UTF-16, not UTF-8 bytes or scalar counts. */
XL_API xl_status xl_view_text_input(xl_view* view, const char* text, size_t size, uint32_t* handled);
XL_API xl_status xl_view_set_composition(xl_view* view, const xl_composition* composition, size_t size, uint32_t* handled);
/* UTF-16 range in a rendered text node after whitespace normalization.
   Output coordinates use drawable pixels, clipped to visible overflow and the
   viewport. No partial output on small buffers. node_bounds keeps the full box. */
XL_API xl_status xl_view_text_rects(const xl_view* view, xl_node_id node, uint32_t start, uint32_t end,
                                   xl_node_bounds* output, size_t capacity, size_t* required);
/* Range in the original DOM text node, measured in UTF-16 code units.
   Collapsed whitespace maps to its rendered contribution; empty ranges produce
   no rectangles. Same snapshot and buffer rules as xl_view_text_rects. */
XL_API xl_status xl_view_dom_text_rects(const xl_view* view, xl_node_id node, uint32_t start, uint32_t end,
                                       xl_node_bounds* output, size_t capacity, size_t* required);
/* Drawable pixels -> nearest shaped caret in original DOM text. Affinity
   disambiguates line boundaries and adjacent text nodes. Empty documents return
   XL_NOT_FOUND, as do clipped-out text and pointers outside its clip. The
   output is untouched on failure. This does not move focus. */
XL_API xl_status xl_view_hit_test_text(const xl_view* view, float x, float y, xl_text_position* position, size_t size);
/* View-owned DOM UTF-16 selection. NULL clears it. Both endpoints refer to
   rendered text nodes in the current snapshot; split surrogate pairs are
   invalid. Setting a selection changes paint only, not DOM/focus/layout.
   Retain offsets across layout/style changes; clear on invalid endpoints,
   navigation or document detach. No automatic blink or worker is started. */
XL_API xl_status xl_view_set_selection(xl_view* view, const xl_text_selection* selection, size_t size);
XL_API xl_status xl_view_get_selection(const xl_view* view, xl_text_selection* selection, size_t size);
XL_API xl_status xl_view_selection_rects(const xl_view* view, xl_node_bounds* output, size_t capacity, size_t* required);
/* Original DOM UTF-8 text, in document order. Keeps original whitespace,
   excludes synthetic inline edges and inserts no layout/block separators.
   Output excludes NUL; sizing does not change the selection. */
XL_API xl_status xl_view_selected_text(const xl_view* view, char* output, size_t capacity, size_t* required);
/* One-pixel caret at a shaped cluster boundary; affinity resolves soft wrap
   and bidi edges. Clipped, with zero area when fully hidden. Does not move the
   selection. Uses drawable coordinates. */
XL_API xl_status xl_view_caret_rect(const xl_view* view, const xl_text_position* position, xl_node_bounds* bounds);
#define XL_SELECTION_EXTEND 1u
/* Host pointer bridge: hit test and start/extend a selection in one call.
   Works with a current layout even before selection damage is presented. */
XL_API xl_status xl_view_select_at(xl_view* view, float x, float y, uint32_t flags);
/* Requires a current layout. A release activates the common ancestor of the
   press/release targets, unless cancelled or dragged farther than 4 pixels.
   A new document, cancel or native focus loss clears the pending press. */
XL_API xl_status xl_view_pointer(xl_view* view, const xl_pointer_event* event, size_t size, uint32_t* handled);
/* Opt-in primary-pointer selection for owned windows. Together with keyboard
   input, also forwards native focus/click through xl_view_pointer. The
   host still pumps native events and renders. Borrowed windows/offscreen hosts
   forward their own coordinates through select_at instead. Default disabled. */
XL_API xl_status xl_view_set_selection_input(xl_view* view, uint32_t enabled);
/* AX snapshot belongs to the last successful document render. It is a semantic
   tree/query API; native UIA/AT-SPI/NSAccessibility bridges are separate work.
   Stale snapshots return busy until render synchronizes the current revision. */
XL_API xl_status xl_view_ax_count(const xl_view* view, size_t* count);
XL_API xl_status xl_view_ax_get(const xl_view* view, size_t index, xl_ax_node* node, size_t size);
XL_API xl_status xl_view_ax_name(const xl_view* view, size_t index, char* output, size_t capacity, size_t* required);
XL_API xl_status xl_view_ax_value(const xl_view* view, size_t index, char* output, size_t capacity, size_t* required);
XL_API xl_status xl_document_ax_action(xl_document* document, xl_node_id node, uint32_t action);
/* Opt-in updates are published only after successful render; no callbacks or
   worker threads are started. UI-thread take transfers an immutable snapshot.
   Queue: <=8 batches / 64MiB. Overflow replaces pending batches with RESET and
   OVERFLOW. A single oversize snapshot reports XL_UNSUPPORTED on take; a later
   valid frame recovers with RESET. Poll without rendering for existing updates.
   Each batch contains a complete current tree in DOM order plus ordered events.
   RESET invalidates the prior tree, including across navigation/detach.
   A batch can outlive View/Document/Runtime. Read/clone/release it on any thread,
   with no release racing access to the same handle; release before SDK unload. */
XL_API xl_status xl_view_set_ax_updates(xl_view* view, uint32_t enabled);
XL_API xl_status xl_view_take_ax_update(xl_view* view, xl_ax_update** update);
XL_API xl_status xl_ax_update_clone(const xl_ax_update* update, xl_ax_update** copy);
XL_API void xl_ax_update_release(xl_ax_update* update);
XL_API xl_status xl_ax_update_get_info(const xl_ax_update* update, xl_ax_update_info* info, size_t size);
XL_API xl_status xl_ax_update_get_node(const xl_ax_update* update, size_t index, xl_ax_node* node, size_t size);
XL_API xl_status xl_ax_update_name(const xl_ax_update* update, size_t index, char* output, size_t capacity, size_t* required);
XL_API xl_status xl_ax_update_value(const xl_ax_update* update, size_t index, char* output, size_t capacity, size_t* required);
XL_API xl_status xl_ax_update_get_event(const xl_ax_update* update, size_t index, xl_ax_event* event, size_t size);
XL_API xl_status xl_ax_update_event_text(const xl_ax_update* update, size_t index, char* output, size_t capacity, size_t* required);

#define XL_NATIVE_AX_UIA 1u
/* Native accessibility is opt-in; currently Windows HWND views. Owned windows
   install WM_GETOBJECT support. Borrowed windows leave their procedure intact:
   the host obtains the provider and attaches/forwards it itself. Offscreen and
   unavailable platforms return UNSUPPORTED. UI thread; no private event loop.
   Render also services bounded UIA action requests; needs_frame includes them. */
XL_API xl_status xl_view_set_native_accessibility(xl_view* view, uint32_t enabled);
/* Returns an owned native interface. UIA: IRawElementProviderSimple*, one COM
   reference, Release on any thread. No output changes on failure. Disable or
   destroy the View before destroying a borrowed HWND. Retained providers become
   unavailable after disable/destroy. Keep the SDK loaded while providers exist. */
XL_API xl_status xl_view_get_native_ax_provider(xl_view* view, uint32_t* kind, uintptr_t* provider);
/* Any thread. Counts native COM/provider objects owned by this SDK instance,
   including OS/client references. After disabling/destroying native bridges,
   wait for zero before unloading the SDK; other SDK handles must also be gone. */
XL_API xl_status xl_native_ax_get_object_count(uint64_t* count);

#ifdef __cplusplus
}
#endif
#endif
