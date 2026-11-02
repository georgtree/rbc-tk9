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

Maintenance checks
------------------

When changing this layer, check ordinary and track graphs, nested clipping,
solid and dashed strokes, symbol batches, stipple fills, arcs and text with
parentheses or backslashes. Check failures with a missing prolog separately
from successful output. Keep page framing and delivery in ``rbcGrPs.c``;
component code should request renderer operations rather than assemble a new
PostScript document.
