Image storage and tile clients
==============================

Images cross several ownership boundaries. ``rbcImage.c`` supplies owned pixel
buffers and conversion helpers; ``rbcTile.c`` caches a Tk image's native drawing
resources for multiple clients. Components retain their own option and redraw
responsibilities. These are private services, not the installed vector API.

Pixel storage and Tk images
---------------------------

.. list-table::
   :header-rows: 1
   :widths: 26 74

   * - Representation
     - Lifetime
   * - ``Tk_Image``
     - A Tk image instance acquired by its owner. A borrowed instance, such as
       one returned by a tile, must not be freed by the borrower.
   * - ``Tk_PhotoImageBlock``
     - A descriptor whose pixel pointer normally borrows Tk photo storage.
       Copying the descriptor does not copy pixels or retain the image.
   * - ``Rbc_ColorImage``
     - An owned record plus a separate contiguous ``Pix32`` array, released with
       ``Rbc_FreeColorImage``. It has no Tk name or notification mechanism.
   * - Tile pixmap
     - A server-owned native cache. Clients borrow it only until update/release.

``Pix32`` provides explicit red, green, blue and alpha byte fields. Use these
fields and the image access macros; do not infer a portable packed integer
channel order from the union's ``value`` member. New color images are zeroed,
including alpha, so newly allocated pixels are transparent until populated.
Dimensions are nonnegative and allocation arithmetic is checked; invalid or
unrepresentable sizes panic rather than return a Tcl error. Even an empty image
has an owning record. The free helper requires a valid non-NULL image.

Photo conversion copies rows using Tk's pitch, pixel size and channel offsets.
``Rbc_PhotoToColorImage`` returns an independent snapshot.
``Rbc_PhotoRegionToColorImage`` clips its requested region to the photo and
returns an empty image for an empty intersection. Later photo changes do not
update either snapshot.

``Rbc_ColorImageToPhoto`` resizes the destination and writes using
``TK_PHOTO_COMPOSITE_SET``. Tk copies the supplied block; the caller keeps and
can subsequently free the RBC image. This legacy void wrapper ignores the
return statuses of the Tk resize/write operations. Callers requiring reliable
error propagation must not treat its return as evidence of success.

Shared tile servers, individual clients
---------------------------------------

``Rbc_GetTile`` returns a fresh client token on every successful acquisition.
Servers are cached in interpreter associated data, keyed by display, depth and
interned image name. The key structure is zero-initialized because the hash
includes its padding bytes. Identical names in different interpreters do not
share a server.

The server owns the Tk image instance, cached pixmap, tiling GC, transparency
mask and client chain. Each client stores its borrowed window, callback/data,
origin and chain link. The caller must release its client before destroying the
window or callback data it uses. The last client release destroys the server;
interpreter teardown destroys remaining servers and clients. Pending tile idle
updates are cancelled during server destruction.

``Rbc_FreeTile(NULL)`` is harmless, but the magic field is only a sanity check;
it cannot make use-after-free or a repeated free safe. No client token remains
valid after interpreter teardown.

Idle refresh and notification
-----------------------------

Tk image changes are coalesced into one ``UpdateTile`` idle callback using
``TILE_NOTIFY_PENDING``. The update clears the pending flag, redraws the cache
using a client window, then walks the client chain and calls installed callbacks.
Resizing allocates the replacement pixmap before releasing the old one; redraw
also refreshes the GC and photo transparency mask.

If the Tk image has been deleted, the update releases the cached pixmap and
sets it to ``None``. It does not reset every cached field: size queries can still
report the last dimensions. Query helpers do not force pending idle work.
``Rbc_GetTilePhoto`` distinguishes a deleted image (true with an empty block)
from a non-photo image (false). For a live photo its returned pixels are borrowed
until the next image change, not until the next idle notification.

Notification snapshots preserve the server and each current client record.
Callbacks may release themselves, remove other clients, add clients or process
nested updates. Removed clients are skipped; added clients join a later update.
Callback replacement takes effect when that client's turn is reached. Removing
the last client tears down resources immediately, while record reclamation waits
until active snapshots release them. Each nested pass owns its own snapshot.
Interpreter deletion stops the current pass; interpreter preservation delays its
final teardown until active calls unwind. Callback data is still caller-owned.
Existing graph/line callbacks simply schedule redraw work.

Origin and renderer boundaries
------------------------------

``Rbc_SetTileOrigin`` translates through the Tk window hierarchy to align the
pattern with the toplevel; ``Rbc_SetTSOrigin`` installs the supplied origin
directly. Both store client coordinates and update the server GC. Since the GC
is shared, consumers must establish the intended origin for drawing rather than
assume it is permanent per-client state.

Native tile drawing remains platform-specific: Windows uses DC/pattern/mask
operations, while X11 uses tiled GCs and temporary clipping masks. Renderer
consumers choose the appropriate drawing/export route. Export code borrowing a
photo block must honor pitch, channel offsets and alpha rather than assuming a
packed native pixmap has equivalent storage. See :doc:`rendering` and
:doc:`exports` for those boundaries.

Cleanup and regression coverage
-------------------------------

Server destruction releases the retained transparency mask with the same
platform-specific allocator pairing used during redraw. The last client release
also cancels pending idle updates. Clients must never free server-owned resources
themselves.

Run ``make test-tile`` (with a Tk display) for the private C harness. It covers
self/other/final-client deletion, callback replacement, client addition, nested
idle notification, interpreter deletion, mask release and pending-idle teardown.
The normal suite skips this harness unless ``RBC_TILE_TEST_LIB`` is supplied;
it runs in a separate process and adds no production Tcl command or public stub.
The void photo-write wrapper described above still lacks status propagation.

See :doc:`image-tile-reference` for the maintained C contracts and
:doc:`image-resampling` for resizing and filtering. Bitmap
transforms are covered in :doc:`image-transforms` and color processing in
:doc:`image-colors`. See :doc:`image-compatibility` for temporary-image and private Tk adapters.

Registry and refresh implementation
-----------------------------------

Despite the TILE_THREAD_KEY macro name, the registry belongs to interpreter
associated data. TileInterpData owns the table; it is not process-global or
thread-local sharing. A server is registered only after image acquisition and
initial redraw succeed. The first client is appended after that registration.
Failure to acquire the Tk image removes the newly reserved hash entry.

There are two distinct release phases. DestroyClient removes membership and
invalidates the token immediately, and DestroyTile releases image/native
resources immediately. FreeTileRecord later reclaims only record storage once
notification snapshots release their preserves. Neither preserve extends the
lifetime of a window, image resource, callback data or client chain link.

UpdateTile snapshots membership after refreshing the cache. It deliberately
does not snapshot callback pointers, so replacing another client's callback
changes that client's delivery in the same pass. It also does not save the
interpreter result around callbacks. Clients which evaluate Tcl must apply
their own result-preservation policy where needed.

The refresh path has a different callback boundary from notification delivery.
Tk_GetImage and Tk_RedrawImage can invoke an image provider. Initial redraw
occurs before any client is attached; subsequent redraw uses the first client's
borrowed window before membership is snapshotted. The client-notification
preserves do not by themselves make arbitrary provider reentrancy safe. This
is a separate lifetime boundary to review when extending supported image types.

Redraw replaces a size-dependent pixmap before releasing its predecessor, then
refreshes the tiled GC, dimensions and optional photo mask. A deleted image's
idle update only clears the pixmap; it does not reset the cached dimensions,
mask or GC at that point. Final server destruction releases those resources.
Consumers must use the deleted-image-aware helpers rather than infer liveness
from cached dimensions alone.

Native shape drawing
--------------------

Tile draw calls consume the existing native cache. They do not run pending
image updates, retain the client window or produce an error status. The caller
must provide a live client and drawable with compatible depth/display and a
usable pixmap. Windows repetition additionally requires positive tile width
and height. A deleted image with stale cached dimensions is not a valid tile
source merely because its size query is nonzero.

Rectangle helpers consistently ignore None drawables and nonpositive sizes;
batch helpers also ignore NULL arrays and nonpositive counts. Polygon callers
must provide nonempty arrays: Windows computes bounds from the first point,
and X11's masked path does likewise. These are XPoint/XRectangle interfaces,
so geometry has already crossed the native short-coordinate boundary.

.. list-table:: Platform drawing strategy
   :header-rows: 1
   :widths: 22 39 39

   * - Shape
     - Windows
     - X11
   * - Rectangle
     - Repeated BitBlt copies aligned to the client origin.
     - XFillRectangle with the server's tiled GC.
   * - Rectangle batch
     - One source/destination DC setup for the whole batch.
     - Opaque requests are chunked to the server request limit; masked
       rectangles are handled separately.
   * - Polygon
     - Polygon region clips repetitions over the inclusive bounding box.
     - XFillPolygon uses Complex and CoordModeOrigin; transparency adds a
       polygon-sized one-bit clip mask.

Windows TileRegion aligns the first repetition at or before the destination
origin, then crops color copies along the boundary. Opaque copies use SRCCOPY.
Masked copies use SRCAND followed by SRCPAINT, with destination text/background
colors set for monochrome expansion. Source and mask DCs are borrowed by this
inner helper; the enclosing shape function owns acquisition, selection and
release. It restores the old source bitmap before deleting the memory DC.

Windows masked copies use the same source coordinates for the mask and color
passes, including cropped first rows and columns. The private tile harness
checks resulting pixels against the tile origin for both positive and negative
origins, including untouched pixels outside the requested rectangle.

X11 RectangleMask translates the client origin into rectangle-local coordinates
and fills an owned one-bit pixmap with FillOpaqueStippled. PolygonMask translates
copied points into bounding-box coordinates, clears its bitmap, then uses
FillStippled for the polygon interior. Both release temporary GCs and return
pixmaps which the drawing caller must free. Transparency is represented by
binary masks here, not continuous-alpha compositing.

Masked X11 drawing temporarily installs that mask and its destination origin on
the shared tile GC, then explicitly resets the clip to None and origin to zero.
It does not save a caller's previous server-GC clip. Likewise, origin setters
change shared GC state, while Windows repetition reads the individual client's
stored origin. Consumers must establish the intended drawing state rather than
assume a prior client's origin or clip is isolated. The outer renderer remains
responsible for its own clipping policy.
