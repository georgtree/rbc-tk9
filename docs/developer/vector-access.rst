Vector names, indices and mapped arrays
=======================================

The registry and storage lifetime are described in :doc:`vectors`. Access
helpers in ``rbcVector.c`` translate Tcl input into borrowed objects and storage
indices; they do not create independent slice objects. Their private contracts
are collected in :doc:`vector-access-reference`.

Names and selections
--------------------

:c:func:`Rbc_VectorParseElement` looks for the longest existing vector name at a
balanced-parenthesis boundary. This lets a name such as ``v(out)`` coexist with
range syntax: ``v(out)(1:20)`` selects a range from that vector. Without an
``endPtr`` the whole argument must be consumed; expression parsing supplies an
``endPtr`` to receive the remaining operators.

Successful lookup writes ``first`` and ``last`` on the vector itself. These are
inclusive storage indices, shared by subsequent operations on the same object.
A caller needing stable selection across another lookup or Tcl evaluation must
save the indices, and must separately ensure the vector remains alive. Parse
errors restore the previous selection; a returned pointer is borrowed.

Ordinary numeric indices subtract the vector's external offset. ``end`` forms
are relative to length, while ``++end`` selects the append position. Registered
special indices return a sentinel and callback rather than a storage index.
Bounds checking does not make these special cases safe to dereference. Range
consumers must also handle empty vectors explicitly.

Index expressions and numerical conversion can execute Tcl. Index-expression
evaluation retains the vector and interpreter and detects their deletion before
using the result; vector-reference parsing retains the object through range
parsing so failure cannot restore selection into freed memory. In particular,
index evaluation uses the owning interpreter even if the caller passes a NULL
interpreter to suppress lookup diagnostics. Suppressing an error message is not
a promise that lookup has no side effects.

Mapped arrays
-------------

The mapped Tcl array is a traced access surface and cache, not the authoritative
vector storage. A read materializes a scalar or range; a write converts and
assigns values; unsetting an indexed range compacts the following values.
Writes notify the affected inclusive range, while removal reports a full change
because subsequent source indices shift. Read-only state blocks data writes
and element unsets. Whole-array unsets instead unmap the array and may destroy
the vector according to its ownership setting.

Array names cannot contain parentheses, even though vector command names can.
:c:func:`Rbc_VectorMapVariable` removes the old mapping before establishing the new
one; failure does not restore the old array. Mapping and public cache flushing
preserve the vector/interpreter across variable traces and check for destruction
or reentrant remapping before reinstalling RBC's trace. This local protection
does not automatically protect the caller after the helper returns.

Cache flushing removes RBC's trace before unsetting the array, then recreates
``end`` and reinstalls the trace only if the saved name and flags still match.
Its void interface does not propagate Tcl variable-operation status or preserve
the interpreter result. Callers needing result preservation must supply it;
NotifyCommandWrite does so, while the instance clear handler simply returns.

Mapping copies its requested name before any traces can invalidate it. After
the old mapping is removed it unsets the target and writes ``end``. A failed
initial write still leaves the requested name recorded, although RBC's trace
is installed only on a successful write. Errors are therefore not evidence
that no mapping metadata changed. Reentrant mapping changes are detected and
reported rather than overwritten by the outer operation.

The mapped-array trace now retains the vector record and interpreter throughout
the access, including conversion, cache flushing and immediate notification.
Writes copy the input Tcl object before conversion, so replacement or removal
of the source array entry cannot invalidate a complex pair being converted.
Before committing, the trace checks that the vector survives, remains writable,
and has the same storage pointer, length, capacity and offset as before value
conversion. Structural changes reject the outer write; callback side effects
remain. A replacement vector with the same name is not the original target.

For example, a value expression that empties its own vector now reports an
error instead of writing through the old index into released storage. Ordinary
expressions and append writes retain their existing behavior. The checks do not
freeze values or selection: a callback may edit values without changing the
layout, and the outer write then applies to its captured interval.

Element-unset cache flushing uses the same guarded implementation as other
vector updates. User unset traces may destroy the vector or move its mapping;
RBC does not reinstall a trace on a stale mapping or notify a destroyed vector.
Read-result objects are retained while being stored in the array. Registered
special-index callbacks are checked for vector destruction before storing their
result. No public C signature or stub-table entry changes.

Value objects
-------------

:c:func:`Rbc_GetDouble` first tries direct Tcl conversion, then expression evaluation.
:c:func:`Rbc_GetComplex` accepts a two-item list or a scalar with imaginary part zero.
The pair conversion can write its real component before imaginary conversion
fails. Neither helper establishes an atomic vector update or retains a vector.

The output helpers return new reference-count-zero Tcl objects containing copied
values: doubles for real storage and two-double lists for complex storage.
They do not validate storage bounds. :c:func:`Rbc_GetValues` uses an inclusive interval
and returns an empty list for a reversed interval.

Index parsing branches and caller obligations
---------------------------------------------

The index parser distinguishes several forms before applying ordinary numeric
bounds checks:

* Empty strings are rejected.
* Strings beginning with ``e`` go through Tcl's end-index parser. These forms
  require a nonempty vector and a result within its current storage bounds,
  regardless of ``INDEX_CHECK``. They do not subtract the external offset.
* ``++end`` returns the storage length, including zero for an empty vector.
  It is an append position, not a readable element.
* Registered special names are recognized only when a procedure-output pointer
  is supplied. The parser returns ``SPECIAL_INDEX`` and the procedure pointer;
  it does not call the procedure.
* Other strings first try integer conversion, then Tcl expression evaluation.
  The resulting external integer is converted to storage coordinates by
  subtracting the current offset, with checks against negative results and
  signed overflow. ``INDEX_CHECK`` adds the upper-bound check.

For example, a length-three vector with offset 10 maps numeric index ``11``
to storage index 1, ``end`` to storage index 2, and ``++end`` to storage index
3. A numeric index above the end can be accepted without ``INDEX_CHECK``;
that does not authorize the caller to read it. Failed conversion leaves the
index output unchanged, while the optional procedure output is cleared on entry.

The colon-range parser splits at the first colon when ``INDEX_COLON`` is set.
It does not parse a Tcl expression grammar to locate that separator. An omitted
lower endpoint is storage zero. An omitted upper endpoint is length minus one,
or zero for an empty vector. Thus ``:`` on an empty vector is not a valid
one-element storage interval: consumers must handle emptiness explicitly.
Registered special procedures apply only to single-index parsing, not range
endpoints.

Endpoints are staged locally before selection is committed. This prevents a
partial parser commit, but does not undo effects of Tcl index expressions.
The vector's selection is shared mutable state, not part of a separately owned
view. Retaining the object protects its record across callbacks; it does not
freeze its length, offset, selection or values. Consumers must validate the
state they require after callback-capable parsing.

Finite extrema and low-level writes
-----------------------------------

:c:func:`Rbc_VectorUpdateRange` scans the whole real vector, independently of its
selection or external offset. It ignores both infinities and NaNs, keeps the
first storage index for tied minima/maxima, and clears ``UPDATE_RANGE`` after
installing the cache. Empty and entirely nonfinite vectors use NaN bounds and
indices of -1. Complex vectors use that same cache representation rather than
ordering complex values.

:c:func:`Rbc_VectorGetRange` rejects complex vectors without writing its outputs.
For real vectors it recomputes only an invalid cache and returns the bounds;
NaN bounds for an empty/nonfinite vector are a successful result. Both output
pointers are required. Neither helper publishes an update.

:c:func:`Rbc_ReplicateValue` is deliberately a low-level real-storage loop. Its caller
must validate type, writability and bounds, then invalidate or maintain extrema
and publish the change. It does not perform these steps itself. Likewise,
:c:func:`Rbc_GetValues` copies a validated inclusive storage interval into independent
Tcl values; it neither applies the external offset nor alters selection.

A full :c:func:`Rbc_VectorUpdateClients` invalidates extrema before scheduling a
notification. :c:func:`Rbc_VectorUpdateClientsRange` instead attempts to maintain
real extrema from the changed interval. It can do this when the previous
extrema were not overwritten and the old cache is valid; otherwise the next
range query must scan the whole vector. Invalid notification bounds become a
full update. Neither publication helper flushes mapped arrays or enforces
read-only state. Immediate observers can destroy the vector, so a caller that
continues accessing it must preserve and revalidate the object.
