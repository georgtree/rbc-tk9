Vector client C reference
=========================

The opaque types and observer declarations below come from the installed
``rbcVector.h``. Observer functions are not entries in the public stubs table;
external stub users must not assume they are available through that table.
The final section contains private RBC helpers. See :doc:`vectors` for storage,
read-only state and notification reentry and client-data lifetime.

Types
-----

.. doxygentypedef:: Rbc_Vector

.. doxygenenum:: Rbc_VectorType

.. doxygenstruct:: Rbc_Complex
   :members:

.. doxygentypedef:: Rbc_VectorId

.. doxygenenum:: Rbc_VectorNotify

Callback signature (included verbatim to retain Tcl's function-typedef syntax):

.. literalinclude:: ../../generic/rbcVector.h
   :language: c
   :start-at: typedef void(Rbc_VectorChangedProc)
   :end-at: typedef void(Rbc_VectorChangedProc)

Callbacks borrow their data and must obey the lifetime and reentry restrictions
in :doc:`vectors`.

Observers and range access
--------------------------

.. doxygenfunction:: Rbc_AllocVectorId

.. doxygenfunction:: Rbc_FreeVectorId

.. doxygenfunction:: Rbc_GetVectorById

.. doxygenfunction:: Rbc_SetVectorChangedProc

.. doxygenfunction:: Rbc_NameOfVectorId

.. doxygenfunction:: Rbc_VectorNotifyPending

.. doxygenfunction:: Rbc_VectorGetRange

Private update helpers
----------------------

.. doxygenfunction:: Rbc_VectorUpdateClients

.. doxygenfunction:: Rbc_VectorUpdateClientsRange

.. doxygenfunction:: Rbc_VectorGetChangedRange
