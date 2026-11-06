Geometry, clipping and simplification
=====================================

``rbcGeometry.c`` implements point, segment and polygon operations without Tk resources, renderer state or graph
ownership. Its private declarations remain in ``rbcInt.h``. Callers map coordinates first, select a coordinate system
and provide the arrays. See :doc:`mapping` for graph/track transformations and :doc:`rendering` for actual drawing
clips.

Coordinates and borrowed storage
--------------------------------

:c:struct:`Point2D` and :c:struct:`Segment2D` carry double coordinates. :c:struct:`Extents2D` uses left/right/top/bottom
bounds; valid rectangles have finite coordinates with left <= right and top <= bottom. Zero-width or zero-height
rectangles are not rejected. In screen coordinates, top normally has the smaller Y value.

The helpers do not apply axis transforms, logarithms, track offsets or native pixel rounding. Distances and tolerances
use the same units as their input. Arrays are borrowed. Counts must describe allocated storage: a non-NULL pointer and
a positive count do not prove an array is large enough.

These are numerical primitives with sentinel/count results, not Tcl command error interfaces. Finite-input checks reject
many malformed cases, but they are not a guarantee against every intermediate overflow at extreme magnitudes.
Command-facing callers retain validation and diagnostic responsibilities.

Hit testing
-----------

:c:func:`Rbc_GetClosestPointOnSegment` projects onto the segment direction and clamps to its endpoints. A segment whose
length is at most ``DBL_EPSILON`` uses its first endpoint. It writes the closest point and returns Euclidean distance.
Rejected inputs/intermediates return ``DBL_MAX`` without writing the result; the final distance calculation can still
overflow for extreme finite values.

:c:func:`Rbc_PointInSegments` accepts a finite positive halo and tests ``distance < halo``, not <=. An exact hit with
halo zero therefore does not match. :c:func:`Rbc_PointInPolygon` uses even-odd ray crossings and closes the last vertex
back to the first. Its half-open edge comparisons avoid double-counting vertices, but do not establish a universal
inclusive-boundary policy. Use a separate segment-distance test when a boundary tolerance is required.

Despite its name, :c:func:`Rbc_RegionInPolygon` with ``enclosed`` true checks that the rectangle encloses every polygon
vertex. With ``enclosed`` false it tests all polygon edges against the rectangle, then tests the rectangle's top-left
corner inside the polygon. That final check handles a rectangle wholly inside a polygon without any edge crossing.

Line and polygon clipping differ
--------------------------------

:c:func:`Rbc_LineRectClip` uses Liang-Barsky clipping against the supplied bounds. Its output pointer may be ``NULL``
for visibility testing. With separate output storage, endpoints remain unchanged; rejected input leaves output
untouched.

:c:func:`Rbc_PolyRectClip` preserves a historical pixel-area convention: left/top are used directly, while right/bottom
are expanded by one coordinate unit. It also perturbs zero edge deltas by ``DBL_EPSILON`` internally. It is therefore
not an interchangeable polygon version of the line clipper for arbitrary mathematical coordinates.

The input polygon needs no duplicated closing vertex. Output is explicitly closed and its count includes the repeated
first vertex. The caller supplies a separate buffer and capacity; every append checks capacity, including the final
closing point. A conservative bound for the current implementation is three output vertices per input edge plus one
closing vertex. Validate both the ``3*n + 1`` count arithmetic and allocation-byte arithmetic before allocating.

A zero result can mean no output, invalid input or insufficient capacity. Capacity failure may have written a prefix,
so never draw that prefix as a valid polygon. Clipping can produce a complex path joined by zero-width/height segments
rather than separate output contours.

Clipped centerlines and polygon vertices do not alone constrain the pixels of wide strokes, caps, joins, symbols or
raised borders. Components and renderers must still establish drawing clips; changing this geometry helper is not a
replacement for clipping the rendered primitive.

Simplification and original indices
-----------------------------------

:c:func:`Rbc_SimplifyLine` implements iterative Douglas-Peucker subdivision using a temporary stack. The input interval
is inclusive, and output entries are original indices, not copied points or offsets relative to ``low``. The first and
last points are retained; a single-point interval returns its sole index.

For each interval it finds the greatest perpendicular distance to the infinite line through the endpoints. This differs
from the clamped segment distance used for hit testing. Coincident endpoints instead measure distance from the common
endpoint, preserving excursions that return to their start. It splits only when squared distance is strictly greater
than squared tolerance.

Callers provide finite points and a finite nonnegative tolerance in matching units, plus room for ``high-low+1``
indices. The primitive has no input-length or output-capacity argument and does not validate all numeric values. Its
stack allocation is attempted rather than fatal; failure returns zero. Treat any zero result as unusable output,
including a possible partial prefix from an internal capacity guard.

This helper is not the graph's screen-column extrema decimator. Preserving original indices is useful to callers, but
the helper itself knows nothing about element picking, gaps, vector notifications or renderer batches. Those policies
belong to element/vector code.

See :doc:`geometry-reference` for entry-point and private-helper contracts. This chapter records the existing boundary
and numerical conventions without changing them.

Private validation and subdivision
----------------------------------

The internal helpers are included in :doc:`geometry-reference`. They deliberately have narrower contracts than
command-level validation. :c:func:`IsFinitePoint` tests a pointer and two coordinates; :c:func:`IsValidExtents` accepts
finite ordered bounds, including degenerate rectangles. Neither applies mapping, padding or rounding.

:c:func:`ClipTest` progressively narrows the parametric interval of the line clipper. A single rejected boundary test
leaves its input interval unchanged, but earlier successful boundary tests may already have modified
it. :c:func:`Rbc_LineRectClip` keeps those parameters local and writes output only after all boundaries pass. Do not
interpret that outer guarantee as a general rollback feature of the private helper.

:c:func:`AppendClipVertex` checks only storage and count capacity, then writes one vertex. It neither rejects non-finite
coordinates nor removes duplicates. Successful appends are not rolled back when a later append fails. Consequently, the
caller must discard the entire polygon on the clipper's zero result.

:c:func:`FindSplit` returns squared distance and an original source index. Equal maximum distances retain the first
interior point encountered. Invalid ranges or no interior point return -1, with the output index initialized to -1 when
its pointer is supplied. The helper does not know array capacity or guarantee finite intermediate arithmetic. The outer
simplifier compares squared values, so replacing this with a bounded-segment distance or a different tie rule would
change existing results even if the new code looked mathematically similar.

Output capacity and completion contracts
----------------------------------------

All capacities are numbers of records, not bytes. :c:func:`Rbc_PolyRectClip` checks ``outputCapacity`` on each append
but cannot verify the actual allocation behind that number. Its count includes closure: a caller must not append another
closing point without reserving another slot. :c:func:`Rbc_SimplifyLine` has no capacity argument at all; the caller
must provide room for every input index in the inclusive interval. Neither function returns newly allocated output or
transfers ownership of the input array.

.. list-table:: Interpreting results
   :header-rows: 1
   :widths: 40 60

   * - Function
     - Result and output policy
   * - :c:func:`Rbc_GetClosestPointOnSegment`
     - Returns a distance, using ``DBL_MAX`` for rejected inputs/intermediates. The final distance can overflow after
       the nearest point was written.
   * - :c:func:`Rbc_LineRectClip`
     - Boolean visibility; optional separate output is written only on success.
   * - :c:func:`Rbc_PolyRectClip`
     - Returned vertex count includes closure. Zero means no usable polygon, including capacity failure after partial
       writes.
   * - :c:func:`Rbc_SimplifyLine`
     - Returned count of original indices. Zero means discard the output.
   * - :c:func:`FindSplit`
     - Returns squared distance and writes an original index, not an offset relative to the left endpoint. A negative
       result indicates no split.

These numeric and boolean results are not Tcl statuses: zero is success for ``TCL_OK`` but does not describe a
successful clip or simplification here. The helpers do not set an interpreter result. Command callers translate failure
into their own diagnostics or fallback policy.

Use disjoint input/output arrays unless the specific helper explicitly permits aliasing. In particular, a segment-output
pointer is not an in-place endpoint update interface: retaining separate output preserves the documented input
contract. Geometry clipping also does not establish a renderer clip region; see :doc:`rendering` for stroke, cap and
border containment.
