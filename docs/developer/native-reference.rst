Native backend C reference
==========================

These declarations in ``rbcRenderNative.h`` are private backend services.
Components use ``rbcRender.h`` instead. See :doc:`native-rendering` for platform
behavior and ownership that cannot be inferred from a function signature.

Trace batches
-------------

The X11 constructor and destructor contracts are included with the platform
implementation below. See :doc:`native-rendering` for the shared lifetime
contract and Windows acquisition/restoration requirements.

Clip stacks
-----------

.. doxygenfunction:: Rbc_NativeCreateClip

.. doxygenfunction:: Rbc_NativePushClip

.. doxygenfunction:: Rbc_NativePopClip

.. doxygenfunction:: Rbc_NativeDestroyClip

Shared drawing implementation
-----------------------------

The source contracts below describe conversion, temporary storage, GC mutations
and fallback behavior. These helpers consume mapped geometry; they do not own
component resources or perform graph layout.

.. c:namespace:: rbcNativeShared

.. doxygengroup:: native_shared_drawing
   :members:

.. doxygenstruct:: NativeClipFrame
   :members:

.. doxygenstruct:: Rbc_NativeClip
   :members:

.. c:namespace-pop::

X11 platform implementation
---------------------------

These source contracts describe the X11 implementation. The common interface
also has Windows implementations with different resource and batching rules.

.. c:namespace:: rbcNativeX11

.. doxygengroup:: native_x11_drawing
   :members:

.. doxygenstruct:: Rbc_NativeTrace
   :members:

.. doxygenstruct:: Rbc_NativePolygonBatch
   :members:

.. c:namespace-pop::
