Image resizing and resampling
=============================

``rbcImage.c`` provides CPU-side resizing over owned, row-major RGBA images.
These operations do not use a renderer or native drawable. Tk photo wrappers
snapshot pixels into this representation before processing. See
:doc:`images-tiles` for allocation, channel offsets and photo-write ownership.

Three coordinate contracts
--------------------------

``Rbc_ResizeColorImage`` scales a source region to a requested output size.
Its origin is clamped into the source, then negative or excessive width/height
values extend to the remaining edge. This is an origin-and-span policy, not
geometric rectangle intersection: a negative origin becomes zero without
subtracting its displacement from a positive span.

For each destination position, nearest-neighbour mapping selects the truncated
ratio of destination position to destination size, multiplied by the effective
source span, then adds the source origin. It copies the entire pixel, including
alpha. No averaging or antialiasing is performed.

``Rbc_ResizeColorSubimage`` instead takes a region in a *virtual destination*.
The full source is imagined at ``destWidth`` by ``destHeight``, while only the
requested region is allocated. Its origin and each output position map back
through that virtual scale. Positions beyond either edge clamp to the first
or last source pixel; they do not create transparent padding. This avoids
allocating a potentially much larger scaled image merely to display a crop.

``Rbc_ResampleColorImage`` filters the complete source to a new size. Source
region extraction, when needed, happens before this call. It allocates an
intermediate image of destination width by source height, filters horizontally,
then filters vertically into the final image and releases the intermediate.
All three functions leave the borrowed source intact and return independent
storage released with ``Rbc_FreeColorImage``.

Filters and sampling weights
----------------------------

``Rbc_GetResampleFilterFromObj`` resolves names and unambiguous abbreviations
against the static filter table. Returned records are borrowed and must not
be modified or freed. ``none`` resolves successfully to NULL; it directs
command-level policy rather than representing an identity filter that may be
passed to the filtering kernel.

The table includes box, triangle, bell, B-spline, Catmull-Rom, Mitchell,
Lanczos, sinc, Gaussian and other historical filters with individual support
radii. Horizontal and vertical passes can use different non-NULL filters.

``ComputeWeights`` builds one contribution record per destination coordinate,
reused across all rows or columns in that pass. Downsampling broadens the
support radius by the inverse scale; upsampling uses native filter support.
Source support bounds are clipped to the image. Contributions are normalized
by their sum (with a zero-sum fallback factor of one), then converted to
fixed-point weights with 14 fractional bits.

The loops accumulate red, green, blue and alpha independently in 64-bit
integers. Each pass rounds and clamps its output to byte range. The intermediate
image therefore introduces quantization before the second pass; this is not
a single floating-point two-dimensional convolution. Signed filter lobes can
produce overshoot that is clipped at this boundary.

Alpha and color space
---------------------

Filtering operates on straight RGBA bytes. It does not premultiply RGB by alpha
before averaging, and it does not linearize color through gamma conversion.
RGB values in transparent pixels can consequently affect edge colors. Do not
confuse this CPU resampler with Cairo's separate premultiplied upload path;
see :doc:`cairo-rendering`. Changing either convention would be a visible
behavior change requiring its own tests.

Dimensions and failure policy
-----------------------------

The C helpers require valid image records and valid filter records. Negative
output dimensions panic rather than return a Tcl error; zero output dimensions
are supported. The virtual-region helper permits an empty result before
checking the virtual dimensions, but requires positive virtual dimensions for
a nonempty region. Empty source regions leave zero-initialized pixels.

Allocation and contribution-record size checks prevent representational
overflow, but these functions are not recoverable, command-facing validation
interfaces. Filter support must be finite and nonnegative, with a valid
function pointer. Tcl commands must validate user input before entering them.

Photo wrappers and command policy
---------------------------------

``Rbc_ResizePhoto`` and ``Rbc_ResamplePhoto`` use the destination photo's current
dimensions as the target size. Both copy the source region first, so identical
source and destination handles are permitted. Temporary RBC images are released
after the final write. The underlying void photo-write wrapper does not expose
Tk write failure as a C return status.

The ``winop`` command in ``rbcWinop.c`` adds policy not present in the kernels:
its resample operation rejects source dimensions at most one, handles unset or
matching destination sizes through a direct copy, and selects nearest-neighbour
resizing for ``none``. Do not infer those restrictions from the lower-level C
interfaces, which can process single-pixel dimensions.

Maintenance checks
------------------

Check rectangular and single-pixel images, zero-sized results, source-region
origins outside bounds, negative spans, and virtual crops beyond image edges.
For filtered output, exercise reduction and enlargement with different filters
on each axis, constant colors, transparent colored edges and high-contrast
patterns. Same-photo source/destination calls need separate coverage from
independent images. Verify ownership and dimensions as well as appearance.
