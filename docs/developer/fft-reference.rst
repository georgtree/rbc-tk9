FFT and generator C reference
=============================

These private instance-operation handlers are not public stubs entries. See :doc:`fft` for dispatch preconditions and
commit/callback boundaries.

.. doxygenfunction:: Rbc_VectorFftOp

.. doxygenfunction:: Rbc_VectorFftfreqOp

.. doxygenfunction:: Rbc_VectorWindowOp

Argument ownership and units follow :doc:`calling-conventions` and :doc:`fft`. The operation handlers take full instance
command arrays; allocation and kernel helpers take typed value counts. These internal entry points rely on dispatcher or
caller preconditions, particularly for writable real generator destinations.

Implementation helpers
----------------------

.. doxygengroup:: fft_internals
   :members:
