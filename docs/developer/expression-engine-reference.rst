Expression engine internal C reference
======================================

These helpers are private to ``rbcVecMath.c``. They operate on evaluator-owned
temporaries and must not be used as named-vector mutation APIs. See
:doc:`expression-engine` for parser state, adapter families and current
limitations; :doc:`expression-reference` covers the shared entry points.

.. doxygengroup:: expression_engine
   :members:
