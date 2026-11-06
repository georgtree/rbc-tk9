Export orchestration and document ownership
===========================================

The private export layer has two jobs: orchestrate a graph-sized operation and provide document state to renderer
contexts. ``rbcGrExport.c`` owns the shared traversal and SVG/PDF command services; ``rbcGrSvg.c``, ``rbcGrPdf.c`` and
``rbcGrPs.c`` own format policy and document framing. Backend primitives live in the renderer modules described in
:doc:`rendering`.

These operations run synchronously on the Tk thread. Neither an export token nor a renderer context preserves a graph or
its window. The enclosing widget command retains lifetime protection, and layout/traversal must respect callback
invalidation as described in :doc:`graph-lifecycle`.

Document state
--------------

:c:struct:`Rbc_ExportContext` in ``rbcExport.h`` is caller-owned, normally stack storage. Initialization zeroes the
record and selects its embedded ``storage`` DString as ``buffer``. The interpreter and window are borrowed. The context
is private and does not extend the installed C API.

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - Resource
     - Ownership and release
   * - ``storage``
     - Embedded DString, released by :c:func:`Rbc_ExportFree`.
   * - ``buffer``
     - Append destination; normally aliases storage. PostScript redirects it
       to its separately owned token DString. Shared cleanup does not free an
       alternate buffer.
   * - ``backendData``
     - Format-owned state. PDF releases it through :c:func:`Rbc_PdfFree`; PostScript
       releases its token in its command cleanup.
   * - ``fontData``
     - Document-local font cache, released by shared export cleanup.
   * - ``error``
     - Borrowed static message. :c:func:`Rbc_ExportError` retains the first failure;
       it does not itself set the interpreter result.

Append and formatting helpers write to the current buffer. They do not stop simply because an earlier export error
exists. Traversal and finalization must check errors before publishing the document. End all borrowing renderer contexts
before releasing document resources.

SVG/PDF command transaction
---------------------------

:c:struct:`Rbc_GraphExportFormat <rbcGraphExportCommands.Rbc_GraphExportFormat>` in ``rbcGraph.h`` supplies the
component name/class, integer option offsets, backend, channel
policy and two lifecycle callbacks. Its option specifications contain concrete pixel and boolean fields only.
Initialization uses a temporary Tk option table and retains no Tk resources from that table. Runtime parsing stages all
option values before committing; an invalid option leaves the previous configuration intact. Repeated valid options use
the last value, but an invalid earlier occurrence still fails.

Successful ``output`` options remain configured even if rendering or delivery subsequently fails. Geometry restoration
does not reset those options. The shared output helper performs this sequence:

1. Save graph dimensions, initialize the document context and enter export mode.
2. Apply positive configured dimensions, request layout/mapping and lay out.
3. Invoke ``documentProc`` only after successful layout. It returns a new,
   unreferenced Tcl object on success. The shared helper takes a reference.
4. End export mode, restore the saved dimensions and request layout/mapping.
   Remap immediately only if the window survives and axis formatting has not
   invalidated the graph. Preserve an earlier error across this restoration;
   if export succeeded, a restoration failure becomes the command failure.
5. Return or write the complete document only on success. Release the object,
   call optional ``freeProc`` and release shared context storage on every path.

``documentProc`` owns document framing and format finalization, not screen restoration or context cleanup. ``freeProc``
releases backend-owned state only and must accept an empty context, since initial layout can fail before the backend is
initialized. SVG finishes embedded font definitions before creating its string object. PDF initializes its resource
writer, traverses the graph, then serializes the complete PDF object. Neither exporter requires Cairo.

Format policies and extension constraints
-----------------------------------------

The built-in policies deliberately share a small configuration model:

.. list-table::
   :header-rows: 1
   :widths: 25 35 40

   * - Property
     - SVG
     - PDF
   * - Resource name/class
     - ``svg`` / ``Svg``
     - ``pdf`` / ``Pdf``
   * - Width and height defaults
     - Zero: use current graph dimensions
     - Zero: use current graph dimensions
   * - Decorations default
     - Enabled
     - Enabled
   * - Font embedding default
     - Disabled
     - Enabled
   * - Document object
     - Tcl string containing UTF-8-declared XML
     - Tcl byte array containing serialized PDF
   * - File channel policy
     - UTF-8 encoding, LF translation
     - ISO-8859-1 encoding, binary translation
   * - Backend cleanup callback
     - None; shared font/storage cleanup suffices
     - :c:func:`Rbc_PdfFree` before shared cleanup

The policy's field offsets address native ``int`` members in :c:type:`Graph`. They are trusted private metadata, not
checked offsets supplied by a script. The shared parser supports concrete pixel and boolean specifications only; adding
a string, synonym or resource-owning option requires extending that parser and its ownership model. An unsupported
specification type reaches Tcl_Panic. Integer query objects report normalized values rather than the original input
spelling. Initialization may write fields before failing; it belongs to graph construction, unlike the rollback
guarantee of runtime configuration.

