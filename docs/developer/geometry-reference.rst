Geometry C reference
====================

These declarations remain in ``rbcInt.h``. See :doc:`geometry` for conventions.

.. doxygenfunction:: Rbc_GetClosestPointOnSegment

.. doxygenfunction:: Rbc_LineRectClip

.. doxygenfunction:: Rbc_PolyRectClip

.. doxygenfunction:: Rbc_PointInPolygon

.. doxygenfunction:: Rbc_RegionInPolygon

.. doxygenfunction:: Rbc_PointInSegments

.. doxygenfunction:: Rbc_SimplifyLine

Parameter directions follow :doc:`calling-conventions`. Inputs remain borrowed;
output buffers belong to the caller. Counts, distances and booleans have distinct
completion conventions, summarized in :doc:`geometry`.

Implementation helpers
----------------------

.. doxygengroup:: geometry_internals
   :members:
