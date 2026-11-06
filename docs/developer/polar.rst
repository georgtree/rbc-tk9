Polar data and grid geometry
============================

Polar elements reuse the line element implementation in ``rbcGrLine.c``. ``rbcGrPolar.c`` supplies polar/Smith grid
geometry and label presentation. The graph's mapped plane remains Cartesian. Selecting a Smith grid does not itself
convert an element's source values into impedance or admittance. See :doc:`polar-reference` for selected private helper
contracts.

Source representation and conversion
------------------------------------

:c:func:`rbcGrLineData.LinePointCount` selects either the complex-vector length or the shorter X/Y
length. :c:func:`rbcGrLineData.GetLineDataPoint` then supplies Cartesian coordinates to extents, screen mapping and
other consumers. The data mode determines the adapter:

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

``-z0`` is validated as positive and finite. Grid angular tick values use degrees; this must not be confused with the
radians used for polar element input. Axis inversion and screen scaling happen later through :c:func:`Rbc_Map2D`.

The direct Cartesian/gamma adapters can return nonfinite coordinates; success only means that the source sample was
available. Mapping callers still check finite values and domain validity. Complex conversion uses
:c:func:`rbcGrLineConfiguration.ComplexDivide`, which rejects nonfinite operands and zero denominators and checks its
results. It is not an arbitrary-range exact complex arithmetic service. Outputs may have been written before a failed
conversion, so callers must honor its return value.

Complex-vector ownership
------------------------

:c:func:`ParseComplexVectorObj` accepts a named complex RBC vector, with no list fallback. It allocates a client ID,
verifies the vector type and borrows the record and numeric array. A null/empty option produces an empty candidate. It
initializes its destination rather than releasing an existing candidate.

:c:func:`rbcGrLineData.PrepareLineComplexDataTransaction` processes initial defaults and explicit options in
order. Nonempty ``-cdata`` selects complex storage; empty ``-cdata`` or explicit ordinary data options select X/Y
storage. Canonical option names ensure that aliases participate in the same ordering. Staging acquires a new client
before releasing an earlier staged candidate. Failure frees staged resources without replacing the live line data.

Commit transfers the candidate into the stable ``linePtr->z`` member before registering
:c:func:`rbcGrLineData.ComplexVectorChangedProc`. Switching away from complex storage releases its client and clears the
retained ``-cdata`` option. The element does not free the source array or destroy the application vector.

On update, the callback reacquires the record through its client ID and refreshes length, capacity and numeric
pointer. On destruction it clears those borrowed fields and the ID. It requests axis reset/remapping and, for displayed
elements, backing-store redisplay. It does not draw synchronously. See :doc:`vectors` and :doc:`elements` for shared
notification and data-transaction rules.

Polar grid construction
-----------------------

:c:func:`GetPolarRadiusRange` requires linear mapped axes with finite increasing bounds. It finds the minimum distance
from the origin to the data viewport rectangle and the maximum distance to its farthest corner. This supports panned
views where the origin is outside the visible rectangle. A separate helper, :c:func:`PolarCompleteCircleRadius`,
determines a fully contained circle for labels; these two radii serve different purposes.

:c:func:`MapPolarCircles` obtains radial candidates from the mapped X axis's grid segments. It discards nonpositive
radii and candidates outside the visible radial range. Each retained circle is approximated by 180 straight segments,
with Cartesian points mapped through the selected axis pair. Temporary axis segments are freed, and the new array
becomes ``gridPtr->x.segments``.

:c:func:`MapPolarSpokes` reads the global grid's angular tick arrays. Major spokes are always considered; minor spokes
follow the minor-grid setting. :c:func:`ClipPolarRay` intersects each forward ray with the data viewport before mapping
endpoints. The retained spokes become ``gridPtr->y.segments``. Mapping allocates these arrays; the shared grid
lifecycle clears old caches and owns final cleanup. It must not call these mappers over populated arrays without that
cleanup. See :doc:`grids` for grid ownership and renderer clipping of complete strokes.

Smith grid construction
-----------------------

Smith geometry also requires linear Cartesian axes. Resistance/conductance circles and reactance/susceptance arcs are
generated in the Gamma plane and then screen-mapped. The normalized admittance grid negates both coordinates of the
impedance construction, rotating it by 180 degrees.

:c:func:`SmithReactanceGamma` parameterizes the nonnegative real component as ``t/(1-t)``. The endpoint at ``t=1`` is
supplied explicitly instead of dividing by zero: ``(+1,0)`` for impedance and ``(-1,0)`` for admittance. Current grid
resolution uses 180 segments for circles and 90 for reactive arcs. These are implementation sampling choices, not
guarantees of exact analytic curves. The grid conversion helpers operate on selected grid values; they are not the same
validated adapter used for arbitrary element complex-vector input.

Polar and Smith labels have dedicated screen and export routines, including viewport-aware positioning and command-based
formatting. They are separate from segment mapping: the pure geometry helpers documented here do not execute Tcl
callbacks. Label formatting and export still run within the graph lifecycle and export orchestration described in
:doc:`graph-lifecycle` and :doc:`exports`.

Integration boundaries
----------------------

Polar line smoothing uses the parametric path where appropriate, rather than assuming monotonic Cartesian X. Density
decimation excludes polar elements. Ordinary area fills can use origin/chord closure on their mapped geometry; error
bands remain restricted to Cartesian graphs. See :doc:`splines`, :doc:`decimation` and :doc:`fills`.

Closest-point information can include derived normalized and physical Smith quantities. Reverse conversion explicitly
represents the open-circuit gamma point ``(+1,0)`` as infinite impedance and the short-circuit point ``(-1,0)`` as
infinite admittance. These reporting conventions do not change stored input, axis units or the data-mode selection.

Parallel parameter ownership and validation
-------------------------------------------

Line and polar elements can retain a real parallel ``-param`` input for source metadata; strip elements do not expose
this option. Parameter data has its own transaction and notification callback because it does not define plotted
geometry. It can be a named real vector or a list of numeric expressions, using the common element parser. Complex
``-cdata`` instead requires a named complex vector and has no list fallback.

:c:func:`rbcGrLineData.PrepareLineParamTransaction` processes an initial option-database value unless an explicit
parameter option overrides it. Every explicit occurrence is parsed in caller
order. :c:func:`rbcGrLineData.StageLineParam` acquires a new candidate before discarding the old staged one, so failed
parsing does not lose the earlier candidate. Preparation failure then releases all remaining staged ownership. It does
not commit the candidate or validate its final relationship to the prospective data count.

:c:func:`rbcGrLineData.CommitLineParamTransaction` moves the slot to the stable line record before installing
:c:func:`rbcGrLineData.LineParamVectorChangedProc`. A parameter notification refreshes borrowed metadata without
resetting axes, remapping geometry or scheduling a redraw. Destruction clears the client and storage; lookup failure
clears potentially stale storage while retaining the client ID for cleanup. These clear paths leave extrema unchanged,
so length and storage validity must be checked before using values.

:c:func:`rbcGrLineData.LineParamConfigured` uses the retained option text, not the source's current length. An empty
option disables the parallel mapping. Initial configuration and explicit changes to parameter or source-data options,
selected by :c:func:`rbcGrLineData.LineParamLengthValidationNeeded`, require matching lengths. Unrelated style changes
remain legal while independent external vector updates have temporarily produced a mismatch. Validation occurs before
any of the line's prepared transactions commit; it neither frees nor commits the candidates itself.

Current counts versus cached sample storage
-------------------------------------------

:c:func:`rbcGrLineData.LinePointCount` now reads named-vector lengths directly, so activation sees a resize even before
its queued notification runs. It does not refresh cached array pointers or consume that notification.
:c:func:`rbcGrLineData.GetLineDataPoint`, by contrast, reads cached slots. A caller must not use a fresh point count as
permission to read an old array. Source synchronization and mapping remain separate responsibilities.

Configuration validation selects staged slots where supplied and retained live slots
otherwise. :c:func:`rbcGrLineData.ProspectiveLinePointCount` and :c:func:`rbcGrLineData.ValidateLineParamLength` query
current lengths for named sources, including complex data and parameter vectors; literal candidates use their stored
counts. This makes validation independent of whether pending resize notifications have run. It neither refreshes
borrowed sample arrays nor delivers or cancels notifications. Real mismatches are still rejected before commit, and
unrelated style changes retain the existing policy of permitting temporary data/parameter mismatch.

Cartesian X/Y sample access returns source values without a finite check. Polar X/Y access checks finite theta and
radius before converting radians; complex access delegates to its representation-specific converter. Callers must honor
failure and validate mapped results. Source length, available cached sample and finite graph coordinate are separate
conditions.

Complex mode selection and cleanup
-----------------------------------

:c:func:`rbcGrLineData.PrepareLineComplexDataTransaction` tracks both a candidate source and the selected mode. Explicit
ordinary data options select X/Y; a later nonempty complex option selects complex mode again. Canonical aliases
participate in this same sequence. An invalid earlier complex option still fails even if a later option would switch
away from it. A staged client may therefore need cleanup even when the final mode is X/Y.

Commit in complex mode transfers any staged source and installs the callback; without a candidate it retains the live
source. Commit in X/Y mode releases both live and staged complex clients and clears the retained complex option object.
:c:func:`rbcGrLineData.CommitLineComplexDataTransaction` performs those transfers;
:c:func:`rbcGrLineData.FreeLineComplexDataTransaction` and :c:func:`rbcGrLineData.FreeLineParamTransaction` dispose of
abandoned candidates. The transaction cleanup functions are safe after commit because moved ownership is cleared. None
of these cleanup paths destroys the application's source vector. :c:func:`rbcGrLineData.ReplaceLineOptionObject`
retains a replacement before releasing the previous Tcl object and treats identical pointers as a no-op.

Complex notifications invalidate axes and mapping and schedule redraw for visible plotted elements. Parameter
notifications deliberately do none of those things. The difference reflects the role of the data, rather than an
interchangeable implementation of the common X/Y notification policy.

Segment cache construction and failure policy
---------------------------------------------

The mapping entry points consume prepared axes and require old X/Y grid caches to have been released by the shared grid
lifecycle. They do not replace and free an existing cache themselves. Circle and spoke/arc allocations are independent:
a failed allocation can leave one family present and the other absent. Overflow checks and ``Tcl_AttemptAlloc`` failures
return silently, without a Tcl diagnostic. The next normal remapping starts from cleared caches again.

Polar radial candidates are reconstructed from X-axis grid segments using :c:func:`PolarGridSegmentValue`. Inverted
graphs recover the tick from screen Y instead of screen X. Only finite positive radii within the viewport's
minimum/maximum radial distances are retained. Circles are complete sampled polylines; rendering clips their
strokes. Spokes instead intersect their forward data-space ray with the axis rectangle before mapping. A tangential
intersection can produce a zero-length segment; the clipping helpers do not impose a minimum pixel length.

Smith real circles use center r/(r+1) and radius 1/(r+1), with both coordinates negated for admittance. In both-domain
mode the zero-conductance circle is omitted when the impedance unit circle already represents it. Each nonzero reactive
magnitude produces positive and negative arcs per selected domain; the shared zero-reactive real diameter is emitted
once. Global grid settings supply tick arrays and domain selection, while the supplied grid supplies axes and minor-grid
policy. These helpers do not implement independently configured polar track grids.

These normalized grid formulas use direct floating-point squared terms. They are not overflow-resistant replacements for
the complex-vector data adapter. Count checks protect allocation sizes, not arbitrary numeric input to the private
geometry helpers. A successful mapping operation alone is not proof that every resulting coordinate is finite for
extreme values.

Label placement in full and cropped views
-----------------------------------------

.. list-table:: Position and anchor policy
   :header-rows: 1
   :widths: 23 77

   * - Label family
     - Placement
   * - Polar radial
     - The point (radius,0), only when visible. Uses the X-axis style, a two-pixel upper-left offset and the configured
       anchor (automatic is SE).
   * - Polar angular, centered view
     - At 90 percent of the largest fully contained origin-centered circle. Automatic anchor derives from the data
       angle.
   * - Polar angular, cropped view
     - At the far visible endpoint of the clipped forward spoke. Automatic anchor derives from the mapped screen
       direction; invisible rays are skipped.
   * - Smith real
     - At the zero-reactive Gamma coordinate. Zero values extend inward from the unit-circle endpoints; other
       impedance/admittance labels sit two pixels above/below the real axis respectively.
   * - Smith reactive, complete unit circle
     - At 94 percent of the r=0/g=0 Gamma point, with a data-angle anchor.
   * - Smith reactive, cropped unit circle
     - At the first visible entry into the sampled reactive contour, walking from its unit-circle endpoint. The anchor
       follows the reversed local screen direction to extend inward.

The full-circle test is inclusive for the Smith unit circle but requires the origin strictly inside both axis ranges for
a positive polar complete radius. Reactive cropped placement clips the same sampled chords used by rendering, rather
than solving an analytic circle/rectangle intersection. Its accuracy therefore follows the current 90-segment
approximation.

Automatic inward anchors use eight direction sectors separated by 22.5 degrees plus multiples of 45 degrees. Degenerate
screen segments select ``CENTER``. Explicit polar anchors override automatic selection. Centered-view anchor selection
uses data angles; cropped-view selection uses screen directions. These are current policies, not a promise that label
orientation is invariant under axis inversion or reversal. Anchors alone do not guarantee the complete text fits in the
plot.

All label paths copy the axis text style and force zero text rotation. Radial and Smith real labels use X-axis
``showTicks`` / ``style``; angular and Smith reactive labels use Y-axis ``showTicks`` / ``style``. Only major ticks are
labelled. Screen drawing rounds positions before applying fixed offsets; export retains fractional positions. Both paths
release their temporary renderer, labels and tick snapshots.

Formatter callbacks and cancellation
------------------------------------

Angular prefixes receive widget path and numeric degrees, without the default degree suffix. Smith prefixes receive
widget path, impedance/admittance domain and the original signed normalized value, without the default j decoration. A
prefix is duplicated before arguments are appended, and evaluation uses the current Tcl context, matching axis
formatters. A ``NULL`` or empty prefix retains normal formatting. A successful empty result suppresses that label; an
ordinary callback error is reported as a background exception and retains default text. The prior interpreter state is
restored after callback evaluation.

:c:func:`FormatGridLabel` shares the existing ``axisFormatActive`` / ``axisFormatInvalid`` mechanism. While it evaluates
Tcl, recursive snapshot and export requests are rejected before they change dimensions or renderer state. Layout
recursion is likewise rejected; read-only operations that need no layout may still succeed. Redraw requests or relevant
window events during the callback mark the current pass invalid.

After formatting, validity is checked before any cached grid, axis or copied text-style resource is reused. Invalidation
returns ``NULL`` while leaving the caller's DString initialized for cleanup. Each label loop stops and releases its
owned temporary resources; Smith phase/domain dispatch and polar radial/angular dispatch also stop before moving to
another phase. A surviving graph is queued for a fresh redraw. Snapshot/export callers receive the existing ``RBC GRAPH
CALLBACK`` error rather than a completed image/document from stale state. Drawing already performed into an intermediate
target is not rolled back.

