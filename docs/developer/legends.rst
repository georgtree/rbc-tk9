Legend ownership and layout
===========================

``rbcGrLegd.c`` implements the global legend, optional external legend window
and track-local legends. They share entry measurement and drawing machinery,
but differ in ownership, placement and event routing. ``rbcGrLegd.h`` exposes
an opaque legend type and shared services. See :doc:`legend-reference` for
selected contracts.

Entries and geometry
--------------------

A legend presents elements from the graph's display list rather than owning
duplicate element records. An element without a label has no entry. A local
legend additionally filters for elements belonging to its track; the global
legend does not apply that track filter. Plot visibility and legend membership
are separate concerns, so do not remove an entry merely because its trace is
not currently painted.

``Rbc_MapLegend`` resets its cached entry counts and dimensions before measuring
labels, symbols, borders and padding. Hidden legends or unusable available
sizes yield no displayed layout. An external legend can use its actual window
dimensions instead of the supplied plot dimensions.

Single-row layouts use individually measured entry widths. Multi-row layouts
retain uniform cell sizing based on the maximum entry size. Top and bottom
placement can select a single row when its summed widths fit. Drawing and
picking must agree on these widths; using a uniform column index for a compact
single row would select the wrong element.

Mapping computes dimensions, while ``SetLegendOrigin`` computes placement.
Local legends are mapped after track rectangles are available and do not add
graph margins. Their available size comes from the full track world rectangle,
so partial scrolling must not reflow the legend into only the visible remnant.
See :doc:`mapping` for full and visible track geometry.

Drawing and picking
-------------------

Normal legend drawing uses mapped entries, positions the legend and draws
symbols through the concrete element's symbol callback. Legend samples are
separate from plotted element geometry. Screen drawing and export have separate
entry points; track export wraps the local legend in a visible-track clip.
Track dispatch also selects the requested raised or non-raised layer and skips
collapsed visible track regions.

``LegendContains`` checks hidden state, mapped entries and legend bounds. For
local legends it first restricts the point to the visible track rectangle.
``LegendAtPoint`` searches raised legends before non-raised legends, checking
the global in-widget legend before local legends within each layer. An external
legend is excluded from this in-widget overlap search.

Binding dispatch returns the selected element as the object and the owning
legend as context. The context matters: the same element may have an entry in
both global and local legends. See :doc:`bindings` for generic dispatch and
callback lifetime.

Local legends share ``graphPtr->trackLegendBindings`` for event routing, created
when first needed. Each local legend owns a tag table providing separate binding
identities. Destroying one local legend removes its tag bindings but must not
destroy the shared binding table while other local legends still use it.
The global legend has its own binding table.

External window and redraw
--------------------------

An external global legend has its own window, command and event handler.
``Rbc_RedrawExternalLegend`` schedules its independent idle draw when it exists
and its resources are still valid. ``EventuallyRedrawLegend`` coalesces requests
using ``REDRAW_PENDING``; scheduling is not immediate painting.

The idle callback clears the pending flag, checks window existence, and for an
external legend remaps when its actual dimensions differ from cached legend
dimensions. It skips unmapped windows and windows too small to draw. Changes
to graph layout alone do not substitute for this external redraw path.
Font-change handling resets legend text styles and also requests external
redisplay when applicable.

An external legend window is owned by the legend machinery. Final destruction
removes its event handler and instance-data association, deletes its command,
and destroys the window after clearing stored handles. Code that changes
external placement must account for all these associations, not just the
``Tk_Window`` pointer.

Resource teardown
-----------------

``Rbc_ReleaseLegendTkResources`` visits local and global legends while the graph
window remains valid. It cancels pending idle callbacks, releases the text
style's graphics context and the separately allocated shadow color, then frees
Tk option resources and the option table. Release guards prevent duplicate
cleanup. In-widget legends stop retaining the graph's window pointer; an
external window has its own remaining lifetime.

Final destruction frees legend records and binding state. Local destruction
clears the track's legend pointer and removes its tag keys. Global destruction
also destroys the shared track-legend binding table, so local legends must be
cleaned up before that shared routing infrastructure disappears. See
:doc:`graph-lifecycle` for the enclosing teardown sequence.

Regression coverage should include differently sized labels in one row,
multi-row picking, entries appearing in both global and local legends,
raised-layer overlap, partial track scrolling, external window resize and
pending redraw cancellation during destruction. Export checks should cover
local clips as well as restoration after a temporary resized layout.
