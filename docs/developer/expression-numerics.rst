Expression reductions and scalar kernels
========================================

The numerical functions in ``rbcVecMath.c`` operate beneath the adapters described in :doc:`expression-engine`. They are
distinct from the instance commands in :doc:`vector-numerics`. Their contracts are collected in
:doc:`expression-numerics-reference`. This chapter records implemented formulas and limitations; it does not change
numerical conventions.

Selection, finite samples and ownership
---------------------------------------

Most real reductions traverse ``first..last`` using :c:func:`First` and :c:func:`Next`, which skip nonfinite
values. Complex reductions traverse the same interval and accept a sample only when both components are finite. Dropping
one component alone would change the sample population and is not how these reducers work.

Expression references are copied into temporaries whose selection covers their used length. That matters for helpers
which do not follow the general selection rule: extrema and normalization scan the full length, while median/quartile
indexing uses full length after requesting a sort index for the selection. Those order-statistic helpers therefore
require a full-length selection. They must not be reused directly for an arbitrary private slice without adjustment.

Reducers borrow storage and do not publish notifications. :c:func:`Sort` and :c:func:`Norm` mutate detached expression
values; sorting allocates an index and temporary value buffer, whereas normalization rewrites the full buffer in
place. Component kernels accept/return numeric values and own no vector or Tcl object. Random kernels additionally
advance the shared random generator.

Implemented reduction formulas
-------------------------------

Let :math:`n` be the number of accepted finite samples and :math:`m` their mean. For complex samples, :math:`|z-m|` is
Euclidean distance in the complex plane.

.. list-table:: Real and complex reductions
   :header-rows: 1
   :widths: 20 40 40

   * - Function
     - Real samples
     - Complex samples
   * - ``sum``
     - :c:func:`Sum`: Ordinary sum
     - :c:func:`ComplexSum`: Componentwise sum
   * - ``prod``
     - :c:func:`Product`: Ordinary product
     - :c:func:`ComplexProduct`: Complex multiplication
   * - ``mean``
     - :c:func:`Mean`: Sum divided by :math:`n`
     - :c:func:`ComplexMean`: Complex sum divided by :math:`n`
   * - ``length``
     - :c:func:`Length`: Finite selected count
     - :c:func:`ComplexLength`: Count with both components finite
   * - ``nz``
     - :c:func:`Nonzeros`: Finite count unequal to zero
     - :c:func:`ComplexNonzeros`: Finite count with either component nonzero
   * - ``adev``
     - :c:func:`AvgDeviation`: :math:`\sum |x-m|/n`
     - :c:func:`ComplexAvgDeviation`: :math:`\sum |z-m|/n`, a real scalar
   * - ``var``
     - :c:func:`Variance`: :math:`\sum (x-m)^2/(n-1)`
     - :c:func:`ComplexVariance`: :math:`\sum |z-m|^2/(n-1)`, a real scalar
   * - ``sdev``
     - :c:func:`StdDeviation`: Square root of positive variance
     - :c:func:`ComplexStdDeviation`: Square root of positive squared-magnitude variance

The complex variance is not the mean of complex squared deviations and is not a covariance matrix. Both deviation
reducers and variance return zero for fewer than two finite samples. Empty sums/products return their identities,
zero/one; empty means are nonfinite and fail final expression validation.

These reducers use ordinary double accumulation. Complex distance uses ``hypot`` to avoid unnecessary overflow in its
intermediate squares, but squaring that distance or accumulating many values can still overflow. There is no compensated
summation or general guarantee for extreme dynamic ranges. Standard deviation returns zero whenever the variance is not
greater than zero, including a NaN comparison; this is an implementation detail worth preserving or deliberately
revising with tests, rather than assuming every nonfinite intermediate propagates.

Real order statistics and normalization
---------------------------------------

:c:func:`Median`, :c:func:`Q1` and :c:func:`Q3` allocate a sorted index without reordering source data. Median selects
the middle value for odd length and averages the middle pair for even length. Empty input or index-allocation failure
returns ``-DBL_MAX``. This is a finite sentinel, so the evaluator's final finite check does not itself turn it into an
error. These helpers have numeric return signatures, not Tcl status returns; their allocation-failure behavior needs
separate attention if revised.

Quartiles use legacy integer index formulas. For length below four, ``q1`` is the minimum and ``q3`` the
maximum. Otherwise, with zero-based sorted positions, set ``mid = (length-1)/2`` using integer division. The lower index
is ``mid/2``; the upper is ``mid + (length-mid)/2``. An odd ``mid`` selects that index, while an even ``mid`` averages
it with the following value. This is not a selectable or universally standardized percentile method. For ``{1 2 3 4}``,
the current results are ``q1=1`` and ``q3=3``.

:c:func:`Norm` implements ``norm`` and computes ``(x-min)/(max-min)`` over the full temporary. There is no
constant-range guard, so nonempty constant input produces nonfinite values and is rejected by final expression
validation. This helper is distinct from any instance normalization command and should not inherit that command's
behavior by assumption.

Higher moments and compatibility
---------------------------------

:c:func:`Kurtosis` implements ``kurtosis`` using sample variance :math:`s^2` and returns math:`\sum (x-m)^4/(n
:s^4)-3`. It returns zero for fewer than two samples or zero variance. This is neither a bias-corrected estimator nor
:the usual population-moment normalization.

The current :c:func:`Skew` calculation (``skew``) uses **absolute** deviations: :math:`\sum |x-m|^3/(n s^3)`. It
therefore loses the sign of asymmetry and is not conventional signed skewness. For ``{-1 0 1}`` it returns approximately
``2/3``, although conventional skewness is zero. Fewer than two samples return zero; constant input of length at least
two produces a nonfinite result.

``skew`` retains this legacy formula for compatibility. The separate ``skewness`` function uses signed population
central moments, with no bias correction: :math:`g_1=m_3/m_2^{3/2}`, where :math:`m_k=\sum(x-m)^k/n`. It returns zero
for symmetric samples and reverses sign under reflection. Empty or zero-variance input returns NaN internally and
becomes an expression domain error; no partial result is assigned to a named destination.

:c:func:`Skewness` scales finite samples by their maximum absolute value before computing the mean and central
moments. This avoids cubing raw magnitudes and keeps moment accumulation bounded for extreme finite values. It performs
three linear scans and allocates no numerical buffer. Like the other real reducers, it skips nonfinite selected
samples. The function is real-only; it does not introduce an ordering or signed skewness definition for complex values.

Complex elementary functions
-----------------------------

Adapter selection depends on the temporary's type. Real input uses real libm functions; it is not automatically promoted
when a real-domain operation fails. An explicitly complex pair selects the complex kernel even when its imaginary part
is zero. See :doc:`expression-engine` for type-changing adapter boundaries.

:c:func:`ComplexLog` combines a scaled logarithmic magnitude with ``atan2`` phase. :c:func:`ComplexSqrt` scales large
components before computing half the magnitude. Its negative-real branch tests ``imag < 0``; negative imaginary zero
therefore chooses the same side as positive zero. Do not claim complete C99 complex signed-zero branch semantics for
these custom kernels.

:c:func:`ComplexTan` and :c:func:`ComplexTanh` use decaying exponentials and rearranged denominators to reduce overflow
and cancellation. Their axis-special cases use ordinary ``tan``; above magnitude 350 the decaying term is set to zero to
avoid an irrelevant underflow setting ``errno``. Sine, cosine and their hyperbolic counterparts retain direct formulas
and can overflow in intermediate libm calls.

:c:func:`ComplexAsin` uses distances to plus/minus one, clamps rounding excursions in its auxiliary quantities and
applies the imaginary sign with ``copysign``. :c:func:`ComplexAcos` derives from inverse sine. :c:func:`ComplexAtan`
scales the angle calculation and uses logarithmic distances to plus/minus i. These strategies reduce specific numerical
problems; they do not guarantee finite results for all finite inputs or remove genuine poles.

:c:func:`ComplexPower` uses exponentiation by squaring for finite real integral exponents, keeping the exponent as a
double rather than narrowing it to a C integer. Zero to zero returns one. Negative powers of zero fail; zero to a
positive real nonintegral power returns zero; a finite nonreal exponent of zero fails. Other cases use
``exp(exponent*Log(base))`` on the implemented logarithm branch.

Most scalar kernels return numbers rather than Tcl statuses. The adapters check ``errno`` and/or component finiteness,
and complete expression evaluation checks its final values again. A successful internal arithmetic dispatch alone is not
a promise of a finite result. Rounding operates independently on complex components and uses half-away-from-zero; it
does not round magnitude or phase.

Scalar ownership and dispatch
-----------------------------

Component kernels receive their numeric input by value. For example, :c:func:`ComplexReal` and :c:func:`ComplexImag`
extract copied components, :c:func:`ComplexConj` returns a copied conjugate, and :c:func:`ComplexAbs` uses the magnitude
of both components. They acquire no vector or Tcl-object references and cannot publish a vector change themselves.

:c:func:`Identity` and :c:func:`Zero` supply the real-input branches of paired adapters. :c:func:`Arg` uses the phase of
a real value; :c:func:`ComplexArg` uses both components. :c:func:`ComplexCeil`, :c:func:`ComplexFloor` and
:c:func:`ComplexRound` operate independently on the two components. Their scalar nature does not imply an
error-reporting or finite-output guarantee: the adapter and final evaluator apply those policies.

:c:func:`Random` ignores its input and advances the shared random generator once. :c:func:`ComplexRandom` draws
independent real and imaginary components, advancing it twice. These scalar signatures therefore describe value passing,
not purity. :c:func:`ComplexValueIsFinite` provides the separate test that both components are finite.

Return values and mutation boundaries
-------------------------------------

The C return type is part of the contract. :c:func:`Sort` returns a Tcl status and leaves values unchanged if its
temporary allocation fails. :c:func:`Norm` also returns a status, but ``TCL_OK`` only means that its loop completed: a
constant range can still produce nonfinite output. Both are mutation helpers for detached temporaries, not
notification-producing named-vector operations.

:c:func:`Median`, :c:func:`Q1` and :c:func:`Q3` instead return a numeric ``-DBL_MAX`` sentinel for an empty vector or
sort-index failure. Do not test these results as Tcl completion codes. Their full-selection requirement is stronger than
the inclusive-range contract of :c:func:`Mean` or :c:func:`ComplexMean`. The real reducers receive an ``Rbc_Vector *``
prefix but cast it back to RBC's internal ``VectorObject``; an independently fabricated public prefix is not a valid
substitute for the internal record.

:c:func:`ComplexPower` writes its output only on ``TCL_OK``. Its explicit zero-base errors leave output storage
unchanged, but successful arithmetic can still be nonfinite. :c:func:`ComplexExp`, :c:func:`ComplexLog10`,
:c:func:`ComplexSin`, :c:func:`ComplexCos`, :c:func:`ComplexSinh` and :c:func:`ComplexCosh` return copied numeric values
without a Tcl status. The caller retains responsibility for checking those results.

:c:func:`LogHypot` avoids forming an overflowing magnitude before taking its logarithm. :c:func:`RealAcosh` requires an
argument at least one. Neither helper supplies a Tcl diagnostic or clears ``errno``. Their callers establish the domain
and apply the expression error policy.

Legacy scalar conventions remain deliberate distinctions: :c:func:`Fabs` preserves negative zero, :c:func:`Round`
computes a rounded double without converting to an integer, and :c:func:`Fmod` implements a floor-based remainder with a
zero-divisor result of zero. These names must not be treated as aliases for the corresponding C library routines.

Full-storage extrema inputs
---------------------------

:c:func:`Rbc_VecMin` and :c:func:`Rbc_VecMax` require real vector storage and scan the complete array, ignoring the
current first/last selection. They update the corresponding scalar cache but do not publish notifications, update
extrema indices or clear invalidation flags. Empty or wholly nonfinite input returns NaN.
