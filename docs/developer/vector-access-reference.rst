Vector access C reference
=========================

These declarations are private to RBC. See :doc:`vector-access`.

Parameter directions and Tcl ownership follow :doc:`calling-conventions`.
Indices in these contracts are zero-based storage positions unless explicitly
identified as external indices. Selection and append sentinels require caller
validation before storage access.

.. doxygenfunction:: Rbc_VectorFlushCache

.. doxygenfunction:: Rbc_VectorParseElement

.. doxygenfunction:: Rbc_VectorMapVariable

.. doxygenfunction:: Rbc_VectorGetIndex

.. doxygenfunction:: Rbc_VectorGetIndexRange

.. doxygenfunction:: Rbc_GetDouble

.. doxygenfunction:: Rbc_GetComplex

.. doxygenfunction:: Rbc_NewComplexObj

.. doxygenfunction:: Rbc_NewVectorValueObj

.. doxygenfunction:: Rbc_GetValues

.. doxygenfunction:: Rbc_VectorSortIndex

.. doxygenfunction:: Rbc_ReplicateValue

.. doxygenfunction:: Rbc_VectorUpdateRange
