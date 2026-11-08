Image compatibility C reference
===============================

These functions are defined by RBC in ``rbcImage.c`` and declared privately in ``rbcImage.h``.
See :doc:`image-compatibility` for lifetime and private-layout requirements.

.. doxygenfunction:: Rbc_ImageIsDeleted

.. doxygenfunction:: Rbc_ImageGetMaster

.. doxygenfunction:: Rbc_ImageGetType

.. doxygenfunction:: Rbc_ImageGetPhotoPixmap

.. doxygenfunction:: Rbc_ImageGetPhotoGC

.. doxygenfunction:: Rbc_NameOfImage

.. doxygenfunction:: Rbc_CreateTemporaryImage

.. doxygenfunction:: Rbc_DestroyTemporaryImage

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: TkImageMasterStruct
   :members:

.. doxygenstruct:: TkImageStruct
   :members:

.. doxygenstruct:: TkPhotoInstanceStruct
   :members:
