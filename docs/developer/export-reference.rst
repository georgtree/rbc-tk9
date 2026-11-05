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

SVG and PDF command helpers and format adapters
-----------------------------------------------

The policy record and internal helpers below implement the transaction described
in :doc:`exports`. The concrete adapters own document framing, while shared
services own configuration, geometry restoration and delivery.

.. c:namespace:: rbcGraphExportCommands

.. doxygengroup:: graph_export_commands
   :members:

.. c:namespace-pop::

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in
the reference does not make private layouts part of the installed C API.

.. doxygenstruct:: Rbc_ExportContext
   :members:
