Grid mapping and inheritance
============================

``rbcGrGrid.c`` owns grid options, axis references, segment caches and dispatch to screen or document output. A global
:c:type:`Grid` belongs to the graph. A track can own a local :c:type:`Grid` and always has separate X/Y segment caches
for its plot rectangle. See :doc:`grid-reference` for the shared interfaces.

Configuration and axis selection
--------------------------------

Grid configuration prepares replacement dash patterns, line width, axis references and representation-specific tick
lists before committing active state. Axis lookup acquires references that must be released on failure or
replacement. Local grids require a shared X axis and a Y axis owned by the same track; either mapping can be empty to
disable that direction.

The global grid uses the named ``grid``/:c:type:`Grid` option-database component. Local-grid initialization follows a
different path: it initializes a local record and copies the effective inherited options into it. Do not infer a
separate named option-database child merely from the nested Tcl command. See :doc:`configuration` for the common option
transaction rules.

Inherited and local state
-------------------------

When ``track->grid`` is NULL, the track uses global grid settings. Its X axis comes from the global grid. If the global
Y mapping is enabled and the track has axes, the inherited Y mapping uses the track's first axis; otherwise that
direction is disabled. It does not blindly reuse another track's Y axis.

:c:func:`rbcGrid.Rbc_TrackGridOp` builds a temporary view for inherited queries. A ``cget`` or configuration query
therefore does not materialize a local grid or freeze its defaults. The view borrows global resources and has a
temporary option table and Y-axis name object; cleanup must not treat it as an independently owned live grid.

The first modifying operation creates a candidate local grid from the effective settings. It is attached to the track
only when the requested operation succeeds. Failure destroys the candidate. Once local state exists, subsequent
operations use it rather than continuing to inherit changes to the global settings.

Nested dispatch temporarily selects the local grid or query view through ``graphPtr->gridPtr`` and restores the global
pointer afterward. This is a synchronous adapter for existing grid operations, not a transfer of ownership or permission
to retain the temporary view.

Segment mapping
---------------

:c:func:`Rbc_MapGrid` frees previous global X/Y segments and resets their counts. For an ordinary Cartesian graph,
:c:func:`rbcGrAxisPresentation.Rbc_GetAxisSegments` generates segments from each selected axis's ticks, including minor
ticks when requested. Mapping consumes prepared axes and plot geometry; it does not compute a new axis range or paint
anything.

For trackgraphs, the mapper clears and rebuilds ``track->gridX`` and ``track->gridY`` using the effective local or
inherited configuration. Collapsed visible regions are skipped. Generation runs inside a paired track viewport scope so
segments span the owning plot region. Style ownership and geometry ownership are therefore distinct: an inherited style
can be shared while every track still owns its own mapped segment arrays.

For polar widgets, mapping delegates according to representation to :c:func:`Rbc_MapPolarGrid` or
:c:func:`Rbc_MapSmithGrid`. Their specialized geometry remains behind the same grid output interface. Tick-list and
label-format settings for those representations should not be mistaken for Cartesian per-track options.

Hidden state is evaluated at output, not used to avoid rebuilding these caches. Changing visibility alone must not
leave an independently visible local grid using stale or missing geometry. See :doc:`mapping` for layout ordering.

Screen and document output
--------------------------

Both :c:func:`Rbc_DrawGrid` and :c:func:`Rbc_GridExport` dispatch trackgraphs before testing the global grid's hidden
flag. :c:func:`rbcGrid.DrawTrackGrids` checks each track's effective grid independently. This allows a local grid to
remain visible while the global grid is hidden and other tracks still inherit that hidden state.

Screen output first attempts the renderer stroke path. The native fallback for track grids clips complete strokes
through a reusable scratch drawable, rather than relying on a GC clip that Windows segment emulation does not honor.
Clipping only segment centerlines would allow thick boundary strokes to escape into neighboring tracks. Dash coordinates
must remain consistent through this fallback.

Export uses renderer segment output with an explicit visible-track clip. Both paths enter and leave the track viewport
around their work. Ordinary non-track output uses the global segment caches and skips a hidden global grid. The output
functions do not remap geometry or finalize export documents. See :doc:`rendering` for renderer ownership and
:doc:`mapping` for temporary export layouts and screen restoration.

Cleanup and regression checks
-----------------------------

:c:func:`Rbc_DestroyTrackGrid` destroys local option resources, clears the local-grid pointer, releases both per-track
segment arrays and zeros their cache records. It also handles tracks with inherited configuration and no local grid.
:c:func:`Rbc_DestroyGrid` clears the global pointer, releases every track grid, then releases the global record. The
track list must still be valid during this pass.

Grid destruction requires a live Tk window for option cleanup. Axis references, option tables, private graphics
contexts, segment arrays and specialized tick arrays each have explicit cleanup. Do not free borrowed query-view
resources using this owning destructor.

Useful regression cases include querying before global reconfiguration, first local mutation, independent local
visibility, alternate axis selection, inversion, thick boundary strokes and resized export followed by restored screen
geometry. Check both screen renderers and document output: a correct centerline or unchanged transform alone does not
prove complete stroke clipping.

See :doc:`polar` for polar/Smith geometry and its separation from element data conversion.

Tick transactions and duplicate options
---------------------------------------

Tk's saved-options transaction restores option objects and inline option values. The grid additionally needs its own
transaction for derived double arrays. Restoring a Tcl list object alone cannot restore an array that was already freed
or replaced. The implementation helpers and staging records are included in :doc:`grid-reference`.

Angular ticks have major/minor slots; Smith ticks have real/imaginary slots at both tiers. Each slot has an owned
pointer, a count and a staged flag. The flag is necessary because these states have different meanings:

.. list-table::
   :header-rows: 1
   :widths: 25 25 50

   * - Staged
     - Pointer/count
     - Commit meaning
   * - False
     - NULL/0
     - Keep the existing active list
   * - True
     - NULL/0
     - Replace the active list with an empty list
   * - True
     - Allocated/N
     - Transfer the complete N-value list to the grid

Preparation first identifies options explicitly present in the caller's original argument sequence. During initial
configuration, defaults or option-database values are staged only for lists not explicitly overridden. Explicit
occurrences are then parsed in caller order. An invalid first occurrence remains an error even if a later duplicate
supplies a valid list. A successful duplicate frees only the previous staged array; active arrays remain untouched until
commit.

The parser preserves order and duplicates within a list. Angular values must be finite degrees in [0,360); Smith real
ticks allow zero, while imaginary tick magnitudes must be strictly positive. Negative zero is canonicalized when
accepted. These parsers validate values, not their eventual visual spacing. Label command options receive only
list-syntax validation here: an empty list is accepted, command existence is not checked, and no label callback is run.

After all conversion, axis and prefix checks succeed, configuration prepares the replacement GC and commits derived
state. Commit transfers staged pointers, clears staging ownership and frees replaced active arrays. Cleanup can
therefore run after either preparation failure or commit without freeing live arrays. The owning configure operation
separately releases or restores Tk saved options and schedules the appropriate reset/redraw. These helpers must not be
used as an independent Tk option rollback mechanism.

The custom automatic anchor is simpler: its internal representation contains only a Tk anchor and a boolean. Set saves
the old inline value, get constructs a fresh Tcl string, restore copies the saved value, and free is intentionally a
no-op. It owns no resource requiring deferred release.

Command transactions and visibility
-----------------------------------

The graph grid dispatcher checks command names and arity before handlers access arguments. ``cget`` and both forms of
``configure`` query return Tk option values or metadata without configuring the grid. An update stages Tk options,
exposes the original argument sequence to derived-state validation, then clears that borrowed sequence before either
committing or restoring saved options. Keeping the sequence is necessary for duplicate tick-option validation, not
permission to retain command arguments beyond the call.

Successful polar changes request a world reset. Changing grid axis mappings also requests that reset when polar
automatic aspect depends on those axes. Options marked for redraw invalidate the backing store and schedule an idle
display. Visibility commands only change hidden state and schedule that redraw; they do not erase pixels directly or
discard mapped segments.

Track dispatch validates the adapted command before materializing local state. Its query classification is based on the
selected handler and argument count, so abbreviated operation names retain query semantics. Any successful modifying
command detaches an inherited grid, even an ``on`` or ``off`` command that leaves the effective hidden value
unchanged. Failed first mutations destroy the candidate and preserve inheritance. Subsequent mutations operate on the
existing local record with its own saved-options transaction.

The temporary global-grid pointer replacement is safe only while operations stay synchronous and do not evaluate Tcl
callbacks. Format-prefix validation checks list syntax here; actual label evaluation belongs to drawing. Adding event
processing or callbacks inside grid configuration would require revisiting this selection and candidate-lifetime
protocol.

Parameter boundaries for configuration and drawing
--------------------------------------------------

The graph-facing lifecycle starts with :c:func:`Rbc_CreateGrid`. Its record owns axis references, option state and
derived arrays, but a track's inherited query view borrows these resources. Only owning records may be passed to
:c:func:`rbcGrid.DestroyGrid`; :c:func:`Rbc_DestroyTrackGrid` additionally releases the track's separate segment caches
and clears its local pointer.

.. list-table:: Tick-array ownership at each stage
   :header-rows: 1
   :widths: 35 65

   * - Operation
     - Caller contract
   * - :c:func:`rbcGrid.ParsePolarAngleTicks` / :c:func:`rbcGrid.ParseSmithTicks`
     - Required pointer/count outputs receive owned storage on success, or NULL/zero for empty input or failure.
   * - :c:func:`rbcGrid.StageGridPolarAngleTicks` / :c:func:`rbcGrid.StageGridSmithTicks`
     - Transaction must already be initialized. A successful replacement frees only that slot's previous staged array.
   * - :c:func:`rbcGrid.PrepareGridPolarAngleTicksTransaction` / :c:func:`rbcGrid.PrepareGridSmithTicksTransaction`
     - Output storage must not own arrays before entry: preparation zeroes it before processing defaults and explicit options.
   * - :c:func:`rbcGrid.CommitGridPolarAngleTicksTransaction` / :c:func:`rbcGrid.CommitGridSmithTicksTransaction`
     - Successful preparation is required. Transferred pointers are detached before subsequent staging cleanup.
   * - :c:func:`rbcGrid.FreeGridPolarAngleTicksTransaction` / :c:func:`rbcGrid.FreeGridSmithTicksTransaction`
     - Requires initialized storage; frees owned arrays and resets the record, including after a prior cleanup.

These output contracts deliberately differ from inline option conversion. :c:func:`rbcGrid.SetPolarLabelAnchor` leaves
the active value unchanged on failure and optionally saves its previous value in Tk rollback storage.
:c:func:`rbcGrid.GetPolarLabelAnchor` returns a fresh zero-reference Tcl object;
:c:func:`rbcGrid.RestorePolarLabelAnchor` copies an inline value and :c:func:`rbcGrid.FreePolarLabelAnchor` owns nothing
to free.

:c:func:`rbcGrid.Rbc_TrackGridOp` receives the full nested command with the grid operation at index four. It constructs
an adapted command for :c:func:`rbcGrid.Rbc_GridOp` and its handlers, where the operation is at index two. The temporary
original option/value slice used by :c:func:`rbcGrid.ConfigureGrid` is borrowed only for that configuration call.
Neither the adapter nor the tick transaction transfers ownership of Tcl command arguments.

:c:func:`rbcGrid.DrawTrackGrids` takes either a screen drawable or a non-NULL export context. In the export case the
drawable is ignored; in the screen case the destination remains borrowed. Both paths consume prepared segment caches,
restore temporary track viewports and end renderer contexts without finalizing a document. A new configuration must be
mapped before relying on these caches.
