Native renderer internals
=========================

The native backend implements the renderer interface without changing component
mapping, painter order or picking. ``rbcRenderNative.h`` is an internal boundary;
components should call ``rbcRender.h``. See :doc:`rendering` for dispatch and
:doc:`native-reference` for selected batch and clip lifetime contracts.

Platform responsibilities
-------------------------

``rbcRenderNative.c`` contains shared native geometry and Tk-based text, image,
bitmap, relief and copy operations. Configure selects either
``rbcRenderNativeX11.c`` or ``rbcRenderNativeWin.c`` for platform batches and
platform clipping. Xlib-shaped calls in shared code are not necessarily Unix
only: Tk implements that API on Windows as well.

``rbcWinDraw.c`` is the Windows compatibility layer beneath these services. It
confines private Tk Windows interfaces, wraps drawable/DC acquisition, translates
GC state to GDI operations and supplies specialized raster/XOR helpers. It is
not another renderer context or an alternative graph mapper. New component code
should not bypass the renderer to call it directly.

Batch lifetime and coordinate conversion
----------------------------------------

``Rbc_NativeBeginTrace`` allocates context and point workspace together. X11
capacity follows the request-size limit. Windows uses a smaller 100-point base
capacity for wide polylines, otherwise the request-derived size. Continuation
chunks repeat the preceding endpoint to preserve connectivity; they do not
restart at an unrelated sample.

The borrowed GC carries the configured raster operation and drawing style.
X11 converts trace coordinates to ``short`` and emits ``XDrawLines``. Windows
converts to ``int`` and emits ``Polyline`` while retaining one acquired DC and
selected pen/brush across the trace batch. ``Rbc_NativeEndTrace`` restores the
previously selected objects, deletes the objects created by the batch, releases
DC state and frees the allocation. It does not free the GC or graph.

Independent strips have their own path. X11 delegates to native segments;
Windows batches two-point polylines with capacities of 8192 segments for narrow
lines and 680 for wide lines. These are implementation choices balancing device
limits and GDI costs. They should not be replaced by per-point dispatch.

The shared segment helper clips to the signed 16-bit protocol region before
converting out-of-range coordinates to ``XSegment``. This protection is specific
to that helper: it does not make every native primitive an arbitrary-coordinate
API. Trace and polygon paths retain their own historical conversion rules.
Callers supply mapped geometry with appropriate clipping.

Polygon and symbol batches
--------------------------

``Rbc_NativePolygonPointCount`` rejects undersized or unrepresentable requests;
X11 additionally checks its polygon request limit. A rejected polygon is not
automatically split, since splitting arbitrary filled polygons changes their
interior. Components that can safely partition their geometry do so before
drawing, as described for error bands in :doc:`fills`.

The Windows fill-only polygon batch owns point workspace, a solid brush and DC
state. It selects a null pen, installs the GC fill rule, and restores the old
fill mode and objects on completion. Its point conversion deliberately retains
the short-coordinate conversion used by the original path. X11 returns NULL
from this optional batch constructor, leaving the caller's ordinary fill path
in charge. A missing optimization is different from a valid empty clip target.

Symbol backends batch circles, squares and polygons while preserving symbol
selection counters, fill/outline order and resource style. Bar batches preserve
relief and border handling. These paths have platform-specific fallback rules;
sharing a renderer interface does not require replacing them with one generic
polygon loop. See :doc:`bars` for the component's mapped rectangles.

Rectangular clip frames
-----------------------

A native clip stack borrows its graph and base drawable. Each nesting depth
owns a reusable frame and, if needed, a graph-sized scratch pixmap. Dimensions
must remain stable for that stack lifetime. Frames intersect requested bounds
with the parent's rectangle and drawable bounds; endpoint arithmetic uses a
wide integer before clamping. Coordinates remain absolute widget coordinates.

After every push, obtain ``Rbc_NativeClipDrawable``. ``None`` means no drawing
is permitted, whether the intersection was empty or setup failed. The frame
still exists and must be popped. Reusing the previous drawable would bypass the
clip and can paint into adjacent tracks.

With scratch clipping, only the intersected rectangle is copied from parent to
scratch before drawing. Pop copies that same rectangle back. This preserves
existing background pixels and painter order, including operations involving
XOR or bitmap masks, without changing shared component GCs. The scratch may
receive drawing outside the rectangle, but those pixels are never committed.
An internally acquired copy GC is owned by the stack; a caller-supplied copy GC
must be suitable for these copies and remains borrowed.

Frames and scratch targets survive pops for reuse. Destruction first pops all
active frames, then releases cached pixmaps, frames and only the internally
owned copy GC. Destruction therefore commits outstanding drawing; it is not a
transaction-abort operation.

Windows DC clips
----------------

When DC clipping is permitted, Windows acquires the parent DC, saves its state,
intersects the rectangle and promotes that clip to a meta region. The meta
region survives bitmap drawing that installs a separate clip mask. A temporary
Tk-compatible drawable wraps the DC so existing native operations use it.
Pop frees the wrapper, restores the saved DC state and releases the acquisition.

``Rbc_NativePlatformClip`` returning true means the platform has handled the
request, not necessarily that it produced a drawable. Failed Windows setup can
return true with ``None``; callers must skip drawing and still unwind the frame.
X11 returns false, selecting scratch clipping. Do not turn a Windows setup
failure into an unclipped retry.

The drawable wrapper borrows its HDC: freeing the wrapper does not release the
DC. Conversely, ``Rbc_WinAcquireDrawableDC`` allocates a release-state record
even if acquisition produces no HDC. The record must always be passed to
``Rbc_WinReleaseDrawableDC``. That helper releases Tk's drawable state and its
own record; backend code remains responsible for restoring selected objects
and any additional GDI state it changed.

Runs and fallback boundaries
----------------------------

``Rbc_NativeRun`` calls its run callback synchronously. Windows acquires and
wraps one destination DC for the whole run. If acquisition or wrapping fails,
it calls the run on the original drawable instead. X11 invokes the callback on
the original drawable directly. Callback data and temporary targets must not
escape the call, and the callback must not invalidate borrowed graph resources.
This is an internal drawing callback, not a Tcl evaluation boundary.

``Rbc_NativeBitmapRun`` is restricted to opaque, unmasked, GXcopy-compatible
bitmaps. Windows first tries the optimized opaque-bitmap batch; X11 declines it.
Fallback clears the supplied GC's clip mask/origin and replays ``XCopyPlane``
for all positions. Replaying after a partial optimized failure is safe because
this path has copy semantics. It must not be generalized to XOR or masked runs,
or passed a GC whose mask must remain installed.

Cairo graphs still use native text, some raster fallbacks, decorations and XOR
presentation. Ordinary Cairo painting cannot substitute for XOR composition.
Keep platform resource acquisition at batch boundaries, and preserve fallback
order, rounding, stipple origins and complete-stroke clipping when changing
these implementations. Validation must cover both native platforms; a working
X11 path alone does not exercise GDI state restoration or Windows presentation.

Shared primitive contracts
--------------------------

The shared implementation is deliberately more specific than a general drawing
API. It consumes mapped geometry and borrowed resource styles. It does not
perform layout, repair invalid geometry, or uniformly preserve GC state.

.. list-table:: State changes made by shared helpers
   :header-rows: 1
   :widths: 28 42 30

   * - Path
     - State changed
     - Caller obligation
   * - Mapped bitmap
     - Installs mask and destination clip origin on the bitmap GC.
     - Use the designated private GC; the state remains installed.
   * - Bitmap symbols
     - Scales bitmap/mask, changes outline GC clipping, releases temporary pixmaps.
     - Reconfigure clipping before unrelated reuse of that GC.
   * - Bitmap-run fallback
     - Clears mask/origin and replays the complete run.
     - Supply opaque, copy-compatible input; never XOR input.
   * - Legend bar
     - Sets pattern origin locally, then resets it to zero.
     - Do not expect a previous nonzero origin to be restored.
   * - Tiled polygon
     - Sets tile origin through Rbc_SetTileOrigin before filling.
     - Treat tile-origin configuration as mutable client state.
   * - Interval-selected symbols
     - Advances the mutable style counter while examining centers.
     - Preserve the counter across related batches.

