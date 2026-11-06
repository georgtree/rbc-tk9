Vector indexed edits and rearrangement
======================================

This chapter covers the remaining legacy instance-command mutation paths in ``rbcVecCmd.c``. It complements
:doc:`vector-commands`, which describes the staged set/append paths, binary input and coordinated sorting. Contracts for
the handlers below are collected in :doc:`vector-edit-reference`. Numerical queries and generators are covered in
:doc:`vector-numerics`.

Mutation boundaries
-------------------

The instance dispatcher retains the receiver record and interpreter while a handler runs. Output handlers additionally
retain destination identities, and ``merge`` retains each resolved source through subsequent range evaluation.
Retaining a record does not retain its numeric storage after destruction. Handlers therefore check lifetime,
writability and relevant bounds after expressions that can execute Tcl.

Prepared data is filled before :c:func:`FinishCommandWrite <rbcVectorCommands.FinishCommandWrite>` installs it and
releases old storage. A custom free procedure therefore sees complete data. Ordinary indexed writes keep their existing
fast path; resizing generators and append paths reuse capacity when possible. Population and normalization stage a full
result; splitting snapshots its shared source once. Duplication copies directly into prepared output storage without
callbacks before the copy completes.

New real outputs receive their automatic array mapping only after data is complete. Existing mappings and command
identities are retained. Cache flushing precedes notification, and both stages skip destroyed identities and preserve
the command result. A callback that destroys and recreates a name cannot redirect an in-progress write into the
replacement.

Callback side effects are not rolled back. Multi-output splitting prechecks existing destinations before creating
missing names, but is not a transaction: a custom release callback can prevent a later commit. Earlier committed outputs
are still published. Notifications and mapping traces run after the write loop, so they cannot invalidate a destination
still awaiting a write.

Indexed access and deletion
---------------------------

:c:func:`IndexOp` uses the shared inclusive selection parser. Reading returns a Tcl list, even for one selected
value. Writing converts one real or complex value and replicates it through the selection, returning the original Tcl
input object. The append index may extend storage by one element but cannot be read. The handler checks lifetime,
writable state and saved indices after value conversion. An append follows the current end if a callback changed length;
an ordinary index that is no longer valid produces an error.

For an existing real-value write, the handler reports the exact changed range; recognized appends also report a
range. Other writes report a full update. This distinction lets graph clients maintain extrema and decimation caches
without treating every single-value update as a complete replacement.

:c:func:`DeleteOp` with explicit indices allocates one bitmap bit per original value, marks all selected ranges, then
compacts surviving entries in place. Overlapping ranges therefore remove a value once. Capacity is retained, length is
reduced, and clients receive a full update because later indices have shifted. Argument errors before compaction leave
values untouched in ordinary execution. If an index-expression callback changes length, the operation rejects the stale
bitmap before marking or compacting values. Destroyed/read-only receivers and out-of-range selections are also rejected.

With no indices, ``delete`` destroys the vector. The dispatcher still applies its receiver-writable check to this legacy
command. This differs from destroying a vector through the top-level ownership command and should not be mistaken for a
new lifecycle-locking policy.

Length and offset
-----------------

:c:func:`LengthOp` parses a nonnegative Tcl-sized integer, prepares storage while retaining the existing allocation when
its capacity matches the requested size class, and zero-fills new entries before commit. Substantial shrinking releases
excess capacity, and length zero releases storage. It returns the requested length, including when a post-commit
callback destroys the vector. Notification does not overwrite that result.

:c:func:`OffsetOp` changes external numeric indexing without moving values. Both query and setter return the current
offset. The setter does not flush mapped array entries or notify clients, although it is classified as a receiver
mutation for the read-only check. Offsets are metadata, not an address displacement of the stored array.

Copying, interleaving and splitting
-----------------------------------

.. list-table:: Destination behavior
   :header-rows: 1
   :widths: 18 38 44

   * - Operation
     - Data relationship
     - Commit behavior
   * - ``dup``
     - Copy source selection and offset
     - Prepare and copy one destination before release or publication
   * - ``merge``
     - Interleave equal-length source selections
     - Validate retained sources, build one full result, then transfer storage
   * - ``split``
     - Distribute source values round-robin
     - Append to each destination sequentially
   * - ``populate``
     - Insert linear intermediate values
     - Build the complete output before resolving its destination

:c:func:`DupOp` skips a destination that is the receiver itself. Other destinations inherit the source numeric type when
created, and existing types must be compatible. The command table permits one destination. No callback occurs between
output resolution and completing the copy; the source therefore needs no extra snapshot. Outputs use the current
namespace, matching creation semantics rather than falling back to a same-named global vector.

:c:func:`MergeOp` saves the selected first index of each source, requires matching types and selected lengths, and
stages values in source-argument order. For sources ``{a0 a1}`` and ``{b0 b1}``, the result is ``{a0 b0 a1
b1}``. Ordinary self-merge works because staging precedes destination replacement. Each resolved source record is
retained, and all saved ranges are validated again after the last source lookup. A callback destroying or shrinking an
earlier source causes an error before destination data is modified.

:c:func:`SplitOp` requires divisibility of source length by the number of destinations. Destination zero receives
source positions 0, K, 2K, and so on for K destinations; the next receives 1, K+1, 2K+1. Existing destination values are
retained and the subsequence is appended. The full source is snapshotted before destination resolution, including when
the source is itself an output. Duplicate destination names append their subsequences in argument order. Each
destination is checked again immediately before writing; cache flushing and notification follow the write loop.

For real destinations, these creation paths normally request a same-named array mapping unless the name contains an
opening parenthesis. Mapping is deferred until the new output is complete. Existing destinations retain their mappings.
Complex destinations are created without that automatic array mapping. These rules are separate from the registry's
support for literal vector names containing parentheses.

Population and source aliasing
------------------------------

:c:func:`PopulateOp` takes an integer density D of at least one, meaning D inserted values between neighboring
samples. For N greater than zero, the output has ``(N-1)*(D+1)+1`` values. Empty input gives empty output; one input
value stays one value. Count arithmetic is checked before allocation.

Each interval contributes its first endpoint and D intermediate values; the last source endpoint is written once at the
end. Complex values interpolate real and imaginary components separately. The result is computed into owned storage
before resolving its output. This handles source/output aliasing and ensures that storage-release and array-mapping
callbacks see a complete result.

Paired-vector simplification
----------------------------

The standalone ``rbc::vector::simplify`` command and the vector ensemble ``simplify`` operation share
:c:func:`VectorSimplifyObjCmd`. The ensemble adapter only shifts arguments. This algorithm operates on whole existing
real vectors in data coordinates; it is separate from screen-space line decimation and the mapped-polyline helper
described in :doc:`geometry`.

Both inputs must have equal lengths and finite coordinates. Tolerance must be finite and nonnegative. Both destinations
must exist, be real and writable, and be distinct from each other. Either destination may also be either input because
all computation completes before destination storage changes. No command creates output vectors, interprets range
suffixes, or changes vector type here.

Distance and subdivision
~~~~~~~~~~~~~~~~~~~~~~~~

The algorithm measures distance to the closed segment between the current endpoints, not the infinite supporting
line. This retains backtracking features that a supporting-line test could discard. Coincident endpoints reduce to point
distance. Differences and projection are scaled before arithmetic; a half-coordinate fallback avoids overflowing
subtraction on platforms where long double has the same range as double. A true distance beyond the available range can
still be infinite, which exceeds any accepted finite tolerance.

Subdivision uses an explicit stack of high indices. The first point is retained; for each interval, the earliest point
whose distance is the greatest value strictly above tolerance becomes the next split. Otherwise the high endpoint is
emitted, and traversal continues with the pending interval. Equality with the tolerance does not force retention. This
preserves input order and endpoints without recursion. Empty input returns zero points, and a singleton returns one.

The two output arrays and the index stack reserve space proportional to input length, with at least one allocation slot
for empty input. Allocation arithmetic is checked before allocation. Worst-case repeated interval scans remain
quadratic; the explicit stack avoids recursive call-depth growth but does not change that complexity. The tolerance is
geometric, not an amplitude or frequency error bound.

Commit and observer lifetime
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

All lookup, coordinate validation, computation and temporary observer-ID allocation precede mutation. Failures in those
steps free temporary buffers and IDs, leaving both destinations unchanged. Commit installs both numeric arrays, lengths,
capacities and full-vector selections, and invalidates extrema before calling any old custom storage releaser or
mapped-array trace. Existing vector identity, external index offset and bindings remain intact.

Old storage is released according to each destination's prior ownership policy. This happens after both outputs are
visible, so a release callback cannot observe only half of the initial commit. Subsequent callback mutations are
permitted and are not undone. The returned count describes the computed simplification, not a promise that callbacks
left both vectors at that length.

Temporary observer IDs use :c:func:`SimplifyDeleted` to clear their local slots when a destination is
destroyed. :c:func:`SimplifyFlush` snapshots the mapped-array name and re-fetches the server through the surviving ID
after trace-producing operations. It does not find a replacement vector by name. Surviving destinations then receive
full update notifications, and surviving temporary IDs are released. This protocol concerns destination identity; it is
not a general transaction covering all user trace or custom-release side effects.

Regression coverage should distinguish zero tolerance, coincident endpoints, backtracking, very large finite
coordinates, empty/singleton inputs, aliases between inputs and outputs, protected destinations, and deletion or
mutation of either destination during post-commit callbacks. Internal contracts are included in
:doc:`vector-storage-private-reference`.
