Spline interpolation and graph smoothing
========================================

``rbcSpline.c`` contains array-based interpolation kernels and the Tcl spline
command. ``rbcGrLine.c`` uses those kernels for mapped trace smoothing. The
kernels never notify vectors or invoke Tcl code; the command layer owns vector
lookup, creation, resizing and notification. See :doc:`spline-reference`.

Coordinate and parameter interfaces
-----------------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Routine family
     - Input/output convention
   * - Natural/quadratic Cartesian
     - Finite source points with strictly increasing X. Query X values may be
       unsorted; only query Y is replaced. At least three source points.
   * - Natural/quadratic parametric Eval
     - Finite path points with no monotonic-X requirement. Each query is an
       interval number in X and a fraction in Y; both become output coordinates.
   * - Natural parametric sampler
     - Generates a requested number of points, using extents to scale the chord
       metric. Supports open and closed curves; returns a count rather than boolean.
   * - Catmull-Rom parametric
     - Uses interval/fraction queries and repeated endpoint control points.
       Requires at least one source point; does not use a chord-length metric.

For interval/fraction queries, the interval must be integral and within the
source range, and the fraction must be between zero and one. The final knot is
represented specially by the last source index with fraction zero. Invalid
parameters are rejected before writing output.

Cartesian kernels
-----------------

The natural cubic kernel builds a tridiagonal system with natural endpoint
conditions, then back-substitutes interval coefficients. It checks interval
widths, divisions and intermediate coefficients for finite arithmetic. The
quadratic kernel estimates knot slopes, selects a shape-preserving case for
each interval and reuses that case while successive queries remain in the
same interval. Both use binary search for in-domain queries, preserve source
knots exactly and extrapolate using the first or final interval outside the
domain. Neither requires query abscissas to be sorted.

Both kernels accept zero queries but still require a non-NULL query pointer
and valid source data. They allocate checked private workspace and release it
before returning. Their boolean failure result can follow writes to some query
Y values; callers needing an unchanged destination must evaluate into scratch
storage and discard that storage on failure.

Parametric paths
----------------

The natural and quadratic Eval routines use Euclidean chord distances in the
supplied coordinate system. For graph smoothing these are already screen
coordinates, so the metric is isotropic in pixels. Consecutive degenerate
segments and non-finite or non-increasing accumulated lengths are rejected.
The quadratic path constructs independent coordinate splines on the same
parameter; the natural path uses cubic coefficients. Exact source-knot queries
are copied from the source. Both Eval routines stage complete coordinates and
copy back only on success, preserving input parameters on failure.

The uniform natural sampler is a different entry point. Its public wrapper
uses absolute extent spans as metric scales and clamps tiny or non-finite spans
to FLT_EPSILON. Closed curves append the first point privately when necessary.
The underlying sampler slightly undershoots the total parameter length to avoid
losing the final sample to rounding, then restores the endpoint exactly when
the requested count is generated. Callers must check that count. Sampling in
chord parameter is not exact equal-distance sampling along the curved result.

The Tcl complex-vector command uses the natural core with unit scales and an
open curve, or the quadratic core sampled uniformly over its common chord
parameter. It requires at least three source points and at least two output
samples. It rejects consecutive duplicate points before computation; numerical
checks inside the kernels can reject additional nearly degenerate cases.

Catmull-Rom copies the source and repeats endpoints for its local cubic control
points. It validates all query parameters before evaluation, but arithmetic can
still become non-finite after some outputs have been written. Its failure
contract therefore differs from the two staged parametric Eval routines.

Tcl vector ownership
--------------------

The Cartesian command reads real X, Y and query-X vectors. The parametric
command reads one complex vector and writes a complex result. Both copy all
inputs into private Point2D arrays and generate the complete result before
looking up, creating or resizing the output. Output aliasing an input is safe
for numerical computation because source storage is no longer needed then.

An existing output must have the required type and be writable. A missing output
is created without an array mapping initially. ``SplineStoreResult`` allocates
and fills the final numeric buffer before changing the destination, then installs
its pointer, length and capacity together. No resize notification exposes an
intermediate output. Source data, output generation and allocation failures
leave an existing destination unchanged.

