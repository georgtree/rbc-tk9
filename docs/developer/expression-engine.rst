Expression parser and adapter internals
=======================================

``rbcVecMath.c`` contains a recursive expression evaluator, not a wrapper around
Tcl's ``expr``. It uses Tcl parsing and evaluation for substitutions, then
performs vector arithmetic on its own temporary records. See :doc:`expressions`
for command entry points and result ownership, :doc:`expression-numerics` for
reduction formulas and scalar kernels, and
:doc:`expression-engine-reference` for the source contracts.

Parser state and precedence
---------------------------

``Value`` holds an already allocated temporary vector. ``ParseInfo`` borrows the
complete writable expression string and stores ``nextPtr`` plus the last scanned
token. A token describes characters immediately before ``nextPtr``; returning
from a recursive value parse does not rewind the terminating operator.

:c:func:`NextValue` allocates a second operand temporary for each invocation. It first
handles an ordinary value, parenthesized expression or unary operator, then
recursively obtains right operands while their operators have greater precedence
than the caller's limit. It stops at the end, comma, closing parenthesis or an
operator at or below that limit. The second temporary is reused for subsequent
operators in that invocation and freed on every exit.

The precedence table is part of RBC's grammar. Multiplication/division/modulus
bind more tightly than addition/subtraction; rotations follow; comparisons,
equality and logical operators occupy lower levels. Exponentiation is above
multiplication but below unary operators. Equal precedence stops recursion, so
the current implementation groups repeated operators, including exponentiation,
from the left. Do not assume Tcl or another numerical language's associativity.

Both operands are evaluated before logical AND/OR are applied. They are
vector operations, not short-circuit control flow: a command substitution in
the right operand can run even when every left value already determines the
logical result. Failed evaluation discards temporary results but cannot undo
those Tcl side effects.

Token recognition and substitutions
------------------------------------

:c:func:`NextToken` tries an unsigned-leading numeric token first; leading ``+`` and
``-`` remain operator tokens. It recognizes punctuation/operators directly.
Other text first tries a known function call, then a vector/range reference.
An unknown function name returns the internal sentinel ``TCL_RETURN`` from
:c:func:`ParseMathFunction` so vector parsing can try the same text. It does not mean
a Tcl script executed ``return``.

Function syntax requires the opening parenthesis immediately after the scanned
name. Lookup temporarily changes it to NUL, then restores it. This is why the
expression input must be writable even though most parsing only reads it.
Ordinary function arguments are complete expressions ending at a close
parenthesis. ``map`` has its own two-argument parser.

.. list-table:: Value-producing paths
   :header-rows: 1
   :widths: 25 45 30

   * - Syntax/path
     - Evaluation
     - Conversion
   * - Bare number
     - ``strtod`` token scan
     - Explicit real scalar setter
   * - Variable
     - ``Tcl_ParseVar``; traces may run
     - :c:func:`ParseString`
   * - Bracketed script
     - Find closing bracket with Tcl parser, then evaluate script
     - Retained result through :c:func:`ParseString`
   * - Quoted value
     - Tcl quoted tokens and substitutions
     - Retained result through :c:func:`ParseString`
   * - Braced value
     - Tcl brace token rules
     - Retained result through :c:func:`ParseString`
   * - Vector/range
     - Registry and index lookup
     - Copy selected values into temporary storage

:c:func:`ParseString` accepts a completely consumed number, a two-element complex
pair, or a completely consumed vector reference. It does not turn an arbitrary
list of numbers into a vector. Substitution-result conversion retains the Tcl
result object before clearing the interpreter result, protecting the string
while conversion itself may report an error.

Bracket scanning uses ``Tcl_ParseCommand`` to find the real closing bracket
rather than searching for the next literal ``]``. Token evaluation follows Tcl's
quote/brace rules. These helpers do not independently retain the interpreter or
its registry across arbitrary destructive callbacks. In particular, ordinary
function lookup borrows a hash entry while its argument expression is evaluated.
Callers must obey the surrounding lifetime requirements.

Temporary storage and type changes
-----------------------------------

Only evaluator temporaries may change numeric type. They have no registry entry,
command or mapped array. :c:func:`SetExpressionVectorType` discards their old storage;
promotion instead allocates complex capacity, copies used real values with zero
imaginary parts and then releases the old allocation. These operations do not
change named-vector type or send client notifications.

:c:func:`FreeExpressionData` and :c:func:`ReleaseExpressionStorage` are deliberately small
private helpers. They rely on exclusive temporary ownership. Unlike the public
reset APIs, they do not install new metadata before a custom release callback or
retain the vector through destructive reentry. Ordinary evaluator allocations
are dynamic storage with no user release callback; do not reuse these helpers
as a general named-vector commit API.

Complex binary arithmetic promotes the left temporary and broadcasts a
length-one operand; other lengths must agree. Boolean operators build a separate
real result before replacing the left storage. Equality compares both components
exactly; logical truth is nonzero in either component. Ordering and modulus are
not supported by the complex operator dispatcher.

Shift operators rotate values rather than shift integer bits. A count must be a
scalar with zero imaginary part. Positive finite counts are reduced modulo the
length and truncated; nonpositive counts retain the historical no-op behavior.
Length zero or one returns before finite-count validation. Rotation uses a hold
buffer for the wrapped portion and overlap-safe movement for the remainder.

Function adapters and validation
--------------------------------

Built-in descriptors are shared static records, while each interpreter owns its
lookup table. Descriptor ``clientData`` selects the actual component, reduction
or whole-vector procedure. The adapter controls type handling and storage:

.. list-table:: Adapter families
   :header-rows: 1
   :widths: 30 70

   * - Adapter
     - Contract
   * - :c:func:`ComponentFunc` / :c:func:`ComplexComponentFunc`
     - Transform values in place; errors can leave earlier temporary values changed.
   * - :c:func:`ComplexRealFunc`
     - Complex input is converted into a separate real buffer, installed after validation.
   * - :c:func:`ScalarFunc` / :c:func:`ComplexScalarFunc`
     - Compute a reduction before resizing to one; retain the supported input type.
   * - :c:func:`ComplexRealScalarFunc`
     - Compute before releasing/retyping, then store one real result.
   * - :c:func:`VectorFunc`
     - Reject complex input and delegate the whole-vector mutation policy.

Real component iteration uses :c:func:`First`/:c:func:`Next` to skip nonfinite values within
the selected interval. Complex component iteration visits the full used length.
Those are different internal policies, not interchangeable loops. The final
:c:func:`EvaluateExpression` pass rejects any nonfinite real or imaginary component
remaining in the result. Reducer adapters check ``errno`` but can defer finite
validation to that final pass. :c:func:`MathError` reads ``errno``; callers relying on
it must reset it before the operation.

The ``map`` adapter differs from these C math adapters. It retains each command
prefix word, allocates an independent result, validates every returned scalar or
pair, and attaches an element index to callback errors. Prefix substitutions run
once, numeric argument evaluation follows, and only then do per-element calls
begin. See :doc:`expressions` for its completion-code and promotion rules.

Scalar conversion and operand reuse
------------------------------------

:c:func:`NextValue` reuses its right-operand temporary between operations. Each parsed
value must therefore establish its own type, rather than relying on the type
left by a preceding operand. The numeric branch of :c:func:`ParseString` uses
:c:func:`SetExpressionRealScalar`, just like bare numeric tokens. A preceding complex
operand cannot contribute a stale imaginary component to a later real scalar.

Braces, quotes, variables and command substitutions all use this conversion.
These expressions both return ``{{9.0 6.0}}``::

   rbc::vector expr {{1 2} + {3 4} + {5}}
   rbc::vector expr {{1 2} + {3 4} + 5}

A two-element pair still explicitly establishes complex type, including a pair
whose imaginary component is zero. This rule concerns evaluator temporaries;
it does not permit changing the type of an existing named vector.
