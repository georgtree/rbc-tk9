Binding and picking C reference
===============================

All interfaces here are private. Objects, contexts and tag tokens are borrowed; the owning component supplies their
lifetime. See :doc:`bindings` for event order, picking priority, callback reentry and the distinction between objects
and binding keys.

Binding table state
-------------------

.. doxygenstruct:: Rbc_BindTableStruct
   :members:

Shared interface
----------------

.. doxygentypedef:: Rbc_BindTable

.. doxygentypedef:: Rbc_BindPickProc

.. doxygentypedef:: Rbc_BindTagProc

The picker writes an object context alongside its returned pointer. The tag collector appends stable keys to the
temporary list supplied by dispatch; neither callback acquires ownership of its arguments.

.. doxygenfunction:: Rbc_CreateBindingTable

.. doxygenfunction:: Rbc_DestroyBindingTable

.. doxygenfunction:: Rbc_ConfigureBindingsFromObj

.. doxygenfunction:: Rbc_PickCurrentItem

.. doxygenfunction:: Rbc_DeleteBindings

.. doxygenfunction:: Rbc_MoveBindingTable

Internal dispatch callbacks
---------------------------

.. doxygengroup:: binding_callbacks
   :members:
   :content-only:

Graph-specific callbacks
------------------------

.. doxygengroup:: graph_picking
   :members:
   :content-only:
