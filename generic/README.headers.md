# Header dependencies

The [developer manual](../docs/developer/README.md) now provides maintained
architecture chapters and a source-generated renderer reference.

Ordinary RBC headers include the prerequisites for their own declarations.
Include them directly, without preparing an undocumented sequence of other
RBC includes. Include guards allow repeated inclusion. Build configuration
macros and compiler include paths still come from the normal build system.

- Installed API: use `rbc.h` or `rbcVector.h`. Neither requires Tk.
  `rbcDecls.h` is a generated companion included by `rbc.h`, not a
  standalone entry point. Do not edit generated declarations to fix dependencies.
- Resource defaults: `rbcDefaults.h` owns common and platform-specific
  color/font selections. Both internal and Windows drawing headers use it.
  Image channels use explicit `pixel.rgba.red` (and green/blue/alpha)
  members; no generic `Red`, `Green`, `Blue`, or `Alpha` macros leak
  into later system headers.
- Shared private types: `rbcTypes.h` owns padding, geometry, and dash records.
  `rbcText.h` uses these types directly; it does not include `rbcInt.h`.
  This keeps text declarations independent of the internal utility header
  that also uses them.
- Graph components: `rbcGrAxis.h` includes its chain/list prerequisites.
  `rbcGrLegd.h` forward-declares the graph, element, and legend pointer
  types. `rbcGrElem.h` includes `rbcGraph.h` for its shared graph/pen
  declarations. The renderer headers retain their existing graph dependencies.
- Platform configuration: `rbcConfig.h` and `rbcWinConfig.h` describe
  different platforms. Include only the applicable one. `rbcMath.h` and
  `rbcImage.h` select their own configuration prerequisites.
  `rbcWin.h` is Windows-only and includes Tk for its drawing declarations.

Use forward declarations for pointer-only dependencies; include the defining
header when a complete record is needed. Keep type definitions in one place.
Do not solve include-order problems by making two headers include each other.

## Compile-only checks

After configuring, run:

```sh
make test-headers
```

The MSVC makefile also supports `nmake -f makefile.vc test-headers`.
These are explicit developer checks, separate from the runtime Tcl test suite;
they do not load RBC, link an executable, or require a display.

`tests/headerProbes.tcl` discovers headers and generates translation units
for standalone/repeated inclusion, both orders relative to `rbcGraph.h`,
and forward/reverse aggregate inclusion. It checks generated declarations via
the public entry point and rejects an accidental Tk dependency in that entry.
Additional probes declare SDK-style color member names after the image header
and check platform default string sizes in both include orders.
Platform-inapplicable headers are excluded. Probes are compiled with the build's
compiler and flags; run the target in both Cairo-enabled and Cairo-disabled
builds and on both Windows and Unix. The generated sources remain in
`header-probes` for diagnosis and are removed by `make clean`.

These checks detect declaration and include-order failures. They do not replace
runtime tests or establish ABI compatibility on their own.
