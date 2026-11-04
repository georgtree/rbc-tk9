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

.. list-table:: Notification callback arguments
   :header-rows: 1
   :widths: 20 80

   * - Parameter
     - Contract
   * - ``interp``
     - Borrowed owning interpreter; see :ref:`interpreter arguments <rbc_calling_conventions_1rbc_interp_argument>`.
   * - ``clientData``
     - Caller-owned registration data, never freed or preserved by RBC. The
       observer's owner must keep it valid through nested callbacks.
   * - ``notify``
     - UPDATE announces changed values; DESTROY announces teardown. During
       DESTROY, discard the token and borrowed vector views. Remaining tokens
       are freed by RBC after delivery.

Use :c:func:`Rbc_SetVectorChangedProc` to install or disable the callback, and
:c:func:`Rbc_FreeVectorId` to detach an observer. A token observes an identity;
it is not a preservation reference. For ranged updates, the private
:c:func:`Rbc_VectorGetChangedRange` reports the current callback's snapshot.

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
