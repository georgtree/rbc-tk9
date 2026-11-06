EPS canvas C reference
======================

See :doc:`eps-canvas` for ownership, failure and platform boundaries.  These are private implementation interfaces.

.. doxygengroup:: eps_canvas
   :members:

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: EpsItem
   :members:

.. doxygenstruct:: EpsParseInfo
   :members:
