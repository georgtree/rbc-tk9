Header structure
================

Ordinary headers include the prerequisites for their own declarations.
Callers should not need an undocumented sequence of includes.

.. list-table::
   :header-rows: 1
   :widths: 32 68

   * - Header
     - Role
   * - ``rbc.h``, ``rbcVector.h``
     - Installed entry points with no Tk dependency.
   * - ``rbcDecls.h``
     - Generated companion included through ``rbc.h``.
   * - ``rbcTypes.h``
     - Shared padding, geometry and dash records.
   * - ``rbcDefaults.h``
     - Common and platform-specific resource defaults.
   * - ``rbcText.h``
     - Text declarations using shared types, without including ``rbcInt.h``.
   * - ``rbcGraph.h``, ``rbcGrTrack.h``
     - Common graph definitions; track records and services respectively.
   * - ``rbcRender.h``
     - Component-facing private rendering contracts.
   * - ``rbcRenderInt.h``
     - Backend context prefix and operation tables.
   * - ``rbcRenderNative.h``
     - Native services with opaque platform resources.

Use forward declarations for pointer-only dependencies and the defining header
for embedded records. Define each type once. Include guards make repeated
inclusion safe; reciprocal includes do not solve dependency cycles.

Only include the applicable platform configuration. ``rbcWin.h`` is
Windows-only. Pixel channels use explicit members such as ``pixel.rgba.red``;
generic macros named ``Red`` or ``Blue`` would alter later SDK declarations.

Run ``make test-headers`` after dependency changes. Its compile-only probes
cover standalone, repeated, forward/reverse and graph-relative inclusion,
public-header Tk isolation, and image/default-name regressions.
Run them on both platforms and with both Cairo configurations.
These checks complement runtime tests; they do not establish ABI compatibility.
