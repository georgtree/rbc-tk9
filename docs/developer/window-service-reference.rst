Shared drawing and window C reference
=====================================

See :doc:`window-services` for ownership, failure and platform boundaries.
These are private implementation interfaces.

.. doxygenfunction:: Rbc_GetPrivateGC

.. doxygenfunction:: Rbc_GetPrivateGCFromDrawable

.. doxygenfunction:: Rbc_FreePrivateGC

.. doxygenfunction:: Rbc_SetDashes

.. doxygenfunction:: Rbc_MaxRequestSize

.. doxygenfunction:: Rbc_Draw3DRectangle

.. doxygenfunction:: Rbc_GetRealWindowId

.. doxygenfunction:: Rbc_SetWindowInstanceData

.. doxygenfunction:: Rbc_GetWindowInstanceData

.. doxygenfunction:: Rbc_DeleteWindowInstanceData

Window lookup and table cleanup internals
-----------------------------------------

.. c:namespace:: rbcWindowHelpers

.. doxygengroup:: window_helpers
   :members:

.. c:namespace-pop::

The Windows handle lookup is extracted separately in
:doc:`native-windows-reference`.

Native stroke setup
-------------------

.. doxygenfunction:: Rbc_NativeLineWidth

Dash descriptor scanning
------------------------

.. doxygengroup:: shared_draw_helpers
   :members:
