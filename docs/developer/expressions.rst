Vector expression evaluation
============================

``rbcVecMath.c`` evaluates numeric expressions using unregistered vector objects. ``rbcVector.c`` selects list output or
named-result creation, while ``rbcVecCmd.c`` owns assignment to an existing vector. These paths share parsing but have
different result ownership and notification responsibilities. See :doc:`expression-reference` and
:doc:`vectors`. Parser state, temporary storage, function adapters and scalar-conversion invariants are described in
:doc:`expression-engine`.

Entry points and result ownership
---------------------------------

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Tcl operation
     - C path and result
   * - ``rbc::vector expr expression``
     - :c:func:`Rbc_ExprVector` with a NULL destination evaluates a temporary, converts its values to a Tcl list and
       frees the temporary.
   * - ``rbc::vector expr -name name expression``
     - :c:func:`Rbc_VectorEvaluate` returns an owned temporary. The command creates a vector of that type, transfers
       storage and returns its qualified name.
   * - ``vectorName expr expression``
     - :c:func:`Rbc_ExprVector` evaluates a temporary and copies into the existing destination. :c:func:`ExprOp
       <rbcVectorCommands.ExprOp>` then flushes its cache when enabled and notifies clients.

A scalar is a vector of length one inside the evaluator. List output still uses a list for one value; complex values are
two-element real/imaginary pairs. There is no registered command or array for an expression temporary.
:c:func:`Rbc_VectorFree` releases temporaries on all exit paths. An internal caller of :c:func:`Rbc_VectorEvaluate` owns
the returned record until it frees it or explicitly transfers its storage.

Parsing and input snapshots
---------------------------

:c:struct:`ParseInfo` tracks the input buffer, scan position and current token. :c:func:`NextValue` performs
precedence-based evaluation; :c:func:`NextToken` handles numeric literals, vector references, operators and Tcl
substitutions. :c:func:`EvaluateExpression` checks that the entire expression was consumed and rejects non-finite
components in the final result.

Vector references are copied into expression storage when encountered. A selected vector range is copied through the
same duplication helper. Later arithmetic can modify that temporary without writing back to the source. This is not a
snapshot of every input taken at expression entry: Tcl code executed between references can change what a later
reference reads.

Variable, bracketed-command and quoted substitutions can invoke Tcl code. The parser also temporarily replaces
delimiters while looking up math functions. Its C string argument therefore remains writable and valid for the whole
evaluation; a string literal is not a suitable direct C argument. Callers keep the interpreter and its vector state
alive during evaluation.

Real and complex temporaries
----------------------------

:c:func:`SetExpressionVectorType` is restricted to unnamed, unmapped temporaries. It releases old storage when changing
type. :c:func:`CopyExpressionVector` sets the temporary's type before duplication; promotion to complex allocates
complex storage and copies real components with zero imaginary parts.

Ordinary elementwise binary arithmetic broadcasts a length-one operand; otherwise operand lengths must match. Individual
operators impose further constraints, such as real-only operations. Temporary promotion is an evaluator implementation
detail, not permission to change a named vector's type.

Assignment and named creation
-----------------------------

For an existing destination, :c:func:`Rbc_ExprVector` checks writability before any expression evaluation. It attaches a
temporary observer to detect destruction while substitutions or map callbacks execute. On successful evaluation it
checks that the original destination survived and is still writable, then copies the result. Recreating the same command
name does not restore the old object's identity. The observer is detached unless destruction already released it.

Matching types are copied. A real result can be copied into a complex target with zero imaginary components. A complex
result is rejected for a real target, even if every imaginary component is zero. The evaluator itself does not flush
mapped-array values or send update notifications; the Tcl instance operation does that after successful copying. See
:doc:`vectors` for immediate callbacks and changed-range ownership.

For named creation, the command resolves the requested namespace and checks for an existing vector or command before
evaluation. It checks collisions again afterward because callbacks may have created that name. A ``#auto`` tail uses the
normal automatic-name allocator. Creation does not map an array, so it cannot invoke a user variable trace while
installing the result.

The newly created empty record receives the temporary's data pointer, length, capacity and release policy. The
temporary's storage pointer and sizes are cleared before freeing it. This avoids conversion through a Tcl list and
avoids a second full numeric-buffer copy at the commit step; evaluating the expression can still allocate and copy
intermediate buffers.

Map callback boundary
---------------------

``map(commandPrefix, expression)`` parses and retains one Tcl command prefix, then evaluates the numeric argument before
calling the prefix once per value. The comma and closing parenthesis are required. Prefix substitutions run once in the
caller's frame. Each invocation appends one argument: a real number or a single Tcl list containing the complex pair.

:c:func:`MapVector` retains its own references to the prefix words, so recursive Tcl evaluation cannot invalidate the
argument array by changing the prefix object's internal representation. It builds results in separate temporary storage.
Each return value must be a finite number or a pair of finite numbers. A pair promotes the result to complex even when
its imaginary component is zero; all-scalar returns produce a real result even for complex input. Empty input makes no
callbacks and leaves an empty real result.

A Tcl error keeps its details and gains the zero-based element index in the error stack. Other non-OK completion codes
become ``RBC VECTOR MAP COMPLETION`` errors; invalid numeric results use ``RBC VECTOR MAP VALUE``. Processing stops at
the first error and releases the partial temporary.

Failure does not commit the partial map result to a named destination, but callbacks may themselves modify or destroy
vectors, change variables or perform other work. Those side effects cannot be rolled back by the expression engine. The
destination observer detects destruction and a second writable check detects locking; neither is a general transaction
or an interpreter-lifetime guard.
