# Graph renderer implementation

Graph components map data into display coordinates and call the private interface
in `rbcRender.h`. These interfaces are not part of the installed RBC C API.

| File | Responsibility |
| --- | --- |
| `rbcRender.c` | Common operation dispatch, arc validation, and native screen adapters. |
| `rbcRenderInt.h` | Operation tables and the common context prefix. |
| `rbcRenderCairo.c` | Cairo drawing, surfaces, marker-pass targets, clipping, and disabled-Cairo fallbacks. |
| `rbcRenderPostscript.c` | PostScript drawing, prolog symbol setup, and clipping. |
| `rbcRenderSvg.c` | SVG drawing, patterns, bitmap definitions, embedded text, and clipping. |
| `rbcRenderPdf.c` | PDF drawing, image resources, patterns, text, and clipping. |
| `rbcRenderExport.c` | Export constructor selection, shared style helpers, and Tk image capture. |
| `rbcRenderExportInt.h` | Shared export state and private cross-file helpers. |
| `rbcRenderNative.c` | Native geometry, platform batching, and native clipping. |

The `rbcGrSvg.c`, `rbcGrPdf.c`, and `rbcGrPs.c` modules retain widget commands,
options, and document orchestration. Font discovery and PDF font resources remain
in the existing font modules. `rbcWinDraw.c` supplies lower-level Windows/Tk
compatibility used by renderers and by other image, tile, and export code.

## Context ownership

Every allocated backend context embeds `Rbc_RenderContext` as its first member.
The common prefix contains only operations, the borrowed graph pointer, and clip
depth. Native and Cairo state is private to its implementation file. Exporters
share `Rbc_RenderExportState`; PostScript and SVG extend it with their own state.
Cairo headers and Cairo types appear only in `rbcRenderCairo.c`.

A constructor initializes the complete backend context. `Rbc_RenderEnd` unwinds
outstanding clips and calls that context's destructor. Export contexts borrow
the document token; destroying a drawing context does not destroy the document.
Images, GCs, tiles, and drawables are borrowed unless a constructor explicitly
owns temporary resources. The one-shot native screen adapters use stack contexts
internally and must not call `Rbc_RenderEnd` on those contexts.

## Capabilities and clipping

An operation table is a capability contract. A NULL callback is unsupported;
callers must use only operations documented for their constructor in
`rbcRender.h`. It is not a request for an automatic fallback. Screen constructors
that can decline Cairo return NULL so their caller can select the native path.

Clip scopes dispatch through `clipBegin` and `clipEnd`. Only clip-capable contexts
may receive clip operations. Backend code owns the actual Cairo save/restore,
SVG groups, PDF graphics states, PostScript graphics states, or native clip
frames. The common dispatcher owns nesting depth and final unwinding.

Text, widget decorations, pixel copies, and XOR overlays retain native behavior
where required even on Cairo graphs. Do not route XOR strokes through ordinary
Cairo painting. Windows XOR mask composition remains in `rbcWinDraw.c`.

## Building and checking changes

Both Cairo-enabled and disabled builds compile the separate Cairo translation
unit; the disabled build contains its existing fallback implementations and does
not include Cairo headers or link Cairo libraries. The private headers are build
dependencies, not installed headers. Add source files in `configure.ac` and keep
the distributed `configure` and `win/makefile.vc` source lists synchronized.

Use the renderer, export, and track tests for backend changes. Compare rendered
snapshots and exported documents when moving code; preserve batching boundaries,
coordinate rounding, painter order, and the lifetimes of native DCs and surfaces.
