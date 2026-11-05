SVG backend C reference
=======================

These static helpers belong to ``rbcRenderSvg.c``. They borrow the export token;
component callers use the renderer interface. See :doc:`svg-output`.

Use :c:func:`SvgStrokeAttributes` and :c:func:`SvgFillPattern` for paint-state
contracts, :c:func:`SvgString` for text escaping, and :c:func:`SvgPhotoBlock`
for raster encoding. PNG helpers count bytes; geometry helpers count vertices,
segments or rectangles. Emission is append-only and document errors remain
separate from a helper's return value.

.. doxygengroup:: svg_backend
   :members:
