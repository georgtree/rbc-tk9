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
   * - BeginStroke
     - Polyline for traces; Segments for strips/segments; End
     - Borrowed native GC preserves its raster operation and clip.
   * - BeginFill / BeginExportFill
     - FillPolygon, FillRectangles, FillArc, End
     - Screen creation can return NULL; export creation is Cairo-independent.
   * - BeginNativeFill / BeginBars
     - FillPolygon / FillRectangles respectively; End
     - Native bar relief and tile behavior remain intact.
   * - BeginScreenText
     - Text, TextLayout, TextSize, End
     - Tk text on native and Cairo graphs.
   * - BeginScreenRaster
     - TkImage, MappedBitmap, Pixmap, End
     - Mapped geometry; Cairo attempt with native raster fallback.
   * - BeginScreenClip
     - ClipBegin, ClipDrawable, ClipEnd, End
     - Drawing uses the returned drawable, not this context's geometry table.
   * - BeginExport
     - Polyline, Segments, Arc, LineStyle, DashBackground, End
     - Borrows the document state.
   * - BeginExportOutput
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
