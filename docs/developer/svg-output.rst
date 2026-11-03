SVG definitions and image encoding
==================================

See :doc:`document-rendering` for shared context ownership and the PDF/PostScript
primitive contracts, including graphics-state and capture boundaries.

``rbcGrSvg.c`` frames the document; ``rbcRenderSvg.c`` emits primitives through
the renderer interface. Both borrow a shared ``Rbc_ExportContext``. SVG requires
no Cairo backend or PDF object builder. See :doc:`exports` for command-level
layout, restoration and delivery, and :doc:`svg-reference` for helper contracts.

Document and context lifetime
-----------------------------

The SVG document callback appends an XML declaration and root element using the
mapped graph width/height for both dimensions and viewBox. It traverses graph
output, finishes embedded fonts after successful traversal, and appends the
root close. On success it creates a Tcl string from the document buffer. The
shared command checks export status and owns delivery with UTF-8/LF settings.
An unsuccessful partially generated document must not be returned as valid SVG.

``Rbc_RenderCreateSvg`` allocates a zeroed context containing shared export
state and bitmap-symbol definition state. It borrows the token and its buffer;
renderer completion frees the context without closing the document. All contexts
must finish before shared export storage and font resources are released.
Drawing appends XML immediately; it does not retain a DOM or a deferred scene.

Definitions and scopes
----------------------

Rectangular clip pushes increment ``nextResourceId``, emit a clipPath definition
in user coordinates, and open a group referring to it. Pop closes the group.
Common renderer depth tracking controls pairing and unwind. Plot output has a
separate ``rbcPlot`` definition/group and paints its background before entering
the plot clip. That fixed name belongs to the current single-graph document;
it is not a namespace scheme for concatenating independent SVG documents.

Patterns and bitmap-symbol definitions also use document-generated identifiers.
A fill context caches its pattern ID, so repeated polygons with the same style
reuse that definition. This is not PDF-style document-wide byte interning.
Changing resource styles behind a context's cached ID would reuse the wrong
definition; configure a context before drawing and keep borrowed resources live.

Tile patterns use photo blocks when available and otherwise capture generic Tk
images. Their origin accounts for the widget's offset from its toplevel, matching
native tile alignment. Stipple patterns read monochrome bits with XYPixmap,
emit an optional background rectangle and foreground runs, and release the
readback image. Pattern dimensions and contents use userSpaceOnUse coordinates.
Definitions are emitted inline as needed, rather than collected in one header.

Primitive geometry and paint
----------------------------

Polylines with fewer than two points and polygons with fewer than three points
emit nothing. Independent segments are emitted as individual two-point
polylines. A dashed stroke with an off-color emits a solid underlay first, then
the dashed foreground. For segments, both passes finish for each segment before
the next segment begins. Caps and joins come from retained export state; native
GC objects are not consulted during SVG emission.

Solid polygons use the even-odd fill rule and explicit fill opacity. Patterned
polygons and arcs instead use the pattern's paint; solid-fill opacity is not
applied to these patterns. Rectangle fills use their own optional opacity group
around each rectangle, including patterned rectangles. These paths are not
interchangeable opacity implementations. Keep the component's style semantics
when reusing a primitive. A failed rectangle pattern balances the current group
before returning, but does not roll back earlier rectangles.

Arc geometry arrives normalized by the common dispatcher. Positive mathematical
angles map upward in SVG screen coordinates. A full ellipse is represented by
two arc commands with a close, avoiding a radial seam; a partial pieslice adds
the center, while a chord closes directly between endpoints. ``SvgArcPath``
leaves the path element open for its caller to append fill or stroke attributes.

Background clearing paints white rather than removing alpha. Relief borders
force Tk's lazy shadow-color allocation before reading colors, then approximate
edges with filled geometry. Groove and ridge are composed from nested reliefs;
flat or undersized borders retain only the requested background fill.

Symbol definitions and placement
--------------------------------

Geometric symbols use their shape-specific size factors and rounding rules.
Skinny plus/cross symbols are unfilled strokes; ordinary symbols combine fill
and outline. Bar legend samples use translated rectangle fills with the current
bar style. These are component conventions, not an SVG-wide rounding policy.

Bitmap symbols are read back once per context and stored in a document resource.
An explicit mask is used when a fill color is present and must match the bitmap's
dimensions. Transparent symbols do not acquire that separate mask. Set pixels
are encoded as unit-height horizontal vector runs, with optional mask
intersection. Repeated symbols use translated/scaled references with a uniform
scale that preserves aspect ratio. Readback images are temporary; the SVG
resource and its dimensions remain cached until context destruction.

Strings and error propagation
-----------------------------

``SvgString`` escapes ampersands, angle brackets and both quote characters.
Other bytes are decoded as Tcl UTF characters and checked for prohibited XML
characters. Errors are recorded on the export context; already emitted bytes
are not removed. The helper expects valid readable Tcl UTF input, not arbitrary
binary data. Binary images and font payloads use separate encoding paths.

A void drawing helper or nonzero resource ID does not establish overall success.
The shared export error records the first failure, and the outer command must
honor it. Do not implement a fallback by appending a second representation after
an encoding helper has partially emitted XML and recorded an error.

Raster encoding
---------------

``SvgPhotoBlock`` emits an inline PNG data URI. It copies each source row using
the Tk pitch and pixel layout, adds a zero PNG filter byte, and writes straight
RGBA pixels. Unlike Cairo image surfaces, these channels are not premultiplied.
A valid alpha offset distinct from RGB is used; otherwise alpha becomes 255.
The producer must supply valid RGB offsets and readable backing storage.

The helper rejects invalid basic storage and raw buffers exceeding its INT_MAX
size checks. Empty dimensions produce no image. Tcl zlib compresses the raw
scanlines; interpreter state is saved/restored around compression and the
compressed result is retained while its bytes are used. Temporary raw and
compressed objects are released explicitly.

PNG assembly writes signature, IHDR, IDAT and IEND chunks, including chunk
checksums. Base64 encodes the binary PNG into an image href with explicit mapped
position and dimensions. Temporary PNG storage is freed after emission. Encoding
is in memory and does not create temporary image files or invoke a Tcl image
format command.

Generic Tk images expose drawing rather than a universal pixel layout. Their
export path uses the shared raster-image capture service, including its
source-over/two-matte assumptions, then releases captured color images after
encoding. Window markers use a separate capture path: unmapped or zero-sized
windows are skipped; mapped windows are read from their current native drawable.
Export does not instantiate or lay out replacement child widgets. See
:doc:`images-tiles` and :doc:`markers` for source lifetime and window behavior.

Text and integration boundaries
-------------------------------

SVG text selects embedded-font output when requested; otherwise it resolves Tk
font attributes and emits text using those attributes. The nonembedded path
calls Tcl's font command, so it is not a pure geometry operation. Font state
belongs to the shared export token, and final embedded-font definitions are
emitted before the closing root element. See :doc:`text-fonts` for font ownership.

SVG paths, patterns and clip groups preserve renderer painter order. No native
pixel rounding should be introduced simply to resemble a screen backend.
Validation should parse the emitted XML and exercise escaped labels, transparent
images, repeated patterns, nested track clips, font modes and export followed by
screen-geometry restoration. A visually correct image alone does not validate
XML quoting, resource references or encoded binary payloads.

Text placement and font finalization
------------------------------------

Embedded text first resolves characters to export faces and measures the layout.
When Tk reports a zero-width character but the selected fallback face has a
positive advance, the layout gains the rounded-up fallback advance; genuine
zero-width glyphs retain their positions. Justification and rotated anchor
placement are then recalculated. Shadow and foreground passes emit per-character
spans, keeping Unicode text editable and registering faces on the export token.

Nonembedded text obtains attributes through the global Tcl ``font actual``
command and uses SVG ``textLength`` to preserve Tk's measured line widths. Its
attribute result is retained while interpreter state is restored. That restores
the Tcl result, not widget lifetime: replacing the global font command with a
mutating script is outside this helper's resource-lifetime guarantee.

``Rbc_SvgFontsFinish`` runs after drawing has registered all used faces. It hashes
the current document and original font bytes into a deterministic family prefix,
replaces temporary family attributes, and emits base64 Unicode font programs in
CSS font-face definitions. This reduces family collisions between inline SVG
documents with different character maps; it is not a security hash or a general
namespace for clip/pattern identifiers. The operation mutates the document buffer
and belongs to one finalization pass. Font-program failure can leave partial
output, which the outer export command must reject.
