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

The splitter also prints an early-stop message to stderr, and empty palette
boxes have a stderr diagnostic. These are not Tcl interpreter results.

Maintenance checks
------------------

Check pixel values as well as dimensions and return codes. Greyscale and gamma
need alpha-preservation checks; convolution needs asymmetric kernels, edge
replication, explicit divisors and opaque-alpha checks. Quantization needs solid
colors with zero-valued channels, five-bit bin boundaries, alpha preservation,
in-place output, fewer available colors than requested, and unchanged output on
invalid arguments. Keep behavior fixes separate from comment-only changes.
