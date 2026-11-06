Package initialization C reference
==================================

These are installed interfaces. The two stubs helpers are linked into consumers, not invoked through a table slot. See
:doc:`initialization` for loading order.

.. doxygenfunction:: Rbc_Init

.. doxygenfunction:: Rbc_InitStubs

.. doxygenfunction:: Rbc_VectorInitStubs

Private registration helpers
----------------------------

These records and callbacks belong to the loader implementation, not the installed C API. The consumer-side stubs object
is documented alongside them because it receives the table published by package initialization.

.. c:namespace:: rbcInitialization

.. doxygengroup:: initialization_private
   :members:

.. c:namespace-pop::

Windows loader wrappers
-----------------------

These entry points do not initialize Tcl packages. Their platform-selected contracts come from the separate Windows
Doxygen input.

.. c:namespace:: rbcWindowsInitialization

.. doxygengroup:: initialization_windows
   :project: rbc-win
   :members:

.. c:namespace-pop::
