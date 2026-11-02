Windows native backend C reference
==================================

Windows contracts use a separate Doxygen XML project so identical backend
function and structure names cannot merge with the X11 implementation.

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
