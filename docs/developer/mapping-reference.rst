Coordinate mapping C reference
==============================

These private helpers borrow their graph, axes and track records. Callers prepare current layout and retain the records
throughout the operation.  See :doc:`mapping` for coordinate conventions and scoped viewport usage.

Parameter directions use :doc:`calling-conventions`. Raw data, axis-space coordinates, normalized fractions and widget
pixels are distinct inputs; the helper-stage table in :doc:`mapping` links the internal range implementations in
:doc:`axis-reference`.

Data and screen coordinates
---------------------------

.. doxygenfunction:: Rbc_HMap

.. doxygenfunction:: Rbc_VMap

.. doxygenfunction:: Rbc_Map2D

.. doxygenfunction:: Rbc_InvMap2D

Track layout and viewport scopes
--------------------------------

.. doxygenstruct:: RbcTrackViewport
   :members:

.. doxygenfunction:: Rbc_LayoutTracks

.. doxygenfunction:: Rbc_EnterTrack

.. doxygenfunction:: Rbc_LeaveTrack

The graph layout entry point is documented in :doc:`graph-reference`.

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: ElemComplexVector
   :members:

.. doxygenstruct:: Line
   :members:

.. doxygenstruct:: LineAreaTransaction
   :members:

.. doxygenstruct:: LineBandPolygon
   :members:

.. doxygenstruct:: LineComplexDataTransaction
   :members:

.. doxygenstruct:: LineErrorBand
   :members:

.. doxygenstruct:: LineParamTransaction
   :members:

.. doxygenstruct:: LinePen
   :members:

.. doxygenstruct:: LinePenStyle
   :members:

.. doxygenstruct:: LineScalarTransaction
   :members:

.. doxygenstruct:: LineTrace
   :members:

.. doxygenstruct:: MapInfo
   :members:

.. doxygenstruct:: ParsedSymbol
   :members:

.. doxygenstruct:: SmoothingInfo
   :members:

.. doxygenstruct:: Symbol
   :members:
