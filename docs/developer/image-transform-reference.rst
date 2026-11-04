Image transform C reference
===========================

These private declarations are in ``rbcImage.h``. Color rotation is implemented
in ``rbcImage.c``; bitmap transforms have Windows and X11 implementations in
``rbcWinImage.c`` and ``rbcUnixImage.c``. See :doc:`image-transforms`.

.. doxygenfunction:: Rbc_RotateColorImage

.. doxygenfunction:: Rbc_RotateBitmap

.. doxygenfunction:: Rbc_ScaleBitmap

.. doxygenfunction:: Rbc_ScaleRotateBitmapRegion

Color rotation implementation
-----------------------------

These CPU-side helpers implement exact quadrant copies and the residual
three-shear path. Native bitmap transforms use separate implementations.

.. c:namespace:: rbcImageRotation

.. doxygengroup:: image_rotation_internals
   :members:

.. c:namespace-pop::

X11 image and bitmap helpers
----------------------------

.. c:namespace:: rbcX11Image

.. doxygengroup:: x11_image_helpers
   :members:

.. c:namespace-pop::
