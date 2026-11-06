Expression engine internal C reference
======================================

These helpers are private to ``rbcVecMath.c``. They operate on evaluator-owned temporaries and must not be used as
named-vector mutation APIs. See :doc:`expression-engine` for parser state, adapter families and current limitations;
:doc:`expression-reference` covers the shared entry points.

Parameter directions and Tcl ownership use :doc:`calling-conventions`. Parser cursors point into caller-owned
expression storage; returned prefix objects and evaluator temporaries have distinct release requirements. A helper may
modify temporary state before returning an error. See the cursor and failure-state descriptions in
:doc:`expression-engine`.

.. doxygengroup:: expression_engine
   :members:

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: ComplexComponentFunction
   :members:

.. doxygenstruct:: ComplexRealFunction
   :members:

.. doxygenstruct:: ComplexRealScalarFunction
   :members:

.. doxygenstruct:: ComplexScalarFunction
   :members:

.. doxygenstruct:: MathFunction
   :members:
