Windows native backend C reference
==================================

Windows contracts use a separate Doxygen XML project so identical backend
function and structure names cannot merge with the X11 implementation.
Parameter descriptions identify retained DC/batch state, caller-owned clip
payloads, and temporary drawables. Return values that select fallback are not
interchangeable with drawing-success reports; see :doc:`native-rendering`.

For a retained polygon batch, pair
:c:func:`rbcNativeWindows.Rbc_NativeBeginPolygonBatch` with
:c:func:`rbcNativeWindows.Rbc_NativeEndPolygonBatch`. For direct clipping,
:c:func:`rbcNativeWindows.Rbc_NativePlatformUnclip` must unwind even partial
setup from :c:func:`rbcNativeWindows.Rbc_NativePlatformClip`.

.. c:namespace:: rbcNativeWindows

.. doxygengroup:: native_windows_drawing
   :project: rbc-win
   :members:

.. doxygenstruct:: Rbc_NativeTrace
   :project: rbc-win
   :members:

.. doxygenstruct:: Rbc_NativePolygonBatch
   :project: rbc-win
   :members:

.. doxygenstruct:: NativeDCClip
   :project: rbc-win
   :members:

.. c:namespace-pop::

Tk Windows resource boundary
----------------------------

These compatibility services confine private Tk drawable/DC representations to
``rbcWinDraw.c``. Components use the renderer interface rather than these helpers.

.. c:namespace:: rbcWindowsResources

.. doxygengroup:: windows_drawing_resources
   :project: rbc-win
   :members:

.. doxygenstruct:: Rbc_WinDrawableDC
   :project: rbc-win
   :members:

.. c:namespace-pop::

Compatibility drawing primitives
--------------------------------

These entries implement the Windows Xlib-shaped drawing boundary. They consume
mapped integer geometry and configured GCs, with the historical conversions
and fallback policies described in :doc:`native-rendering`.

.. c:namespace:: rbcWindowsPrimitives

.. doxygengroup:: windows_drawing_primitives
   :project: rbc-win
   :members:

.. c:namespace-pop::

Bitmap capture and transforms
-----------------------------

Windows-specific image contracts are extracted without the same-named shared
header declarations. See :doc:`images-tiles` for common image ownership and
:doc:`native-rendering` for platform representations and limits.

.. c:namespace:: rbcWindowsImages

.. doxygengroup:: windows_bitmap_images
   :project: rbc-win
   :members:

.. c:namespace-pop::

Native window handle lookup
---------------------------

.. c:namespace:: rbcWindowsHandles

.. doxygengroup:: window_windows
   :project: rbc-win
   :members:

.. c:namespace-pop::

DC and handle ownership
-----------------------

:c:func:`rbcWindowsResources.Rbc_WinAcquireDrawableDC` returns a release record even
when acquisition fails. Pass that record to
:c:func:`rbcWindowsResources.Rbc_WinReleaseDrawableDC`; restoring explicitly selected
GDI objects remains the caller's responsibility. In contrast,
:c:func:`rbcWindowsResources.Rbc_WinCreateDrawableFromDC` wraps an already borrowed DC:
:c:func:`rbcWindowsResources.Rbc_WinFreeDrawableFromDC` releases only the wrapper.

:c:func:`rbcWindowsResources.Rbc_WinCreatePixmapFromBitmap` transfers a native bitmap
into Tk pixmap ownership. A handle returned by
:c:func:`rbcWindowsResources.Rbc_WinGetPixmapHandle` is borrowed and must not be
independently deleted. These ownership rules differ from the transient static
window wrapper returned by :c:func:`rbcWindowsResources.Rbc_WinWindowFromHWND`.

Tiled shapes
-------------

These contracts describe the Windows DC implementation, separately from the
same-named X11 helpers in :doc:`image-tile-reference`. Origins and mask/color
source offsets must agree across cropped copies; see :doc:`images-tiles`.

.. c:namespace:: rbcWindowsTiles

.. doxygengroup:: tile_windows_drawing
   :project: rbc-win
   :members:

.. c:namespace-pop::
