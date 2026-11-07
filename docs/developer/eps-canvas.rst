EPS canvas item implementation
==============================

``rbcCanvEps.c`` registers a Tk ``eps`` canvas item. It is independent of graph markers and graph renderer dispatch. Tk
owns the item record and invokes its creation, configuration, drawing, hit-testing, transformation, PostScript and
destruction callbacks through a ``Tk_ItemType``. The item uses legacy ``Tk_ConfigSpec`` and ``Tk_ConfigureWidget``, not the
component ``Tk_OptionTable`` transaction machinery.

Parsing and retained content
----------------------------

:c:func:`OpenEpsFile` owns a binary stdio ``FILE`` and calls :c:func:`ReadPostScript`. Windows recognizes the DOS EPS
header and records PostScript/preview section offsets and lengths. The parser seeks to the PostScript section and bounds
reads by its nonzero length. Ordinary EPS input reads to EOF. Failed open/seek/header parsing returns a Tcl diagnostic;
:c:func:`OpenEpsFile` closes its stream on parse failure.

:c:func:`ReadPostScript` requires a ``%!PS`` signature and a numeric integer ``BoundingBox`` in the header. Bounds and
extents must fit int, with positive width and height. This is a limited metadata parser: it does not implement general
PostScript, trailer ``(atend)`` bounding-box resolution or arbitrary DSC processing. :c:func:`ReadPsLine` has a fixed
buffer and may split long physical lines. :c:func:`AppendPsLine` adds a newline when a retained chunk lacks one;
retained content is therefore not guaranteed byte-identical to the source file.

The parser keeps a title, optional EPSI preview and body text in item-owned storage. It retains the first body line
already consumed while scanning the header. The original file need not stay open for drawing or PostScript output.
Recording TIFF/WMF header fields must not be confused with complete support for rendering those preview formats.

Preview ownership and configuration
-----------------------------------

.. list-table::
   :header-rows: 1
   :widths: 25 45 30

   * - Resource
     - Meaning
     - Release
   * - ``preview``
     - Tk image instance of the supplied photo
     - ``Tk_FreeImage``
   * - colorImage
     - Owned snapshot or decoded EPSI pixels
     - :c:func:`Rbc_FreeColorImage`
   * - ``tmpImage``
     - Private named photo and retained display instance
     - :c:func:`Rbc_DestroyTemporaryImage`
   * - ``dString`` / ``title``
     - Retained PostScript body / parsed title
     - ``Tcl_DStringFree`` / ``ckfree``
   * - ``psFile``
     - Open parser stream
     - :c:func:`CloseEpsFile`
   * - ``fillGC`` / ``pixmap``
     - Tk GC reference / native pixmap
     - ``Tk_FreeGC`` / ``Tk_FreePixmap``

:c:func:`ConfigureEps` detects image, file and quick options with prefix matching after ``Tk_ConfigureWidget``
succeeds. It drops old resources and replaces them in steps; a later error is not a rollback to the previous complete
preview. A supplied image must be a photo. Its pixels are copied rather than continuously used as a live
source. Width/height zero derive dimensions from the preview or EPS bounding box. Changing quick mode invalidates cached
scaled dimensions.

:c:func:`DisplayEps` rescales the saved pixels when dimensions change: nearest-style resize for quick mode, box
resampling otherwise. It publishes the result into ``tmpImage`` and frees the intermediate pixels. With no visible
preview it draws a placeholder, optionally followed by a fitting title and relief border. All screen work uses Tk/native
services, without a graph renderer context.

:c:func:`ImageChangedProc` does not refresh the saved color snapshot on ordinary source photo writes. Its deletion
branch clears the source name and schedules canvas redraw, but retains the deleted instance until image reconfiguration
or item teardown. Tk's deletion loop still accesses the instance after the callback returns, so ``Tk_FreeImage`` must
not run inside that callback. Keeping the token in the item allows its ordinary cleanup paths to release it exactly
once. Teardown also detaches ``tmpImage`` before its deletion can notify other clients.

Coordinates and export
----------------------

The item stores one canvas anchor plus width and height. :c:func:`ComputeEpsBbox` rounds the anchor and applies Tk
anchoring; the redraw bbox uses exclusive right/bottom edges. Point and area picking use this rectangle, not alpha
pixels or EPS paths. Translate changes the anchor. :c:func:`ScaleEps` scales only the anchor about the supplied origin:
it does not scale width or height. Resizing the preview requires those options to change.

:c:func:`EpsToPostScript` is a canvas callback with a no-op prepass. With an EPS file, it loads ``rbcCanvEps.pro``,
establishes translation, scale and clipping, and embeds the retained body between BeginEPSF/EndEPSF guards. With no file
it serializes the cached temporary photo if present; it does not force a new preview render. There is no corresponding
canvas SVG/PDF callback here. See :doc:`postscript-output` for the shared :c:struct:`PostScript token <PsTokenStruct>`
services and :doc:`exports` for the separate graph export pipeline.

Maintenance checks
------------------

Keep parsing, display and export expectations separate. Existing ``eps.file.test``, ``eps.image.test`` and
the other ``eps.*`` files cover canvas options and lifecycle cases. Review malformed headers, file/image
replacement, quick-mode cache invalidation, source-image deletion, anchor picking and PostScript body preservation when
changing these paths. Use :doc:`image-compatibility` for temporary-photo acquisition and release.

The ``make test-resources`` harness (also run by ``make test``) counts source-instance releases on deletion followed by reconfiguration or
teardown, without relying on process memory measurements. It runs separately from the ordinary suite.

Preview-byte decoding details
-----------------------------

:c:func:`GetHexValue` reads from the parser's current cursor, requesting a new bounded chunk when necessary. Each new
preview chunk must begin with ``%``. Whitespace is skipped before each byte pair, but the two hexadecimal digits must
occur in the same chunk. A matching ``%%EndPreview`` prefix returns ``TCL_RETURN``; malformed text or premature EOF
returns ``TCL_ERROR``. Only ``TCL_OK`` supplies a decoded byte. These statuses are internal parser control, not Tcl
script return codes propagated to the canvas command.

:c:func:`ReadEPSI` accepts dimensions from 1 through ``SHRT_MAX`` and depths of 1 or 8 bits. The declared number of
preview lines is parsed but not enforced. Decoding consumes the bytes needed for the dimensions; it does not separately
require an end marker after the final pixel. A premature marker or malformed byte abandons the preview and frees its
image. The stream is not rewound and preview failure does not itself become a Tcl error.

The two legacy decode branches have different pixel conventions. Eight-bit samples are inverted to grayscale, assigned
opaque alpha and written from the bottom row upward. One-bit rows are written from the top down, consume the high bit
first and discard unused bits at each row boundary. A zero bit becomes opaque white; a set bit leaves the
zero-initialized RGBA pixel, including zero alpha. This describes the current implementation, rather than a general
bitmap conversion contract. Changes to preview appearance must test both depths, row orientation, padding and alpha
explicitly.

The conditional TIFF reader is unfinished: it calls ``TIFFReadRGBAImage`` but leaves component reordering as a
placeholder. It is not called by the ordinary EPSI parsing path. Recording TIFF section offsets in a DOS EPS header does
not provide a working TIFF preview pipeline.

Small callback and geometry contracts
-------------------------------------

:c:func:`EpsOptionSpecified` is a post-configuration detector, not an option parser. It scans option-value pairs and
accepts option-name prefixes only after Tk's configuration call succeeds. It does not independently enforce unique
prefixes or reject malformed argument lists.

The supplied-image change callback ignores ordinary pixel updates because rendering uses the retained snapshot. On image
deletion it clears the source name and requests redraw while retaining the Tk instance until ordinary cleanup. Neither
that callback nor translation recreates the snapshot.

Point and area hit tests intentionally use different boundary comparisons. Point distance is zero on every rectangle
edge. An area touching only an edge is classified as outside; a query enclosing the full item is inside; remaining
intersections overlap. Both use the cached rectangle regardless of transparency. Translation changes the anchor and
recomputes that rectangle without resizing or resampling pixels. Tk's canvas operation owns the surrounding redraw work.

Canvas callback argument contracts
----------------------------------

:c:func:`Rbc_InitEpsCanvasItem` registers the static item type; it does not create an item. :c:func:`CreateEps`
initializes a canvas-owned record whose Tk header already exists. :c:func:`ConfigureEps` applies legacy options
incrementally, whereas :c:func:`EpsCoords` parses both new coordinates before updating the anchor and bbox.

:c:func:`DisplayEps` may rebuild preview caches but borrows the canvas drawable. Its redisplay-rectangle arguments are
not used by the implementation. :c:func:`DeleteEps` releases initialized resources through their matching APIs; the
canvas retains ownership of the item record itself.
