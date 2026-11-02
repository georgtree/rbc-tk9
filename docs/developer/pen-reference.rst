Pen and palette C reference
===========================

These private services distinguish counted named-pen references from borrowed
normal-palette entries. See :doc:`pens` for lifetime and selection rules.

Named pen references
--------------------

.. doxygenfunction:: Rbc_GetPen

.. doxygenfunction:: Rbc_FreePen

Graph resource teardown
-----------------------

.. doxygenfunction:: Rbc_ReleasePenTkResources

.. doxygenfunction:: Rbc_DestroyPens

Element palettes
----------------

.. doxygenfunction:: Rbc_FreePalette

.. doxygenfunction:: Rbc_StyleMap

Named pen implementation
-------------------------

The implementation group uses a documentation-only ``rbcGrPen`` scope to
separate file-local handler names (such as DeleteOp) from identical names in
other modules. This does not rename C symbols or introduce a runtime namespace.

.. c:namespace-push:: rbcGrPen

.. doxygengroup:: pen_internals
   :members:

.. c:namespace-pop::

Palette implementation
----------------------

.. doxygengroup:: palette_internals
   :members:
