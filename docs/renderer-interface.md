# Graph renderer interface

The private interface is declared in `generic/rbcRender.h`. The PostScript backend retains the emitters in
`generic/rbcPs.c` and shapes in `library/rbcGraph.pro`; it does not depend on Cairo.

Graph components keep responsibility for mapping, clipping geometry, pen selection, label formatting and drawing
order. Drawing operations pass through renderer contexts. Screen native fallbacks remain in the components.

| Context           | Operations                                                                                                  |
|-------------------|-------------------------------------------------------------------------------------------------------------|
| PostScript stroke | Polyline, segments, line style, dash background                                                             |
| PostScript fill   | Polygon and rectangle fills                                                                                 |
| PostScript symbol | Symbol instances, including bitmap masks                                                                    |
| PostScript output | Text, photos, window snapshots, mapped bitmap masks, background polygons/rectangles, borders, plot clipping |
| Cairo screen      | Existing strokes, fills, shape batches and image operations                                                 |

Call `Rbc_RenderEnd` for every context. Contexts borrow the PostScript token, colors and Tk resources. Ending a context
releases its memory; it does not close the page or undo clipping. Pair `Rbc_RenderPlotBegin` with
`Rbc_RenderPlotEnd`. Finish a symbol batch before initializing another because the legacy prolog shares its symbol
procedure.

Export presentation operations are available on PostScript contexts, not on Cairo screen contexts. Screen text continues
to use Tk. The distinct interfaces preserve screen batching and avoid routing window redraws through export code.

`rbcGrPs.c` still owns the public command, layout/remapping pass, EPS preamble, page transforms, trailer, file output
and error handling. Graph traversal and component export entry points still carry `PsToken`; they are not yet a
backend-neutral document API. A future SVG implementation needs its own presentation capabilities and document
lifecycle, plus context propagation through that traversal. This migration centralizes the drawing primitives first.

PostScript limitations remain unchanged: area opacity is ignored, image-tiled line areas export only their configured
background, and failed window capture uses the existing gray rectangle fallback. Font/color maps and monochrome
foreground/background distinctions remain in the existing emitter.

Regression tests are in `tests/RBC.graph.postscript.*.test`. The existing bitmap-mask, polygon-clipping and arrow tests
cover those specialized paths; renderer tests separately exercise screen output.
