Geometry, clipping and simplification
=====================================

``rbcGeometry.c`` implements point, segment and polygon operations without Tk
resources, renderer state or graph ownership. Its private declarations remain
in ``rbcInt.h``. Callers map coordinates first, select a coordinate system and
provide the arrays. See :doc:`mapping` for graph/track transformations and
:doc:`rendering` for actual drawing clips.

Coordinates and borrowed storage
--------------------------------

``Point2D`` and ``Segment2D`` carry double coordinates. ``Extents2D`` uses
left/right/top/bottom bounds; valid rectangles have finite coordinates with
left <= right and top <= bottom. Zero-width or zero-height rectangles are not
rejected. In screen coordinates, top normally has the smaller Y value.

The helpers do not apply axis transforms, logarithms, track offsets or native
pixel rounding. Distances and tolerances use the same units as their input.
Arrays are borrowed. Counts must describe allocated storage: a non-NULL pointer
and a positive count do not prove an array is large enough.

These are numerical primitives with sentinel/count results, not Tcl command
error interfaces. Finite-input checks reject many malformed cases, but they are
not a guarantee against every intermediate overflow at extreme magnitudes.
Command-facing callers retain validation and diagnostic responsibilities.

Hit testing
-----------

``Rbc_GetClosestPointOnSegment`` projects onto the segment direction and clamps
to its endpoints. A segment whose length is at most ``DBL_EPSILON`` uses its
first endpoint. It writes the closest point and returns Euclidean distance.
Rejected inputs/intermediates return ``DBL_MAX`` without writing the result;
the final distance calculation can still overflow for extreme finite values.

``Rbc_PointInSegments`` accepts a finite positive halo and tests ``distance <
halo``, not <=. An exact hit with halo zero therefore does not match.
``Rbc_PointInPolygon`` uses even-odd ray crossings and closes the last vertex
back to the first. Its half-open edge comparisons avoid double-counting vertices,
but do not establish a universal inclusive-boundary policy. Use a separate
segment-distance test when a boundary tolerance is required.

Despite its name, ``Rbc_RegionInPolygon`` with ``enclosed`` true checks that the
rectangle encloses every polygon vertex. With ``enclosed`` false it tests all
polygon edges against the rectangle, then tests the rectangle's top-left corner
inside the polygon. That final check handles a rectangle wholly inside a polygon
without any edge crossing.

Line and polygon clipping differ
--------------------------------

``Rbc_LineRectClip`` uses Liang-Barsky clipping against the supplied bounds.
Its output pointer may be NULL for visibility testing. With separate output
storage, endpoints remain unchanged; rejected input leaves output untouched.

``Rbc_PolyRectClip`` preserves a historical pixel-area convention: left/top are
used directly, while right/bottom are expanded by one coordinate unit. It also
perturbs zero edge deltas by ``DBL_EPSILON`` internally. It is therefore not an
interchangeable polygon version of the line clipper for arbitrary mathematical
coordinates.

The input polygon needs no duplicated closing vertex. Output is explicitly
closed and its count includes the repeated first vertex. The caller supplies a
separate buffer and capacity; every append checks capacity, including the final
closing point. A conservative bound for the current implementation is three
output vertices per input edge plus one closing vertex. Validate both the
``3*n + 1`` count arithmetic and allocation-byte arithmetic before allocating.

A zero result can mean no output, invalid input or insufficient capacity.
Capacity failure may have written a prefix, so never draw that prefix as a valid
polygon. Clipping can produce a complex path joined by zero-width/height
segments rather than separate output contours.

Clipped centerlines and polygon vertices do not alone constrain the pixels of
wide strokes, caps, joins, symbols or raised borders. Components and renderers
must still establish drawing clips; changing this geometry helper is not a
replacement for clipping the rendered primitive.

Simplification and original indices
-----------------------------------

``Rbc_SimplifyLine`` implements iterative Douglas-Peucker subdivision using a
temporary stack. The input interval is inclusive, and output entries are original
indices, not copied points or offsets relative to ``low``. The first and last
points are retained; a single-point interval returns its sole index.

For each interval it finds the greatest perpendicular distance to the infinite
line through the endpoints. This differs from the clamped segment distance used
for hit testing. Coincident endpoints instead measure distance from the common
endpoint, preserving excursions that return to their start. It splits only when
squared distance is strictly greater than squared tolerance.

Callers provide finite points and a finite nonnegative tolerance in matching
units, plus room for ``high-low+1`` indices. The primitive has no input-length or
output-capacity argument and does not validate all numeric values. Its stack
allocation is attempted rather than fatal; failure returns zero. Treat any zero
result as unusable output, including a possible partial prefix from an internal
capacity guard.

This helper is not the graph's screen-column extrema decimator. Preserving
original indices is useful to callers, but the helper itself knows nothing about
element picking, gaps, vector notifications or renderer batches. Those policies
belong to element/vector code.

See :doc:`geometry-reference` for the seven function contracts. This chapter
records the existing boundary and numerical conventions without changing them.
