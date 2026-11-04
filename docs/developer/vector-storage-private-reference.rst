Vector storage private C reference
===================================

These registry, allocation, creation and destruction helpers are private to RBC.
They are not substitutes for installed public stub entries. See
:doc:`vector-storage` for their relationships and callback restrictions.

.. doxygengroup:: vector_storage_private
   :members:

Parameter and ownership conventions
-----------------------------------

Allocation, storage replacement and notification helpers use explicit parameter
directions. A borrowed record may still be mutated; an input buffer may be
adopted if the reset contract says so. Output arguments are not implicitly
initialized on failure. The individual contract defines each case; see
:doc:`calling-conventions` for the common terminology.
