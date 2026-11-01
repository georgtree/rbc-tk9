Vector names, indices and mapped arrays
=======================================

The registry and storage lifetime are described in :doc:`vectors`. Access
helpers in ``rbcVector.c`` translate Tcl input into borrowed objects and storage
indices; they do not create independent slice objects. Their private contracts
are collected in :doc:`vector-access-reference`.

Names and selections
--------------------

``Rbc_VectorParseElement`` looks for the longest existing vector name at a
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
``Rbc_VectorMapVariable`` removes the old mapping before establishing the new
one; failure does not restore the old array. Mapping and public cache flushing
preserve the vector/interpreter across variable traces and check for destruction
or reentrant remapping before reinstalling RBC's trace. This local protection
does not automatically protect the caller after the helper returns.

Value objects
-------------

``Rbc_GetDouble`` first tries direct Tcl conversion, then expression evaluation.
``Rbc_GetComplex`` accepts a two-item list or a scalar with imaginary part zero.
The pair conversion can write its real component before imaginary conversion
fails. Neither helper establishes an atomic vector update or retains a vector.

The output helpers return new reference-count-zero Tcl objects containing copied
values: doubles for real storage and two-double lists for complex storage.
They do not validate storage bounds. ``Rbc_GetValues`` uses an inclusive interval
and returns an empty list for a reversed interval.
