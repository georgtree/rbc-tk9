Line fill C reference
=====================

These static helpers belong to ``rbcGrLine.c``. They are not cross-module APIs.
See :doc:`fills` for geometry, ownership and renderer fallback rules.

.. doxygengroup:: line_fills
   :members:

Configuration and resource transactions
---------------------------------------

These helpers cover fill resources and the adjacent scalar/symbol parsers used
by line configuration. The scope is documentation-only and avoids collisions
with file-local names in other modules.

.. c:namespace-push:: rbcGrLineOptions

.. doxygengroup:: line_option_transactions
   :members:

.. c:namespace-pop::
