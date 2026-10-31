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
