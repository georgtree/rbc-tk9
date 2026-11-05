Grid C reference
================

These private entry points borrow the graph and require the preparation or
cleanup context described in :doc:`grids`.

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
