Image resizing and resampling
=============================

``rbcImage.c`` provides CPU-side resizing over owned, row-major RGBA images. These operations do not use a renderer or
native drawable. Tk photo wrappers snapshot pixels into this representation before processing. See :doc:`images-tiles`
for allocation, channel offsets and photo-write ownership.

Three coordinate contracts
--------------------------

:c:func:`Rbc_ResizeColorImage` scales a source region to a requested output size. Its origin is clamped into the
source, then negative or excessive width/height values extend to the remaining edge. This is an origin-and-span policy,
not geometric rectangle intersection: a negative origin becomes zero without subtracting its displacement from a
positive span.

For each destination position, nearest-neighbour mapping selects the truncated ratio of destination position to
destination size, multiplied by the effective source span, then adds the source origin. It copies the entire pixel,
including alpha. No averaging or antialiasing is performed.

:c:func:`Rbc_ResizeColorSubimage` instead takes a region in a *virtual destination*. The full source is imagined at
``destWidth`` by ``destHeight``, while only the requested region is allocated. Its origin and each output position map
back through that virtual scale. Positions beyond either edge clamp to the first or last source pixel; they do not
create transparent padding. This avoids allocating a potentially much larger scaled image merely to display a crop.

:c:func:`Rbc_ResampleColorImage` filters the complete source to a new size. Source region extraction, when needed,
happens before this call. It allocates an intermediate image of destination width by source height, filters
horizontally, then filters vertically into the final image and releases the intermediate. All three functions leave the
borrowed source intact and return independent storage released with :c:func:`Rbc_FreeColorImage`.

Filters and sampling weights
----------------------------

:c:func:`Rbc_GetResampleFilterFromObj` resolves names and unambiguous abbreviations against the static filter
table. Returned records are borrowed and must not be modified or freed. ``none`` resolves successfully to NULL; it
directs command-level policy rather than representing an identity filter that may be passed to the filtering kernel.

The table includes box, triangle, bell, B-spline, Catmull-Rom, Mitchell, Lanczos, sinc, Gaussian and other historical
filters with individual support radii. Horizontal and vertical passes can use different non-NULL filters.

:c:func:`rbcImageResampling.ComputeWeights` builds one contribution record per destination coordinate, reused across all
rows or columns in that pass. Downsampling broadens the support radius by the inverse scale; upsampling uses native
filter support. Source support bounds are clipped to the image. Contributions are normalized by their sum (with a
zero-sum fallback factor of one), then converted to fixed-point weights with 14 fractional bits.

The loops accumulate red, green, blue and alpha independently in 64-bit integers. Each pass rounds and clamps its output
to byte range. The intermediate image therefore introduces quantization before the second pass; this is not a single
floating-point two-dimensional convolution. Signed filter lobes can produce overshoot that is clipped at this boundary.

Kernel table and support
------------------------

Support is a table policy, not always an intrinsic cutoff in the function. All values below are source-pixel distances
before downsampling broadens them. The aliases are distinct table entries even when they share a procedure.

.. list-table::
   :header-rows: 1
   :widths: 18 15 67

   * - Name
     - Support
     - Implemented kernel
   * - ``bell``
     - 1.5
     - Even piecewise quadratic bell.
   * - ``bessel``
     - 3.2383
     - j1(pi*x)/(2*x), with pi/4 at zero; legacy Borland builds return zero.
   * - ``box``
     - 0.5
     - One on the closed interval [-0.5, 0.5], zero outside.
   * - ``bspline``
     - 2
     - Even cubic B-spline; value at zero is 2/3.
   * - ``catrom``
     - 2
     - Interpolating Catmull-Rom cubic with signed lobes.
   * - ``default``
     - 1
     - For t = abs(x) below one: 2*t^3 - 3*t^2 + 1; otherwise zero.
   * - ``dummy``
     - 0.5
     - Diagnostic abs(x), not an identity filter.
   * - ``gauss8``
     - 8
     - exp(-2*x*x) times sqrt(2/pi).
   * - ``gaussian``
     - 1.25
     - Same procedure as gauss8, sampled over a narrower support.
   * - ``gi``
     - 1.25
     - Asymmetric piecewise approximation to a Gaussian tail integral.
   * - ``lanczos3``
     - 3
     - sinc(x)*sinc(x/3), zero at absolute distances of at least three.
   * - ``mitchell``
     - 2
     - Piecewise cubic with B = C = 1/3 and signed lobes.
   * - ``none``
     - 0
     - NULL procedure; lookup returns NULL for caller-selected resize policy.
   * - ``sinc``
     - 4
     - sin(pi*x)/(pi*x), with one at zero.
   * - ``triangle``
     - 1
     - max(1 - abs(x), 0).

None of these kernel functions reads image storage or normalizes its output. The caller samples the support and
normalizes the resulting contribution record. ``rbcBoxFilterPtr`` borrows the box table entry; neither that pointer nor
a lookup result transfers ownership of the static record.

Contribution layout and rounding
--------------------------------

Each Sample stores a source start index, count and variable-length ResampleWeight
array. :c:func:`rbcImageResampling.ComputeWeights` returns the byte stride, which must be used to advance between
records; ordinary Sample pointer arithmetic would be incorrect. :c:func:`rbcImageResampling.GetSampleRecordSize`
accounts for the first inline weight and checks extra storage before
multiplication. :c:func:`rbcImageResampling.GetFilterSize` caps capacity by source length before narrowing to int, and
:c:func:`rbcImageResampling.GetFilterBounds` clips each inclusive interval before converting its endpoints.

For scale = destination/source and destination coordinate x, center = x/scale. The kernel argument is i + 0.5 - center
for enlargement, and that expression times scale for reduction. Bounds use center +/- radius + 0.5, followed by clipping
and integer truncation. This historical phase convention is not a promise that filtering at unchanged dimensions copies
source pixels exactly. Clipped edge contributions are normalized over retained source samples; there is no mirrored or
wrapped padding.

Weights initially occupy the float member of a union and are summed in double. After normalization, each is replaced by
its integer representation using ``(int)(weight * 16384 + 0.5)``. The same +0.5 applies to negative weights, so
conversion is not symmetric rounding about zero. Quantized weights are not adjusted again to make their integer sum
exactly 16384. A zero floating sum uses a normalization factor of one rather than selecting a fallback pixel.

The horizontal pass requires source and destination heights to match; the vertical pass requires widths to match. They
do not validate these internal shape invariants or support aliased storage. :c:func:`Rbc_ResampleColorImage` constructs
the intermediate and final images to satisfy them. Custom C filters must return finite weights whose normalized
fixed-point values are representable; the kernel does not validate arbitrary callback results. Changing phase,
normalization or rounding requires explicit compatibility tests.

Alpha and color space
---------------------

Filtering operates on straight RGBA bytes. It does not premultiply RGB by alpha before averaging, and it does not
linearize color through gamma conversion. RGB values in transparent pixels can consequently affect edge colors. Do not
confuse this CPU resampler with Cairo's separate premultiplied upload path; see :doc:`cairo-rendering`. Changing either
convention would be a visible behavior change requiring its own tests.

Dimensions and failure policy
-----------------------------

The C helpers require valid image records and valid filter records. Negative output dimensions panic rather than return
a Tcl error; zero output dimensions are supported. The virtual-region helper permits an empty result before checking the
virtual dimensions, but requires positive virtual dimensions for a nonempty region. Empty source regions leave
zero-initialized pixels.

Allocation and contribution-record size checks prevent representational overflow, but these functions are not
recoverable, command-facing validation interfaces. Filter support must be finite and nonnegative, with a valid function
pointer. Tcl commands must validate user input before entering them.

Photo wrappers and command policy
---------------------------------

:c:func:`Rbc_ResizePhoto` and :c:func:`Rbc_ResamplePhoto` use the destination photo's current dimensions as the target
size. Both copy the source region first, so identical source and destination handles are permitted. Temporary RBC images
are released after the final write. The underlying void photo-write wrapper does not expose Tk write failure as a C
return status.

The ``winop`` command in ``rbcWinop.c`` adds policy not present in the kernels: its resample operation rejects source
dimensions at most one, handles unset or matching destination sizes through a direct copy, and selects nearest-neighbour
resizing for ``none``. Do not infer those restrictions from the lower-level C interfaces, which can process single-pixel
dimensions.

Nearest-neighbour implementation
---------------------------------

:c:func:`Rbc_ResizeColorImage` allocates its requested result before checking whether the effective source region is
empty. A nonempty resize precomputes separate int maps for output columns and rows, copies full Pix32 values, then frees
both maps. :c:func:`rbcImageStorage.MapNearestCoordinate` truncates position*span/destinationSpan and adds the clamped
source origin, with a final upper-edge cap. Source-region clipping uses subtraction from a clamped origin rather than
potentially overflowing endpoint addition.

:c:func:`Rbc_ResizeColorSubimage` allocates only the requested region. Its maps add the region origin and output
position in double before virtual scaling, avoiding signed-int coordinate
addition. :c:func:`rbcImageStorage.MapVirtualCoordinate` clamps values beyond the virtual extent to source edges before
integer conversion. Empty output returns before virtual-size validation; nonempty output requires positive virtual
sizes. An empty source leaves the allocated result transparent and zeroed.

The photo resize wrapper takes the destination's existing dimensions, copies the source region, resizes that owned
snapshot, and only then writes the photo. It frees both temporaries even when source and destination handles are equal.
This snapshot ordering is what permits same-photo operation; the raw mapping loops themselves write into independently
allocated images.

Maintenance checks
------------------

Check rectangular and single-pixel images, zero-sized results, source-region origins outside bounds, negative spans, and
virtual crops beyond image edges. For filtered output, exercise reduction and enlargement with different filters on
each axis, constant colors, transparent colored edges and high-contrast patterns. Same-photo source/destination calls
need separate coverage from independent images. Verify ownership and dimensions as well as appearance.
