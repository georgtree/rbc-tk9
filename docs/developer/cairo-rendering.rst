Cairo surfaces and drawing contexts
===================================

``rbcRenderCairo.c`` implements screen rendering and the disabled-Cairo entry points. SVG, PDF and PostScript do not use
this backend. Components still own mapped geometry, clipping scopes and painter order; see :doc:`rendering`. Private
helper contracts are in :doc:`cairo-reference`.

Component-facing attempts
-------------------------

The one-shot Cairo helpers return a handled/fallback flag. A nonzero result means the caller must suppress its native
fallback; it does not promise that pixels were painted. For eligible Cairo calls, empty photos and nonpositive bitmap or
legend rectangles can be handled no-ops. Late drawing errors are reported during context completion, not by requesting
replay. Without Cairo, these attempts decline and their constructors return NULL.

:c:func:`Rbc_RenderArea` and :c:func:`Rbc_RenderAreaOpacity` fill polygons; func:`Rbc_RenderTileArea` fills from a
:c:repeating photo block; func:`Rbc_RenderPhoto` paints that block once. The block and its readable storage remain
:c:caller-owned. :c:func:`Rbc_RenderRectangles` applies opacity to the composed batch, including stipples; polygon
:c:opacity applies only to solid fills. :c:func:`Rbc_RenderLegendBar` uses the full drawable clip and restarts its
:c:stipple at the swatch origin.

:c:func:`Rbc_RenderBitmap` handles one mapped bitmap and an optional background polygon. For repeated symbols,
:c:func:`Rbc_RenderBeginBitmapSymbols` converts the source once, :c:func:`Rbc_RenderBitmapSymbols` paints centers in
input order, and :c:func:`Rbc_RenderEnd` releases the context and copied pattern. Positive target width selects an
explicit drawable extent and requires positive target height; zero selects the graph plot clip. Bitmap symbols truncate
their center coordinates before subtracting half the bitmap dimensions.

These helpers consume mapped coordinates. They do not perform data mapping, image scaling, or rotation; the component
must prepare those inputs first.

Construction and ownership
--------------------------

``CairoRenderContext`` embeds :c:func:`Rbc_RenderContext` first and owns a Cairo surface reference, ``cairo_t`` and
optional bitmap/stipple pattern. It copies stroke colors, dash values and offset. Its fill style is a shallow copy, so
referenced component colors remain borrowed until drawing completes.

:c:func:`BeginRenderTarget` rejects non-Cairo selection, absent drawable/color, nonfinite or nonpositive line width, and
nonpositive target dimensions. Setup failure frees partial resources and returns NULL. On X11 it wraps the drawable
using the Tk visual and explicit target dimensions. On Windows it either references the current marker-pass image or
acquires a drawable DC, verifies a raster-display target, saves the DC state and wraps it in a Win32 surface.
Printing/metafile targets retain the caller's native path.

The initial clip uses inclusive plot edges. It is established before translating by half a pixel, matching RBC's integer
pixel-center convention. The constructor sets line width, butt caps, round joins, graph antialias mode and dash state.
:c:func:`Rbc_RenderBegin` supplies graph/plot dimensions; :c:func:`Rbc_RenderBeginDrawable` uses the entire explicitly
sized target, for output such as legend samples. The latter is not implicitly constrained to the plot rectangle.

Contexts must end while their graph, drawable, component resources and enclosing marker target are
alive. :c:func:`CairoEnd` flushes the surface before native drawing can resume, captures context/surface status and
frees resources. A drawing failure is reported as a background Tcl exception while preserving interpreter state. This
occurs after drawing; it does not request a native replay. :c:func:`FreeRenderContext` alone is the
partial-construction cleanup helper and does not perform that completion/reporting sequence.

Path and resource contracts
----------------------------

:c:func:`BeginRenderTarget` takes a stroke width separately from drawable width/height. Its clip edges are inclusive;
the half-pixel translation is applied after clipping. :c:func:`GetAntialias` maps an already parsed option value, and
:c:func:`SetStrokeColor` copies color channels into the Cairo source.

The helpers below differ in their treatment of pending paths and state. A Cairo save/restore pair restores graphics
state, but does not preserve a consumed path. Callers must finish unrelated geometry before dispatching these
operations.

