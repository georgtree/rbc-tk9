Export orchestration C reference
================================

These interfaces are private. See :doc:`exports` for ownership, format policy
and the distinction between ending export mode and restoring screen layout.

Document context
----------------

.. doxygenfunction:: Rbc_ExportInit

.. doxygenfunction:: Rbc_ExportError

.. doxygenfunction:: Rbc_ExportFree

.. doxygenfunction:: Rbc_ExportAppend

.. doxygenfunction:: Rbc_ExportFormat

Graph command and traversal
---------------------------

.. doxygenfunction:: Rbc_InitGraphExportOptions

.. doxygenfunction:: Rbc_GraphExportOp

.. doxygenfunction:: Rbc_ExportBeginGraph

.. doxygenfunction:: Rbc_ExportEndGraph

.. doxygenfunction:: Rbc_ExportGraph
