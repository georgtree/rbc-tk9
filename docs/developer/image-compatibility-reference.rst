Image compatibility C reference
===============================

These functions are defined by RBC in ``rbcImage.c`` and declared privately in ``rbcImage.h``, including those whose
names start with ``Tk_``. See :doc:`image-compatibility` for lifetime and private-layout requirements.

.. doxygenfunction:: Tk_ImageIsDeleted

.. doxygenfunction:: Tk_ImageGetMaster

.. doxygenfunction:: Tk_ImageGetType

.. doxygenfunction:: Tk_ImageGetPhotoPixmap

.. doxygenfunction:: Tk_ImageGetPhotoGC

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
