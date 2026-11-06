Element data and dispatch
=========================

``rbcGrElem.c`` owns shared element commands, common data transactions and iteration over the graph's element display
list. Concrete element modules supply configuration, extents, mapping, drawing, export and closest-point operations
through :c:type:`ElementProcs`. The private records and declarations live in ``rbcGrElem.h``; see
:doc:`element-reference` for selected contracts.

Data slots
----------

:c:type:`ElemVector` represents one common data input: X, Y, weights or an error-bar input. Its ownership depends on how
it was populated:

.. list-table::
   :header-rows: 1

   * - Source
     - Owned by the slot
     - Borrowed by the slot
   * - Tcl list of numeric expressions
     - Converted double array
     - Owning element pointer
   * - Named real RBC vector
     - Vector client ID
     - Vector record, numeric array and owning element pointer

:c:func:`Rbc_ParseElemVectorObj` first tries named-vector lookup. Otherwise it parses the option as a list of numeric
expressions. This shared parser accepts real vectors only; polar complex input has a separate implementation and must
not be inferred from this parser's contract. Parsing initializes the candidate, so passing an already populated
candidate would lose its owned resources.

:c:func:`Rbc_FreeElemVector` detaches a client ID or frees a literal array, then zeros the slot. It never destroys the
named source vector. Borrowed vector data must not be freed or treated as stable across source updates. See
:doc:`vectors` for vector storage and observer lifetime.

Staging and commit
------------------

The live element must not receive partially validated inputs. :c:func:`Rbc_PrepareElemDataTransaction` collects common
data into temporary slots. On initial configuration it handles relevant defaults; explicit arguments are processed in
their original order. Looking only at the final retained Tcl option objects would hide an invalid earlier occurrence of
a repeated option.

The transaction also preserves the ordering relationship between paired ``-data`` input and separately supplied X/Y
inputs. A replacement candidate is parsed before discarding an earlier candidate for the same destination. Failure
frees staged resources and leaves the live slots unchanged.

Concrete configuration procedures prepare other resource transactions as needed, including axes, pens, styles, tags and
state. All fallible preparation must finish before committing these resources. This complements Tk option rollback; it
is not replaced by it. See :doc:`configuration`.

:c:func:`Rbc_CommitElemDataTransaction` releases replaced live slots and transfers the staged records into them. Only
then does the private :c:func:`CommitElemVector` install the named-vector callback: its client data must point to the
stable member of the element, never a temporary stack candidate. Commit clears the staged mask and moved
candidates. Freeing the transaction afterward is safe. An abandoned successful transaction must also be freed.

:c:func:`Rbc_ElemDataTransactionPointCount` returns the smaller effective X/Y length without committing. It selects
staged or retained slots and queries current named-vector lengths without consuming pending notifications. Literal
inputs use their stored counts. It is an ordinary paired-data helper, not the polymorphic point-count service used for
polar elements.

Updates and invalidation
------------------------

:c:func:`rbcGrElemData.VectorChangedProc` receives the stable data slot as client data. On update it reads the changed
interval while that notification is active, reacquires the vector record through its ID and refreshes the borrowed
array, length, capacity and extrema. On destruction it clears the client ID, array and length, leaving no usable data in
that slot.

Pending intervals are accumulated until consumed by element mapping. Once an unknown or full change has been seen, later
precise intervals cannot make the combined update precise again. This information allows concrete mapping code to choose
an appropriate cache update; it is not a promise that every element performs partial remapping.

The callback marks the graph's axes and the element's mapping dirty. It requests backing-store redraw when the element
is neither hidden nor plot-hidden. It does not draw synchronously. Cached numeric pointers must be refreshed through
this notification path, rather than retained independently by a new component implementation.

Type dispatch and display order
-------------------------------

:c:type:`ElementProcs` separates shared command behavior from element-specific work. Extents describe data bounds for
axis scaling; mapping prepares display geometry; normal and active drawing consume it. Export and legend-symbol
operations have separate callbacks. Closest-point callbacks report through :c:type:`ClosestSearch`, including data
coordinates and an original data index.

:c:func:`rbcGrElemLifecycle.Rbc_MapElements` walks the display list, skipping hidden and plot-hidden elements. It remaps
when the graph requests all items or the element has ``MAP_ITEM`` set. Each mapping call runs within a paired track
viewport scope, and the item flag is cleared afterward. Native remapping also records a graph change for interaction
updates; export mapping suppresses that screen event.

Normal drawing follows display-list order with the same visibility checks. Track drawing enters the owning viewport and
skips collapsed visible regions. Scrolled tracks and track bar elements additionally use a renderer clip around the
complete drawing call, including raised bar borders. A viewport scope alone does not install that renderer clip. See
:doc:`mapping` and :doc:`rendering`.

A concrete implementation must keep drawing reductions separate from source identity. Decimation or clipping can change
the mapped arrays without changing which original sample a closest-point result identifies. Legend symbols also have
their own geometry and must not be mistaken for plot pixels by image tests.

Destruction
-----------

:c:func:`rbcGrElemLifecycle.ReleaseElementResources` clears borrowed configuration arguments and invokes the concrete
destructor before freeing Tk-managed option resources. Derived GCs may still refer to colors, fonts or bitmaps owned by
the option table. The concrete destructor is called even after failed option initialization: constructors can already
have allocated arrays, chains or embedded pens.

Release flags prevent repeating resource cleanup. The graph's element-resource release pass runs while the Tk window
context is available; element record and registry cleanup follow their owning lifecycle. New element types must release
vector clients, owned arrays and derived resources without destroying external source vectors or double-freeing Tk-owned
values.

See :doc:`decimation` for dense line mapping, cache maintenance and source picking.

See :doc:`bars` for bar grouping, placement, error translations and mapped ownership.

See :doc:`fills` for area closure, uncertainty-band runs and fill resource ownership.

Synchronous layout before idle notifications
--------------------------------------------

Vector updates normally notify clients at idle. A command can replace vector storage and immediately request export or
axis limits before that notification runs. :c:func:`rbcGrElemData.Rbc_SyncElementVectors` refreshes pending coordinate,
weight and error slots before graph layout or axis reset uses their borrowed arrays. It marks mapping and ranges dirty
without delivering client callbacks or processing Tcl events. A full change is recorded because exact ranged metadata is
only available inside the notification callback; the queued notification remains scheduled normally.

Transaction records and failure rules
-------------------------------------

The shared configuration helpers operate on fresh temporary records. Prepare functions zero their output before use:
they are not replacement operations for a transaction that still owns candidates. Release or commit that earlier
transaction first. The concrete configurator prepares all required families before making live-field commits; a
successful prepare in one family does not permit committing it while another family has failed.

.. list-table::
   :header-rows: 1
   :widths: 20 40 40

   * - Candidate family
     - Preparation and ownership
     - Commit/abandon behavior
   * - Tags
     - One Tcl-allocated block; empty input can be explicitly staged as NULL
     - Commit moves it and frees old live tags; free releases the candidate
   * - Axes
     - One acquired reference per staged X/Y mapping
     - Commit moves staged references and releases old mappings; free releases candidates
   * - State
     - Scalar value and staged flag, no owned resource
     - Commit only after TCL_OK; failed preparation can retain an earlier staged scalar
   * - Data
     - Owned numeric arrays or named-vector client IDs
     - Commit moves slots and binds callbacks to stable element fields; free releases candidates

Tag and axis preparation clean their staged resources before returning an error. State preparation has nothing to
release and may leave an earlier valid scalar marked staged when a later occurrence fails. Therefore checking ``staged``
alone is insufficient: the caller must first check the preparation return code. State commit does not independently
schedule redraw or repick bindings. Those remain responsibilities of the enclosing configuration path.

Axis staging requires an existing axis and does not accept an empty mapping. Lookup checks the orientation of an in-use
axis; an unused or untyped axis can acquire the requested class at lookup. Candidate cleanup releases the reference, but
does not restore that class field. The guarantee here is unchanged live element mappings, not an unchanged axis
registry. Staging acquires a successor before releasing the previous candidate, even if both resolve to the same
record. At commit, each staged reference is moved to the live field before the old reference is released. A final
release can destroy a delete-pending axis. Empty pen selection has different semantics; see :doc:`pens` rather than
applying the axis rule to all named resources.

Original arguments and repeated options
---------------------------------------

The identity helpers use the element's concrete option table through :c:func:`Rbc_GetCanonicalOptionName` after Tk
validation. They recognize canonical options, synonyms and accepted abbreviations. In particular, exact ``-x`` and
``-y`` retain priority over longer names such as ``-xerror``. These helpers do not parse values or authorize a commit.

Preparation first identifies which options were supplied explicitly. On first configuration, it processes retained
defaults only for identities that were not explicitly supplied, then walks the original explicit option/value sequence
in order. This serves two purposes: an explicit value can override an invalid unused default, but a later valid repeated
option must not hide an invalid earlier explicit occurrence. Reading only the final Tk-managed object fields would lose
that distinction.

Data staging applies the same rule to aliases between destinations: ``-data`` replaces both X and Y candidates, whereas
a later independent coordinate option replaces only that coordinate. Both candidates from a paired parse must succeed
before either previous candidate is released. The live element remains separate from all of this temporary storage.

Retained coordinate representations
-----------------------------------

Internal numeric arrays and the objects reported by configuration queries are different representations with separate
ownership. After a successful data commit, :c:func:`Rbc_SyncElemDataOptionObjects` replays initial defaults and explicit
arguments in the same order used for staging. It does not reevaluate numeric expressions or reconstruct input strings
from floating-point results.

For example, with expression objects preserved in the input:

.. code-block:: tcl

   .g element configure trace -data {1 {2+3} 2 {4+5}} -y {7 8}

The later Y value makes the retained paired ``-data`` representation invalid, so it is cleared; X retains the values
derived from the earlier pairs. Reversing the order lets the final ``-data`` establish both coordinates and a coherent
paired representation again. The individual X/Y lists contain the original coordinate objects, preserving expression
spellings such as ``2+3``.

:c:func:`ReplaceElemOptionObject` retains a new object before releasing the old one and handles identical pointers as a
no-op. During first configuration, synchronization also pins initial option objects while replacing fields that may own
their last reference. :c:func:`SetElemDataPairOptionObjects` assumes prior validation: a malformed or odd-length list at
this phase is an internal fatal error, not a second user-input validation opportunity. Run synchronization while the
first-configuration flag still describes the configuration being committed.

Callback boundaries
-------------------

String-list parsing for tags and exact state parsing do not execute binding scripts. Common numeric data parsing is
different: list entries are expressions, and their evaluation can invoke Tcl commands or variable traces. Candidate
staging protects the live value from a partial commit, but does not by itself preserve the graph, element or borrowed
arguments across such callbacks. Audit those lifetimes at the caller and parser boundary separately; these contracts do
not establish general reentrancy for configuration.

:c:func:`CommitElemVector` installs a vector-change callback only after moving the slot to its stable destination. A
callback must never retain a pointer to a temporary transaction field. Moving the client ID transfers observation
ownership, not ownership of the named source vector or its storage.

Parsing, allocation and extrema
-------------------------------

:c:func:`GetElemArrayByteCount <rbcGrElemData.GetElemArrayByteCount>` rejects negative counts, zero element size and
products that exceed SIZE_MAX, assigning its result
only on success. Zero count is valid. This check cannot repair overflow that occurred while a caller computed the count
before invoking it.

:c:func:`EvalExprListObj <rbcGrElemData.EvalExprListObj>` initializes its outputs to zero/NULL and uses Tcl_AttemptAlloc
for a nonempty result. Each list member is evaluated with Tcl_ExprDouble; an error releases the temporary numeric array.
The helper borrows the list's object-pointer array across evaluation. It does not take an immutable snapshot or add an
owner-preservation boundary, so this local allocation cleanup must not be confused with protection against callbacks
that mutate owners or input representations.

:c:func:`ParseElemVectorPairsObj <rbcGrElemData.ParseElemVectorPairsObj>` evaluates the flat input before checking
whether its length is even. An odd-length failure can
therefore follow expression side effects; it is not a side-effect-free prevalidation pass. Successful parsing uses a
temporary flat array followed by separate X and Y arrays, computes extrema, and releases the temporary array. This
paired-input parser always treats input as expressions, unlike :c:func:`Rbc_ParseElemVectorObj`'s initial named-vector
lookup.

The range helpers have distinct contracts:

.. list-table::
   :header-rows: 1
   :widths: 26 40 34

   * - Helper
     - Behavior
     - Empty/unusable data
   * - :c:func:`SyncElemVector <rbcGrElemData.SyncElemVector>`
     - Borrows source storage and obtains source-maintained extrema
     - Requires a live source vector
   * - :c:func:`FindRange <rbcGrElemData.FindRange>`
     - Scans literal data, ignoring nonfinite samples
     - Empty/NULL leaves cached extrema untouched; all-nonfinite nonempty data gives DBL_MAX/-DBL_MAX
   * - :c:func:`Rbc_FindElemVectorMinimum <rbcGrElemData.Rbc_FindElemVectorMinimum>`
     - Returns the smallest finite value at or above minLimit
     - Returns minLimit if its accumulator remains DBL_MAX

Neither scanning helper computes and stores a separate positive minimum. The bounded-minimum helper also treats a sole
qualifying DBL_MAX value as its unchanged sentinel and returns minLimit. That is current compatibility behavior, not a
promise that every finite sample has a distinct successful result.

Notification metadata and synchronous refresh
---------------------------------------------

:c:func:`RecordElemVectorChange <rbcGrElemData.RecordElemVectorChange>` merges inclusive source intervals by their outer
bounds. An unknown/full change dominates later
precise ranges until the owning mapping code consumes or resets that pending state. The helper itself neither validates
indices nor fetches data. Reading the vector's client-specific changed range belongs inside
:c:func:`VectorChangedProc <rbcGrElemData.VectorChangedProc>`, while
that notification snapshot is valid.

The destruction notification clears clientId, valueArr and nValues and records a full change. It does not zero vecPtr,
capacity or extrema. Those remaining fields must not be treated as valid source storage after destruction; the cleared
live-data fields govern subsequent use.

:c:func:`Rbc_SyncElementVectors <rbcGrElemData.Rbc_SyncElementVectors>` visits the registry rather than only the visible
display list. Pending common X/Y, weight and error inputs refresh their borrowed arrays and mark a full change before
immediate layout or export. It neither delivers nor cancels the queued notification and does not independently queue a
redraw. :c:func:`VectorChangedProc <rbcGrElemData.VectorChangedProc>` has a different scheduling policy: it always marks
axes and mapping dirty, but only schedules backing-store redraw for plotted elements.

Value-label formatting
----------------------

:c:func:`Rbc_ValidateValueFormat <rbcGrElemData.Rbc_ValidateValueFormat>` accepts literal-only strings, escaped percent
signs and at most one floating conversion. Formatting passes one double at a time; even SHOW_BOTH formats X and Y
separately with the same format and joins them with a comma. Dynamic widths/precisions and conversions requiring another
argument type are rejected. Format length, width and precision are each bounded by the current VALUE_FORMAT_LIMIT of
200. Validation does not guarantee that every formatted number fits the destination buffer.

:c:func:`FormatValueLabel <rbcGrElemData.FormatValueLabel>` supplies terminated, bounded fallback text. It avoids
appending Y when X has already been truncated.
:c:func:`Rbc_GetElementValueLabel <rbcGrElemData.Rbc_GetElementValueLabel>` creates and retains that fallback before
executing application code. A nonempty command prefix is duplicated, then receives graph path, element name, original
index, X and Y in the global Tcl evaluation context. The returned label owns one Tcl reference which the drawing caller
must release. Successful callback text is not limited to the fallback buffer size.

Command-prefix validation checks list syntax only; it does not check command existence. Empty prefixes retain fallback
formatting. Callback errors are reported through the background exception mechanism and use fallback text, while the
enclosing interpreter state is restored. This result preservation is not lifetime protection for drawing records:
value-label callbacks must not mutate drawing state or enter a nested event loop.

Offsets are a separate parser: :c:func:`Rbc_GetValueOffset <rbcGrElemData.Rbc_GetValueOffset>` requires two signed Tcl
ints, commits both together, and accepts no screen-unit suffixes. It stores the parsed values in the double fields of
:c:struct:`Point2D` without evaluating expressions.

Closest-result source values and traces
---------------------------------------

:c:func:`ClosestErrorValue <rbcGrElemData.ClosestErrorValue>` rejects a pending named-vector update before touching its
borrowed array. It also rejects out-of-range indices, NULL arrays and nonfinite samples.
:c:func:`AppendClosestErrors <rbcGrElemData.AppendClosestErrors>` reports the original source error values, including
explicit low/high bounds, rather than transformed or bar-translated positions. A nonempty symmetric-error vector takes
precedence; an unusable symmetric sample does not fall back to explicit bounds. Explicit low and high must both be
usable before either is reported for that axis.

Closest results are collected into a private dictionary before any destination array unset or write. The snapshot owns
common fields, mapped bar rectangles, source error values and concrete line/polar metadata. Concrete closest-info
callbacks append values to this dictionary without running Tcl scripts.

Publication clears the same optional error fields and writes snapshot entries in collection order. Both unset and write
traces may change vectors, delete the selected element or destroy the graph: publication makes no further graph or
element reads. A failing write returns its diagnostic and leaves earlier writes in place; this is a stable snapshot, not
an atomic Tcl-array transaction. The snapshot is released on success and failure. Existing unrelated/stale fields other
than the six optional error fields retain their previous behavior.

:c:func:`GetIndex <rbcGrElemData.GetIndex>` receives a captured point count before evaluating non-``end`` input as a Tcl
expression. It converts the result to Tcl_Size and checks against those captured bounds.
:c:func:`rbcGrElemCommands.ActivateOp` supplies lifetime protection and checks all staged indices against current
lengths before commit. Exact ``end`` avoids expression evaluation; empty elements are rejected before either path.

Registry and display-list ownership
-----------------------------------

The element registry owns records; the display chain contains borrowed pointers.
:c:func:`rbcGrElemLifecycle.NameToElement` searches the registry without filtering visibility. Creating an element
reserves its registry entry before initializing options, then configures both defaults and explicit arguments. Failure
destroys the partial record. Success prepends the record to the display list and invalidates mapping and axes.
Trackgraphs accept line, strip and bar elements through this shared path.

:c:func:`rbcGrElemLifecycle.RebuildDisplayList` parses the complete Tcl list before replacing membership. Malformed
syntax leaves the old list intact. Successful parsing clears the chain and marks every registered element hidden, then
appends recognized names in input order and unhides them. Unknown names are ignored; repeated names retain only their
first occurrence. The temporary hidden state doubles as the membership check, so rebuilding needs no additional set or
repeated chain scans. This is not deletion: omitted elements remain addressable in the registry.

:c:func:`rbcGrElemLifecycle.DestroyElement` removes bindings, legend references, resources, all matching chain links and
the registry entry before freeing the name and record. Rebuilding maintains unique membership; destruction also
defensively removes every borrowed reference, saving the next link before unlinking the current one. Redraw is queued
once when a non-hidden element was displayed.

Option initialization and resource lifetime
-------------------------------------------

:c:func:`rbcGrElemLifecycle.InitElementOptions` obtains the element component resource name and uses class
:c:type:`Element`. It is a no-op after successful initialization. If Tk option setup fails, it frees partially
initialized option values and the option table; the concrete constructor's own allocations still need destruction.

:c:func:`rbcGrElemLifecycle.ConfigureElementOptions` retains Tk saved options while validating track ownership and
running concrete configuration. Trackgraph Y axes must belong to the selected track; a nonempty track is rejected on
ordinary graphs. The original argument vector and option mask are borrowed transaction context, cleared before restoring
options on failure or releasing saved options on success. Concrete configuration must still stage derived resources: Tk
rollback alone cannot undo arbitrary writes to live C fields. The optional output mask is written only on success.

Resource teardown and record teardown are separate. :c:func:`rbcGrElemLifecycle.Rbc_ReleaseElementTkResources` walks the
registry while the Tk window remains usable, including elements omitted from the display list. Repeated resource release
is guarded. :c:func:`rbcGrElemLifecycle.Rbc_DestroyElements` later destroys records without deleting hash entries during
hash traversal, then deletes both hash tables and the display chain. :c:func:`rbcGrElemLifecycle.DestroyElement`
releases resources and registry membership immediately, but uses Tcl_EventuallyFree for the record itself so an
activation callback can retain its identity until validation ends. Preservation does not keep released resources
usable. That final container destruction is not an idempotent operation.

Normal, active and export passes
--------------------------------

The common dispatcher consumes geometry already mapped by concrete elements. Normal and active passes share visibility
and track-size checks; the active pass additionally requires ``ELEM_ACTIVE``. The enclosing graph decides pass ordering.
All these traversals borrow records and are not snapshots that tolerate arbitrary callback-driven deletion during
drawing.

Screen track clipping intersects inclusive track bounds with graph dimensions before opening the renderer clip. Export
uses the track rectangle in export geometry. Both normal and active export callbacks support the selected backend; only
the diagnostic element comments are PostScript-specific. Viewport entry and renderer clipping are separate, balanced
scopes. Scrolled tracks and track bar elements use the full-callback clip; other paths retain their existing concrete
clipping behavior.

:c:func:`rbcGrElemLifecycle.Rbc_GraphUpdateNeeded` is deliberately narrower than a general redraw predicate. It checks
pending X/Y vector notifications only for visible display-list elements. It neither processes notifications nor
synchronizes arrays, and does not inspect weights, error inputs or concrete polar data slots. Callers needing current
data must use the appropriate synchronization and layout path, not treat a false result as proof that every input is
current.

Command dispatch and query domains
-----------------------------------

:c:func:`rbcGrElemCommands.Rbc_ElementOp` validates the operation and argument count using the typed elemOps table
before calling a handler. The supplied class UID selects creation behavior; most other handlers operate on the named
record regardless of that UID. This entry point does not add record lifetime protection around Tcl evaluation.

Queries do not all enumerate the same collection:

.. list-table:: Element query domains
   :header-rows: 1

   * - Operation
     - Source and order
   * - names
     - Registry hash order; optional glob patterns are combined with OR.
   * - exists / type / cget
     - Exact registry lookup, including hidden records.
   * - activate without a name
     - Registry hash order filtered by ELEM_ACTIVE, including hidden records.
   * - show
     - Display-chain order, including hideplot entries.
   * - bind without a tag
     - Interned tag-table keys, even without live elements or binding scripts.
   * - get current
     - Current binding item only when the context and concrete class are elements.

:c:func:`rbcGrElemCommands.GetOp` currently treats an unsupported selector as a successful no-result operation, not an
error. It does not perform picking. :c:func:`rbcGrElemCommands.CgetOp` delegates to Tk option-value retrieval; configure
queries use Tk option-information retrieval instead.

Binding tokens
--------------

:c:func:`rbcGrElemCommands.Rbc_MakeElementTag` interns a string in the graph's element tag table and returns the
table-owned key as the binding token. :c:func:`rbcGrElemCommands.Rbc_AppendElementBindingTags` appends name, class and
configured tags in that order. It neither clears the receiving list nor removes repeated tags. These helpers assemble
lookup keys; they do not run binding scripts. Tokens remain valid until the tag table is destroyed.
:c:func:`rbcGrElemCommands.BindOp` may intern a new tag even when subsequent binding configuration fails.

Mutation boundaries
-------------------

:c:func:`rbcGrElemCommands.ConfigureOp` resolves the leading names before mutation, then configures them
sequentially. Each element has its own Tk/derived-resource transaction; a failure on a later element does not roll back
earlier ones. Queries with several names return information for the first one. Successful mutations synchronize hidden
state with display membership and invalidate axes, mapping and layout. The final redraw scheduling is reached only after
the loop completes successfully.

:c:func:`rbcGrElemCommands.DeleteOp` and :c:func:`rbcGrElemCommands.DeactivateOp` instead resolve and act on each name
in sequence. A later missing name leaves earlier changes in place. Repeated deletion names therefore produce an error
after the first deletion. Deactivation clears the active flag and owned index array; if a later lookup fails, its final
redraw request is not reached. These operations do not promise command-wide atomicity.

:c:func:`rbcGrElemCommands.ActivateOp` stages explicit indices before replacing the old selection. Allocation or
ordinary conversion failure frees the candidate without changing the old selection. No explicit indices means
whole-element activation, represented by a count of -1. Explicit indices retain order and duplicates, and successful
commit sets both ELEM_ACTIVE and ACTIVE_PENDING.

Index expressions may invoke application code. :c:func:`rbcGrElemCommands.ActivateOp` preserves the element record
during evaluation, while the widget dispatcher preserves the graph. After each successful evaluation it checks window
existence and the element's resource-release flag before accessing data again. Deletion, same-name recreation, or widget
destruction cancels the outer activation with error code ``RBC ELEMENT CALLBACK``. Preserving storage prevents allocator
address reuse from making a replacement look like the original element.

Before commit, every staged index is checked against the current point count, including indices evaluated before a later
expression changed the data. Real and complex named-vector lengths are read directly, without waiting for idle
notifications or consuming them. Exact end is resolved using the current length at its own evaluation; its resulting
numeric index is revalidated at commit. Pending growth is therefore visible to a subsequent index, and pending shrink
cannot commit an out-of-range selection.

Evaluation/conversion errors keep their original diagnostics. Candidate storage and preservation scopes are released on
all exits. The outer command leaves activation unchanged on failure, apart from any deliberate callback side effects;
these are not rolled back. Successful nested activation is permitted, and a successful outer command then replaces that
selection with its own candidate.

Closest-search phases
---------------------

:c:func:`rbcGrElemCommands.ClosestOp` synchronizes common data inputs and resets axes when requested before parsing
coordinates and switches. It does not map dirty elements as part of the search: records marked MAP_ITEM or with pending
X/Y notifications are skipped. Explicit hidden or unplotted names cause an error; an implicit search skips them and
visits the display chain in reverse. Track eligibility additionally requires a noncollapsed viewport containing the
query position.

:c:func:`rbcGrElemCommands.ParseClosestOptions` starts at argument six and accepts unambiguous switch
abbreviations. Direction values must be exactly x, y or both; halo values use nonnegative screen-distance conversion;
interpolation uses Tcl boolean parsing. Parsing stops at the first non-option or after --. It updates a local search
record incrementally, so parse failure need not roll back already parsed fields. The final hit comparison is strictly
less than the halo, not less than or equal.

Search and metadata collection finish before :c:func:`rbcGrElemCommands.PublishClosestInfo` runs any array traces. The
caller retains the result dictionary while publication clears six optional error fields and writes the snapshot. A miss
writes an empty name; other old metadata fields are not generally erased. A write error preserves preceding writes and
returns its diagnostic. Concrete closest-info collectors must remain script-free for this separation to protect graph
lifetime.

For the line-specific complex and parallel-parameter transactions, see :doc:`polar` and the grouped contracts in
:doc:`polar-reference`. Those paths have different input restrictions and notification policies from common X/Y data.

Line-family geometry ownership
------------------------------

Line, strip and polar elements share a line record, with type-specific option specifications and polar dispatch where
needed. :c:func:`rbcGrLineGeometry.Rbc_LineElement` allocates the record, duplicated names, built-in pen and palette;
common element creation still performs registration and option initialization. :c:func:`rbcGrLineGeometry.Rbc_LinePen`
similarly creates an unregistered pen. Both use :c:func:`rbcGrLineGeometry.InitPen` on zero-allocated storage; it
installs callbacks and defaults without initializing Tk options. Neither factory produces configured drawing GCs.

:c:func:`rbcGrLineGeometry.ConfigurePen` stages derived resources separately from Tk option values. All fallible
conversions precede replacement GC construction and commit. Failure releases staged colors, shadow and bitmap
resources; the surrounding transaction restores Tk options. Symbol, text and error-bar GCs use Tk ownership, whereas the
trace GC is private so its dash state can be set independently. :c:func:`rbcGrLineGeometry.DestroyPen` releases derived
resources, leaving option values and record disposal to its owner.

Mapped points and discontinuities
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrLineGeometry.GetScreenPoints` starts with an empty output map and creates parallel screen-point and
original-index arrays. Nonfinite source or mapped coordinates are omitted; gaps in original indices create break
flags. This stage does not clip to the viewport. Source synchronization and disposal of any previous map are caller
responsibilities. Allocation failure leaves an empty map without a Tcl error.

:c:func:`rbcGrLineGeometry.ReducePoints` simplifies continuous runs separately and commits replacement storage only
after all allocations succeed. :c:func:`rbcGrLineGeometry.GenerateSteps` also preserves breaks; it inserts the data-X
change before the data-Y change, including for inverted axes. Inserted points retain the following source index. A
failed step allocation keeps the old map and falls back to effective linear smoothing, without changing the requested
option. These are display-geometry operations, not vector edits.

:c:func:`rbcGrLineGeometry.ScaleSymbol` captures axis-range baselines when requested, applies valid range ratios and
returns a bounded odd pixel size. :c:func:`rbcGrLineGeometry.MapActiveSymbols` separately rebuilds explicit active
selections, retaining their original indices and filtering them through symbol-map extents. It discards old active
geometry before allocating and clears ACTIVE_PENDING even on allocation failure; retained selection indices are
unchanged.

Reset versus destruction
~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrLineGeometry.ResetLine` frees mapped traces, symbols, strips, fill/error-band polygons and error bars,
together with their source-index arrays. :c:func:`rbcGrLineGeometry.ClearPalette` only clears style counts; it does not
own those shared arrays. :c:func:`rbcGrLineGeometry.FreeTraces` owns each trace's arrays and record as well as the
chain. :c:func:`rbcGrLineGeometry.ResetLine` retains source subscriptions, pens, activation indices and the source
decimation cache. Explicit active selections become pending again for the next mapping pass.

Error-bar filtering takes place before :c:func:`rbcGrLineMapping.MergePens` reorders palette
geometry. :c:func:`rbcGrLineGeometry.FilterLineErrorBars` selects source indices using the same palette traversal and
symbol interval policy, then compacts both error-bar arrays in place. Shafts and caps follow their source point
together; allocation capacity is unchanged.

:c:func:`rbcGrLineGeometry.DestroyLine` additionally releases subscriptions, pen references, the decimation cache,
palette, tags and fill resources. Common lifecycle code guards this callback against repeated release and handles
record/name and Tk option cleanup. It must not be treated as an independently repeatable destructor. Referenced vectors
remain owned by their creators.

Font-change handling resets named-pen value styles through the pen registry and built-in styles through the element
registry, including hidden elements. The caller remains responsible for arranging layout and redraw.

Line mapping pipeline
---------------------

:c:func:`rbcGrLineMapping.MapLine` runs with the graph's axes and viewport already prepared. It consumes pending
vector-change metadata after refreshing or invalidating source-domain decimation summaries, then resets old drawing
geometry. Error-band mapping precedes center-line mapping. Cached and uncached pre-map reduction are attempted only when
eligible and sufficiently dense; full point mapping is the fallback. See :doc:`decimation` for the source-summary and
picking contracts.

Normal symbol centers are copied before smoothing or trace reduction. Their mapping bounds include conservative padding
from every palette pen and the active pen, retaining centers outside the plot whose symbols may overlap it. This
expansion is not a drawing clip: renderers still clip to the viewport. Explicit active symbols are mapped separately.

Smoothing, gaps and symbol selection
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`GenerateSplineRuns <rbcGrLineMapping.GenerateSplineRuns>` copies each continuous run and smooths it
independently, then joins the arrays while restoring break
flags. Runs shorter than three points stay unchanged. An individual generator can fall back for one run while others
remain smoothed. Failure to allocate or combine the temporary runs keeps the original complete map and sets effective
smoothing to linear. The requested option remains unchanged. :c:func:`rbcGrLineMapping.FreeMapRun` releases temporary
arrays and clears their record; :c:func:`rbcGrLineMapping.CopyMapRun` requires an unused destination and a valid source
interval.

After smoothing, the mapper applies pixel-density reduction where appropriate, then tolerance reduction. Reduced-symbol
mode matches both original index and exact coordinates: a generated step corner or spline point with the same index must
not substitute for an original center. Removing centers marks pointMapOmitted so exact picking can use original
samples. This matching requires source order and must precede palette grouping.

Clipping and palette grouping
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Area filling occurs before connected-trace clipping because :c:func:`rbcGrLineMapping.MapTraces` can change its
temporary input coordinates. Trace breaks respect missing-data boundaries, clipping and the requested
increasing/decreasing data-X direction. :c:func:`rbcGrLineMapping.TraceAbscissa` accounts for inverted and descending
axes; it is an ordering coordinate, not an inverse transform. :c:func:`rbcGrLineMapping.ClipSegment` modifies endpoints
and returns zero for acceptance. :c:func:`rbcGrLineMapping.SaveTrace` copies each surviving run into independently owned
trace storage.

:c:func:`rbcGrLineMapping.MapStrip` instead clips adjacent pairs independently, skips gap crossings and associates each
segment with its starting source index. The element class selects this representation even in a trackgraph containing
both line and strip elements. :c:func:`rbcGrLineMapping.MapStrip` does not apply the connected-line trace-direction
filter.

The general mapping path computes palette sizes, maps eligible XY error bars, filters error bars to match selected
symbols, and calls :c:func:`rbcGrLineMapping.MergePens` last. With multiple styles, :c:func:`rbcGrLineMapping.MergePens`
reorders arrays by palette order while preserving order inside each style. Style pointers are borrowed slices of
line-owned allocations, not independently allocated geometry. A single style borrows the existing arrays directly. The
restricted pre-map path uses this single-style case without allocating a source-sized style map.

Mapping is not a transaction over the entire element. Old geometry is discarded first, and individual helpers have their
own empty-result or unchanged-input fallbacks. This differs from configuration transactions that retain old resources
until a candidate is ready.

Source extents
~~~~~~~~~~~~~~

:c:func:`rbcGrLineMapping.GetLineExtents` computes bounds from source data rather than reduced or smoothed screen
geometry. Ordinary XY bounds include error bars and account for positive minimums on log axes. Complex input and
polar-coordinate input instead use a symmetric square enclosing the maximum Cartesian radius. No valid data leaves empty
sentinel bounds; the caller owns source synchronization and axis reset.

Line-family picking and metadata
--------------------------------

:c:func:`rbcGrLinePicking.ClosestLine` chooses points or mapped segments. Automatic mode uses segments when the normal
pen has positive trace width and the element has more than one point. Strip elements search independent segments; line
and polar elements search connected trace chains. Both need current geometry and the correct graph or track
viewport. These helpers do not synchronize pending source changes.

Point and trace metrics
~~~~~~~~~~~~~~~~~~~~~~~

Point picking compares screen coordinates: both uses Euclidean distance, x uses absolute horizontal distance, and y uses
absolute vertical distance. Only centers inside the unpadded plot rectangle participate, even when a symbol whose center
is outside could contribute visible pixels. Hits recover original source indices and data coordinates. Strict
comparisons preserve the earlier candidate on ties.

Directional interpolated picking has different semantics. :c:func:`rbcGrLinePicking.DistanceToX` intersects a segment
at the query's screen X coordinate and measures vertical distance; :c:func:`rbcGrLinePicking.DistanceToY` intersects at
screen Y and measures horizontal distance. A query outside the corresponding endpoint span cannot hit that
segment. Near-vertical or near-horizontal degenerate cases select the nearer endpoint where needed. Euclidean
interpolation instead finds the nearest point on the finite segment.

Interpolated results inverse-map the selected screen coordinate, but their index belongs to the segment's retained
source start. A directional segment search that does not improve the incoming best distance falls back to point
picking. A both-direction segment search has no such fallback.

Source picking after reduction
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrLinePicking.ClosestPoint` uses retained centers unless pointMapOmitted requests a source
search. :c:func:`rbcGrLinePicking.ClosestSourcePoint` maps original samples on demand, rejecting nonfinite coordinates
and centers outside the viewport. Cached monotonic source X can restrict candidate ranges, but the range restriction
must be justified by the actual screen-distance metric.

Pruning is valid for both-direction queries in either orientation. For a single-direction query it is valid only for
screen X in normal graphs and screen Y in inverted graphs. The other direction must retain the full source range: a
sample far away along the ignored coordinate can still be the closest hit. Axis descending settings change the
range-search ordering, not this eligibility.

Collect before publishing
~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrLinePicking.LineClosestInfo` collects an optional parallel parameter value. It skips pending
parameter-vector notifications, mismatched lengths and invalid indices rather than dereferencing stale storage. For an
interpolated hit, param remains the source-index value; it is not geometrically interpolated.

:c:func:`rbcGrLinePicking.PolarClosestInfo` derives radius and normalized radian angle from the final Cartesian hit,
plus the source representation name. Smith results also include gamma, reference impedance, and convertible normalized
and physical impedance and admittance. Exact open/short singularities are represented with positive infinity; other
failed conversions omit their respective fields. Parameter metadata follows the source index while these geometric
fields follow the hit.

The closest-info helpers, including :c:func:`SetClosestInfoObj <rbcGrLinePicking.SetClosestInfoObj>` and
:c:func:`SetClosestInfoPair <rbcGrLinePicking.SetClosestInfoPair>`, build dictionary entries, including two-double lists
for complex pairs. They do not write Tcl variables or run array traces. The common closest handler retains this snapshot
before publishing it, separating geometry access from application trace callbacks. Collection errors return their Tcl
status; previously inserted dictionary fields are not rolled back.

Screen stroke adapters
~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrLinePicking.DrawTraces` batches connected polylines through one trace stroke context;
:c:func:`rbcGrLinePicking.DrawRenderedStrips` submits an independent segment batch through one strip stroke
context. Both borrow mapped geometry and pen resources, resolve the default off-dash color, and end a successfully
created context. Context creation failure skips drawing. These adapters neither map source data nor own persistent
arrays.

Line symbols and output dispatch
--------------------------------

Normal screen drawing and export use the same broad order: area fill, uncertainty band, strokes, then per-style error
bars, symbols and labels. Independent strips use palette pens; connected trace chains use the normal pen. Drawing
consumes mapped geometry. In particular, :c:func:`rbcGrLineOutput.NormalLineExport` does not itself remap or disable
decimation: export preparation owns that responsibility.

Active output is separate. Explicit active indices draw symbols and labels only, refreshing their geometry when
pending. Whole-element activation also draws the available strokes. Neither active path adds fills or error bars. Both
use the active pen and its scaled symbol size.

Symbol selection and fallback
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`rbcGrLineOutput.SymbolInterval` divides total palette center count by the requested maximum. It supplies a
density interval, not a strict count cap. Normal output advances a shared counter across palette slices; styles without
symbols still contribute their center count. Label selection uses a matching interval and slice offset. The screen path
resets its interval after the normal pass; export uses local interval/counter variables. Active export bypasses
sampling.

:c:func:`rbcGrLineOutput.DrawSymbols` tries tiny Cairo points, Cairo bitmap symbols, then Cairo geometric symbols before
native fallback. Tiny points use fill color only and bypass the interval. Geometric fill and outline passes start from
the same counter so they select identical centers; the counter advances once for the entire pass. Segment-shaped
symbols have no fill and retain hairline behavior.

Bitmap drawing scales bitmap and optional mask once per pass. A transparent background uses the bitmap as its own
mask. Renderer setup captures image data before temporary pixmaps are freed; selected centers are submitted in bounded
batches. Explicit drawable targets, used for legends, bypass plot sampling.

Native fallback skips clip setup for wholly interior batches. If conservative symbol bounds cross the plot boundary, it
uses the screen clip adapter and its target drawable. :c:func:`rbcGrLineOutput.DrawSymbolsUnclipped` only translates
pen resources into native renderer style and propagates the sampling counter; it does not establish a clip.

:c:func:`rbcGrLineOutput.GetRenderedSymbolShape` retains native proportions and integer template rounding. None and
bitmap symbols do not have a geometric template. Screen legend samples use explicitly sized targets and their own
fallback sequence. Export legends use a single thicker trace segment, whereas the screen trace sample uses two adjacent
segments; their constructions should not be assumed identical.

Fills and labels
~~~~~~~~~~~~~~~~

:c:func:`rbcGrLineOutput.DrawRenderedFill` attempts Cairo output and reports whether it handled the fill. Tiles take
precedence over patterns, including when a tile cannot be rendered; it does not expose the pattern underneath.
:c:func:`DrawLineFill` owns subsequent native fallback and suppresses an empty tile. Pattern opacity is separate from
tile image transparency. Export fill policy is delegated to its backend. See :doc:`fills` for the resource and geometry
contracts.

:c:func:`rbcGrLineOutput.DrawValues` and :c:func:`rbcGrLineOutput.ValuesExport` filter centers against the unpadded
viewport, select source indices and obtain retained label objects through
:c:func:`Rbc_GetElementValueLabel <rbcGrElemData.Rbc_GetElementValueLabel>`. Each object is
released after drawing or skipping an empty label. Screen labels check offset coordinates against integer limits and
convert to integers; export labels retain double coordinates.

These loops borrow arrays, pens and element records across label-command calls. The existing restriction on mutating
drawing state or entering nested event loops therefore applies to both screen and export callbacks. Retaining the label
result and preserving interpreter state do not protect borrowed drawing storage.

Line configuration and continuous-run smoothing
-----------------------------------------------

:c:func:`rbcGrLineConfiguration.ConfigureLine` orchestrates the smaller transactions described above. Tk has already
installed candidate option values; the element carries the changed-option mask and original argument order. The routine
stages resources selectively, validates data/parameter compatibility and error-band smoothing, and configures the
embedded pen before transferring prepared state. Successful completion marks mapping and symbol-scale baselines dirty
where needed. The enclosing common configuration routine owns saved Tk option restoration and final acceptance.

Polar reference impedance is validated before the embedded pen is reconfigured or any staged resources are
transferred. A combined update such as ``-smooth step -z0 0`` therefore fails without changing derived drawing state.
The enclosing caller restores the reported Tk options, while transaction cleanup releases candidate resources. Valid
updates commit the prepared state together.

Ordinary and parametric resampling
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

:c:func:`GenerateSpline <rbcGrLineConfiguration.GenerateSpline>` handles natural/quadratic curves expressed as functions
of mapped data X. It swaps coordinate roles for
inverted graphs and reflects decreasing abscissas into increasing order rather than reversing source arrays. Every knot
is retained; additional samples follow visible screen pixels along that axis. The inserted samples retain their
interval's starting source index.

:c:func:`GenerateParametricSpline <rbcGrLineConfiguration.GenerateParametricSpline>` uses
source-interval/local-parameter pairs for natural, quadratic or Catmull-Rom evaluation. It
retains all knots and samples roughly every two pixels over visible portions of the original straight segments. This is
a sampling heuristic, not adaptive subdivision of the final curve. It does not require monotonically increasing data X.

Both functions consume continuous runs. :c:func:`GenerateSplineRuns <rbcGrLineMapping.GenerateSplineRuns>` handles
discontinuous maps externally, preserving gap
boundaries. Replacement point/index arrays are committed only after successful evaluation. Allocation and evaluator
failures retain original geometry and set effective smoothing to linear; the requested option is unchanged.

Some ordinary-spline early exits differ: too few points, equal/nonmonotonic abscissas, invisibility or an empty screen
span return without clearing the effective mode. :c:func:`GenerateSplineRuns <rbcGrLineMapping.GenerateSplineRuns>`
observes that mode to track successful runs, so the
mode is not proof that coordinates were actually resampled. Code that needs such proof must not infer it solely from the
smoothing flag.

Scalar adapters and ownership
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The pen-color parser :c:func:`rbcGrLineConfiguration.GetLinePenColorFromObj` uses
:c:func:`rbcGrLineConfiguration.IsLinePenPrefix` to accept nonempty prefixes of defcolor as an inherited-color
sentinel. Empty values produce NULL only when permitted; actual Tk colors carry an acquired
reference. :c:func:`rbcGrLineData.FreeLinePenColor` releases actual Tk colors while ignoring NULL and the
sentinel. Callers still clear their own fields after release. The local :c:func:`rbcGrLineConfiguration.Round` helper
requires bounded finite input and does not provide checked conversion.

:c:func:`rbcGrLineConfiguration.PixelAbscissa` and :c:func:`PixelOrdinate <rbcGrLineConfiguration.PixelOrdinate>` select
physical coordinate roles according to inversion; neither removes axis descending direction.
:c:func:`rbcGrLineConfiguration.MapLineDataAbscissa` invokes the appropriate axis mapper.
:c:func:`rbcGrLineConfiguration.ClearLineVectorChanges` consumes recorded X/Y change ranges after cache handling,
without delivering or canceling vector notifications.

:c:func:`rbcGrLineConfiguration.ComplexDivide` uses a denominator-component ratio and rejects nonfinite results, but
does not guarantee overflow-free arithmetic for all finite extreme inputs. Its failure outputs must not be used. The
normalized Smith conversion helpers :c:func:`rbcGrLineConfiguration.GammaToNormalizedImpedance` and
:c:func:`rbcGrLineConfiguration.GammaToNormalizedAdmittance` special-case exact open and short points as successful
positive-infinity results; callers must not equate a successful conversion with finite output.

Pen, palette and error-geometry boundaries
------------------------------------------

The pen and palette transactions complement the common data transaction. :c:func:`Rbc_PrepareElemPenTransaction` and
:c:func:`Rbc_PrepareElemStylesTransaction` validate explicit values in caller order without replacing live
resources. Their output storage must not already own candidates. Failure releases candidates and clears the transaction.
After all validation, :c:func:`Rbc_CommitElemPenTransaction` and :c:func:`Rbc_CommitElemStylesTransaction` transfer
ownership and release the old resources. :c:func:`Rbc_FreeElemPenTransaction` and
:c:func:`Rbc_FreeElemStylesTransaction` release anything left uncommitted. These operations do not supply rollback
after commit.

:c:func:`Rbc_ExpandErrorBarExtents` and :c:func:`Rbc_MapErrorBars` share the same source-point limits and
symmetric-versus-explicit error precedence. :c:func:`Rbc_ExpandErrorBarExtentsOffset` and
:c:func:`Rbc_MapErrorBarsOffset` apply a uniform data-space X shift. Their per-point counterparts,
:c:func:`Rbc_ExpandErrorBarExtentsTranslated` and :c:func:`Rbc_MapErrorBarsTranslated`, use an optional full
source-indexed array of X/Y translations instead. A non-finite translation omits that point. Neither path modifies the
source vectors.

Before mapping, the caller releases old error caches and selects the owning track viewport. Styles supply cap
half-widths in pixels; translations remain in data units. :c:func:`AllocateErrorBarArrays` reserves up to three segments
and indices per point. Overflow is checked, but the void mapping API cannot report that failure and may already have
mapped the other direction.
