Marker C reference
==================

These private entry points borrow the graph. Mapping and drawing assume the
caller has prepared the appropriate layout and retained graph lifetime.
See :doc:`markers` for class dispatch, special window behavior and cleanup.

Mapping and output
------------------

.. doxygenfunction:: Rbc_MapMarkers

.. doxygenfunction:: Rbc_DrawMarkers

.. doxygenfunction:: Rbc_MarkersExport

Resource and record cleanup
---------------------------

.. doxygenfunction:: Rbc_ReleaseMarkerTkResources

.. doxygenfunction:: Rbc_DestroyMarkers

Internal class interface
------------------------

The class callbacks are local to ``rbcGrMarker.c``. Their exact signatures and
option-table fields are included directly:

.. literalinclude:: ../../generic/rbcGrMarker.c
   :language: c
   :start-at: typedef Marker *(MarkerCreateProc)
   :end-at: } MarkerClass;