.. list-table:: Internal drawing contracts
   :header-rows: 1
   :widths: 35 65

   * - Helper
     - Path and state effect
   * - :c:func:`CairoPolyline`, :c:func:`CairoSegments`
     - Build mapped strokes and consume their paths through :c:func:`StrokeRenderPath`; segment flushing preserves
       double-dash order.
   * - :c:func:`CairoArcPath`
     - Replaces the path with a validated ellipse, restoring its temporary transform before :c:func:`CairoArc` or
       :c:func:`CairoFillArc` paints it.
   * - :c:func:`AppendRenderSymbol`
     - Appends independent shapes without painting or clearing earlier shapes.
   * - :c:func:`FillRenderArea`
     - Consumes the polygon path and changes transform/fill state; its caller must bracket or finish the context.
   * - :c:func:`CairoFillPolygon`, :c:func:`CairoFillRectangles`
     - Save and restore graphics state around painting, but consume the path. Rectangle opacity applies to the composed
       group.
   * - :c:func:`CairoClipBegin`, :c:func:`CairoClipEnd`
     - Pair graphics-state scopes through common dispatch. Pushing a clip consumes the pending path; popping cannot
       recover it.

:c:func:`CreateRenderStipple`, :c:func:`CreateRenderBitmap` and func:`CreateRenderPhoto` return owned patterns
:c:containing copied pixels. Their input images and colors remain borrowed. :c:func:`RenderAreaPixel` converts a
:c:missing color to transparency; this is distinct from constructors that reject a missing required stroke
:c:color. Completion through func:`CairoEnd` flushes and reports errors; failure cleanup through
:c:func:`FreeRenderContext` only releases resources.

Paths and clip scopes
---------------------

Each polyline call creates a separate trace. Independent segments remain separate subpaths. Stroke state belongs to the
context. For two-color dashes, :c:func:`StrokeRenderPath` paints a solid off-color stroke with the path preserved, then
foreground dashes. Equal foreground/off colors are reduced to one solid stroke at construction to avoid duplicate
coverage.

Nested rectangular clips use Cairo save/rectangle/clip and restore operations. The common renderer tracks nesting and
unwinds outstanding clips on End. These are Cairo graphics-state scopes, not the scratch-pixmap frames used by native
clipping. Pair operations through the common renderer API rather than mixing private restores with its depth
bookkeeping.

Arc geometry is already mapped before entering Cairo. The backend constructs an ellipse path using a temporary
translate/scale and restores the transform before stroking, preserving screen-space line width. Arc orientation and
closure remain the shared renderer convention; no data-axis mapping happens here.

Patterns and pixel conversion
-----------------------------

:c:func:`Rbc_RenderBeginFill` can decline unsupported styles, including a missing foreground or background-only
policy. Stipple bits are read before acquiring the destination context. A successful context owns the resulting pattern
and borrows the colors in its copied style.

:c:func:`CreateRenderStipple` reads monochrome data with ``XYPixmap``; this avoids the Tk/Windows monochrome inversion
associated with the other image format. It creates ARGB32 pixels from foreground/background colors, destroys the XImage,
and returns a repeating nearest-filtered pattern. A missing background can leave transparent pattern pixels. Solid-fill
opacity is applied separately; polygon and arc pattern pixels retain their constructed alpha rather than being
multiplied by solid-fill opacity. Rectangle batches and legend bar swatches instead apply opacity to a composed group,
including stipple paint.

:c:func:`CreateRenderPhoto` copies a borrowed Tk photo block into a new ARGB32 image. RGB offsets must be within the
pixel size. Alpha is used only if its offset is valid and distinct from all RGB offsets; otherwise pixels are
opaque. Each RGB channel is premultiplied using ``(channel * alpha + 127) / 255``. Source rows follow the Tk pitch;
destination rows follow Cairo's stride.

The new surface is marked dirty after direct pixel writes. Creating a pattern retains the surface, allowing the helper's
local reference to be destroyed. The returned pattern owns the copy independently of future Tk photo changes. Callers
must still provide a readable source block: channel checks do not validate the size of its backing allocation.

Photo tiles repeat from the toplevel-relative origin to match native tiling. A one-shot photo instead disables
repetition, positions the pattern at integer coordinates and cancels the context's half-pixel translation. Already
rasterized pixels must not be shifted and resampled as though they were vector geometry. Empty images are handled
without drawing, preserving tile precedence rather than exposing another fill behind a deleted/empty tile.

Windows marker-pass target
--------------------------

Windows can create one RGB24 DIB for a marker pass, avoiding per-marker bitmap
readback. :c:func:`Rbc_RenderBeginMarkerPass` rejects nested targets and invalid graph sizes. It acquires the
destination DC, creates the DIB surface, obtains its DC and image view, copies the existing destination into it and
marks the image dirty. Only after successful setup does it install ``graphPtr->renderTarget`` and replace the caller's
drawable with a temporary Tk-compatible wrapper.

The pass owns the surface, drawable wrapper and destination acquisition. Its image view and DIB DC are borrowed from the
surface. A context drawing on the exact shared target drawable takes its own reference to the image view. Before that
reference is used, GDI is flushed and the image is marked dirty, because native text or fallback operations may have
changed the DIB.

Every child context must finish before the pass ends. End flushes the image, copies the composed DIB back, clears the
graph target, frees the wrapper and surface, and releases the destination DC state. Copy failure produces a background
exception with interpreter state preserved. It does not replay the marker pass. Non-Windows builds decline this optional
optimization.

Fallback and validation boundaries
----------------------------------

Disabled-Cairo builds compile fallback entry points returning NULL/false or performing no operation as
appropriate. Successful geometry dispatch still requires a valid context; no-op stubs are not a replacement for caller
checks. The documentation enables the Cairo preprocessor branch solely for extraction.

Callers decide whether a declined constructor/helper should use native output. A successful context does not acquire
missing operations automatically: Cairo screen text remains native, and XOR presentation retains native semantics. See
:doc:`native-rendering` for those services and :doc:`fills` for fill-specific precedence and fallback rules.

Renderer changes should check alternating native/Cairo operations on one target, marker passes containing text and
photos, nested clipping, transparent pixels, stipple backgrounds, dash backgrounds and resized targets. Windows DIB/DC
synchronization and X11 surface wrapping exercise different lifetime paths.

Path consumption and batching
-----------------------------

Cairo paths are not part of saved graphics state. A save/restore pair restores sources, transforms and clipping, but
does not resurrect geometry consumed by fill, stroke or clip. Callers must finish each primitive before entering another
operation or clip scope. :c:func:`CairoArcPath` explicitly replaces the path; other append helpers rely on the preceding
operation having consumed its geometry.

Segment strokes flush every 8192 segments to bound path storage. With two-color dashes they instead finish each segment
separately, preserving painter order where segments intersect. Symbol batches append independent subpaths: circles start
new subpaths, segment symbols use separate pairs, and polygons close. The component bounds ordinary symbol batches;
tiny point symbols flush their one-pixel rectangle paths every 8192 points.

Rectangle fills also flush every 8192 input entries. If opacity is requested, these fills accumulate into one group and
opacity is applied once to that group. Overlapping rectangles in a single call therefore do not accumulate opacity as
independent translucent objects. This differs from solid polygon calls, which paint their opacity directly. Preserve
call boundaries when changing batching.

Coordinate and pattern state
----------------------------

Stroke contexts start with a half-pixel translation. Area boundaries, integer rectangles, bitmap symbols and photos
cancel it where their contracts require pixel-boundary coordinates. :c:func:`FillRenderArea` makes that translation
without saving state itself: reusable polygon callers bracket it, while one-shot tile and bitmap callers finish the
context afterward. Calling it repeatedly without a corresponding restore would accumulate translations.

Stipples and photos produce repeating nearest-filtered patterns. Mapped bitmap patterns do not repeat, and one-shot
photos explicitly disable repetition after conversion. Bitmap masks may alias their source bitmap; the conversion helper
shares and releases the readback once in that case. Dimensions remain the caller's responsibility, especially for raw
mapped pixmaps outside Tk's named bitmap registry.

Stipple colors are converted to eight-bit channels with rounded division by 257, with NULL meaning transparent
zero. Photo colors use alpha premultiplication instead. These conversions serve different source layouts and should not
be merged by substituting simple channel truncation.

Initial stroke construction collapses equal foreground/off-colors to a solid stroke. Later :c:func:`CairoDashBackground`
calls only enable or disable the underlay and copy its color; they do not repeat that optimization or reconstruct dash
values discarded during construction. Callers needing a different dash pattern must construct an appropriately
configured context.
