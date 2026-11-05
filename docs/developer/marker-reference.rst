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

Shared implementation reference
-------------------------------

These groups cover common transactions, lifecycle, mapping helpers and
commands. Concrete class contracts and pass batching follow below. Namespaces disambiguate private names without changing C linkage.

Option preparation and commit
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. c:namespace-push:: rbcGrMarkerConfiguration

.. doxygengroup:: marker_configuration
   :members:

.. c:namespace-pop::

Record lifetime
~~~~~~~~~~~~~~~

.. c:namespace-push:: rbcGrMarkerLifetime

.. doxygengroup:: marker_lifecycle_core
   :members:

.. c:namespace-pop::

Mapping helpers and picking
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. c:namespace-push:: rbcGrMarkerMapping

.. doxygengroup:: marker_mapping_core
   :members:

.. c:namespace-pop::

Registry and commands
~~~~~~~~~~~~~~~~~~~~~

.. c:namespace-push:: rbcGrMarkerCommands

.. doxygengroup:: marker_commands
   :members:

.. c:namespace-pop::

Line geometry and lifetime
--------------------------

.. c:namespace-push:: rbcGrMarkerLine

.. doxygengroup:: marker_line_geometry
   :members:

.. c:namespace-pop::

Polygon and arc geometry and ownership
--------------------------------------

.. c:namespace-push:: rbcGrMarkerPolygon

.. doxygengroup:: marker_polygon_geometry
   :members:

.. c:namespace-pop::

Geometric renderer dispatch
---------------------------

.. c:namespace-push:: rbcGrMarkerShapeOutput

.. doxygengroup:: marker_shape_output
   :members:

.. c:namespace-pop::

Bitmap marker resources and presentation
----------------------------------------

.. c:namespace-push:: rbcGrMarkerBitmap

.. doxygengroup:: marker_bitmap
   :members:

.. c:namespace-pop::

Image marker resources and presentation
---------------------------------------

.. c:namespace-push:: rbcGrMarkerImage

.. doxygengroup:: marker_image
   :members:

.. c:namespace-pop::

Text marker layout and presentation
-----------------------------------

.. c:namespace-push:: rbcGrMarkerText

.. doxygengroup:: marker_text
   :members:

.. c:namespace-pop::

Window marker geometry management and callbacks
-----------------------------------------------

.. c:namespace-push:: rbcGrMarkerWindow

.. doxygengroup:: marker_window
   :members:

.. c:namespace-pop::

Pass batching and renderer dispatch
-----------------------------------

.. c:namespace-push:: rbcGrMarkerBatches

.. doxygengroup:: marker_batches
   :members:

.. c:namespace-pop::

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in
the reference does not make private layouts part of the installed C API.

.. doxygenstruct:: MarkerClass
   :members:

.. doxygenstruct:: MarkerOpSpec
   :members:

.. doxygenstruct:: MarkerStruct
   :members:

.. doxygenstruct:: ParsedMarkerOptions
   :members:
