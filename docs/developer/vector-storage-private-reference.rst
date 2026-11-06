Vector storage private C reference
===================================

These registry, allocation, creation and destruction helpers are private to RBC. They are not substitutes for installed
public stub entries. See :doc:`vector-storage` for their relationships and callback restrictions.

.. doxygengroup:: vector_storage_private
   :members:

Parameter and ownership conventions
-----------------------------------

Allocation, storage replacement and notification helpers use explicit parameter directions. A borrowed record may still
be mutated; an input buffer may be adopted if the reset contract says so. Output arguments are not implicitly
initialized on failure. The individual contract defines each case; see :doc:`calling-conventions` for the common
terminology.

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: VectorOpSpec
   :members:

.. doxygenstruct:: ParseInfo
   :members:

.. doxygenstruct:: Rbc_VectorIdStruct
   :members:

.. doxygenstruct:: Rbc_Vector_s
   :members:

.. doxygenstruct:: Value
   :members:

.. doxygenunion:: VectorData

.. doxygenstruct:: VectorInterpData
   :members:
