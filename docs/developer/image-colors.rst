Image color processing
======================

``rbcImage.c`` contains greyscale conversion, gamma correction, a square-kernel
filter and RGB palette reduction. These CPU-side operations work on RBC color
images, without renderer state or Tk callbacks. Their ownership and alpha
policies differ; they are not interchangeable image transformations.

Mutation and alpha
------------------

.. list-table::
   :header-rows: 1
   :widths: 30 35 35

   * - Operation
     - Destination
     - Alpha policy
   * - Greyscale
     - Mutates the supplied image
     - Preserved per pixel
   * - Gamma correction
     - Mutates the supplied image
     - Preserved per pixel
   * - Convolution
     - Newly allocated, same-size image
     - Set to 255 for every output pixel
   * - Quantization
     - Existing equal-size image; may alias source
     - Copied from the source pixel

Caller-owned source images are never released by these routines. Convolution
returns storage released with ``Rbc_FreeColorImage``. Quantization allocates
only temporary working storage and does not resize its destination.
See :doc:`images-tiles` for the underlying storage contract.

Greyscale and gamma
-------------------

Greyscale uses ``0.212671 * R + 0.715160 * G + 0.072169 * B``, clamps to byte
range and truncates, then writes the same byte to all three color channels.
It operates directly on stored channel values without first linearizing them.

``Rbc_GammaCorrectColorImage`` builds a 256-entry lookup table using
``255 * pow(value / 255, 1 / gamma)``. It clamps and truncates each entry before
applying the table independently to RGB. Gamma must be finite and positive;
invalid values panic. Neither operation changes alpha or dimensions.

Square-kernel filtering
-----------------------

``Rbc_ConvolveColorImage`` interprets ``Filter2D.support`` as a truncated integer
radius with a minimum of one. Even support zero therefore requires a 3-by-3
kernel. The caller provides a complete row-major square kernel; the structure
contains no length with which the function could verify that allocation.

At each output pixel, the code visits the neighborhood from top-left to
bottom-right and consumes kernel coefficients in stored order. It does not
reverse the kernel, so the operation is correlation in mathematical terminology;
this distinction matters for asymmetric kernels. Coordinates outside the image
repeat the nearest edge pixel.

RGB sums are divided by ``Filter2D.sum``, clamped and truncated. The function
does not calculate that divisor from the coefficients, and ``Filter2D.scale``
is unused. Output alpha is opaque, regardless of source transparency. There is
no gamma conversion or premultiplied-alpha processing.

The implementation rejects missing kernel pointers, non-finite or negative
support, excessively large support and non-finite or zero divisors by panicking.
It does not validate coefficient values or kernel allocation length. Callers
must supply finite coefficients and arithmetic that remains representable;
these are private C preconditions, not Tcl error handling.

Palette reduction
-----------------

``Rbc_QuantizeColorImage`` accepts a requested color count from 1 through 256
and an existing destination with exactly matching dimensions. It returns
``TCL_ERROR`` for invalid count or mismatched dimensions without changing pixels
or producing an interpreter diagnostic. Empty images return ``TCL_OK``.

The algorithm accumulates a 33-by-33-by-33 histogram with five bits per RGB
channel and a zero boundary plane for cumulative moments. It records sample
counts, channel sums and squared-color sums, then converts them into cumulative
moments for box queries. Alpha is excluded from statistics: RGB in transparent
pixels contributes equally to palette selection.

Color-space boxes are split using variance reduction. The requested count is
an upper bound; splitting stops when no positive variance remains. Each box
receives a representative color derived from its channel sums and population.
There is no dithering or error diffusion. The histogram, box array and lookup
table are released during the operation. Mapping can run in place because all
statistics and palette construction finish before output overwrites pixels.

Palette lookup coordinates
--------------------------

Histogram construction, lookup-table population and pixel mapping all address
channels as ``(value >> 3) + 1``. Index zero is reserved for cumulative-moment
boundaries and is not a populated palette cell. Preserve this offset in every
stage: omitting it during mapping selects the preceding bin or a zero-filled
boundary cell, turning colors with low-valued channels black.

An empty palette box has a defensive stderr diagnostic and receives black;
ordinary splitting rejects empty halves. Early termination below the requested
palette size is normal and no longer emits a message. Neither case produces
a Tcl interpreter diagnostic.

Quantizer stage contracts
-------------------------

The implementation follows the Wu-style greedy orthogonal partitioner credited
in the source. Its statistics record has five signed 64-bit arrays: population,
three channel sums and squared-color sums. The squared sums use original
8-bit RGB values even though bin addresses use five bits. GetColorImageStatistics
owns the initial allocation; M3d transforms it in place into prefix sums once.
Reapplying M3d would accumulate already cumulative data and corrupt queries.

Each Cube describes a lower-exclusive, upper-inclusive interval on each axis.
The initial cube is (0, 32] on all three axes. Its geometric volume counts
histogram cells, not pixels. Volume uses eight-corner inclusion-exclusion to
query any one cumulative array. Bottom and Top split that expression into a
cut-independent term and a term containing the proposed upper boundary.

Variance is the squared-color sum minus the sum of squared channel totals
divided by population. It represents total squared error, or variance times
population. Maximize omits the squared-color term because it is constant across
a partition, and maximizes the combined mean-color scores of the two halves.
Candidate cuts with an empty half are skipped. Strict score comparison keeps
the earliest winning cut; Cut resolves equal axis scores in red, green, blue
order. These rules can affect reproducibility even when several partitions
have the same mathematical quality.

SplitColorSpace owns a temporary variance array and writes into the caller's
cube array. It splits the selected box, updates the two variances and chooses
the first box with the largest positive variance. A failed cut disables that
box and retries the unused output slot. Geometric one-cell boxes are not split
again. Their pixels can still differ within the same five-bit bin, so asking
for more colors does not guarantee recovery of all distinct 8-bit RGB values.
The requested color count is a cap, not an exact palette-size promise.

CreateColorLookupTable computes each populated box's channel means using
integer division. The legacy multiply-by-257 and right-shift-by-eight sequence
preserves those byte means; it does not introduce fractional rounding. Mark
fills every cell in the box with the packed representative pixel. Mapping uses
the partition cell directly rather than choosing the nearest palette color.
This table is private native Pix32 storage, not a portable serialized palette.

All histogram and palette construction finishes before destination pixels are
written. Statistics and cubes are freed before MapColors, and the lookup table
is freed afterward. MapColors saves source alpha before overwriting a pixel,
which permits exact in-place operation. It relies on the entry point's equal-size
validation; it is not independently safe for arbitrary destination dimensions
or partially overlapping storage.

The fixed histogram grid bounds indexing and geometric-volume arithmetic, but
moment accumulation still requires representable signed 64-bit sums. Floating
variance and score calculations use double and do not implement exact rational
comparison or clamp cancellation artifacts. These helpers are private stages,
not individually validating APIs for external callers.

Maintenance checks
------------------

Check pixel values as well as dimensions and return codes. Greyscale and gamma
need alpha-preservation checks; convolution needs asymmetric kernels, edge
replication, explicit divisors and opaque-alpha checks. Quantization needs solid
colors with zero-valued channels, five-bit bin boundaries, alpha preservation,
in-place output, fewer available colors than requested, and unchanged output on
invalid arguments. Keep behavior fixes separate from comment-only changes.
