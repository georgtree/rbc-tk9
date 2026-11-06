Polar data and geometry C reference
===================================

These helpers are private to their implementation modules. See :doc:`polar` for source ownership and the distinction
between data and grid coordinates.

Element data adapters (rbcGrLine.c)
-----------------------------------

.. doxygengroup:: polar_data
   :members:

Grid geometry (rbcGrPolar.c)
----------------------------

.. doxygengroup:: polar_geometry
   :members:

Line and polar data transactions (rbcGrLine.c)
----------------------------------------------

These helpers also cover the real parallel parameter input shared by graph line and polar elements. The
documentation-only scope keeps private names distinct from similarly named helpers in other modules.

.. c:namespace-push:: rbcGrLineData

.. doxygengroup:: line_data_transactions
   :members:

.. c:namespace-pop::

Label placement and presentation (rbcGrPolar.c)
------------------------------------------------

The formatting helpers may execute Tcl. Their contracts describe cancellation of the current drawing pass when callbacks
mutate or destroy its graph.

.. doxygengroup:: polar_labels
   :members:

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: PolarRadiusRange
   :members:
