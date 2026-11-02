Spline C reference
==================

These private geometry services use caller-owned arrays, not vector handles.
See :doc:`splines` for parameter formats, failure behavior and graph integration.

.. doxygenfunction:: Rbc_NaturalSpline

.. doxygenfunction:: Rbc_QuadraticSpline

.. doxygenfunction:: Rbc_NaturalParametricSplineEval

.. doxygenfunction:: Rbc_QuadraticParametricSplineEval

.. doxygenfunction:: Rbc_NaturalParametricSpline

.. doxygenfunction:: Rbc_CatromParametricSpline

Implementation helpers
----------------------

.. doxygengroup:: spline_internals
   :members:
