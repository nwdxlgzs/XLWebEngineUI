# Canvas 2D

Canvas has private per-node drawing state and bitmap content. The public C ABI
is unchanged. `HTMLCanvasElement`, `CanvasRenderingContext2D`, `CanvasGradient`
and `ImageData` operate on the same author DOM used by the C/C++ API.

The bitmap defaults to 300 by 150. Content attributes set its dimensions;
CSS sets its independent displayed size. Setting either bitmap dimension,
including setting the current value again, clears the bitmap, path, clip and
drawing state. It keeps the identity of the existing context. `reset()` clears
the same state without changing dimensions. Cloning creates a blank canvas,
and navigation releases the old document's bitmap and context state.

Implemented drawing includes rectangles and clearing; paths, curves, arcs and
ellipses; nonzero/evenodd fill and clip; stroke widths, caps, joins and dashes;
save/restore and affine transforms; solid CSS colors and linear, radial and
conic gradients; global alpha and the exposed compositing/blending modes.
Current paths keep their construction transforms and are independent from the
saved drawing-state stack. Gradient coordinates use the drawing transform.
Stop edits affect subsequent draws, while earlier commands retain their colors.

`fillText`, `strokeText` and `measureText` use the existing complex-text shaper,
registered fonts, platform font fallback and ICU lifetime rules. Text is queued
in order with other drawing commands. Ordinary rendering prepares ICU through
the existing asynchronous resource path when pending Canvas text first needs
it. Synchronous measurement or pixel/export reads that require pending text
wait for that same bounded worker request, then continue on the UI thread.
This is an explicit synchronous read, not startup decompression. Shape-only
block canvases and pixel operations do not require ICU. On platforms without
system font discovery, the host must register usable fonts before synchronous
text operations; WebTest's existing render fallback remains separate.

Canvas/image/SVG sources support the three `drawImage` forms, cropping and
scaling. Canvas sources are immutable snapshots at the call. `getImageData`
returns unpremultiplied RGBA8 with transparent pixels outside the bitmap.
`putImageData` supports dirty rectangles and ignores transforms, clipping and
global alpha/compositing as required for direct pixel replacement. `toDataURL`
encodes PNG, including the PNG fallback for unimplemented requested formats;
empty or unsupported-size canvases return `data:,`. Image data stays in memory.

The UI thread owns mutable paths/state and ordered drawing commands. It applies
pending commands to a lazily allocated raster surface and publishes immutable
SkImage snapshots to the existing render executor. Skia's copy-on-write protects
older frames and source snapshots. A failed preparation does not replay already
committed alpha draws. Frames hold their own references across reset, navigation
or later drawing. No host resource callback runs on the render thread.

Bitmap changes have a separate internal revision. They refresh the retained
boxes and nested paragraph/atomic scenes without rebuilding layout or AX
geometry. Scroll projection retains those replacements; normal DOM/style/font
or viewport changes still perform their existing layout work. Damage comparison
uses immutable image identity. Static canvases do not repeatedly rasterize, and
two Views each refresh their own retained frame.

Limits include 16 million pixels / 64 MiB per bitmap, 32,768 pixels per axis,
128 MiB per-document estimated pending/retained Canvas content, 65,536 pending
commands with a 64 MiB command estimate, 65,536 path points, and 1,024 saved
states/active clip levels. Gradient stops and dash arrays also have bounds.
These estimates do not include every Skia allocation, GPU copy, active old
frame, shared source image or JS object. They are not a total process-memory cap.
Limits fail explicitly rather than silently allocating unbounded backing stores.

The public rendering tests exercise real CPU/SwiftShader pixels, transparent
readback, state/reset semantics, transformed paths/gradients, compositing,
pixel round trips, image snapshot isolation, PNG export/reload, shaped/stroked
text, multiple Views, retained nested scenes and scrolling versus forced
relayout. Dynamic load/use/close tests include Canvas text and bitmaps. The
user's palette HTML is kept unchanged; its first-frame smoke and a controlled
Canvas-update experiment are separate evidence. Stopping its unrelated timers
in the latter does not prove the original page's animation speed. A screenshot
also shows outstanding CSS/overlay differences; unsupported counters are not a
visual-fidelity verdict.

On the development Windows machine the isolated three-update experiment took
93.412 ms with zero additional layouts. Its unrelated timers were explicitly
stopped after the first frame. The author's running timers caused earlier
60-second test timeouts; those failures are retained. Neither number establishes
60 Hz animation or the original page's overall performance.

Still required for complete Canvas support: Path2D, patterns, shadows/filters,
image smoothing controls, full context attributes/color spaces, OffscreenCanvas,
canvas capture/Blob export, advanced text properties and exact baseline metrics,
all source types and remaining Web API/specification edge cases. WebGL/WebGPU
contexts, Dawn, image animation and the full engine/platform plan remain open.
This stage does not certify full HTML Canvas conformance or old-system support.

Specification reference: WHATWG HTML, the canvas element and 2D rendering
context, https://html.spec.whatwg.org/multipage/canvas.html .
