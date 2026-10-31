# Graph renderer implementation

Graph components map data into display coordinates and call the private interface
in `rbcRender.h`. These interfaces are not part of the installed RBC C API.

| File                    | Responsibility                                                                        |
|-------------------------|---------------------------------------------------------------------------------------|
| `rbcRender.c`           | Common operation dispatch, arc validation, and native screen adapters.                |
| `rbcRenderInt.h`        | Operation tables and the common context prefix.                                       |
| `rbcRenderCairo.c`      | Cairo drawing, surfaces, marker-pass targets, clipping, and disabled-Cairo fallbacks. |
| `rbcRenderPostscript.c` | PostScript drawing, prolog symbol setup, and clipping.                                |
| `rbcRenderSvg.c`        | SVG drawing, patterns, bitmap definitions, embedded text, and clipping.               |
| `rbcRenderPdf.c`        | PDF drawing, image resources, patterns, text, and clipping.                           |
| `rbcRenderExport.c`     | Export constructor selection, shared style helpers, and Tk image capture.             |
| `rbcRenderExportInt.h`  | Shared export state and private cross-file helpers.                                   |
| `rbcRenderNative.c`     | Shared mapped geometry, Tk text/images/decorations, and the native clip stack.        |
| `rbcRenderNativeWin.c`  | GDI trace, strip, symbol, polygon and bar batches, plus DC clipping.                  |
| `rbcRenderNativeX11.c`  | X11 trace and symbol requests, protocol limits, and platform fallbacks.               |
| `rbcRenderNative.h`     | Private native services; platform contexts are opaque.                                |
| `rbcWinDraw.c`          | Shared Windows/Tk compatibility, Xlib emulation, and XOR presentation.                |

The `rbcGrSvg.c` and `rbcGrPdf.c` modules retain their option tables, defaults, document builders and encoding
policies. `rbcGrExport.c` shares their command dispatch, atomic native-integer option updates, export geometry
restoration and channel delivery. It selects option conversions by `Tk_OptionSpec.type`, not table position.
Format callbacks return an unreferenced document object; the shared lifecycle retains and releases it and invokes
backend cleanup even on failure. SVG remains UTF-8 text; PDF remains binary. PostScript keeps its own command
lifecycle in `rbcGrPs.c`, including page layout and Windows preview handling. `rbcPdf.c` owns PDF resource allocation,
streams, object offsets and document serialization.
Font discovery and PDF font resources remain in the existing font modules. `rbcWinDraw.c` supplies
lower-level Windows/Tk compatibility used by renderers and by other image, tile, and export code.

## Supporting module boundaries

| File | Responsibility |
|------|----------------|
| `rbcGeometry.c` | Segment distances, polygon hit testing, rectangle clipping and line simplification; no graph state or drawing. |
| `rbcScroll.c` | Scroll request parsing and viewport arithmetic; callers own widget state and redraw decisions. |
| `rbcDraw.c` | Shared Tk/Xlib GC ownership, dash setup, request-size queries and outlined relief used by native rendering and canvas EPS items. |
| `rbcConfig.c` | Shared option conversion, component initialization and error-preserving Tk option rollback. |
| `rbcGrMisc.c` | Graph coordinate parsing, color-pair options and plot extents, including the active track's clipping bounds. |
| `rbcGrTrack.h` | Private track records, viewport snapshots and track-specific component services. |

Geometry and scrolling declarations remain in the private `rbcInt.h`. The general graph header forward-declares
`RbcTrack` for borrowed pointers; implementation files that inspect track fields or call track services explicitly
include `rbcGrTrack.h`. That header includes the graph definitions required by its embedded grid segments.

Component validation uses `Rbc_RestoreOptionsOnError` after a successful `Tk_SetOptions` call when a later
conversion fails. It consumes the saved transaction and preserves the complete Tcl error state. Callers clear
borrowed option arguments before rollback and retain responsibility for derived resources and success commits.
Tk handles rollback itself when `Tk_SetOptions` fails; that path must not restore the same transaction twice.

`rbcDraw.c` is shared Tk infrastructure, not a backend used by every renderer. Component configuration retains GC
ownership and dash setup; text and tile utilities also use these services. Request-size queries serve both the native
backend and tile drawing. Outlined relief remains shared because canvas EPS items use it without a graph renderer.

Segment conversion/batching and filled relief are private helpers in `rbcRenderNative.c`. X11 strip drawing delegates
to the common native segment entry point; Windows screen segments retain the same common implementation. Integer
conversion, clipping, request splitting and relief shading are unchanged. Private Windows/Tk compatibility remains in
`rbcWinDraw.c`.

Error-band mapping asks `Rbc_RenderPolygonPointLimit` for the selected output's limit before creating a drawing
context. Native X11 reports its request capacity; native Windows, Cairo and exports report `TCL_SIZE_MAX` (no request
bound). The component reserves one closure point and six clipping vertices per boundary sample, and splits at shared
samples. Backend selection and platform limits belong to the renderer; geometry-specific splitting belongs to the
component. This query does not change native fallback policy for Cairo drawing.

## Context ownership

Every allocated backend context embeds `Rbc_RenderContext` as its first member.  The common prefix contains only
operations, the borrowed graph pointer, and clip depth. Native and Cairo state is private to its implementation
file. Exporters share `Rbc_RenderExportState`; PostScript and SVG extend it with their own state.  Cairo headers and
Cairo types appear only in `rbcRenderCairo.c`.

