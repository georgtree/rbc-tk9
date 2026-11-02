Element data and dispatch
=========================

``rbcGrElem.c`` owns shared element commands, common data transactions and
iteration over the graph's element display list. Concrete element modules
supply configuration, extents, mapping, drawing, export and closest-point
operations through ``ElementProcs``. The private records and declarations live
in ``rbcGrElem.h``; see :doc:`element-reference` for selected contracts.

Data slots
----------

``ElemVector`` represents one common data input: X, Y, weights or an error-bar
input. Its ownership depends on how it was populated:

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

``Rbc_ParseElemVectorObj`` first tries named-vector lookup. Otherwise it parses
the option as a list of numeric expressions. This shared parser accepts real
vectors only; polar complex input has a separate implementation and must not
be inferred from this parser's contract. Parsing initializes the candidate,
so passing an already populated candidate would lose its owned resources.

``Rbc_FreeElemVector`` detaches a client ID or frees a literal array, then zeros
the slot. It never destroys the named source vector. Borrowed vector data must
not be freed or treated as stable across source updates. See :doc:`vectors`
for vector storage and observer lifetime.

Staging and commit
------------------

The live element must not receive partially validated inputs.
``Rbc_PrepareElemDataTransaction`` collects common data into temporary slots.
On initial configuration it handles relevant defaults; explicit arguments are
processed in their original order. Looking only at the final retained Tcl
option objects would hide an invalid earlier occurrence of a repeated option.

The transaction also preserves the ordering relationship between paired
``-data`` input and separately supplied X/Y inputs. A replacement candidate is
parsed before discarding an earlier candidate for the same destination.
Failure frees staged resources and leaves the live slots unchanged.

Concrete configuration procedures prepare other resource transactions as
needed, including axes, pens, styles, tags and state. All fallible preparation
must finish before committing these resources. This complements Tk option
rollback; it is not replaced by it. See :doc:`configuration`.

``Rbc_CommitElemDataTransaction`` releases replaced live slots and transfers
the staged records into them. Only then does the private ``CommitElemVector``
install the named-vector callback: its client data must point to the stable
member of the element, never a temporary stack candidate. Commit clears the
staged mask and moved candidates. Freeing the transaction afterward is safe.
An abandoned successful transaction must also be freed.

``Rbc_ElemDataTransactionPointCount`` returns the smaller effective X/Y length
without committing. It selects staged or retained slots and queries current
named-vector lengths without consuming pending notifications. Literal inputs use
their stored counts. It is an ordinary paired-data helper, not the polymorphic
point-count service used for polar elements.

Updates and invalidation
------------------------

``VectorChangedProc`` receives the stable data slot as client data. On update
it reads the changed interval while that notification is active, reacquires
the vector record through its ID and refreshes the borrowed array, length,
capacity and extrema. On destruction it clears the client ID, array and
length, leaving no usable data in that slot.

Pending intervals are accumulated until consumed by element mapping. Once an
unknown or full change has been seen, later precise intervals cannot make the
combined update precise again. This information allows concrete mapping code
to choose an appropriate cache update; it is not a promise that every element
performs partial remapping.

The callback marks the graph's axes and the element's mapping dirty. It requests
backing-store redraw when the element is neither hidden nor plot-hidden.
It does not draw synchronously. Cached numeric pointers must be refreshed
through this notification path, rather than retained independently by a new
component implementation.

Type dispatch and display order
-------------------------------

``ElementProcs`` separates shared command behavior from element-specific work.
Extents describe data bounds for axis scaling; mapping prepares display
geometry; normal and active drawing consume it. Export and legend-symbol
operations have separate callbacks. Closest-point callbacks report through
``ClosestSearch``, including data coordinates and an original data index.

``Rbc_MapElements`` walks the display list, skipping hidden and plot-hidden
elements. It remaps when the graph requests all items or the element has
``MAP_ITEM`` set. Each mapping call runs within a paired track viewport scope,
and the item flag is cleared afterward. Native remapping also records a graph
change for interaction updates; export mapping suppresses that screen event.

Normal drawing follows display-list order with the same visibility checks.
Track drawing enters the owning viewport and skips collapsed visible regions.
Scrolled tracks and track bar elements additionally use a renderer clip around
the complete drawing call, including raised bar borders. A viewport scope alone
does not install that renderer clip. See :doc:`mapping` and :doc:`rendering`.

A concrete implementation must keep drawing reductions separate from source
identity. Decimation or clipping can change the mapped arrays without changing
which original sample a closest-point result identifies. Legend symbols also
have their own geometry and must not be mistaken for plot pixels by image tests.

Destruction
-----------

``ReleaseElementResources`` clears borrowed configuration arguments and invokes
the concrete destructor before freeing Tk-managed option resources. Derived
GCs may still refer to colors, fonts or bitmaps owned by the option table.
The concrete destructor is called even after failed option initialization:
constructors can already have allocated arrays, chains or embedded pens.

Release flags prevent repeating resource cleanup. The graph's element-resource
release pass runs while the Tk window context is available; element record and
registry cleanup follow their owning lifecycle. New element types must release
vector clients, owned arrays and derived resources without destroying external
source vectors or double-freeing Tk-owned values.

See :doc:`decimation` for dense line mapping, cache maintenance and source picking.

See :doc:`bars` for bar grouping, placement, error translations and mapped ownership.

See :doc:`fills` for area closure, uncertainty-band runs and fill resource ownership.

Synchronous layout before idle notifications
--------------------------------------------

Vector updates normally notify clients at idle. A command can replace vector
storage and immediately request export or axis limits before that notification
runs. ``Rbc_SyncElementVectors`` refreshes pending coordinate, weight and error
slots before graph layout or axis reset uses their borrowed arrays. It marks
mapping and ranges dirty without delivering client callbacks or processing Tcl
events. A full change is recorded because exact ranged metadata is only
available inside the notification callback; the queued notification remains
scheduled normally.

Transaction records and failure rules
-------------------------------------

The shared configuration helpers operate on fresh temporary records. Prepare
functions zero their output before use: they are not replacement operations
for a transaction that still owns candidates. Release or commit that earlier
transaction first. The concrete configurator prepares all required families
before making live-field commits; a successful prepare in one family does not
permit committing it while another family has failed.

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

Tag and axis preparation clean their staged resources before returning an
error. State preparation has nothing to release and may leave an earlier
valid scalar marked staged when a later occurrence fails. Therefore checking
``staged`` alone is insufficient: the caller must first check the preparation
return code. State commit does not independently schedule redraw or repick
bindings. Those remain responsibilities of the enclosing configuration path.

Axis staging requires an existing axis and does not accept an empty mapping.
Lookup checks the orientation of an in-use axis; an unused or untyped axis can
acquire the requested class at lookup. Candidate cleanup releases the reference,
but does not restore that class field. The guarantee here is unchanged live
element mappings, not an unchanged axis registry. Staging acquires a successor before releasing the
previous candidate, even if both resolve to the same record. At commit, each
staged reference is moved to the live field before the old reference is
released. A final release can destroy a delete-pending axis. Empty pen selection
has different semantics; see :doc:`pens` rather than applying the axis rule to
all named resources.

Original arguments and repeated options
---------------------------------------

The identity helpers use the element's concrete option table through
Rbc_GetCanonicalOptionName after Tk validation. They recognize canonical
options, synonyms and accepted abbreviations. In particular, exact ``-x`` and
``-y`` retain priority over longer names such as ``-xerror``. These helpers do
not parse values or authorize a commit.

Preparation first identifies which options were supplied explicitly. On first
configuration, it processes retained defaults only for identities that were
not explicitly supplied, then walks the original explicit option/value sequence
in order. This serves two purposes: an explicit value can override an invalid
unused default, but a later valid repeated option must not hide an invalid
earlier explicit occurrence. Reading only the final Tk-managed object fields
would lose that distinction.

Data staging applies the same rule to aliases between destinations: ``-data``
replaces both X and Y candidates, whereas a later independent coordinate option
replaces only that coordinate. Both candidates from a paired parse must succeed
before either previous candidate is released. The live element remains separate
from all of this temporary storage.

Retained coordinate representations
-----------------------------------

Internal numeric arrays and the objects reported by configuration queries are
different representations with separate ownership. After a successful data
commit, Rbc_SyncElemDataOptionObjects replays initial defaults and explicit
arguments in the same order used for staging. It does not reevaluate numeric
expressions or reconstruct input strings from floating-point results.

For example, with expression objects preserved in the input:

.. code-block:: tcl

   .g element configure trace -data {1 {2+3} 2 {4+5}} -y {7 8}

The later Y value makes the retained paired ``-data`` representation invalid,
so it is cleared; X retains the values derived from the earlier pairs. Reversing
the order lets the final ``-data`` establish both coordinates and a coherent
paired representation again. The individual X/Y lists contain the original
coordinate objects, preserving expression spellings such as ``2+3``.

ReplaceElemOptionObject retains a new object before releasing the old one and
handles identical pointers as a no-op. During first configuration, synchronization
also pins initial option objects while replacing fields that may own their last
reference. SetElemDataPairOptionObjects assumes prior validation: a malformed or
odd-length list at this phase is an internal fatal error, not a second user-input
validation opportunity. Run synchronization while the first-configuration flag
still describes the configuration being committed.

Callback boundaries
-------------------

String-list parsing for tags and exact state parsing do not execute binding
scripts. Common numeric data parsing is different: list entries are expressions,
and their evaluation can invoke Tcl commands or variable traces. Candidate
staging protects the live value from a partial commit, but does not by itself
preserve the graph, element or borrowed arguments across such callbacks. Audit
those lifetimes at the caller and parser boundary separately; these contracts
do not establish general reentrancy for configuration.

CommitElemVector installs a vector-change callback only after moving the slot
to its stable destination. A callback must never retain a pointer to a temporary
transaction field. Moving the client ID transfers observation ownership, not
ownership of the named source vector or its storage.

Parsing, allocation and extrema
-------------------------------

GetElemArrayByteCount rejects negative counts, zero element size and products
that exceed SIZE_MAX, assigning its result only on success. Zero count is
valid. This check cannot repair overflow that occurred while a caller computed
the count before invoking it.

EvalExprListObj initializes its outputs to zero/NULL and uses Tcl_AttemptAlloc
for a nonempty result. Each list member is evaluated with Tcl_ExprDouble; an
error releases the temporary numeric array. The helper borrows the list's
object-pointer array across evaluation. It does not take an immutable snapshot
or add an owner-preservation boundary, so this local allocation cleanup must
not be confused with protection against callbacks that mutate owners or input
representations.

ParseElemVectorPairsObj evaluates the flat input before checking whether its
length is even. An odd-length failure can therefore follow expression side
effects; it is not a side-effect-free prevalidation pass. Successful parsing
uses a temporary flat array followed by separate X and Y arrays, computes
extrema, and releases the temporary array. This paired-input parser always
treats input as expressions, unlike Rbc_ParseElemVectorObj's initial named-vector
lookup.

The range helpers have distinct contracts:

.. list-table::
   :header-rows: 1
   :widths: 26 40 34

   * - Helper
     - Behavior
     - Empty/unusable data
   * - SyncElemVector
     - Borrows source storage and obtains source-maintained extrema
     - Requires a live source vector
   * - FindRange
     - Scans literal data, ignoring nonfinite samples
     - Empty/NULL leaves cached extrema untouched; all-nonfinite nonempty data gives DBL_MAX/-DBL_MAX
   * - Rbc_FindElemVectorMinimum
     - Returns the smallest finite value at or above minLimit
     - Returns minLimit if its accumulator remains DBL_MAX

Neither scanning helper computes and stores a separate positive minimum. The
bounded-minimum helper also treats a sole qualifying DBL_MAX value as its
unchanged sentinel and returns minLimit. That is current compatibility behavior,
not a promise that every finite sample has a distinct successful result.

Notification metadata and synchronous refresh
---------------------------------------------

RecordElemVectorChange merges inclusive source intervals by their outer bounds.
An unknown/full change dominates later precise ranges until the owning mapping
code consumes or resets that pending state. The helper itself neither validates
indices nor fetches data. Reading the vector's client-specific changed range
belongs inside VectorChangedProc, while that notification snapshot is valid.

The destruction notification clears clientId, valueArr and nValues and records
a full change. It does not zero vecPtr, capacity or extrema. Those remaining
fields must not be treated as valid source storage after destruction; the
cleared live-data fields govern subsequent use.

Rbc_SyncElementVectors visits the registry rather than only the visible display
list. Pending common X/Y, weight and error inputs refresh their borrowed arrays
and mark a full change before immediate layout or export. It neither delivers
nor cancels the queued notification and does not independently queue a redraw.
VectorChangedProc has a different scheduling policy: it always marks axes and
mapping dirty, but only schedules backing-store redraw for plotted elements.

Value-label formatting
----------------------

Rbc_ValidateValueFormat accepts literal-only strings, escaped percent signs and
at most one floating conversion. Formatting passes one double at a time; even
SHOW_BOTH formats X and Y separately with the same format and joins them with a
comma. Dynamic widths/precisions and conversions requiring another argument type
are rejected. Format length, width and precision are each bounded by the current
VALUE_FORMAT_LIMIT of 200. Validation does not guarantee that every formatted
number fits the destination buffer.

FormatValueLabel supplies terminated, bounded fallback text. It avoids appending
Y when X has already been truncated. Rbc_GetElementValueLabel creates and retains
that fallback before executing application code. A nonempty command prefix is
duplicated, then receives graph path, element name, original index, X and Y in
the global Tcl evaluation context. The returned label owns one Tcl reference
which the drawing caller must release. Successful callback text is not limited
to the fallback buffer size.

Command-prefix validation checks list syntax only; it does not check command
existence. Empty prefixes retain fallback formatting. Callback errors are
reported through the background exception mechanism and use fallback text, while
the enclosing interpreter state is restored. This result preservation is not
lifetime protection for drawing records: value-label callbacks must not mutate
drawing state or enter a nested event loop.

Offsets are a separate parser: Rbc_GetValueOffset requires two signed Tcl ints,
commits both together, and accepts no screen-unit suffixes. It stores the parsed
values in the double fields of Point2D without evaluating expressions.

Closest-result source values and traces
---------------------------------------

ClosestErrorValue rejects a pending named-vector update before touching its
borrowed array. It also rejects out-of-range indices, NULL arrays and nonfinite
samples. AppendClosestErrors reports the original source error values, including
explicit low/high bounds, rather than transformed or bar-translated positions.
A nonempty symmetric-error vector takes precedence; an unusable symmetric sample
does not fall back to explicit bounds. Explicit low and high must both be usable
before either is reported for that axis.

Closest results are collected into a private dictionary before any destination
array unset or write. The snapshot owns common fields, mapped bar rectangles,
source error values and concrete line/polar metadata. Concrete closest-info
callbacks append values to this dictionary without running Tcl scripts.

Publication clears the same optional error fields and writes snapshot entries in
collection order. Both unset and write traces may change vectors, delete the
selected element or destroy the graph: publication makes no further graph or
element reads. A failing write returns its diagnostic and leaves earlier writes
in place; this is a stable snapshot, not an atomic Tcl-array transaction. The
snapshot is released on success and failure. Existing unrelated/stale fields
other than the six optional error fields retain their previous behavior.

GetIndex receives a captured point count before evaluating non-``end`` input
as a Tcl expression. It converts the result to Tcl_Size and checks against those
captured bounds. ActivateOp supplies lifetime protection and checks all staged indices against
current lengths before commit. Exact ``end`` avoids expression
evaluation; empty elements are rejected before either path.

Registry and display-list ownership
-----------------------------------

The element registry owns records; the display chain contains borrowed pointers.
NameToElement searches the registry without filtering visibility. Creating an
element reserves its registry entry before initializing options, then configures
both defaults and explicit arguments. Failure destroys the partial record.
Success prepends the record to the display list and invalidates mapping and axes.
Trackgraphs accept line, strip and bar elements through this shared path.

RebuildDisplayList parses the complete Tcl list before replacing membership.
Malformed syntax leaves the old list intact. Successful parsing clears the chain
and marks every registered element hidden, then appends recognized names in input
order and unhides them. Unknown names are ignored; repeated names retain only
their first occurrence. The temporary hidden state doubles as the membership
check, so rebuilding needs no additional set or repeated chain scans. This is not
deletion: omitted elements remain addressable in the registry.

DestroyElement removes bindings, legend references, resources, all matching
chain links and the registry entry before freeing the name and record. Rebuilding
maintains unique membership; destruction also defensively removes every borrowed
reference, saving the next link before unlinking the current one. Redraw is queued
once when a non-hidden element was displayed.

Option initialization and resource lifetime
-------------------------------------------

InitElementOptions obtains the element component resource name and uses class
``Element``. It is a no-op after successful initialization. If Tk option setup
fails, it frees partially initialized option values and the option table; the
concrete constructor's own allocations still need destruction.

ConfigureElementOptions retains Tk saved options while validating track ownership
and running concrete configuration. Trackgraph Y axes must belong to the selected
track; a nonempty track is rejected on ordinary graphs. The original argument
vector and option mask are borrowed transaction context, cleared before restoring
options on failure or releasing saved options on success. Concrete configuration
must still stage derived resources: Tk rollback alone cannot undo arbitrary writes
to live C fields. The optional output mask is written only on success.

Resource teardown and record teardown are separate. Rbc_ReleaseElementTkResources
walks the registry while the Tk window remains usable, including elements omitted
from the display list. Repeated resource release is guarded. Rbc_DestroyElements
later destroys records without deleting hash entries during hash traversal, then
deletes both hash tables and the display chain. DestroyElement releases resources
and registry membership immediately, but uses Tcl_EventuallyFree for the record
itself so an activation callback can retain its identity until validation ends.
Preservation does not keep released resources usable. That final container destruction
is not an idempotent operation.

Normal, active and export passes
--------------------------------

The common dispatcher consumes geometry already mapped by concrete elements.
Normal and active passes share visibility and track-size checks; the active pass
additionally requires ``ELEM_ACTIVE``. The enclosing graph decides pass ordering.
All these traversals borrow records and are not snapshots that tolerate arbitrary
callback-driven deletion during drawing.

Screen track clipping intersects inclusive track bounds with graph dimensions
before opening the renderer clip. Export uses the track rectangle in export
geometry. Both normal and active export callbacks support the selected backend;
only the diagnostic element comments are PostScript-specific. Viewport entry and
renderer clipping are separate, balanced scopes. Scrolled tracks and track bar
elements use the full-callback clip; other paths retain their existing concrete
clipping behavior.

Rbc_GraphUpdateNeeded is deliberately narrower than a general redraw predicate.
It checks pending X/Y vector notifications only for visible display-list elements.
It neither processes notifications nor synchronizes arrays, and does not inspect
weights, error inputs or concrete polar data slots. Callers needing current data
must use the appropriate synchronization and layout path, not treat a false result
as proof that every input is current.

Command dispatch and query domains
-----------------------------------

Rbc_ElementOp validates the operation and argument count using the typed elemOps
table before calling a handler. The supplied class UID selects creation behavior;
most other handlers operate on the named record regardless of that UID. This
entry point does not add record lifetime protection around Tcl evaluation.

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

GetOp currently treats an unsupported selector as a successful no-result operation,
not an error. It does not perform picking. CgetOp delegates to Tk option-value
retrieval; configure queries use Tk option-information retrieval instead.

Binding tokens
--------------

Rbc_MakeElementTag interns a string in the graph's element tag table and returns
the table-owned key as the binding token. Rbc_AppendElementBindingTags appends
name, class and configured tags in that order. It neither clears the receiving
list nor removes repeated tags. These helpers assemble lookup keys; they do not
run binding scripts. Tokens remain valid until the tag table is destroyed.
BindOp may intern a new tag even when subsequent binding configuration fails.

Mutation boundaries
-------------------

ConfigureOp resolves the leading names before mutation, then configures them
sequentially. Each element has its own Tk/derived-resource transaction; a failure
on a later element does not roll back earlier ones. Queries with several names
return information for the first one. Successful mutations synchronize hidden
state with display membership and invalidate axes, mapping and layout. The final
redraw scheduling is reached only after the loop completes successfully.

DeleteOp and DeactivateOp instead resolve and act on each name in sequence.
A later missing name leaves earlier changes in place. Repeated deletion names
therefore produce an error after the first deletion. Deactivation clears the
active flag and owned index array; if a later lookup fails, its final redraw
request is not reached. These operations do not promise command-wide atomicity.

ActivateOp stages explicit indices before replacing the old selection. Allocation
or ordinary conversion failure frees the candidate without changing the old
selection. No explicit indices means whole-element activation, represented by a
count of -1. Explicit indices retain order and duplicates, and successful commit
sets both ELEM_ACTIVE and ACTIVE_PENDING.

Index expressions may invoke application code. ActivateOp preserves the element
record during evaluation, while the widget dispatcher preserves the graph.
After each successful evaluation it checks window existence and the element's
resource-release flag before accessing data again. Deletion, same-name recreation,
or widget destruction cancels the outer activation with error code
``RBC ELEMENT CALLBACK``. Preserving storage prevents allocator address reuse from
making a replacement look like the original element.

Before commit, every staged index is checked against the current point count,
including indices evaluated before a later expression changed the data. Real and
complex named-vector lengths are read directly, without waiting for idle
notifications or consuming them. Exact end is resolved using the current length
at its own evaluation; its resulting numeric index is revalidated at commit.
Pending growth is therefore visible to a subsequent index, and pending shrink
cannot commit an out-of-range selection.

Evaluation/conversion errors keep their original diagnostics. Candidate storage
and preservation scopes are released on all exits. The outer command leaves
activation unchanged on failure, apart from any deliberate callback side effects;
these are not rolled back. Successful nested activation is permitted, and a
successful outer command then replaces that selection with its own candidate.

Closest-search phases
---------------------

ClosestOp synchronizes common data inputs and resets axes when requested before
parsing coordinates and switches. It does not map dirty elements as part of the
search: records marked MAP_ITEM or with pending X/Y notifications are skipped.
Explicit hidden or unplotted names cause an error; an implicit search skips them
and visits the display chain in reverse. Track eligibility additionally requires
a noncollapsed viewport containing the query position.

ParseClosestOptions starts at argument six and accepts unambiguous switch
abbreviations. Direction values must be exactly x, y or both; halo values use
nonnegative screen-distance conversion; interpolation uses Tcl boolean parsing.
Parsing stops at the first non-option or after --. It updates a local search
record incrementally, so parse failure need not roll back already parsed fields.
The final hit comparison is strictly less than the halo, not less than or equal.

Search and metadata collection finish before PublishClosestInfo runs any array
traces. The caller retains the result dictionary while publication clears six
optional error fields and writes the snapshot. A miss writes an empty name;
other old metadata fields are not generally erased. A write error preserves
preceding writes and returns its diagnostic. Concrete closest-info collectors
must remain script-free for this separation to protect graph lifetime.

For the line-specific complex and parallel-parameter transactions, see
:doc:`polar` and the grouped contracts in :doc:`polar-reference`. Those paths
have different input restrictions and notification policies from common X/Y data.
