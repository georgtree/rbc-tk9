Bar elements
============

``rbcGrBar.c`` implements bar elements through the shared ``ElementProcs``
interface. Source data and axis transactions follow :doc:`elements`; screen
mapping uses the axis/track scope described in :doc:`mapping`. The contracts in
:doc:`bar-reference` cover the concrete module from construction through
cleanup. Placement and mapping borrow source arrays without running Tcl
scripts; configuration may evaluate data expressions, and value labels may
invoke application commands. Those callback boundaries need separate lifetime
review.

Records and ownership
---------------------

``Bar`` embeds ``Element`` first and ``BarPen`` embeds ``Pen`` first. The
conversion and offset macros preserve that common-header layout; a static
assertion checks the element header offset. Constructors return common-header
pointers, not independently allocated header objects. ``BarRectangle`` aliases
the renderer's rectangle type.

.. list-table:: Storage and release responsibilities
   :header-rows: 1
   :widths: 25 45 30

   * - Record or resource
     - Ownership
     - Release path
   * - Concrete bar and its copied name/label
     - Shared element lifecycle owns the allocation returned by :c:func:`rbcGrBarConfiguration.Rbc_BarElement`
     - Shared element teardown, after the concrete destructor
   * - Embedded normal pen
     - Storage belongs to the bar; it is not a named-pen allocation
     - :c:func:`rbcGrBarConfiguration.DestroyPen` for derived resources; element options for direct Tk resources
   * - Named normal, active and palette pens
     - References acquired through shared pen/element transactions
     - Reference release through the pen manager or palette cleanup
   * - Option objects, fonts, borders and direct colors
     - Tk option tables manage configured fields
     - Shared Tk option teardown
   * - Derived error color, shadow and GCs
     - :c:func:`rbcGrBarConfiguration.ConfigurePen` stages and commits these resources
     - :c:func:`rbcGrBarConfiguration.DestroyPen`
   * - Normal and active rectangles, source-index maps, mapped errors
     - Bar/Element own the arrays; styles borrow slices
     - :c:func:`rbcGrBarGeometry.ResetBar`
   * - Source vector slots
     - Element slots own subscriptions or private data, not application vectors
     - Rbc_FreeElemVector during concrete destruction
   * - Duplicate-X records
     - Graph owns freqArr; hash values borrow its records
     - Frequency-table rebuild or graph teardown

``BarPenStyle`` stores a concrete ``BarPen *`` alongside its weight range and
borrowed geometry slices. Never free those slices separately. :c:func:`rbcGrBarGeometry.ClearPalette`
invalidates only counts: after :c:func:`rbcGrBarGeometry.ResetBar` the slice addresses may still contain
old values and must not be dereferenced until remapping installs valid views.

Creation and configuration
--------------------------

:c:func:`rbcGrBarConfiguration.Rbc_BarElement` allocates zeroed storage, copies the element name and initial
label, installs ``barProcs``, initializes the embedded pen, and creates an empty
palette. It does not register the element, apply Tk option defaults or produce
geometry. Shared creation must finish these steps. The method table uses ordinary
paired X/Y counting; it has no complex-vector point-count override.

:c:func:`rbcGrBarConfiguration.Rbc_BarPen` similarly creates an unregistered named pen. Only the exact name
``activeBar`` selects active defaults. :c:func:`rbcGrBarConfiguration.InitPen` installs callbacks and fallback
fields in zeroed storage; it is not a substitute for Tk option initialization.
Named pens and embedded pens share derived-resource configuration, but have
different allocation and option-table owners. See :doc:`pens` and
:doc:`configuration` for the common machinery.

:c:func:`rbcGrBarConfiguration.ConfigureBar` runs after candidate Tk option fields have been installed:

1. Reject nonfinite bar width and offset. A finite nonpositive element width
   intentionally means use the graph width.
2. Prepare changed data, named pens, axis/track references, state, tags and palette
   through the shared transactions. Initial configuration prepares every category.
3. Validate and configure the embedded pen when its options changed.
4. Commit state, tags, axes, pens and palette; point the default palette entry at
   the resulting concrete normal pen.
5. Commit staged data, synchronize its retained Tcl representations and mark
   mapping dirty when required by the option mask.

On a preparation error, the function frees prepared candidates and returns
``TCL_ERROR``. The surrounding option transaction restores candidate Tk fields.
There is no fallible element operation after the embedded pen commits. This
ordering must be retained when adding options: validation after that point could
leave derived state inconsistent with restored Tcl options. Data-expression
callbacks retain the shared transaction's lifetime restrictions; arbitrary
application side effects are not rolled back.

:c:func:`rbcGrBarConfiguration.ConfigurePen` validates all fallible derived values before replacing resources:
opacity, label offset, command-prefix syntax, format, finite rotation, pixel
widths, error color, show masks and shadow. It then builds replacement GCs and
commits them together with the parsed values. On failure it releases staged
colors; old derived resources remain live. The value command is validated here,
not executed. Direct candidate Tk fields still need the caller's rollback.

Error-color conversion accepts an empty value as NULL and any nonempty,
case-sensitive prefix of ``defcolor`` as ``COLOR_DEFAULT``. The latter is a
sentinel, not a Tk color reference; :c:func:`rbcGrBarConfiguration.FreeBarPenColor` must not pass it to
``Tk_FreeColor``. Fill GC defaults choose foreground, then border color, then
screen black. With both foreground and border, stipple gaps can be opaque;
without both, the GC uses transparent stippling.

Grouping keys and passes
------------------------

:c:func:`rbcGrBarLayout.Rbc_InitFreqTable` rebuilds graph-owned duplicate-X metadata from displayed,
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
resets counters again before its ordered pass. :c:func:`rbcGrBarLayout.Rbc_ResetStacks` clears
``count`` and ``lastY``, retaining frequency and precomputed sums. A new consumer
must not call :c:func:`rbcGrBarLayout.PlaceBar` as an independent query: it advances these counters.
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

:c:func:`rbcGrBarLayout.GetBarExtents` uses full anchored X intervals, source Y extrema, baseline
rules and :c:func:`rbcGrBarLayout.CheckStacks` totals. Those totals are final signed sums computed
separately by :c:func:`rbcGrBarLayout.Rbc_ComputeStacks`; they are not a list of every intermediate
stack endpoint. Consequently this is not a tight union of all final screen
rectangles. Linear Y retains the historical zero-inclusive lower-bound rule.
Log X bounds use valid positive full intervals.

Extents also run placement without viewport rejection so that error-bar
translations follow group order. In ``-errorbarposition bar`` mode,
:c:func:`rbcGrBarGeometry.SetBarErrorOffset` records, per original source index::

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

:c:func:`rbcGrBarLayout.MapBar` first releases previous mapped storage. It calls :c:func:`rbcGrBarLayout.PlaceBar` in
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
mapped error segments and their source-index maps. :c:func:`rbcGrBarGeometry.MergePens` can reorder
rectangles and errors into pen groups, keeping the corresponding index maps in
step. Style slices borrow the resulting storage. :c:func:`rbcGrBarGeometry.ResetBar` clears palette
views, frees mapped arrays and zeros pointers/counts; source vectors remain
owned by their original creators.

Normal drawing batches rectangles through :c:func:`Rbc_RenderBeginBars` and
:c:func:`Rbc_RenderFillRectangles`. Export has its own callbacks consuming mapped bar
geometry and pen styles. Value labels and legend samples are separate drawing
paths. A legend sample outside the plot is not evidence that a bar escaped its
clip.

:c:func:`rbcGrBarLayout.ClosestBar` measures distance to mapped rectangles, then returns the original
source X/Y and index through ``rectToData``. A stacked hit therefore reports the
sample contribution, not the cumulative top. :c:func:`rbcGrBarGeometry.Rbc_GetBarRectangle` supplies
mapped bounds when a caller needs the actual placed rectangle instead.

Palette grouping and active geometry
------------------------------------

:c:func:`rbcGrBarGeometry.MergePens` runs after normal rectangles and error segments have been mapped.
One palette entry simply borrows the complete arrays. With multiple styles it
allocates replacement arrays and groups entries by palette order, retaining the
previous order within each style. Rectangle and error source-index maps move
with their geometry. Labels and picking must use these maps rather than treating
mapped-array position as a source index.

The multiple-style path scans the mapped arrays for each palette entry; it is
not an in-place partition. Do not change grouping order casually: it also sets
the order in which overlapping rectangles are painted. Checked array-byte
calculations reject invalid counts and size overflow, but these void mapping
helpers do not report Tcl allocation errors. Ordinary allocations use the
existing checked/fatal allocation conventions.

:c:func:`rbcGrBarGeometry.MapActiveBars` frees its previous copies and scans mapped rectangles against
the explicit source-index selection. Repeated requested indices do not duplicate
a rectangle. Clipped or otherwise unmapped source samples cannot appear in the
active subset. ``activeToData`` contains original data indices even after palette
reordering. ``ACTIVE_PENDING`` is cleared after rebuilding, including empty
results. Normal remapping builds explicit active copies before regrouping normal
arrays; later activation can rebuild them from the regrouped arrays.

The selection count is a three-way protocol: positive means explicit indices,
negative means all mapped rectangles, and zero means none. All-active drawing
uses the normal arrays directly instead of allocating a second full copy. Both
screen and export paths use the active pen for rectangles and labels, without
adding a separate active error-bar overlay.

Screen and export dispatch
--------------------------

Normal output visits each palette style in this order: rectangle batch, enabled
X errors, enabled Y errors, then value labels. Source-map offsets advance by the
style's rectangle count, not the number of error segments. Active output has its
own callbacks. Neither output path performs placement again: grouping counters
belong to extents/mapping passes, not rendering.

:c:func:`rbcGrBarOutput.DrawBarSegments` passes the complete batch and its relief, border, stipple and
opacity to the screen renderer. :c:func:`rbcGrBarOutput.SegmentsExport` instead prepares a fill style
and walks rectangles through the selected document backend; its historical name
does not mean line segments or PostScript-only output. It preserves the one-pixel
inset for fills and original rectangle dimensions for relief borders. Zero fill
opacity suppresses the fill, but does not itself suppress an available nonflat
border. An absent foreground and border produces no rectangle output.

Stipple export without a foreground promotes the background color to stipple
foreground and leaves the gaps transparent. Legend samples perform the same
color normalization, but are independent square symbols drawn with the normal
pen, not miniatures of individual palette or active rectangles. Screen samples
retain integer centering and size-minus-one geometry, trying the legend renderer
before the screen fallback. Zero opacity suppresses the legend sample even when
a data rectangle could still have a relief border.

Error colors use :c:func:`rbcGrBarConfiguration.GetBarColor` for native GC creation, screen strokes and
all document backends. Explicit colors remain unchanged. Both an empty color
and ``defcolor`` inherit foreground, then the bar background, then black if
neither is present. The final fallback uses caller-owned ``XColor`` storage:
its RGB components are black and its pixel is the screen's black pixel. Keep
that storage alive through the renderer context's use; no Tk color reference
is acquired. Configuration passes the staged error color before committing it.
Screen strokes still supply at least one pixel, while document strokes retain
the configured width and the backend's hairline handling.

Value labels and callbacks
--------------------------

:c:func:`rbcGrBarOutput.DrawBarValues` and :c:func:`rbcGrBarOutput.BarValuesExport` recover original X/Y values through
the rectangle's source-index map. Stacked labels therefore receive the source
contribution, not the cumulative stack top. Each call to the shared label helper
returns one owned Tcl object reference, released by the loop even for empty text.
See :doc:`elements` for command arguments, fallback formatting, background-error
reporting and interpreter-result preservation.

Without inversion the anchor is the horizontal center of the rectangle's top
edge, switching to its bottom edge when source Y is below the effective baseline.
With inversion it is the vertical center of the right edge, switching to the left
edge under the same source-value test. This decision uses source Y, not a new
comparison of cumulative stack endpoints. The pen offset is added afterwards.
Screen output checks integer bounds and truncates coordinates; document output
retains floating-point coordinates.

Label commands execute application Tcl while the output loop holds borrowed
element, pen and geometry pointers. These loops do not preserve drawing records
against deletion, reconfiguration or nested event processing. Callbacks must
leave that drawing state unchanged and avoid nested event loops. Ownership of
the returned label object protects its text, not the objects being drawn.

Cleanup and font changes
------------------------

:c:func:`rbcGrBarGeometry.DestroyBar` releases axes and named pens, destroys the embedded pen's derived
resources, detaches every data slot, resets mapped arrays and releases explicit
active indices, palette and tags. It does not free the concrete allocation or
Tk-managed option resources: shared element teardown owns those remaining steps.
Detaching a named vector does not destroy the application's vector.

:c:func:`rbcGrBarConfiguration.DestroyPen` releases text-style drawing resources and manually derived colors
and GCs, clearing released pointers. It deliberately leaves direct Tk option
resources to the appropriate shared pen or element owner. Keeping this boundary
explicit avoids double release of the embedded pen's options.

:c:func:`rbcGrBarConfiguration.Rbc_BarFontsChanged` visits both the named-pen table and every bar in the
element table, including undisplayed elements. It resets label text styles with
fonts, covering active/style pens through the named table and private embedded
pens through the element table. It does not itself remap rectangles or schedule
redrawing; the graph's font-change workflow owns that work.

Array and coordinate contracts
------------------------------

Bar helpers use three different indices. Source indices address paired X/Y
samples; mapped indices address surviving rectangles after placement and
clipping decisions; palette slices group those mapped rectangles for drawing.
The parallel ``rectToData`` map preserves the link back to the original sample.
Use :c:func:`rbcGrBarGeometry.Rbc_GetBarRectangle` with a source index, not an
index into a style's rectangle slice.

.. list-table:: Array extents and ownership
   :header-rows: 1
   :widths: 34 66

   * - Helper
     - Required relationship
   * - :c:func:`rbcGrBarGeometry.NewBarErrorOffsets`
     - Count covers original source slots, including samples that later fail placement. Caller frees the allocated array.
   * - :c:func:`rbcGrBarGeometry.SetBarErrorOffset`
     - Index must be valid in both source arrays and the offset array. Corners must still be in data space.
   * - :c:func:`rbcGrBarGeometry.MergePens`
     - Style lookup is indexed by original samples. The bar owns replacement geometry; palette entries only borrow slices.
   * - :c:func:`rbcGrBarOutput.DrawBarValues` / :c:func:`rbcGrBarOutput.BarValuesExport`
     - Rectangle and source-index arrays have matching counts and must remain stable across label callbacks.
   * - :c:func:`rbcGrBarOutput.DrawBarErrorSegments`
     - Count denotes independent mapped strokes, including caps, rather than source errors or rectangles.

:c:func:`rbcGrBarLayout.GetBarArrayByteCount` validates multiplication but
allocates nothing. Its output is unchanged on error. That differs from the
mapping helpers that discard old geometry before rebuilding it; a failed or
empty mapping is not a promise to preserve the previous image.

:c:func:`rbcGrBarLayout.PlaceBar` returns data-space corners and advances mutable
group state. :c:func:`rbcGrBarLayout.BarAnchorOffset` uses the full effective
width before any aligned/overlap subdivision. After mapping,
:c:func:`rbcGrBarGeometry.Rbc_GetBarRectangle` returns physical window bounds,
including inversion, but does not intersect them with the final renderer clip.
It writes output only on a successful source-index match.

Resource and output contracts
-----------------------------

:c:func:`rbcGrBarConfiguration.GetBarPenColorFromObj` can return NULL, the
``COLOR_DEFAULT`` sentinel, or an acquired Tk color. Use
:c:func:`rbcGrBarConfiguration.FreeBarPenColor` for these staged results.
By contrast, :c:func:`rbcGrBarConfiguration.GetBarColor` returns a borrowed
resolved color or the supplied fallback storage. It acquires no reference;
keep fallback storage alive until the renderer has finished using it.

:c:func:`rbcGrBarConfiguration.DestroyPen` does not free the pen allocation,
and :c:func:`rbcGrBarGeometry.DestroyBar` does not free the element allocation.
The embedded pen is part of the bar record. Shared lifecycle code owns final
record and Tk-option cleanup, while these concrete callbacks release derived
resources and dependencies.

Screen and export helpers borrow their destination and prepared geometry.
The outer element scope supplies clipping, including the relief borders that
can extend beyond a rectangle's fill. Legend-symbol callbacks instead receive
sample-center coordinates; their size is independent of the mapped data bars.
The screen callback's width and height describe its destination drawable, not
the requested sample dimensions. No output helper should call placement again
or consume grouping cursors merely to render an existing rectangle.
