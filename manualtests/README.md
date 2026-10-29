# Manual visual tests

The manual suite complements `tests/RBC.*.A.test`. It exercises appearance, mouse interaction and platform-dependent
display behavior. Successful script execution is not a visual pass.

## Running tests

Use Tcl/Tk with the current RBC build available on `auto_path`. Every top-level, component and topic runner accepts the
same command-line options:

```sh
# Whole suite, native renderer, three seconds between stages:
tclsh91 manualtests/runAll.tcl -renderer native -delay 3000

# One component:
tclsh91 manualtests/polar/runAll.tcl -renderer cairo -delay 1000

# One topic:
tclsh91 manualtests/graph/errorbandRunAll.tcl -renderer native -delay 3000

# List options without loading Tk or requiring a display:
tclsh91 manualtests/runAll.tcl -help
```

`-renderer` accepts `native` or `cairo` and defaults to `native`. Requesting Cairo on a build without Cairo support
fails before any cases run. `-delay` is a non-negative integer in milliseconds, defaults to `200`, and applies between
Setup, Body and Cleanup. Use `0` for an execution smoke check; use a longer delay or individual cases for visual review
and mouse interaction.

Options are parsed once by the outermost runner and inherited by nested runners.  Each runner finds its fixtures
relative to its own file, so the command may be launched from any working directory. The caller's working directory is
restored afterwards, including on failure. A direct command-line run closes the empty Tk root window at completion;
sourcing a runner keeps the caller's root window.

To source a runner from an existing Tcl/Tk console, set its argument list first:

```tcl
set argv {-renderer native -delay 3000}
source manualtests/trackgraph/runAll.tcl
```

For careful before/after inspection, invoke a case manually:

```tcl
cd manualtests/graphtoolbar
source RBC.graphtoolbar.controls.M.tcl
::graphtoolbar.controls::RBC.graphtoolbar.controls.M.1.3.Setup
# Inspect, zoom individual tracks, click local legends and drag dividers.
::graphtoolbar.controls::RBC.graphtoolbar.controls.M.1.3.Body
# Inspect again after track reordering.
::graphtoolbar.controls::RBC.graphtoolbar.controls.M.1.3.Cleanup
```

Files use `package provide component.topic`, RBC command imports and numbered `RBC.component.topic.M.major.minor.Setup`,
`.Body`, and `.Cleanup` procedures in the component namespace. Case comments describe the intended behavior.

## Reading the output

- `OPTIONS` reports the selected renderer and delay once per outermost run.
- `COMPONENT` identifies the current component suite.
- `CASE RBC.…` identifies the case responsible for the following diagnostic output.  Closest-point dictionaries, event
  messages and other legacy observations are informational; they are not automatically declared correct.
- `CHECK …` reports a numerical check that raises an error if its tolerance is exceeded. Transform checks use explicit
  axis limits and round trips with a one-pixel tolerance, rather than a fixed screen coordinate or exact decimal text.
- `ERROR case stage: …` identifies a Setup, Body or Cleanup failure. Cleanup is attempted even when Setup or Body
  fails. Subsequent cases still run.
- `EXECUTION topic: N completed, M errors; visual review still required.` summarizes execution, not visual
  correctness. The final `all` summary covers the entire run; execution errors also cause the top-level runner to return
  an error.
- `Inspect …: path` gives an exported file to open in an appropriate viewer.

For example:

```text
CASE RBC.graph.axis.transform.M.1.1
CHECK axis round trip: data=5.0 pixel=... restored=... (tolerance 1 pixel)
EXECUTION graph.axis: ... completed, 0 errors; visual review still required.
```

A different pixel coordinate on another DPI, window size or platform is normal.  Extra decimal digits in Tcl
floating-point output are not, by themselves, a failure. Inspect cases visually even when the execution summary reports
no errors.

## Suite coverage

| Component                   | Coverage                                                                                                                                                                                                                                  |
|-----------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Graph                       | Widget configuration, axes and bindings, transforms, elements and picking, bars, pens, markers, crosshairs, grids, legends, snapshots and PostScript output                                                                               |
| Graph fills and bands       | Baseline/chord/origin area closure, solid/transparent-stipple/opaque-stipple/image-tile fills, opacity, inversion, symmetric and explicit asymmetric error bounds, X/Y bands, step bands and clipping                                     |
| Graph rendering and vectors | Native/Cairo geometry, arcs and arrowheads, single-row legend sizing, dense-trace decimation, vector updates and rename, readonly-backed connections, FFT/IFFT reconstruction, Hann windowing and simplification                          |
| Barchart                    | Axes, bindings, configuration, elements, picking, pens, markers, crosshairs, grids, legends, transforms, snapshots and PostScript; mixed bar alignment/offsets, grouping modes, opacity, symmetric/explicit error bars and inverted caps  |
| Stripchart                  | Axes, bindings, configuration, elements, picking, pens, markers, crosshairs, grids, legends, transforms, snapshots and PostScript; dense traces, appending/scrolling data and Cartesian error bands                                       |
| Polar/Smith                 | Representations, smoothing, Smith grids, aspect ratio, inversion, off-centre ranges and data outside the unit circle; area closures, solid/stipple/tiled fills and opacity                                                                |
| Trackgraph                  | Shared axes, weighted layout, dividers, inversion, order, viewport and local data scrolling, global/local legends, independent grids, window/XOR markers, track bindings, mixed line/strip/bar elements, clipping and per-track bar modes |
| Graphtoolbar                | Toolbar/context controls, two-state legends, export menus/dialogs, crosshairs, zoom/pan and track-aware interactions                                                                                                                      |
| Exports                     | SVG/PDF fonts and math glyphs, mapped windows and resized output; scrolled/inverted track viewport export to SVG, PDF and PostScript with screen geometry restored                                                                        |
| Winop                       | Window positioning, mapping, stacking, pointer operations, snapshots and image processing, including resampling, convolution, quantization and JPEG reading                                                                               |
| EPS canvas item             | Anchors, dimensions, source/image display, fills, outlines, borders, relief, shadows, stipple and title formatting/rotation                                                                                                               |

Numerical precision, C API contracts and exhaustive argument validation belong to the automated suite.

## Renderers and export inspection

Repeat visual checks using `-renderer native` and `-renderer cairo` on a Cairo-enabled build. The selected renderer
applies throughout the run, including the rendering topic; no case silently switches to another renderer. Resize windows
and check at normal display scaling; text, anti-aliasing and child-window capture can differ between Windows and X11.
Window markers retain the ordinary graph visibility policy; the suite does not assume a clipping container for partially
visible child windows.

Export cases print unique temporary paths and retain files for browser/PDF/ PostScript viewer inspection. Delete them
after review. Unembedded PDF uses supported Windows-1252 text; its case first checks the unsupported-math-glyph
error. Embedded PDF and SVG exercise math text. Compare exports with the screen, including fills and clipping, and
verify that resized export does not change the screen geometry.
