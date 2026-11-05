PostScript token C reference
============================

These private services are declared in ``rbcPs.h`` and implemented in
``rbcPs.c``. See :doc:`postscript-output` for ownership and prolog requirements.

Token allocation and borrowed buffers
-------------------------------------

.. doxygengroup:: ps_token_lifetime
   :members:

Appending syntax and loading prologs
------------------------------------

.. doxygengroup:: ps_token_output
   :members:

Legacy drawing primitives
-------------------------

.. c:namespace:: rbcPsPrimitives

.. doxygengroup:: ps_legacy_primitives
   :members:

.. c:namespace-pop::

Raster payloads and capture
---------------------------

The bitmap entry below describes the X11 implementation. The Windows source
has a separate contract for its DIB acquisition and failure path; see the
platform comparison in :doc:`postscript-output`.

.. c:namespace:: rbcPsRaster

.. doxygengroup:: ps_raster_encoding
   :members:

.. c:namespace-pop::

Font mapping and text placement
-------------------------------

The X11 property helpers below supplement the shared font-selection path;
Windows goes directly to the final fallback when family recognition fails.

.. c:namespace:: rbcPsText

.. doxygengroup:: ps_text_mapping
   :members:

.. c:namespace-pop::

Graph options and document assembly
-----------------------------------

These contracts describe the shared graph-facing command layer in
``rbcGrPs.c``. Windows binary preview packaging is separate from this group.

.. c:namespace:: rbcGraphPostscript

.. doxygengroup:: graph_postscript
   :members:

.. c:namespace-pop::
