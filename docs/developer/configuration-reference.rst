Component configuration C reference
====================================

These are private helpers implemented in ``rbcConfig.c`` and declared in
``rbcInt.h``. They introduce no public stubs. See :doc:`configuration` for
transaction ordering, resource identity and cleanup responsibilities.

Resource initialization
-----------------------

.. doxygenfunction:: Rbc_GetComponentResourceName

.. doxygenfunction:: Rbc_InitComponentOptions

Rollback
--------

.. doxygenfunction:: Rbc_RestoreOptionsOnError

Option lookup and metadata
--------------------------

.. doxygenfunction:: Rbc_GetComponentOptionIndex

.. doxygenfunction:: Rbc_GetComponentOptionInfo

.. doxygenfunction:: Rbc_GetCanonicalOptionName
