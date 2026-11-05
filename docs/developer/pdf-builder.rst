PDF resource builder and serialization
======================================

See :doc:`document-rendering` for shared context ownership and the PDF/PostScript
primitive contracts, including graphics-state and capture boundaries.

``rbcPdf.c`` owns PDF object bodies and final document assembly.
``rbcRenderPdf.c`` emits drawing operators and registers resources;
``rbcPdfFont.c`` resolves document fonts. ``rbcGrPdf.c`` selects scale and invokes
the builder, while the shared export command owns layout, delivery and cleanup.
See :doc:`exports` and :doc:`pdf-builder-reference`.

One export, one builder
-----------------------

After :c:func:`Rbc_ExportInit`, :c:func:`Rbc_PdfBegin` attaches a new ``PdfDocument`` to
``backendData``. It stores scale/height and appends ``q`` plus the initial matrix
to the current content buffer. This maps downward-increasing screen Y to PDF's
page coordinates while converting pixels to physical points. The graph command
calculates scale from Tk's screen dimensions and physical screen width.

Begin neither clears existing content nor releases an earlier builder. Call it
once on a fresh export context. Geometry emitters append content and register
resources during the synchronous graph traversal. All renderer contexts must
finish before finalization and cleanup.

:c:func:`Rbc_PdfDocument` finalizes document fonts, appends the matching ``Q`` and
serializes the page. The result is a new Tcl byte-array object, not Unicode
text. The caller manages its references and either returns it or writes it with
binary channel settings. The builder opens no files and performs no channel
operations.

Finalization is not an idempotent query: it changes the content buffer and can
add or replace resources while finishing fonts. Do not call it twice or retry
it against the same partially finalized context. On failure, discard the export
and release its resources through the normal cleanup path.

Object numbers and resource categories
--------------------------------------

The current writer emits a single-page PDF 1.4 document with fixed leading
objects:

.. list-table::
   :header-rows: 1

   * - Object
     - Contents
   * - 1
     - Catalog
   * - 2
     - Pages tree with one child
   * - 3
     - Page, scaled MediaBox, resource and content references
   * - 4
     - Page resource dictionary
   * - 5
     - Drawing content stream
   * - 6 onward
     - Registered resource bodies in insertion order

:c:func:`Rbc_PdfResource` interns the pair of category and exact body bytes. It copies
a new body's bytes into a resource-owned ``Tcl_DString``; an identical entry
reuses its number. The search is linear and storage capacity grows in blocks
of 32 pointers. IDs remain stable as the pointer array grows. Neither source
pointers nor caller buffers are retained.

Categories ``F``, ``G``, ``I`` and ``P`` populate the page's Font, ExtGState,
XObject and Pattern dictionaries respectively, with names such as ``/F6``.
Other categories are still serialized as objects but are not automatically
listed in those dictionaries. Font support uses ``D`` for indirectly referenced
objects such as font programs, mappings and descriptors.

This is byte interning, not semantic PDF deduplication: differently formatted
but equivalent dictionaries are different resources. Explicit lengths are
required for binary data; negative resource lengths use ``strlen``. Callers
supply valid PDF syntax and valid sizes. The internal resource API is not a
parser for untrusted PDF documents and does not provide recoverable allocation
failure handling or general ID validation.

Reserved resources and streams
------------------------------

Font drawing reserves object IDs before all character codes are known. At
finalization, :c:func:`Rbc_PdfFontsFinish` can append auxiliary objects and replace the
reserved bodies through :c:func:`Rbc_PdfSetResource`. Replacement preserves the ID and
category and does not re-intern the new body. Every reference to an interned ID
sees the replacement; placeholders that must remain independent therefore need
distinct initial bodies. ``SetResource`` assumes a valid existing ID and copies
from nonaliased source storage.

:c:func:`Rbc_PdfStream` assembles dictionary entries, an explicit byte Length, the
stream marker, payload and end marker in a temporary DString. It temporarily
redirects ``token->buffer``, restores the previous pointer, then interns the
complete body and frees temporary storage. Embedded NULs are copied with their
explicit length. It neither compresses nor encodes payload bytes; filter entries
in the supplied dictionary must describe the actual payload.

The main page content stream uses the same byte-count principle, but is emitted
as fixed object 5 during serialization. Content and resources remain separate
until that step. The builder assumes content graphics-state scopes are balanced
apart from the initial save that its final ``Q`` closes.

Serialization and errors
------------------------

Font finalization happens before calculating the total object count. The writer
then redirects output to a temporary document DString, emits the header and
objects, and records each object's byte offset in a ``Tcl_WideInt`` array.
Classic xref includes the free object-zero entry and ten-digit offsets for the
remaining objects. The trailer points to the catalog and ``startxref`` records
the xref position.

An xref position above 9,999,999,999 records an export error instead of emitting
an invalid classic table. This bound is a format check, not a promise that the
builder supports allocations or resource counts approaching that size. The
current implementation does not emit object streams, compressed xref streams,
incremental updates or multiple pages.

A byte-array result is created only when serialization has no recorded export
error. Temporary offsets/document storage are freed and the original content
buffer pointer is restored. Font-finalization failure returns before document
assembly. Internal append/format helpers and resource IDs are not individual
success signals; callers must honor the export error and finalization result.

With identical content and resource insertion order, the writer's object layout
is deterministic. That does not establish byte identity for an entire export
whose upstream font discovery, callbacks or drawing inputs differ.

Cleanup boundaries
------------------

:c:func:`Rbc_PdfFree` releases each resource body, each resource record, the pointer
array and the builder, then clears ``backendData``. Calling it with no builder
is harmless. It does not release document-local font state or content storage;
:c:func:`Rbc_ExportFree` handles those separately. Neither function destroys the
caller's graph or a successfully returned byte-array object.

Future writer changes should preserve binary lengths, stable reserved IDs,
font-finalization order and buffer restoration on every exit. Useful checks
include repeated resource reuse, font placeholder replacement, image payloads
containing NUL bytes, empty drawing content and independent repeated exports.
These are builder concerns; geometry restoration and file-delivery behavior
belong to the shared export orchestration.

Stored geometry queries
------------------------

:c:func:`Rbc_PdfScale` returns the pixel-to-point conversion supplied at builder
creation. :c:func:`Rbc_PdfHeight` returns the saved page height in display pixels.
Neither queries the current graph or Tk window, and neither accepts an absent
builder. These are document metrics, not live widget measurements: resizing or
restoring the graph does not update them. Both require the PDF backend lifetime
to continue until the query returns.
