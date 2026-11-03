Text and font C reference
=========================

These interfaces are private. See :doc:`text-fonts` for ownership and lifetime.

.. doxygenfunction:: Rbc_GetTextLayout

.. doxygenfunction:: Rbc_GetTextExtents

.. doxygenfunction:: Rbc_InitTextStyle

.. doxygenfunction:: Rbc_ResetTextStyle

.. doxygenfunction:: Rbc_FreeTextStyle

.. doxygenfunction:: Rbc_FirstExportFont

.. doxygenfunction:: Rbc_ExportFontCharacter

.. doxygenfunction:: Rbc_ExportFontsFree

.. doxygenfunction:: Rbc_FontUnicodeProgram

Shared text drawing implementation
----------------------------------

.. c:namespace:: rbcTextDrawing

.. doxygengroup:: text_drawing
   :members:

.. c:namespace-pop::

Native font acquisition and parsing
-----------------------------------

.. doxygengroup:: export_font_internals
   :members:

PDF font resource encoding
---------------------------

.. doxygengroup:: pdf_font_internals
   :members:
