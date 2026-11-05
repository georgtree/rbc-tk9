Rendering architecture and contracts
====================================

Components map data before drawing. Renderer calls receive display coordinates,
resolved colors and borrowed resources. Axis transforms, track ownership,
picking and painter order remain component responsibilities.

Module boundaries
-----------------

.. list-table::
   :header-rows: 1
   :widths: 36 64

   * - Module
     - Responsibility
   * - ``rbcRender.c``
     - Common dispatch, arc validation and native screen adapters.
   * - ``rbcRenderCairo.c``
     - Cairo geometry, surfaces, marker targets and disabled-Cairo fallbacks.
   * - ``rbcRenderNative.c``
     - Shared native geometry, Tk text/images/relief, clip stacks.
   * - ``rbcRenderNativeWin.c``
     - GDI batches, DC clipping and platform resources.
   * - ``rbcRenderNativeX11.c``
     - X11 batches, request limits and platform fallback decisions.
   * - ``rbcRenderExport.c``
     - Export constructor selection, shared styles and image capture.
   * - ``rbcRenderPostscript.c``
     - PostScript primitives, prolog symbols and clip scopes.
   * - ``rbcRenderSvg.c``
     - SVG primitives, patterns, text, images and clipping.
   * - ``rbcRenderPdf.c``
     - PDF drawing primitives and resource use.

Configure selects one native platform implementation. The common native layer
uses Tk/Xlib-shaped calls on both platforms; Tk implements those calls on
Windows too. Platform files hold algorithms that actually differ, not every
function whose name starts with X.

Cairo types remain in the Cairo implementation. Disabled-Cairo builds still
compile that translation unit for its fallback entry points. SVG, PDF and
PostScript output do not require Cairo.

Contexts and capabilities
-------------------------

Every allocated backend context embeds :c:struct:`Rbc_RenderContext` first.
Its operation table determines the legal operations. A missing callback is
unsupported, not a request for automatic fallback; many dispatchers call the
callback directly.

.. list-table::
   :header-rows: 1
   :widths: 30 45 25

   * - Constructor family
     - Operations
     - Important constraint
   * - :c:func:`Rbc_RenderBeginStroke`
     - Polyline for traces; Segments for strips/segments; End
     - Borrowed native GC preserves its raster operation and clip.
   * - :c:func:`Rbc_RenderBeginFill` / :c:func:`Rbc_RenderBeginExportFill`
     - FillPolygon, FillRectangles, FillArc, End
     - Screen creation can return NULL; export creation is Cairo-independent.
   * - BeginNativeFill / BeginBars
     - FillPolygon / FillRectangles respectively; End
     - Native bar relief and tile behavior remain intact.
   * - :c:func:`Rbc_RenderBeginScreenText`
     - Text, TextLayout, TextSize, End
     - Tk text on native and Cairo graphs.
   * - :c:func:`Rbc_RenderBeginScreenRaster`
     - TkImage, MappedBitmap, Pixmap, End
     - Mapped geometry; Cairo attempt with native raster fallback.
   * - :c:func:`Rbc_RenderBeginScreenClip`
     - ClipBegin, ClipDrawable, ClipEnd, End
     - Drawing uses the returned drawable, not this context's geometry table.
   * - :c:func:`Rbc_RenderBeginExport`
     - Polyline, Segments, Arc, LineStyle, DashBackground, End
     - Borrows the document state.
   * - :c:func:`Rbc_RenderBeginExportOutput`
     - Text, images, window capture, backgrounds, borders, plot scopes, End
     - PlotBegin and PlotEnd must be paired.

Consult :doc:`reference` for the native arc, symbol and specialized Cairo
families. Do not use this table to infer extra capabilities.

Finish each allocated context with :c:func:`Rbc_RenderEnd`. It requires a
non-NULL live context, unwinds outstanding rectangular clips, and calls the
backend destructor. It does not destroy borrowed graph, document or component
resources. Stack-based one-shot screen adapters manage their own lifetime.

For example, a mapped trace batch:

.. code-block:: c

   Rbc_RenderContext *ctx = Rbc_RenderBeginStroke(
       graphPtr, drawable, gc, RBC_RENDER_STROKE_TRACES,
       color, width, dashes, offColor);
   if (ctx != NULL) {
       Rbc_RenderPolyline(ctx, points, count);
       Rbc_RenderEnd(ctx);
   } else {
       /* The caller handles unavailable drawing resources here. */
   }

Keep borrowed geometry and resources alive for the documented lifetime. Native
arc contexts retain mapped arrays until End; symbol sampling can mutate the
borrowed native style counter. Export symbol batches must finish before starting
another because the PostScript symbol prolog is shared.

Clipping and coordinate rules
-----------------------------

Rectangular clips are nested through :c:func:`Rbc_RenderClipBegin` and
:c:func:`Rbc_RenderClipEnd` on clip-capable contexts only. The common layer
owns depth; the backend owns Cairo saves, document graphics states or native
clip frames.

A screen clip preserves absolute widget coordinates, GC state and bitmap masks.
Fetch :c:func:`Rbc_RenderClipDrawable` after each push. None means the clip is
empty or failed, so skip drawing. Pop commits the child target to its parent.
Windows can use DC clipping where requested; other cases retain scratch pixmaps.

Arcs use a mapped ellipse. Zero degrees points right and positive sweeps turn
counterclockwise. Sweeps of magnitude at least 360 form one closed ellipse
without a radial seam. Components supply geometry; the renderer builds the
path under its clip. Geometry transforms do not scale stroke widths or dashes.

:c:func:`Rbc_RenderPolygonPointLimit` reports the selected backend's request
capacity before a context exists. The component reserves closure/clipping
vertices and splits at shared samples. Native X11 has a request-size limit;
the other selected outputs report TCL_SIZE_MAX.

Native details and fallback policy
----------------------------------

Text, widget decorations, focus, pixel copies and XOR overlays keep native
behavior where required on Cairo graphs too. Ordinary Cairo painting must not
replace XOR drawing. The shared Windows compatibility layer retains offscreen
XOR mask composition and one-step presentation.

Preserve conversion order and batching when moving code: text backgrounds add
the anchor before integer conversion; arrowheads keep doubles through scan
conversion; legend samples retain stipple origins. Marker run callbacks are
synchronous, and no callback data escapes the call.

The Cairo-attempt helpers can return NULL or false. Their callers select the
native fallback. This is distinct from invoking an unsupported operation on an
already-created context, which is a caller error.

Common dispatch and constructor state
-------------------------------------

``rbcRender.c`` supplies capability-specific screen adapters and the common
operation wrappers. Its native context embeds ``Rbc_RenderContext`` first so
backend callbacks can recover the enclosing record. Trace, clip and polygon
batch records are owned; windows, drawables, GCs, tiles, styles and mapped arc
arrays are borrowed. A bar style is copied by value, but its Tk resources are
still borrowed.

Select operations from the constructor's contract, not merely from non-NULL
entries in its operation table. Several constructors share a table while
initializing different state. For example, a native trace context owns trace
scratch storage, whereas a segment-only context does not. Likewise, native
symbol, fill, bar and arc constructors populate different fields of the shared
geometry record. Calling a sibling operation can therefore be invalid even
when the table contains that callback.

Allocated contexts must end exactly once. :c:func:`Rbc_RenderEnd` first unwinds
outstanding clips, then invokes the family destructor. One-shot decoration and
marker-detail adapters instead place a context on the stack and invoke their
callback directly; their tables have no destructor. They must never escape the
call or be passed to :c:func:`Rbc_RenderEnd`.

Screen fallback order
~~~~~~~~~~~~~~~~~~~~~

.. list-table:: Adapter decisions
   :header-rows: 1
   :widths: 24 76

   * - Operation
     - Selection and fallback
   * - Traces and segments
     - Try the Cairo constructor first. On failure, use a native context with
       the caller's configured GC. Only trace construction acquires trace state.
   * - Mapped bitmap
     - Resolve an explicit or GC foreground and try the renderer bitmap helper.
       If it cannot handle the bitmap, retain the native bitmap path and style.
   * - Tk image
     - On Cairo graphs, try a Tk photo block. Unsupported images or unsuccessful
       rendering fall back to native image drawing.
   * - Bar rectangles
     - Try the renderer fill. If handled, suppress only the native fill in a
       local style copy and retain native border drawing. Otherwise draw both
       fill and border natively.
   * - Polygon batch
     - Use the platform batch when available. A missing batch, including the
       X11 implementation, falls back to ordinary native polygon filling.
   * - Text and narrow screen details
     - Preserve native text, decoration, focus, copy, legend sample and marker
       run behavior, including on Cairo graphs.

A successful fallback decision is local to that operation; it does not change
the graph's selected renderer. The adapter does not retain a Tk photo block
beyond its synchronous call. Native marker runs similarly borrow callback data
only for the duration of the call; this is not a general Tcl callback lifetime
guard.

Validation at dispatch boundaries
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The common layer is not a general argument validator. Most wrappers require a
live context with the requested capability. Symbol and rectangle batches skip
nonpositive counts, and polygons require at least three points, while polyline
and segment dispatch leave count handling to their backend. Callers must retain
the corresponding arrays and satisfy each operation's coordinate contract.
Native text adapters truncate double coordinates to integers; they do not add
rounding or range checks.

Arc dispatch is an explicit exception: it rejects nonfinite geometry,
nonpositive radii, zero extent and invalid styles; it reduces the start angle
with ``fmod`` and clamps the extent to one revolution in either direction.
A negative start angle may remain negative. Open arcs do not fill. Native arc
contexts draw the component's borrowed, already-mapped segment and fill arrays,
so these arrays must describe the same arc as the analytic arguments. Common
normalization does not remap them or validate later integer conversion ranges.

Document ownership
------------------

Renderer contexts emit primitives; graph export commands own the document
lifecycle. ``rbcGrSvg.c`` and ``rbcGrPdf.c`` retain options and builders.
``rbcGrExport.c`` shares command dispatch, option rollback, geometry restoration
and channel delivery. PostScript retains its lifecycle in ``rbcGrPs.c``.
``rbcPdf.c`` owns PDF resources, streams, offsets and serialization.

Export contexts borrow document tokens. Errors use the first static message
recorded by ``Rbc_ExportError``; context destruction does not finalize output.
SVG remains UTF-8 text and PDF binary data. Font discovery and embedding remain
in the font modules.

Validation
----------

Changes to renderer code need native and Cairo checks on Windows and Linux,
including exports and tracks. Preserve painter order, pixel rounding, clipping
and batching. Documentation-only changes should still pass the strict manual
build and preserve all C declarations.

See :doc:`native-rendering` for platform batching, clip frames and Windows DC ownership.

See :doc:`cairo-rendering` for Cairo target setup, image conversion and mixed native drawing.

Argument lifetimes and dispatch requirements
--------------------------------------------

The component-facing declarations in :doc:`reference` describe parameter
directions using :doc:`calling-conventions`. An input array is borrowed for
the call unless a constructor explicitly retains it until End. Its count is
a number of records: :c:func:`Rbc_RenderSegments` counts ``Segment2D`` values,
not individual endpoints, while :c:func:`Rbc_RenderPolyline` counts points.
No dispatcher can verify the allocated capacity behind a pointer/count pair.

:c:func:`Rbc_RenderFillPolygon` suppresses counts below three;
:c:func:`Rbc_RenderFillRectangles` and :c:func:`Rbc_RenderSymbolPoints`
suppress nonpositive counts. This does not establish a universal validation
policy for every operation. Most wrappers immediately dispatch through the
operation table, so callers must supply a live, capable context and valid
geometry even when another operation tolerates a null pointer or empty batch.
In particular, :c:func:`Rbc_RenderArc` and :c:func:`Rbc_RenderFillArc` accepting
a NULL context does not make NULL safe for :c:func:`Rbc_RenderEnd`.

:c:func:`Rbc_RenderBeginStroke` uses supplied color, width and dash data for
Cairo, while its native fallback uses the borrowed GC. The caller must prepare
that GC consistently; passing a new color argument does not recolor the native
GC. :c:func:`Rbc_RenderGCForeground` resolves an existing GC foreground when
Cairo setup needs it. :c:func:`Rbc_RenderLineStyle` and
:c:func:`Rbc_RenderDashBackground` require supporting Cairo/export contexts
and do not provide generic mutation of a native stroke GC.

:c:func:`Rbc_RenderBeginExportFill`, :c:func:`Rbc_RenderBeginExportSymbol` and
:c:func:`Rbc_RenderBeginExportBarSymbol` copy style records, but the resource
pointers inside those records remain borrowed. Keep fonts, colors, bitmaps and
tiles alive for their documented use. :c:func:`Rbc_RenderSetFillTile` retains
a tile for export filling; it does not acquire ownership.

Screen text and raster batches
------------------------------

:c:func:`Rbc_RenderBeginScreenText` creates a Tk-backed batch even for a Cairo
graph. :c:func:`Rbc_RenderTextLayout` consumes a cached layout, whereas
:c:func:`Rbc_RenderTextSize` draws text and writes the rotated extent into
caller-owned storage. :c:func:`Rbc_RenderText` also serves export-output
contexts; its shared name does not make cached screen-layout operations
available on export contexts. Text styles and their resources remain owned
by the component.

:c:func:`Rbc_RenderBeginScreenRaster` supplies image presentation.
:c:func:`Rbc_RenderMappedBitmap` receives mapped geometry and a bitmap style,
and :c:func:`Rbc_RenderPixmap` uses the caller's copy GC. Neither performs
component-level scaling or rotation. :c:func:`Rbc_RenderTkImage` is supported
by screen raster and export output, while :c:func:`Rbc_RenderPhotoImage` and
:c:func:`Rbc_RenderWindow` are export-output operations. Export capture policy
and backend omissions still apply; a shared interface does not promise equal
support for every image source.

A marker-pass target has a separate lifetime from a renderer context.
:c:func:`Rbc_RenderBeginMarkerPass` may replace the caller's drawable and
install a temporary graph target. On failure it leaves the original drawable
usable. :c:func:`Rbc_RenderEndMarkerPass` presents and releases a successful
target and accepts NULL, but does not restore the caller's drawable variable.
The replacement drawable must not be used afterward. Do not pass this target
to :c:func:`Rbc_RenderEnd`.

Export scopes and completion
----------------------------

:c:func:`Rbc_RenderBeginExportOutput` borrows a supported document token; it
does not open or finish a file. :c:func:`Rbc_RenderPlotBegin` and
:c:func:`Rbc_RenderPlotEnd` delimit backend plot setup and must be balanced
before releasing the context. They are distinct from the common rectangular
clip stack. :c:func:`Rbc_RenderEnd` unwinds clips, but does not substitute for
PlotEnd or document finalization.

:c:func:`Rbc_RenderBackgroundPolygon`, :c:func:`Rbc_RenderBackgroundRectangles`,
:c:func:`Rbc_RenderBorder` and :c:func:`Rbc_RenderClearRectangle` consume
resolved presentation data. :c:func:`Rbc_RenderBitmapMask` preserves the
component's background/foreground pass selector: PostScript uses it for color
setup, while SVG/PDF paint the mask with the supplied color. These operations
return void, not a Tcl completion code; command-level export error handling
remains responsible for deciding whether the document succeeded.

The Cairo-specific :c:func:`Rbc_RenderPoints` and :c:func:`Rbc_RenderSymbols`
likewise require the correct context family. Symbol templates describe local
geometry, centers describe mapped positions, and stroke widths remain display
quantities. No renderer operation performs axis transforms or establishes
component ownership.

Internal adapter contracts
---------------------------

The common callbacks in :doc:`reference` now identify the context family they
require. :c:func:`rbcRenderDispatch.BeginNativeGeometry` installs a shared table
but initializes only its common fields. Specialized constructors still have to
populate symbol, bar, polygon or arc state before the corresponding callback
can be used. A non-NULL callback pointer alone is not a capability check.

:c:func:`rbcRenderDispatch.NativePolyline` requires a retained native trace;
:c:func:`rbcRenderDispatch.NativeSegments` consumes independent endpoint pairs.
:c:func:`rbcRenderDispatch.NativeSymbolPoints` borrows mutable interval state.
:c:func:`rbcRenderDispatch.NativeArc` and
:c:func:`rbcRenderDispatch.NativeFillArc` use precomputed arrays, while
:c:func:`rbcRenderDispatch.NormalizeRenderArc` validates the analytic geometry
before dispatch. Those two representations must remain consistent.

:c:func:`rbcRenderDispatch.ScreenMappedBitmap` and
:c:func:`rbcRenderDispatch.ScreenTkImage` select Cairo attempts before native
fallback. :c:func:`rbcRenderDispatch.ScreenBarRectangles` can suppress an already
handled fill in a local style copy while retaining native relief drawing.
These callbacks do not transfer ownership of component images or style resources.

Native clip pushes and pops use :c:func:`rbcRenderDispatch.NativeClipBegin` and
:c:func:`rbcRenderDispatch.NativeClipEnd`; final release uses
:c:func:`rbcRenderDispatch.ScreenClipEnd`. The last operation destroys the stack,
whereas a pop only completes one scope. Likewise,
:c:func:`rbcRenderDispatch.NativeGeometryEnd` and
:c:func:`rbcRenderDispatch.NativeEnd` release different retained backend state.
Stack-based decoration and detail adapters must never enter these destructors.
