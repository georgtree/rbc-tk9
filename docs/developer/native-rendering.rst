Native renderer internals
=========================

The native backend implements the renderer interface without changing component
mapping, painter order or picking. ``rbcRenderNative.h`` is an internal boundary;
components should call ``rbcRender.h``. See :doc:`rendering` for dispatch and
:doc:`native-reference` for selected batch and clip lifetime contracts.

Component-facing screen operations
----------------------------------

The one-shot adapters in :doc:`reference` preserve native drawing even when the
graph selects Cairo. They borrow their resources until return and do not return
contexts to destroy. :c:func:`Rbc_RenderScreenBorder`,
:c:func:`Rbc_RenderScreenRectangle` and :c:func:`Rbc_RenderScreenFocus` provide
widget decoration; :c:func:`Rbc_RenderScreenCopyArea` copies already painted
pixels. :c:func:`Rbc_RenderScreenSegments` preserves XOR and other GC operations.

:c:func:`Rbc_RenderScreenTextBackground` adds the text anchor before short
conversion. :c:func:`Rbc_RenderScreenArrowHead` keeps doubles until scan
conversion. Neither operation performs axis mapping. Keep track clip scopes
outside these calls; graph bounds alone do not identify the owning track.

Legend samples have distinct coordinate contracts:
:c:func:`Rbc_RenderScreenLegendLine` takes a center and half-length, whereas
:c:func:`Rbc_RenderScreenLegendBar` takes a top-left corner and side length.
The bar adapter resets the GC stipple origin to zero after drawing, rather than
restoring a saved nonzero origin.

:c:func:`Rbc_RenderScreenRun` invokes a synchronous C callback, with a temporary
shared Windows target when available. Its data and target cannot escape the
call. :c:func:`Rbc_RenderScreenBitmapRun` accepts only opaque, unmasked,
copy-compatible batches: fallback may replay the full batch and leaves GC
clipping cleared. These restrictions matter when sharing GCs between components.

:c:func:`Rbc_RenderBeginScreenClip` instead returns an owned context. Query
:c:func:`Rbc_RenderClipDrawable` after each push, draw only if it is not None,
and pop with :c:func:`Rbc_RenderClipEnd` to commit to the parent. End the context
with :c:func:`Rbc_RenderEnd`; do not reuse a popped temporary drawable.

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

Backend contract navigation
----------------------------

The native reference documents parameter counts, coordinate units and ownership
at each backend entry point. These are private services; graph components enter
through the screen adapters described above. Platform-specific references are
separate because the same function name can have different resource and failure
contracts on X11 and Windows.

.. list-table:: Batch interfaces and platform contracts
   :header-rows: 1
   :widths: 22 39 39

   * - Service
     - X11
     - Windows
   * - Trace lifetime
     - :c:func:`rbcNativeX11.Rbc_NativeBeginTrace` owns point workspace.
       :c:func:`rbcNativeX11.Rbc_NativeEndTrace` frees it.
     - :c:func:`rbcNativeWindows.Rbc_NativeBeginTrace` also retains a DC and
       selected objects; :c:func:`rbcNativeWindows.Rbc_NativeEndTrace` restores them.
   * - Fill-only polygon batch
     - :c:func:`rbcNativeX11.Rbc_NativeBeginPolygonBatch` returns NULL;
       use the ordinary fill path.
     - :c:func:`rbcNativeWindows.Rbc_NativeBeginPolygonBatch` reserves vertex
       workspace. Each :c:func:`rbcNativeWindows.Rbc_NativeBatchPolygon` must fit it.
   * - Bar borders
     - :c:func:`rbcNativeX11.Rbc_NativeBarBorders` declines optimization.
     - :c:func:`rbcNativeWindows.Rbc_NativeBarBorders` handles supported bevels;
       its result describes the configuration, not verified GDI output.
   * - Direct clipping
     - :c:func:`rbcNativeX11.Rbc_NativePlatformClip` returns false, selecting
       shared scratch clipping.
     - :c:func:`rbcNativeWindows.Rbc_NativePlatformClip` returns true even after
       partial setup failure. A None drawable suppresses drawing; unclip is required.
   * - Opaque bitmap runs
     - :c:func:`rbcNativeX11.Rbc_NativeOpaqueBitmapRun` requests shared fallback.
     - :c:func:`rbcNativeWindows.Rbc_NativeOpaqueBitmapRun` attempts a copy batch;
       replay policy remains with :c:func:`rbcNativeShared.Rbc_NativeBitmapRun`.

Counts are records, not bytes: traces count points; strip/arc operations count
Segment2D pairs; bar operations count rectangles. Polygon-symbol stride includes
the repeated closing point, whereas the distinct-vertex count excludes it.
These interfaces consume already mapped coordinates; parameter documentation
does not imply that every backend clamps or clips out-of-range geometry.

Batch lifetime and coordinate conversion
----------------------------------------

:c:func:`rbcNativeX11.Rbc_NativeBeginTrace` allocates context and point workspace together. X11
capacity follows the request-size limit. Windows uses a smaller 100-point base
capacity for wide polylines, otherwise the request-derived size. Continuation
chunks repeat the preceding endpoint to preserve connectivity; they do not
restart at an unrelated sample.

The borrowed GC carries the configured raster operation and drawing style.
X11 converts trace coordinates to ``short`` and emits ``XDrawLines``. Windows
converts to ``int`` and emits ``Polyline`` while retaining one acquired DC and
selected pen/brush across the trace batch. :c:func:`rbcNativeX11.Rbc_NativeEndTrace` restores the
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

:c:func:`rbcNativeX11.Rbc_NativePolygonPointCount` rejects undersized or unrepresentable requests;
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

After every push, obtain :c:func:`rbcNativeShared.Rbc_NativeClipDrawable`. ``None`` means no drawing
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

:c:func:`rbcNativeX11.Rbc_NativePlatformClip` returning true means the platform has handled the
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

:c:func:`rbcNativeX11.Rbc_NativeRun` calls its run callback synchronously. Windows acquires and
wraps one destination DC for the whole run. If acquisition or wrapping fails,
it calls the run on the original drawable instead. X11 invokes the callback on
the original drawable directly. Callback data and temporary targets must not
escape the call, and the callback must not invalidate borrowed graph resources.
This is an internal drawing callback, not a Tcl evaluation boundary.

:c:func:`rbcNativeShared.Rbc_NativeBitmapRun` is restricted to opaque, unmasked, GXcopy-compatible
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

For symbols smaller than three pixels, the current implementation draws all centers with
the fill GC and returns before symbol-type or interval dispatch. Larger symbols preserve
their historical templates and rounding: circle/square paths are platform batches,
simple crosses are independent segments, and filled shapes use polygon templates. The
helper named :c:func:`rbcNativeShared.Round` rounds ties away from zero; it assumes a
finite result that fits int.

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

Arc strokes combine segments only when successive endpoints compare exactly equal. Each
connected run becomes one XDrawLines request; a gap starts another request and therefore
another native dash phase. The current path converts to short coordinates without the
protocol clipping and request-size chunking used by
:c:func:`rbcNativeShared.DrawSegments`. Its callers must supply bounded mapped runs. A
failed outer allocation skips the stroke; a failed per-run allocation skips only that
run.

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

Windows compatibility resource boundary
---------------------------------------

The resource helpers in rbcWinDraw.c isolate private Tk Windows types. A drawable
wrapper, a DC acquisition record and a pixmap wrapper have different ownership
rules even though each bridges native Windows handles to Tk representations.

.. list-table:: Windows wrapper ownership
   :header-rows: 1
   :widths: 30 35 35

   * - Result
     - Ownership
     - Release rule
   * - Drawable from HDC
     - Owned wrapper, borrowed HDC.
     - Free the wrapper; restore/release the HDC separately.
   * - Drawable DC acquisition
     - Owned state record, even if the DC is NULL.
     - Always release the record once.
   * - Window from HWND
     - One static wrapper overwritten on each call.
     - Do not free, retain across calls, or treat as thread-local storage.
   * - Pixmap from HBITMAP
     - Tk bitmap wrapper with native bitmap lifetime.
     - Use normal Tk pixmap destruction; do not double-delete the HBITMAP.
   * - Pixmap handle lookup
     - Borrowed HBITMAP.
     - Do not delete it independently.
   * - System palette copy
     - Owned HPALETTE, or NULL.
     - DeleteObject when no longer used.

The acquisition record preserves Tk's drawable state; it does not remove the
backend's responsibility to restore explicitly selected pens, brushes and saved
GDI attributes. Rbc_GCToPen creates an owned pen but also sets TRANSPARENT
background mode on its borrowed DC. It maps caps/joins and bounded GC dash bytes;
a single dash value is duplicated for the Windows repeating user style. Pen
creation is asserted, rather than returned as a Tcl error.

Monochrome readback and fast paths
----------------------------------

Rbc_GetBitmapData requests positive-height one-bit BI_RGB data. Rows are bottom-up
and padded to DWORD boundaries. The returned pitch describes that padding; the
buffer contains bits only, without the DIB header or color table. The caller
frees it with ckfree. Temporary global-memory headers and DC acquisitions remain
internal. A zero or undersized biSizeImage is replaced by the calculated minimum
storage requirement. The current success check accepts any nonzero GetDIBits
return, rather than verifying the full requested row count.

The opaque-stipple fast path uses a monochrome pattern brush. It sets DC text
color from GC background and DC background color from GC foreground to preserve
stipple expansion, and applies the GC tile/stipple origin. It restores those
attributes and the previous brush after painting. Its FALSE result can mean
partial output; the caller can replay because eligibility is limited to GXcopy.
Failed DC acquisition releases both the allocated acquisition record and the
pattern brush before requesting fallback. A NULL DC does not imply that the
acquisition record is absent.

Opaque bitmap runs similarly retain source and destination DCs across the run,
restore background mode/colors, and release acquired state. The helper uses
SRCCOPY and relies on its caller for unmasked copy eligibility; it does not
inspect the GC operation to enforce that contract. A FALSE result after partial
painting therefore supports complete replay only under that caller contract.

Border-color extraction and rectangle fills use private Tk Windows helpers so
bevel geometry and shades remain consistent with Tk. The emulated request-size
function is a historical constant, not a live device-capacity query. These
compatibility services are not independent renderers and should not become new
entry points for graph components.

Windows compatibility primitives
--------------------------------

The remaining rbcWinDraw.c entry points implement Xlib-shaped rectangle, arc,
segment, point and text operations. They are lower-level compatibility services,
not graph mappers. Arrays and GCs remain borrowed, and the caller supplies valid
mapped geometry. Most entry points reject empty arrays or None targets, but do
not turn every GDI failure into a Tcl error or restore already painted pixels.

Rectangles and arcs
~~~~~~~~~~~~~~~~~~~

Rectangle outlines use one acquired DC and GC-derived pen for the entire array,
with a stock null brush. Their far edges include an extra pixel to retain the
historical X-to-Windows convention. Rectangles remain in input order, so
intersections can be painted repeatedly under the configured raster operation.

The arc converter accepts XArc angles in units of one sixty-fourth degree.
Sweeps with magnitude at least a full turn select Ellipse. Partial sweeps
normalize the starting/ending directions, swap them for negative extent and
invert the sine contribution for screen Y. Radial endpoints retain the existing
add-one-half then integer conversion; this is not symmetric rounding for
negative coordinates. Bounding-box far edges likewise gain one pixel.

A zero brush argument chooses an open Arc. A nonzero brush chooses Chord or Pie
according to the arc mode, with the actual pen and brush already selected by the
caller. The filled-arc entry point retains a GC-derived pen alongside its solid
brush, so it is not implemented as a null-pen fill-only operation. Zero sweeps
are not explicitly rejected in the converter, and DC arc direction is not set
there. These details describe the compatibility implementation; they do not
replace the higher-level arc marker's mapping contract.

XOR composition and direct fallback
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The XOR segment helper computes the segment bounds, expands them for stroke
width and intersects them with the destination clip box. Empty coverage is a
successful no-op. Otherwise it creates a compatible bitmap and memory DC,
clears the bitmap to black and translates the viewport so absolute segment
coordinates can be reused. Black is the XOR identity, so no destination
readback is required for composition.

Each segment is drawn as an independent Polyline into that bitmap. This keeps
dash restart and cancellation at intersections consistent whether crosshairs
are shown, hidden, or moved using a combined old/new segment set. Once all
segments have been composed, a single SRCINVERT operation presents the result.
This avoids displaying the intermediate erased crosshair state.

A failed offscreen attempt selects direct fallback. That path still uses one
Polyline per XOR segment; it must not switch to grouped XOR PolyPolyline calls,
which previously made erasure depend on batch grouping. Other raster operations
use bounded PolyPolyline batches, with MoveToEx/LineTo per-segment fallback if a
batch is rejected. The direct path may expose intermediate drawing and does not
provide transactional rollback. Setup, composition and destination-copy failure
must therefore be distinguished when changing replay policy.

Points and text
~~~~~~~~~~~~~~~

Point drawing selects the GC raster operation but writes foreground colors with
SetPixelV. It does not implement an explicit pixel-combination algorithm of its
own. Absolute mode uses each XPoint directly; other modes accumulate offsets in
LONG coordinates without an overflow check. Do not infer complete X11 raster
semantics solely from the presence of the ROP2 setup call.

String drawing delegates directly to Tk_DrawChars with the Tk_Font stored in
the GC, the counted string and baseline coordinates. Font ownership, encoding
interpretation and actual native text drawing remain with that Tk service.
The compatibility wrapper neither creates a separate font nor retains text.

Windows bitmap capture and transforms
-------------------------------------

The Windows image backend sits below the common image API. Its temporary
monochrome buffers use DWORD-aligned rows and MSB-first bit addressing. GetBit
and SetBit translate logical Y through ``height-y-1``. The byte stride is
``((width+31)&~31)/8``; the helper rejects widths that would overflow this
alignment calculation. Individual callers still own height/product validation.

Drawable capture uses a 32-bit DIB and SRCCOPY. Conversion reverses bottom-up
rows, reads BGR channels using the DIB row stride and sets output alpha to 255.
The gamma lookup applies ``pow(channel/255, inputGamma)``, with positive finite
gamma required. Captured pixels therefore represent the drawable's composed
appearance, not an extraction of original source-image alpha. The caller owns
the resulting RBC color image; temporary DC, bitmap and palette resources stay
inside the capture routine.

Transparency masks are binary: only alpha zero sets a mask bit. Partial alpha
is treated as opaque. None can mean that no transparent pixels were found as
well as an invalid-size or creation failure, so it is not a diagnostic status.
Photo blocks are traversed using their own pitch, pixel size and alpha offset.
The photo-mask path and RBC color-image mask path retain different row traversal
orders before CreateBitmapIndirect; they should not be consolidated by assuming
identical source storage. Both return owned Tk pixmaps when a mask is created.

.. list-table:: Monochrome transform contracts
   :header-rows: 1
   :widths: 25 40 35

   * - Operation
     - Mapping
     - Validation and result
   * - Rotate whole bitmap
     - Normalized angle; exact quadrant mappings or inverse rotation.
     - Checks dimensions and storage, bounds-checks samples, publishes output
       dimensions only on success after initial argument validation.
   * - Scale bitmap
     - StretchBlt with SRCCOPY.
     - Caller supplies valid dimensions; drawing success is not checked.
   * - Scale/rotate region
     - Region coordinates mapped through a virtual destination size.
     - Positive sizes and storage are checked; caller supplies an in-bounds,
       representable region. Quadrant sampling has no per-pixel bounds check.

The whole-bitmap rotation rounds its enclosing dimensions upward and checks
both size_t allocation limits and the DWORD biSizeImage boundary. The regional
operation allocates only the requested region, avoiding a full virtual bitmap.
Both release a destination pixmap on checked readback/upload failure. Their
SetDIBits success test is nonzero return, not an exact scanline-count check.
ScaleBitmap is less defensive: it returns the allocated pixmap after StretchBlt
without separately validating DC acquisition or the copy result. These contracts
must not be generalized into a promise that every returned native bitmap has
been fully verified.

Rbc_LastError captures the Windows error code and formats it into one static
buffer. The text is borrowed until the next call and is not thread-local. It
strips trailing line endings and supplies a numeric fallback, but uses the ANSI
FormatMessage variant. Callers retaining it or presenting it to a UTF-8 Tcl
interface must account for lifetime and encoding separately.

Internal template and buffer contracts
--------------------------------------

:c:func:`rbcNativeWindows.GetCirclePathTemplate` owns the temporary GDI path and
transfers two allocated arrays only on success. Its radius must keep
``2*radius+1`` representable; the function rejects a nonpositive radius but is
not an arbitrary-integer overflow checker. Release both arrays with ``ckfree``.
:c:func:`rbcNativeWindows.LoadCirclePath` then translates that template into
caller-sized buffers without allocating or checking capacity.
:c:func:`rbcNativeWindows.DrawCirclePathBatch` borrows those buffers and the
selected DC resources. The caller decides whether a failed batch may safely be
replayed through :c:func:`rbcNativeWindows.DrawCircleCentersDirect`.

:c:func:`rbcNativeWindows.LoadPolygonBatch` uses polygon offsets and a source
stride that includes closure, while its per-polygon copy count may omit closure
for filling. It widens existing ``XPoint`` shorts into ``POINT`` coordinates;
it cannot recover precision already lost before entry. Destination capacity
and source bounds belong to the caller.

The bevel helpers :c:func:`rbcNativeWindows.DrawWin3DRectangle1` and
:c:func:`rbcNativeWindows.DrawWinRbcOuter3DBorder` borrow extracted colors and a
prepared DC. :c:func:`rbcNativeWindows.Clamp3DBorderWidth` limits an already valid
positive width against the rectangle dimensions. The shared
:c:func:`rbcNativeShared.Fill3DRectangle` retains the historical outer-border
adjustment before delegating the remaining fill to Tk.

Bitmap packing and output ownership
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcWindowsImages.GetMonoBitmapStride` returns DWORD-aligned row bytes;
:c:func:`rbcX11Image.GetPackedBitmapRowBytes` returns tightly packed row bytes.
Neither alone validates the complete image allocation. The X11 pixel-count
helper :c:func:`rbcX11Image.GetUnixImagePixelCount` checks dimension multiplication,
while :c:func:`rbcX11Image.GetBitmapDimension` converts a finite rounded extent.
These helpers have different failure contracts: panic, zero result and ordinary
status-returning callers must not be treated interchangeably.

:c:func:`rbcWindowsImages.Rbc_RotateBitmap` assigns output dimensions only after
its initial argument validation; subsequent checked failure leaves them zero.
:c:func:`rbcWindowsImages.Rbc_ScaleRotateBitmapRegion` expects a region within the
virtual image, especially for exact quadrant mappings. A successful returned
pixmap is owned by the caller. In contrast,
:c:func:`rbcWindowsImages.Rbc_ScaleBitmap` does not independently report a failed
``StretchBlt``: a non-None pixmap alone is not proof of a successful copy.

:c:func:`rbcWindowsImages.Rbc_PhotoImageMask` borrows a photo block's pixels even
though the descriptor is passed by value.
:c:func:`rbcWindowsImages.Rbc_ColorImageMask` similarly borrows the color image.
Both represent exactly transparent pixels with a binary mask; partial alpha
is opaque for these operations. :c:func:`rbcWindowsImages.Rbc_DrawableToColorImage`
instead returns a newly owned, fully opaque color image after gamma conversion.

Retained native geometry contexts
---------------------------------

:c:func:`Rbc_RenderBeginNativeSymbols` retains the symbol-style pointer;
:c:func:`Rbc_RenderBeginBars` instead copies its style record, while borrowing
the resources referenced by that record. Both borrow the graph and drawable
until :c:func:`Rbc_RenderEnd`.

:c:func:`Rbc_RenderBeginNativeFill` passes the tile and stipple selector to the
native fill path. With no tile, a non-None selector enables GC filling; None
alone does not mean solid fill. :c:func:`Rbc_RenderBeginNativeSegments` retains
the supplied GC, including XOR and its existing clip.

:c:func:`Rbc_RenderBeginNativeArc` borrows already mapped fill vertices and
outline segments. Keep them consistent with the analytic arc passed to dispatch
and alive until End. :c:func:`Rbc_RenderBeginNativePolygonBatch` may retain a
Windows DC and point buffer; an unavailable batch falls back to ordinary native
polygon filling. It still returns a context requiring End.