For symbols smaller than three pixels, the current implementation draws all
centers with the fill GC and returns before symbol-type or interval dispatch.
Larger symbols preserve their historical templates and rounding: circle/square
paths are platform batches, simple crosses are independent segments, and filled
shapes use polygon templates. The helper named Round rounds ties away from zero;
it assumes a finite result that fits int.

Native bar filling tests whether fillOpacity is positive; it does not implement
fractional alpha blending here. Representable rectangles are batched; an
out-of-range XRectangle flushes that batch before the wider-argument Xlib call.
That fallback avoids premature narrowing in RBC, but does not remove limits of
the eventual platform API. Relief borders follow fills, with a platform batch
attempt before the ordinary rectangle loop.

The polygon helper handles tiles and stipples only. With neither supplied it
draws nothing; solid fills have other dispatch paths. Tile availability and
polygon point limits are checked before allocating short-coordinate workspace.
A tile overrides the stipple selector, while stippled drawing assumes the GC
already contains the required fill configuration.

Arc strokes and arrowheads
--------------------------

Arc strokes combine segments only when successive endpoints compare exactly
equal. Each connected run becomes one XDrawLines request; a gap starts another
request and therefore another native dash phase. The current path converts to
short coordinates without the protocol clipping and request-size chunking used
by DrawSegments. Its callers must supply bounded mapped runs. A failed outer
allocation skips the stroke; a failed per-run allocation skips only that run.

Arrowheads instead retain floating-point vertices until scan conversion. At each
integer row, half-open edges avoid counting shared vertices twice; sorted
intersections define filled pixel spans. The helper clips spans to graph and
signed-short bounds and batches at most 128 rectangles. The renderer's enclosing
clip still determines which pixels belong to a track. Replacing this with a
rounded XFillPolygon changes diagonal neck coverage and shaft alignment.

Native legend line samples deliberately use two separate line requests. Merging
them into one polyline would change dash restart and the connection between rows.
Text-background polygons similarly retain addition of the anchor before short
conversion and the Convex fill rule.

X11 request and fallback contracts
----------------------------------

The X11 backend owns conversion workspace, but borrows component GCs and
already mapped input. A trace context and its point buffer share one allocation.
The base chunk capacity is one less than the XPoint request limit; the spare
slot lets continuation requests repeat the preceding endpoint without exceeding
that limit. This preserves connectivity. Each chunk is still a separate
XDrawLines request, with no explicit accumulated dash offset carried between
requests. Trace conversion directly narrows to short; independent strips use
the shared segment helper's protocol clipping instead.

Circle and square symbol helpers allocate workspace for the complete incoming
center array, then submit request-sized batches. Request batching therefore
bounds server requests, not peak client allocation. Each batch is filled before
its outlines are drawn. Polygon symbols instead fill the complete symbol set
before drawing the complete outline set. Preserve this distinction when
refactoring overlapping symbols: reordering fills and outlines can change the
visible result.

Coordinate conversion also differs intentionally in the current implementation:
circles cast the center before subtracting radius, whereas squares subtract
radius before casting. Both store unsigned-short diameters. These are historical
rasterization rules, not general validation for arbitrarily large coordinates.
Interval selection advances the caller's mutable counter for every examined
center; skipped centers still contribute to the next batch's phase.

.. list-table:: X11 optional-service results
   :header-rows: 1
   :widths: 30 25 45

   * - Service
     - Result
     - Shared caller action
   * - Fill-only polygon batch
     - NULL context
     - Use ordinary polygon drawing.
   * - Optimized bar borders
     - FALSE
     - Draw relief rectangles individually.
   * - Direct platform clip
     - FALSE, output untouched
     - Use a reusable scratch pixmap.
   * - Opaque bitmap batch
     - FALSE, no drawing
     - Replay the complete run with XCopyPlane.
   * - Generic native run
     - Synchronous callback
     - Use the original drawable; no DC wrapper is needed.

These declined optimizations are normal backend choices. In particular, they
must not be interpreted as failed component rendering or as successfully
installed empty clips. X11 clip payload size is zero, while the common clip
frame still owns its scratch pixmap and bounds. Platform cleanup is a no-op;
shared pop performs the visible-region copy back to the parent target.

Single polygon requests are checked against both INT_MAX and the XPoint request
limit. An oversized polygon is rejected rather than naively split: independent
pieces need not reproduce its interior. Bar-fill batches are already packed and
bounded by the shared caller, so the X11 endpoint simply issues XFillRectangles.

Windows batch eligibility and ownership
---------------------------------------

Windows batches retain a drawable DC and selected objects across many primitives.
Trace state owns a pen, brush and trailing POINT workspace. EndTrace restores
previous selections before deleting the batch objects and releasing DC state.
Polyline coordinates convert to int; strips convert to LONG. The polygon marker
batch intentionally converts through short first to preserve its earlier native
rasterization. These paths do not share a universal coordinate-conversion rule.

Circle batching asks GDI to generate an Ellipse path once, using a bounding box
of diameter ``2*radius+1``. Translated copies reproduce that geometry without
substituting an independently approximated circle. The template arrays and batch
buffers are owned by the call and released even when setup selects fallback.

.. list-table:: Windows symbol batching rules
   :header-rows: 1
   :widths: 25 35 40

   * - Drawing path
     - Batch choice
     - Fallback or ordering constraint
   * - Filled circles
     - At most 32 figures with WINDING fill.
     - GXcopy only; combined fill/outline also requires equal colors.
   * - Circle outlines
     - 512 figures, or 64 with wide outlines.
     - GXcopy only; failed batches replay individual Ellipse calls.
   * - Polygon fills
     - Plus: 16; cross: 8; other templates: 32.
     - Copy fills use WINDING unions; non-copy fills remain individual.
   * - Polygon outlines
     - 16384-point budget, or 1360 for wide pens.
     - Copy paths use PolyPolyline; non-copy outlines remain individual.
   * - Independent strips
     - 8192 segments, or 680 with wide pens.
     - Each PolyPolyline entry contains exactly two points.

Polygon fill batches omit the repeated closing vertex; outline batches include
it. All fills precede all outlines. A failed copy batch can replay individual
shapes because repeating copy painting is safe. That argument does not apply to
XOR or other order-sensitive raster operations, which use individual polygon
calls. Circle setup failure similarly selects the existing per-symbol path;
it must not silently broaden eligibility to unequal fill/outline colors.

Bar bevel batching handles raised/sunken widths one and two. One-pixel bevels
reproduce Tk strip order and tiny-rectangle clamping. Two-pixel bevels reproduce
RBC's special outer layer followed by the inner Tk-style bevel. Other reliefs
and widths return FALSE for ordinary shared fallback. TRUE reports that the
configuration was handled, not that every GDI operation was checked.

Failure and cleanup boundaries
------------------------------

Direct clip setup always returns TRUE on Windows, including acquisition or
SaveDC failure. The shared caller initializes the output to None and treats a
missing target as unavailable drawing, while still calling unclip. Successful
setup promotes the intersection to a meta region before wrapping the DC; bitmap
clip-mask changes therefore cannot bypass the enclosing rectangle. Cleanup frees
the wrapper, restores saved state and releases any acquired record.

Generic runs have a different policy: failed acquisition or wrapping invokes the
callback once on the original drawable. Successful runs invoke it on the wrapper
and then release that temporary target. Callback data and borrowed graph
resources must remain valid for the synchronous call.

The native APIs are mostly void drawing services, not transactional error
reporters. Several paths check workspace allocation but do not separately check
DC acquisition, selected-object creation, or each GDI drawing result. The
source contracts identify these limits rather than promising rollback or
complete output. The polygon and circle copy-replay paths handle particular
failures; they are not a blanket recovery guarantee for every native operation.
