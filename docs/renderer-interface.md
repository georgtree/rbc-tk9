# Graph renderer interface

`generic/rbcRender.h` declares drawing operations. `generic/rbcExport.h` declares the private document context,
`Rbc_ExportContext`, with an explicit PostScript or SVG backend, output buffer, error state and decoration policy.
Screen rendering and document export use separate constructors.

| File                      | Responsibility                                                                         |
|---------------------------|----------------------------------------------------------------------------------------|
| `rbcGrExport.c`           | Shared export setup/cleanup, drawing order, plot clipping and margins                  |
| `rbcGrPs.c`               | PostScript command/options, page layout, EPS preamble/trailer, preview and file output |
| `rbcFont.c` | Shared Tk 9 font selection, native font programs, validation and Unicode character maps |
| `rbcPdfFont.c` | PDF font resources, standard-font mode, CID/character mappings and ToUnicode |
| `rbcGrPdf.c` | PDF options, binary file/bytearray output, document objects, resources and byte-offset cross-reference table |
| `rbcGrSvg.c`              | SVG command/options, canvas dimensions, XML document envelope and UTF-8 file output    |
| `rbcRender.c`             | Drawing dispatch and backend primitive implementations                                 |
| `rbcPs.c`, `rbcGraph.pro` | Existing PostScript emitters and symbol procedures                                     |

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

SVG supports vector geometry, editable text, solid fill opacity, clipping, vector bitmap masks and symbols, and embedded
PNG photo markers with alpha. Document-local identifiers reuse bitmap geometry per symbol pass. PNG compression and
checksums use Tcl zlib APIs; no extra build dependency is needed. Stipple fills use vector patterns; photo area tiles
reuse the PNG writer in repeating patterns. `Rbc_RenderSetFillTile` supplies the borrowed tile to an export fill
context; the PostScript adapter retains its background-only fallback. Non-photo image tiles/markers render to black and
white pixmaps and recover alpha for conventional source-over image types. Window markers use the native drawable
capture and embed opaque PNG. Capture errors fail the export before opening the file. These raster paths reuse the PNG
writer and require no additional dependency. The SVG command renders and validates before opening its output file.

PostScript limitations remain unchanged: area opacity is ignored, tiled areas export their configured background,
and failed window capture uses the existing gray rectangle fallback. Font/color maps stay in the PS backend.

Tests are in `tests/RBC.graph.svg.A.test` and `tests/RBC.graph.postscript.*.test`. The SVG tests also exercise
alternating exports and recovery after an export error. Screen renderer tests remain separate.

## Direct PDF backend

`RBC_EXPORT_PDF` selects `pdfOps` and `pdfOutputOps` in `rbcRender.c`. It shares the export traversal and semantic
style inputs with SVG/PostScript, but writes PDF operators directly. The document writer in `rbcGrPdf.c` owns
resource storage. Stream lengths and cross-reference offsets count bytes; the returned document is a Tcl bytearray.
Resource entries keep stable addresses because Tcl_DString may point into its own inline buffer. Each primitive
balances its graphics-state saves/restores; the plot clip is shared across the traversal. The initial matrix maps
graph pixels into physical points and flips Y without screen-rendering half-pixel translations.

PDF/SVG font state is document-local and freed by `Rbc_ExportFree` on success and failure. `rbcFont.c` uses the public Tk 9
`font actual ... -- character` query (checked against Tk 9.0.3 and 9.1.0), not copied platform-private structures.
References: `generic/tkFont.c`, `unix/tkUnixFont.c`, `unix/tkUnixRFont.c` and `win/tkWinFont.c` in Tk 9.
Fontconfig finds scalable sfnt files on X11; GDI supplies individual tables on Windows. Collection faces are
rebuilt into standalone sfnt programs with corrected checksums. TrueType uses Type0/CIDFontType2 resources,
explicit CID-to-glyph maps and per-character widths. Name-keyed CFF uses embedded Type1C resources, split into
255-character encodings when necessary. Both paths emit UTF-16BE ToUnicode maps, including supplementary scalars.
Glyph codes and Unicode mappings are separate so characters sharing a glyph retain distinct extraction semantics.
Complete font programs are embedded; subsetting, variable fonts, CID-keyed CFF/CFF2 and color fonts are not implemented.
The font data reader checks table bounds and embedding permissions; fonts are not sourced from the PDF viewer.

Regression tests: `tests/RBC.graph.pdf.A.test` and `tests/RBC.graph.pdf.font.A.test`. They validate all object offsets and stream lengths, binary file
roundtrips, option isolation, resource growth, font errors before opening files, transparency and geometry restoration.

PDF `-embedfonts` defaults to true. With false, `rbcPdfFont.c` creates standard Type1 resources with
WinAnsiEncoding and rejects characters outside Windows-1252 before writing the file.

SVG `-embedfonts` defaults to false. With true, `SvgEmbeddedText` uses the shared native font selection
and explicit per-character positions, preserving the original Unicode in text/tspan elements.
`Rbc_SvgFontsFinish` emits base64 sfnt programs through CSS @font-face. It assigns document-specific
family aliases and rebuilds Unicode cmap formats 4/12 for used characters, including legacy Symbol fonts.
All glyph outlines are retained; table checksums and head checksumAdjustment are rebuilt. Embedding
permissions and supported-font restrictions match PDF. Importers must support CSS webfonts and data URLs.
Option/default/error recovery coverage is in `tests/RBC.graph.export.font.A.test`.
