Graph lifecycle and redisplay
=============================

``rbcGraph.c`` owns the common widget lifecycle for graph, barchart, stripchart,
polar and trackgraph. Components own their data and mapped geometry; renderers
consume that geometry. All of this work runs on the owning Tk thread.
The :doc:`graph-reference` records the contracts for the entry points below.

Creation and ownership
----------------------

``NewGraph`` validates the command arguments and delegates to ``CreateGraph``.
Creation allocates a zeroed ``Graph``, creates the Tk window, initializes tables
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

Expose and configure events use ``EventuallyRedrawSettledGraph``. It replaces
an ordinary pending display with ``SettleGraphRedraw``, which samples the widget
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

``Rbc_GraphInstCmdProc`` preserves the graph around operation dispatch. It also
rejects snapshot/export rendering from an active formatter before temporary
output dimensions can disturb the outer layout. Idle drawing has no such
command scope: ``DisplayGraph`` separately preserves the graph and interpreter
and saves/restores interpreter state around ``DisplayGraphFrame``.

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

``DestroyGraph`` then destroys markers and elements before tracks, axes and
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