A constructor initializes the complete backend context. `Rbc_RenderEnd` unwinds outstanding clips and calls that
context's destructor. Export contexts borrow the document token; destroying a drawing context does not destroy the
document.  Images, GCs, tiles, and drawables are borrowed unless a constructor explicitly owns temporary resources. The
one-shot native screen adapters use stack contexts internally and must not call `Rbc_RenderEnd` on those contexts.

## Capabilities and clipping

An operation table is a capability contract. A NULL callback is unsupported; callers must use only operations documented
for their constructor in `rbcRender.h`. It is not a request for an automatic fallback. Screen constructors that can
decline Cairo return NULL so their caller can select the native path.

Clip scopes dispatch through `clipBegin` and `clipEnd`. Only clip-capable contexts may receive clip operations. Backend
code owns the actual Cairo save/restore, SVG groups, PDF graphics states, PostScript graphics states, or native clip
frames. The common dispatcher owns nesting depth and final unwinding.

Text, widget decorations, pixel copies, and XOR overlays retain native behavior where required even on Cairo graphs. Do
not route XOR strokes through ordinary Cairo painting. Windows XOR mask composition remains in `rbcWinDraw.c`.

## Building and checking changes

Both Cairo-enabled and disabled builds compile the separate Cairo translation unit; the disabled build contains its
existing fallback implementations and does not include Cairo headers or link Cairo libraries. The private headers are
build dependencies, not installed headers. Add source files in `configure.ac` and keep the distributed `configure` and
`win/makefile.vc` source lists synchronized.

Use the renderer, export, and track tests for backend changes. Compare rendered snapshots and exported documents when
moving code; preserve batching boundaries, coordinate rounding, painter order, and the lifetimes of native DCs and
surfaces.

Export failures are recorded through `Rbc_ExportError` in `rbcGrExport.c`, declared with the document context in
`rbcExport.h`. Renderers, font loaders and document writers retain the first static error message; the helper returns
`TCL_ERROR` for callers that propagate status. The context borrows the message and never frees it.

## Native platform boundary

Configure selects exactly one of `rbcRenderNativeWin.c` and `rbcRenderNativeX11.c`; the MSVC build selects the Windows
file. Shared geometry calls the selected implementation once per batch, without a platform dispatch inside point
loops. Trace state and its point buffer use one allocation. Native clip frames reserve trailing space for private
platform state in the same allocation; that state is reused with the frame at each nesting depth.

The common native implementation retains Tk's text, image, tile, and relief helpers. Xlib-shaped calls in this shared
file are intentional: Tk implements these operations on Windows too. Platform files contain the algorithms that actually
differ, including GDI path batching and X11 request-size limits.  X11 declines the Windows-only polygon/DC fast paths,
so existing callers retain their ordinary polygon and scratch-pixmap paths. Windows also retains scratch clipping where
a caller does not request DC clipping.

`rbcWinDraw.c` is not a graph backend. It isolates private Tk Windows headers, drawable/DC wrappers, GC-to-pen
conversion, bitmap/palette helpers, and Xlib emulation used by graph, image, tile, and export code. Its XOR segment
renderer builds a mask offscreen and presents it once; this preserves dash phase and avoids crosshair flicker. Keep that
lower-level implementation shared.

## Component drawing audit

Text-marker backgrounds, scan-converted arrowheads, native legend samples, and Windows text/opaque-bitmap marker runs
now enter through the screen-detail operations in `rbcRender.h`. These are native-only presentation operations;
components still attempt their existing Cairo path first where applicable.

- Backgrounds add the anchor before converting the four vertices to shorts and
  retain the Convex fill rule.
- Arrowheads keep double coordinates until scan conversion, half-open edges,
  graph/protocol clipping, and batches of 128 one-pixel spans.
- Legend lines retain two independent line requests. Bar samples set the stipple
  origin to the sample and restore it to zero after drawing.
- Marker components own eligibility, run grouping, and painter order. The renderer
  invokes a synchronous text-run callback on one shared Windows DC, or releases
  failed setup before drawing on the original target. No callback data escapes.
- Opaque bitmap runs use full-width integer positions without copying to a second
  position array. The backend keeps the shared source/destination DC fast path
  and replays the entire run through XCopyPlane after a partial GXcopy failure.

Depth-one mask construction and child-window geometry remain resource/layout work owned by their components. Private Tk
DC operations remain in `rbcWinDraw.c`; marker components no longer acquire native DCs themselves.  The DC release
helper accepts a failed acquisition and frees its bookkeeping without passing a NULL DC to Tk. The Windows XOR
implementation is unchanged.

This document describes renderer boundaries only. A future source manual should also cover widget/component lifecycles,
mapping and invalidation, binding and picking, vector notification, export orchestration, and platform resource
ownership, with links from those topics to these renderer contracts.

## Shared component helpers

`rbcConfig.c` owns the legacy first-byte resource-name conversion. Its returned copy is freed by the caller;
component registry names remain unchanged. This deliberately preserves byte-based `tolower` behavior rather
than introducing Unicode case folding.

`rbcUtil.c` owns saturation from `Tcl_WideInt` to `int`. Axis and legend sizes still allow zero, axis ranges and
PostScript dimensions still have a minimum of one, and floating-point rounding stays in each component.

`rbcGrTrack.c` owns the paired nested-command argument adapter used by local grids and legends. The adapter
owns its argument array and the list-valued command prefix, borrows the remaining objects, and preserves quoting
in diagnostics. Dispatch tables, argument validation and component lifetime remain with the callers.
