Spline C reference
==================

These private geometry services use caller-owned arrays, not vector handles. See :doc:`splines` for parameter formats,
failure behavior and graph integration.

.. doxygenfunction:: Rbc_NaturalSpline

.. doxygenfunction:: Rbc_QuadraticSpline

.. doxygenfunction:: Rbc_NaturalParametricSplineEval

.. doxygenfunction:: Rbc_QuadraticParametricSplineEval

.. doxygenfunction:: Rbc_NaturalParametricSpline

.. doxygenfunction:: Rbc_CatromParametricSpline

Parameter directions follow :doc:`calling-conventions`. Array capacities are caller obligations; counts describe used
records, not byte sizes. The numerical entry points below do not acquire vector ownership or publish notifications.

Implementation helpers
----------------------

.. doxygengroup:: spline_internals
   :members:

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: Cubic2D
   :members:

.. doxygenstruct:: CubicSpline
   :members:

.. doxygenstruct:: Quint2D
   :members:

.. doxygenstruct:: SplineOpSpec
   :members:
