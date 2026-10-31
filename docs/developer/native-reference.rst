Native backend C reference
==========================

These declarations in ``rbcRenderNative.h`` are private backend services.
Components use ``rbcRender.h`` instead. See :doc:`native-rendering` for platform
behavior and ownership that cannot be inferred from a function signature.

Trace batches
-------------

.. doxygenfunction:: Rbc_NativeBeginTrace

.. doxygenfunction:: Rbc_NativeEndTrace

Clip stacks
-----------

.. doxygenfunction:: Rbc_NativeCreateClip

.. doxygenfunction:: Rbc_NativePushClip

.. doxygenfunction:: Rbc_NativePopClip

.. doxygenfunction:: Rbc_NativeDestroyClip