The commit preserves the destination record and interpreter across old-storage
release, mapped-array traces and notification. It checks destruction after each
callback boundary and never resolves the output name again. A callback-created
replacement therefore remains untouched. Callbacks may rename or destroy the
original, or change its values; their side effects are not rolled back.

New real vectors retain automatic array mapping unless their names contain
parentheses. Mapping happens after the final values are installed. Mapping
preserves the record, owns a copy of the requested name and checks for deletion
or reentrant remapping after traces. Mapping failure reports an error and removes
the newly created vector if it still exists, without removing a replacement.
Existing mappings are flushed when enabled. Surviving destinations receive one
completed-value update rather than a resize update followed by a value update.
See :doc:`vectors` for immediate versus deferred notification policies.

Graph smoothing
---------------

Graph smoothing consumes mapped points and builds replacement point/index arrays.
For Cartesian natural/quadratic smoothing it chooses the screen coordinate
corresponding to data X: screen X normally, screen Y when inverted. Decreasing
screen abscissas are reflected without reversing source order. Generated
coordinates are converted back after successful interpolation.

Parametric smoothing builds interval/fraction queries and dispatches to natural,
quadratic or Catmull-Rom evaluation. Natural/quadratic polar smoothing uses the
parametric path. Trace-run handling keeps separate runs from being joined by a
spline. Generated samples retain source-index information for later graph work.

Replacement geometry is committed only when interpolation succeeds. Failure
retains the original mapped points and falls back to ordinary line segments.
Smoothing does not edit the attached source vectors. Mapping, clipping and
rendering remain separate responsibilities; see :doc:`mapping` and :doc:`elements`.

Private helper contracts
-------------------------

The :doc:`spline-reference` now includes the internal validation, quadratic,
cubic and command helpers. These are separate numerical and ownership layers;
none of the interpolation kernels resolves vector names or invokes Tcl code.

The byte-count helper accepts zero-length arrays and rejects overflow without
setting an interpreter message. Its callers decide whether zero queries are
valid and whether to allocate a non-NULL sentinel. Source validation checks
both coordinates; Cartesian query validation checks only X because Y will be
replaced. These checks are intentionally different.

Quadratic preparation runs ``QuadSlopes`` once, then ``QuadSelect`` combines
case selection and ten-parameter generation for each needed source interval.
``QuadEval`` caches only the most recently selected interval. Arbitrarily
ordered queries remain supported, but switching intervals rebuilds that small
parameter set. Its status is not boolean: 0 means interpolation only, 1 means
at least one extrapolation, and 2 means an internal lookup failure. The public
wrapper accepts 0 and 1 and performs a final finite-coordinate check.

The historical parametric cubic construction differs from the Cartesian
natural cubic. ``CubicSlopes`` limits the scaled right-hand-side norm to 8.5
before solving; for an open path it copies the adjacent solved second
derivatives to the endpoints. It does **not** impose the zero endpoint second
derivatives used by ``Rbc_NaturalSpline``. The existing natural-parametric
operation name is retained, but should not be taken as a promise that these
two kernels generate identical curves. Changing this construction would change
existing smoothing and interpolation results.

``SolveCubic1`` overwrites a symmetric cyclic tridiagonal matrix with factors.
``SolveCubic2`` solves the X and Y right-hand sides together using those factors;
it does not validate its results. ``CubicSlopes`` checks the solved derivatives
and owns both coefficient and matrix allocations. Only the first N-1 ``t``
entries of an N-point coefficient array are interval lengths.

Failure contracts must also stay distinct:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Helper or entry point
     - Output on failure
   * - Cartesian natural/quadratic kernels
     - Some query Y values may already have changed
   * - Natural/quadratic parametric Eval
     - Caller query parameters remain unchanged; coordinates are staged
   * - ``CubicEval`` and uniform parametric cores
     - Partial coordinates or temporary query parameters may remain; compare
       returned count with requested count
   * - Catmull-Rom Eval
     - Parameters validate before writes, but arithmetic failure can leave
       partial output
   * - Tcl spline command
     - Numerical failure discards private arrays before destination commit;
       postcommit callback changes are not rolled back

Direct numerical callers should use disjoint source and output arrays unless
an entry point explicitly guarantees otherwise. The Tcl command supports vector
aliasing by copying every input first; that command-level guarantee does not
make each private array kernel safe for in-place use.
