Vector ownership and notifications
==================================

Vectors are interpreter-owned numeric objects. A Tcl command, an optional
mapped array, graph elements and C consumers can access the same vector without
copying its values. This is sharing within one interpreter, not an interface
for sharing mutable storage across interpreters or threads.

Module boundaries
-----------------

.. list-table::
   :header-rows: 1
   :widths: 32 68

   * - Module
     - Responsibility
   * - ``rbcVector.c``
     - Interpreter registry, object creation/destruction, storage management,
       mapped variables, client notifications and C accessors.
   * - ``rbcVecCmd.c``
     - Vector instance operations and their Tcl argument handling.
   * - ``rbcVecMath.c``
     - Expression evaluation and mathematical functions.
   * - ``rbcVecFft.c``
     - FFT-related operations and their destination handling.
   * - ``rbcVector.h``
     - Opaque public types and client notification declarations.
   * - ``rbcVectorInt.h``
     - Private representation and internal mutation helpers.
   * - ``rbc.decls``
     - Authoritative public stub signatures and slot numbers.

Installed consumers include ``rbc.h`` and use the generated declarations through
RBC's stubs setup. Do not include ``rbcVectorInt.h`` or edit ``rbcDecls.h`` by
hand. A declaration being present in ``rbcVector.h`` does not imply that it has
a public stub slot: the observer-ID functions are not in ``rbc.decls``.
The :doc:`vector-reference` distinguishes these client declarations from private
update helpers. This installment does not document every numerical operation.

Records, names and storage
--------------------------

``VectorInterpData`` contains the vector registry and math/index-function tables
for an interpreter. ``VectorObject`` is the internal alias of the opaque
``Rbc_Vector`` record. It holds type, length, capacity, storage ownership,
command identity, optional variable mapping, observer chain and cached extrema.

A new internal record starts real and empty: its data pointer is NULL and its
length and capacity are zero. Growth allocates storage; there is no inline array.
Real storage contains doubles; complex storage contains ``Rbc_Complex`` records
with separate real and imaginary doubles. Length and capacity count values,
not bytes or individual complex components.

``Rbc_VectorData`` and ``Rbc_VectorComplexData`` return borrowed storage, or NULL
for the wrong type. NULL can also represent an empty vector, so query type and
length rather than using the pointer alone as a type test. Reacquire pointers
after resize/reset and before using data following callbacks or Tcl evaluation.
The vector record itself becomes invalid on destruction.

Registered names are borrowed hash-key strings. Rename retains vector identity
and observer connections, but invalidates a cached name pointer. Obtain the
current name again or keep an owned copy. The mapped array name is a separate
property. Real vectors created through ``Rbc_CreateVectorWithType`` receive the
traditional automatic array mapping unless their names contain ``(``; complex
vectors do not receive that automatic mapping.

Storage replacement versus in-place modification
------------------------------------------------

``Rbc_ResetVector`` and ``Rbc_ResetComplexVector`` replace the storage contract
without changing vector type. The replacement includes pointer, length,
capacity and release policy. The real reset rejects complex vectors and vice
versa. Validation includes writability, nonnegative sizes, length not exceeding
capacity, and byte-count overflow.

.. list-table:: Reset ownership policy
   :header-rows: 1
   :widths: 25 75

   * - Policy
     - Consequence on a successful reset
   * - ``TCL_STATIC``
     - Borrow the supplied buffer. Its owner keeps it alive while RBC uses it;
       RBC does not free it when replacing or destroying the vector.
   * - ``TCL_DYNAMIC``
     - RBC takes responsibility for releasing the buffer with Tcl's allocator.
   * - ``TCL_VOLATILE``
     - Copy the used values into RBC-owned storage, including when the supplied
       pointer equals the current pointer. The caller retains its source buffer.
   * - Custom ``Tcl_FreeProc``
     - RBC calls this procedure to release the adopted buffer.

A NULL pointer or zero capacity denotes empty storage after size validation.
Replacement frees the previous owned buffer only if its address differs from
the installed address. A same-address reset still updates the complete ownership
contract. Do not use reset merely to announce a value change: accidentally
changing its release policy can leak or incorrectly free storage.

For an in-place write, leave length and ownership unchanged, check writability
before writing, then announce the change. For example, inside a C command with
a live borrowed vector:

.. code-block:: c

   if (Rbc_VectorCheckWritable(interp, vecPtr) != TCL_OK) {
       return TCL_ERROR;
   }
   if (Rbc_VectorGetType(vecPtr) != RBC_VECTOR_REAL ||
       index < 0 || index >= Rbc_VectorLength(vecPtr)) {
       Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid real vector index", -1));
       return TCL_ERROR;
   }
   double *values = Rbc_VectorData(vecPtr);
   values[index] = value;
   Rbc_VectorChangedRange(vecPtr, index, index);
   /* Do not keep values across subsequent vector operations or callbacks. */

``Rbc_VectorChanged`` reports a full/unknown change. Its ranged counterpart uses
inclusive zero-based storage indices, independent of the Tcl vector index
offset. Invalid intervals conservatively become full updates. Both public
reporting functions flush the mapped-array cache when configured to do so.
The private ``Rbc_VectorUpdateClients*`` helpers do not perform that cache flush.
Neither reporting family checks writability: the caller must check before
performing the write, when rejecting it is still possible.

Read-only state
---------------

Read-only protects data mutations; it is not a lifetime lock or a security
boundary. ``Rbc_VectorSetReadOnly`` changes the flag without changing storage,
identity or observer connections and without sending a value-change notification.
Renaming and destruction remain possible. ``Rbc_VectorCheckWritable`` reports
``RBC VECTOR READONLY`` when protection is active.

A native pointer write cannot be intercepted. Producers must check writability
before changing borrowed storage, and validate all output vectors before any
writes when an operation has multiple destinations. Input-only consumers can
continue using protected vectors. Rechecking after writing cannot undo the
write, and sending a notification does not retroactively authorize it.

Observers and callback lifetime
-------------------------------

:c:func:`Rbc_AllocVectorId` allocates an observer attached to an existing vector.
It does not allocate a second numeric array or keep the vector alive. Register a
callback with :c:func:`Rbc_SetVectorChangedProc`; RBC borrows the callback data.
When detaching, release the ID with :c:func:`Rbc_FreeVectorId` before freeing its
callback data. This is supported inside callbacks too: detachment invalidates
the ID immediately, while active notification snapshots defer its reclamation.

Updates normally coalesce until Tcl idle processing. The notification policy
can instead request immediate callbacks or suppress updates. Do not assume that
a mutation API always returns before its notification callback executes.
:c:func:`Rbc_VectorNotifyPending` tests the idle-pending flag only; it is not a
general dirty test and its nonzero value is not necessarily 1.

Destruction cancels pending idle notification and sends DESTROY synchronously,
regardless of the ordinary update policy. The vector's length is already zero
at this point. A client discards its saved ID and borrowed data pointers, marks
its own view invalid, and schedules any later owner work. After callbacks return,
RBC frees the remaining IDs and vector storage. Do not query or free those IDs
later. An API's disconnected-token check does not make freed memory safe to use.

Notification delivery snapshots and preserves client IDs before calling them.
Removing another observer before its turn skips that observer. Added observers
join a subsequent delivery, not the already captured pass. Destroying the vector
during UPDATE delivers DESTROY synchronously and stops the remaining UPDATE
callbacks. Recursive destruction of the same vector is ignored.

The old registry entry is removed before destruction callbacks, allowing a new
vector with the same name to be created. The old vector keeps a temporary name
copy during delivery, and its cleanup does not remove the replacement.

Nested immediate updates save and restore each client's changed-range snapshot.
The graph/vector records and interpreter are retained where notification and
destruction need them. This does not preserve caller-owned callback data or
its owner: a callback that invokes nested code must account for that code
possibly destroying its own client state. Raw pointers and IDs still must not
be used after their documented lifetime, and no cross-thread access is added.

Changed intervals and extrema
-----------------------------

The vector accumulates a bounding interval across ranged updates. A full/unknown
update overrides that interval. Delivery snapshots the pending range, clears
server-side pending state, and copies the snapshot into each client's record
for its callback. :c:func:`Rbc_VectorGetChangedRange` reads that snapshot only
during delivery; the prior snapshot is restored afterward (normally no active interval). Coalesced intervals can include
unchanged values between separate writes.

Real extrema are cached along with their source indices. A ranged update can
scan just the changed interval when no cached extremum was overwritten. If an
old extremum was touched, or the cache was already invalid, a full scan is needed.
:c:func:`Rbc_VectorGetRange` refreshes the cache on demand; empty or entirely
non-finite real data has NaN extrema. Complex storage has no real min/max range
through this function.

Graph consumers
---------------

``rbcGrElem.c`` owns the graph element's observer callback. UPDATE records the
changed interval, borrows the vector again and synchronizes element data.
DESTROY clears the saved client ID and data pointer and sets the value count to
zero. Both invalidate mapping and axis limits; visible elements also invalidate
the plot backing store and request redraw.

The graph display callback defers drawing while vector notification is pending,
so borrowed source values and mapped geometry do not get presented out of sync.
See :doc:`graph-lifecycle`. A notification is an invalidation signal, not an
instruction to draw immediately from inside the callback.
