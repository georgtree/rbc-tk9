Text layout and document fonts
==============================

Text has three distinct resource layers: component-owned Tk options, temporary
layout/style views, and document-owned export fonts. ``rbcText.c`` measures and
draws multiline text; ``rbcFont.c`` resolves native outline programs for SVG/PDF.
Neither layer transfers ownership of a component's Tk font to a renderer.
See :doc:`rendering` for rendering contexts and :doc:`exports` for document
finalization and cleanup.

Layouts borrow the text
-----------------------

``Rbc_GetTextLayout`` returns one allocation containing a ``TextLayout`` and its
fragment array. Free it with ``ckfree``. Each fragment points into the original
string, which must remain alive and unchanged while the layout is used. Fragment
counts are UTF byte counts; glyph processing must decode characters rather than
index bytes as characters.

Newlines terminate fragments, including empty lines. A trailing newline does
not create an additional final empty fragment. NULL is treated as an empty
string. Tk metrics and ``Tk_TextWidth`` supply line dimensions; padding, shadow
offset, justification and interline leader determine the unrotated box and
fragment baselines. Anchoring and rotation are applied later. Large dimensions
are bounded to integer geometry, while impossible fragment/count allocations
are fatal rather than Tcl command errors.

``Rbc_GetTextExtents`` is a separate measurement path, not an allocation-free
alias for the layout algorithm. NULL produces zero extents regardless of
padding. For nonempty multiline text it retains the final leader spacing,
whereas the layout builder removes that final leader. Callers must choose the
appropriate measurement rather than assume the two are interchangeable.

Styles are not universal owners
-------------------------------

``TextStyle`` groups font, colors, border, GC and geometry settings. Merely
copying or passing it does not acquire Tk references. Components usually own
font/color options; temporary styles borrow them.

``Rbc_InitTextStyle`` sets selected defaults, but does not initialize ``gc``,
``border``, ``width`` or ``height``. Callers must initialize any such fields they
use. ``Rbc_SetDrawTextStyle`` installs the supplied GC without acquiring it;
this does not grant permission to release a caller-owned GC.

For an owning style, ``Rbc_ResetTextStyle`` obtains a Tk GC from the current font
and optional foreground, then releases the previous GC. Its old GC must be
NULL or a valid owned Tk GC. ``Rbc_FreeTextStyle`` releases only that GC; it does
not free font, colors, border or the style itself, and does not clear the GC
field. Repeated cleanup therefore requires an explicit reset. These are existing
helper contracts, not an automatic resource-management convention.

Native layout presentation
--------------------------

``Rbc_DrawTextLayout`` consumes the existing unrotated layout. It normalizes
``theta`` in the style, computes and rounds the rotated bounding dimensions,
then translates the requested anchor to that box's upper-left corner. The
private drawing loop places each fragment baseline relative to this origin.
For zero rotation it uses ``Tk_DrawChars``. For other angles it rotates the
baseline around the layout center and calls Tk's private ``TkDrawAngledChars``.
It does not create, rotate or stipple a bitmap of the text.

Angles must be finite and bounded before entering this layer: the historical
``FMOD`` macro converts a quotient to ``int``. The later finite checks on
coordinates do not make arbitrary angles safe. Direct callers of
``Rbc_GetBoundingBox`` should supply normalized angles in [0, 360); its exact
quadrant switch assumes that convention. Corner outputs preserve the original
NW, NE, SE, SW indexing, centered at the origin, rather than sorting the rotated
corners by their final position.

Integer anchor translation uses integer half-width/half-height and saturates
wide intermediate subtraction to ``int``. ``Rbc_TranslatePoint`` instead keeps
fractional half-pixel offsets. Layout sizes clamp negative results to zero and
large values to ``INT_MAX``; floating drawing coordinates also handle nonfinite
values before rounding. These policies bound representation, not visibility:
clipping remains the drawing context's responsibility.

State selects the drawing passes in this order:

.. list-table:: Native text passes
   :header-rows: 1
   :widths: 25 75

   * - State
     - Drawing behavior
   * - Disabled or emphasis
     - Two border-colored passes, offset by one pixel; emphasis swaps colors.
       Shadow and active-color paths are bypassed.
   * - Normal or active
     - A positive-offset, non-NULL-color shadow is drawn first, if configured;
       then the main pass uses the current foreground or active color.

Color-changing paths reset the GC foreground to the style's normal color,
not to the original GC value. The normal path without a shadow does not
explicitly select the normal color, so the supplied GC must already have the
intended foreground. Callers must supply any colors or border required by the
selected state and permit the temporary GC mutation. ``Rbc_SetPrintTextStyle``
does not initialize a GC and is not sufficient preparation for native drawing.

``Rbc_DrawText`` and ``Rbc_DrawText2`` build and release temporary layouts while
borrowing the input string. Both skip NULL and empty strings; the latter also
zeros a supplied result area on that path. Its nonempty result is the rounded
rotated layout size, independent of the anchor position. Exporters retain their
own text serialization paths; these native draw calls do not embed fonts.

Bitmap GC lifetime
------------------

``Rbc_GetBitmapGC`` supplies a different resource: a shared mutable depth-one GC
cached by Display pointer in a process-static hash table. Its creation bitmap
is temporary, but the GC is retained. Subsequent requests do not reset its
foreground, background or other attributes. Callers borrow it, establish their
required state and must not free it through text-style cleanup. The cache has
no teardown or synchronization in this module; it should not be treated as a
per-interpreter or thread-isolated resource owner.

Document-local font resolution
------------------------------

An export context lazily owns an ``ExportFontCache`` through ``fontData``.
The cache has two levels:

* A character lookup keyed by the borrowed ``Tk_Font`` identity and Unicode
  scalar, retaining the resolved font, character ID and pixel size.
* Font program records shared by family and physical bold/italic style, when
  the candidate program contains the requested glyph. Different sizes can share
  one program. Each record owns bytes, character/glyph arrays and lookup state.

``Rbc_ExportFontCharacter`` first rejects invalid scalars and pre-existing export
errors. On a cache miss it invokes Tk 9's global ``font actual`` command with
``-displayof`` and the character, so fallback selection is character-specific.
The query saves/restores interpreter state and retains its result only while
extracting attributes. Positive point sizes are converted to screen pixels;
negative Tk sizes already denote pixels.

This Tcl evaluation is a callback boundary, not a pure font-table read. The
helper itself does not preserve a graph. Callers must retain the surrounding
export lifetime and callback-validity discipline; saving interpreter state alone
does not make arbitrary reentry safe.

A successful lookup returns a borrowed ``Rbc_ExportFont`` and a one-based
character ID. That ID is independent of the native glyph index, allowing two
Unicode characters sharing an outline to retain distinct extraction mappings.
The returned advance is computed from horizontal font metrics at the resolved
pixel size. Outputs must not be consumed after failure.

Native sources and font programs
--------------------------------

On Windows, font acquisition creates a temporary GDI font and DC and reads
sfnt tables using ``GetFontData``. On X11, Fontconfig selects a scalable
TrueType/CFF candidate with the character; Tcl filesystem channels read its
font file. This may use a scalable substitute for a legacy core-X11 bitmap or
Type1 face. Collection face selection is validated; unsupported variable-font
instances fail rather than silently selecting another instance.

Temporary source resources are released after constructing the program or on
failure. The program builder creates a standalone sfnt, removes stale
signatures and recomputes checksums. Table views borrow ranges within the owned
program bytes; they must not be individually freed or retained after cache
cleanup. Font validation checks required tables, supported formats and embedding
permissions. Embedding retains outlines rather than performing glyph subsetting.

Backend integration and cleanup
-------------------------------

SVG's embedded-font path retains Unicode text and uses
``Rbc_FontUnicodeProgram`` to rebuild a cmap for the used characters, including
legacy Symbol mappings. It appends to a caller-initialized DString without
changing the cached font. Finalization generates font definitions and
content-derived family names to avoid collisions when separate SVG documents
are included in one page. The nonembedded SVG path uses viewer font selection
instead of these embedded programs.

PDF uses its own font-resource and character-code writer around the shared font
cache. Both exporters start from Tk text layout, but exported fallback glyphs
can require an advance where core X11 reported zero width; backend text code
adjusts such layout rather than assuming all screen and document glyph metrics
are identical. Font embedding policy and final document serialization remain
backend responsibilities. PostScript retains its separate font mapping/token
path and does not use this cache for embedded outlines.

``Rbc_FirstExportFont`` returns a borrowed list head. ``Rbc_ExportFontsFree``
releases cache entries, programs, character arrays and optional Symbol encodings,
then clears ``fontData``. An empty cache is safe to free. Every returned font
pointer and table view becomes invalid, but borrowed Tk fonts are not released.
Partial cache state after an error is retained for this cleanup; do not resume
serialization after a failed character lookup. Shared ``Rbc_ExportFree`` calls
this cleanup as part of ending the document.

See :doc:`text-font-reference` for the maintained function contracts. Detailed
sfnt/CFF parsing and PDF resource encoding remain with their implementation
modules.
