Line decimation C reference
===========================

These static helpers belong to ``rbcGrLine.c``. They are not cross-module APIs.  See :doc:`decimation` for fallback
order and source-index ownership.

.. doxygengroup:: line_decimation
   :members:

Source summaries and bucket searches
------------------------------------

These helpers distinguish owned block storage from borrowed source arrays, cache validity from log-domain eligibility,
and partial append results from complete mappings. The namespace is documentation-only.

.. c:namespace-push:: rbcGrLineCache

.. doxygengroup:: line_cache_internals
   :members:

.. c:namespace-pop::

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: LineDecimateBlock
   :members:

.. doxygenstruct:: LineDecimateCache
   :members:
