Expression numerical C reference
================================

These private reducers and scalar kernels implement the expression adapters.
They are not installed C API entry points. See :doc:`expression-numerics` for
sample selection, formulas, ownership and compatibility caveats.

Scalar kernel arguments are copied values, while selection helpers borrow a
live real vector. Neither form acquires vector ownership. Parameter contracts
state the input interpretation; the surrounding adapters control error and
finite-value handling. See :doc:`expression-engine` and
:doc:`calling-conventions`.

.. doxygengroup:: expression_numerics
   :members:
