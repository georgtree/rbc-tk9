Crosshair state and presentation
================================

``rbcGrHairs.c`` owns one crosshair record per graph. Its two segments are a
screen-space overlay, not an element, marker or per-track object. The hotspot
is in widget coordinates and the segment lengths follow the graph plot bounds.
See :doc:`crosshair-reference` for the graph-facing services.

Requested and actual visibility
-------------------------------

``hidden`` records the requested option state. ``visible`` tracks whether XOR
hairs have actually been drawn. They need not be logical opposites: requested
hairs can remain absent when the hotspot is outside the plot or the window is
unmapped. Never infer actual pixels solely from the ``-hide`` option.

``TurnOnHairs`` draws only on a mapped window when the hairs are not already
visible and the hotspot is inside the plot. ``TurnOffHairs`` erases visible
hairs on a mapped window by submitting the same segments with the same XOR GC.
The public enable/disable helpers additionally honor ``hidden`` without changing
it. Tcl on/off operations update the requested state separately.

The private GC uses ``GXxor`` with a foreground derived from the requested color
and plot background. A background change therefore needs the GC rebuilt.
Configuration validates replacement values and prepares a replacement GC before
erasing old geometry. Erasing after replacing either the GC or endpoints would
not reliably restore the pixels that were changed originally.

Motion and geometry changes
---------------------------

On Windows, a position-only configuration uses ``MoveCrosshairs``. It computes
old and new visibility, compares segment geometry, and submits old erase and new
draw segments together. Unchanged geometry avoids unnecessary work. This keeps
ordinary pointer motion from acquiring a separate drawing context between the
erase and draw submissions. Changes to style or visibility use the general
configuration path.

``Rbc_UpdateCrosshairs`` only changes segment lengths to match current graph
bounds. It leaves the hotspot and visible state unchanged and performs no
drawing. Old endpoints must remain available until their pixels have either
been erased or replaced by a completed frame.

Trackgraphs still have a graph-wide crosshair record. Track-local transforms
and viewport scopes used by element drawing do not create independent hairs.
Update and presentation belong to the graph's display path after those temporary
scopes have been restored. See :doc:`mapping` for track geometry.

Double-buffered presentation
----------------------------

The double-buffered display path leaves the visible window untouched while
preparing a complete temporary pixmap. The reusable element backing store
contains graph content without crosshairs.

Before updating segment geometry, ``Rbc_CrosshairsNeedFullRedraw`` examines the
old state. Wide strokes, or old visible segments outside the new plot bounds,
can require a full-frame copy. The caller also requests margin drawing so every
pixel of that larger source rectangle has been initialized.

After graph drawing and callback-validity checks,
``Rbc_PresentGraphWithCrosshairs`` updates endpoints, draws requested hairs into
the temporary frame and copies the completed frame to the window. It then
records the new actual visibility. The copy replaces the old XOR pixels; there
is no separate window erase followed by a later window draw. This presentation
path is shared across platforms, while the position-only batching optimization
is Windows-specific.

Do not call the ordinary enable helper after presentation: the hairs are already
part of the copied frame. Likewise, do not pass the reusable backing store or an
export drawable as the temporary frame. This function mutates its source pixmap
and assumes a live mapped destination window.

No Tcl callbacks or event processing may occur between updating the crosshair
geometry and committing visible state. Otherwise subsequent direct movement
could attempt to erase different pixels from those last presented.

Direct drawing and teardown
---------------------------

Without double buffering, display uses the traditional sequence: disable hairs
using old geometry, update segment lengths, draw graph content directly, check
callback validity, then enable hairs. This sequence must not be substituted for
the completed-frame presentation protocol during buffered drawing.

Crosshairs are not part of the document-export content. Their pixel state
belongs to on-screen presentation and should not contaminate cached plot data
or document rendering. See :doc:`rendering` and :doc:`graph-lifecycle` for the
surrounding drawing and callback boundaries.

Destruction clears the graph's crosshair pointer and releases Tk option state,
the option table, private GC and record. It requires the window context used by
option cleanup. The destructor does not itself implement an erase-and-restore
operation on a continuing graph; visibility transitions belong to the drawing
and configuration paths.

Regression checks should cover repeated stationary motion, rapid movement,
wide and dashed hairs, hotspot transitions across plot edges, plot shrinkage,
background changes and both buffered/direct rendering. Windows motion tests
and visual checks for flicker complement pixel checks: correct final pixels do
not prove that an intermediate erased frame was never displayed.

Parsing and configuration boundaries
------------------------------------

``GetCrosshairPositionFromObj`` accepts an empty position as the off-plot
``INT_MIN,INT_MIN`` sentinel. Nonempty values must have the ``@x,y`` form;
both coordinates use Tk screen-distance conversion, so they become integer
pixels before being stored as doubles. Temporary coordinate objects are
retained and released locally. A malformed position leaves the previous parsed
hotspot unchanged. This parser does not move or draw the overlay.

``ConfigureCrosshairs`` runs all selected conversions before erasing anything.
Tk has already stored the candidate option fields, but the active dash pattern,
width, hotspot and GC remain unchanged on conversion failure. The command
handler then restores the saved Tk options with the original diagnostic.
On success it releases saved options after the visual transition, without
scheduling a complete graph redraw. See :doc:`crosshair-reference` for the
helper contracts.

The low-level visibility helpers deliberately do not implement the same policy:

.. list-table::
   :header-rows: 1
   :widths: 25 40 35

   * - Helper
     - Eligibility
     - Requested hidden state
   * - ``TurnOnHairs``
     - Mapped, not already visible, hotspot inside plot
     - Neither reads nor writes it; caller decides whether to show
   * - ``TurnOffHairs``
     - Mapped and actually visible
     - Neither reads nor writes it; unmapped calls leave visible unchanged
   * - Windows ``MoveCrosshairs``
     - Computes old/new eligibility, including hidden and mapping
     - Reads it; always stores the new hotspot, segments and actual visibility

These differences let on/off commands change requested state separately while
configuration and frame presentation retain the geometry needed for XOR erasure.
They should not be consolidated merely because all three can draw segments.
The Windows helper is compiled only for Windows; its source contract complements
the platform-independent reference and the presentation protocol above.
