Typed command dispatch
=======================

Rbc_GetOpIndexFromObj in ``rbcUtil.c`` selects an operation and validates its
argument count. It does not invoke the callback, own a command object or manage
component lifetime. Each component supplies a persistent typed table whose
first member is Rbc_OpSpecHeader, followed by its correctly typed callback and
any component-specific fields.

Table layout and lifetime
-------------------------

The first header field is the operation name; a NULL name terminates the array.
The caller passes sizeof of the complete entry as specSize, not sizeof of the
header or callback. The helper uses that byte stride both for Tcl's indexed-name
lookup and to locate the selected header. Keeping dispatch at the caller avoids
casting unrelated function pointer types through a shared generic signature.

Tcl_GetIndexFromObjStruct accepts unique operation-name abbreviations and caches
lookup information in the Tcl object. Tables and their name strings must outlive
objects that can reuse that information. Function-local temporary arrays are
not suitable replacements for the usual static const tables.

Argument counts and diagnostics
-------------------------------

operPos identifies the operation word in the complete objc/objv vector. Both
minArgs and maxArgs count every word in that vector, including the widget or
command name and intervening subcommands. A nonpositive maximum is unbounded.

For a missing operation word the helper reports a generic subcommand usage.
For an unknown or ambiguous operation it retains Tcl's lookup error. Once an
operation is selected, a count error uses its usage suffix and the prefix
through the operation word. indexPtr is written only after all checks succeed;
on error callers must not dispatch using an old or uninitialized index.

A nested dispatcher that shifts objc/objv must define its counts against the
vector it actually passes to lookup. For example, the winop image dispatcher
validates its own nested table before removing the image word for the shared
handler. See :doc:`winop-internals`.

Selection versus execution
--------------------------

The helper asserts a non-NULL table/output pointer and an adequate stride in
assert-enabled builds. It does not validate arbitrary memory, ensure callback
signatures match, or check an entire table's structural consistency at runtime.
The table declaration and caller form that contract.

Successful selection also says nothing about callback safety. A selected
operation can evaluate Tcl, change vector storage or destroy its widget. The
owning command must preserve/revalidate the appropriate resources and return
the handler's status. See :doc:`graph-lifecycle`, :doc:`expressions` and
:doc:`bindings` for those separate boundaries.

Maintenance checks
------------------

When adding an operation, test exact names, accepted abbreviations, ambiguity,
missing arguments and excess arguments in the existing component test family.
Check nested paths independently. A header or stride change should also pass
``make test-headers``. Reference documentation describes the common selector;
the individual Tcl command manuals remain the source for user-facing syntax.
