Window and photo command implementation
=======================================

``rbcWinop.c`` implements the standalone ``rbc::winop`` command when NO_WINOP
is not defined. It owns no per-handle object: :c:func:`WinopCmd` selects an operation
and passes the current Tk main window as client data. See the Tcl manual for
command syntax; this chapter describes dispatch, resources and limitations.

Dispatch and platform boundaries
--------------------------------

The direct winOps table and nested imageOps table are separate. :c:func:`ImageOp` validates
its own operation/arity, then removes the ``image`` word by passing ``objc - 1``
and ``objv + 1``. Shared handlers consequently keep the same source/destination
argument indices. Do not add a handler to one table assuming the other is an
automatic alias: quantize is direct-only, while gradient and rotate are nested.

:c:func:`GetRealizedWindow` resolves a Tk path and creates its native window if necessary;
it does not map it. :c:func:`StringToWindow` accepts paths or legacy Tcl integer IDs.
Toplevel paths select wrappers. Numeric parsing uses Tcl_GetInt, so this path
must not be described as accepting arbitrary pointer-width native handles.
Windows numeric IDs pass through :c:func:`rbcWindowsResources.Rbc_WinWindowFromHWND`. See
:doc:`window-services` for raw-handle versus drawable distinctions.

Window operations issue native map, unmap, stack and movement requests. Pointer
query/warp uses screen or window coordinates, not graph data coordinates.
The colormap operation is compiled only for X11. These operations neither
participate in graph mapping nor acquire renderer contexts.

Native command contracts
------------------------

Lower and raise accept Tk paths only. They resolve and restack each window in
order, stop on a lookup error, and do not propagate Tk_RestackWindow status.
Map and unmap also accept legacy integer IDs, but do not use :c:func:`StringToWindow`:
numeric values are cast directly to Window. Their Tk-path branch realizes the
client window, edits TK_MAPPED through Tk_FakeWin, and sends the native request.
It does not select the toplevel wrapper or call Tk_MapWindow/Tk_UnmapWindow.
Consequently, the conversion and wrapper rules of snap must not be generalized
to these operations. Multi-window commands do not roll back earlier requests
when a later argument is invalid; an empty window list succeeds.

Move validates both screen distances before Tk_MoveWindow and uses coordinates
relative to the parent. Changes realizes a window, applies backing-store and
save-under hints to a toplevel's real window, and is a no-op for child windows.
Successful submission does not guarantee that a server honors these hints.

Query reports root coordinates as ``@x,y``. If XQueryPointer returns false, it
returns TCL_OK without setting a new result. Warpto without an argument is a
query; an ``@x,y`` argument selects the screen root, while a Tk path selects
the center of a mapped window. These are native pointer operations and can
generate events, but the handlers do not run a Tcl event loop themselves.

For numeric snapshot targets, :c:func:`GetWindowSize` has platform-specific geometry:
Windows reads the outer GetWindowRect, while X11 uses XGetGeometry dimensions.
The X11 implementation synchronizes before removing its scoped error handler,
whose client data points to a stack-local flag. Failed queries leave output
dimensions untouched; only dimension overflow adds a diagnostic in this helper.
The Windows helper likewise writes dimensions only after a successful query.

Colormap is a legacy X11 diagnostic, not a general visual inspection API. It
probes at most 256 writable cells, releases them, then queries IDs 0 through
255. Its bookkeeping assumes returned pixel IDs fit that range. Returned pairs
identify cells not acquired by the probe; they do not identify their owners.

Photo-operation ownership
-------------------------

Most operations require existing Tk photos. Tk_FindPhoto handles and filter
descriptors are borrowed. Color-image snapshots, transformed results and
convolution kernels are owned locally and released through their matching
APIs. Destination writes can notify Tk image clients; a handle lookup is not a
lifetime guard. See :doc:`image-compatibility` and :doc:`images-tiles`.

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * - Operation
     - Work and sizing
     - Important boundary
   * - convolve
     - Copy source; allocate kernel and output pixels
     - Nonempty square finite kernel and finite sum required
   * - quantize
     - Resize destination, then copy source and destination
     - One to 256 colors; resizing precedes color-count validation
   * - resample
     - Destination dimensions choose output size
     - Default none filter; equal sizes take a copy path
   * - subsample
     - Validate source rectangle; resize into destination
     - Default box filter; positive extent and nonnegative origin
   * - rotate
     - Evaluate angle, copy source, allocate rotated pixels
     - Finite degrees; Tcl expression evaluation can invoke callbacks
   * - gradient
     - Allocate pixels at existing photo size and temporary Tk colors
     - Linear/radial/rectangular paths include random perturbation
   * - readjpeg
     - Delegate file decoding into a borrowed destination
     - Returns an unsupported diagnostic without JPEG build support
   * - snap
     - Capture native pixels, optionally resize
     - Captures a drawable; does not rerender a graph at export size

Gradient retains its historical formulas. Linear uses clamped random noise
multiplied by the horizontal pixel index, rather than a normalized left-to-right
ramp. Radial uses distance from the normalized center; rectangular uses a fixed
rotation and Manhattan distance. Blank produces opaque white while still
requiring valid endpoint colors. Tests must not assume deterministic pixels for
the three randomized modes. Readjpeg delegates to the platform decoder only
when JPEG support was compiled in; otherwise it fails before photo lookup.

Resample rejects source dimensions of one pixel or less. If either destination
dimension is one pixel or less, it sets source dimensions and copies. Subsample
instead uses the requested region dimensions for such a destination. Both
resolve an optional vertical filter only when the first filter is non-NULL.
The pixel algorithms and edge policies are described in :doc:`image-resampling`,
:doc:`image-transforms` and :doc:`image-colors`.

Failure and callback boundaries
-------------------------------

These handlers do not provide a uniform transaction. Quantize can resize its
destination before detecting an invalid color count. Several paths ignore
Tk_PhotoSetSize/Tk_PhotoPutBlock status or publish via a void helper, so TCL_OK
must not be interpreted as a checked commit of every lower-level photo write.
Do not document a destination-unchanged-on-error guarantee for this family.

Rotate validates photo names before evaluating the angle, preserving the
interpreter across Tcl_ExprDoubleObj. After evaluation it rejects destroyed Tk
state and looks up both photos again before accessing pixels. Deleted photos
produce an error; same-name replacements become the current source/destination.
Expression errors retain their original diagnostic. This protects the angle
callback boundary without changing the family's photo-publication semantics.

Snap has a deliberate path-specific distinction: a Tk path uses the client
window and Tk dimensions, while numeric IDs use native geometry lookup.
Visibility and platform capture behavior matter. Tests should not confuse
this with widget ``snap`` or SVG/PDF/PostScript output, which have their own
rendering and restoration contracts.

Maintenance checks
------------------

Use the existing ``RBC.winop.*.A.test`` and ``RBC.winop.image.*.A.test`` families.
Check direct and nested dispatch separately, invalid kernels and regions,
source/destination dimensions, and toplevel snapshots on both platforms.
These tests need a Tk display. Pixel algorithms alone cannot validate native
capture, wrapper handling or photo client notifications.
