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
