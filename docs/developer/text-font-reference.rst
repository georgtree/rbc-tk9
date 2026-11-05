Text and font C reference
=========================

These interfaces are private. See :doc:`text-fonts` for ownership and lifetime.

Layout measurement
-------------------

.. doxygenfunction:: Rbc_GetTextLayout

.. doxygenfunction:: Rbc_GetTextExtents

Style initialization and GC ownership
--------------------------------------

.. doxygenfunction:: Rbc_InitTextStyle

.. doxygenfunction:: Rbc_ResetTextStyle

.. doxygenfunction:: Rbc_FreeTextStyle

Document font cache
--------------------

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

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in
the reference does not make private layouts part of the installed C API.

.. doxygenstruct:: ExportCharacter
   :members:

.. doxygenstruct:: ExportFontCache
   :members:

.. doxygenstruct:: FontSource
   :members:

.. doxygenstruct:: FontUnicodePair
   :members:

.. doxygenstruct:: Rbc_ExportFont
   :members:

.. doxygenstruct:: Rbc_FontTable
   :members:

.. doxygenstruct:: Shadow
   :members:

.. doxygenstruct:: TextFragment
   :members:

.. doxygenstruct:: TextLayout
   :members:

.. doxygenstruct:: TextStyle
   :members:
