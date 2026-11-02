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

Parallel parameter ownership and validation
-------------------------------------------

Line and polar elements can retain a real parallel ``-param`` input for source
metadata; strip elements do not expose this option. Parameter data has its own
transaction and notification callback because it does not define plotted
geometry. It can be a named real vector or a list of numeric expressions, using
the common element parser. Complex ``-cdata`` instead requires a named complex
vector and has no list fallback.

PrepareLineParamTransaction processes an initial option-database value unless an
explicit parameter option overrides it. Every explicit occurrence is parsed in
caller order. StageLineParam acquires a new candidate before discarding the old
staged one, so failed parsing does not lose the earlier candidate. Preparation
failure then releases all remaining staged ownership. It does not commit the
candidate or validate its final relationship to the prospective data count.

CommitLineParamTransaction moves the slot to the stable line record before
installing its callback. A parameter notification refreshes borrowed metadata
without resetting axes, remapping geometry or scheduling a redraw. Destruction
clears the client and storage; lookup failure clears potentially stale storage
while retaining the client ID for cleanup. These clear paths leave extrema
unchanged, so length and storage validity must be checked before using values.

The configured/not-configured decision uses the retained option text, not the
source's current length. An empty option disables the parallel mapping. Initial
configuration and explicit changes to parameter or source-data options require
matching lengths. Unrelated style changes remain legal while independent external
vector updates have temporarily produced a mismatch. Validation occurs before
any of the line's prepared transactions commit; it neither frees nor commits the
candidates itself.

Current counts versus cached sample storage
-------------------------------------------

LinePointCount now reads named-vector lengths directly, so activation sees a
resize even before its queued notification runs. It does not refresh cached array
pointers or consume that notification. GetLineDataPoint, by contrast, reads cached
slots. A caller must not use a fresh point count as permission to read an old
array. Source synchronization and mapping remain separate responsibilities.

Configuration validation selects staged slots where supplied and retained live
slots otherwise. ProspectiveLinePointCount and ValidateLineParamLength query
current lengths for named sources, including complex data and parameter vectors;
literal candidates use their stored counts. This makes validation independent of
whether pending resize notifications have run. It neither refreshes borrowed
sample arrays nor delivers or cancels notifications. Real mismatches are still
rejected before commit, and unrelated style changes retain the existing policy
of permitting temporary data/parameter mismatch.

Cartesian X/Y sample access returns source values without a finite check. Polar
X/Y access checks finite theta and radius before converting radians; complex
access delegates to its representation-specific converter. Callers must honor
failure and validate mapped results. Source length, available cached sample and
finite graph coordinate are separate conditions.

Complex mode selection and cleanup
-----------------------------------

PrepareLineComplexDataTransaction tracks both a candidate source and the selected
mode. Explicit ordinary data options select X/Y; a later nonempty complex option
selects complex mode again. Canonical aliases participate in this same sequence.
An invalid earlier complex option still fails even if a later option would switch
away from it. A staged client may therefore need cleanup even when the final mode
is X/Y.

Commit in complex mode transfers any staged source and installs the callback;
without a candidate it retains the live source. Commit in X/Y mode releases both
live and staged complex clients and clears the retained complex option object.
The transaction cleanup functions are safe after commit because moved ownership
is cleared. None of these cleanup paths destroys the application's source vector.
ReplaceLineOptionObject retains a replacement before releasing the previous Tcl
object and treats identical pointers as a no-op.

Complex notifications invalidate axes and mapping and schedule redraw for visible
plotted elements. Parameter notifications deliberately do none of those things.
The difference reflects the role of the data, rather than an interchangeable
implementation of the common X/Y notification policy.
