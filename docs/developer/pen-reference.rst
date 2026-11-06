Pen and palette C reference
===========================

These private services distinguish counted named-pen references from borrowed normal-palette entries. See :doc:`pens`
for lifetime and selection rules.

Pen ownership and palette records
---------------------------------

.. doxygentypedef:: Pen

.. doxygenstruct:: PenStruct
   :members:

.. doxygenstruct:: Weight
   :members:

.. doxygenstruct:: PenStyle
   :members:

Pen lifecycle callback types
----------------------------

.. doxygentypedef:: PenCreateProc

.. doxygentypedef:: PenConfigureProc

.. doxygentypedef:: PenDestroyProc

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

The implementation group uses a documentation-only ``rbcGrPen`` scope to separate file-local handler names (such as
DeleteOp) from identical names in other modules. This does not rename C symbols or introduce a runtime namespace.

.. c:namespace-push:: rbcGrPen

.. doxygengroup:: pen_internals
   :members:

.. c:namespace-pop::

Palette implementation
----------------------

.. doxygengroup:: palette_internals
   :members:

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: PenOpSpec
   :members:
