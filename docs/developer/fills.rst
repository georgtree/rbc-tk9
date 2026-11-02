Area fills and error bands
==========================

Line elements keep ordinary area fills and uncertainty bands as separate
geometry. Both are implemented in ``rbcGrLine.c`` and use renderer polygon
services, but their input, closure and continuity rules differ. Selected
private contracts are in :doc:`fill-reference`.

Resources and configuration
---------------------------

An area and a band each have independent colors, pattern, tile, opacity and GC.
A pattern is ``None`` for disabled, the ``PATTERN_SOLID`` sentinel for solid,
or a retained Tk bitmap. Only actual bitmaps are passed to ``Tk_FreeBitmap``.
Tiles are retained RBC tile clients, not element-owned Tk image commands.

``PrepareLineAreaTransaction`` stages pattern and tile acquisition and builds
a candidate GC. It handles initial defaults/option-database values and checks
explicit occurrences in their original order. A failed later option releases
the staged resources without replacing the live ones. Commit transfers resource
ownership, installs the tile-change callback on the stable line record and
releases replaced resources. This follows :doc:`configuration`, rather than
relying on Tk option rollback to undo derived-resource changes.

Both opacity values must be finite and within zero to one. Enabled error bands
require Cartesian axes and linear or step smoothing; configuration rejects
other smoothing modes. Ordinary area fills also support polar elements and
can follow their parametric smoothing. The shared implementation does not mean
that every concrete element exposes every fill option.

``TileChangedProc`` schedules graph/backing-store redisplay while the Tk window
exists. ``DestroyLine`` releases tile clients, real pattern bitmaps and derived
GCs. It does not destroy the application's source images. See :doc:`images-tiles`
for tile notification and resource lifetime.

Ordinary area geometry
----------------------

``MapLine`` clears old geometry, maps the source points, applies smoothing and
any explicit geometric reduction, then calls ``MapFillArea``. Configured area
fills exclude the pixel-density decimation path; explicit ``-reduce`` remains
separate. See :doc:`decimation` and :doc:`splines`.

The area stores one polygon in ``fillPts``/``nFillPts``. If the mapped input has
a ``breakBefore`` array, no area is produced: joining separate runs into one
polygon would bridge gaps. This is a deliberate limitation, not the multi-run
behavior used for bands.

.. list-table::
   :header-rows: 1

   * - Closure
     - Construction
     - Minimum mapped points
   * - Baseline
     - Add vertical screen edges from the last and first points to a lower screen baseline.
     - Two
   * - Origin
     - Add the mapped data origin ``(0,0)``.
     - Two
   * - Chord
     - Close directly from last point to first.
     - Three

Baseline closure retains the historical one-pixel positive screen-X offset.
Its baseline is the greater of the current plot bottom and the largest mapped
screen Y. It is not a data-Y zero baseline, and inversion does not turn it into
a data-axis-relative closure. Origin closure has no such offset and is skipped
when either axis is logarithmic or the mapped origin is nonfinite. Chord adds
no auxiliary vertex.

The mapper allocates temporary unclipped points and a clipping destination,
with overflow checks and capacity ``3 * inputCount + 1``. ``Rbc_PolyRectClip``
generates the closing edge and clips against the current graph/track extents.
The temporary input is freed; a successful destination transfers to the line.
Too few clipped points or allocation failure leaves no new area. Plot clipping
and its allocation contract are described in :doc:`geometry`.

Error-band geometry
-------------------

``MapErrorBand`` runs before center-line mapping and reads original source
arrays. Symbol thinning, center-line decimation and geometric reduction cannot
remove an independently varying uncertainty bound. A band needs an enabled
X/Y mode, a tile or pattern, and at least two usable samples.

For an X band, boundaries are ``(high,y)`` and ``(low,y)``; for a Y band they are
``(x,high)`` and ``(x,low)``. Nonempty symmetric error input takes precedence over
explicit low/high arrays. Processing stops at the shortest required input.
Reversed low/high bounds are swapped, including those produced by negative
symmetric errors.

Nonfinite source/bound values, log-domain violations and nonfinite mapped
coordinates end the current run. A trace-direction break also ends it, but the
valid breaking sample starts the next run. The direction test uses original
source X. Each run needs two boundary samples before it can form a polygon.

Linear boundaries connect adjacent samples directly. Step boundaries insert
the same X-first, then Y corner convention as the trace; inversion changes
which screen coordinate implements that step. Boundaries are not independently
spline-smoothed.

``AppendErrorBandPolygon`` copies the upper boundary forward and the lower
boundary backward, then clips the polygon. Each successful node owns its point
array; nodes are linked through ``errorBand.polygons`` in traversal order.
Temporary upper/lower workspaces are freed after mapping. Failure to construct
one polygon omits that piece; already appended pieces are retained.

The workspace also respects ``Rbc_RenderPolygonPointLimit``. A finite native
limit reserves worst-case clipping expansion and closure space. Long runs split
at shared boundary samples so neighboring pieces meet. Cairo, export and the
current Windows native backend report no finite point-count bound; native X11
supplies a request-size limit. This chunking applies to band construction and
must not be assumed to describe the ordinary area mapper.

Drawing, export and cleanup
---------------------------

Normal drawing paints the area first, then band polygons, then traces and
annotations. These fills are element-level resources, rather than per-point
palette styles. Picking remains based on the element's trace/symbol geometry;
the fill polygon does not introduce a separate interior-hit operation.

``DrawLineFill`` borrows the polygons and resources. A tile takes precedence
over a pattern; a tile without a pixmap suppresses output. With Cairo selected,
``DrawRenderedFill`` first attempts a photo tile or an opacity-aware pattern.
The tile path uses image transparency and does not receive the area/band opacity
argument. Failed or unsupported Cairo rendering falls back to a native fill
context. Native pattern filling retains its non-blended behavior. Do not infer
identical transparency support merely from the common polygon interface.

Export builds ``Rbc_RenderFillStyle`` and uses export fill contexts with the
mapped geometry. It preserves area-before-band ordering and supplies tiles
through ``Rbc_RenderSetFillTile``. PostScript retains its opaque/tile-background
policy; format capabilities remain the renderer's responsibility. See
:doc:`rendering` and :doc:`exports`.

``ResetLine`` frees every band node and its points, then frees and zeros the
ordinary fill. It runs before mapping can return early, so shortened or empty
data cannot leave a stale polygon on screen. Mapping helpers assume this reset
has happened; they are not replacement APIs for an already populated list.

Pattern and tile transaction details
------------------------------------

GetLineAreaOption resolves canonical names using the concrete element option
table. The same transaction code handles ordinary area and error-band resources,
selected by its band argument. This shares ownership machinery, not option
availability or geometry rules.

Pattern parsing has three results: empty/NULL means None, exact solid means
PATTERN_SOLID, and other strings acquire a Tk bitmap. GetPatternFromString writes
its output only after successful lookup. FreeLinePattern releases only actual
bitmap references. Tile parsing similarly treats empty/NULL as a staged clear;
nonempty input acquires a client through Rbc_GetTile. Both staging helpers acquire
the new resource before releasing an earlier candidate, preserving that candidate
if the replacement fails.

PrepareLineAreaTransaction initializes fresh candidate storage. Initial defaults
are considered only for fields not explicitly supplied; explicit repetitions
are processed in caller order. A bad early pattern or tile still fails even if
a later occurrence would replace it. The fill colors are already managed by Tk's
saved-option transaction and are borrowed by this preparation step.

CreateLineFillGC sets foreground/background pixels only for non-NULL colors. A
real bitmap selects transparent stippling without a background or opaque
stippling with one. None and PATTERN_SOLID leave stippling disabled. The GC's
existence does not itself mean an area will be drawn: geometry and draw dispatch
still determine that. Opacity and tile rendering are not implemented by this GC
constructor.

Commit transfers staged resources, installs tile notifications on the stable
line record, and releases the displaced resources. An unchanged retained tile
also receives the callback if needed. Moved candidate fields and the staged mask
are cleared so subsequent transaction cleanup is safe. Cleanup frees only the
resources it still owns; it never destroys the application's image command or
frees the Tk-owned fill colors. A tile change requests redisplay while the graph
window exists, even if the element is hidden. It does not draw synchronously.

Adjacent scalar and symbol parsing
-----------------------------------

Line scalar configuration separately stages maxsymbols, smooth and trace values.
GetLineScalarOption recognizes canonical option identities after Tk has validated
the names. PrepareLineScalarTransaction follows the same default-versus-explicit
ordering as resource transactions. Failed preparation can retain earlier staged
numbers, but owns no resources requiring cleanup; those numbers must not be
committed. Commit copies only marked fields and clears the mask, leaving mapping
and redraw to the enclosing configuration operation.

The parsers deliberately preserve their existing differences:

.. list-table:: Scalar parsing rules
   :header-rows: 1

   * - Value
     - Accepted form
   * - maxsymbols
     - The existing nonnegative pixel conversion, rather than a separate count parser.
   * - smooth
     - Exact linear, step, natural, cubic, quadratic or catrom; cubic aliases natural.
   * - trace
     - Nonempty prefixes of increasing, decreasing or both.

The smoothing failure diagnostic lists all accepted names, including the cubic
alias and catrom. Matching remains exact; the diagnostic does not change parsing.

GetSymbolFromObj initializes a fresh ParsedSymbol rather than releasing an old
one. Empty input disables symbols. Geometric names use legacy prefix rules:
none, diamond, plus, triangle and arrow accept one-character prefixes; circle,
cross, square, splus and scross require at least two characters. Matching is
case-sensitive. Other input is parsed as a one- or two-item bitmap/mask list.
The bitmap is released if mask lookup fails, and success transfers both acquired
references to the result. This parser does not validate bitmap/mask dimensions.
FreeParsedSymbol clears released handles but leaves the type field unchanged.
