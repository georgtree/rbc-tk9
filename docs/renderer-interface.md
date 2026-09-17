# Graph renderer interface

`generic/rbcRender.h` declares drawing operations. `generic/rbcExport.h` declares the private document context,
`Rbc_ExportContext`, with an explicit PostScript or SVG backend, output buffer, error state and decoration policy.
Screen rendering and document export use separate constructors.

| File | Responsibility |
|------|----------------|
| `rbcGrExport.c` | Shared export setup/cleanup, drawing order, plot clipping and margins |
| `rbcGrPs.c` | PostScript command/options, page layout, EPS preamble/trailer, preview and file output |
| `rbcGrSvg.c` | SVG command/options, canvas dimensions, XML document envelope and UTF-8 file output |
| `rbcRender.c` | Drawing dispatch and backend primitive implementations |
| `rbcPs.c`, `rbcGraph.pro` | Existing PostScript emitters and symbol procedures |

Graph components retain mapping, pen selection, label formatting and geometry preparation. Their `...Export`
entry points receive a generic document context and call `Rbc_RenderBeginExport*` constructors. The shared
traversal does not create document headers or select printer/canvas dimensions. `GRAPH_EXPORT` suppresses
screen-density decimation and temporary-layout change notifications for both export formats.

The PostScript adapter borrows its legacy `PsToken` through backend-private state. The SVG writer allocates no
PostScript token and emits XML directly; there is no conversion from PostScript and no Cairo dependency.
The public `.g postscript ...` and `.g svg ...` commands keep independent settings.

Call `Rbc_RenderEnd` for every drawing context. These contexts borrow document/color/Tk resources; ending one
does not close the document. Pair `Rbc_RenderPlotBegin` with `Rbc_RenderPlotEnd`. Finish each symbol batch before
starting another because the PostScript prolog shares its symbol procedure. Export presentation operations
are unavailable on screen contexts; screen text continues to use Tk.

SVG supports vector geometry, editable text, solid fill opacity, clipping, vector bitmap masks and symbols,
and embedded PNG photo markers with alpha. Document-local identifiers reuse bitmap geometry per symbol pass.
PNG compression and checksums use Tcl zlib APIs; no extra build dependency is needed. Stipple fills use vector patterns; photo
area tiles reuse the PNG writer in repeating patterns. `Rbc_RenderSetFillTile` supplies the borrowed tile to
an export fill context; the PostScript adapter retains its background-only fallback. Non-photo image
tiles/markers render to black and white pixmaps and recover alpha for conventional source-over image types.
Window markers use the native drawable capture and embed opaque PNG. Capture errors fail the export before
opening the file. These raster paths reuse the PNG writer and require no additional dependency. The SVG command renders and validates before opening its output file.
PostScript limitations remain unchanged: area opacity is ignored, tiled areas export their configured background,
and failed window capture uses the existing gray rectangle fallback. Font/color maps stay in the PS backend.

Tests are in `tests/RBC.graph.svg.A.test` and `tests/RBC.graph.postscript.*.test`. The SVG tests also exercise
alternating exports and recovery after an export error. Screen renderer tests remain separate.
