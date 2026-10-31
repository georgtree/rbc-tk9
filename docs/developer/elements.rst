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
