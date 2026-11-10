# Rendering measurements, November 4, 2026

The three original `tests/user-dev-tests` pages remain unchanged. Current
Windows x64 Debug measurements still show slow initial frames and software
scrolling. They are development observations, not a performance acceptance
result or a Release/old-platform benchmark.

The private timing DLL uses copied first-party sources and the existing Debug
objects; it does not modify production objects or third-party sources. On the
second page its initial software render took 2715.59 ms: layout 2080.38 ms,
including block layout 1783.09 ms, text shaping 1070.57 ms (2298 calls), computed
styles 459.51 ms (5544 calls), and AX construction 297.28 ms. These timings
are nested, not additive. Three scrolling renders took 849.18 ms; raster
painting consumed 829.78 ms and box painting 779.14 ms. There were no layout,
style refresh or shaping calls during those scrolls.

Two changes address drawing work:

- The display list rejects commands outside the conservative damaged region
  before entering Skia. Bounds retain antialias expansion and clipping.
- Background gradients use the native Skia shader when every potentially
  painted pixel cell lies within one tile. Repetition, spacing, fractional
  boundaries and regions outside that proof retain the procedural shader.
  No width × height intermediate image or new resource pool is allocated.

The retained culling-only baseline measured the complex page's CPU scroll at
284.64 ms; the first gradient-fast-path six-step run measured about 151 ms.
An equal three-step repeat and the other pages are recorded separately in
`build/reports/absolute/performance-comparison.json`: complex-page CPU scroll
284.64→164.29 ms and first render 2648.67→2511.20 ms; D3D12 scroll 40.16→36.71 ms
and first render 4464.98→3975.51 ms. The first page's software scroll remained
137.35→139.97 ms; this optimization does not improve every case. All three
D3D12 final images and the first CPU page kept their hashes; the two other
CPU page hashes changed, consistent with the quantified rounding difference
in fixtures, without claiming a per-pixel bound for those complete pages.
The original pre-culling
sample was about 304 ms, but its raw worker report was removed by the three-
report retention policy. Some earlier samples overlapped related compilation
or tests and must not be treated as a controlled benchmark. Initial frames
remain around 2.6 seconds on that page; this change does not solve layout or
shader compilation latency.

The direct and procedural paths were compared for linear, Oklab alpha,
elliptical radial and conic gradients, positive and negative tile translations,
and rounded clipping. On CPU, 54–65 output channels per 240 × 180 frame differed
by at most 1/255; the tested GPU images were identical. Tests enforce the
one-channel quantization bound, plus existing partial repaint, fixed/local
background, static frame and retained-scroll versus forced-layout checks.
This is not a claim that every page's full pixel hash is identical.

Next performance work remains initial intrinsic text measurement/style reuse,
fine DOM invalidation, reduced repeated border/background work, incremental AX
publication and GPU presentation without synchronous full readback. Native GL
and FXC availability do not by themselves solve those bottlenecks. SVG/WebGPU
and codec integration remain required functionality, separately tracked in PLAN.
