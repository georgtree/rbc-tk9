Graph lifecycle C reference
===========================

These are private interfaces. Shared contracts live at their header declarations; static lifecycle helpers are
documented next to their definitions. This section covers lifecycle callbacks, option transactions, command routing and
screen composition helpers.

Widget ownership and layout records
-----------------------------------

.. doxygentypedef:: Graph

.. doxygenstruct:: GraphStruct
   :members:

.. doxygenstruct:: Component
   :members:

.. doxygenstruct:: Margin
   :members:

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

Commands and screen composition
-------------------------------

.. c:namespace:: rbcGraphCommands

.. doxygengroup:: graph_commands_drawing
   :members:
   :content-only:

.. c:namespace:: NULL

Construction and redraw support
-------------------------------

.. c:namespace:: rbcGraphLifecycle

.. doxygengroup:: graph_lifecycle_support
   :members:
   :content-only:

.. doxygenstruct:: GraphRedrawSettle
   :members:

.. c:namespace:: NULL

Windows metafile packaging
--------------------------

These private functions use the Windows extraction project. Their scope below is documentation-only and does not change
the compiled symbols.

.. c:namespace-push:: rbcWindowsSnapshot

.. doxygengroup:: graph_snapshot_windows
   :project: rbc-win
   :members:

.. c:namespace-pop::

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: GraphBarModeTransaction
   :members:

.. doxygenstruct:: GraphOpSpec
   :members:

.. doxygenstruct:: GraphOptionName
   :members:

.. doxygenstruct:: GraphPaddingTransaction
   :members:

.. doxygenstruct:: GraphPixelTransaction
   :members:

.. doxygenstruct:: GraphShadowTransaction
   :members:

.. doxygenstruct:: GraphTileTransaction
   :members:

.. doxygenstruct:: SnapData
   :members:
