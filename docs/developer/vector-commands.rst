Vector instance commands and binary input
=========================================

``rbcVecCmd.c`` dispatches instance operations and implements many of their
conversion and mutation paths. This chapter covers selected common paths, not
every instance operation. See :doc:`vector-command-reference` for their C
contracts, :doc:`vector-access` for evaluating conversions, and :doc:`vectors`
for storage ownership and client notifications.

Dispatch and mutation boundaries
--------------------------------

:c:func:`Rbc_VectorInstanceObjCmd <rbcVectorCommands.Rbc_VectorInstanceObjCmd>`
checks operation spelling and arity, rejects operations
unsupported for complex vectors, and checks read-only state for operations
that write their receiver. Output operations must check their own destinations.
It resets the receiver's selection to the full vector before calling a handler.
It preserves the receiver and interpreter during the handler. This retains the
record, not its destroyed storage: each handler still needs lifetime checks
after callback-capable work.

The handler receives the **full** argument array: ``objv[0]`` is the vector
command, ``objv[1]`` the operation, and operation-specific input starts at
``objv[2]``. The dispatcher does not shift this array. This differs from
:c:func:`ConvertCommandList <rbcVectorCommands.ConvertCommandList>`, whose
arguments contain only values. See :doc:`calling-conventions` for borrowed
Tcl argument lifetimes. Each handler reference specifies its accepted count
and the meaning of the remaining positions.

Handler return status and interpreter result are separate contracts. Query
handlers explicitly set their results; successful mutation handlers do not
uniformly return a name, length or value list. A successful status also does
not promise that callbacks left the committed data unchanged.

Mutation, array-cache flushing and client notification are separate steps.
A helper that changes storage does not necessarily perform all three. Immediate
notifications and Tcl variable traces are callback boundaries; numerical
expression conversion and reflected-channel operations can also execute Tcl.
Handler-specific protection must not be inferred from the common dispatcher.

Control operations and array access
-----------------------------------

Complex support is selected by handler identity, separately from the operation
table. Aliases sharing a handler share its eligibility. The read-only gate is
also explicit: it checks receiver writes, including argument-dependent setters,
while operations writing other vectors must validate those destinations.
Unsupported complex operations are rejected before the read-only check.
Dispatch errors leave the previous selection unchanged; accepted operations
start with the complete receiver selected.

:c:func:`TypeOp <rbcVectorCommands.TypeOp>` reports ``real`` or ``complex`` and never changes storage type.
:c:func:`ReadOnlyOp <rbcVectorCommands.ReadOnlyOp>` returns the current boolean, optionally changing it after boolean
conversion succeeds. It is reversible and emits no value-change notification.
The flag protects data writes, not command renaming, mapping or lifecycle.

:c:func:`ClearOp <rbcVectorCommands.ClearOp>` implements ``clear``,
meaning **clear the mapped-array cache**, not empty the vector. It is
allowed for protected vectors and explicitly flushes even when automatic
flushing is disabled. The cache normally retains only its ``end`` entry after
recreation; subsequent reads materialize values again. User unset/write traces
may run during flushing, and their side effects can include data changes,
remapping or destruction. The handler returns TCL_OK after the void helper and
does not report lower-level cache recreation status.

:c:func:`VariableOp <rbcVectorCommands.VariableOp>` queries the stored mapping name, or delegates replacement/removal
to :c:func:`Rbc_VectorMapVariable`. An empty argument removes the association; it does not
rename the vector command or discard numerical storage. The returned mapping
spelling need not be a fully qualified command name. Array names reject
parentheses before removing an old mapping, although literal vector command
names may contain them. For later mapping failures, the old array is not
restored. See :doc:`vector-access` for trace ownership and lifetime boundaries.

:c:func:`ExprOp <rbcVectorCommands.ExprOp>` assigns to the existing receiver. Evaluation creates a
temporary result and checks that the original destination survives and remains
writable before copying compatible data. A real result can populate a complex
destination with zero imaginary parts; a complex result cannot change a real
destination's type. On success the handler requests a full notification through
:c:func:`NotifyCommandWrite <rbcVectorCommands.NotifyCommandWrite>`. On evaluation/assignment failure it returns the error
without that publication step. Neither case rolls back Tcl callback effects.
This differs from ensemble expression forms that return a list or create a
named vector; see :doc:`expression-engine` for the evaluator contracts.

Append and replacement
----------------------

:c:func:`AppendOp <rbcVectorCommands.AppendOp>` tries each argument as a vector reference before treating it as a
list. Vector append requires matching types and permits self-append. List
conversion stages every value before changing destination storage. Failed
conversion or a read-only check cannot leave an appended prefix of that list.
Earlier successful arguments remain appended if a later argument fails, and
the handler still notifies clients about those earlier changes.

:c:func:`SetOp <rbcVectorCommands.SetOp>` also prefers vector references. It stages vector ranges, including
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
legacy mutation handler. Mapped-array trace protection and its layout checks
are described separately in :doc:`vector-access`.

Sorting
-------

:c:func:`Rbc_VectorSortIndex` builds an owned array of indices without moving values.
The first vector's selected interval determines the indices; callers must
provide valid corresponding storage in every key vector. Keys are compared
lexicographically, and a heap sort constructs the permutation. Sorting is not
stable and does not define a total ordering for NaNs. Complex keys are rejected.
An empty successful sort still returns an allocation so NULL denotes failure.

:c:func:`SortOp <rbcVectorCommands.SortOp>` resolves and retains all destination objects, validates them,
and allocates the permutation and one reusable scratch array. It applies every
permutation before invoking any variable trace or observer. This avoids a full
output allocation per vector. Callbacks see all sorted data; later callbacks
may legitimately edit or delete it. Renames retain identity, and deleted objects
are skipped rather than resolving a replacement with the same name.

Binary channel input
--------------------

:c:func:`BinreadOp <rbcVectorCommands.BinreadOp>` retains a readable Tcl channel
for the duration of the command. Formats select supported native C
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

Vector ensemble and registry operations
---------------------------------------

The ensemble in ``rbcVector.c`` is separate from the instance dispatcher in
``rbcVecCmd.c``. Its typed table validates operation names and argument counts;
each handler owns its own mutation and callback policy. See
:doc:`vector-storage-private-reference` for the handler contracts and
:doc:`vector-storage` for registry ownership.

Creation and reuse
~~~~~~~~~~~~~~~~~~

:c:func:`VectorCreateObjCmd` collects names and options, then prechecks existing
candidate vectors for read-only state. This is not a transaction across the
whole argument list: subsequent names, size specifications, command collisions
or mapping failures can fail after earlier vectors were successfully processed.
The result on success is the last vector's qualified name, not a list of names.

An exact registered name wins over the legacy numeric ``name(size)`` or
``name(first:last)`` syntax. Otherwise a numeric-looking final balanced group
is interpreted as a size/range; ``-literal`` disables that interpretation.
The first index becomes the vector offset. An explicitly supplied type must
match an existing vector; omission preserves its type. Positive sizes request a
length change, while zero does not truncate existing storage. New storage starts
empty unless a positive size is requested.

Default array mapping is requested only for real vectors whose names contain
no opening parenthesis. Complex vectors and parenthesized names require an
explicit separate array name to request mapping. Absence of a mapping request
must not be confused with unmapping an existing array. Command and mapping
setup are delegated to :c:func:`Rbc_VectorCreate`; reusing a vector can execute
command-deletion and variable traces. The handler retains the returned object
across resizing and publication, then checks that it still exists before
reading its name. Existing-vector changes publish a full update.

Named expression results
~~~~~~~~~~~~~~~~~~~~~~~~

Ordinary ``expr`` returns Tcl values through :c:func:`Rbc_ExprVector`. With ``-name``,
the handler resolves the destination namespace, checks for collisions and
evaluates once into a temporary vector. It checks collisions again because
expression callbacks may have created the requested target. A final ``#auto``
component delegates unique naming to vector creation.

The new vector has the evaluated real or complex type and no mapped array.
Its storage pointer, length, capacity and release procedure are transferred
from the temporary; the temporary is detached before destruction. This avoids
conversion through a Tcl list and never changes an existing vector's type.
On success the Tcl result is the new qualified name. Errors dispose of the
owned temporary, but do not undo side effects of expression callbacks.

Rename, destruction and enumeration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Rename uses literal registry names rather than range syntax. It preserves the
C object, data allocation and observer connections. The mapped array retains
its original name and association. A commandless vector can be renamed by
moving its registry entry alone. Otherwise the handler calls global ``::rename``
and monitors command traces for deletion, recursive rename and a changed final
command name. It checks registry collisions again before moving the entry.
These checks are not rollback: trace side effects, including an already renamed
command, can remain after a later error. Read-only data does not prohibit rename.

Destroy resolves each argument immediately before freeing that vector. Earlier
destructions remain if a later name fails, and callbacks can affect later
lookups. Its lookup accepts a selection, but destruction removes the whole
vector, not the selected values. Calling destroy without names succeeds without
work. Destruction remains allowed for read-only vectors.

Names enumerates registry keys, not Tcl command aliases or array variables.
The optional Tcl glob pattern matches the fully qualified key, and results
follow hash-table order rather than sorted order. Enumeration performs no
vector notification. The range-aware lookup used by destroy is different:
:c:func:`Rbc_VectorLookupName` can change a selection and evaluate index expressions,
returns a borrowed pointer, and does not retain a vector for its caller.

Shared command-helper ownership
-------------------------------

The common helpers separate storage preparation, commit and publication.
A successful preparation alone does not change the vector's metadata or
publish data. The ownership transition depends on the helper:

.. list-table:: Storage returned by command helpers
   :header-rows: 1
   :widths: 30 35 35

   * - Helper
     - Successful result
     - Caller responsibility
   * - :c:func:`PrepareCommandWrite <rbcVectorCommands.PrepareCommandWrite>`
       / :c:func:`PrepareCommandResize <rbcVectorCommands.PrepareCommandResize>`
     - Current borrowed pointer, independent allocation, or NULL for a zero-capacity resize.
     - Initialize required values; commit or release only independently allocated storage.
   * - :c:func:`AllocateCommandData <rbcVectorCommands.AllocateCommandData>`
     - Independent, uninitialized allocation, including for zero values.
     - Release with ``ckfree`` or transfer at commit.
   * - :c:func:`ConvertCommandList <rbcVectorCommands.ConvertCommandList>`
     - Independent converted values; NULL for an empty list.
     - Release non-NULL storage or transfer at commit.
   * - :c:func:`FinishCommandWrite <rbcVectorCommands.FinishCommandWrite>`
     - Completed storage installed in the vector; no output allocation is returned.
     - Do not free transferred storage. Recheck liveness before further vector access.

Failed preparation does not establish a usable output contract: output fields
may have been written before failure. In particular, a prepared current pointer
is borrowed and cannot be freed as abandoned staging storage. These helpers
are synchronous; holding a prepared pointer across unrelated Tcl evaluation
requires separate protection against storage replacement. Their
callers retain the vector and interpreter; the helpers' liveness checks cannot
make an unretained pointer safe. :c:func:`CheckCommandRange <rbcVectorCommands.CheckCommandRange>` validates each endpoint
against current length but does not establish ordering or writability.

:c:func:`PrepareCommandWrite <rbcVectorCommands.PrepareCommandWrite>` reuses sufficient capacity and grows geometrically when
necessary, copying the current logical prefix. :c:func:`PrepareCommandResize <rbcVectorCommands.PrepareCommandResize>` instead
recomputes capacity from the explicit-resize policy, including zero capacity
at zero length and smaller allocations after substantial shrink. Neither helper
initializes added values or releases the old allocation. A prepared pointer may
still be the vector's current storage, so an abandoned preparation must not
unconditionally free it. Callers must supply nonnegative requested lengths.

:c:func:`AllocateCommandData <rbcVectorCommands.AllocateCommandData>` always returns independent uninitialized storage, with
a one-byte allocation for an empty result. :c:func:`ConvertCommandList <rbcVectorCommands.ConvertCommandList>` instead uses
NULL for an empty successful conversion and retains a private outer Tcl list
while converting its elements. Neither helper installs destination metadata.
Conversion failure frees staged storage; effects of conversion callbacks remain.

:c:func:`FinishCommandWrite <rbcVectorCommands.FinishCommandWrite>` installs data, dimensions, whole-vector selection and
invalid extrema before releasing detached old storage. Reused storage keeps its
release procedure; a new pointer becomes dynamically owned. Custom release
callbacks see the committed vector, may change or destroy it, and cannot be
followed by unchecked storage access. The helper preserves interpreter state
across release and does not notify observers.

:c:func:`NotifyCommandWrite <rbcVectorCommands.NotifyCommandWrite>` performs enabled cache flushing, rechecks liveness and
publishes either a full update or a suffix beginning at the supplied index.
The suffix ends at the current length after flushing. It preserves command
results and error options, but does not freeze the vector against trace or
observer side effects.

Output identity and partial creation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Output names are qualified in the current namespace before lookup. A missing
local output therefore does not reuse a same-named global vector. Existing
outputs must have matching type and be writable; resolving them does not
replace their commands or remap their variables. New outputs are initially
created without a mapped array so mapping traces cannot observe unfinished data.

:c:func:`GetCommandOutputs <rbcVectorCommands.GetCommandOutputs>` first checks existing destinations, then collects
preserved identities and creates missing outputs. This is not an all-or-nothing
creation transaction: a later failure releases references and temporary records
but does not destroy earlier new vectors. Duplicate output names can represent
multiple references to the same object; handlers decide whether that aliasing
is permitted for their operation.

After data commit, :c:func:`PublishCommandOutput <rbcVectorCommands.PublishCommandOutput>` adds a default array mapping only
for new real vectors whose names contain no opening parenthesis. It publishes
the retained identity rather than finding a possibly different vector by name.
Mapping errors remain errors after commit; successful mapping and notification
do not roll back callback effects. :c:func:`ReleaseCommandOutputs <rbcVectorCommands.ReleaseCommandOutputs>` releases the
preservation references and record array, not the vectors themselves.

Permutation mechanics
~~~~~~~~~~~~~~~~~~~~~

The sort helpers operate on an index array. The first vector's inclusive
selection determines the initial storage indices, and every key vector must
support those same indices. The allocation helper checks numeric types and
allocation sizes, but does not validate all key lengths for its callers.

:c:func:`CompareVectorIndices <rbcVectorCommands.CompareVectorIndices>` examines
keys lexicographically. Equal comparisons advance to
the next key; a NaN also advances because both ordered comparisons are false.
No original-index tie-breaker is added.
:c:func:`SortVectorIndices <rbcVectorCommands.SortVectorIndices>` uses
:c:func:`SiftVectorIndices <rbcVectorCommands.SiftVectorIndices>` to repair the heap.
Sorting therefore remains unstable
and does not define a total ordering for NaNs. It uses constant auxiliary
storage beyond the permutation and O(n log n) comparisons, each potentially
examining every key. The numerical vectors remain untouched until a command
handler applies the resulting permutation.
