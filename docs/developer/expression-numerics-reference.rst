Expression numerical C reference
================================

These private reducers and scalar kernels implement the expression adapters.
They are not installed C API entry points. See :doc:`expression-numerics` for
sample selection, formulas, ownership and compatibility caveats.

Scalar kernel arguments are copied values, while reducers borrow a
live real or complex internal vector. Neither form acquires vector ownership. Parameter contracts
state the input interpretation; the surrounding adapters control error and
finite-value handling. See :doc:`expression-engine` and
:doc:`calling-conventions`.

Reducers document selected versus full-length input explicitly. Numeric return
values, Tcl statuses and output-pointer writes have different failure contracts;
see :doc:`expression-numerics` before reusing a helper outside its adapter.

.. doxygengroup:: expression_numerics
   :members:
