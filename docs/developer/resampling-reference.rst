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
