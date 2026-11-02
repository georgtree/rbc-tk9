Scrolling C reference
=====================

These interfaces are private. See :doc:`scrolling` for viewport policy.

.. doxygenfunction:: Rbc_AdjustViewport

.. doxygenfunction:: Rbc_ParseScrollRequest

.. doxygenfunction:: Rbc_GetScrollInfoFromObj

Track-local axis scrolling is documented as
:c:func:`rbcGrAxisLayoutControl.Rbc_TrackYViewOp` in :doc:`axis-reference`.

.. doxygenfunction:: Rbc_NotifyTrackView

Implementation helpers
----------------------

.. doxygengroup:: scroll_internals
   :members:
