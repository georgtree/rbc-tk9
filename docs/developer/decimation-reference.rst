Line decimation C reference
===========================

These static helpers belong to ``rbcGrLine.c``. They are not cross-module APIs.
See :doc:`decimation` for fallback order and source-index ownership.

.. doxygengroup:: line_decimation
   :members:

Source summaries and bucket searches
------------------------------------

These helpers distinguish owned block storage from borrowed source arrays,
cache validity from log-domain eligibility, and partial append results from
complete mappings. The namespace is documentation-only.

.. c:namespace-push:: rbcGrLineCache

.. doxygengroup:: line_cache_internals
   :members:

.. c:namespace-pop::
