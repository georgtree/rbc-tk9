Marker lifecycle and drawing
============================

``rbcGrMarker.c`` owns the marker registry, display list, shared commands and concrete marker classes.
:c:struct:`MarkerStruct` contains common coordinates, axes, track association, visibility, tags and option state. Each
concrete record adds its own resources and cached geometry. :c:struct:`MarkerClass` supplies configuration, mapping,
drawing, export, point/region tests and cleanup. See :doc:`marker-reference` for the shared graph-facing contracts.

Coordinates and mapping
-----------------------

Marker world coordinates are owned by the marker. Class mapping builds display geometry using its selected axes, anchor,
pixel offsets and other type-specific options. :c:func:`Rbc_MapMarkers` visits the display list and maps items selected
by ``MAP_ALL`` or ``MAP_ITEM``. Hidden markers and markers with no coordinates are skipped; a successful dispatch clears
the item's mapping flag.

The private marker :c:func:`rbcGrMarkerMapping.HMap`, :c:func:`rbcGrMarkerMapping.VMap` and
:c:func:`rbcGrMarkerMapping.MapPoint` helpers are distinct from :c:func:`Rbc_HMap`, :c:func:`Rbc_VMap` and
:c:func:`Rbc_Map2D`. Marker parsing represents elastic ``Inf`` and ``-Inf`` endpoints using positive and negative
``DBL_MAX``. These sentinels select normalized endpoints before applying descending direction and screen
orientation. They must not simply be sent through the ordinary element-coordinate path.

The legacy marker logarithmic path also differs: positive values use ``log10``, negative values are replaced with zero
before normalization, and zero remains zero. It does not share the ordinary mapping helpers' nonpositive-input rejection
and overflow-safe normalization. This chapter describes existing behavior; consolidating those paths would require a
separate compatibility change and tests.

Track-owned axes map against full world bounds, including the displaced part of a scrolled track. Visible bounds are for
clipping. Each marker mapper also runs within a paired track viewport scope. Region tests can map original coordinates
outside that drawing scope, so axis ownership must remain enough to select the proper full track extent. See
:doc:`mapping`.

Layers, order and visibility
----------------------------

The display list defines painter order. ``drawUnder`` selects the below-element or above-element pass. Drawing excludes
hidden and wholly clipped markers, markers without coordinates, and markers associated with an existing hidden
element. A missing associated element is not itself the same as a hidden one. The association check uses the element's
``hidden`` state; do not assume it includes every element-specific visibility flag.

Ordinary graphs can batch compatible adjacent marker primitives. The component chooses eligibility and order; renderer
services own native drawing targets and backend details. A new batch must flush at incompatible styles or intervening
content so it preserves the original painter order.

Trackgraphs retain that order even when track-owned and graph-wide markers are interleaved. Track drawing uses renderer
clipping for the visible owning rectangle, then enters the track viewport around class drawing. This clips complete
native primitives without changing shared Tk graphics contexts. Graph-wide markers retain their graph-wide path. Window
markers are handled separately rather than drawn into the clipping surface.

Window markers
--------------

A window marker manages a real Tk child. Mapping computes its size from the child's requested dimensions and any
positive explicit width/height, applies the anchor and pixel offsets, and tests its rectangle for overlap with the
current plot viewport. Drawing moves/resizes and maps the child; the drawable argument is not used to paint that widget
into a pixmap.

:c:func:`rbcGrMarkerWindow.SyncWindowMarkers` runs independently of the selected drawing layer. It processes both
under/over windows, unmapping hidden or wholly clipped children and children in collapsed track regions. This is
necessary because skipping a paint operation cannot erase an already mapped child, and the below-element backing store
may be reused without repainting its markers.

Partial overlap deliberately retains existing graph behavior: the full child remains mapped and is clipped by its graph
parent, not by the plotting area or track. There is no per-track clipping container. Do not describe this as the same
clipping guarantee as a line, image or polygon marker.

Window-marker cleanup destroys its currently embedded child through
:c:func:`rbcGrMarkerWindow.ReleaseWindowMarkerChild`. This ownership differs from borrowing an image or numeric
source. Code that changes geometry management or child association must preserve event-handler and geometry-manager
cleanup as well as pointer state.

XOR paths
---------

Line and polygon markers retain native XOR behavior. Their configuration paths track whether old immediate geometry is
present so it can be erased before replacement. Eligible graph-wide markers can use immediate drawing; track-owned
markers use scheduled redraw and the track drawing path instead. Polygon immediate drawing has additional outline-only
and axis-reset conditions.

XOR primitives bypass ordinary Cairo drawing where required. Old XOR geometry must be erased using its old geometry and
compatible native state, not newly configured coordinates or an ordinary paint operation. Do not put XOR markers into
the normal compatible-stroke batches or assume that redrawing them twice is harmless across changes of target or
clipping. Document export is a separate class callback, not a capture of the screen's XOR state.

Export and cleanup
------------------

:c:func:`Rbc_MarkersExport` walks the same display list and selects the requested layer. It checks for an export
callback, coordinates and visibility. Non-window track markers get an export clip around their class exporter and are
skipped when wholly clipped or in collapsed regions. Window export follows its own path: it asks the renderer to export
the child only when it is currently mapped. Backend capture support and export options determine the resulting content;
the shared marker pass does not create an independently laid-out copy of the child widget.

Resource release is separate from record destruction. While the Tk window is live,
:c:func:`Rbc_ReleaseMarkerTkResources` invokes each initialized class's cleanup, frees common coordinates and tags,
releases axis references, and frees the Tk option record and table. Its release guard prevents repeating this work.
:c:func:`rbcGrMarkerLifetime.DestroyMarker` also handles failed initialization, removes bindings and
registry/display-list membership, and frees the record. The whole-registry :c:func:`Rbc_DestroyMarkers` then destroys
marker tables and the display list. See :doc:`configuration` and :doc:`graph-lifecycle` for the broader ordering.

Changes should be checked with both marker layers, overlapping display order, track scrolling and inversion, child
hide/remap transitions, XOR replacement, and resized document export. Screen snapshots alone do not establish child
window ownership or correct binding and region-test behavior.

Common configuration and ownership
----------------------------------

Concrete marker records embed ``Marker`` as their first member, checked by static assertions. Class methods receive that
common header and recover the concrete record. :c:struct:`MarkerClass` combines Tk option specifications with configure,
map, draw, export, point-test, region-test and cleanup callbacks. The common constructor chooses a class; it does not
initialize Tk options or publish a registry entry. Arc construction reuses polygon storage and several polygon services,
rather than introducing a second common marker lifecycle.

.. list-table:: Common resources and owners
   :header-rows: 1

   * - Resource
     - Ownership and release
   * - Marker record
     - Concrete allocation; :c:func:`rbcGrMarkerLifetime.DestroyMarker` frees it
   * - Name and direct option fields
     - Tk option ownership after initialization; partial-creation cleanup handles earlier state
   * - ``worldPts``
     - Owned parsed coordinate array, replaced at common-option commit
   * - tags
     - One ``Tcl_SplitList`` allocation, freed as a whole
   * - X/Y axes
     - Acquired references, dropped on replacement or release
   * - Track
     - Borrowed association validated against Y-axis ownership
   * - Registry hash entry and display link
     - Graph infrastructure; marker stores handles for removal
   * - Interned binding-tag strings
     - Graph marker-tag table, not individual marker records
   * - Mapped/type-specific resources
     - Concrete class cleanup before Tk option cleanup

:c:func:`rbcGrMarkerConfiguration.InitMarkerOptions` temporarily detaches the preliminary name while applying component
defaults through the option proxy. It uses the normalized marker name as resource name and the class UID as resource
class. Monochrome alternatives are selected when available on a one-bit display. Both success and failure restore the
preliminary name; an option-database name must not silently rename the new record or be freed twice during failed
initialization.

:c:func:`rbcGrMarkerConfiguration.ConfigureMarkerOptions` installs candidate Tk fields under saved options, validates
the name and calls the class configure method with ``optionMask``. Reconfiguration cannot rename over another existing
marker. The registry rename is committed only after class configuration succeeds. On failure Tk fields are restored and
the diagnostic retained. This wrapper cannot undo derived resources that a class commits prematurely; each class must
stage its own fallible work.

Common class preparation uses :c:struct:`ParsedMarkerOptions`. :c:func:`rbcGrMarkerConfiguration.ParseMarkerOptions`
zeros it, parses tags, coordinates and state, acquires both axes and validates the optional track. The selected Y axis
must belong to exactly that track, including the graph-wide ``NULL`` case. Failure releases candidate allocations and
axis references. :c:func:`rbcGrMarkerConfiguration.CommitMarkerOptions` frees old common resources, transfers new
allocations/references and clears transferred pointers. It does not register, map or schedule the marker; concrete
configuration completes those tasks.

Coordinate and color parsing
----------------------------

Empty coordinates are accepted for every marker class and suppress normal mapping/drawing until populated. Nonempty
coordinate lists obey these limits:

.. list-table:: Coordinate-list arity
   :header-rows: 1

   * - Class
     - Number of X/Y pairs
   * - Text, window
     - Exactly one
   * - Image, bitmap
     - One or two
   * - Line
     - At least two
   * - Polygon
     - At least three
   * - Arc
     - Exactly two

Odd list lengths fail before allocation. Byte-size checks and ``Tcl_AttemptAlloc`` make coordinate allocation failure
reportable as a Tcl error. Each component is evaluated separately. Exact ``Inf``, ``+Inf`` and ``-Inf`` spellings select
elastic ``DBL_MAX`` sentinels; other strings go through ``Tcl_ExprDouble``. This parser does not impose a general
finite-value policy.

Expression evaluation can execute application Tcl. The parser borrows list storage and the marker record throughout
evaluation; ordinary candidate cleanup is not protection against callbacks deleting owners or mutating borrowed inputs.
Future lifetime changes must address that boundary explicitly.

Single colors accept empty values as ``NULL`` and, when enabled by the caller, nonempty prefixes of ``defcolor`` as a
sentinel. Other values acquire Tk colors. A color pair contains at most foreground and background. Pair conversion
stages both colors, releases a successfully acquired first color if the second fails, and transfers ownership only on
success. It does not free an older destination pair. Sentinel-aware release remains essential.

Creation, replacement and rename
--------------------------------

:c:func:`rbcGrMarkerCommands.CreateOp` resolves the marker type and scans complete option/value pairs for the last
abbreviated or full ``-name`` occurrence. Without one it generates a ``markerN`` name. The malformed trailing-option
case is left to Tk configuration rather than reading past the argument vector. The concrete constructor copies the
preliminary name; configured names must be nonempty and must not start with a dash.

Creation configures the new record before replacing an existing same-named marker. Replacement detaches the old record's
hash handle so its destructor cannot delete the entry being reused, then publishes the new value and appends its display
link. This is destruction/recreation, not an in-place type change: class-owned resources of the old marker are released,
including a managed child for a window marker. Reconfiguration uses different collision rules and rejects an existing
destination name.

:c:func:`rbcGrMarkerCommands.ConfigureOp` validates the list of names before starting updates, but each marker update is
its own transaction. A failure on a later marker leaves earlier successful updates in place. Query forms return
information for the first named marker. Names are re-resolved as updates proceed, which matters if an earlier update
renamed a marker.

:c:func:`rbcGrMarkerCommands.DeleteOp` ignores missing exact names, clears lookup diagnostics and schedules a
redraw. :c:func:`rbcGrMarkerCommands.ExistsOp` tests the registry; :c:func:`rbcGrMarkerCommands.NamesOp` enumerates
display order. Those views should agree in a valid registry/list state, but they are different structures and must be
maintained together.

Bindings, point picking and region searches
-------------------------------------------

Marker tags are interned in their own graph table. Binding enumeration appends name, class and configured tags in
order. Tag strings can outlive a marker and can be registered before it exists. Deleting an object clears its
object-pointer binding/current-item state; that is distinct from deleting scripts on an interned name tag.

``marker get current`` checks the graph binding context before interpreting the current item as a marker. The handler
does not accept coordinate picking or ordinary marker names; older legend-related comments here were misleading.
:c:func:`rbcGrMarkerMapping.Rbc_NearestMarker` is the point-picking service: it traverses display order backwards and
returns the first class point test that succeeds, not a global minimum-distance result.

The shared selection filters differ:

.. list-table:: Shared query filters
   :header-rows: 1

   * - Operation
     - Order and eligibility
   * - Nearest point
     - Reverse display order; requested layer, coordinates present, not hidden, ``STATE_NORMAL``
   * - Region find
     - Forward display order; not hidden and no existing hidden associated element
   * - Track-use query
     - Registry order; association only, regardless of visibility or coordinates

Both point and region queries enforce visible track constraints for local markers. Region overlapping mode intersects
its query with track bounds; enclosed mode retains the original query rectangle after an intersection test.
:c:func:`rbcGrMarkerCommands.FindOp` normalizes integer rectangle corners and returns only the first match. It does not
filter by layer or marker state. Point picking does not repeat the associated-element hidden check used by region
queries and drawing. These are current contracts, not interchangeable implementations of visibility.

The command dispatcher validates operation spelling and arity but does not itself refresh layout. Picking and class
region methods consume current mapped state or use their own mapping helpers. Callers must not assume a command query
implicitly performs the graph's complete layout pass.

Display-order changes
---------------------

:c:func:`rbcGrMarkerCommands.RelinkOp` resolves the target and optional reference before unlinking the existing
display-list link. Invalid names leave both the registry and display order unchanged. Moving a marker relative to itself
succeeds without changing order. A successful move retains the same marker and link, invalidates the backing store
according to the moved marker's under-layer setting (not the reference marker's setting), and schedules redraw. Omitting
the reference retains the existing front/back placement conventions.

Teardown and invalidation details
---------------------------------

:c:func:`rbcGrMarkerLifetime.DestroyMarker` invalidates the backing store for under-layer markers and removes object
bindings. Initialized records use the guarded Tk-resource release path; records that failed before initialization
receive explicit common cleanup. It then deletes non-``NULL`` registry/list handles and frees the concrete allocation.
Callers, rather than this destructor, schedule the eventual redraw.

