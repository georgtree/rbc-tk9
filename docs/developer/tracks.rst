Track ownership and layout
==========================

A trackgraph is one graph widget with a linked list of named plot regions.
Tracks are not child windows or independent graph instances. ``rbcGrTrack.c``
owns track records, configuration, ordering and stack layout. Axis, element,
marker, grid and legend implementations retain their ordinary module owners.

Records and dependencies
------------------------

The graph owns the track list. Each record owns its name and axis-pointer
array, holds references to its assigned Y axes, and owns optional local grid
and legend components. An axis has a back-pointer to its track. Elements and
markers refer to tracks but remain graph-managed objects; deleting a track
through the command interface does not implicitly delete its data components.

Track names are case-sensitive and lookup scans the list. Reordering relinks
the same records, preserving component and binding identities. A borrowed
``RbcTrack *`` remains valid across reordering, but not deletion or teardown.
The ``all`` track binding target uses the address of the graph's list field,
not the current first track. See :doc:`bindings` for dispatch and repicking.

Configuration and axis ownership
--------------------------------

Creation reads defaults using the named ``Track`` option-database component,
with the resource-name conversion used by older components. Temporary
``TrackDefaults`` fields retain Tcl objects rather than immediately converting
axis names or dimensions. Explicit options replace database defaults before
validation; the effective option list goes through ``ConfigureTrack``.

Configuration stages scalar values and a replacement axis array. It rejects
duplicate axes, incompatible classes or margins, axes owned by another track,
and axes used by graph-wide markers. Removing an axis is rejected when an
existing track element, local grid or track marker still uses it. New axis
references are acquired before old ones are released, so an axis retained in
both lists survives replacement.

On success, the caller synchronizes the Y-axis display chain. ``SyncTrackAxes``
rebuilds that chain in track order and axis-list order, updates on-screen flags,
marks ``RESET_WORLD`` and schedules redraw. This is logical Y-axis ownership:
inversion changes physical orientation, not the axis class.

Sizing and visible bounds
-------------------------

Tracks share the graph's other plot dimension. Normal layout divides the
vertical range; inverted layout divides the horizontal range. Historical
``height`` and ``minHeight`` field names mean size along this stacking direction.

Fitted layout subtracts inter-track gaps from the available range. If the sum
of minima fits, each track receives its minimum plus a weighted share of the
remainder. If minima exceed the available space, their proportions determine
the reduced sizes. Positive bounded weights are enforced by configuration.
Fitted gaps shrink with the available range, up to 12 pixels.

Scrolling layout uses ``max(height, minHeight)`` for each track and 12-pixel
gaps. Its content extent may exceed the viewport. Each track stores full world
bounds and their clamped visible bounds. Components must use world bounds for
mapping and visible bounds for clipping; using visible height as the data
scale would move data when scrolling. See :doc:`mapping` for paired
``Rbc_EnterTrack``/``Rbc_LeaveTrack`` scopes and :doc:`scrolling` for the
separate stack-offset and local data-axis scroll operations.

A fitted sash adjusts the adjacent pair's weights while preserving their sum.
If the pair has no room beyond its minima, the operation does nothing. In
scrolling layout the sash changes the first track's requested height instead.
Both paths invalidate layout and schedule redraw; they do not directly draw
into the window.

Local components and dispatch
-----------------------------

A NULL local grid means inheritance from the graph grid. A local grid keeps
its own configuration and mapped segment caches. Local legends use track
geometry without contributing outer graph margins. Track bar mode ``-1``
means inheritance from the graph. See :doc:`grids`, :doc:`legends` and
:doc:`bars` for component-specific behavior.

Nested component commands adapt their argument vector for the existing grid
or legend dispatcher. The adapter allocates an array and one retained,
list-valued command prefix; all other entries borrow the original objects.
Use the paired free helper, not a loop decrementing every entry. This keeps
ordinary component command handling and diagnostics reusable.

Deletion and teardown
---------------------

Command deletion validates every requested name and dependency before removing
anything. Duplicate names are harmless. Tracks with elements or markers must
first have those components moved or deleted. Each removed track releases its
bindings, local grid and legend, axis references, pointer array and name.
The remaining axis chain is then synchronized for redraw.

Graph teardown follows a different entry point: after dependent components are
disposed of, ``Rbc_DestroyTracks`` releases the entire list before axes are
destroyed. It does not revalidate user-facing dependencies or schedule redraw.
Keep binding tables, the Tk window and other component cleanup resources alive
until their owners have been released; see :doc:`graph-lifecycle`.

Maintenance checks
------------------

Changes to ownership should cover failed configuration without partial commits,
axis retention across replacement, complete and invalid permutations, duplicate
delete requests, dependency rejection, and graph teardown. Layout changes need
normal and inverted orientations, undersized fitted regions, scrolling,
resizing and resized export. Reordering must also repick a stationary pointer
after layout, without requiring a new motion event.
