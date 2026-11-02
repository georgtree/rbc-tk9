PostScript output and drawing state
===================================

See :doc:`document-rendering` for shared context ownership and the PDF/PostScript
primitive contracts, including graphics-state and capture boundaries.

PostScript output is independent of Cairo. ``rbcGrPs.c`` owns page setup and
command delivery, ``rbcRenderPostscript.c`` implements renderer primitives,
and ``rbcPs.c`` supplies the private output token and legacy drawing helpers.
``library/rbcGraph.pro`` supplies executable PostScript procedures used by
both layers. See :doc:`exports` for callback protection, geometry restoration,
file delivery and the Windows EPS preview wrapper.

Token ownership
---------------

``Rbc_GetPsToken`` allocates a token with an empty DString, NULL font/color map
names and color output selected. Its interpreter and Tk window are borrowed.
The command assigns mapping variable names from its PostScript configuration;
the token does not own those strings. ``Rbc_ReleasePsToken`` frees the DString
and token, but none of these borrowed objects.

During ``GraphToPostScript``, the shared export context's ``buffer`` points to
the token's DString and ``backendData`` points to the token. Renderer contexts
borrow that export context and token. Destroy renderer contexts before ending
the enclosing export; releasing shared export state does not release the
alternate DString. The command releases the PostScript token separately.

The returned output pointer is a temporary view, not a detached document.
Appending can invalidate it. Scratch storage is separate from the DString but
is reused by formatting and prolog reads. Neither storage is safe for an
independent, interleaved writer. Raw append helpers accept C strings; they are
not a binary document API.

Prolog and page setup
---------------------

Page setup emits EPS metadata and includes ``rbcGraph.pro`` from the directory
named by ``rbc::library``. An installed Tcl library is therefore a runtime
export dependency, even though primitives are implemented in C. The included
prolog defines such operations as ``Fill`` and symbol procedures; primitive
output is not a self-contained PostScript document.

Setup converts display pixels to points using screen resolution, reverses the
vertical direction, then applies page origin, orientation and scale. Drawing
primitives continue to receive display coordinates. A successful traversal
appends ``showpage`` and the closing graphics/dictionary scope. Creation-date
metadata means byte equality across separately timed exports is not a reliable
geometry test.

Prolog loading can leave partial output after an error. It reports lookup,
open and read failures, closes the channel, and does not propagate close
failure. The enclosing export must discard failed output rather than treat
the buffer as a completed document.

Graphics-state boundaries
-------------------------

Plot begin establishes the background and opens a saved clipping scope; plot
end restores it. Nested rectangular clips also pair ``gsave`` and
``grestore``. Preserve this nesting when adding track-local or component clips.

Dash-background strokes use ``DashesProc``; symbol batches use
``DrawSymbolProc``. These procedures are redefined in the document dictionary.
A graphics-state save does not make dictionary definitions local. Emit a
batch's definition and uses together; do not interleave another batch that
redefines the same procedure.

Elliptical arcs temporarily transform a unit circle. The backend saves and
restores the current transformation matrix on the operand stack, retaining the
constructed path. It restores display units before stroking or filling so
line widths, dashes and stipples are not stretched with the ellipse.
Polyline output is split at 1500 path components, restarting at the shared
endpoint. Changes to this batching must preserve connected geometry and
stroke behavior.

Legacy primitive contracts
--------------------------

The helpers in ``rbcPs.c`` append to the existing token; they do not construct a
page, write a file or own a renderer context. Their coordinates are already
mapped. They rely on the enclosing transformation and clip, and generally do
not validate geometry or save/restore graphics state. Components should still
use renderer operations rather than add direct calls to these legacy helpers.

Foreground and background conversion use the upper eight bits of each XColor
component. The default prolog maps foreground to black and background to white
in monochrome mode, or converts to grey in greyscale mode. An optional Tcl
color-array entry instead supplies raw PostScript code and bypasses these
operators. A lookup may execute a read trace, so this is a callback boundary,
not just a color-table read. The enclosing export must protect borrowed
lifetimes; the void color helpers do not expose lookup errors or undo output.
``Rbc_ClearBackgroundToPostScript`` merely selects white: it clears no pixels.

Line attributes have their own compatibility rules: zero-width lines become
one user unit, CapNotLast maps to butt, and dash phase is always zero. The
combined attribute helper also replaces ``DashesProc`` with an empty procedure.
Callers that need a dash-background pass must define it after that setup.

.. list-table:: Legacy path effects
   :header-rows: 1
   :widths: 30 70

   * - Helper
     - Effect on the current path
   * - Region
     - Starts a closed Box without painting.
   * - Rectangle / rectangles
     - Starts each Box and consumes it with built-in fill.
   * - Path
     - Starts an open path; does not paint or split it.
   * - Polygon
     - Closes a new path and calls Fill, which preserves the path.
   * - Integer segments
     - Does not clear an existing path before the first stroke.
   * - Floating segments
     - Starts with newpath so a previous retained polygon is not stroked.
   * - Integer polyline
     - Starts a new path; strokes and restarts at shared batching endpoints.

Nonpositive point or segment counts append nothing, leaving any existing path
untouched. Each independent segment ends with ``DashesProc stroke``. Integer
polyline batches use the 1500-component boundary; generic floating paths and
polygons do not implement that splitting. Connected endpoints do not imply
continuous dash phase or identical joins across separate strokes.

The integer polyline helper treats fewer than two points as a no-op, without
reading the point array or modifying the output. Two or more points retain the
existing batching and stroke behavior. Its private regression checks run with
``make test-resources``; they cover empty/single-point input, ordinary lines and
the 1500-component boundary.

Relief rectangles
-----------------

``Rbc_Draw3DRectangleToPostScript`` draws only border bands. It initializes Tk's
light-border GC before querying shadow colors, so export need not wait for a
screen draw. Bottom and right strips are rectangles; the top/left band is a
polygon. Raised and sunken relief exchange the light and dark sides. Groove
and ridge recurse with half-width bands and complementary relief. Odd border
widths retain the existing integer split and inset rules.

The helper omits borders if either dimension is smaller than twice the border
width. Inputs must keep that multiplication and inset arithmetic representable;
this is not an arbitrary-size geometry validator. Colors and the final polygon
path remain selected after emission. ``Rbc_Fill3DRectangleToPostScript`` first
fills the whole background, then invokes the border helper: skipping the border
does not skip the fill. Border colors use background conversion, preserving the
monochrome policy rather than treating relief bands as foreground strokes.

Text and fill limitations
-------------------------

``Rbc_AppendPostScriptString`` emits literal-string contents, not the enclosing
parentheses. It decodes Tcl UTF text, escapes delimiters and controls, and
encodes ISO-8859-1 characters with octal escapes where needed. Characters
outside that repertoire become spaces. This path does not use the SVG/PDF
embedded-font mechanism described in :doc:`text-fonts`.

The renderer's fill path supports foreground, optional background and stipple
operations. It does not apply the fill opacity used by other backends.
Document these format differences rather than assuming that a shared renderer
operation guarantees identical capabilities in every exporter.

Raster ownership and encoding
-----------------------------

The legacy raster helpers do not use SVG/PDF font or image resources. They
append inline hex data and depend on the current PostScript environment.
Their inputs and temporary resources have distinct ownership:

.. list-table:: Image conversion ownership
   :header-rows: 1
   :widths: 32 68

   * - Entry point
     - Input and temporary image lifetime
   * - ColorImageToPsData
     - Borrows the image unchanged; appends to an initialized DString.
   * - ColorImageToPostScript
     - Borrows the image, but modifies its RGB bytes in noncolor mode.
   * - PhotoToPostScript
     - Copies the photo into a temporary color image, then frees the copy.
   * - WindowToPostScript
     - Owns and frees a native capture, or draws a grey rectangle on failure.

``Rbc_ColorImageToPsData`` traverses rows from bottom to top. RGB output emits
red, green and blue bytes. Its one-component branch emits the bitwise complement
of the red channel; it does not itself calculate luminance. The outer serializer
first calls ``Rbc_ColorImageToGreyscale`` for noncolor output, modifying the
borrowed image. Both monochrome and greyscale token modes use this same raster
branch; they do not threshold each pixel into a one-bit mask. Alpha is ignored,
so transparent samples are neither omitted nor composited with a background.

Data lines hold at most sixty hex digits, prefixed by the supplied string and a
space. Wrapping continues across image rows; the final partial line also gets a
newline. The return value counts emitted data lines, not success/failure.
Unsupported component counts append nothing and return zero. ``ByteToHex``
writes exactly two uppercase digits without a NUL terminator.

The image program defines ``tmpStr`` as a row-sized input buffer and places an
eight-bit ``image`` or ``colorimage`` under a translated/scaled graphics state.
The negative vertical image matrix and bottom-to-top sample traversal are
existing encoding behavior. ``grestore`` restores transforms and drawing state,
but does not undo the dictionary definition of ``tmpStr``.

The row-buffer byte count is formatted as ``size_t``, preserving counts above
``INT_MAX`` without a varargs type mismatch. This is serialization of the count,
not a guarantee that a PostScript interpreter accepts arbitrarily large strings.

Bitmap acquisition and payloads
-------------------------------

Bitmap payloads are angle-bracket hex strings. Each row is packed separately,
with the first pixel in the high bit and unused tail bits zero. This differs
from the continuous byte stream of a color image. ``ReverseBits`` implements
the bit-order conversion; wrapping counts complete output bytes.

.. list-table:: Native bitmap capture
   :header-rows: 1
   :widths: 20 40 40

   * - Platform
     - Capture and cleanup
     - Existing failure behavior
   * - X11
     - XGetImage plane-one capture; XDestroyImage after encoding.
     - No payload if capture fails; no pixel access or Tcl status.
   * - Windows
     - Owned bottom-up DIB bytes, reverse row traversal, then ckfree.
     - Debug message and no payload if acquisition fails; no Tcl status.

Both paths require a live depth-one bitmap and positive matching dimensions.
They do not acquire ownership of the bitmap. The void interface cannot tell an
outer emitter to discard partially written operators when acquisition fails.
These limitations are separate from the drawable capture fallback used for
window export.

``Rbc_BitmapToPostScript`` centers the scaled mask at the current origin and
paints set bits in the current color inside a saved graphics state. It leaves
unset bits unpainted. ``Rbc_StippleToPostScript`` instead clips to the current
path and repeats the bitmap through the prolog's ``StippleFill`` procedure.
Both require a registered Tk bitmap because they query its dimensions with
``Tk_SizeOfBitmap``. Neither supplies an independent error channel or rolls
back a partially emitted program.

Legacy font selection
---------------------

``Rbc_FontToPostScript`` chooses a name and point size for the prolog's
``SetFont``. It does not embed font programs or resolve fallback glyphs per
character. Font selection follows this order:

.. list-table:: Selection order
   :header-rows: 1
   :widths: 30 70

   * - Source
     - Behavior
   * - Configured font-map array
     - Lookup by Tk_NameOfFont. A found entry ends the search, even when its
       list or size is malformed.
   * - Recognized family prefix
     - Case-insensitive prefix match against the static alias list, followed
       by Tk_PostscriptFontName for the actual name and size.
   * - X11 core font properties
     - XLoadQueryFont by Tk name, optional POINT_SIZE divided by ten, then
       FULL_NAME/FOUNDRY/FAMILY_NAME mapping. This stage is absent on Windows.
   * - Final fallback
     - Helvetica-Bold, with size twelve unless an X11 point-size property was
       obtained during the preceding attempt.

A font-map entry's first element replaces the name. Its size is used only for
an exactly two-element list with a valid integer second element. A one-element
mapping retains the initial size twelve; it does not inherit the Tk font size.
An empty list retains the Tk name. An invalid list or size may leave an
interpreter diagnostic, but the helper still emits SetFont and returns void.
Mapped names are inserted as PostScript name syntax without escaping or
validation, so callers must supply usable PostScript names.

For core X11 fonts, property mapping requires all three named properties.
Adobe families remain unchanged. Other foundries strip a leading ``itc`` plus space and
use an exact alias match, falling back to Helvetica. A typeface suffix is taken
from the full name only when it starts with the original family; spaces and
dashes are removed before appending that suffix. The returned mapped name lives
in a caller-owned DString, allowing the X font to be freed before emission.
Atom names themselves remain borrowed from Tk.

The alias table is a compatibility heuristic, not an inventory of installed
PostScript fonts. A prefix match in the Tk-family path and an exact match in
the X11-property path are intentionally documented as different operations.
SVG/PDF font embedding uses the separate machinery in :doc:`text-fonts`.

Text transforms and fragment widths
-----------------------------------

``Rbc_TextToPostScript`` builds a borrowed-string layout, computes the rotated
box, translates the requested anchor to that box's center, and emits BeginText.
The prolog saves graphics state, translates to the center, rotates by the
negative angle, then translates to the unrotated layout origin. Font selection
and any shadow pass follow; the main pass selects active or normal foreground.
EndText restores graphics state. No native disabled/emphasis relief pass is
implemented here, and the style's angle is not rewritten.

Each nonempty fragment emits its escaped literal string, measured screen width
and baseline through DrawAdjText. That prolog procedure adjusts character
spacing to match the requested width, flips the vertical scale and uses ashow.
Empty fragments contribute layout spacing but emit no string, avoiding division
by zero in the prolog's length-based adjustment. The string encoding remains
ISO-8859-1 with unsupported characters replaced by spaces; width adjustment
does not recover glyphs lost during that conversion.

Font and color mapping can execute Tcl variable traces while the layout borrows
its original string. The helper does not preserve the enclosing graph, style,
font or string itself; callers own that lifetime protection. Temporary layouts,
font-description objects, split-list arrays and mapped-name DStrings are released
by their respective helpers.

Fragment baselines and shadow offsets are added in ``Tcl_WideInt`` and emitted
without narrowing to int. Normal coordinates retain the same decimal output;
large sums no longer overflow signed int. The bounding-box calculation
normalizes the angle remainder into [0, 360), so equivalent negative and
positive rotations use the same anchor position. BeginText still receives the
original angle and the style is unchanged. Callers must keep input angles and
rounded dimensions within the conversion helpers' ranges.

Maintenance checks
------------------

When changing this layer, check ordinary and track graphs, nested clipping,
solid and dashed strokes, symbol batches, stipple fills, arcs and text with
parentheses or backslashes. Check failures with a missing prolog separately
from successful output. Keep page framing and delivery in ``rbcGrPs.c``;
component code should request renderer operations rather than assemble a new
PostScript document.
