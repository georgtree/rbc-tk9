Image resampling C reference
============================

These private services are declared in ``rbcImage.h`` and implemented in
``rbcImage.c``. See :doc:`image-resampling` for coordinate conventions and
:doc:`images-tiles` for image storage ownership.

.. doxygenfunction:: Rbc_GetResampleFilterFromObj

.. doxygenfunction:: Rbc_ResampleColorImage

.. doxygenfunction:: Rbc_ResizeColorImage

.. doxygenfunction:: Rbc_ResizeColorSubimage

.. doxygenfunction:: Rbc_ResamplePhoto

.. doxygenfunction:: Rbc_ResizePhoto

Filter kernels and contribution records
---------------------------------------

These helpers implement the filtered path in ``rbcImage.c``. See
:doc:`image-resampling` for kernel support, coordinate conventions and
fixed-point arithmetic.

.. c:namespace:: rbcImageResampling

.. doxygengroup:: image_resample_internals
   :members:

.. c:namespace-pop::
