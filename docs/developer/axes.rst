Axis ownership and commands
===========================

``rbcGrAxis.c`` owns the graph's named axis registry, option transactions, margin membership and axis commands. This
chapter covers that control layer; :doc:`mapping` explains coordinate spaces and track viewports. The source contracts
are in :doc:`axis-reference`. The following sections cover option transactions, ranges, tick generation, label
presentation, margin layout, scrolling and notifications.

Names, references and margin membership
---------------------------------------

The graph owns ``axes.table``. Each record owns its copied name and derived resources. Name lookup and reference
acquisition are different operations:

.. list-table::
   :header-rows: 1

   * - Operation
     - Ownership effect
   * - :c:func:`rbcGrAxisRegistry.NameToAxis`
     - Borrow a live record; reject pending deletion.
   * - :c:func:`rbcGrAxisRegistry.GetAxis` with no class
     - Borrow a live record without increasing its count.
   * - :c:func:`rbcGrAxisRegistry.GetAxis` with an X/Y class
     - Acquire a reference and enforce compatible logical axis type.
   * - :c:func:`rbcGrAxisRegistry.FreeAxis`
     - Release one reference; destroy only if deletion is pending and count becomes zero.
   * - :c:func:`rbcGrAxisRegistry.DestroyAxis`
     - Unconditionally release the record and resources.

An unreferenced live axis stays registered. A delete-pending referenced axis stays allocated but is excluded from
ordinary name lookup and ``axis names``. Recreating its name revives the same record, retaining its references and
resources. This is different from allocating a replacement record.

``classUid`` represents logical X/Y identity, not the current screen direction. An untyped or unused record may acquire
a class; a referenced record cannot be acquired with the opposite class. Inversion changes margin selection rather than
exchanging logical data coordinates.

The margin chain controls display order. ``linkPtr``, ``chainPtr`` and ``AXIS_ONSCREEN`` describe membership
independently of ``refCount``. Do not use membership as proof of a retained reference. The four default axes receive one
reference during initialization. Track-owned Y axes are arranged through track configuration; margin ``use`` rejects
that arrangement in trackgraphs.

Configuration boundary
----------------------

Initialization uses the named ``Axis`` option-database component, including component-name normalization. Initialization
failure releases partially installed Tk resources. Even an empty explicit option vector must pass through configuration
so database defaults become derived axis state.

:c:func:`rbcGrAxisConfiguration.ConfigureAxisOptions` holds a Tk saved-options transaction and temporarily exposes the
original argument vector and change mask to :c:func:`rbcGrAxisConfiguration.ConfigureAxis`. The vector is borrowed,
never retained after the call. Derived limits, ticks, loose ranges, pixels, shadows, binding tags and limit formats are
prepared before committing any of them. Command-prefix validation does not execute Tcl callbacks. On recoverable
failure, prepared resources are released and Tk-managed options are restored. Success refreshes styles and title
extents, marks mapping dirty, requests redraw and records a pending configure notification; it does not synchronously
notify listeners here.

The atomic boundary is one axis. Multi-axis ``configure`` validates names first but applies configuration
sequentially. A later failure does not undo an earlier successful axis. Likewise, multi-name deletion can partially
succeed before a later lookup or track-ownership error.

Dispatch and bindings
---------------------

Named-axis dispatch receives the full widget command vector. Margin dispatch selects the first axis in the margin chain
and passes an argument suffix to its handler. Keep these conventions distinct when sharing command helpers. The
full-vector handlers read the operation at index 2 and the first operand at index 3. Margin dispatch removes the first
three entries before calling its selected handler. For example, :c:func:`rbcGrAxisCommands.CgetVirtualOp` resolves the
name at index 3, then passes the option suffix beginning at index 4 to :c:func:`rbcGrAxisCommands.CgetOp`, where the
option is at index 0.

:c:func:`rbcGrAxisLayoutControl.Rbc_TrackYViewOp` also receives a full vector, with the track name at index 3. It
consumes optional ``-mapy axis`` entries before constructing a temporary named-axis view request. These arrays and
objects are borrowed during dispatch; changing their offsets does not change ownership or acquire references to the
resolved axes.

Bindings use graph-owned interned keys for the axis name, logical class and configured tags, in that order. ``axis
bind`` can create bindings for a tag without an existing axis. Its tag listing is not a list of live axes. ``axis
names`` excludes pending deletion and follows hash order; margin ``use`` queries follow display-chain order. Picking
queries check the binding context before interpreting the current item as an axis.

Cleanup and failure recovery
-------------------------------

:c:func:`rbcGrAxisRegistry.DestroyAxis` cancels pending scrollbar notification and releases bindings, margin links,
registry membership, Tk options and derived resources. Text styles own their GCs, while options own configured fonts and
foreground colours; separately allocated shadow colours require separate release. Graph teardown destroys all axes
regardless of reference counts, so consumers must already have been detached. Partial default-axis initialization is
left for that teardown path.

Margin ``use`` validates the complete replacement before changing chains or logical types. Invalid names or incompatible
axes leave all margin lists unchanged, including axes that would have moved from another margin.

Failed creation frees a new record. Failed revival instead restores pending deletion and the previous
notification-validity flag after configuration rollback, preserving existing consumers. A later successful revival
exposes the same record again; releasing its final reference while still deleted continues to destroy it normally.

Option transaction internals
----------------------------

Tk owns the retained option objects. The seven private transaction records hold parsed candidates separately from live
derived fields. They are local to one configuration attempt, not persistent state on an axis.

.. list-table::
   :header-rows: 1

   * - Family
     - Candidate ownership
     - Commit effect
   * - Limits (:c:func:`rbcGrAxisTransactions.PrepareAxisLimitTransaction`)
     - Four scalar values and a changed-field mask
     - Replace only staged display/scroll limits.
   * - Pixels (:c:func:`rbcGrAxisTransactions.PrepareAxisPixelTransaction`)
     - Three integer values and a mask
     - Replace widths and scroll increment.
   * - Loose ranges (:c:func:`rbcGrAxisTransactions.PrepareAxisLooseTransaction`)
     - Two policy values and a staged flag
     - Replace both endpoint policies together.
   * - Ticks (:c:func:`rbcGrAxisTransactions.PrepareAxisTickTransaction`)
     - Owned major/minor arrays and a mask
     - Transfer arrays and update explicit-tick flags.
   * - Shadows (:c:func:`rbcGrAxisTransactions.PrepareAxisShadowTransaction`)
     - Owned Tk colour references with offsets and a mask
     - Transfer colours into text styles; release old references.
   * - Binding tags (:c:func:`rbcGrAxisTransactions.PrepareAxisTagsTransaction`)
     - One owned string-list allocation and a staged flag
     - Transfer the list and free the old allocation.
   * - Limit formats (:c:func:`rbcGrAxisTransactions.PrepareAxisFormatTransaction`)
     - One owned split-list allocation, count and staged flag
     - Transfer the list/count and free the old allocation.

Preparation initializes fresh storage. Reusing an owning transaction without first releasing it would lose its
candidates. During initial configuration, preparation finds explicit overrides before parsing effective database/default
objects. This lets a valid explicit override replace an invalid unused default. Every explicit occurrence is then
parsed in caller order. For example, an invalid first ``-majorticks`` list is not ignored merely because the same
command ends with a valid ``-majorticks`` list.

Option classification uses the canonical option-table name, including synonyms such as ``-bd``. These helpers assume Tk
has already rejected unknown and ambiguous options. Prefix matching should not be reimplemented independently inside
each transaction family.

Stage helpers acquire replacement storage before freeing an earlier candidate. An empty candidate is still meaningful:
a staged ``NULL`` tick array clears the explicit-tick flag and restores automatic ticks. The staged flag or mask must
therefore be checked separately from the pointer. Commit transfers owned storage, clears the transferred candidate
fields and releases the old live resource. Cleanup then remains safe after a successful commit. Scalar-only transactions
need no resource destructor.

Transaction argument contracts
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Preparation takes fresh caller-owned storage and initializes it; stage helpers instead take an initialized candidate and
may replace an earlier candidate. Commit requires successful preparation of all affected families. It transfers owned
resources or copies scalars, then clears staging state. These are distinct contracts even though each helper accepts a
transaction pointer. The parameter sections in :doc:`axis-reference` identify the required state at each boundary.

Lookup outputs also differ. :c:func:`rbcGrAxisRegistry.NameToAxis` clears its output on a failed
lookup. :c:func:`rbcGrAxisRegistry.GetAxis` leaves the caller's output unchanged on
error. :c:func:`rbcGrAxisRegistry.Rbc_GetAxisFromObj` sets it to ``NULL`` for an empty name, but retains the
unchanged-on-error rule for nonempty names. Check the status before consuming an output, and release a reference only
when a successful lookup requested a non-``NULL`` logical class.

Parsing policies
~~~~~~~~~~~~~~~~

Display and scroll limits accept finite Tcl expressions; empty values select automatic limits. The limits transaction
starts with current effective values, then validates the final pairs. When both endpoints are defined, minimum must be
less than maximum. Logarithmic scaling requires every defined display or scroll endpoint to be positive, including when
only ``-logscale`` changes.

Explicit ticks are finite expressions in input order. The parser neither sorts nor deduplicates them. It rejects counts
beyond ``MAXTICKS`` before allocation, checks the variable-sized array calculation for overflow and reports allocation
failure through the interpreter. Empty lists select automatic generation. Widths must be nonnegative; scroll increment
must be positive. Pixel conversion uses the widget's screen scaling. Loose-range policies accept one or two Tcl booleans
or the exact word ``always``; a single entry applies to both ends.

Limit formats are a list of at most two strings. Each string permits literal text, escaped percent signs and at most one
floating-point conversion accepting a ``double``. Dynamic width/precision and incompatible conversion types are
rejected. Format length, width and precision are bounded by ``TICK_LABEL_SIZE``. Those input bounds are not a proof
that every formatted numeric result fits that many characters; output routines must retain their own buffer bounds.

Expression evaluation boundary
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Limit and explicit-tick parsers use ``Tcl_ExprDoubleObj``. Unlike command-prefix validation, this can execute
substitutions, variable traces and Tcl commands. The transaction rolls back RBC candidate resources and Tk option
changes; it cannot undo arbitrary Tcl side effects. The low-level parsers do not themselves retain graph/axis lifetime
or validate it after evaluation. Treat them as Tcl evaluation boundaries when auditing reentrancy, rather than assuming
that all configuration parsing is callback-free.

Ranges and tick generation
--------------------------

Range calculation precedes tick-array allocation, label layout and mapping. The range fields are not interchangeable:

.. list-table::
   :header-rows: 1

   * - State
     - Meaning
   * - ``valueRange``
     - Accumulated element bounds, subsequently repaired for empty/degenerate data.
   * - ``reqMin`` / ``reqMax``
     - User requests, or the undefined sentinel for automatic limits.
   * - ``min`` / ``max``
     - Resolved display limits before scaling and loose-endpoint policy.
   * - ``axisRange``
     - Prepared mapping bounds; logarithmic axes store base-ten exponents here.
   * - ``majorSweep`` / ``minorSweep``
     - Recipes for generated ticks, not allocated arrays.
   * - ``t1Ptr`` / ``t2Ptr``
     - Explicit or generated tick arrays, distinguished by configuration flags.

:c:func:`rbcGrAxisRanges.Rbc_ResetAxes` prepares bar frequencies and stacks before collecting extents. It excludes
hidden and plot-hidden elements, but visits all registered axes, including pending deletions retained by
consumers. Polar grid axes with no contributing data receive a symmetric fallback; Smith grid axes always include the
intrinsic unit-circle bounds in their accumulated nonlogarithmic range. Explicit requested limits are applied later and
can still restrict the displayed region.

:c:func:`rbcGrAxisRanges.FixAxisRange` supplies missing bounds, expands degenerate data and applies requested
endpoints. Automatic window shifting applies only when neither endpoint was requested. Reset then constrains the scaling
inputs by configured scroll limits, selects linear or logarithmic scaling, and requests later layout and mapping. It
does not construct labels or paint the graph.

Linear sweep policy
~~~~~~~~~~~~~~~~~~~

The automatic interval uses decimal 1/2/5/10 multiples, targeting approximately four intervals. A requested positive
interval is tried first, but historical behavior reduces an overly large interval by halves. An interval producing more
than ``MAXTICKS`` or nonfinite sweep arithmetic is rejected internally and replaced by the automatic interval. If
neither produces a usable sweep, the range is retained with no generated major ticks.

:c:func:`rbcGrAxisRanges.GetLinearSweep` computes enclosing endpoints in units of the step. It validates the count while
still represented as a double, before converting to ``Tcl_Size``. This avoids overflowing the raw endpoint difference or
converting an unbounded count to an integer. :c:func:`rbcGrAxisRanges.RoundToStep` similarly avoids integer rounding and
retains the input when quotient/product arithmetic is unusable.

Loose policy applies separately to each endpoint. ``tight`` keeps the resolved endpoint; ordinary ``loose`` uses an
enclosing tick unless that endpoint was explicitly requested; ``always`` uses the enclosing tick even for an explicit
request. The tick sweep can extend beyond the mapping range, so later geometry must filter ticks against the range.

Logarithmic sweep policy
~~~~~~~~~~~~~~~~~~~~~~~~

Logarithmic mapping stores exponent coordinates. A span covering at most ten enclosing decades uses a major step of one
decade. Wider spans select a nice interval in exponent space. Requested linear ``-stepsize`` is not consulted by this
scaling routine.

Narrow logarithmic minor ticks use the eight offsets ``log10(2)`` through ``log10(9)``. Their sweep has ``step == 0`` as
a special selector; it does not mean repeated coincident ticks. Wider logarithmic sweeps instead use four fractions at
0.2 increments between major ticks.

:c:func:`rbcGrAxisRanges.LogAxisValue` converts back only within the interval bounded by ``DBL_MIN`` and ``DBL_MAX`` and
returns ``NAN`` outside it. Here ``DBL_MIN`` means the smallest positive *normal* double, not the smallest subnormal
value. Exact endpoint handling avoids power-function rounding across these bounds.

Explicit arrays and ownership
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrAxisRanges.SweepTicks` replaces generated arrays but preserves arrays selected by ``AXIS_CONFIG_MAJOR`` or
``AXIS_CONFIG_MINOR``. Explicit major ticks disable automatic minor generation; an explicit minor list remains
available. With explicit majors, minor fractions are placed between adjacent major entries, including descending
pairs. Equal endpoints, missing following endpoints, fractions outside the open interval and rounded positions outside
it are rejected. No minor is extrapolated after the final explicit major.

:c:func:`rbcGrAxisRanges.GenerateTicks` returns an owned allocation even for an empty sweep. Invalid internal sweeps
degrade to empty arrays rather than a layout error. Allocation itself uses the fatal Tcl
allocator. :c:func:`rbcGrAxisRanges.Rbc_AllocAxisMajorTicks` always returns an independent array: it copies an existing
cache or generates from the sweep when no cache exists, notably for hidden Polar axes. Its copy path uses attempted
allocation and can return ``NULL``. Callers must both check that result and free successful results; they must not assume
every cache is an explicitly supplied list.

Numerical mapping boundary
~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrAxisRanges.SetAxisRange` caches the endpoint difference and reciprocal. Very small absolute spans receive
a unit cached range without changing the endpoints. Overflow-sized spans are handled by half-scaled
normalization/interpolation. These helpers do not establish a universal finite-result guarantee: extrapolation and
extreme inputs can still be nonfinite. Mapping consumers must retain their checks before integer conversion or
rendering.

Horizontal and vertical helpers use the full owning-track world rectangle, not just its visible clipped
portion. Descending reversal and downward screen Y are applied after normalization. The low-level forward coordinate
helpers expect logarithmic conversion to have happened already; inverse helpers convert exponents back to data
values. See :doc:`mapping` for clipping and viewport responsibilities.

Labels, geometry and presentation
---------------------------------

Axis presentation has separate measurement and mapping phases. Measurement rebuilds the filtered label chain and records
text dimensions and perpendicular axis thickness. Mapping computes final anchors and cached segments for the current
plot rectangle. Drawing and export consume those caches; they do not invoke the formatter again.

:c:func:`rbcGrAxisPresentation.NewTickLabel` stores a copied string in the label allocation and initializes its anchor
to ``DBL_MAX``. Measurement fills its dimensions, but only mapping replaces the anchor with a drawable
position. :c:func:`rbcGrAxisPresentation.AxisLayoutUsable` prevents screen drawing, export and picking when either plot
range is at most one pixel, because mapping was skipped and the cached positions may be stale or uninitialized. This is
particularly important during resizing with large or rotated labels.

Formatter callback boundary
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The formatter receives the widget path and the legacy default numeric label. The command prefix is duplicated before
appending arguments. The callback's result is copied into an owned label; the caller's interpreter state is
restored. Callback errors are reported as background exceptions and normally fall back to the default label.

A callback may also delete the axis, destroy the widget or replace layout resources. After evaluation,
:c:func:`rbcGrAxisPresentation.Rbc_AllocAxisTickLabel` checks graph validity without dereferencing the old axis
pointer. Invalidation releases the newly allocated label and returns
``NULL``. :c:func:`rbcGrAxisPresentation.GetAxisGeometry` must stop immediately; its callers must check callback validity
before resuming their own traversal. A callback error and callback invalidation are therefore different outcomes.
Neither a numeric fallback nor preserved graph storage proves an old axis or label-chain pointer is still valid.

Geometry and picking
~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrAxisPresentation.GetAxisGeometry` measures multiline text and rotated bounds, includes tick, baseline and
title spacing, and stores the orientation-dependent thickness. Wide accumulators and clamped conversions protect the
stored integer sizes. :c:func:`rbcGrAxisPresentation.GetMarginGeometry` aggregates visible axes and alternate-title
requirements. For a trackgraph's local Y margin it reserves the maximum per-track sum; multiple axes within one track
still occupy separate bands.

:c:func:`rbcGrAxisPresentation.AxisOffsets` chooses title/tick anchors and absolute perpendicular positions for the
selected margin. Negative tick lengths preserve inward-tick behavior by exchanging the baseline and major-tick
positions. :c:func:`rbcGrAxisPresentation.MapAxis` allocates segments for the baseline and in-range major/minor ticks
and positions the already measured labels. Count overflow or segment allocation failure leaves no segment array and is
not reported as a Tcl command error.

:c:func:`rbcGrAxisPresentation.Rbc_GetAxisSegments` is a separate grid interface. It borrows cached ticks when available
and generates temporary ticks otherwise, including for axes without visible rectangular decorations. It returns a
caller-owned segment array or ``NULL``/zero, and always releases its temporary tick arrays. Grid spanning uses the current
plot viewport; callers requesting local track grids must establish that viewport first.

Picking is containment, not a distance minimization despite the name :c:func:`rbcGrAxisPresentation.Rbc_NearestAxis`. It
visits axes in registry order and tests tick-label polygons, title polygons and the axis line region, recording the
matching ``detail``. It applies the visible scrolling-band checks used by presentation. A returned axis is
borrowed. :c:func:`rbcGrAxisPresentation.Rbc_GetFirstAxis` is simpler still: it returns the first chain entry without
filtering hidden or pending-deletion records.

Screen and document output
~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrAxisPresentation.DrawAxis` uses the renderer interface for borders, native text and line segments,
retaining the native segment fallback when no drawable context is available. It also computes scrollbar fractions and
queues notification; it does not invoke the scroll command synchronously. :c:func:`rbcGrAxisPresentation.AxisExport`
emits cached title, labels and segments through the shared export renderer, without scrollbar work. Current screen
strokes use projecting caps while exported strokes use butt caps; the optional screen axis border is not exported.

Local axes map against their full track world rectangle. When track scrolling is enabled, screen output draws into a
temporary pixmap and copies back only the visible band, including the axis margin. Export applies a corresponding clip
scope. This keeps partially scrolled text and wide strokes away from fixed shared axes without changing their logical
positions.

Limit annotations and remaining differences
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Limit annotations are a separate registry-wide pass selected by ``nFormats``, not by the ordinary visible-axis
chain. One format applies to both ends; two formats select them independently. Empty format strings omit an
endpoint. Both screen and export format ``axisRange`` directly, so logarithmic annotations currently show exponent
values rather than the raw data-space results returned by the limits command.

Screen and export use inversion-aware orientation, swap formatted endpoint labels for descending axes, and measure
spacing with the limits text style. Each format stays associated with its numeric endpoint when positions swap; an
omitted endpoint remains omitted. Tick-font changes do not alter spacing between limit annotations when the limits font
is unchanged.

Margin layout and aspect
------------------------

:c:func:`rbcGrAxisLayoutControl.ComputeMargins` measures axes first, checking formatter validity after each margin. It
then reserves graph-title space, maps the legend against the estimated cavity and adds space for a margin-hosted
legend. A legend in the plot, at explicit coordinates or in another window does not enlarge margins. Aspect correction
follows, then adjoining space for alternate axis titles. Positive explicit margin sizes override those calculated
requirements last. Consequently an explicit margin can override room previously reserved by aspect or decoration
calculations.

Polar automatic aspect follows the axes selected by the grid. It uses their prepared mapping ranges and excludes
logarithmic or nonfinite ranges. In the ordinary orientation the target mapped-width/mapped-height ratio is the X/Y
range ratio; inversion reverses it. This equalizes physical distance per data unit. Plot padding is removed for this
calculation and restored afterward. An explicit fixed aspect instead constrains the outer plot cavity ratio.

:c:func:`rbcGrAxisLayoutControl.Rbc_LayoutMargins` adds widget/plot insets, stores plot bounds and derives mapping
offsets, ranges and reciprocal scales after plot padding. Wide intermediates and clamped integer storage avoid wrapping
dimensions. Its one-pixel minimum permits safe arithmetic during resizing; presentation still requires the stricter
:c:func:`rbcGrAxisPresentation.AxisLayoutUsable` check. Layout does not itself map axis segments or certify that every
requested decoration fits.

Queries and freshness
---------------------

Limits and transform handlers synchronize attached vectors and reset pending axis ranges. They do not perform complete
margin layout. ``limits`` returns a two-value minimum/maximum pair in data coordinates, converting logarithmic mapping
bounds back from exponents. Forward transform accepts a Tcl expression and returns an integer coordinate truncated
toward zero after finite/range checks. Inverse transform accepts an integer coordinate and returns a double. Both
choose direction using logical axis class and inversion.

Forward-transform expression evaluation can execute Tcl commands or traces. The current handler does not revalidate its
borrowed axis afterward. This is an additional evaluation boundary to include in a runtime reentrancy audit; ordinary
numeric validation alone is not a lifetime guarantee.

Scrolling state and local views
-------------------------------

:c:func:`rbcGrAxisLayoutControl.GetAxisScrollState` derives its world from accumulated data bounds, falling back to
current view endpoints for missing sentinels and then applying explicit scroll bounds. It requires a finite increasing
world. The current view is intersected with that world; a wholly outside view collapses to the nearest boundary, and a
zero-width view remains valid. Logarithmic scrolling performs this arithmetic in exponent coordinates.

:c:func:`rbcGrAxisLayoutControl.ViewOp` converts normalized data endpoints into screen-oriented fractions. Queries
clamp the pair to 0..1. Mutations use the shared scroll-request parser: ``moveto`` sets the offset, pages move by 90
percent of the current view, and units use the configured scroll
increment. :c:func:`rbcGrAxisLayoutControl.AdjustViewport` applies the canvas-style offset bounds. Reconstructed limits
must be finite and increasing before they become requested axis limits. An empty view accepts movement as a no-op.

For ordinary axes the current implementation selects ``hScale`` or ``vScale`` using its forward/reverse
branch. Track-owned axes instead divide by their full world extent in the physical axis direction. These are the actual
current formulas; the ordinary branch should not be assumed to use physical direction independently of descending
state. :c:func:`rbcGrAxisLayoutControl.ViewOp` itself does not refresh layout.

Track-local ``yview`` validates optional axis membership, performs graph layout and delegates to the same view
handler. A query uses the selected or first member. A mutation visits all members unless ``-mapy`` selects one. It saves
requested limits before mutation; if a later member fails, it restores all of those limits and the old graph flags,
retaining a newly scheduled redraw flag because its idle callback may already exist. This is a requested-limit
transaction, not rollback of arbitrary layout or Tcl side effects.

Notifications and delivery
--------------------------

Scrollbar updates and axis-change events have separate mechanisms:

.. list-table::
   :header-rows: 1

   * - Mechanism
     - Data retained
     - Delivery contract
   * - Scrollbar command
     - Latest first/last fractions, one pending idle flag
     - Idle evaluation of a copied prefix with two fractions appended.
   * - Axis change events
     - Last notified data-space limits and pending configure flag
     - Event name lists prepared after screen display.

:c:func:`rbcGrAxisLayoutControl.UpdateAxisScrollbar` suppresses temporary export updates and coalesces repeated screen
updates. At idle, :c:func:`rbcGrAxisLayoutControl.NotifyAxisScrollbar` clears the pending flag, checks eligibility and
copies all command arguments before Tcl execution. It preserves graph/interpreter storage and interpreter state and does
not access the axis after evaluation. Errors are background exceptions; axis destruction cancels outstanding idle
callbacks.

:c:func:`rbcGrAxisLayoutControl.Rbc_NotifyAxisChanges` uses a separate notification baseline because a query can reset
ranges before the next display. It skips deleted/unconfigured axes, collects names in registry order and updates
baselines before submitting ``RbcAxisChanged`` and ``RbcAxisLimitsChanged``. Configuration changes contribute to the
first event; changed limits contribute to both. The display caller must exclude temporary export mappings: this function
does not contain its own export guard. Its event lists own their copied names and are released after submission.
