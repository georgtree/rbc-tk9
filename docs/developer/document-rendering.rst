Document renderer primitives
============================

The SVG, PDF and PostScript renderers consume mapped display geometry through ``Rbc_RenderOps``. They do not perform
axis mapping, decimation, page delivery or graph lifetime management. ``rbcRenderExport.c`` selects the backend and
prepares its style context; ``rbcRenderExportInt.h`` describes the shared state. See :doc:`rendering`, :doc:`exports`
and :doc:`document-render-reference`.

Context lifetime and state
--------------------------

Each constructor allocates a short-lived renderer context borrowing the ``Rbc_ExportContext``. A PDF context requires an
initialized PDF builder; a PostScript context requires the legacy token in ``backendData``. SVG writes to the current
export buffer. None of these constructors begins or completes a document. The export and graph must outlive all contexts
that reference them.

Stroke construction copies SVG/PDF colors and dash values, clamps width to at least one, and caches cap/join
settings. Fill and symbol constructors make shallow style copies: referenced colors, bitmaps and tiles remain borrowed.
Fill/symbol contexts also borrow the graph for display and window information. Changing a resource or destroying its
owner during drawing is not supported merely because the style structure was copied.

PostScript stroke/symbol setup emits state immediately. SVG/PDF generally read cached style when drawing. In particular,
PostScript's ``DashesProc`` and ``DrawSymbolProc`` are dictionary definitions shared by the document. Complete one
configured batch before another context changes them; ``gsave`` does not isolate these definitions. This differs from
PDF's local graphics-state scopes.

Use :c:func:`Rbc_RenderEnd` rather than calling the backend destructor directly. It unwinds explicit clips tracked by
the common dispatcher, then frees the context. It does not finish the document, release builder resources or balance a
separate plot-begin scope. Plot begin/end must remain paired by the caller.

Geometry and painter order
--------------------------

Primitives receive pixels with downward-increasing Y. The enclosing PDF page matrix converts pixels to points and
reverses Y. PostScript page setup supplies the equivalent coordinate system. Applying this conversion again in a
component would misplace geometry and scale stroke widths twice.

PDF paths use decimal coordinates and cubic Bezier pieces of at most 90 degrees for elliptical arcs. The common arc
dispatcher supplies finite, normalized, nonzero extents. Full ellipses close; partial chords close between endpoints;
pieslices add the center before closing. PostScript constructs a scaled unit circle and restores the transformation
matrix while retaining the path, so stroke widths and dashes stay in display units.

PDF independent segments are emitted one at a time, each with its own solid background pass followed by dashed
foreground when configured. This ordering matters at intersections. Connected polylines retain one connected path per
pass. PostScript splits connected paths at the legacy 1500-component limit and restarts at the shared
endpoint. Replacing either strategy with a different batching policy is a rendering change, not only an optimization.

Fills, patterns and resource ownership
--------------------------------------

PDF prepares fill resources before constructing the shape path. Pattern construction can emit temporary paths, so
reversing this order can consume the shape intended for painting. Polygon and arc fills use the even-odd rule; rectangle
and geometric-symbol fills use their respective ordinary fill paths.

:c:func:`PdfPattern` temporarily redirects the export buffer to a local DString, builds a colored tiling pattern,
restores the original buffer and registers the pattern with the PDF builder. The builder owns copied bytes; the renderer
caches only its resource ID. Stipples become horizontal bitmap runs over an optional background. Tiles use photo pixels
when available, otherwise the shared Tk image capture helper. Tile placement accounts for the graph's offsets within its
containing toplevel; pattern matrices also include page scale and Y reversal.

:c:func:`Rbc_RenderSetFillTile` borrows the new tile and invalidates the cached pattern ID for SVG/PDF. Existing
document resources remain owned by the export. Other style mutation is not a general cache-invalidation API: construct
another fill context when changing the style.

PDF fill opacity below one uses a nonstroking alpha resource. Nonpositive opacity suppresses filling. PostScript retains
its legacy foreground/background and stipple behavior; it does not gain alpha or image-tile support from sharing the
interface. Its rectangle and polygon paths also retain different fallback rules. See :doc:`postscript-output` before
attempting to unify them.

Images and masks
----------------

.. list-table:: PDF image paths
   :header-rows: 1
   :widths: 25 45 30

   * - Input
     - Representation
     - Temporary ownership
   * - Tk photo
     - Copied RGB stream, optional grayscale soft mask
     - Temporary RGB/alpha DStrings
   * - Other Tk image
     - Reconstructed RGBA capture, then RGB/soft-mask streams
     - Caller-owned color image freed after registration
   * - Window marker
     - Current mapped window pixels
     - Captured color image freed after registration
   * - Bitmap marker or symbol
     - Horizontal runs of filled vector rectangles
     - Acquired XImages released after emission

Photo conversion respects Tk pitch, pixel size and channel offsets. A distinct alpha channel contributes an SMask only
if at least one pixel is nonopaque. Resource registration copies bytes, so neither the photo block nor the capture must
remain alive after registration. The image-use matrix corrects raster Y orientation independently of the enclosing page
matrix.

The shared non-photo capture helper renders the image over black and white. It estimates alpha from the largest
per-channel difference, then unpremultiplies the black capture. It releases its pixmap, GC and second capture and
returns one owned color image. This approximates conventional alpha compositing; it cannot recover arbitrary custom
image drawing semantics. Deleted or zero-sized images are omitted, whereas pixmap/capture failures record export errors.

Window export captures the current mapped window; it does not create a hidden widget layout or a clipping container. The
component/export policy determines whether to request capture. PostScript deliberately omits generic non-photo Tk images
and uses its legacy photo/window emitters for the other cases.

Text, borders and symbols
-------------------------

PDF text obtains an owned layout, resolves document font characters and adjusts zero-width Tk measurements only when the
fallback glyph has a positive advance. It recomputes justification before anchoring and rotating the layout. Shadow is
painted before normal/active foreground. Glyph placement combines Tk prefix advances with fallback corrections; this
emitter is not an independent shaping engine. See :doc:`text-fonts` for font selection and embedding and
:doc:`pdf-builder` for finalization of reserved font resources.

PDF relief borders use Tk's lazily initialized border colors and paint solid rectangles plus a polygon. Groove/ridge
compose two smaller reliefs. Clearing a PDF rectangle paints opaque white; it does not restore transparency. These
operations do not acquire ownership of the Tk border or colors.

Symbol batches preserve legacy per-shape size corrections and rounding. Geometric symbols fill before outlining, skinny
plus/cross symbols only stroke, and bitmap symbols preserve aspect ratio. PDF reads bitmap/mask XImages once per batch
and rejects mismatched mask dimensions when the mask is used. Bar legend samples use unscaled squares with the fill
context. PostScript instead configures prolog macros; its validity requirements and fallback behavior must be reviewed
separately from PDF's checks.

Errors and callback boundaries
------------------------------

Most emitters return void. A zero resource ID or false fill result is not a complete export status: empty/disabled
content can be omitted normally, while other failures record an error on the export. Partial output must be discarded
when the enclosing export fails. Temporary content buffers must be restored even when resource creation fails; renderer
destruction alone cannot repair an unbalanced graphics scope or finish a partially emitted document.

These functions run on the Tk interpreter's thread and borrow live Tk resources. Ordinary append-only geometry does not
evaluate Tcl scripts, but image display procedures and legacy token helpers can cross external callback boundaries. The
non-photo raster helper checks image validity at entry but does not retain or revalidate all borrowed resources across
arbitrary custom image display callbacks. Do not infer protection against such destructive reentry from the export
command's graph guard. This documentation patch does not change that behavior or the public C API.

Backend parameter and scope contracts
--------------------------------------

The references describe mapped coordinates in display pixels, counts in geometry records and raster lengths in
bytes. These units are not interchangeable. :c:func:`PdfPath` appends geometry without painting; :c:func:`SvgArcPath`
opens an XML path that its caller must finish; :c:func:`PostScriptArcPath` leaves a path for a subsequent stroke or
fill. None performs data-axis mapping.

.. list-table:: Format-specific state policies
   :header-rows: 1
   :widths: 20 27 27 26

   * - Operation
     - SVG
     - PDF
     - PostScript
   * - Stroke state
     - :c:func:`SvgStrokeAttributes` emits cached attributes.
     - :c:func:`PdfStrokeStyle` emits cached state inside a caller-owned scope.
     - :c:func:`PostScriptLineStyle` emits state immediately;
       :c:func:`PostScriptDashBackground` changes a document-wide procedure.
   * - Missing color
     - :c:func:`SvgColor` writes ``none``.
     - :c:func:`PdfColor` leaves the current color unchanged.
     - Callers use the legacy foreground/background mapping helpers.
   * - Fill preparation
     - :c:func:`SvgFillPattern` defines or reuses the context pattern.
     - :c:func:`PdfFillStyle` prepares resources before path construction.
     - :c:func:`PostScriptFillPath` uses legacy solid/stipple behavior.
   * - Rectangular clip
     - Pair :c:func:`SvgClipBegin` with :c:func:`SvgClipEnd`.
     - Pair :c:func:`PdfClipBegin` with :c:func:`PdfClipEnd`.
     - Pair :c:func:`PostScriptClipBegin` with :c:func:`PostScriptClipEnd`.

The color row describes the low-level color helpers, not a universal rule for all primitives: individual polygon,
rectangle and background callbacks have additional omission or fallback policies. Plot scopes are separate from explicit
clip depth. Pair :c:func:`SvgPlotBegin`, :c:func:`PdfPlotBegin` or :c:func:`PostScriptPlotBegin` with its matching
plot-end callback before ending the renderer context.

Raster inputs stay borrowed. :c:func:`PdfPhotoResource` copies pixels into document resources, while
:c:func:`PdfImageUse` places the resulting object ID. :c:func:`SvgPhotoBlock` emits an inline PNG using
:c:func:`SvgPngChunk` and :c:func:`SvgBase64`. Byte-count and channel-layout requirements remain the caller's
responsibility even where helpers check size limits. Returned resource IDs and handled flags do not replace the export
document's error status.

Shared backend setup
---------------------

:c:func:`Rbc_RenderCreateSvg`, :c:func:`Rbc_RenderCreatePdf` and func:`Rbc_RenderCreatePostscript` allocate operation
:c:contexts without creating a document. The initialized export token remains borrowed. A PDF builder or PostScript
:c:token must be ready before the first drawing operation. These constructors are internal building blocks; component
:c:code normally uses the style-aware constructors in :doc:`reference`.

:c:func:`Rbc_RenderExportLineStyle` caches cap/join settings and func:`Rbc_RenderExportDashBackground` copies an
:c:optional color for SVG/PDF. The corresponding PostScript setup is immediate: func:`Rbc_RenderPostscriptStroke`,
:c::c:func:`Rbc_RenderPostscriptSymbol` and func:`Rbc_RenderPostscriptBarSymbol` emit attributes or procedure
:c:definitions. The style pointers do not transfer ownership of colors or bitmaps.

:c:func:`Rbc_RenderExportRasterImage` returns a caller-owned color image; NULL can mean ordinary omission or capture
failure, so also inspect the document error state. :c:func:`Rbc_RenderExportSymbolRound` preserves legacy rounding
halfway away from zero and requires a finite value whose adjusted result fits an int. Neither helper is a general
input-validation boundary.

:c:func:`Rbc_RenderExportEnd` only frees the context allocation. Call func:`Rbc_RenderEnd` to unwind tracked clips
:c:first, and finish the separate plot scope before doing so. Backend resources and document buffers remain the export
:c:owner's responsibility.
