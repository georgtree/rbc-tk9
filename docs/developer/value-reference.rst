Value conversion C reference
============================

See :doc:`value-conversion` for lifetime and failure policy.

.. doxygenfunction:: Rbc_GetPixels

.. doxygenfunction:: Rbc_GetPixelsFromObj

.. doxygenfunction:: Rbc_GetPadFromObj

.. doxygenfunction:: Rbc_GetShadowFromObj

.. doxygenfunction:: Rbc_GetDashesFromObj

.. doxygenfunction:: Rbc_GetFillFromObj

.. doxygenfunction:: Rbc_GetStateFromObj

.. doxygenfunction:: Rbc_GetStringListFromObj

.. doxygenfunction:: Rbc_GetXY

.. doxygenfunction:: Rbc_ClampToInt

Private parsing and legacy Tk adapters
--------------------------------------

These string adapters implement ``Tk_CustomOption`` callbacks, not the newer
``Tk_OptionSpec`` transaction protocol. See :doc:`value-conversion` for the
replacement and printer ownership differences.

.. doxygengroup:: rbcConfig_internals
   :members:

.. doxygengroup:: rbcGrMisc_internals
   :members:
