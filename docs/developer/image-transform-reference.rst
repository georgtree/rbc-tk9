Image transform C reference
===========================

These private declarations are in ``rbcImage.h``. Color rotation is implemented
in ``rbcImage.c``; bitmap transforms have Windows and X11 implementations in
``rbcWinImage.c`` and ``rbcUnixImage.c``. See :doc:`image-transforms`.

.. doxygenfunction:: Rbc_RotateColorImage

.. doxygenfunction:: Rbc_RotateBitmap

.. doxygenfunction:: Rbc_ScaleBitmap

.. doxygenfunction:: Rbc_ScaleRotateBitmapRegion
