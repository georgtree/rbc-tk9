Crosshair C reference
=====================

These private helpers borrow graph state. See :doc:`crosshairs` before changing
erase/update/present ordering.

Lifecycle and background changes
--------------------------------

.. doxygenfunction:: Rbc_CreateCrosshairs

.. doxygenfunction:: Rbc_ConfigureCrosshairs

.. doxygenfunction:: Rbc_DestroyCrosshairs

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

.. c:namespace-push:: rbcCrosshairs

.. doxygengroup:: crosshair_internals
   :members:

.. c:namespace-pop::

Windows position-only transition
--------------------------------

This function is extracted from the Windows branch separately. Its documented
scope does not change the compiled symbol or the shared crosshair interfaces.

.. c:namespace-push:: rbcWindowsCrosshairs

.. doxygenfunction:: MoveCrosshairs
   :project: rbc-win

.. c:namespace-pop::
