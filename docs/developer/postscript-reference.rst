PostScript token C reference
============================

These private services are declared in ``rbcPs.h`` and implemented in
``rbcPs.c``. See :doc:`postscript-output` for ownership and prolog requirements.

Persistent graph PostScript configuration
-----------------------------------------

.. doxygenstruct:: PostScript
   :members:

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
``rbcGrPs.c``. The parameter lists distinguish full command vectors from option slices and
borrowed output buffers from owned component state. Windows binary preview
packaging has its source-level contract on :c:func:`CreateWindowsEPS <rbcWindowsPostscript.CreateWindowsEPS>` and its
platform behavior described in :doc:`postscript-output`.

.. c:namespace:: rbcGraphPostscript

.. doxygengroup:: graph_postscript
   :members:

.. c:namespace-pop::

Windows binary preview packaging
--------------------------------

The Windows-only helper is extracted in the separate platform project. It
shares the graph command's lifetime protection but owns its temporary native
resources. See :doc:`postscript-output` for the EPSI/WMF distinction.

.. c:namespace:: rbcWindowsPostscript

.. doxygengroup:: graph_postscript_windows
   :project: rbc-win
   :members:

.. c:namespace-pop::
