Legend ownership and layout
===========================

``rbcGrLegd.c`` implements the global legend, optional external legend window and track-local legends. They share entry
measurement and drawing machinery, but differ in ownership, placement and event routing. ``rbcGrLegd.h`` exposes an
opaque legend type and shared services. See :doc:`legend-reference` for the complete function reference.

Entries and geometry
--------------------

A legend presents elements from the graph's display list rather than owning duplicate element records. An element
without a label has no entry. A local legend additionally filters for elements belonging to its track; the global legend
does not apply that track filter. Plot visibility and legend membership are separate concerns, so do not remove an entry
merely because its trace is not currently painted.

:c:func:`rbcGrLegendLayout.Rbc_MapLegend` resets its cached entry counts and dimensions before measuring labels,
symbols, borders and padding. Hidden legends or unusable available sizes yield no displayed layout. An external legend
can use its actual window dimensions instead of the supplied plot dimensions.

Single-row layouts use individually measured entry widths. Multi-row layouts retain uniform cell sizing based on the
maximum entry size. Top and bottom placement can select a single row when its summed widths fit. Drawing and picking
must agree on these widths; using a uniform column index for a compact single row would select the wrong element.

Mapping computes dimensions, while :c:func:`rbcGrLegendLayout.SetLegendOrigin` computes placement. Local legends are
mapped after track rectangles are available and do not add graph margins. Their available size comes from the full track
world rectangle, so partial scrolling must not reflow the legend into only the visible remnant. See :doc:`mapping` for
full and visible track geometry.

Drawing and picking
-------------------

Normal legend drawing uses mapped entries, positions the legend and draws symbols through the concrete element's symbol
callback. Legend samples are separate from plotted element geometry. Screen drawing and export have separate entry
points; track export wraps the local legend in a visible-track clip. Track dispatch also selects the requested raised
or non-raised layer and skips collapsed visible track regions.

:c:func:`rbcGrLegendBindings.LegendContains` checks hidden state, mapped entries and legend bounds. For local legends it
first restricts the point to the visible track rectangle. :c:func:`rbcGrLegendBindings.LegendAtPoint` searches raised
legends before non-raised legends, checking the global in-widget legend before local legends within each layer. An
external legend is excluded from this in-widget overlap search.

Binding dispatch returns the selected element as the object and the owning legend as context. The context matters: the
same element may have an entry in both global and local legends. See :doc:`bindings` for generic dispatch and callback
lifetime.

Local legends share ``graphPtr->trackLegendBindings`` for event routing, created when first needed. Each local legend
owns a tag table providing separate binding identities. Destroying one local legend removes its tag bindings but must
not destroy the shared binding table while other local legends still use it. The global legend has its own binding
table.

External window and redraw
--------------------------

An external global legend has its own window, command and event handler.
:c:func:`rbcGrLegendLifetime.Rbc_RedrawExternalLegend` schedules its independent idle draw when it exists and its
resources are still valid. :c:func:`rbcGrLegendLifetime.EventuallyRedrawLegend` coalesces requests using
``REDRAW_PENDING``; scheduling is not immediate painting.

The idle callback clears the pending flag, checks window existence, and for an external legend remaps when its actual
dimensions differ from cached legend dimensions. It skips unmapped windows and windows too small to draw. Changes to
graph layout alone do not substitute for this external redraw path. Font-change handling resets legend text styles and
also requests external redisplay when applicable.

An external legend window is owned by the legend machinery. Final destruction removes its event handler and
instance-data association, deletes its command, and destroys the window after clearing stored handles. Code that changes
external placement must account for all these associations, not just the ``Tk_Window`` pointer.

Resource teardown
-----------------

:c:func:`rbcGrLegendLifetime.Rbc_ReleaseLegendTkResources` visits local and global legends while the graph window
remains valid. It cancels pending idle callbacks, releases the text style's graphics context and the separately
allocated shadow color, then frees Tk option resources and the option table. Release guards prevent duplicate
cleanup. In-widget legends stop retaining the graph's window pointer; an external window has its own remaining lifetime.

Final destruction frees legend records and binding state. Local destruction clears the track's legend pointer and
removes its tag keys. Global destruction also destroys the shared track-legend binding table, so local legends must be
cleaned up before that shared routing infrastructure disappears. See :doc:`graph-lifecycle` for the enclosing teardown
sequence.

Regression coverage should include differently sized labels in one row, multi-row picking, entries appearing in both
global and local legends, raised-layer overlap, partial track scrolling, external window resize and pending redraw
cancellation during destruction. Export checks should cover local clips as well as restoration after a temporary resized
layout.

Record and construction boundaries
----------------------------------

:c:struct:`LegendStruct <rbcGrLegendLifetime.LegendStruct>` owns configuration and cached geometry, not a separate list
of entry objects. Its graph and optional track pointers are borrowed. :c:type:`Element` records, labels and symbol
methods stay with the graph. The distinction matters when an element appears in both a global and local legend: there
are two layout contexts, but only one element and one ``LABEL_ACTIVE`` flag.

.. list-table:: Ownership by legend kind
   :header-rows: 1

   * - Resource
     - Global legend
     - Track-local legend
   * - Owner pointer
     - ``graphPtr->legend``
     - ``track->legend``
   * - Binding table
     - Owned by the legend
     - Borrowed ``graphPtr->trackLegendBindings``
   * - Interned tag strings
     - Graph element tag table
     - Private legend tag table
   * - Drawing window
     - Borrowed graph window or owned external window
     - Borrowed graph window
   * - Option resources and derived shadow/text resources
     - Owned by the legend
     - Owned by the legend

:c:func:`rbcGrLegendConfiguration.Rbc_CreateLegend` initializes the ``legend`` / ``Legend`` option-database
component. :c:func:`rbcGrLegendConfiguration.Rbc_CreateTrackLegend` initializes ``trackLegend`` / ``TrackLegend`` and
lazily creates the graph-owned local binding table. Both constructors install their owner pointer before initializing
options, so failure cleanup must clear it. A local creation failure retains the shared table for the graph. They assume
no existing legend at their destination; they are not replacements.

Local option specifications inherit global styling and override defaults to hidden, plotarea, northeast anchor and
raised. These defaults keep local legends opt-in. A shared option table definition does not imply shared configured
values: each legend has its own record and Tk option resources.

Configuration transaction
--------------------------

:c:func:`rbcGrLegendCommands.ConfigureOp` handles both option queries and updates. Updates install candidate fields with
``Tk_SetOptions`` and retain ``Tk_SavedOptions``. :c:func:`rbcGrLegendConfiguration.ConfigureLegend` then validates and
stages derived state:

1. Check nonnegative entry/outer border widths and requested row/column counts.
2. Parse interior and exterior padding into temporary structures.
3. Acquire a candidate shadow color and parse the requested position.
4. Prepare a new external window and command if required, while the existing external window is still intact.
5. Commit padding/shadow and placement, reset text style as needed, and request graph layout and/or legend redisplay
   according to the option mask.

Failure frees staged resources; the command wrapper restores saved Tk fields and preserves the diagnostic. Success frees
saved options. Keeping fallible validation before commit prevents restored Tcl options from describing stale derived
resources. This is ordinary configuration rollback, not isolation from application side effects during Tcl evaluation or
Tk window destruction.

Global placement accepts nonempty keyword prefixes, an empty value selecting rightmargin, a dot-prefixed external window
path, or ``@x,y``. The two coordinate parts are Tcl long expressions, not merely decimal literals. Parsing them can
execute Tcl. The temporary ``LegendPosition.windowName`` borrows the option object's string and must not outlive
it. Local placement uses exact names ``plotarea``, ``left``, ``right``, ``top`` and ``bottom``; it rejects coordinates
and external windows.

Configuration and nested track dispatch borrow their owners across operations that can invoke application code. They do
not themselves establish a preservation boundary around arbitrary coordinate-expression or layout callbacks. Do not
infer callback deletion safety from successful option rollback.

Detailed layout rules
----------------------

Measurement, drawing, export and picking traverse the display list from its last link backwards. All use the same
label/track membership filter. A hidden trace can still have an entry; the legend's own hidden flag suppresses its
layout. Symbol space derives from font ascent, not from the largest plotted symbol. The cell inset includes symbol
space, entry border, internal padding and the fixed label gap.

Layout chooses row/column counts as follows:

.. list-table:: Requested grid dimensions
   :header-rows: 1

   * - Request
     - Selection
   * - Rows only
     - Clamp rows to entry count; derive columns by ceiling division
   * - Columns only
     - Clamp columns to entry count; derive rows by ceiling division
   * - Both
     - Clamp each independently to entry count
   * - Neither, top/bottom
     - Use a compact single row if it fits, otherwise derive rows from available columns
   * - Neither, other sites
     - Derive columns from the rows fitting the available height

The both-specified case does not enlarge an undersized grid to guarantee enough cells. Drawing still walks eligible
entries; do not treat requested row/column product as a validated entry capacity. Single-row finalization always sets
the column count to the number of entries and uses summed individual widths.

Entries fill down a column before advancing to the next. Grid picking computes ``column * nRows + row`` with an overflow
check, while single-row picking subtracts individual widths in traversal order. Empty final cells return no entry. A new
layout path must preserve this order in all four consumers.

Sizing checks use wide arithmetic and reject dimensions that cannot fit an ``int``. Oversized single-row sums saturate
to a sentinel so a multi-row layout can still be considered. :c:func:`rbcGrLegendLayout.GetLegendPixelDimension` checks
the border/padding base before multiplying cell count by cell size. Rejected layouts leave the reset counts and
dimensions empty; they do not raise a Tcl error.

Positioning is separate from measurement. Global margin sites use remaining margin space after axis offsets; top
placement also accounts for the graph title. Explicit negative X/Y values are relative to the graph window extent.
Local sites use full world bounds, anchoring to an edge or the whole track. External placement forces origin zero and
the northwest anchor. The public-to- other-modules accessors simply read cached fields; they do not make layout current.

Picking, tags and activation
----------------------------

There are two distinct questions: whether a legend rectangle covers a point, and whether that point selects an
entry. :c:func:`rbcGrLegendBindings.LegendContains` intentionally counts blank areas for occlusion, while
:c:func:`rbcGrLegendBindings.PickLegendIn` excludes outer border/padding and empty grid cells. A foreground legend's
blank area can therefore prevent picking an underlying legend entry. Local tests also enforce visible track bounds.

:c:func:`rbcGrLegendBindings.LegendAtPoint` checks the raised layer first, global before local within each layer, then
local legends in track-list order. :c:func:`rbcGrLegendBindings.Rbc_PointInTrackLegend` asks whether the *topmost*
legend is local, not whether any local rectangle exists under the point. External windows are outside this in-widget
ordering.

Binding pickers return a borrowed element and legend context. Tag enumeration appends element name, element class and
configured bindtags in that order. The context selects the tag table, which makes identical tag strings independent
between local legends. Global tags use the graph element-tag registry. Merely interning a tag does not mean a binding
script exists, so ``bind`` without arguments lists known tags rather than only tags with scripts.

Activation differs from tag identity: ``activate`` and ``deactivate`` match names in the complete element table and
update shared ``LABEL_ACTIVE`` bits. Local commands filter by track. With no patterns they change nothing and return
the active names in scope. Returned names follow hash iteration, not display order, and can include elements with no
displayed entry. Activating a local entry also changes the state seen by its global entry. Track graphs request
redisplay of other copies, including an external global legend.

``get current`` reads binding state and, for a local legend, verifies its context. ``get @x,y`` performs coordinate
picking. Despite an older source comment, this handler does not look up ordinary element names. Malformed coordinates
return ``TCL_ERROR`` with the coordinate parser's diagnostic, including when the legend is hidden or has no
entries. Valid coordinates with no matching entry return success with an empty result. Both global and local commands
use this contract.

:c:func:`rbcGrLegendBindings.Rbc_LegendRemoveElement` removes object-pointer binding/current-item state from the
relevant tables. It does not remove interned string tags or scripts by name; element deletion and tag lifetime must not
be conflated.

Screen composition and export
-----------------------------

Screen drawing composes a temporary pixmap. An explicit legend background fills it directly. A transparent in-plot or
local legend copies from the backing store when available, otherwise using the plot fill. Local background copies are
bounded to the graph window first. Margin/external backgrounds use the graph's tile with an adjusted origin or its
normal fill.

Each entry draws active or ordinary relief, the concrete element's legend symbol, and its label. ``LABEL_ACTIVE``
selects text state and entry decoration; it does not select a different element record. The outer border uses the legend
border or falls back to the graph border. Local legends copy only the intersection with the visible track. This
final-copy clipping allows full-world placement without painting beyond the visible row.

Export does not capture the temporary screen pixmap. It emits decoration, element symbol methods and text through the
export renderer. The output box subtracts exterior padding from cached dimensions. With decorations disabled it clears
that box; with decorations enabled it draws the configured border and optional background. Screen and document
background composition are therefore not identical algorithms. Track export pushes a visible rectangle clip around the
entire legend export and balances it afterwards. Backend selection remains in the renderer; these callbacks are not
PostScript-only.

Entry loops borrow graph, legend and element data while invoking concrete symbol callbacks. They do not make an
ownership snapshot. Symbol implementations must respect the surrounding drawing lifetime; no arbitrary application
mutation safety should be inferred from double buffering.

External windows and command routing
------------------------------------

:c:func:`rbcGrLegendLifetime.PrepareLegendWindow` creates the window, instance association, exposure/structure event
handler and a command forwarding to :c:func:`rbcGraphCommands.Rbc_GraphInstCmdProc`. The external window command is
consequently a graph dispatcher alias in behavior, not a separate legend-only command. Creation does not pack or map the
window.

Replacing an external window prepares the new one first. Closing the old one cancels its pending redraw, resets stored
handles, removes the handler and instance association, deletes the command, destroys the window and moves bindings back
to the graph before adoption of the replacement.

An application-driven external ``DestroyNotify`` instead restores rightmargin, updates the retained ``-position``
object, moves the binding table and requests full graph layout/backing redraw. Deleting the external command alone is
not the same lifecycle operation: it was created without a command-delete callback.

The idle scheduler only requires a window pointer and no pending request. It does not require a mapped
window. :c:func:`rbcGrLegendLifetime.DisplayLegend` clears the pending bit, checks external size, remaps if needed, then
checks mapping before painting. Plot-area activation must rebuild the graph backing store rather than painting a legend
directly over the window; margin/external activation can use the cheaper legend-only path when no graph redraw is
pending.

Global command dispatch validates operation/arity and calls the handler. Nested track legend dispatch additionally
normalizes its argument vector and refreshes graph layout before calling shared handlers. The local-only ``bbox``
returns ``x y width height`` for the visible intersection, or an empty result when hidden, empty or fully clipped. It
does not return four corner coordinates.

Parameter and ownership boundaries
----------------------------------

The parameters distinguish three coordinate spaces: graph-window pixels for in-widget overlap tests, drawing-window
pixels for individual entry picking, and full track world rectangles for local placement. Picking an external legend
uses its own window's coordinates. None of these helpers accepts data coordinates or a position relative to the legend's
cached origin.

.. list-table:: Output ownership and failure contracts
   :header-rows: 1
   :widths: 35 65

   * - Helper
     - Caller responsibility
   * - :c:func:`rbcGrLegendLayout.GetLegendPixelDimension`
     - Supply required int output storage; failure leaves its old value unchanged.
   * - :c:func:`rbcGrLegendConfiguration.GetLegendPositionFromObj`
     - Supply staging storage and retain the input object while using ``windowName``. Failure can leave partial staging
       values.
   * - :c:func:`rbcGrLegendLifetime.PrepareLegendWindow`
     - Supply both handle outputs; success transfers responsibility to adopt or clean up the window and command together.
   * - :c:func:`rbcGrLegendBindings.LegendTag`
     - Use the returned borrowed key only while its owning tag registry remains alive.
   * - :c:func:`rbcGrLegendBindings.PickLegendEntry`
     - Optional context output receives the global legend even if no entry is returned.
   * - :c:func:`rbcGrLegendBindings.PickTrackLegendEntry`
     - Context output is required and may identify a global occluding legend even when no local entry is returned.

A non-``NULL`` picking context is therefore not proof that an entry was selected. The returned element pointer determines
whether an item was found; the context identifies the scope for subsequent tag collection. Neither result acquires an
element or legend reference.

:c:func:`rbcGrLegendOutput.Rbc_DrawTrackLegends` and
:c:func:`Rbc_ExportTrackLegends <rbcGrLegendOutput.Rbc_ExportTrackLegends>` compare the
requested ``raised`` value directly with the stored flag. Pass zero or one, rather than an arbitrary nonzero
value. Their drawable or export context remains borrowed. The direct :c:func:`rbcGrLegendOutput.Rbc_LegendExport`
call does not supply the track clip itself; that is the layer wrapper's responsibility.

The full nested argument vector passed to :c:func:`rbcGrLegendCommands.Rbc_TrackLegendOp` has its operation at index
four. The adapter creates a normalized vector with the operation at index two, which is what the shared handlers
expect. It also refreshes graph layout before calling them. :c:func:`rbcGrLegendCommands.Rbc_LegendOp` receives an
ordinary full widget command and performs no corresponding layout refresh.

Lifecycle order remains explicit: cancel idle delivery and release Tk resources while the window context is valid;
destroy local legend records while their shared binding table still exists; then destroy the global legend and shared
local binding infrastructure. The private :c:func:`rbcGrLegendLifetime.DestroyLegend` does not clear its owner's
pointer. Use :c:func:`rbcGrLegendLifetime.Rbc_DestroyTrackLegend` or :c:func:`rbcGrLegendLifetime.Rbc_DestroyLegend` at
those ownership boundaries.
