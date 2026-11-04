Axes, mapping and track viewports
=================================

Mapping converts logical data coordinates into double-precision widget
coordinates. Renderers consume that geometry; they do not choose axis ranges
or track ownership. The private interfaces described here are implemented in
``rbcGrAxis.c`` and ``rbcGrTrack.c``. See :doc:`mapping-reference` for their
contracts and :doc:`rendering` for the later drawing boundary.

Registry ownership and command dispatch are described in :doc:`axes`.

Preparing geometry
------------------

:c:func:`Rbc_LayoutGraph` performs pending work in dependency order:

1. Reset axis ranges when ``RESET_AXES`` is set.
2. Lay out margins when ``LAYOUT_NEEDED`` is set, checking whether a label
   callback invalidated the graph before continuing.
3. When both plot ranges exceed one pixel, map axes when ``MAP_WORLD`` is set,
   then map elements, markers, the grid and local legends.

These stages are distinct from painting. The low-level coordinate helpers do
not run layout on behalf of their callers. Successful layout can leave mapping
pending when the plot is too small. Do not draw or pick stale axis geometry
just because a previous layout existed. See :doc:`graph-lifecycle` for the
callback lifetime rules and the existing :c:func:`Rbc_LayoutGraph` contract.

:c:func:`Rbc_ResetAxes` initializes data limits, asks visible contributing elements
for extents and accumulates those extents into their selected X and Y axes.
Bar frequency and stack state are prepared before collecting extents. Requested
limits, scrolling limits and linear or logarithmic scaling then determine the
axis ranges. The operation requests subsequent geometry, layout and mapping
work; it does not itself produce a complete rendered graph.

``Axis.valueRange`` holds accumulated data bounds. ``Axis.axisRange`` is the
range used by the mapping helpers, including the effects of axis scaling and
limit policy. On a logarithmic axis its mapping coordinates are base-10
exponents. Do not substitute requested limits or raw data bounds for the
prepared mapping range.

Coordinate conventions
----------------------

:c:func:`Rbc_HMap` maps one data value in the horizontal screen direction;
:c:func:`Rbc_VMap` maps one in the vertical direction. They reject nonfinite inputs
and nonpositive logarithmic inputs with ``NAN``. After logarithmic conversion
when required, they normalize through the prepared axis range. Descending
axes reverse the normalized fraction.

For an ordinary finite linear range, the mapping is equivalent to:

.. math::

   u = (v - a_{min}) / (a_{max} - a_{min})

After replacing :math:`u` by :math:`1-u` for descending axes, horizontal mapping
is :math:`x = o_x + u r_x`, while vertical mapping is
:math:`y = o_y + (1-u)r_y`. The vertical reversal accounts for screen Y
increasing downward. The implementation also handles ranges whose subtraction
overflows using half-scaled arithmetic; avoid duplicating the simple formula
in a component and losing that protection.

:c:func:`Rbc_Map2D` chooses which helper receives each logical axis:

.. list-table::
   :header-rows: 1

   * - Orientation
     - Screen X
     - Screen Y
   * - Ordinary
     - X data through the X axis
     - Y data through the Y axis
   * - Inverted
     - Y data through the Y axis
     - X data through the X axis

Inversion does not exchange the logical identity of the axes.
:c:func:`Rbc_InvMap2D` reverses this mapping and returns logical X/Y values. It
extrapolates outside plot bounds rather than clipping them. Its logarithmic
conversion checks the supported ``DBL_MIN`` to ``DBL_MAX`` interval and may
return ``NAN``. Consumers must check finiteness before converting coordinates
to integers or passing geometry to drawing code.

Mapping is not clipping, rounding or picking. A finite mapped point can be
outside the plot, and different renderers retain their own integer conversion
rules. Elements retain original data indices separately so reduced drawing
geometry does not replace the identity used by closest-point queries.

Full tracks and visible tracks
------------------------------

Each track has two rectangles, both expressed in widget-coordinate space:

.. list-table::
   :header-rows: 1

   * - Fields
     - Purpose
   * - ``worldLeft``, ``worldRight``, ``worldTop``, ``worldBottom``
     - Full track extent after layout and scrolling, possibly outside the plot.
   * - ``left``, ``right``, ``top``, ``bottom``
     - Intersection of that extent with the graph's plot viewport.

:c:func:`Rbc_LayoutTracks` distributes fitted tracks using minima and weights.
With track scrolling enabled, requested sizes and minima establish the full
extent and the scroll offset positions it within the viewport. Inverted
graphs stack tracks horizontally. Track-owned axes map against the full world
extent, not the visible intersection. Otherwise scrolling a track partly out
of view would rescale its data rather than simply hide the displaced portion.
Shared axes use the graph's mapping offsets and ranges.

Axis ownership is sufficient for coordinate transforms. Component operations
that depend on graph-level clipping, reduction or drawing fields additionally
use a paired viewport scope:

.. code-block:: c

   RbcTrackViewport saved;

   Rbc_EnterTrack(graphPtr, track, &saved);
   /* Work with this track's viewport. */
   Rbc_LeaveTrack(graphPtr, &saved);

Entry saves both directions but changes only the stacking direction's bounds,
scale and padding, along with ``activeTrack``. Visible bounds supply clipping
limits while full world bounds supply offsets and ranges. Entry with a NULL
track still changes the active-track state and must be paired with leave.
Nested scopes need separate saved records and restoration in reverse order.
These helpers neither install a renderer clip nor preserve object lifetime;
cleanup on every return path remains the caller's responsibility.

During axis mapping, ``Rbc_MapAxes`` first lays out tracks, then walks margin
axes. Track axes accumulate their own axis offsets, allowing multiple local
axes. Completely clipped tracks are skipped. Axis geometry uses the full track
rectangle so its placement remains stable while scrolling; visible bounds
still govern later clipping.

Temporary export layouts
------------------------

Document export can change graph dimensions and remap the same component
records. ``GRAPH_EXPORT`` prevents temporary layout from being announced as
an ordinary screen view change. Track layout also avoids committing its
export-clamped offset to the stored screen scroll position.

SVG and PDF use ``OutputGraphExport`` in ``rbcGrExport.c``. It saves screen
dimensions, lays out the requested export size, generates the document, then
restores dimensions and requests another layout. Immediate screen remapping is
attempted only while the window exists and axis formatting has not invalidated
it. A prior export error is preserved across that restoration attempt; a
restoration error can fail an otherwise successful export.

PostScript retains its separate preamble and document orchestration in
``rbcGrPs.c``. The shared ``Rbc_ExportEndGraph`` clears export state, returns to
window dimensions and schedules remapping/redraw when the window still exists.
Do not assume this helper alone synchronously restores every geometry cache.
New export paths must account for failure and callback-driven destruction as
well as successful output.

Useful regression coverage
--------------------------

Mapping changes should exercise ordinary and inverted layouts, descending and
logarithmic axes, extreme finite ranges, tiny plot sizes and multiple axes.
Track tests should include partial scrolling, reordering, local-axis transforms
and independent renderer clipping. Export tests need resized output followed
by restored screen transforms and geometry. A visually plausible image alone
does not prove that picking, inverse transforms and cached layout agree.

Axis-space helper contracts
---------------------------

The public mapping entry points accept data values, but the numerical helpers
beneath them operate in **axis space**. For a logarithmic axis that means
base-ten exponents. Passing raw logarithmic data to an axis-space helper skips
a required conversion; applying a logarithm twice is equally incorrect.

.. list-table:: Mapping stages
   :header-rows: 1
   :widths: 38 62

   * - Helper
     - Input and responsibility
   * - :c:func:`GetDataLimits <rbcGrAxisRanges.GetDataLimits>`
     - Accumulates raw element bounds into ``valueRange`` endpoints only.
       It does not refresh cached range/scale or apply requested limits.
   * - :c:func:`SetAxisRange <rbcGrAxisRanges.SetAxisRange>`
     - Stores axis-space endpoints and cached range/scale without sorting or
       validating them; tiny spans use a unit cached range.
   * - :c:func:`NormalizeAxisValue <rbcGrAxisRanges.NormalizeAxisValue>`
     - Converts axis-space values to normalized fractions, without direction
       reversal, logarithms or clipping.
   * - :c:func:`MapHorizontalAxisCoordinate <rbcGrAxisRanges.MapHorizontalAxisCoordinate>` and
       :c:func:`MapVerticalAxisCoordinate <rbcGrAxisRanges.MapVerticalAxisCoordinate>`
     - Apply axis direction and graph/track world geometry to already-converted
       values. The vertical helper also accounts for downward screen Y.
   * - :c:func:`InvHMap <rbcGrAxisRanges.InvHMap>` and
       :c:func:`InvVMap <rbcGrAxisRanges.InvVMap>`
     - Reverse screen scaling and direction, interpolate axis space, then
       convert logarithmic exponents back to raw data.

:c:func:`InterpolateAxisValue <rbcGrAxisRanges.InterpolateAxisValue>` accepts
fractions outside ``[0,1]``; it extrapolates rather than clips.
:c:func:`InRange <rbcGrAxisRanges.InRange>` is the separate finite-range test
with its small normalized tolerance.
:c:func:`AxisIsHorizontal <rbcGrAxisRanges.AxisIsHorizontal>` combines logical
axis identity with inversion; it does not change ownership or the axis class.

:c:func:`LogAxisValue <rbcGrAxisRanges.LogAxisValue>` supports normal positive
doubles from ``DBL_MIN`` through ``DBL_MAX`` and rejects exponents outside that
interval with ``NAN``. It deliberately does not promise subnormal results.
:c:func:`MakeLogRange <rbcGrAxisRanges.MakeLogRange>` accepts a raw data value
and writes two raw positive limits; it is not an exponent-output routine.

:c:func:`AdjustViewport <rbcGrAxisLayoutControl.AdjustViewport>` operates on
a normalized offset and view/world size ratio. When the view exceeds the world,
its allowed offsets differ from the usual ``0..1-windowSize`` interval. Callers
establish finite valid scroll state first; this helper does not validate axes
or refresh geometry.

All these routines borrow their graph/axis records and perform no Tcl evaluation.
Their low-level nature does not make stale layout safe: callers must prepare
layout first, check nonfinite results before integer conversion, then apply
geometry and renderer clipping at their respective boundaries.
