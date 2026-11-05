Allocation, strings and interned names
======================================

``rbcAlloc.c`` and ``rbcUtil.c`` supply shared infrastructure. Their contracts
are not interchangeable: an allocated copy, a reference-counted UID, a borrowed
UID lookup and a scratch formatting buffer all have different lifetimes.
See :doc:`utility-reference` for the selected C declarations.

Allocation and failure policy
-----------------------------

:c:func:`RbcCalloc` checks the multiplication of element count and element size against
SIZE_MAX. Overflow invokes Tcl_Panic. A zero logical size still allocates and
zeros one byte, retaining ckalloc-style non-NULL behavior. :c:func:`RbcStrdup` copies a
non-NULL NUL-terminated string, including its terminator. Both use Tcl allocation
and return storage to release with ckfree, not libc free.

These wrappers do not offer a recoverable allocation-failure protocol. Code
that requires a diagnostic and TCL_ERROR must choose an appropriate allocation
strategy explicitly. A check after a fatal allocator is not a substitute.
Likewise, a successful allocation does not prove that earlier arithmetic used
to compute a count was safe; callers own those calculations.

.. list-table::
   :header-rows: 1
   :widths: 30 40 30

   * - Result
     - Ownership and lifetime
     - Release/copy rule
   * - :c:func:`RbcCalloc` / :c:func:`RbcStrdup`
     - New allocation owned by caller
     - ckfree
   * - :c:func:`Rbc_GetUid`
     - One reference to shared immutable characters
     - :c:func:`Rbc_FreeUid` once per acquisition
   * - :c:func:`Rbc_FindUid`
     - Borrowed existing key, no new reference
     - Acquire separately if it must outlive its owner
   * - :c:func:`Rbc_Itoa` / :c:func:`Rbc_Utoa` / :c:func:`Rbc_Dtoa`
     - One shared process-static scratch buffer
     - Copy immediately if it must be retained
   * - :c:func:`Rbc_GetStringListFromObj`
     - One allocation containing pointers and characters
     - One ckfree of the block

UID table
---------

:c:func:`Rbc_GetUid` lazily initializes a process-static Tcl hash table keyed by string
contents. It increments the entry count and returns the table's key pointer.
Equal names share identity while references remain. :c:func:`Rbc_FreeUid` decrements the
count and deletes the entry at zero. The historical Rbc_Uid typedef is mutable,
but callers must not modify hash-key characters or release them directly.

:c:func:`Rbc_FindUid` returns the same pointer without incrementing its count. It is not
an acquisition. Retaining its result after the last owner releases the UID is
invalid, even if the characters previously matched another string. An unknown
still-valid string passed to :c:func:`Rbc_FreeUid` produces a stderr diagnostic; this is
not protection against NULL, stale or already-freed pointers.

The table is not interpreter-associated and has no synchronization or teardown
hook for each interpreter. Do not treat sharing a UID pointer as cross-thread
support. These declarations already appear in ``rbc.h``; they do not have
entries in the vector stubs table. Documentation adds neither new symbols nor
stub slots. See :doc:`initialization` for the actual client-stubs boundary.

Formatting and result assembly
------------------------------

The integer and double formatting helpers all return the same static buffer.
Calling one overwrites the others' output. In particular, using two formatter
calls as arguments of one function does not produce two independent strings;
argument evaluation order cannot make that safe. Copy or append one result
before requesting the next. These helpers have no thread synchronization.

:c:func:`Rbc_AppendResultStrings` duplicates the current Tcl result object, appends raw
NUL-terminated fragments and installs the completed duplicate. It preserves
shared original objects, but does not quote list elements, retain binary data
past NUL, or set errorCode/errorInfo. Its varargs terminate with a NULL string
pointer. :c:func:`Rbc_DStringAppendElements` instead appends each argument through Tcl's
list-element quoting API. It borrows an initialized DString; the caller remains
responsible for freeing it. Neither helper is an arbitrary Tcl evaluator.

Other utility boundaries
------------------------

:c:func:`Rbc_InitHexTable` initializes only the hexadecimal digit entries. Callers must
initialize the remaining slots to their desired invalid-digit sentinel; the
helper neither clears the table nor maintains an initialization flag.

:c:func:`Rbc_RandomDouble` consumes the C library rand state and divides by RAND_MAX + 1,
producing values from zero inclusive to one exclusive. It does not seed a
per-interpreter generator, promise identical sequences across C libraries or
provide a cryptographic random source.

The same module contains legacy byte-case fallback routines, dictionary
comparison and stdio filename conversion. They are not unified Unicode string
or Tcl-channel abstractions. In particular, :c:func:`Rbc_OpenUtfFile` converts a name to
the default external encoding and uses fopen; it is not Tcl filesystem/VFS
access. Their presence does not change the ownership rules above.

Maintenance checks
------------------

Review allocation/free pairs and UID acquisitions/releases at call sites.
Test shared Tcl result objects when changing result assembly. Keep long-lived
names separate from scratch formatting results. For parser-owned string arrays,
review replacement and failure cleanup together as described in
:doc:`value-conversion`. No generated stubs should be edited for these private
utility changes.

Dictionary ordering and fatal diagnostics
-----------------------------------------

:c:func:`Rbc_DictionaryCompare` handles decimal runs without first converting them to
machine integers. Thus ``x9`` precedes ``x10`` even when runs exceed an integer
value's range. Commas encountered while advancing digit runs are skipped by
the legacy algorithm; this is not a general numeric parser. Leading zeros and
letter case supply secondary differences only after primary comparisons tie.
For example, ``x2`` precedes ``x02``, and ``B`` precedes ``b`` but follows ``a``.
The first secondary difference encountered is retained.

Other characters are decoded with Tcl_UtfToUniChar and compared after Tcl
lowercase conversion. There is no normalization or locale collation. Both
arguments are borrowed Tcl UTF strings despite their historical mutable C
parameter types. Consumers should test the sign of the result, not expect
exact values of minus one or plus one. This comparator is different from the
conditional byte-oriented strcasecmp fallback in the same module.

:c:func:`Rbc_OpenUtfFile` owns only its temporary encoding-conversion buffer. A
successful FILE stream belongs to its caller and must be closed with fclose;
NULL reports stdio failure, without a Tcl interpreter result. This helper
cannot substitute for Tcl filesystem operations on virtual paths.

:c:func:`Rbc_Assert` and :c:func:`Rbc_Panic` are process-fatal diagnostics. They flush stderr and
abort, rather than returning TCL_ERROR or invoking the ordinary ownership
cleanup paths. The assertion function is compiled only in non-NDEBUG builds.
Do not use either for expected malformed user input; that belongs in a
recoverable parser with a diagnostic and explicit cleanup.

Byte-string case conversion
----------------------------

:c:func:`strtolower` is a platform fallback for a writable NUL-terminated byte
string. It uses the C locale's unsigned-byte case mapping in place; it is not
Tcl Unicode conversion and does not allocate a replacement string.

Fallback byte-string comparisons
--------------------------------

:c:func:`strcasecmp` and :c:func:`strncasecmp` use the fixed byte-folding table
when the platform lacks these routines. Their ordering is not Tcl Unicode
ordering and does not follow the C locale used by :c:func:`strtolower`.
The bounded comparison reads at most its byte limit and stops at NUL.