:c:func:`rbcGraphExportCommands.SvgDocument` frames an XML document with pixel dimensions and an equal viewBox. After
shared traversal succeeds, it finalizes font definitions, closes the SVG and copies the buffer into a new Tcl
string. The closing tag is appended even on failure, but no partial document is
returned. :c:func:`rbcGraphExportCommands.PdfDocument` initializes the PDF builder using horizontal screen
pixels/millimeters to obtain points per pixel, traverses the graph and invokes the binary serializer only on success.
Neither adapter opens files or restores screen geometry.

:c:func:`rbcGraphExportCommands.OutputGraphExport` acquires a reference to a document returned by the callback and
releases it after delivery or failure. A callback must return a non-NULL new object on success; cleanup accepts NULL on
failure. Backend cleanup still runs when initial layout fails before the document callback is entered. A new format must
therefore separate backend teardown from shared font and buffer cleanup, and must tolerate that partially initialized
state.

Graph traversal and geometry
----------------------------

:c:func:`Rbc_ExportGraph` assumes export geometry is already mapped. It opens the plot output context and emits the plot
background, optional track separators, grids and polar/Smith labels. It then validates callback state before continuing
with under-markers, unraised local/global legends, axis limits, elements, raised local/global legends, over-markers and
active elements. Finally it ends the plot region and emits margins, margin legend, title and axes.

Track grids are dispatched even when the global grid is hidden; the track services decide effective visibility. Track
components own their scopes and clipping. Crosshairs are screen-only. See :doc:`markers`, :doc:`grids` and
:doc:`legends` for component-specific output rules.

:c:func:`Rbc_ExportBeginGraph` supplies requested Tk dimensions when graph dimensions are not yet usable and sets
``GRAPH_EXPORT``. It does not save geometry or perform layout. :c:func:`Rbc_ExportEndGraph` clears the flag; for a
surviving window it adopts actual Tk dimensions, replaces graph flags with ``MAP_WORLD`` and schedules redraw. It is not
a general state-stack operation and is not nestable. See :doc:`mapping` for immediate screen restoration in SVG/PDF
versus deferred screen remapping in PostScript.

PostScript policy and delivery
------------------------------

PostScript retains its own option transaction and output command because it also handles page setup, printer mappings
and optional preview formats. :c:func:`rbcGraphPostscript.GraphToPostScript` borrows a PostScript token, redirects the
export buffer to that token, enters export mode, prepares the page and traverses components. It appends the trailer on
success, ends export mode and frees shared state. The outer command releases the PostScript token. Unlike SVG/PDF, this
path does not perform the SVG/PDF restoration transaction. Its optional Windows metafile preview does explicitly lay out
and draw at screen dimensions.

All three formats finish document generation before opening a filename for writing. SVG uses UTF-8/LF channel settings;
PDF uses ISO-8859-1/binary for its byte-valued document object. PostScript writes bytes with binary translation,
including the optional Windows DOS EPS preview wrapper. An output without a filename returns the document instead.

This ordering protects an existing file against rendering failure, but it is not atomic file replacement: opening with
``w`` can truncate the destination, and later writing or closing can fail. A prior write/channel error is preserved
across cleanup; otherwise a close error is reported. Successful file output clears the interpreter result. PostScript
metadata can vary between exports, so byte equality is not a universal test for geometry restoration.

See :doc:`export-reference` for the shared C contracts. See :doc:`pdf-builder` for PDF resource ownership and
serialization, and :doc:`text-fonts` for font ownership.

See :doc:`svg-output` for SVG definitions, escaped text and inline image encoding.

Parameter slices and failure boundaries
---------------------------------------

:c:func:`Rbc_GraphExportOp` receives the full widget command, with the operation at index two. Its private parser
:c:func:`rbcGraphExportCommands.SetExportOptions` receives only the option/value slice, starting at index zero. Passing
the full command to that parser would interpret command words as options. The parser stages integer values before
committing; :c:func:`Rbc_InitGraphExportOptions` instead initializes a new graph and does not promise rollback on
failure.

The document callback output is also deliberately narrower than the command
result. :c:func:`rbcGraphExportCommands.SvgDocument` leaves its output pointer unchanged on
failure. :c:func:`rbcGraphExportCommands.PdfDocument` leaves it unchanged when traversal fails, but writes NULL when
serialization fails. :c:func:`rbcGraphExportCommands.OutputGraphExport` initializes that pointer to NULL, takes a
reference when a document is returned, and releases it after result delivery or failure. New callbacks must follow this
ownership contract.

:c:func:`Rbc_ExportAppend` takes C strings terminated by a pointer-valued NULL sentinel, not a byte
count. :c:func:`Rbc_ExportFormat` takes printf arguments. Both append to the context's current buffer, which may differ
from its embedded storage; neither suppresses output because an earlier error was recorded. :c:func:`Rbc_ExportError`
records the first static error string without setting the Tcl result. Final traversal or serialization must turn that
failure into a command error before delivery.

:c:func:`rbcGraphExportCommands.WriteExportDocument` only receives a complete document. Its success means writing and
closing succeeded, not that replacement was atomic. Keeping the Tcl object alive through channel operations is the
caller's responsibility; the helper neither takes ownership of the document nor retains the filename after returning.
