Cairo backend C reference
=========================

These static helpers belong to ``rbcRenderCairo.c``. They are not component APIs.
The documentation preprocessor enables ``RBC_HAVE_CAIRO`` to extract this group;
it does not change the configured library build or require Cairo to build docs.
See :doc:`cairo-rendering` for platform-specific target lifetime, path consumption
and pattern ownership. The parameter contracts describe backend state, not
permission to call a helper on an arbitrary renderer context. In particular,
fill callbacks require initialized fill style, and cleanup consumes its context.

Start with :c:func:`BeginRenderTarget` for setup,
:c:func:`StrokeRenderPath` for stroke composition, and
:c:func:`CreateRenderPhoto` for copied pixel storage. Component code should use
the interface in :doc:`reference` instead of calling these static functions.

.. doxygengroup:: cairo_backend
   :members:
