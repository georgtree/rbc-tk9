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
without committing. It is an ordinary paired-data helper, not the polymorphic
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
