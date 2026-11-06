Text layout and document fonts
==============================

Text has three distinct resource layers: component-owned Tk options, temporary layout/style views, and document-owned
export fonts. ``rbcText.c`` measures and draws multiline text; ``rbcFont.c`` resolves native outline programs for
SVG/PDF. Neither layer transfers ownership of a component's Tk font to a renderer. See :doc:`rendering` for rendering
contexts and :doc:`exports` for document finalization and cleanup.

Layouts borrow the text
-----------------------

:c:func:`Rbc_GetTextLayout` returns one allocation containing a ``TextLayout`` and its fragment array. Free it with
``ckfree``. Each fragment points into the original string, which must remain alive and unchanged while the layout is
used. Fragment counts are UTF byte counts; glyph processing must decode characters rather than index bytes as
characters.

Newlines terminate fragments, including empty lines. A trailing newline does not create an additional final empty
fragment. NULL is treated as an empty string. Tk metrics and ``Tk_TextWidth`` supply line dimensions; padding, shadow
offset, justification and interline leader determine the unrotated box and fragment baselines. Anchoring and rotation
are applied later. Large dimensions are bounded to integer geometry, while impossible fragment/count allocations are
fatal rather than Tcl command errors.

:c:func:`Rbc_GetTextExtents` is a separate measurement path, not an allocation-free alias for the layout algorithm. NULL
produces zero extents regardless of padding. For nonempty multiline text it retains the final leader spacing, whereas
the layout builder removes that final leader. Callers must choose the appropriate measurement rather than assume the two
are interchangeable.

Styles are not universal owners
-------------------------------

``TextStyle`` groups font, colors, border, GC and geometry settings. Merely copying or passing it does not acquire Tk
references. Components usually own font/color options; temporary styles borrow them.

:c:func:`Rbc_InitTextStyle` sets selected defaults, but does not initialize ``gc``, ``border``, ``width`` or
``height``. Callers must initialize any such fields they use. :c:func:`rbcTextDrawing.Rbc_SetDrawTextStyle` installs the
supplied GC without acquiring it; this does not grant permission to release a caller-owned GC.

For an owning style, :c:func:`Rbc_ResetTextStyle` obtains a Tk GC from the current font and optional foreground, then
releases the previous GC. Its old GC must be NULL or a valid owned Tk GC. :c:func:`Rbc_FreeTextStyle` releases only that
GC; it does not free font, colors, border or the style itself, and does not clear the GC field. Repeated cleanup
therefore requires an explicit reset. These are existing helper contracts, not an automatic resource-management
convention.

Native layout presentation
--------------------------

:c:func:`rbcTextDrawing.Rbc_DrawTextLayout` consumes the existing unrotated layout. It normalizes ``theta`` in the
style, computes and rounds the rotated bounding dimensions, then translates the requested anchor to that box's
upper-left corner. The private drawing loop places each fragment baseline relative to this origin. For zero rotation it
uses ``Tk_DrawChars``. For other angles it rotates the baseline around the layout center and calls Tk's private
``TkDrawAngledChars``. It does not create, rotate or stipple a bitmap of the text.

Angles must be finite and bounded before entering this layer: the historical ``FMOD`` macro converts a quotient to
``int``. The later finite checks on coordinates do not make arbitrary angles safe. Direct callers of
:c:func:`rbcTextDrawing.Rbc_GetBoundingBox` should supply normalized angles in [0, 360); its exact quadrant switch
assumes that convention. Corner outputs preserve the original NW, NE, SE, SW indexing, centered at the origin, rather
than sorting the rotated corners by their final position.

Integer anchor translation uses integer half-width/half-height and saturates wide intermediate subtraction to
``int``. :c:func:`rbcTextDrawing.Rbc_TranslatePoint` instead keeps fractional half-pixel offsets. Layout sizes clamp
negative results to zero and large values to ``INT_MAX``; floating drawing coordinates also handle nonfinite values
before rounding. These policies bound representation, not visibility: clipping remains the drawing context's
responsibility.

State selects the drawing passes in this order:

.. list-table:: Native text passes
   :header-rows: 1
   :widths: 25 75

   * - State
     - Drawing behavior
   * - Disabled or emphasis
     - Two border-colored passes, offset by one pixel; emphasis swaps colors. Shadow and active-color paths are
       bypassed.
   * - Normal or active
     - A positive-offset, non-NULL-color shadow is drawn first, if configured; then the main pass uses the current
       foreground or active color.

Color-changing paths reset the GC foreground to the style's normal color, not to the original GC value. The normal path
without a shadow does not explicitly select the normal color, so the supplied GC must already have the intended
foreground. Callers must supply any colors or border required by the selected state and permit the temporary GC
mutation. :c:func:`rbcTextDrawing.Rbc_SetPrintTextStyle` does not initialize a GC and is not sufficient preparation for
native drawing.

:c:func:`rbcTextDrawing.Rbc_DrawText` and :c:func:`rbcTextDrawing.Rbc_DrawText2` build and release temporary layouts
while borrowing the input string. Both skip NULL and empty strings; the latter also zeros a supplied result area on that
path. Its nonempty result is the rounded rotated layout size, independent of the anchor position. Exporters retain their
own text serialization paths; these native draw calls do not embed fonts.

Bitmap GC lifetime
------------------

:c:func:`rbcTextDrawing.Rbc_GetBitmapGC` supplies a different resource: a shared mutable depth-one GC cached by Display
pointer in a process-static hash table. Its creation bitmap is temporary, but the GC is retained. Subsequent requests do
not reset its foreground, background or other attributes. Callers borrow it, establish their required state and must not
free it through text-style cleanup. The cache has no teardown or synchronization in this module; it should not be
treated as a per-interpreter or thread-isolated resource owner.

Document-local font resolution
------------------------------

An export context lazily owns an ``ExportFontCache`` through ``fontData``. The cache has two levels:

* A character lookup keyed by the borrowed ``Tk_Font`` identity and Unicode scalar, retaining the resolved font,
  character ID and pixel size.
* Font program records shared by family and physical bold/italic style, when the candidate program contains the
  requested glyph. Different sizes can share one program. Each record owns bytes, character/glyph arrays and lookup
  state.

:c:func:`Rbc_ExportFontCharacter` first rejects invalid scalars and pre-existing export errors. On a cache miss it
invokes Tk 9's global ``font actual`` command with ``-displayof`` and the character, so fallback selection is
character-specific. The query saves/restores interpreter state and retains its result only while extracting
attributes. Positive point sizes are converted to screen pixels; negative Tk sizes already denote pixels.

This Tcl evaluation is a callback boundary, not a pure font-table read. The helper itself does not preserve a
graph. Callers must retain the surrounding export lifetime and callback-validity discipline; saving interpreter state
alone does not make arbitrary reentry safe.

A successful lookup returns a borrowed ``Rbc_ExportFont`` and a one-based character ID. That ID is independent of the
native glyph index, allowing two Unicode characters sharing an outline to retain distinct extraction mappings. The
returned advance is computed from horizontal font metrics at the resolved pixel size. Outputs must not be consumed after
failure.

Native sources and font programs
--------------------------------

On Windows, font acquisition creates a temporary GDI font and DC and reads sfnt tables using ``GetFontData``. On X11,
Fontconfig selects a scalable TrueType/CFF candidate with the character; Tcl filesystem channels read its font
file. This may use a scalable substitute for a legacy core-X11 bitmap or Type1 face. Collection face selection is
validated; unsupported variable-font instances fail rather than silently selecting another instance.

Temporary source resources are released after constructing the program or on failure. The program builder creates a
standalone sfnt, removes stale signatures and recomputes checksums. Table views borrow ranges within the owned program
bytes; they must not be individually freed or retained after cache cleanup. Font validation checks required tables,
supported formats and embedding permissions. Embedding retains outlines rather than performing glyph subsetting.

Backend integration and cleanup
-------------------------------

SVG's embedded-font path retains Unicode text and uses :c:func:`Rbc_FontUnicodeProgram` to rebuild a cmap for the used
characters, including legacy Symbol mappings. It appends to a caller-initialized DString without changing the cached
font. Finalization generates font definitions and content-derived family names to avoid collisions when separate SVG
documents are included in one page. The nonembedded SVG path uses viewer font selection instead of these embedded
programs.

PDF uses its own font-resource and character-code writer around the shared font cache. Both exporters start from Tk text
layout, but exported fallback glyphs can require an advance where core X11 reported zero width; backend text code
adjusts such layout rather than assuming all screen and document glyph metrics are identical. Font embedding policy and
final document serialization remain backend responsibilities. PostScript retains its separate font mapping/token path
and does not use this cache for embedded outlines.

:c:func:`Rbc_FirstExportFont` returns a borrowed list head. :c:func:`Rbc_ExportFontsFree` releases cache entries,
programs, character arrays and optional Symbol encodings, then clears ``fontData``. An empty cache is safe to
free. Every returned font pointer and table view becomes invalid, but borrowed Tk fonts are not released. Partial cache
state after an error is retained for this cleanup; do not resume serialization after a failed character lookup. Shared
:c:func:`Rbc_ExportFree` calls this cleanup as part of ending the document.

See :doc:`text-font-reference` for maintained contracts for the text services, shared sfnt/CFF readers and PDF font
resource writer.

Reader trust boundaries
-----------------------

The small big-endian readers and writers do not check buffer lengths. Their callers establish readable/writable spans
first. :c:func:`Rbc_FontGetTable` similarly assumes a directory rebuilt by :c:func:`FontProgram`; it is not an entry
point for arbitrary unvalidated file bytes. A missing table returns a NULL/zero slice. Present slices borrow the owning
program and cannot outlive it.

:c:func:`FontProgram` accepts TrueType, the legacy true signature and OpenType OTTO. It checks the directory count and
table spans, omits DSIG signatures, rejects fvar variable-font tables and limits rebuilt storage to 64 MiB. Table data
is padded to four-byte boundaries, checksums are recalculated and head.checkSumAdjustment is repaired. The original
table order is retained. This creates an independent sfnt for a selected collection face, without removing unused
outlines.

The destination owns its allocation as soon as rebuilding starts. An error in a later table or a missing head table
therefore requires ordinary font cleanup, not merely source cleanup. :c:func:`FontSourceFree` releases the temporary
input resources; :c:func:`FontFree` releases the rebuilt program. Neither routine can safely be called twice on the same
unchanged record.

:c:func:`FontTables` checks the fields used by export: head, hhea, maxp, horizontal metrics, OS/2 permissions and the
cmap record directory. It rejects the supported set of recognized color-font tables and permission bits prohibiting
outline embedding. A usable PostScript name is restricted to safe ASCII name bytes; otherwise a local RBCFont serial
name is used. These checks do not constitute complete validation of glyph instructions, outlines, all cross-table
relationships or all possible OpenType extensions.

Unicode and legacy Symbol lookup
--------------------------------

:c:func:`FontGlyph` performs four passes, returning the first usable nonzero glyph:

#. Unicode format 12, covering BMP and supplementary-plane characters.
#. Unicode format 4, covering the BMP.
#. Windows Symbol format 4 using its F000-offset byte range.
#. Windows Symbol format 4 using the legacy unoffset byte range.

Unicode candidates use platform 0 or Windows Unicode encoding records. Format-12 lookup binary-searches groups and
checks the resulting glyph against glyphCount; its search assumes sorted groups. Format 4 walks segments, handles
idDelta and relative glyph arrays, and preserves glyph zero as missing. Malformed bounds or unsupported subtables are
skipped, so another candidate can still resolve the character. Bounds checks do not promise acceptance or rejection of
every malformed cmap arrangement.

For Symbol records, a retained Tcl encoding named after the lowercased family converts a Unicode character to one byte
where possible. Without that encoding, only direct byte values and the F000--F0FF range can use this fallback. The cache
keeps Unicode characters distinct even when multiple characters resolve to one native glyph; text extraction must not
infer Unicode from glyph numbers alone.

Name-keyed CFF metadata
-----------------------

:c:func:`CffIndex` checks its count, offset width, initial one-based offset, monotonic following offsets and final data
extent. Success advances the caller's cursor and returns a borrowed complete INDEX; failure does not advance the cursor,
but its output slice may already be partially written. :c:func:`CffItem` is deliberately lighter: it requires an already
validated INDEX and returns a borrowed item, which can be empty. It performs no repeated directory validation.

:c:func:`FontCffInit` supports CFF version 1 with one name and one Top DICT. It reads the charset and CharStrings
offsets needed by export, checks the CharStrings count against the sfnt glyph count, then constructs a glyph-to-SID
array. ISOAdobe, Expert and ExpertSubset predefined charsets and explicit formats 0/1/2 are supported. Standard SIDs
refer to static names; custom SIDs refer to String INDEX items. Names must be nonempty and at most 127 bytes before PDF
serialization. They are byte slices, not necessarily NUL-terminated strings.

CID-keyed CFF and CFF2 are outside this path. The parser interprets selected metadata and bounds its operand stack; it
does not execute or fully validate charstring programs or every unused DICT operator. Partial SID allocations remain
attached to the font for normal failure cleanup.

SVG cmap reconstruction
-----------------------

:c:func:`Rbc_FontUnicodeProgram` sorts temporary Unicode/glyph pairs from the collected characters. It generates format
12 entries for the complete used set and format 4 entries for up to 8,188 BMP characters below U+FFFF, plus the
mandatory sentinel. This limit keeps format 4 within its 16-bit length field; format 12 still represents characters
omitted from that compact table. Each pair maps the original Unicode scalar to the resolved glyph, including Symbol
fallback results.

The routine copies the font, redirects its cmap directory entry to the new data, then passes that temporary source
through :c:func:`FontProgram` again to restore directory layout and checksums. It appends the rebuilt bytes to the
caller's DString only on success. Temporary arrays and programs are released in both cases, and the cached font remains
unchanged. Retaining every outline is deliberate full embedding, not glyph subsetting.

PDF character codes and finalization
------------------------------------

.. list-table:: Font encoding paths
   :header-rows: 1
   :widths: 24 38 38

   * - Mode
     - Content codes
     - Resources
   * - Nonembedded
     - One-byte WinAnsi; unsupported characters fail explicitly.
     - Standard Courier, Times or Helvetica family/style dictionaries.
   * - Embedded TrueType
     - One-based document-local CID, emitted with four hexadecimal digits.
     - Full sfnt, descriptor, CIDToGIDMap, widths, ToUnicode and Identity-H Type0 font.
   * - Embedded name-keyed CFF
     - Codes 1--255 within each block, emitted with two hexadecimal digits.
     - Shared CFF program/descriptor; separate Type1 encoding, widths and ToUnicode per block.

Character lookup reserves placeholders before final dictionaries are known. :c:func:`Rbc_PdfFontsFinish` replaces them
after all text has been collected and before final PDF serialization. CFF blocks split every 255 distinct characters;
they share the program rather than duplicate it. The shared cache limits a font to 65,535 distinct characters. Callers
must finalize once and stop collecting text at that point: the API does not freeze the cache or prevent repeated
finalization.

Widths and descriptor geometry scale native units to 1,000-unit PDF text space. Glyphs beyond the long-metric count use
the last horizontal advance. TrueType CIDToGIDMap controls which outline is drawn, while ToUnicode independently
controls extraction. BMP values use one UTF-16 code unit and supplementary values use a surrogate pair; mapping blocks
contain at most 100 entries. CFF Differences names escape PDF delimiters and nonprintable bytes instead of assuming all
glyph names are directly usable PDF names.

Resource serialization temporarily redirects token->buffer to local DStrings and restores the previous pointer on
return. Font records and table views stay owned by the export context; PDF resource copies have their own document
lifetime. A failed export is not a checkpoint from which serialization can resume.

Parameter units and identifier domains
---------------------------------------

The character and font-resource APIs deliberately use different integer domains. A plain C ``int`` does not make the
following values interchangeable:

.. list-table:: Font identifiers and units
   :header-rows: 1
   :widths: 25 40 35

   * - Value
     - Meaning
     - Producer or consumer
   * - Unicode scalar
     - Original text character; not an outline index.
     - :c:func:`Rbc_ExportFontCharacter`, :c:func:`FontGlyph`
   * - Native glyph index
     - Zero-based index into one program's glyph metadata; zero denotes missing
       in the lookup path.
     - :c:func:`FontGlyph`, :c:func:`Rbc_FontGlyphName`
   * - Shared character ID
     - One-based index into one cached font's collected characters.
     - :c:func:`Rbc_ExportFontCharacter`
   * - PDF resource ID
     - Object number identifying a font dictionary in this document.
     - :c:func:`Rbc_PdfFontCharacter`
   * - PDF character code
     - WinAnsi byte, TrueType CID or CFF block-local code for the selected resource.
     - :c:func:`Rbc_PdfFontCharacter`
   * - Size and advance
     - Display pixels; PDF widths later use 1,000-unit font space.
     - :c:func:`Rbc_ExportFontCharacter`, :c:func:`FontCffResources`

:c:func:`Rbc_PdfFontCharacter` returns the hexadecimal width separately from the code: two digits for standard fonts and
CFF, four for TrueType. In nonembedded mode it sets the advance to zero and the width to two before checking whether the
character is supported. Outputs are therefore usable only on ``TCL_OK``; error return does not promise untouched output
pointers. The shared native resolver writes its result pointers only after successful resolution.

:c:func:`FontActual` returns a retained Tcl object, which its caller releases. func:`FontAttribute` instead returns a
:c:borrowed value inside that object. Keeping the attribute pointer does not retain the dictionary. Likewise,
:c:func:`Rbc_FontGetTable` and :c:func:`CffItem` return borrowed byte slices; func:`Rbc_FontGlyphName` may return static
:c:standard-name bytes or a slice of font-owned custom-name storage. None should be freed by the caller.

:c:func:`FontSourceOpen` leaves partial acquisition resources with its caller on failure. Pair it with
:c:func:`FontSourceFree` on either outcome. The rebuilt program has a separate owner: :c:func:`FontProgram` can attach
its destination allocation before a later validation error, and :c:func:`FontFree` releases that font record. The source
and rebuilt program must not be treated as aliases.

Choosing a text helper
-----------------------

.. list-table:: Measurement and drawing contracts
   :header-rows: 1
   :widths: 27 38 35

   * - Helper
     - Geometry and result
     - Resources and mutation
   * - :c:func:`Rbc_GetTextLayout`
     - Unrotated fragment baselines and extents; removes the final leader when fragments exist. NULL is treated as empty
       text, with style padding.
     - Returns one owned allocation; fragments borrow the input text.
   * - :c:func:`Rbc_GetTextExtents`
     - Unrotated extents without fragments; retains final leader spacing.
       NULL returns zero without consulting the style.
     - Writes both required output pointers; allocates no layout.
   * - :c:func:`rbcTextDrawing.Rbc_DrawTextLayout`
     - Anchors the rotated bounding box at the requested pixel position.
     - Borrows the layout; normalizes style theta and may change GC foreground.
   * - :c:func:`rbcTextDrawing.Rbc_DrawText2`
     - Draws and optionally returns rotated bounding dimensions, independent of anchor position. NULL/empty text returns
       zero dimensions.
     - Builds and frees a temporary layout; inherits drawing state mutations.

The internal :c:func:`rbcTextDrawing.DrawTextLayout` takes an already anchored upper-left position. The externally named
drawing helpers instead take an anchor position and apply the style's anchor themselves. Passing an already translated
origin to those helpers applies the anchor offset twice.

:c:func:`rbcTextDrawing.Rbc_GetBoundingBox` returns floating extents and optional centered corners. It does not place
the box on a drawable. Use :c:func:`rbcTextDrawing.Rbc_TranslateAnchor` for saturated integer placement or
:c:func:`rbcTextDrawing.Rbc_TranslatePoint` to retain half-pixel center offsets. The ``TK_ANCHOR_NULL`` enum leaves a
position unchanged; it does not permit a NULL input point or missing required output pointers.

:c:func:`rbcTextDrawing.Rbc_SetDrawTextStyle` borrows its GC without acquiring a Tk
reference. :c:func:`Rbc_ResetTextStyle` and :c:func:`Rbc_FreeTextStyle` require an owned Tk GC or NULL, so do not apply
them directly to a style containing someone else's GC. The print setter's ``activeColor`` is an active foreground, not a
background color; :c:func:`rbcTextDrawing.Rbc_SetPrintTextStyle` does not initialize a GC or prepare the style for
native drawing.
