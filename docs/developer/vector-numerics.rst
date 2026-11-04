Vector queries and numerical generators
=======================================

These instance handlers live in ``rbcVecCmd.c``; they are distinct from the
expression evaluator in :doc:`expressions` and transform operations in
:doc:`fft`. Their C contracts are collected in :doc:`vector-numeric-reference`.
The callback ownership rules in :doc:`vector-operations` also apply here.

Arithmetic returns values
-------------------------

:c:func:`ArithOp` implements the instance arithmetic operators by constructing a Tcl
list. It does not replace the receiver. A vector operand is resolved first and
its selected length must match the receiver; otherwise the argument is converted
to a scalar and broadcast over the receiver.

A two-item Tcl list is explicitly a complex scalar. For example, ``{3 -4}``
means 3-4i, not the expression 3-4. Complex operands produce pairs, including
when their imaginary component is zero. Real-only arithmetic uses native double
operators, whereas :c:func:`ComplexArithmetic` explicitly reports division by an
exactly zero complex value. The paths do not impose identical exceptional-value
policies. Operand evaluation can execute Tcl. Receiver lifetime is checked
again afterward; arithmetic then reads current storage without further callbacks.
The result list is released on every error, including complex division by zero.

Ranges and searches
-------------------

:c:func:`RangeOp` includes both endpoints and returns descending source order when
the first endpoint exceeds the last. This differs from the shared colon-range
parser, which rejects reversed selections. Returned objects copy values and
do not retain vector storage. Endpoints are evaluated in sequence; the first
is revalidated after evaluating the second. A destroyed receiver, stale endpoint
or append position produces an error before reading storage.

Real :c:func:`SearchOp` accepts one target or two interval bounds. It returns external
indices with the vector offset, or matching values with ``-value``. Complex
search accepts a single target and compares real and imaginary components
separately; complex interval ordering is deliberately unsupported.

``InRange`` supplies the historical tolerance. For an interval narrower than
``DBL_EPSILON``, it tests absolute distance from the upper bound strictly below
that epsilon. Otherwise it normalizes by interval width, accepts positions down to
``-DBL_EPSILON``, and permits a small excess above one. It does not swap bounds or
implement an independent finite-value policy. Search checks positive offset
addition for overflow before returning indices. Receiver lifetime is rechecked
after bound conversion. A clearly reversed real interval explicitly returns an
empty result, regardless of conversion side effects on the interpreter result.

Normalization and random values
-------------------------------

:c:func:`NormalizeOp` refreshes source extrema and computes
``(value-min)/(max-min)``. With no destination it returns a Tcl list. With a
name it stages the full result before resolving a real destination and
transferring completed storage. There is no explicit
constant-vector or nonfinite-input policy in the handler: callers should not
infer a special all-zero result for a zero range. Complex receivers are rejected
by the instance dispatcher's supported-operation check.

:c:func:`RandomOp` preserves length and numeric type, replacing each component with
:c:func:`Rbc_RandomDouble`. Real vectors use one sample per value and complex vectors
two. The shared generator produces samples in [0,1) using the C library random
state; it is not a cryptographic source or a generator owned by each vector.
The handler flushes and reports a full update after filling storage.

Sequence length and endpoint policy
-----------------------------------

For real numeric bounds, :c:func:`SeqOp` computes the count as
``trunc((finish-start)/step)+1``. The truncation is toward zero, retaining the
legacy calculation rather than applying floor or endpoint snapping. A
nonpositive count leaves the old vector unchanged. Positive counts generate
``start+i*step`` for each index, so the requested endpoint is included only when
the count calculation places a sample there.

Numeric bounds and step must be finite, step must be nonzero, and the derived
count must fit the supported Tcl size before conversion. Literal ``end`` has a
different meaning: fill the existing length. It bypasses the numeric-bound
count checks and allows a zero step. This distinction matters when documenting
or changing validation; the two forms currently do not apply identical checks.

Complex sequences use the default step 1+0i. Each varying component supplies a
count through :c:func:`GetComplexSequenceComponentLength`; when both vary, their
counts must match. A constant component with zero step imposes no count, while
zero step with unequal bounds is an error. If both components are constant with
zero step, a numeric finish cannot determine a count and is rejected. Literal
``end`` instead uses the existing length, including for a zero complex step.

Both generators recheck receiver lifetime and writability after argument
conversion. They reuse capacity where possible and fill prepared storage before
committing it and releasing the old allocation. Cache flushing and notification
skip destroyed identities and preserve the command result.

Scalar helper contracts
------------------------

:c:func:`GetArithmeticScalar` distinguishes an explicit pair from a scalar
before calling :c:func:`Rbc_GetComplex`. Its complex flag describes the input
syntax, even if the imaginary component is zero; the flag is not evidence
that conversion succeeded. Conversion can execute Tcl and partially write
its output, so callers must inspect the status before using the value.

:c:func:`Rbc_ComplexAdd`, :c:func:`Rbc_ComplexSub` and
:c:func:`Rbc_ComplexMul` perform ordinary double arithmetic on copied values.
:c:func:`Rbc_ComplexDiv` uses a ratio-based denominator to reduce avoidable
intermediate overflow and underflow. It requires the caller to reject zero
and does not report nonfinite results. :c:func:`ComplexArithmetic` adds the
zero-divisor error check for the instance arithmetic path. These are distinct
contracts, not interchangeable error-reporting interfaces.

:c:func:`Rbc_ComplexFromReal` supplies a zero imaginary component, while
:c:func:`Rbc_VectorValueAsComplex` copies one caller-validated storage index
and promotes real storage if necessary. Neither helper changes a vector's
type or transfers ownership. :c:func:`Rbc_ComplexIsZero` tests exact zero in
both components; it is not a magnitude tolerance test. The declarations are
collected in :doc:`vector-access-reference`.
