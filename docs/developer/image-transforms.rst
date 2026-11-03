Color-image rotation and bitmap transforms
==========================================

Color images and monochrome bitmaps use different transformation paths.
``rbcImage.c`` operates on CPU-side RGBA storage; ``rbcWinImage.c`` and
``rbcUnixImage.c`` operate on native depth-one pixmaps. These services are
private image utilities used by components and native renderers, not additional
renderer backends. See :doc:`images-tiles` for storage ownership and
:doc:`image-resampling` for ordinary resizing.

Color-image rotation
--------------------

``Rbc_RotateColorImage`` borrows the source and always returns independent
storage. A zero-degree operation still copies the image. Empty input is copied
before quadrant handling, retaining its dimensions. Angles must be finite and
are normalized modulo 360 degrees; positive rotation is counterclockwise in
display coordinates.

The function first selects a multiple of 90 degrees, leaving a residual angle
in the range -45 to 45 degrees. Exact quadrant rotations copy complete pixels
without interpolation; 90 and 270 degrees exchange width and height. For
example, the 90-degree path maps source pixel ``(x, y)`` to
``(y, sourceWidth - x - 1)``.

A nonzero residual goes through ``Rotate45``. Despite that helper's name,
its argument is radians, not degrees. It performs a row shear, a column shear,
and a final row shear, using tangent of the half-angle and sine of the angle.
Two intermediate images are released internally. The public function also
releases any quadrant-rotated temporary, leaving only the final owned result.

Each shear blends neighboring contributions with fixed-point arithmetic and
clamps channels to byte range. Background is transparent white, including its
RGB values: it is not transparent black. RGB and alpha are processed separately,
without premultiplication or gamma conversion. This affects partially transparent
edge pixels, as discussed for filtering in :doc:`image-resampling`.

Dimensions are derived from the three-shear construction, with checked integer
conversion and extra space in the later passes. Do not substitute a generic
rotated bounding-box formula and assume identical dimensions or pixels.
Non-finite angles and unrepresentable rotation dimensions panic; command-level
validation remains the caller's responsibility.

Quadrant selection and intermediate geometry
--------------------------------------------

After normalization into [0, 360), the dispatcher selects these paths:

.. list-table::
   :header-rows: 1
   :widths: 30 35 35

   * - Normalized angle
     - Initial exact rotation
     - Residual angle
   * - [0, 45]
     - None (copy if zero)
     - angle
   * - (45, 135]
     - 90 degrees
     - angle - 90
   * - (135, 225]
     - 180 degrees
     - angle - 180
   * - (225, 315]
     - 270 degrees
     - angle - 270
   * - (315, 360)
     - None
     - angle - 360

The endpoint choices are intentional: 45 degrees uses no quadrant copy,
while 135 degrees uses a 90-degree copy followed by a positive 45-degree
residual. Changing which exact rotation precedes a shear can change intermediate
rounding even when the final mathematical angle is equivalent.

For source width W and height H *after* any quadrant copy, Rotate45 uses
s = sin(theta), c = cos(theta), and t = tan(theta/2). Its image sizes are:

.. list-table::
   :header-rows: 1
   :widths: 25 40 35

   * - Stage
     - Width
     - Height
   * - First row shear
     - truncate(W + H*abs(t))
     - H
   * - Column shear
     - First-stage width
     - truncate(W*abs(s) + H*c + 1)
   * - Final row shear
     - truncate(H*abs(s) + W*c + 1)
     - Second-stage height

GetRotationDimension checks finiteness and the nonnegative int range before
truncation. It does not ceil the result. GetShearOffset floors the displacement,
including negative values, and checks the int range. The fractional argument
is the original displacement minus that integer offset, so it lies in [0, 1)
for valid geometry. The caller's residual-angle restriction keeps the
construction within the intended two-neighbor filtering regime.

Shear traversal and boundary pixels
-------------------------------------

The historical helper names describe the fixed coordinate: ShearY processes
a row at fixed y and moves pixels horizontally; ShearX processes a column at
fixed x and moves pixels vertically. Each initializes its complete destination
row or column to the background before sampling. The source and destination
must be distinct, since that initialization would overwrite an aliased source.

Each source channel contributes a fraction computed with the same 14-bit
fixed-point macros used by resampling. The output combines the current pixel
with the carried fraction from its predecessor and clamps to a byte. The carry
starts from the background channel value; after the last source pixel, an
additional residual pixel is emitted if it falls within the destination.
The routines still advance carry state for clipped source contributions.
Clipping therefore limits writes without restarting the filter at the first
visible pixel.

These are private kernels with caller-established invariants: the fixed row
or column exists in both images, the fraction is in range, and advancing the
integer displacement across the source remains representable. They validate
individual destination writes, not arbitrary source indices or caller-provided
fractions. The public rotation path supplies matching intermediate dimensions.
Each pass clamps independently, and transparent white participates in straight
RGBA filtering. It must not be silently replaced with premultiplied or
transparent-black interpolation during refactoring.

The first intermediate is freed after the column shear; the second is freed
after the final row shear. A quadrant-rotated temporary belongs to the outer
dispatcher and is freed only when a further residual rotation replaces it.
With an exact quadrant angle, that temporary itself becomes the returned image.
Zero rotation returns a full copy. Empty input bypasses all shears and retains
its original dimensions, even when the requested angle would normally exchange
them.

One-bit bitmap ownership
------------------------

Bitmap transforms borrow a valid Tk window and a source pixmap whose actual
size matches the supplied dimensions. Successful results are newly owned
pixmaps, released using ``Tk_FreePixmap`` on the corresponding display. They
are not ``Rbc_ColorImage`` objects and must not be freed with ``ckfree`` or
``Rbc_FreeColorImage``.

Rotation computes a containing bounding box and uses special cases for exact
right angles. General angles map destination pixels back to source coordinates;
samples outside the source are left clear. Unlike color rotation, this is
discrete one-bit sampling with no partially covered pixels.

X11 implementations obtain source and destination ``XImage`` objects, populate
the destination and upload it with ``XPutImage``. Temporary XImages are released
with ``XDestroyImage``. Windows rotation works through monochrome bitmap bytes
and DIB metadata, including DWORD-aligned scanlines and checked buffer sizes.
Those byte-layout rules must not be replaced with packed RGBA assumptions.

Scaling and virtual regions
---------------------------

``Rbc_ScaleBitmap`` returns the full requested output. X11 explicitly samples
nearest source pixels. Windows uses ``StretchBlt`` with ``SRCCOPY`` through
paired drawable-DC acquisition and release. These are separate implementations;
do not infer identical downscaling details from their shared C signature.

``Rbc_ScaleRotateBitmapRegion`` avoids allocating the full virtual output.
It uses the rotated source bounding box and the virtual destination dimensions
to map a requested region back to source pixels. The returned bitmap is only
``regionWidth`` by ``regionHeight``. Bitmap markers use this route for the
visible portion of a larger transformed marker, applying the same transform
to its mask when needed.

The region must lie within the virtual destination, and coordinate additions
must remain representable. The right-angle paths read source samples directly;
they do not implement general out-of-range padding or edge clamping. This is
unlike ``Rbc_ResizeColorSubimage``, which explicitly clamps virtual coordinates
to source edges. Keep these contracts distinct when sharing callers.

Failure and platform differences
--------------------------------

``None`` indicates a detected bitmap-transform failure, but failure checking
is not uniform. X11 scaling rejects nonpositive dimensions explicitly;
Windows scaling relies on its caller and native operations. Windows scaling
also returns its allocated pixmap without propagating ``StretchBlt`` failure.

The rotation dimension outputs are not portable failure indicators. X11 writes
them on success; Windows clears them after initial argument validation and
before later operations. Always check the returned pixmap first, and require
non-NULL output pointers on both platforms. These helpers do not provide a
uniform Tcl diagnostic interface.

Maintenance checks
------------------

Use an asymmetric pattern to detect mirroring and rotation direction. Check
zero, positive and negative right angles, angles near quadrant boundaries,
non-square images and arbitrary rotations. For color images, inspect RGBA
values at transparent edges as well as appearance. For bitmaps, exercise
scanline widths around byte and DWORD boundaries, scaling and contained virtual
regions on both Windows and X11. Verify returned dimensions, source preservation
and matching resource cleanup independently of the rendered result.
