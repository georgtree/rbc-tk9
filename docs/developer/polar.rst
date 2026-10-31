Polar data and grid geometry
============================

Polar elements reuse the line element implementation in ``rbcGrLine.c``.
``rbcGrPolar.c`` supplies polar/Smith grid geometry and label presentation.
The graph's mapped plane remains Cartesian. Selecting a Smith grid does not
itself convert an element's source values into impedance or admittance.
See :doc:`polar-reference` for selected private helper contracts.

Source representation and conversion
------------------------------------

``LinePointCount`` selects either the complex-vector length or the shorter X/Y
length. ``GetLineDataPoint`` then supplies Cartesian coordinates to extents,
screen mapping and other consumers. The data mode determines the adapter:

.. list-table::
   :header-rows: 1

   * - Input
     - Graph-plane coordinates
   * - Cartesian X/Y
     - Source X and Y directly
   * - Polar X/Y
     - ``radius*cos(theta)``, ``radius*sin(theta)``; theta is in radians
   * - Complex gamma
     - Real and imaginary parts directly
   * - Complex impedance Z
     - Normalize ``z = Z/Z0``, then ``Gamma = (z-1)/(z+1)``
   * - Complex admittance Y
     - Normalize ``y = Y*Z0``, then ``Gamma = (1-y)/(1+y)``

``-z0`` is validated as positive and finite. Grid angular tick values use degrees;
this must not be confused with the radians used for polar element input.
Axis inversion and screen scaling happen later through ``Rbc_Map2D``.

The direct Cartesian/gamma adapters can return nonfinite coordinates; success
only means that the source sample was available. Mapping callers still check
finite values and domain validity. Complex conversion uses ``ComplexDivide``,
which rejects nonfinite operands and zero denominators and checks its results.
It is not an arbitrary-range exact complex arithmetic service. Outputs may have
been written before a failed conversion, so callers must honor its return value.

Complex-vector ownership
------------------------

``ParseComplexVectorObj`` accepts a named complex RBC vector, with no list
fallback. It allocates a client ID, verifies the vector type and borrows the
record and numeric array. A null/empty option produces an empty candidate.
It initializes its destination rather than releasing an existing candidate.

``PrepareLineComplexDataTransaction`` processes initial defaults and explicit
options in order. Nonempty ``-cdata`` selects complex storage; empty ``-cdata``
or explicit ordinary data options select X/Y storage. Canonical option names
ensure that aliases participate in the same ordering. Staging acquires a new
client before releasing an earlier staged candidate. Failure frees staged
resources without replacing the live line data.

Commit transfers the candidate into the stable ``linePtr->z`` member before
registering ``ComplexVectorChangedProc``. Switching away from complex storage
releases its client and clears the retained ``-cdata`` option. The element does
not free the source array or destroy the application vector.

On update, the callback reacquires the record through its client ID and refreshes
length, capacity and numeric pointer. On destruction it clears those borrowed
fields and the ID. It requests axis reset/remapping and, for displayed elements,
backing-store redisplay. It does not draw synchronously. See :doc:`vectors` and
:doc:`elements` for shared notification and data-transaction rules.

Polar grid construction
-----------------------

``GetPolarRadiusRange`` requires linear mapped axes with finite increasing
bounds. It finds the minimum distance from the origin to the data viewport
rectangle and the maximum distance to its farthest corner. This supports panned
views where the origin is outside the visible rectangle. A separate helper,
``PolarCompleteCircleRadius``, determines a fully contained circle for labels;
these two radii serve different purposes.

``MapPolarCircles`` obtains radial candidates from the mapped X axis's grid
segments. It discards nonpositive radii and candidates outside the visible
radial range. Each retained circle is approximated by 180 straight segments,
with Cartesian points mapped through the selected axis pair. Temporary axis
segments are freed, and the new array becomes ``gridPtr->x.segments``.

``MapPolarSpokes`` reads the global grid's angular tick arrays. Major spokes are
always considered; minor spokes follow the minor-grid setting. ``ClipPolarRay``
intersects each forward ray with the data viewport before mapping endpoints.
The retained spokes become ``gridPtr->y.segments``. Mapping allocates these
arrays; the shared grid lifecycle clears old caches and owns final cleanup.
It must not call these mappers over populated arrays without that cleanup.
See :doc:`grids` for grid ownership and renderer clipping of complete strokes.

Smith grid construction
-----------------------

Smith geometry also requires linear Cartesian axes. Resistance/conductance
circles and reactance/susceptance arcs are generated in the Gamma plane and
then screen-mapped. The normalized admittance grid negates both coordinates
of the impedance construction, rotating it by 180 degrees.

``SmithReactanceGamma`` parameterizes the nonnegative real component as
``t/(1-t)``. The endpoint at ``t=1`` is supplied explicitly instead of dividing
by zero: ``(+1,0)`` for impedance and ``(-1,0)`` for admittance. Current grid
resolution uses 180 segments for circles and 90 for reactive arcs. These are
implementation sampling choices, not guarantees of exact analytic curves.
The grid conversion helpers operate on selected grid values; they are not the
same validated adapter used for arbitrary element complex-vector input.

Polar and Smith labels have dedicated screen and export routines, including
viewport-aware positioning and command-based formatting. They are separate
from segment mapping: the pure geometry helpers documented here do not execute
Tcl callbacks. Label formatting and export still run within the graph lifecycle
and export orchestration described in :doc:`graph-lifecycle` and :doc:`exports`.

Integration boundaries
----------------------

Polar line smoothing uses the parametric path where appropriate, rather than
assuming monotonic Cartesian X. Density decimation excludes polar elements.
Ordinary area fills can use origin/chord closure on their mapped geometry;
error bands remain restricted to Cartesian graphs. See :doc:`splines`,
:doc:`decimation` and :doc:`fills`.

Closest-point information can include derived normalized and physical Smith
quantities. Reverse conversion explicitly represents the open-circuit gamma
point ``(+1,0)`` as infinite impedance and the short-circuit point ``(-1,0)`` as
infinite admittance. These reporting conventions do not change stored input,
axis units or the data-mode selection.
