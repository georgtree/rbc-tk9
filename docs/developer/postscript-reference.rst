PostScript token C reference
============================

These private services are declared in ``rbcPs.h`` and implemented in
``rbcPs.c``. See :doc:`postscript-output` for ownership and prolog requirements.

.. doxygenfunction:: Rbc_GetPsToken

.. doxygenfunction:: Rbc_ReleasePsToken

.. doxygenfunction:: Rbc_PostScriptFromToken

.. doxygenfunction:: Rbc_ScratchBufferFromToken

.. doxygenfunction:: Rbc_AppendToPostScript

.. doxygenfunction:: Rbc_FormatToPostScript

.. doxygenfunction:: Rbc_AppendPostScriptString

.. doxygenfunction:: Rbc_FileToPostScript

Legacy drawing primitives
-------------------------

.. c:namespace:: rbcPsPrimitives

.. doxygengroup:: ps_legacy_primitives
   :members:

.. c:namespace-pop::
