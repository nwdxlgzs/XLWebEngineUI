# Absolute and fixed positioning

The private layout engine removes `absolute` and `fixed` boxes from normal
flow, Flex/Grid placement and intrinsic contributions. Deferred records retain
the DOM identity, inherited computed values, static position and containing
block. Their own contents are laid out after that block has its final size.
No public ABI types or exports were added.

Absolute positioning uses the nearest positioned ancestor's padding box;
otherwise it uses the initial containing block. Fixed positioning uses the
viewport. Positioned inline ancestors use the first and last padding fragments.
Physical insets, percentages, auto dimensions/margins, shrink-to-fit widths,
min/max constraints, border-box dimensions and horizontal direction resolve
against that containing block. A positioning box establishes a containing
block for its own absolute descendants.

An absent inline box records a UTF-16 boundary in the existing paragraph,
without inserting a placeholder character, changing whitespace collapse,
creating an AX text node or forcing an empty line. Flex static positions use
the container's content area and main/cross alignment. Definite Grid lines
use the solved tracks and named lines; out-of-flow items cannot create or
occupy implicit tracks. Auto Grid edges retain padding edges for the containing
block and content edges for the static-position rectangle.

Deferred boxes bypass clips and scrolling on intermediate static ancestors.
Absolute children join their actual containing block before its overflow is
computed. Fixed boxes join after viewport overflow and do not enlarge its
scroll range. Both use the existing shared display list, reverse hit testing,
text geometry, AX geometry and retained scroll projection. Fixed boxes also
create a stacking context when z-index is auto. AX reading order stays in DOM
order.

`document.absolute`, its static-library variant and SwiftShader variant cover
the public API, geometry, pixel colors, AX, clipping, overflow, viewport and
ancestor scrolling, projection versus forced relayout, Flex/Grid, inline and
atomic containing blocks. A current host result is not old-system or complete
CSS positioning certification.

Remaining work includes sticky positioning, logical axes/writing modes,
transform/filter/perspective containing blocks, the complete modern alignment
model, fragmentation, replaced images/media dimensions and comprehensive WPT
comparison. These remain engine goals; this stage does not reduce them.
