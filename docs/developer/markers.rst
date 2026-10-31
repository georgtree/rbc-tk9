Marker lifecycle and drawing
============================

``rbcGrMarker.c`` owns the marker registry, display list, shared commands and
concrete marker classes. ``MarkerStruct`` contains common coordinates, axes,
track association, visibility, tags and option state. Each concrete record
adds its own resources and cached geometry. ``MarkerClass`` supplies
configuration, mapping, drawing, export, point/region tests and cleanup.
See :doc:`marker-reference` for the shared graph-facing contracts.

Coordinates and mapping
-----------------------

Marker world coordinates are owned by the marker. Class mapping builds display
geometry using its selected axes, anchor, pixel offsets and other type-specific
options. ``Rbc_MapMarkers`` visits the display list and maps items selected by
``MAP_ALL`` or ``MAP_ITEM``. Hidden markers and markers with no coordinates are
skipped; a successful dispatch clears the item's mapping flag.

The private marker ``HMap``, ``VMap`` and ``MapPoint`` helpers are distinct from
``Rbc_HMap``, ``Rbc_VMap`` and ``Rbc_Map2D``. Marker parsing represents elastic
``Inf`` and ``-Inf`` endpoints using positive and negative ``DBL_MAX``.
These sentinels select normalized endpoints before applying descending direction
and screen orientation. They must not simply be sent through the ordinary
element-coordinate path.

The legacy marker logarithmic path also differs: positive values use ``log10``,
negative values are replaced with zero before normalization, and zero remains
zero. It does not share the ordinary mapping helpers' nonpositive-input
rejection and overflow-safe normalization. This chapter describes existing
behavior; consolidating those paths would require a separate compatibility
change and tests.

Track-owned axes map against full world bounds, including the displaced part
of a scrolled track. Visible bounds are for clipping. Each marker mapper also
runs within a paired track viewport scope. Region tests can map original
coordinates outside that drawing scope, so axis ownership must remain enough
to select the proper full track extent. See :doc:`mapping`.

Layers, order and visibility
----------------------------

The display list defines painter order. ``drawUnder`` selects the below-element
or above-element pass. Drawing excludes hidden and wholly clipped markers,
markers without coordinates, and markers associated with an existing hidden
element. A missing associated element is not itself the same as a hidden one.
The association check uses the element's ``hidden`` state; do not assume it
includes every element-specific visibility flag.

Ordinary graphs can batch compatible adjacent marker primitives. The component
chooses eligibility and order; renderer services own native drawing targets and
backend details. A new batch must flush at incompatible styles or intervening
content so it preserves the original painter order.

Trackgraphs retain that order even when track-owned and graph-wide markers
are interleaved. Track drawing uses renderer clipping for the visible owning
rectangle, then enters the track viewport around class drawing. This clips
complete native primitives without changing shared Tk graphics contexts.
Graph-wide markers retain their graph-wide path. Window markers are handled
separately rather than drawn into the clipping surface.

Window markers
--------------

A window marker manages a real Tk child. Mapping computes its size from the
child's requested dimensions and any positive explicit width/height, applies
the anchor and pixel offsets, and tests its rectangle for overlap with the
current plot viewport. Drawing moves/resizes and maps the child; the drawable
argument is not used to paint that widget into a pixmap.

``SyncWindowMarkers`` runs independently of the selected drawing layer. It
processes both under/over windows, unmapping hidden or wholly clipped children
and children in collapsed track regions. This is necessary because skipping a
paint operation cannot erase an already mapped child, and the below-element
backing store may be reused without repainting its markers.

Partial overlap deliberately retains existing graph behavior: the full child
remains mapped and is clipped by its graph parent, not by the plotting area or
track. There is no per-track clipping container. Do not describe this as the
same clipping guarantee as a line, image or polygon marker.

Window-marker cleanup destroys its currently embedded child through
``ReleaseWindowMarkerChild``. This ownership differs from borrowing an image
or numeric source. Code that changes geometry management or child association
must preserve event-handler and geometry-manager cleanup as well as pointer
state.

XOR paths
---------

Line and polygon markers retain native XOR behavior. Their configuration paths
track whether old immediate geometry is present so it can be erased before
replacement. Eligible graph-wide markers can use immediate drawing; track-owned
markers use scheduled redraw and the track drawing path instead. Polygon
immediate drawing has additional outline-only and axis-reset conditions.

XOR primitives bypass ordinary Cairo drawing where required. Old XOR geometry
must be erased using its old geometry and compatible native state, not newly
configured coordinates or an ordinary paint operation. Do not put XOR markers
into the normal compatible-stroke batches or assume that redrawing them twice
is harmless across changes of target or clipping. Document export is a separate
class callback, not a capture of the screen's XOR state.

Export and cleanup
------------------

``Rbc_MarkersExport`` walks the same display list and selects the requested
layer. It checks for an export callback, coordinates and visibility. Non-window
track markers get an export clip around their class exporter and are skipped
when wholly clipped or in collapsed regions. Window export follows its own
path: it asks the renderer to export the child only when it is currently mapped.
Backend capture support and export options determine the resulting content;
the shared marker pass does not create an independently laid-out copy of the
child widget.

Resource release is separate from record destruction. While the Tk window is
live, ``Rbc_ReleaseMarkerTkResources`` invokes each initialized class's cleanup,
frees common coordinates and tags, releases axis references, and frees the
Tk option record and table. Its release guard prevents repeating this work.
``DestroyMarker`` also handles failed initialization, removes bindings and
registry/display-list membership, and frees the record. The whole-registry
``Rbc_DestroyMarkers`` then destroys marker tables and the display list.
See :doc:`configuration` and :doc:`graph-lifecycle` for the broader ordering.

Changes should be checked with both marker layers, overlapping display order,
track scrolling and inversion, child hide/remap transitions, XOR replacement,
and resized document export. Screen snapshots alone do not establish child
window ownership or correct binding and region-test behavior.