Whole-registry destruction clears each marker's hash handle before invoking its destructor so iteration owns hash-table
deletion. It finally destroys both the registry and tag table and the display chain. This is final teardown, not a
repeatable empty-the-registry operation on an otherwise initialized graph.

:c:func:`rbcGrMarkerMapping.Rbc_MarkersUseTrack` examines all registered associations to prevent removing still-used
tracks or Y axes, including hidden markers. :c:func:`rbcGrMarkerMapping.Rbc_MarkerFontsChanged` likewise visits hidden
text markers, resets text style and layout, rebuilds the rotated bounding outline and sets ``MAP_ITEM``. Hidden geometry
must be ready to remap when the marker is shown again.

Geometric markers: line, polygon and arc
----------------------------------------

These three classes share renderer services but deliberately retain different geometry and picking contracts.
:c:struct:`LineMarker <rbcGrMarkerLine.LineMarker>` owns its segment array and private GC, with fixed-size arrowhead
arrays inside the record. :c:struct:`PolygonMarker <rbcGrMarkerPolygon.PolygonMarker>` is also the storage type for
arcs: :c:func:`rbcGrMarkerPolygon.CreateArcMarker` changes the class callback table, while configuration and resource
release continue to use :c:func:`rbcGrMarkerPolygon.ConfigurePolygonMarker` and
:c:func:`rbcGrMarkerPolygon.FreePolygonMarker`.

.. list-table:: Geometry ownership and consumers
   :header-rows: 1
   :widths: 20 40 40

   * - Record/cache
     - Contents
     - Consumers
   * - Line ``segments``
     - Plot-clipped independent shaft segments, with arrow necks substituted
     - Screen/export strokes and point picking
   * - Line arrow arrays
     - Five unique display vertices plus a repeated tip
     - Screen/export fills and point/region tests
   * - Polygon ``screenPts``
     - All mapped world vertices, with pixel offsets
     - Polygon point/region tests and generation of clipped caches
   * - Polygon/arc ``fillPts``
     - Rectangle-clipped fill polygon
     - Polygon output, native arc fallback and arc fill picking
   * - Polygon/arc ``outlinePts``
     - Rectangle-clipped outline segments
     - Polygon output, native arc fallback and arc stroke picking
   * - Arc ``screenPts`` and ``nArcPts``
     - Flattened ellipse path, including style-dependent closure
     - Arc enclosure tests and clipped-cache generation
   * - Arc ``arc``
     - Unclipped center, radii, normalized start, signed extent and closure
     - Curved screen and document renderers

The mappers discard old geometry before allocating replacements. Checked ``Tcl_AttemptAlloc`` failures do not propagate
Tcl errors: line mapping remains empty/clipped, polygon mapping can retain only one successful output cache, and arc
mapping can retain exact curve geometry without its flattened caches. Do not assume that a non-clipped arc necessarily
has points available for picking. These are void mapping callbacks, not transactional configuration operations.

Line shafts and arrowheads
~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrMarkerLine.MapLineMarker` first obtains plot extents, maps the original endpoint and its immediate
neighbor, and applies pixel offsets. It creates a requested head only if the tip is inside those extents. Coincident
endpoint/neighbor pairs suppress that head; mapping does not search farther along the polyline for a direction. The
resulting shaft endpoint is backed up to the neck before the shaft edges are individually clipped. A surviving arrowhead
can keep the marker drawable even when no shaft segment survives.

:c:func:`rbcGrMarkerLine.ComputeArrowHead` follows Tk-style arrow construction. Shape distances are adjusted by a small
epsilon, lateral width includes half the effective shaft width, and neck interpolation determines the shaft
backup. :c:func:`rbcGrMarkerLine.ParseArrowShape` requires exactly three valid screen distances; it adds no separate
positivity or ordering validation. The effective width used for head geometry is at least one pixel.

Screen alignment conversions belong to output, not mapping. Native arrowed shafts round coordinates in 128-segment stack
batches. The rendered fill helper copies the five unique head vertices and adds half a pixel to align area boundaries
with the rendered shaft. Neither conversion changes the cached doubles used for picking and document output.

The line's ``-fill`` color is the dash-gap/background color, not a separate arrow fill: heads use the outline. Screen
drawing may recover an absent outline from the GC foreground. :c:func:`rbcGrMarkerShapeOutput.ArrowHeadExport` instead
omits heads when the explicit outline is empty. Export sends mapped doubles through renderer contexts and does not
replay the screen XOR operation or pixel-alignment adjustments.

Polygon fill versus outline
~~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrMarkerPolygon.MapPolygonMarker` maps all world vertices and offsets once. It clips fill only when a fill
foreground exists. Outline generation requires a foreground and positive width and clips every original edge, including
last-to-first. This separation matters: polygon clipping can add viewport-boundary edges to a fill, but those new edges
must not become an artificial visible outline.

Screen drawing submits the clipped fill first and the independent outline segments second. Fill styles carry foreground,
background and stipple. Stroke styles carry width, caps, joins, dashes and optional dash-gap color. Selected renderer
operations have native GC fallbacks; XOR forces native paths. Polygon export uses the same clipped caches through
document renderer contexts. The class does not promise a single continuous stroked polygon after clipping.

Arc mapping and output
~~~~~~~~~~~~~~~~~~~~~~

An arc takes exactly two world corners. After mapping those corners, the mapper constructs an axis-aligned display
ellipse from their center and absolute half-differences. Thus logarithmic, descending and inverted axes affect the box
mapping; arc angles are then display-space angles. Positive angles use ``x = cx + rx*cos(angle)`` and ``y = cy -
ry*sin(angle)``.

Zero extent, zero radii and nonfinite mapped geometry produce no usable arc. Start is reduced modulo 360 degrees and
extent is clamped to one signed turn. A full turn closes on the first point. A partial open arc stays open, a chord
returns directly to the first point, and a pieslice adds the center before returning. Open arcs ignore fill for drawing
and export.

Flattening targets a 0.15-pixel chord error, with angular steps no larger than 0.1 radians and a maximum of 8192
segments. At extreme zoom the cap can prevent that error target from being met. Conservative visibility therefore uses
the full ellipse bounds plus outline margin, not only the flattened path. Curved renderers consume the exact ellipse and
their context applies clipping. Native fallback additionally receives flattened, clipped fill/outline geometry.
:c:func:`rbcGrMarkerShapeOutput.StrokeArcMarker` delegates continuous-path dash and join handling to the renderer
instead of restarting strokes at each flattened vertex.

Picking differences
~~~~~~~~~~~~~~~~~~~

.. list-table:: Point and region semantics
   :header-rows: 1
   :widths: 15 40 45

   * - Class
     - Point picking
     - Region queries
   * - Line
     - Arrow interior or clipped shaft within graph halo
     - Original mapped edges and cached heads; enclosure requires all original vertices and full heads inside
   * - Polygon
     - Interior of the full mapped polygon, even without a visible fill
     - Full mapped polygon, independent of clipped output caches
   * - Arc
     - Clipped fill or clipped outline within max(halo, half linewidth)
     - Enclosure uses full flattened vertices; overlap uses clipped fill/outline

Region queries do not expand by stroke width. Arc queries are flattened tests, not analytic ellipse intersections, and
require nonempty output caches. Line region queries remap original world points whereas point picking consumes the
segment cache. These differences are current behavior, not a unified notion of visible painted coverage. The caller must
still arrange valid mapped state.

Configuration and XOR lifetime
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Both configuration callbacks stage common options and converted class values before replacing active derived state. The
polygon/arc callback also acquires new color pairs; the line callback's colors are Tk-managed. Replacement GCs are
constructed before committing, and error paths release staged resources. The outer Tk option transaction remains
responsible for restoring option fields.

Line configuration uses immediate drawing only for graph-wide XOR markers; track-owned markers and track transitions use
scheduled drawing. Polygon/arc immediate drawing additionally requires an outline-only case and no pending axis
reset. Open arcs count as outline-only even if fill options were supplied. The private outline GC gets the XOR
function; the shared fill GC does not get that same function merely because the marker has ``-xor`` enabled.

:c:func:`rbcGrMarkerShapeOutput.DrawImmediatePolygonOutline` preserves native erase behavior for arcs by temporarily
forcing the XOR dispatch flag and then restoring it. Cleanup must retain the old GC and mapped geometry until erasure is
complete; it then frees class-owned caches/GCs and, for polygon/arc, color pairs. Common cleanup owns Tk options, world
coordinates, tags, axis references and the record itself.

Line configuration and cleanup share :c:func:`rbcGrMarkerShapeOutput.EraseLineMarkerXor`. It redraws the recorded shaft
and each enabled head with the original native GC before those resources are replaced or freed, then clears
``xorState``. Head-only geometry is erased even when no shaft segment survives clipping. Newly installed options such as
``-xor``, ``-arrow`` and colors must not select the erase operation: only the old mapped caches and GC describe the
image currently on screen.

Bitmap, image, text and window resources
----------------------------------------

These classes share common marker option transactions but do not share the same ownership or placement model. Their
class callbacks release derived resources; common cleanup subsequently releases Tk option resources and the record. The
corresponding source contracts are in :doc:`marker-reference`.

.. list-table:: Ownership by class
   :header-rows: 1
   :widths: 15 45 40

   * - Class
     - Class-managed resources
     - Resources retained elsewhere
   * - Bitmap
     - Generated mapped bitmap/mask pixmaps and drawing GCs
     - Source bitmap/mask and colors are Tk option resources
   * - Image
     - Tk image instance, owned photo copy, temporary scaled image, pixmap and GC
     - Named source image belongs to the application
   * - Text
     - Text layout, text/background GCs and shadow color
     - Font, string and foreground/background colors are Tk option resources
   * - Window
     - Geometry management and event registration for the embedded child
     - Marker deletion also destroys the current child; replacing it merely detaches and unmaps the previous child

The zeroed class constructors install callback tables; the text constructor also initializes a text style and default
padding. They do not independently insert records into the registry or finish option initialization.

Bitmap masks and aliasing
~~~~~~~~~~~~~~~~~~~~~~~~~

A bitmap marker keeps ``srcBitmap`` and optional ``srcMask`` separate from ``destBitmap`` and ``destMask``. Configuring
both source handles requires matching dimensions. :c:func:`rbcGrMarkerBitmap.FreeMappedBitmapResources` frees only
handles whose ownership flags are set, then clears the handles/flags. A transformed bitmap may also be its own mask; in
that case only the bitmap handle owns the allocation.

The effective mask depends on background visibility:

.. list-table:: Effective source mask
   :header-rows: 1

   * - Explicit mask
     - Background
     - Effective mask
   * - Present
     - Opaque
     - Borrow the configured mask
   * - Present
     - Transparent
     - Allocate source bitmap AND configured mask
   * - Absent
     - Transparent
     - Borrow the source bitmap itself
   * - Absent
     - Opaque
     - No separate source mask

A single coordinate pair retains natural bitmap dimensions and applies the marker anchor; the unrotated center case
aligns the center pixel explicitly. Two pairs define sorted opposite corners of an inclusive scaled box and ignore the
anchor. Rotation/scale mapping generates only the visible subregion, then frees any temporary source-mask intersection
or transfers it to the destination mask as appropriate. Completely offscreen boxes avoid transformation work.

The mapped background outline is an absolute display polygon. Arbitrary rotation without an explicit mask uses that
polygon for an opaque background and the bitmap itself as the foreground mask. Screen presentation passes this state to
raster renderer services; export emits background/foreground masks or the polygon, freeing its temporary foreground
intersection afterward.

Bitmap picking is geometric, not per-bit: transparent holes are not excluded. Rotated point and region callbacks use
the absolute mapped outline directly, without adding ``anchorPos`` a second time. This differs from the local-outline
convention used by text markers.

Photo and non-photo image paths
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrMarkerImage.ConfigureImageMarker` acquires a new Tk image instance and callback before releasing the old
instance. If the named image is a photo, it also owns an RBC color-image
copy. :c:func:`rbcGrMarkerImage.ImageChangedProc` replaces that copy on every notification, ignoring the changed
subrectangle. If the name is no longer a photo, it clears the old photo representation rather than preserving stale
pixels, marks mapping and backing dirty, and schedules redraw.

Photo mapping supports either a natural-size anchored point or a two-corner scaled box. A scaled photo is materialized
only for its visible subregion in an owned temporary Tk image. Returning to natural size destroys that temporary
image. Fields are cleared before temporary-image destruction. Mapping returns no Tcl status, so failed temporary-image
creation must not be interpreted as a successful atomic replacement of every cache.

Non-photo images follow a different path: they retain Tk's natural dimensions and use the first mapped point plus
offsets as their top-left placement. That branch does not apply the marker anchor or scale to a second corner. Screen
output chooses the temporary/source Tk image unless a cached pixmap is present; export chooses photo output when
possible and otherwise delegates generic Tk image output to the backend.

Image point and region tests use the cached rectangle, not alpha or source-image contents. They do not independently
check whether the source has been deleted. Drawing/export perform their own deleted-instance checks. Freeing a marker
releases its image instance and derived resources, but does not delete the application's named source image.

Text layout and coordinate spaces
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Text configuration rebuilds layout for every successful configuration, since font, text, justification, padding, shadow
and rotation can affect dimensions. The proposed layout uses staged padding/shadow and the Tk-installed style while
saved options still retain the old Tk resources. The shadow color is manually owned and must be released independently
of Tk-managed foreground/background.

The text style's internal anchor is north-west. The marker's separate anchor places the cached rotated bounding
rectangle during mapping. Its outline is local to that rectangle, so point/region tests and output correctly translate
it by ``anchorPos`` once. Mapping itself does not render text or create a rotated bitmap. Named-font changes use the
shared font-invalidation path described earlier in this chapter.

Screen backgrounds try Cairo with integer-converted polygon coordinates, then fall back to native background
services. Glyphs use the screen-text service and cached layout. Document output uses the text string/style and unrounded
translated background vertices. Picking tests the rectangle or rotated outline, not actual glyph coverage. Class cleanup
releases layout, GCs and shadow; common Tk option cleanup releases font, string and configured colors.

Child-window transitions and notifications
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Window configuration parses common options and dimensions and resolves the proposed child before detaching the existing
one. The child must have the graph as its direct parent. Internal zero width/height means requested size, while an
explicitly configured zero is rejected. Positive overrides take precedence during mapping.

:c:func:`rbcGrMarkerWindow.ReleaseWindowMarkerChild` clears its token before removing the event handler and geometry
manager. Replacement unmaps the old child for reuse; marker cleanup destroys the current child. Clearing the token
prevents the marker's own destroy notification from leaving a stale window reference, but should not be treated as a
general lifetime guarantee across arbitrary Tk callbacks. Callers must retain the marker record while Tk services
execute.

.. list-table:: Child callbacks
   :header-rows: 1
   :widths: 25 75

   * - Callback
     - Current action
   * - :c:func:`rbcGrMarkerWindow.ChildEventProc`
     - On ``DestroyNotify``, clears child/dimensions, marks mapping dirty and schedules redraw when the graph window
       still exists; retains marker
   * - :c:func:`rbcGrMarkerWindow.ChildGeometryProc`
     - Copies requested dimensions only where the explicit override is zero, then schedules remapping/redraw when an
       automatic dimension changes
   * - :c:func:`rbcGrMarkerWindow.ChildCustodyProc`
     - Removes the event handler, clears the child token and destroys the marker, preserving the child for its new
       geometry manager

Changed automatic dimensions mark the marker for remapping, invalidate under-layer backing storage when needed, and
schedule graph redraw. Mapping recomputes the anchor and visibility before the child is resized. Explicit width/height
overrides remain authoritative; unchanged or fully overridden requests do not schedule unnecessary redraws.

:c:func:`rbcGrMarkerWindow.DrawWindowMarker` moves/resizes and maps the real child, ignoring its drawable
argument. :c:func:`rbcGrMarkerWindow.SyncWindowMarkers` must run even when a cached under-layer is reused, and processes
both marker layers. Hidden, wholly clipped or collapsed-track children are unmapped; partial overlap retains the full
child with graph-parent clipping only. Export delegates capture only for a currently mapped child and does not map
hidden children to manufacture an export image.

Pass dispatch and batching
--------------------------

:c:func:`Rbc_DrawMarkers` first synchronizes real window children regardless of the requested under/over
pass. Trackgraphs then use :c:func:`rbcGrMarkerBatches.DrawTrackMarkers` and return; ordinary graphs continue through
stroke reuse and, on Windows, native batches. All paths borrow marker records and their mapped resources. They assume
the caller has prepared mapping and retained graph lifetime; the batch arrays are not an ownership mechanism for marker
records.

Ordinary drawing filters window markers, empty coordinates, the other layer, hidden/clipped markers and markers whose
associated element exists and is hidden. Skipped markers contribute no pixels and need not break an otherwise compatible
run. A visible incompatible marker must end the current run before it draws. No optimization may reorder visible
overlapping content.

Windows native batches
~~~~~~~~~~~~~~~~~~~~~~

Before drawing, the Windows path computes upper bounds for five scratch arrays. This preliminary scan deliberately
counts eligible markers even if hidden, clipped or in another layer. Segment counts are summed; fill/text marker arrays
and bitmap-position arrays count records. Size overflow disables the affected batch, and failed ``Tcl_AttemptAlloc``
calls leave that array unavailable. The main pass then falls back to ordinary class drawing for that batch type. There
is no per-marker allocation in the middle of assembling a native run.

.. list-table:: Native run eligibility and style matching
   :header-rows: 1
   :widths: 18 52 30

   * - Run
     - Eligibility
     - Compatibility within run
   * - Line
     - Native, non-XOR, no arrows, solid one-pixel butt/miter stroke with explicit foreground and mapped segments
     - Foreground pixel
   * - Polygon outline
     - Same narrow stroke style, without a drawable fill
     - Outline foreground pixel
   * - Polygon fill
     - Native solid foreground, no XOR/stipple/background/drawable outline, and ``3..INT_MAX`` mapped fill points
     - Fill foreground pixel
   * - Text
     - Layout/string/foreground present, no background, special text state or visible shadow; also usable with
       Cairo-selected graphs
     - Each marker retains its own font/style; only the target is shared
   * - Bitmap
     - Native opaque bitmap with both colors, no destination mask and only
       right-angle rotation
     - Same destination bitmap, dimensions and GC foreground/background

Eligibility must be established before the ``SameWin*Style`` helpers are used. Those helpers intentionally omit
properties already fixed by their predicates. For example, matching foreground pixels alone is sufficient for the
eligible one-pixel solid line run, but would be wrong for arbitrary line styles.

The main pass flushes other batch types before appending to one run, and flushes the same type on style
changes. Individual class drawing flushes every pending native run first. End-of-list flushes handle runs with no
following marker. Flush helpers clear counts/style pointers but do not free scratch arrays or marker resources.

Line and outline runs copy segments into pass-owned arrays. Polygon fills retain marker pointers and submit polygons
individually under one native batch context, whose scratch capacity is the maximum polygon point count in the run. Text
runs invoke :c:func:`rbcGrMarkerBatches.DrawTextMarkerRun` synchronously through renderer target services; the callback
retains nothing. Bitmap runs store integer positions and reuse a representative bitmap/GC. These distinctions preserve
original primitive and text semantics rather than forcing every class through one aggregate path.

Cairo context reuse and native targets
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrMarkerBatches.GetMarkerStroke` accepts solid, non-XOR line shafts without arrows and polygon outlines
without drawable fills. It returns a borrowed :c:struct:`MarkerStroke <rbcGrMarkerBatches.MarkerStroke>` view; a
``FALSE`` result must not be used, even if some fields were written. Arcs retain their class-specific continuous-curve
path. Style equality compares width, cap, join and RGB values, not pointer identity.

Compatible strokes reuse an open renderer context, but each marker still calls :c:func:`Rbc_RenderSegments`
separately. Merging all paths into one stroke would change antialiased overlap coverage. Incompatible visible content
closes the context before dispatch; failure to create one falls back to the class draw callback. Context reuse therefore
changes setup cost, not painter order.

On Windows, :c:func:`rbcGrMarkerBatches.MarkersNeedRenderTarget` avoids an extra Cairo target for passes containing only
native text without backgrounds, real windows, or XOR line/ polygon content. Its scan is conservative: it does not
repeat the associated- element hidden check, so it may request a target that turns out unnecessary. The renderer owns
target acquisition/completion; marker code owns run order and eligibility. Text and bitmap native services remain
renderer responsibilities.

Track and export paths
~~~~~~~~~~~~~~~~~~~~~~

Trackgraphs do not enter the ordinary native-batch allocation pass. They walk visible markers in order, drawing
graph-wide markers directly and track-owned markers through one reusable clip context. Each local rectangle is bounded
by the graph drawable; zero-width and very short regions are skipped. Class drawing runs between
:c:func:`Rbc_EnterTrack`/:c:func:`Rbc_LeaveTrack` calls, and clip scopes are ended before advancing. No shared Tk GC is
rewritten to enforce track clipping.

Document export has its own ordered traversal and class callbacks, not the screen batches. It filters
layer/coordinates/hidden state and associated hidden elements, and wraps non-window track exports in paired
viewport/clip scopes. Graph-wide exports do not use the same blanket ``clipped`` check as screen drawing; class
geometry and exporter clipping remain relevant. Window markers retain their mapped-child capture behavior.

Windows scratch ownership
~~~~~~~~~~~~~~~~~~~~~~~~~

The ordinary Windows pass owns all five scratch arrays: line segments, polygon segments, polygon-fill marker pointers,
text-marker pointers and bitmap positions. Flush helpers reset batch bookkeeping but do not release storage. After the
final synchronous flush, :c:func:`Rbc_DrawMarkers` frees every allocated array before ending the marker render
target. ``NULL`` arrays from unused batches or allocation failures require no release. Freeing the text-marker pointer
array does not free the markers, layouts or fonts it borrowed.

Geometry and batch argument contracts
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The detailed parameter sections in :doc:`marker-reference` distinguish borrowed inputs from replaced mapping caches and
reset batch slots. Coordinates passed to picking and drawing helpers are display coordinates. World coordinates are
mapped first, including marker offsets and the current track viewport.

* :c:func:`rbcGrMarkerLine.ComputeArrowHead` writes a six-point closed head and a shortened shaft endpoint only on
  success. Its three shape distances are pixels, while :c:func:`rbcGrMarkerLine.ParseArrowShape` converts Tcl screen
  distances and may leave a partially written output array on error.
* :c:func:`rbcGrMarkerPolygon.MapArcMarker` keeps exact ellipse geometry separate from flattened picking
  caches. Drawing/export and picking must not treat these representations as interchangeable, especially after
  allocation failure.
* :c:func:`rbcGrMarkerShapeOutput.EraseLineMarkerXor` requires the old GC and geometry. A missing drawable clears
  bookkeeping without erasing pixels; replacing options is not a substitute for retaining the old drawing state.
* :c:func:`rbcGrMarkerBatches.GetMarkerStroke` returns a borrowed view, not a retained snapshot. Only a ``TRUE`` result
  can be compared or submitted, and the marker caches and colors must remain live throughout context reuse.
* Windows flush helpers consume pending counts and clear representative slots. They borrow arrays and markers
  throughout synchronous submission; the outer pass still owns scratch allocation and release. Eligibility predicates
  must precede style comparisons, which intentionally omit already-fixed properties.
