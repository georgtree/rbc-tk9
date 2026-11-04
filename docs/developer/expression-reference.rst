Vector expression C reference
=============================

These are private interfaces in ``rbcVectorInt.h``, not installed stubs entries.
See :doc:`expressions` for temporary ownership and Tcl callback boundaries.

The caller keeps the interpreter and vector subsystem alive across Tcl
substitutions and map callbacks. A successful evaluated temporary is owned by
the caller and released with :c:func:`Rbc_VectorFree`; a destination passed to
assignment remains caller-owned. The internal helpers described in
:doc:`expression-engine-reference` do not replace these entry-point contracts.

.. doxygenfunction:: Rbc_VectorEvaluate

.. doxygenfunction:: Rbc_ExprVector
