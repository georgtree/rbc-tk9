Vector instance commands and binary input
=========================================

``rbcVecCmd.c`` dispatches instance operations and implements many of their
conversion and mutation paths. This chapter covers selected common paths, not
every instance operation. See :doc:`vector-command-reference` for their C
contracts, :doc:`vector-access` for evaluating conversions, and :doc:`vectors`
for storage ownership and client notifications.

Dispatch and mutation boundaries
--------------------------------

The instance dispatcher checks operation spelling and arity, rejects operations
unsupported for complex vectors, and checks read-only state for operations
that write their receiver. Output operations must check their own destinations.
It resets the receiver's selection to the full vector before calling a handler.
It preserves the receiver and interpreter during the handler. This retains the
record, not its destroyed storage: each handler still needs lifetime checks
after callback-capable work.

Mutation, array-cache flushing and client notification are separate steps.
A helper that changes storage does not necessarily perform all three. Immediate
notifications and Tcl variable traces are callback boundaries; numerical
expression conversion and reflected-channel operations can also execute Tcl.
Handler-specific protection must not be inferred from the common dispatcher.

Append and replacement
----------------------

``append`` tries each argument as a vector reference before treating it as a
list. Vector append requires matching types and permits self-append. List
conversion stages every value before changing destination storage. Failed
conversion or a read-only check cannot leave an appended prefix of that list.
Earlier successful arguments remain appended if a later argument fails, and
the handler still notifies clients about those earlier changes.

``set`` also prefers vector references. It stages vector ranges, including
self-copy, and transfers completed list storage into the destination. Failed
list conversion does not resize or overwrite destination values. Callback
side effects are not rolled back: a callback may edit the vector, and append
uses its current length after conversion. Destruction or locking prevents the
subsequent commit. Renaming does not change the retained destination identity.

List staging retains a private Tcl list representation, so conversion callbacks
can shimmer the original argument without invalidating the remaining values.
Storage installation precedes invocation of any old custom free procedure.
Cache flushing and notifications check that the object and interpreter remain
live and preserve command results/errors across callbacks.

These protections cover the reviewed set/append/binread/sort paths, not every
legacy mutation handler or the mapped-array write trace.

Sorting
-------

``Rbc_VectorSortIndex`` builds an owned array of indices without moving values.
The first vector's selected interval determines the indices; callers must
provide valid corresponding storage in every key vector. Keys are compared
lexicographically, and a heap sort constructs the permutation. Sorting is not
stable and does not define a total ordering for NaNs. Complex keys are rejected.
An empty successful sort still returns an allocation so NULL denotes failure.

The sort command resolves and retains all destination objects, validates them,
and allocates the permutation and one reusable scratch array. It applies every
permutation before invoking any variable trace or observer. This avoids a full
output allocation per vector. Callbacks see all sorted data; later callbacks
may legitimately edit or delete it. Renames retain identity, and deleted objects
are skipped rather than resolving a replacement with the same name.

Binary channel input
---------------------

``binread`` retains a readable Tcl channel for the duration of the command. Formats select supported native C
scalar types by family and byte width; bytes are converted to doubles. This is
not a platform-independent file format, and large integers may lose precision.
Real vectors consume one scalar per value; complex vectors consume interleaved
real/imaginary pairs. Byte swapping reverses each scalar independently.

The starting position defaults to the current length. A positive requested
count performs one read, which may be short. A zero count reads successive
chunks until EOF or a zero-byte read. Every returned chunk must contain complete
logical values; partial scalars or complex pairs are rejected rather than
carried into the next read. Nonblocking zero-byte reads stop the operation.

The handler saves and restores the channel's translation option and temporarily
uses binary translation. It does not snapshot all channel settings. A temporary
channel registration prevents reflected callbacks from freeing the channel
under the read. The command checks whether the caller closed it and stops rather
than using an unregistered handle; it does not reopen or restore a closed channel.
The channel remains caller-owned. Destination lifetime/read-only checks follow
reflected reads, and the complete original error state survives cleanup callbacks.

Each successful chunk is copied directly into storage, reusing capacity when
possible. Completed chunks remain if a later read or translation restoration
fails. The error path flushes and notifies any surviving destination when data
was committed, preserving the original error and its options. The incomplete
final chunk is not committed. Successful completion returns the logical value
count. The command does not stage the entire stream or retry a short positive-
count read to satisfy the requested count.
