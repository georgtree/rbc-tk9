Window and photo command implementation
=======================================

``rbcWinop.c`` implements the standalone ``rbc::winop`` command when NO_WINOP
is not defined. It owns no per-handle object: WinopCmd selects an operation
and passes the current Tk main window as client data. See the Tcl manual for
command syntax; this chapter describes dispatch, resources and limitations.

Dispatch and platform boundaries
--------------------------------

The direct winOps table and nested imageOps table are separate. ImageOp validates
its own operation/arity, then removes the ``image`` word by passing ``objc - 1``
and ``objv + 1``. Shared handlers consequently keep the same source/destination
argument indices. Do not add a handler to one table assuming the other is an
automatic alias: quantize is direct-only, while gradient and rotate are nested.

GetRealizedWindow resolves a Tk path and creates its native window if necessary;
it does not map it. StringToWindow accepts paths or legacy Tcl integer IDs.
Toplevel paths select wrappers. Numeric parsing uses Tcl_GetInt, so this path
must not be described as accepting arbitrary pointer-width native handles.
Windows numeric IDs pass through Rbc_WinWindowFromHWND. See
:doc:`window-services` for raw-handle versus drawable distinctions.

Window operations issue native map, unmap, stack and movement requests. Pointer
query/warp uses screen or window coordinates, not graph data coordinates.
The colormap operation is compiled only for X11. These operations neither
participate in graph mapping nor acquire renderer contexts.

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
