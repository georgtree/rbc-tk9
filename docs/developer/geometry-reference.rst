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

Parameter directions follow :doc:`calling-conventions`. Inputs remain borrowed; output buffers belong to the
caller. Counts, distances and booleans have distinct completion conventions, summarized in :doc:`geometry`.

Implementation helpers
----------------------

.. doxygengroup:: geometry_internals
   :members:

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: Dim2D
   :members:

.. doxygenstruct:: Extents2D
   :members:

.. doxygenstruct:: Extents3D
   :members:

.. doxygenstruct:: Point2D
   :members:

.. doxygenstruct:: Point3D
   :members:

.. doxygenstruct:: Rbc_Dashes
   :members:

.. doxygenstruct:: Rbc_Pad
   :members:

.. doxygenstruct:: Region2D
   :members:

.. doxygenstruct:: Segment2D
   :members:
