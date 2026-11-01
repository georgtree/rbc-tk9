EPS canvas item implementation
==============================

``rbcCanvEps.c`` registers a Tk ``eps`` canvas item. It is independent of graph
markers and graph renderer dispatch. Tk owns the item record and invokes its
creation, configuration, drawing, hit-testing, transformation, PostScript and
destruction callbacks through a Tk_ItemType. The item uses legacy Tk_ConfigSpec
and Tk_ConfigureWidget, not the component Tk_OptionTable transaction machinery.

Parsing and retained content
----------------------------

OpenEpsFile owns a binary stdio FILE and calls ReadPostScript. Windows recognizes
the DOS EPS header and records PostScript/preview section offsets and lengths.
The parser seeks to the PostScript section and bounds reads by its nonzero
length. Ordinary EPS input reads to EOF. Failed open/seek/header parsing returns
a Tcl diagnostic; OpenEpsFile closes its stream on parse failure.

ReadPostScript requires a ``%!PS`` signature and a numeric integer BoundingBox
in the header. Bounds and extents must fit int, with positive width and height.
This is a limited metadata parser: it does not implement general PostScript,
trailer ``(atend)`` bounding-box resolution or arbitrary DSC processing.
ReadPsLine has a fixed buffer and may split long physical lines. AppendPsLine
adds a newline when a retained chunk lacks one; retained content is therefore
not guaranteed byte-identical to the source file.

The parser keeps a title, optional EPSI preview and body text in item-owned
storage. It retains the first body line already consumed while scanning the
header. The original file need not stay open for drawing or PostScript output.
Recording TIFF/WMF header fields must not be confused with complete support for
rendering those preview formats.

Preview ownership and configuration
-----------------------------------

.. list-table::
   :header-rows: 1
   :widths: 25 45 30

   * - Resource
     - Meaning
     - Release
   * - preview
     - Tk image instance of the supplied photo
     - Tk_FreeImage
   * - colorImage
     - Owned snapshot or decoded EPSI pixels
     - Rbc_FreeColorImage
   * - tmpImage
     - Private named photo and retained display instance
     - Rbc_DestroyTemporaryImage
   * - dString / title
     - Retained PostScript body / parsed title
     - Tcl_DStringFree / ckfree
   * - psFile
     - Open parser stream
     - CloseEpsFile
   * - fillGC / pixmap
     - Tk GC reference / native pixmap
     - Tk_FreeGC / Tk_FreePixmap

ConfigureEps detects image, file and quick options with prefix matching after
Tk_ConfigureWidget succeeds. It drops old resources and replaces them in steps;
a later error is not a rollback to the previous complete preview. A supplied
image must be a photo. Its pixels are copied rather than continuously used as
a live source. Width/height zero derive dimensions from the preview or EPS
bounding box. Changing quick mode invalidates cached scaled dimensions.

DisplayEps rescales the saved pixels when dimensions change: nearest-style
resize for quick mode, box resampling otherwise. It publishes the result into
tmpImage and frees the intermediate pixels. With no visible preview it draws a
placeholder, optionally followed by a fitting title and relief border. All
screen work uses Tk/native services, without a graph renderer context.

ImageChangedProc does not refresh the saved color snapshot on ordinary source
photo writes. Its deletion branch clears the source name and schedules canvas redraw, but
retains the deleted instance until image reconfiguration or item teardown.
Tk's deletion loop still accesses the instance after the callback returns, so
Tk_FreeImage must not run inside that callback. Keeping the token in the item
allows its ordinary cleanup paths to release it exactly once. Teardown also
detaches tmpImage before its deletion can notify other clients.

Coordinates and export
----------------------

The item stores one canvas anchor plus width and height. ComputeEpsBbox rounds
the anchor and applies Tk anchoring; the redraw bbox uses exclusive right/bottom
edges. Point and area picking use this rectangle, not alpha pixels or EPS paths.
Translate changes the anchor. ScaleEps scales only the anchor about the supplied
origin: it does not scale width or height. Resizing the preview requires those
options to change.

EpsToPostScript is a canvas callback with a no-op prepass. With an EPS file, it
loads ``rbcCanvEps.pro``, establishes translation, scale and clipping, and embeds
the retained body between BeginEPSF/EndEPSF guards. With no file it serializes
the cached temporary photo if present; it does not force a new preview render.
There is no corresponding canvas SVG/PDF callback here. See
:doc:`postscript-output` for shared PsToken services and :doc:`exports` for the
separate graph export pipeline.

Maintenance checks
------------------

Keep parsing, display and export expectations separate. Existing
``RBC.eps.file.A.test``, ``RBC.eps.image.A.test`` and the other ``RBC.eps.*``
files cover canvas options and lifecycle cases. Review malformed headers,
file/image replacement, quick-mode cache invalidation, source-image deletion,
anchor picking and PostScript body preservation when changing these paths.
Use :doc:`image-compatibility` for temporary-photo acquisition and release.

The optional ``make test-resources`` harness counts source-instance releases
on deletion followed by reconfiguration or teardown, without relying on process
memory measurements. It runs separately from the ordinary suite.
