Vector C API and storage primitives
===================================

This chapter connects the installed vector API to its implementation in
``rbcVector.c``. The public contracts are in :doc:`vector-storage-reference`;
private allocation and lifecycle helpers are in
:doc:`vector-storage-private-reference`. See :doc:`vectors` for the ownership
policy table and observer protocol, and :doc:`vector-operations` for the newer
Tcl command commit paths.

Public versus private entry points
----------------------------------

``generic/rbc.decls`` defines the public stub slots. The creation, lookup,
resize/reset, data access, change-reporting and read-only APIs reviewed here
are public entries. ``Rbc_CreateVector2`` and names beginning with
``Rbc_VectorNew``, ``Rbc_VectorCreate``, ``Rbc_VectorChangeLength`` and
``Rbc_VectorDuplicate`` are private implementation helpers; similar spelling
does not make them interchangeable with installed APIs.

Include ``rbc.h`` and initialize the appropriate RBC stubs interface as described
in :doc:`initialization`; see :doc:`headers` for header boundaries. Do not copy private record layouts or add declarations for
private helpers to an external extension. The observer declarations discussed
in :doc:`vector-reference` remain a separate case: declarations in a header do
not by themselves establish public stub availability.

Registry and creation
---------------------

``Rbc_VectorGetInterpData`` lazily creates interpreter-associated registry,
math-function and special-index tables. It does not install Tcl commands. A
private ``Rbc_VectorNew`` allocates an unregistered empty record with no numeric
storage, idle notification policy, invalid extrema and an empty observer chain.
A caller using it as a temporary releases it with ``Rbc_VectorFree``.

``Rbc_VectorCreate`` adds or reuses the registered identity, configures its
instance command and optionally maps an array. It qualifies creation names in
the current namespace, requires existing vectors to be writable and type
compatible, and rejects commands belonging to another object. Its ``#auto``
path checks both registry and command collisions. Missing namespaces are errors;
this helper does not create them.

The public creation functions are **create-or-reuse**, despite old comments
claiming that an existing name was always an error. Their positive initial size
resizes the vector; zero does not clear an existing vector. New records start
empty. Added values are zero-filled, while the retained prefix survives resizing.
The optional output pointer is borrowed, not a new ownership reference.

``Rbc_CreateVector`` selects real type. ``Rbc_CreateVectorWithType`` accepts real
or complex type and normally requests a same-named Tcl command. Automatic array
mapping is requested for real names without ``(``; complex vectors and names
containing parentheses do not get that automatic mapping. Explicit command and
array names belong to the private ``Rbc_CreateVector2`` interface.

Creation is not a transaction. Mapping can run Tcl traces before the initial
positive-size resize, and a later allocation failure does not undo all earlier
registration or existing-object changes. The low-level creation path can also
delete/recreate an existing instance command. The revised command output helper
instead retains existing commands/mappings and maps new outputs after filling
them; that stronger ordering must not be inferred for public C creation.

Lookup and borrowed views
-------------------------

``Rbc_VectorExists2`` tests a literal registry name. It does not accept a client
ID or evaluate range syntax. ``Rbc_GetVector`` accepts a vector reference,
including a selected range, and refreshes invalid extrema before returning.
Selection changes the original record's shared ``first``/``last`` fields; the
returned pointer is not an independent slice and its public length remains the
full vector length. See :doc:`vector-access` for evaluating index expressions.

.. list-table:: Borrowed C access
   :header-rows: 1
   :widths: 30 70

   * - Accessor
     - Meaning and lifetime
   * - ``Rbc_NameOfVector``
     - Current registered name; invalidated by rename or destruction
   * - ``Rbc_VectorData``
     - Real storage, or NULL for a complex vector
   * - ``Rbc_VectorComplexData``
     - Complex storage, or NULL for a real vector
   * - ``Rbc_VectorLength`` / ``Rbc_VectorSize``
     - Used length / capacity, both in values rather than bytes
   * - ``Rbc_VectorGetType``
     - Immutable real/complex storage type
   * - ``Rbc_VectorDirty``
     - Modification counter, not a boolean or pending-idle flag

An empty vector can also have NULL storage. Query type and length separately.
Raw pointers are not retained across storage replacement, destruction or Tcl
callbacks. The dirty counter advances through internal change scheduling,
including when ordinary notifications are disabled; an unreported native write
does not advance it. It is not an object-lifetime token.

Resizing, resetting and copying
-------------------------------

``Rbc_VectorChangeLength`` selects capacity by doubling from ``DEF_ARRAY_SIZE``
until the requested length fits, with overflow checks. Equal capacity reuses the
allocation; otherwise it copies the retained prefix into new storage. It zeros
added values and sets the shared selection to ``0..length-1``. Shrinking may
release excess capacity, and zero length releases storage entirely.

That private primitive invalidates extrema but does not flush the mapped cache or
notify clients. The public ``Rbc_ResizeVector`` adds cache flushing and a full
update. Its failure path replaces the detailed resize message with a generic
message; it is not a name-based lookup API and requires a live vector pointer.

The two public reset functions share ``ResetVectorStorage`` after checking type.
Reset validates nonnegative length/capacity, ``length <= capacity`` and byte-count
overflow. NULL data or zero capacity produces empty static storage after
validation. ``TCL_VOLATILE`` allocates capacity-sized storage and copies only
used values; unused capacity is not initialized by this path. Other policies
borrow or adopt the supplied pointer. See :doc:`vectors` for release ownership.

A same-address reset still changes the ownership contract. Volatile input is
copied even when its address equals the old vector allocation; that old
allocation is then released under its previous policy. This does not extend the
lifetime of a borrowed pointer into the old allocation. Unlike resize, reset
does not reset the private shared selection. Successful reset publishes a full
change, but is not an appropriate substitute for announcing an in-place write.

``Rbc_VectorDuplicate`` is a private resize/copy primitive, not the revised Tcl
``dup`` handler. It copies selected values and offset into an existing same-type
destination without creating it or notifying clients. ``memmove`` handles
buffer overlap, including a same-object selected subrange. Copying finishes
before old storage is released or callbacks can invalidate the source.

Protection, publication and destruction
---------------------------------------

``Rbc_VectorCheckWritable`` checks a live record's protection flag and reports
``RBC VECTOR READONLY``. It does not validate arbitrary or destroyed pointers.
Check before a native write, and recheck after callbacks that might change
protection. ``Rbc_VectorSetReadOnly`` normalizes its argument to a boolean and
sends no value-change notification. Lifetime operations remain separate.

``Rbc_VectorChanged`` and ``Rbc_VectorChangedRange`` publish already-completed
in-place writes without changing length or ownership. The ranged form uses
inclusive storage indices independent of the external offset. Neither function
checks writability; notification cannot retroactively authorize a write.

``Rbc_FreeVector`` delegates to private destruction, which retains the record
and interpreter through callbacks and ignores recursive destruction. It deletes
the command and mapping, removes registration, cancels pending idle delivery,
and sends synchronous DESTROY with length zero. Registration has been removed
before observer DESTROY callbacks, allowing those observers to create a new
identity with the same name. Command deletion/unmapping happen earlier and have
their own callback boundaries. Remaining clients and owned numeric storage are
released before deferred record reclamation. Preservation of a record does not
keep its name, storage or logical identity usable after destruction.

Callback boundaries and ownership commits
-----------------------------------------

These changes preserve public C signatures, installed headers and stub slots.
Resize, reset, creation and publication retain the participating record and
interpreter across callbacks. Retention protects the record itself; destruction
still invalidates its logical identity and borrowed storage. A same-named
replacement is never substituted for that identity.

Storage replacement installs complete metadata before releasing old custom
storage. Custom release procedures can inspect the committed state. Private
resize and duplication recheck identity, protection, storage and selection after
release, returning an error if it is no longer safe for their callers to continue
writing. The committed change and callback effects are not rolled back.

Reset has a different ownership boundary: once it accepts the supplied storage,
it returns success even if a subsequent callback destroys the vector. An error
before that commit leaves ownership with the caller. Returning an error after
adopting a buffer could make a caller free that buffer twice. Success therefore
means that RBC accepted the ownership contract, not that the borrowed vector
pointer remains usable after callbacks.

Publication preserves interpreter result state across cache traces and observers.
It skips notification if flushing destroys the identity. Public resize may return
success after a publication callback destroys the vector; callers must track
lifetime separately before reusing borrowed pointers.

Creation writes its optional output pointer only on success and rejects
callbacks that destroy its identity. Initial-size failure removes a newly
created identity if it remains alive. Existing-object changes and arbitrary
callback effects are not rolled back. Command-deletion traces that install a
replacement command are rejected without overwriting that command.

These protections require a valid pointer at entry. They cannot validate a
pointer already freed by an earlier call, intercept native pointer writes, or
make cross-thread access safe.
