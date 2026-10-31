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

See :doc:`image-tile-reference` for the maintained C contracts. Resampling,
quantization, bitmap transforms and temporary-image compatibility helpers are
separate algorithm/implementation topics.
