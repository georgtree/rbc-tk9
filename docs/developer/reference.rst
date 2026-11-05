Renderer C reference
====================

These declarations are private implementation interfaces, not installed APIs.
Callers must obey the capabilities of the constructor they selected; generic
operation names do not imply that every context implements them.

Parameter directions follow :doc:`calling-conventions`. Context arguments
refer to a specific constructor family; mutable context state does not imply
ownership of borrowed graph, document, image or font resources. See
:doc:`rendering` for batch lifetimes and scope pairing.

Screen adapters and Cairo attempts have different result contracts. Native
one-shot adapters retain the supplied GC behavior, including the explicitly
documented origin/clip mutations. Cairo attempt helpers return a fallback flag;
context constructors return an owned context or their documented failure value.
See :doc:`native-rendering` and :doc:`cairo-rendering` for call sequences and
coordinate conventions.

Component-facing interface
--------------------------

.. doxygenfile:: rbcRender.h
   :sections: briefdescription detaileddescription innerclass define enum func

Auxiliary types
---------------

.. doxygentypedef:: Rbc_RenderPosition

.. doxygentypedef:: Rbc_RenderRunProc

.. doxygentypedef:: Rbc_RenderTarget

The context typedef names the backend record documented below. It is rendered
there once, rather than indexing both the typedef and struct under the same
C identifier.

Backend dispatch
----------------

.. doxygenstruct:: Rbc_RenderContext
   :members:

.. doxygenstruct:: Rbc_RenderOutputOps

.. doxygenstruct:: Rbc_RenderOps

The callback declarations below are included directly from the header. This
preserves their exact C syntax across Doxygen/Breathe versions that otherwise
place function-pointer member names after the pointer declarator.

.. literalinclude:: ../../generic/rbcRenderInt.h
   :language: c
   :start-at: typedef struct {
   :end-at: } Rbc_RenderOps;

Common screen adapters
-----------------------

These private callbacks implement the constructor families described in
:doc:`rendering`. A populated operation table alone does not establish that a
particular constructor initialized every callback's required state. Each
callback's parameter description identifies the required family. Destructors
consume heap contexts; synchronous decoration/detail contexts can be stack-based
and must not be passed to them. See :doc:`rendering` for the internal adapter
call relationships.

.. c:namespace-push:: rbcRenderDispatch

.. doxygengroup:: render_dispatch_internals
   :members:

.. c:namespace-pop::
