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
