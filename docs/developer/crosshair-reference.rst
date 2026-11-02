Crosshair C reference
=====================

These private helpers borrow graph state. See :doc:`crosshairs` before changing
erase/update/present ordering.

Direct overlay control
----------------------

.. doxygenfunction:: Rbc_EnableCrosshairs

.. doxygenfunction:: Rbc_DisableCrosshairs

.. doxygenfunction:: Rbc_UpdateCrosshairs

Buffered presentation
---------------------

.. doxygenfunction:: Rbc_CrosshairsNeedFullRedraw

.. doxygenfunction:: Rbc_PresentGraphWithCrosshairs

Implementation helpers
----------------------

.. doxygengroup:: crosshair_internals
   :members:
