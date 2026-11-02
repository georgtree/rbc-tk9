Graph lifecycle C reference
===========================

These are private interfaces. Shared contracts live at their header declarations;
static lifecycle helpers are documented next to their definitions. This section
covers lifecycle callbacks and option transactions; command and drawing
operations have separate coverage.

Shared entry points
-------------------

.. doxygenfunction:: Rbc_EventuallyRedrawGraph

.. doxygenfunction:: Rbc_LayoutGraph

.. doxygenfunction:: Rbc_GraphCallbackValid

.. doxygenfunction:: Rbc_DrawGraph

Internal lifecycle callbacks
----------------------------

.. doxygengroup:: graph_lifecycle_callbacks
   :members:
   :content-only:

Widget option transactions
--------------------------

.. c:namespace:: rbcGraphOptions

.. doxygengroup:: graph_option_transactions
   :members:
   :content-only:

.. c:namespace:: NULL
