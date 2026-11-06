Image color processing C reference
==================================

These private services are declared in ``rbcImage.h`` and implemented in ``rbcImage.c``. See :doc:`image-colors` for
algorithms and current limitations.

.. doxygenfunction:: Rbc_ColorImageToGreyscale

.. doxygenfunction:: Rbc_ConvolveColorImage

.. doxygenfunction:: Rbc_QuantizeColorImage

Palette-reduction implementation
--------------------------------

These structures and helpers implement the cumulative-moment partitioner in ``rbcImage.c``. See :doc:`image-colors` for
the stage ordering and invariants.

.. c:namespace:: rbcImageQuantization

.. doxygengroup:: image_quantize_internals
   :members:

.. c:namespace-pop::

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: TrueColorMasks
   :members:
