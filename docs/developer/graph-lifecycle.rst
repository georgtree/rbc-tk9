Graph lifecycle and redisplay
=============================

``rbcGraph.c`` owns the common widget lifecycle for graph, barchart, stripchart,
polar and trackgraph. Components own their data and mapped geometry; renderers
consume that geometry. All of this work runs on the owning Tk thread.
The :doc:`graph-reference` records the contracts for the entry points below.

Creation and ownership
----------------------

:c:func:`rbcGraphCommands.NewGraph` validates the command arguments and delegates to :c:func:`rbcGraphLifecycle.CreateGraph`.
Creation allocates a zeroed :c:type:`Graph`, creates the Tk window, initializes tables
and display lists, selects the widget class and installs instance data. A
trackgraph uses line-element class behavior with its separate ``trackGraph``
flag. Its public Tk class remains ``Trackgraph``.

The widget command and event handler are installed before component creation
and initial configuration. Thus geometry requests and queued redisplay from
configuration participate in the normal destruction path if later setup fails.
Pens, axes, export options, crosshairs, legend and grid precede graph
configuration, since configuration uses their resources. The binding table is
created after successful configuration.

The graph record owns its component registries, drawing resources and backing
pixmap. ``tkwin`` and ``interp`` identify the associated Tk/Tcl objects; holding
those pointers is not a lifetime guarantee. Components can be deleted while
the graph record remains alive.

Widget option transactions
--------------------------

The widget itself uses ``Tk_InitOptions`` with its real window. Unlike axes,
tracks and export components, it does not create a synthetic named component
for resource-database lookup. Initialization and derived configuration are
separate: defaults may have been installed successfully while their retained
values still need conversion. Initial configuration therefore runs even with
an empty explicit option list and forces the initialization masks.

Tk owns the option objects and ordinary option resources. RBC retains several
values as objects and converts them into separate live fields:

.. list-table:: Derived option candidates
   :header-rows: 1
   :widths: 24 38 38

   * - Candidate
     - Stored result
     - Ownership at commit
   * - Distances
     - Borders, margins, halo and requested size
     - Copy integers selected by a staged mask.
   * - Padding
     - X/Y two-sided plot padding
     - Copy only staged axes.
   * - Bar mode
     - Internal mode enumeration
     - Copy the staged mode; normal aliases infront.
   * - Shadow
     - Title-shadow color and offset
     - Move the acquired color, then release the old shadow.
   * - Tile
     - Tile client or NULL
     - Move the client, register the callback, then release the old client.

The transaction context borrows the original option/value vector. It must have
an even length and remain valid throughout configuration; it is cleared before
return or rollback. Canonical option lookup recognizes aliases and unambiguous
abbreviations. Each prepare helper first determines which options were supplied
explicitly. On initial configuration, only unoverridden defaults are parsed.
It then processes every explicit occurrence in caller order, rather than only
the last value retained by Tk. An invalid earlier occurrence is consequently
not concealed by a later valid occurrence.

Scalar candidates own no resources. Shadow and tile staging acquire a new
candidate before releasing an earlier one, and prepare failure cleans up that
candidate. An empty tile is a staged NULL, distinct from an unstaged value.
Commit transfers resource ownership and disarms the candidate; cleanup must
never release a successfully transferred resource a second time.

:c:func:`rbcGraphOptions.ConfigureGraphOptions` applies Tk options with saved-option support, then
calls :c:func:`rbcGraphOptions.ConfigureGraph` to prepare derived values. That function validates
track options and renderer availability, rejects nonfinite aspect, bar width
and baseline, and prepares the selected candidate families before committing
any of them. All reportable retained-value conversion failures precede this
commit boundary. Failure returns to the wrapper to restore Tk-managed fields.
Success frees the saved options and marks initial configuration complete.

This is not a snapshot of every graph field. For example, track-option
validation can invalidate the cached scrollbar fraction before a later
validation fails. That notification bookkeeping is outside both Tk's saved
options and the five candidate records. Likewise, candidate preparation is
not a general lifetime barrier around external resource-provider callbacks.
Code extending configuration must review those boundaries explicitly.

After commit, configuration normalizes nonpositive bar width to 0.1, updates
requested window geometry and internal border, rebuilds graph GCs and text
style, and reconfigures crosshairs. Inversion adjusts physical axis pointers
and marks ranges dirty. Layout and representation masks request world
recomputation; plot background, renderer and separator changes invalidate
backing pixels as appropriate. Successful configuration always requests a
redraw. A renderer change additionally requests remapping because automatic
decimation density depends on the renderer.

Polar's default aspect policy is tracked separately from its public numeric
aspect value. An option-database aspect disables automatic data-unit aspect
at creation. A successful explicit aspect option also disables it; a failed
configuration does not perform that success-only transition.

Option teardown must occur while the Tk window is valid. It clears the
borrowed context, frees Tk-owned options and deletes the option table, and is
safe to repeat. Derived GCs, title shadow and tile are released by graph
destruction instead. Partial initialization undefines the window cursor before
freeing its option resource, so the window does not retain a freed cursor.

Redraw requests and dirty state
-------------------------------

:c:func:`Rbc_EventuallyRedrawGraph` coalesces requests with ``REDRAW_PENDING``.
It schedules work, but does not itself invalidate mapped geometry or the
backing store. A component mutator must set the appropriate dirty flags first.
Scheduling does not require a mapped window; presentation checks mapping later.
An active axis formatter also makes a redraw request a cancellation signal
for the current layout.

.. list-table:: Common graph flags
   :header-rows: 1
   :widths: 32 68

   * - Flag
     - Role
   * - ``RESET_AXES``
     - Recompute axis ranges before layout/mapping.
   * - ``LAYOUT_NEEDED``
     - Recompute margins; formatting may evaluate Tcl.
   * - ``MAP_ALL``
     - Request remapping of components rather than only dirty items.
   * - ``MAP_WORLD``
     - Combination of MAP_ALL, RESET_AXES and GET_AXIS_GEOMETRY.
   * - ``REDRAW_BACKING_STORE``
     - Invalidate cached plot pixels independently of geometry.
   * - ``REDRAW_WORLD``
     - Combination of DRAW_MARGINS and DRAW_LEGEND.
   * - ``RESET_WORLD``
     - Combination of REDRAW_WORLD and MAP_WORLD; it does not include
       REDRAW_BACKING_STORE or LAYOUT_NEEDED.
   * - ``REDRAW_PENDING``
     - An ordinary or geometry-settled display request is outstanding.
   * - ``GRAPH_CHANGED``
     - Retain a native view/element change until a completed screen display
       can queue ``<<RbcGraphChanged>>``. Marker-only updates must not set it.
   * - ``GRAPH_EXPORT``
     - Distinguish temporary export layout from an on-screen view change.

Expose and configure events use :c:func:`rbcGraphLifecycle.EventuallyRedrawSettledGraph`. It replaces
an ordinary pending display with :c:func:`rbcGraphLifecycle.SettleGraphRedraw`, which samples the widget
and its Tk ancestors through the toplevel. Presentation waits for two successive
unchanged geometry samples. This coalesces nested geometry-manager work without
entering a recursive event loop; it does not wait for arbitrary application
timers or future geometry changes. Focus changes use ordinary redraw scheduling.

Layout and cancellation
-----------------------

:c:func:`Rbc_LayoutGraph` rejects recursive layout from an axis formatter,
resets the cancellation flag, checks the window, and processes pending work:

1. Retain a screen view-change indication, except during export.
2. Reset axis ranges when requested.
3. Compute margins when needed, then validate callback state.
4. If both plot ranges exceed one pixel, map axes as required, then elements,
   markers, grid and local legends; consume the remapping flags.

A successful return with an unusably small plot does not imply that all mapping
flags were consumed. An error does not roll back partial layout. Callers must
stop before using cached component pointers after cancellation.

:c:func:`Rbc_GraphCallbackValid` checks that ``tkwin`` still exists and that
``axisFormatInvalid`` is false. It reports ``RBC GRAPH CALLBACK`` on failure.
It is a specific axis-format/window guard, not a general validation of every
component pointer. Preservation and validation serve different purposes:

* ``Tcl_Preserve`` delays final freeing of the graph record.
* Revalidation prevents use of window resources or layout invalidated by Tcl.

:c:func:`rbcGraphCommands.Rbc_GraphInstCmdProc` preserves the graph around operation dispatch. It also
rejects snapshot/export rendering from an active formatter before temporary
output dimensions can disturb the outer layout. Idle drawing has no such
command scope: :c:func:`DisplayGraph` separately preserves the graph and interpreter
and saves/restores interpreter state around :c:func:`DisplayGraphFrame`.

Frame drawing and presentation
------------------------------

The frame callback consumes ``REDRAW_PENDING`` first. It returns if the window
is gone, an axis formatter is active, or an element has a pending vector
notification. Waiting for vector notification avoids drawing from vector data
whose corresponding geometry has not yet caught up.

It samples the actual Tk dimensions, requests world reset on a size change,
and lays out. An unmapped widget updates crosshair geometry without presenting.
For a mapped widget, :c:func:`Rbc_DrawGraph` draws already-mapped content.

The plot backing store and full-frame double buffer have different roles:

* The backing store caches the plot background, separators, grids, polar labels,
  under-markers, lowered legends, axis limits and ordinary elements.
* The frame adds above-markers, active elements, requested margins, raised
  legends, widget border and focus highlight.
* Crosshairs are not part of the backing store. Double-buffered presentation
  composes them into the final pixmap and presents once. Direct drawing erases
  old crosshairs, updates their geometry, draws the graph and enables them again.

The drawing path checks cancellation after callback-capable plot drawing.
A cancelled frame is not presented as a completed buffered frame. Direct drawing
can already have changed window pixels; it is not transactional.

After a completed frame, usable screen geometry can trigger the queued
``<<RbcGraphChanged>>`` event and axis-change notifications. Margin-variable
updates and track-view notification follow. The virtual event is queued, not
executed synchronously at the point it is sent. Tcl variable traces and other
notification callbacks remain reentry boundaries in their own right.

Destruction
-----------

Deleting the Tcl widget command and destroying the Tk window converge on the
same lifecycle, with two distinct cleanup phases:

1. While the Tk window is valid, release option-owned Tk resources. The command
   deletion path first undefines the borrowed cursor: destroying a not-yet-created
   window may realize it, so a freed cursor must not remain installed. Clear
   ``tkwin`` before a recursive command/window deletion can repeat this work.
2. ``DestroyNotify`` cancels both ordinary and settled display idle callbacks,
   frees geometry-settle state, and uses ``Tcl_EventuallyFree`` to request final
   graph disposal. Outstanding preserves defer that final disposal.

:c:func:`DestroyGraph` then destroys markers and elements before tracks, axes and
pens; releases remaining components, bindings and drawing resources; and frees
the record. Keeping that order matters because components refer to other
components. Do not move Tk-dependent resource release into the final free
callback: the Tk window can already be gone when it runs.

Contributor checks
------------------

When changing this code, exercise destruction from format callbacks, creation
failure, nested resize, vector updates, snapshots and resized exports. Check
both direct and buffered drawing, track reorder/scroll, and stationary-pointer
repicking. Windows crosshairs additionally depend on preserving the single
presentation step; separating erase and redraw can reintroduce flicker.
These are behavioral checks for future code changes; this documentation change
only adds contracts and prose.

Widget commands and geometry queries
------------------------------------

The instance dispatcher resolves an operation and validates its argument count
before preserving the graph allocation around the handler. Component commands
retain their own dispatchers. Explicit bar, line and strip operations supply a
class identifier; the generic element operation supplies the graph's default
class. Logical X/X2/Y/Y2 operations select their physical margins according to
inversion before entering the margin-axis dispatcher.

Preservation delays freeing the graph record, not destruction of its window or
component registries. A handler which evaluates Tcl cannot infer component
validity from that preservation alone. The dispatcher blocks snapshot and
document-output recursion during axis label formatting before those operations
can alter dimensions. It also rejects widget-wide transform and invtransform
on trackgraphs: callers must select a track through the track interface.

Geometry queries do not all have the same freshness guarantee:

.. list-table:: Widget geometry query behavior
   :header-rows: 1
   :widths: 24 42 34

   * - Operation
     - Preparation
     - Result
   * - extents
     - Reads cached geometry without layout.
     - Scalar size or x/y/width/height; plot sizes include both endpoints.
   * - inside
     - Parses Tk screen distances and reads graph extents.
     - Boolean for the overall plot region, not a track selection.
   * - transform
     - Evaluates expressions, synchronizes vectors and resets dirty ranges.
     - Rounded integer screen coordinates on the first logical X/Y pair.
   * - invtransform
     - Same expression and range preparation.
     - Double data coordinates on that axis pair.

Range synchronization is not full margin layout. Widget transforms can therefore
use scales from an earlier layout while other changes are pending. Each Tcl
expression is followed by a widget-validity check before the next expression
or graph-resource access. Inputs must be finite. Forward mapping rounds in
double precision and checks the integer range before conversion; ordinary
coordinate rounding remains unchanged.

Screen composition and snapshot capture
---------------------------------------

:c:func:`rbcGraphCommands.DrawPlotRegion` constructs the cacheable layer: background, track separators,
grids, Polar/Smith labels, under markers, lowered legends, axis-limit annotations
and ordinary elements. It checks callback validity after the Polar label stage.
Its caller must also check validity because a void drawing return does not
communicate cancellation. :c:func:`Rbc_DrawGraph` adds above markers, active elements,
margins, raised legends, outer border and focus decoration around that layer.

Margin drawing sets a shared tile origin and fills four rectangles surrounding
the plot. Nonpositive rectangles are skipped at the fill helper. The plot
border, margin legend, graph title and axes follow. These helpers consume mapped
geometry and borrowed renderer resources; they do not lay out or present a frame.

A photo snapshot is a new rendering into a temporary pixmap, not a readback of
the displayed window. Width and height are parsed as integers; values below two
use the current Tk dimension. The handler assigns those dimensions to the graph,
requests layout, marks the world dirty and draws without the element backing
store. Callback cancellation releases the pixmap before returning an error.
The normal photo path captures into the named photo and releases the pixmap.

On Windows the same command also supports EMF and WMF. It records drawing into
an enhanced-metafile DC, then copies an EMF or converts the recording to a
placeable WMF file. The special CLIPBOARD target requests system ownership of
the enhanced metafile. These are native recording paths, separate from the
renderer-independent document exporters.

The snapshot wrapper saves graph dimensions and existing flags before capture.
It marks temporary rendering as export work, forces layout at the requested
size, then restores dimensions and merges dirty flags on every return path.
A surviving, uncancelled widget is immediately laid out at its original size;
a later redraw also invalidates backing pixels. Existing pending-idle flags
are retained. Capture errors survive restoration, while a restoration failure
is reported when capture itself succeeded. Destroyed windows are never used
for restoration or redraw scheduling.

Margin reporting copies configured variable names and cached sizes before the
first traced write. Reconfiguration cannot invalidate those names mid-loop.
After each write it checks whether the widget survives and layout remains
valid, stopping both further writes and the subsequent track-view notification
on cancellation. Assignment errors continue to be ignored for compatibility.

Construction helpers and notification ownership
-----------------------------------------------

:c:func:`rbcGraphLifecycle.InitPens` initializes the registry and creates activeLine before activeBar.
A failure leaves already-created resources for the enclosing graph teardown;
it is not an independently repeatable initialization operation. The successful
constructor creates bindings only after all components and widget options have
been configured. Trackgraph selection is initially a distinct constructor UID,
then becomes a line-element class plus the trackGraph flag on the instance.

:c:func:`rbcGraphLifecycle.AdjustAxisPointers` changes only which logical chains each physical margin
borrows. It does not change axis classes or sites, relink axes or acquire
references. Configuration separately invalidates ranges after inversion.
Likewise, the tile callback only requests margin redisplay: despite its former
comment, it does not rebuild a graphics context. Tile-client replacement and
callback registration belong to the option transaction.

Tk's world-change hook refreshes title metrics and component font resources
without replaying configuration. It ignores partially configured graphs and
requests both geometry recomputation and backing-store invalidation once live.
This separates named-font changes from ordinary option parsing and ownership.

The geometry-settling record owns its sample array but only borrows window
identities. Each idle pass walks the current ancestry and compares identity,
position, size and border width. It never dereferences a previously sampled
window handle. Two consecutive unchanged passes are required after the latest
change. A pending settle request absorbs later ordinary redraw requests via
REDRAW_PENDING. Freeing the sampling record alone does not cancel an idle call;
destruction must cancel scheduling separately before releasing it.

:c:func:`rbcGraphLifecycle.Rbc_GetGraphFromWindowData` searches the supplied window and its ancestors
for RBC instance data. The result is borrowed, with no added preservation or
class check. This supports graph descendants but is not a generic checked
conversion of arbitrary RBC instance data into a graph.

The parent window class and anonymous option proxy are established before any
Graph storage or registries are allocated. If proxy creation fails, the parent
window is destroyed directly while the original interpreter error is preserved.
No deferred Graph destructor is needed on that path. After Graph allocation,
instance data, command and event-handler setup establish the normal ownership
path before fallible component initialization begins.

Windows metafile layout and ownership
-------------------------------------

:c:func:`rbcWindowsSnapshot.InitMetaFileHeader` writes the packed placeable-WMF
header with zero origin, horizontal width in Right and vertical height in Bottom.
Both axes use their own screen pixel/millimeter ratio and 1440 units per inch.
Bounds must fit positive signed 16-bit values; invalid dimensions fail before
header output is committed. The checksum XORs the ten preceding little-endian
16-bit words, as specified by Microsoft's
`META_PLACEABLE record <https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-wmf/828e1864-7fe7-42d8-ab0a-1de161b32f27>`_.

:c:func:`rbcWindowsSnapshot.CreateAPMetaFile` completes native conversion before
opening the destination. A zero size, allocation/lock failure or incomplete
conversion returns an error without truncating an existing file. Both writes
check their byte count. A write failure can leave a partial file; the helper
releases its file and temporary memory but borrows the recording and reference DC.
The file path still uses the native ANSI API, as does EMF copying.

The snapshot command builds the EMF description as application-name/NUL,
picture-name/NUL, followed by the DString terminator. Reference-DC acquisition,
recording completion and EMF copy failures now produce diagnostics. Clipboard
ownership transfers only when SetClipboardData succeeds; otherwise RBC deletes
the recording. These checks preserve the existing snapshot wrapper's screen
geometry restoration and do not alter the SVG/PDF/PostScript exporters.
