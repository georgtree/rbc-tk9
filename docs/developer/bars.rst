Bar grouping and placement
==========================

``rbcGrBar.c`` implements bar elements through the shared ``ElementProcs``
interface. Source data and axis transactions follow :doc:`elements`; screen
mapping uses the axis/track scope described in :doc:`mapping`. The routines in
:doc:`bar-reference` share placement rules between axis-limit calculation and
mapping. They do not modify source vectors or call Tcl scripts.

Grouping keys and passes
------------------------

``Rbc_InitFreqTable`` rebuilds graph-owned duplicate-X metadata from displayed,
non-hidden, non-plot-hidden bar elements. It considers paired X/Y samples,
rejecting nonfinite pairs and nonpositive Y on logarithmic Y axes. A key contains
the original X and both axis pointers; signed zero is normalized. Offsets,
alignment, width and pen identity do not form part of the key. Repeated X values
inside one element can therefore participate in a group as well.

Only duplicate keys receive persistent ``FreqInfo`` records. ``freqArr`` owns
these records; hash values borrow pointers into it. ``nStacks`` counts duplicate
keys, not just groups currently drawn in stacked mode. Ordinary normal mode
needs no table. Trackgraphs still build one because individual tracks may select
another mode. The owning Y axis supplies a track override when configured;
otherwise the graph mode applies. Track-owned axis identities isolate groups.

Axis reset initializes this table, computes stack totals where needed, and
resets placement counters before visiting element extents. Element mapping
resets counters again before its ordered pass. ``Rbc_ResetStacks`` clears
``count`` and ``lastY``, retaining frequency and precomputed sums. A new consumer
must not call ``PlaceBar`` as an independent query: it advances these counters.
Skipping or reordering calls can change later bars' placement.

Placement formulas
------------------

Let ``x`` and ``y`` be the source values, ``o`` the element offset and ``W`` its
positive configured width, falling back to the graph width. The full interval
starts at ``L = x + o - a``, where ``a`` is zero for left alignment, ``W/2`` for
center alignment and ``W`` for right alignment. Alignment names refer to data-X
edges, including when axes are inverted or descending.

For a duplicate group of size ``n``, let ``k`` be its one-based placement count
in the traversal. The formulas are:

.. list-table::
   :header-rows: 1

   * - Mode
     - Data-X interval
     - Data-Y edges
   * - Normal (internal ``MODE_INFRONT``)
     - ``[L, L + W]``
     - Baseline to ``y``
   * - Aligned
     - ``[L + (n-k) W/n, L + (n-k+1) W/n]``
     - Baseline to ``y``
   * - Overlap
     - Start ``L + (n-k) s``, width ``(n+1) s``, with ``s = W/(2n)``
     - Baseline to ``y``
   * - Stacked
     - ``[L, L + W]``
     - Previous cumulative value to previous cumulative value plus ``y``

A key without duplicates uses the full interval and baseline. Each member uses
its own ``W``, ``a`` and ``o``. Mixed settings do not first produce one shared
physical interval: they apply a shared slot count to independently anchored
intervals. For example, with ``n=2``, ``x=10``, ``W=4`` and zero offset, a
left-aligned first member occupies ``[12,14]`` in aligned mode; a right-aligned
second member occupies ``[6,8]``. With both centered the slots are ``[10,12]``
and ``[8,10]``. Different anchors can separate, overlap or reverse the apparent
order of the slots without changing group membership.

Stack accumulation starts at zero and uses one signed running total, rather
than separate positive and negative stacks. A nonfinite cumulative result is
rejected without updating that total. Logarithmic Y rejects nonpositive source
values; its first visible stack segment uses the effective baseline when the
previous total is not positive. The effective baseline is the configured
baseline, except that an unusable nonpositive log baseline becomes one.

Data limits and error translations
----------------------------------

``GetBarExtents`` uses full anchored X intervals, source Y extrema, baseline
rules and ``CheckStacks`` totals. Those totals are final signed sums computed
separately by ``Rbc_ComputeStacks``; they are not a list of every intermediate
stack endpoint. Consequently this is not a tight union of all final screen
rectangles. Linear Y retains the historical zero-inclusive lower-bound rule.
Log X bounds use valid positive full intervals.

Extents also run placement without viewport rejection so that error-bar
translations follow group order. In ``-errorbarposition bar`` mode,
``SetBarErrorOffset`` records, per original source index::

    dx = (placedLeft + placedRight) / 2 - sourceX
    dy = placedValueY - sourceY

The implementation averages the two X endpoints as half of each, avoiding an
unnecessary intermediate sum. Offsets are computed before downward bars swap
corners and before axis mapping. Symmetric errors and explicit low/high bounds
receive the same data-space translation. Thus logarithmic axes apply their
transform after translation, rather than translating an already mapped error
segment by a fixed pixel distance.

In ``data`` mode errors retain source positioning plus the element's X offset;
they do not follow alignment, slot subdivision or stack displacement. The shared
error routines expand limits and map segments consistently with that choice.
Temporary per-point offset arrays start with NaN entries: rejected placements
must not acquire an apparently valid zero translation. The array is freed after
error processing and is not stored in the element.

Screen geometry, ownership and output
-------------------------------------

``MapBar`` first releases previous mapped storage. It calls ``PlaceBar`` in
source order with viewport rejection enabled, saves any error translations,
swaps downward Y edges, and maps both corners. Nonfinite results are skipped.
Coordinates are bounded by the graph window before integer conversion. Width
and height retain the historical rounding and inclusive extra pixel; changing
these rules changes native output and picking geometry.

Window-coordinate bounds do not replace plot clipping. Drawing a bar inside a
track requires the renderer clip covering the complete element, including
raised borders. See :doc:`rendering` and :doc:`elements` for that outer scope.

The bar owns its rectangle array and ``rectToData`` source-index map, plus
separate arrays for explicitly active rectangles. Shared element fields own
mapped error segments and their source-index maps. ``MergePens`` can reorder
rectangles and errors into pen groups, keeping the corresponding index maps in
step. Style slices borrow the resulting storage. ``ResetBar`` clears palette
views, frees mapped arrays and zeros pointers/counts; source vectors remain
owned by their original creators.

Normal drawing batches rectangles through ``Rbc_RenderBeginBars`` and
``Rbc_RenderFillRectangles``. Export has its own callbacks consuming mapped bar
geometry and pen styles. Value labels and legend samples are separate drawing
paths. A legend sample outside the plot is not evidence that a bar escaped its
clip.

``ClosestBar`` measures distance to mapped rectangles, then returns the original
source X/Y and index through ``rectToData``. A stacked hit therefore reports the
sample contribution, not the cumulative top. ``Rbc_GetBarRectangle`` supplies
mapped bounds when a caller needs the actual placed rectangle instead.
