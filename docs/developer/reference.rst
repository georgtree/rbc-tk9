Renderer C reference
====================

These declarations are private implementation interfaces, not installed APIs.
Callers must obey the capabilities of the constructor they selected; generic
operation names do not imply that every context implements them.

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
