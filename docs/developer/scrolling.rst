Scrolling and viewport policy
=============================

Scrolling separates request parsing from the owner's coordinate system and
redraw policy. ``rbcScroll.c`` provides shared request and integer-offset helpers.
Axis data scrolling lives in ``rbcGrAxis.c``; track-stack scrolling and its
scrollbar notification live in ``rbcGrTrack.c``. See :doc:`mapping` for the
resulting screen transforms and full versus visible track rectangles.

Two parsing interfaces
----------------------

``Rbc_ParseScrollRequest`` parses a request into ``Rbc_ScrollRequest`` without
modifying graph state. It accepts finite double values for moveto, units and
pages, including fractional unit counts. It validates the complete argument
shape before reading optional values and assigns its output only on success.
It does not clamp fractions to [0,1] or apply viewport bounds. A query with no
request arguments is handled by the owning command, not this parser.

Axis commands enable legacy mode: nonempty abbreviations and a single numeric
unit count are accepted. Track commands use exact ``moveto``, ``scroll``,
``units`` and ``pages`` words. The caller supplies a valid starting argument
index and array; the helper does not validate arbitrary C pointer/index misuse.

``Rbc_GetScrollInfoFromObj`` is an older integer-offset interface. It parses
integer counts for units/pages, a finite double for moveto and legacy numeric
unit requests. It truncates the computed delta before adding it, saturates
integer conversions and applies ``Rbc_AdjustViewport``. Errors leave the input
offset unchanged, but malformed arity does not always produce a diagnostic.
It is not interchangeable with the double-valued axis/track parser.

Integer viewport modes
----------------------

``Rbc_AdjustViewport`` assumes meaningful nonnegative world/window sizes and
positive scroll units. It uses wide intermediate arithmetic and returns an
integer offset. Mode policy is intentionally distinct:

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Mode
     - Offset policy
   * - Canvas
     - Keep a full viewport inside a larger world. If the world is smaller,
       allow offsets from worldSize-windowSize through zero, moving the small
       world within the viewport.
   * - Listbox
     - Permit trailing blank space. When the offset reaches/exceeds worldSize,
       replace it by worldSize-scrollUnits, then clamp negative values to zero.
   * - Hierbox
     - Keep the viewport inside the world and clamp to zero when the world fits.

The integer parser defines a page as 90 percent of the visible window. Unknown
mode values leave the supplied offset unchanged; this is not an enum-validation
interface.

Axis data scrolling
-------------------

Axis scrolling derives an effective world range from data bounds, overridden by
configured scroll limits. Uninitialized data bounds fall back to current axis
limits. It rejects invalid world ranges, but allows a zero-width view for an
unused axis. The current view is intersected with the scroll region before
normalization; an entirely outside view collapses onto the nearest boundary.

Logarithmic axes perform viewport arithmetic in log10 coordinates and convert
requested limits back afterwards. Screen orientation and the descending flag
determine which end corresponds to fraction zero. Units use axis scroll units
and the relevant screen scale; a track-owned axis uses its full world track
extent rather than the clipped visible intersection.

A query returns clamped scrollbar fractions. Movement preserves a zero-width
unused view as a no-op, otherwise applies the request, clamps the normalized
viewport and validates the resulting finite increasing limits. Only then are
``reqMin`` and ``reqMax`` committed and axis layout/redraw flags scheduled.
This changes data limits, not the track-stack offset.

Track stack versus track-local data
-----------------------------------

Track-stack view fractions describe the plot viewport within the total stacked
track extent. The stacking direction uses vertical range normally and horizontal
range when inverted. Movement uses 20 display units per unit request and 90
percent of the visible range per page; moveto is relative to the total extent.
The offset is clamped to the available extent, or forced to zero when track
scrolling is disabled. It requests world reset/redraw without changing data-axis
limits.

``Rbc_TrackYViewOp`` instead reuses axis data scrolling. ``-mapy`` selects one axis
that must belong to the track. Without it, a query reports the first axis and a
movement applies to all track Y axes. Parsing precedes layout. The operation
saves all selected requested limits and graph flags before applying updates;
if any axis rejects the move, it restores the limits and flags while retaining
``REDRAW_PENDING`` because an idle callback may already be scheduled. This is
rollback of requested axis state, not rollback of arbitrary layout callbacks.

Scrollbar callbacks and lifetime
--------------------------------

Axis scrollbar updates are queued through an idle callback, using the latest
stored fractions. Track-stack notification happens after native drawing through
``Rbc_NotifyTrackView``. That helper first repicks a stationary pointer so track
Enter/Leave bindings reflect changed layout, then checks the surviving window
before reporting fractions. Neither notification path emits export-driven
scrollbar updates.

The track scrollbar compares fractions with its cache and updates that cache
before evaluating the command prefix, preventing an unchanged callback-triggered
redraw from notifying forever. Reconfiguring the command invalidates the cache.
Callbacks receive two appended fraction arguments and execute globally. Their
command objects and required lifetimes are retained across evaluation;
interpreter state is saved/restored and failures are background exceptions.

Callbacks can destroy or reconfigure the widget. Preserving the allocation does
not guarantee a live Tk window or unchanged axes/tracks. Callers must respect
the post-callback checks and avoid using graph state after its final release.
See :doc:`graph-lifecycle` and :doc:`bindings` for the surrounding lifetime and
repicking rules, and :doc:`scroll-reference` for the shared contracts.
