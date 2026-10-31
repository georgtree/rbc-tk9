PostScript output and drawing state
===================================

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
