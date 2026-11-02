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

Option transaction internals
----------------------------

Tk owns the retained option objects. The seven private transaction records
hold parsed candidates separately from live derived fields. They are local
to one configuration attempt, not persistent state on an axis.

.. list-table::
   :header-rows: 1

   * - Family
     - Candidate ownership
     - Commit effect
   * - Limits
     - Four scalar values and a changed-field mask
     - Replace only staged display/scroll limits.
   * - Pixels
     - Three integer values and a mask
     - Replace widths and scroll increment.
   * - Loose ranges
     - Two policy values and a staged flag
     - Replace both endpoint policies together.
   * - Ticks
     - Owned major/minor arrays and a mask
     - Transfer arrays and update explicit-tick flags.
   * - Shadows
     - Owned Tk colour references with offsets and a mask
     - Transfer colours into text styles; release old references.
   * - Binding tags
     - One owned string-list allocation and a staged flag
     - Transfer the list and free the old allocation.
   * - Limit formats
     - One owned split-list allocation, count and staged flag
     - Transfer the list/count and free the old allocation.

Preparation initializes fresh storage. Reusing an owning transaction without
first releasing it would lose its candidates. During initial configuration,
preparation finds explicit overrides before parsing effective database/default
objects. This lets a valid explicit override replace an invalid unused default.
Every explicit occurrence is then parsed in caller order. For example, an
invalid first ``-majorticks`` list is not ignored merely because the same
command ends with a valid ``-majorticks`` list.

Option classification uses the canonical option-table name, including synonyms
such as ``-bd``. These helpers assume Tk has already rejected unknown and
ambiguous options. Prefix matching should not be reimplemented independently
inside each transaction family.

Stage helpers acquire replacement storage before freeing an earlier candidate.
An empty candidate is still meaningful: a staged NULL tick array clears the
explicit-tick flag and restores automatic ticks. The staged flag or mask must
therefore be checked separately from the pointer. Commit transfers owned
storage, clears the transferred candidate fields and releases the old live
resource. Cleanup then remains safe after a successful commit. Scalar-only
transactions need no resource destructor.

Parsing policies
~~~~~~~~~~~~~~~~

Display and scroll limits accept finite Tcl expressions; empty values select
automatic limits. The limits transaction starts with current effective values,
then validates the final pairs. When both endpoints are defined, minimum must
be less than maximum. Logarithmic scaling requires every defined display or
scroll endpoint to be positive, including when only ``-logscale`` changes.

Explicit ticks are finite expressions in input order. The parser neither sorts
nor deduplicates them. It rejects counts beyond ``MAXTICKS`` before allocation,
checks the variable-sized array calculation for overflow and reports allocation
failure through the interpreter. Empty lists select automatic generation.
Widths must be nonnegative; scroll increment must be positive. Pixel conversion
uses the widget's screen scaling. Loose-range policies accept one or two Tcl
booleans or the exact word ``always``; a single entry applies to both ends.

Limit formats are a list of at most two strings. Each string permits literal
text, escaped percent signs and at most one floating-point conversion accepting
a ``double``. Dynamic width/precision and incompatible conversion types are
rejected. Format length, width and precision are bounded by ``TICK_LABEL_SIZE``.
Those input bounds are not a proof that every formatted numeric result fits
that many characters; output routines must retain their own buffer bounds.

Expression evaluation boundary
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Limit and explicit-tick parsers use ``Tcl_ExprDoubleObj``. Unlike command-prefix
validation, this can execute substitutions, variable traces and Tcl commands.
The transaction rolls back RBC candidate resources and Tk option changes; it
cannot undo arbitrary Tcl side effects. The low-level parsers do not themselves
retain graph/axis lifetime or validate it after evaluation. Treat them as Tcl
evaluation boundaries when auditing reentrancy, rather than assuming that all
configuration parsing is callback-free.
