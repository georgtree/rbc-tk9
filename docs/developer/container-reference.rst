Container C reference
=====================

These interfaces are private. See :doc:`containers` for ownership and traversal.

.. doxygenfunction:: Rbc_ChainInit

.. doxygenfunction:: Rbc_ChainReset

.. doxygenfunction:: Rbc_ChainDestroy

.. doxygenfunction:: Rbc_ChainAllocLink

.. doxygenfunction:: Rbc_ChainUnlinkLink

.. doxygenfunction:: Rbc_ChainDeleteLink

.. doxygenfunction:: Rbc_ListCreateNode

.. doxygenfunction:: Rbc_ListReset

.. doxygenfunction:: Rbc_ListUnlinkNode

.. doxygenfunction:: Rbc_ListDeleteNode

.. doxygenfunction:: Rbc_ListGetNode

.. doxygenfunction:: Rbc_ListSort

Implementation helpers
----------------------

.. doxygengroup:: list_internals
   :members:

Creation, insertion and lookup
------------------------------

.. doxygenfunction:: Rbc_ChainCreate

.. doxygenfunction:: Rbc_ChainNewLink

.. doxygenfunction:: Rbc_ChainAppend

.. doxygenfunction:: Rbc_ChainPrepend

.. doxygenfunction:: Rbc_ChainLinkAfter

.. doxygenfunction:: Rbc_ChainLinkBefore

.. doxygenfunction:: Rbc_ListInit

.. doxygenfunction:: Rbc_ListCreate

.. doxygenfunction:: Rbc_ListDestroy

.. doxygenfunction:: Rbc_ListAppend

.. doxygenfunction:: Rbc_ListPrepend

.. doxygenfunction:: Rbc_ListLinkAfter

.. doxygenfunction:: Rbc_ListLinkBefore

.. doxygenfunction:: Rb_ListDeleteNodeByKey

.. doxygenfunction:: Rbc_ListGetNthNode