This guard does not undo application changes or prevent users from destroying the graph. The enclosing widget command or
display wrapper retains the graph record across Tcl evaluation. Nor does the guard promise forward progress for a
formatter that changes graph options on every call: applications should make such changes outside formatting or only
once, so a later pass can complete.

The existing polar.grid.options tests exercise all three formatter options with graph destruction, replacement tick
arrays and axis font changes through screen snapshots and all three document exporters. They also check recursive
rendering rejection and successful rendering after a cancelled pass. These tests belong to the normal suite; no private
C harness or installed API extension is required.

Geometry parameter domains and output storage
---------------------------------------------

The private geometry helpers consume already normalized grid values and prepared axis state. They are not replacements
for validating arbitrary element data. :c:func:`SmithImpedanceToGamma` and :c:func:`SmithGridValueToGamma` perform no
reference-impedance scaling and use unchecked squared terms. Callers must not interpret their void return as
confirmation of a finite result.

.. list-table:: Coordinate and buffer contracts
   :header-rows: 1
   :widths: 34 66

   * - Helpers
     - Input and output space
   * - :c:func:`ClipPolarRay`
     - Degree angle in, near/far data-space ray endpoints out; output is valid only on ``TRUE``.
   * - :c:func:`MapPolarSpoke`
     - Degree angle in, mapped pixel segment out; it does not independently validate endpoint finiteness.
   * - :c:func:`MapCircle` / :c:func:`MapSmithResistanceCircle`
     - Caller supplies buffers of ``POLAR_CIRCLE_SEGMENTS`` / ``SMITH_CIRCLE_SEGMENTS`` records respectively; outputs
       are mapped pixels.
   * - :c:func:`MapSmithReactanceArc`
     - Caller supplies ``SMITH_ARC_SEGMENTS`` records; normalized reactive input produces mapped pixel chords.
   * - :c:func:`ClipSegmentToGridViewport`
     - Finite data-space endpoints in and clipped data-space endpoints out; neither screen mapping nor stroke clipping occurs.
   * - :c:func:`GetSmithReactiveEdgeAnchor`
     - Expects mapped pixel endpoints, unlike the preceding data-space clipping helper.

The allocating mappers publish owned arrays into the supplied grid cache. :c:func:`Rbc_MapPolarGrid` and
:c:func:`Rbc_MapSmithGrid` require those caches to be empty before entry; the shared grid lifecycle releases old
arrays. Tick selection and Smith domain settings come from the graph's global grid, even when a grid pointer is passed
separately for axes and cache storage.

:c:func:`GetPolarAngularLabelPosition`, :c:func:`GetPolarRadialLabelPosition` and
:c:func:`GetSmithReactiveLabelPosition` expose success-only outputs. A failed call need not preserve the caller's old
point or anchor. Check the return value before using either. Automatic circular anchors use normalized degree angles;
cropped-view anchors derive their direction from mapped segments instead.

Formatter storage and cancellation
-----------------------------------

:c:func:`FormatPolarAngleLabel` and :c:func:`FormatSmithLabel` initialize the supplied DString themselves. Pass storage
that does not already own an allocated buffer. Their common helper, :c:func:`FormatGridLabel`, instead requires an
initialized DString already containing default text. All three return a borrowed pointer into that string, or ``NULL`` when
the drawing pass has been invalidated.

The caller frees the DString on every path, including ``NULL``. An empty non-``NULL`` string means suppress this label and
continue; ``NULL`` means stop the pass before accessing cached grid, axis or copied text-style resources again. The
returned string does not retain any of those resources. The outer graph command or display wrapper must keep the graph
record alive across callback evaluation.

Screen and export loops share this cancellation rule. Their drawable and export context remain borrowed; ending a
temporary text renderer does not free the screen destination or finalize the enclosing document. Geometry sampling
itself does not invoke label commands or establish a callback-preservation scope.
