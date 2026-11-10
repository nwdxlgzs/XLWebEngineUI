# SVG and image content

Inline `<svg>` and `<img>` participate in the existing private replaced-element
layout. They support intrinsic width/height and viewBox ratio, CSS dimensions,
HTML size attributes, constraints, inline atomic placement, block layout,
Flex/Grid, absolute/fixed placement, overflow, rounded clipping, retained damage
and scrolling. The public ABI remains unchanged.

Inline SVG is serialized from the author's current DOM into a private XML
snapshot. The author DOM and stylesheets are never rewritten. SVG tag and
attribute case is restored for XML; `href` maps to the renderer's xlink entry.
Supported presentation declarations use the existing CSS cascade/variables,
including resolved currentColor and modern CSS colors. HTML ancestor color
and font settings are carried into the root. Geometry attributes remain the
intrinsic-size source, independently from CSS used dimensions.

The backend is the actual Skia SVG module and Expat, not a bitmap placeholder.
SVG is recorded on the UI thread into an immutable SkPicture for the existing
render executor. CPU and GPU consume that same picture. Shapes, paths, groups,
transforms, gradients, clipping, internal use references, SVG text through
HarfBuzz/ICU, and embedded raster images have public rendering tests. A drawing
retains its text session until after its Skia objects are destroyed. Changing
registered fonts changes the recording cache generation.

External SVG and raster files come from explicitly mounted
`xl-webengine-ui://` memory content, relative to the document/base element or
the external SVG's logical URI. Nested raster image loads occur during UI
recording; rendering never calls host file/network code. Data images support
percent-encoded or base64 embedded bytes; their opaque source has no external
relative resource base. No HTTP/HTTPS or filesystem fallback is added.

The explicit SkCodec decoder list includes PNG, JPEG, WebP, GIF, BMP, ICO and
WBMP. The first six have generated real encoded pixel fixtures. GIF and other
animated inputs currently display their initial frame; animation scheduling
remains work to do. Decoding produces bounded N32 premultiplied pixels and
preserves the source color space. The image module alone does not require ICU.
Hidden images perform no load; a shape-only block SVG does not initialize
the text resource worker. Inline boxes still require the surrounding font strut.

Encoded files and decoded/recorded content have a per-Document cache with
64 entries and a 64 MiB retention estimate. Individual raster allocations are
limited to 16 million pixels / 64 MiB; SVG XML and embedded data payloads are
limited to 16 MiB, with 256 levels and 65,536 XML elements. There are at most
four retained SVG recordings per content object. Cache eviction drops only
cache references: active frames, paragraphs and pictures retain their assets.
These limits do not claim to account for every internal Skia/GPU allocation.
XML documents with DTD declarations are rejected. Missing or malformed images
retain layout/alt AX information and increment the unsupported diagnostic.

SVG roots are exposed as images in AX, with aria-label/labelledby and direct
title fallback; IMG uses its alt text. The normal document clipping/projection
provides visible geometry. SVG painting descendants are not emitted as duplicate
HTML boxes or AX text. Full interactive SVG element hit testing and AX subtrees
remain separate requirements.

The Skia recipe now builds actual `svg` and `skresources` targets and their
archive closure, with SVG/XML/Wuffs enabled. Wuffs is locked to the exact Skia
DEPS commit, its official Google mirror archive SHA256, and Apache-2.0 license;
Expat uses its existing MIT source lock. Licenses enter both SDK variants.
No original third-party source is modified; any future source adaptation must
be replayable from `third_party/patches`. The private resource adapter disables
RTTI to match the locked Skia libraries.

Visibility handling also follows the native window's hidden/minimized state.
Document.hidden and visibilityState read that state; queued visibilitychange
events and animation-frame suspension share the bounded task scheduler.
Offscreen views are visible documents. A hidden native view and an offscreen
view exercise real state transitions in the image tests. Window occlusion and
multi-view visibility aggregation remain later platform work.

The user's palette page initially reached missing Canvas 2D functionality after
insertBefore and document visibility gaps were fixed. That historical failure
is retained. The subsequent Canvas stage implements real bitmap drawing and
the page now passes its initial script/frame smoke; see `canvas.md` for the
implemented behavior, controlled update measurements and remaining limitations.
No user HTML is modified. Initial script/frame smoke does not certify animation
speed, all CSS, interaction or visual fidelity.

Still required: complete Canvas 2D, background URL images/object-fit/srcset/picture and
image DOM loading APIs, full external SVG CSS and SVG2 features, nested vector
images, SVG animation, complete filters/masks/foreignObject, advanced font
fallback, animated raster playback and additional codecs such as AVIF/JXL.
Dawn/WebGPU, WebM and the remaining cross-platform/old-system work continue
under PLAN. This stage does not represent a complete SVG/Web engine.
