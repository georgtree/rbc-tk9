Line decimation and source identity
===================================

``rbcGrLine.c`` reduces dense screen traces while retaining original source
indices. It does not rewrite attached vectors. Pixel-density decimation,
geometric reduction and symbol thinning are separate operations; changing one
must not silently change the contracts of the others.
See :doc:`decimation-reference` for selected static helper contracts.

Eligibility and density
-----------------------

``CanPixelDecimateLine`` enables the optimization only for automatic decimation
on ordinary XY line or strip elements, with unsmoothed geometry, fewer than two
palette pens and no tile/stipple area fill. Polar elements and document exports
retain the full mapping path. These checks protect geometry and style semantics.

``LineNeedsPixelDecimation`` separately tests the complete paired source count
against the current plot span in the physical data-X direction. This is screen
X normally and screen Y for inverted axes. Current thresholds are eight samples
per pixel for native rendering and four for Cairo. Invalid or subpixel spans
skip reduction. These constants are performance policy, not numerical accuracy
parameters or public API guarantees.

The count is deliberately the full source count rather than only visible
samples: the fast path avoids mapping the full array. Track mapping supplies
the owning track's axes and plot scope, so its extent and inverted orientation
feed the same decisions. See :doc:`mapping` and :doc:`scrolling`.

Mapping paths and fallback
--------------------------

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Path
     - Work and ownership
   * - Cached pre-map reduction
     - Locates visible source ranges using monotonic X and block summaries;
       maps selected representatives into new point/index arrays.
   * - Scanning pre-map reduction
     - Scans source data when the cached path is unsupported, retaining run
       boundaries and selecting representatives before full mapping.
   * - Full mapping and pixel reduction
     - Maps source points first, then reduces eligible monotonic mapped runs.
       Failure or no benefit retains the existing mapped arrays.

Pre-map reduction is more restrictive than general pixel reduction. Normal
symbols, value labels and error-bar arrays require full point/style mapping.
An all-active selection with symbols or labels also prevents the pre-map path;
explicit active indices can be mapped separately. The single-pen fast path
avoids allocating a source-sized style map, but still attaches strip segments
to the pen through ``MergePens``.

Each pixel bucket retains first, minimum, maximum and last representatives in
original source order, removing duplicate representatives. Inverted axes change
the bucket direction, not source ordering. Run boundaries must survive reduction
so invalid samples or separate traces are not bridged. Arbitrary nonmonotonic
paths cannot be aggregated as if they were a monotonic waveform: returning to
the same column can represent different geometry.

The mapping record owns its temporary arrays, including source indices and any
break-before flags. Trace/strip mapping consumes that geometry, and the temporary
arrays are then freed. Persistent caches own summaries but borrow source arrays;
they do not keep a vector's old allocation alive.

Persistent cache maintenance
----------------------------

The cache summarizes blocks of 64 source samples. For a supported continuous
monotonic-X run, binary searches locate source intervals for physical pixel
buckets. Whole-block extrema and scans of boundary fragments find representatives
without rescanning every source value on each axis remap.

Exact changed Y ranges rebuild only affected blocks. A change wholly beyond
the paired X/Y point count cannot affect the plotted trace. Unknown/full Y
changes invalidate the cache. X changes invalidate ordering rather than trying
to repair it incrementally. Pen or state changes can retain the data-domain
cache even when geometry must be remapped.

A tail append may retain the cached prefix even after vector storage moves,
but only when change metadata proves that prefix unchanged. Shrinkage,
replacement, edits within the prefix or unsupported new ordering cause
invalidation. Cache refresh failure is a request to discard/rebuild the cache,
not a Tcl command error. Non-finite values, unsupported ordering or logarithmic
domain discontinuities fall back to scanning/full mapping.

Changed-range notifications are therefore part of correctness, not merely a
speed hint. C producers must report changes after modifying vector storage;
otherwise consumers cannot know that cached summaries are stale.
See :doc:`vectors` and :doc:`elements`.

Picking, symbols and exports
----------------------------

Closest-point searching uses the full mapped source-point array when available,
or maps original samples on demand when the pre-map path omitted that array.
A point omitted from the display trace therefore remains selectable with its
original index and value. For searches
considering X distance, a valid monotonic cache narrows the candidate interval;
Y-only searches cannot use that X-distance bound. Candidate samples are mapped
and checked against plot extents before distance comparison. This does not make
interpolated segment picking identical to nearest-sample picking.

Symbols and error geometry have their own source-based mapping requirements.
Geometric reduction through ``-reduce`` is applied separately after pixel
reduction; reduced-symbol selection is another explicit policy. See
:doc:`splines` for smoothing, which disables automatic pixel decimation at the
semantic eligibility check.

Document output sets ``GRAPH_EXPORT`` and remaps without pixel decimation.
Thus SVG, PDF and PostScript do not inherit screen-density loss from the display
cache. Other explicitly configured processing, such as geometric reduction,
remains separate. Export restoration remaps the screen afterward; see
:doc:`exports`.
