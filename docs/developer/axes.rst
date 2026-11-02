Axis ownership and commands
===========================

``rbcGrAxis.c`` owns the graph's named axis registry, option transactions,
margin membership and axis commands. This chapter covers that control layer;
:doc:`mapping` explains coordinate spaces and track viewports. The source
contracts for this batch are in :doc:`axis-reference`. Tick generation,
label layout and drawing are separate stages and are not fully covered here.

Names, references and margin membership
---------------------------------------

The graph owns ``axes.table``. Each record owns its copied name and derived
resources. Name lookup and reference acquisition are different operations:

.. list-table::
   :header-rows: 1

   * - Operation
     - Ownership effect
   * - ``NameToAxis``
     - Borrow a live record; reject pending deletion.
   * - ``GetAxis`` with no class
     - Borrow a live record without increasing its count.
   * - ``GetAxis`` with an X/Y class
     - Acquire a reference and enforce compatible logical axis type.
   * - ``FreeAxis``
     - Release one reference; destroy only if deletion is pending and count becomes zero.
   * - ``DestroyAxis``
     - Unconditionally release the record and resources.

An unreferenced live axis stays registered. A delete-pending referenced axis
stays allocated but is excluded from ordinary name lookup and ``axis names``.
Recreating its name revives the same record, retaining its references and
resources. This is different from allocating a replacement record.

``classUid`` represents logical X/Y identity, not the current screen direction.
An untyped or unused record may acquire a class; a referenced record cannot be
acquired with the opposite class. Inversion changes margin selection rather
than exchanging logical data coordinates.

The margin chain controls display order. ``linkPtr``, ``chainPtr`` and
``AXIS_ONSCREEN`` describe membership independently of ``refCount``. Do not
use membership as proof of a retained reference. The four default axes receive
one reference during initialization. Track-owned Y axes are arranged through
track configuration; margin ``use`` rejects that arrangement in trackgraphs.

Configuration boundary
----------------------

Initialization uses the named ``Axis`` option-database component, including
component-name normalization. Initialization failure releases partially
installed Tk resources. Even an empty explicit option vector must pass through
configuration so database defaults become derived axis state.

``ConfigureAxisOptions`` holds a Tk saved-options transaction and temporarily
exposes the original argument vector and change mask to ``ConfigureAxis``.
The vector is borrowed, never retained after the call. Derived limits, ticks,
loose ranges, pixels, shadows, binding tags and limit formats are prepared
before committing any of them. Command-prefix validation does not execute
Tcl callbacks. On recoverable failure, prepared resources are released and
Tk-managed options are restored. Success refreshes styles and title extents,
marks mapping dirty, requests redraw and records a pending configure
notification; it does not synchronously notify listeners here.

The atomic boundary is one axis. Multi-axis ``configure`` validates names
first but applies configuration sequentially. A later failure does not undo
an earlier successful axis. Likewise, multi-name deletion can partially
succeed before a later lookup or track-ownership error.

Dispatch and bindings
---------------------

Named-axis dispatch receives the full widget command vector. Margin dispatch
selects the first eligible axis and passes an argument suffix to its handler.
Keep these conventions distinct when sharing command helpers.

Bindings use graph-owned interned keys for the axis name, logical class and
configured tags, in that order. ``axis bind`` can create bindings for a tag
without an existing axis. Its tag listing is not a list of live axes.
``axis names`` excludes pending deletion and follows hash order; margin
``use`` queries follow display-chain order. Picking queries check the binding
context before interpreting the current item as an axis.

Cleanup and failure recovery
-------------------------------

``DestroyAxis`` cancels pending scrollbar notification and releases bindings,
margin links, registry membership, Tk options and derived resources. Text
styles own their GCs, while options own configured fonts and foreground
colours; separately allocated shadow colours require separate release.
Graph teardown destroys all axes regardless of reference counts, so consumers
must already have been detached. Partial default-axis initialization is left
for that teardown path.

Margin ``use`` validates the complete replacement before changing chains or
logical types. Invalid names or incompatible axes leave all margin lists
unchanged, including axes that would have moved from another margin.

Failed creation frees a new record. Failed revival instead restores pending
deletion and the previous notification-validity flag after configuration
rollback, preserving existing consumers. A later successful revival exposes
the same record again; releasing its final reference while still deleted
continues to destroy it normally.
