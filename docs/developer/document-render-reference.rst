Document renderer C reference
==============================

These are private implementation contracts, not installed C APIs. Component
callers use :doc:`reference`; these helpers borrow export state and have
backend-specific preconditions. See :doc:`document-rendering`.

Shared export support
---------------------

.. doxygengroup:: export_backend
   :members:

PDF drawing callbacks
---------------------

.. doxygengroup:: pdf_backend
   :members:

PostScript drawing callbacks
----------------------------

.. doxygengroup:: postscript_backend
   :members:
