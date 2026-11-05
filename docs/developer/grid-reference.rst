Grid C reference
================

These private entry points borrow the graph and require the preparation or
cleanup context described in :doc:`grids`.

Grid configuration and mapped caches
------------------------------------

.. doxygenstruct:: Grid
   :members:

.. doxygenstruct:: GridSegments
   :members:

.. doxygenstruct:: PolarLabelAnchor
   :members:

Mapping and output
------------------

.. doxygenfunction:: Rbc_MapGrid

.. doxygenfunction:: Rbc_DrawGrid

.. doxygenfunction:: Rbc_GridExport

Ownership and cleanup
---------------------

.. doxygenfunction:: Rbc_CreateGrid

.. doxygenfunction:: Rbc_DestroyTrackGrid

.. doxygenfunction:: Rbc_DestroyGrid

Implementation helpers
----------------------

.. c:namespace-push:: rbcGrid

.. doxygengroup:: grid_internals
   :members:

.. c:namespace-pop::

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in
the reference does not make private layouts part of the installed C API.

.. doxygenstruct:: GridOpSpec
   :members:
