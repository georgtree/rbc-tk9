# RBC rendering benchmarks

These benchmarks are intentionally separate from the normal Tcl test suite.

They measure performance rather than correctness and may process millions of data points.

## Native renderer migration: stage 1

Stage 1 routes line traces, strip segments, and line/strip error bars through the renderer interface.
It retains the native GCs, clipping, coordinate rounding, X11 request splitting, and Windows batch limits.
Symbols, fills, legend samples, and other component drawing paths are outside this stage.

For before/after comparisons, use `line.tcl`, `line.tcl -stripchart`, and `errorbars.tcl -cases y,xy`,
all with `-renderer native`. Use identical options, Tcl/Tk versions, compiler settings, display settings,
and hardware on both revisions. Keep the window mapped throughout each run. Save each repetition and
revision in a separate CSV directory; record the Git revision alongside the results.

The primary measurement is redraw median, which isolates drawing of already mapped geometry. Creation,
axis-remap and vector-update measurements are useful secondary checks. Closest-point timing is not a
rendering measurement. Run both `-decimate none,auto` for line/strip workloads: the shorter decimated paths
also reveal fixed per-batch overhead. The current line benchmark uses solid one-pixel strokes; wide-line
and dash correctness is covered by tests, but these styles are not selected by its command-line options.

`compare.tcl` compares native and Cairo renderers, not source revisions. Do not combine before/after CSV
files under the same host label: they would be treated as repeated samples of the same implementation.

## Comparing before and after revisions

`compare-revisions.tcl` compares two source revisions using the existing wide line/strip CSV format and
long CSV format used by the other workloads. It requires Tcl and Tcllib's `csv` package, but no Tk or RBC.
Keep it beside `compare.tcl`, whose CSV parsing and normalization helpers it reuses.

Run separately for each physical machine. Directories are searched recursively, including `run-1`, `run-2`, etc.:

```sh
tclsh91 tests/benchmark/compare-revisions.tcl \
    -before results/native-stage1/linux/before \
    -after results/native-stage1/linux/after \
    -out results/native-stage1/linux-comparison.csv

tclsh91 tests/benchmark/compare-revisions.tcl \
    -before results/native-stage1/windows/before \
    -after results/native-stage1/windows/after \
    -out results/native-stage1/windows-comparison.csv
```

The default metrics are `redraw,axis-remap`; use `-metrics redraw` or `-metrics all` to change this.
Each timing is the median of the separate run medians, not a pooled median of individual iterations.
Change is `100 * (after / before - 1)`: positive means slower, negative means faster.
The CSV includes each side's run count, minimum and maximum run median, RBC versions, and matching metadata.
These ranges describe repeat variability, not statistical confidence intervals. Small changes within the
run-to-run variation should not be treated as established regressions or improvements.

Workload, count, requested and actual sizes, renderer/antialias settings, platform, architecture and Tcl/Tk
versions must match. RBC versions may differ and are reported. Missing pairs remain visible; zero rounded
timings have no percentage. The CSV does not identify compiler flags, Git commits or physical hosts, so
keep those controlled and record them separately. Keep comparison output outside both input directories.

Utility tests:

```sh
tclsh91 tests/benchmark/RBC.benchmark.revisions.A.test
```

## Line and strip benchmark

The same benchmark can exercise either ordinary line elements or strip elements.

For line elements:

```sh
tclsh tests/benchmark/line.tcl
```

For strip elements:

```sh
tclsh tests/benchmark/line.tcl -stripchart
```

Typical focused runs can select point counts, display sizes, and decimation modes:

``` sh
tclsh tests/benchmark/line.tcl -stripchart -points 100000,1000000,5000000 -sizes 640x480 -decimate none,auto
```

The benchmark disables -bufferelements and measures:

- create-ms — element creation, initial mapping, and drawing;
- redraw-med — redraw of already mapped geometry;
- axis-remap — remapping after a small X-axis range change;
- array-remap — one in-place Y update through the vector's mapped Tcl array;
- index-remap — the equivalent Y update through the vector index command;
- append-remap — appending one monotonically increasing X/Y sample pair, including vector growth, notification, cache
  maintenance, remapping, and redraw;
- closest-med — exact source-point lookup with along both -interpolate 0 -halo 2.

With -decimate auto, eligible dense line and strip elements use persistent data-domain summaries. Axis-only remaps and
ranged Y updates can reuse those summaries.

Pure monotonic tail growth can also extend the persistent cache. Therefore append-remap is intended to measure
continuously growing waveform workloads: its cache-maintenance cost should depend primarily on the newly appended data
and display density rather than on the complete vector length.

With -decimate none, mapping and drawing remain proportional to the complete source data set.

CSV output may be requested with -csv file.

## Marker benchmark

`markers.tcl` measures line, polygon, arc, text, bitmap and image markers. The default cases include:

| Arc case | Geometry | Appearance |
| --- | --- | --- |
| `arc-fill` | Full ellipse | Solid fill, no outline |
| `arc-outline` | Full ellipse | Outline only |
| `arc-open` | Open arc, start 30°, extent 240° | Outline only |
| `arc-chord-fill` | Chord, start 30°, extent 120° | Solid fill, no outline |
| `arc-pieslice-fill` | Pie sector, start 30°, extent 120° | Solid fill, no outline |

Markers occupy the same grid cells as the existing line/polygon cases. Arc boxes use both cell dimensions,
so displayed shapes may be elliptical rather than circular. Fill and outline work are measured separately.

Focused native/Cairo smoke runs (Linux or Windows MSYS2/UCRT64 Bash):

```sh
tclsh tests/benchmark/markers.tcl -renderer native -profile smoke -cases arc-fill,arc-outline,arc-open,arc-chord-fill,arc-pieslice-fill -csv markers-native.csv
tclsh tests/benchmark/markers.tcl -renderer cairo -profile smoke -cases arc-fill,arc-outline,arc-open,arc-chord-fill,arc-pieslice-fill -csv markers-cairo.csv
```

The suite's `run.tcl -benchmarks markers` includes these cases automatically in every profile. Counts, sizes,
warmup and iterations use the existing profile defaults and can be overridden as for the other marker cases.
`create-ms` includes construction, initial mapping and first display; `redraw-med` measures already-mapped drawing;
`axis-remap` includes remapping and redraw. Cairo arcs use curved paths, while native arcs use flattened geometry.
Both build the flattened geometry used for picking during mapping, so that cost is included in creation/remapping.
These are screen-rendering benchmarks, not SVG/PostScript export benchmarks. Historical CSVs lack the new arc rows;
collect new runs on both renderers before comparing them.

## Native versus Cairo

Use `-renderer native|cairo` and `-antialias default|none|gray` on the suite or any individual benchmark. Defaults are
`native` and `default`. The suite forwards both options to every child process. The widget's effective settings are
recorded in every CSV, including line/strip CSVs. Environment variables `RBC_BENCH_RENDERER` and `RBC_BENCH_ANTIALIAS`
no longer select settings.

The earlier, original benchmark scripts only labeled the renderer in some reports, without selecting it. CSVs from those
scripts lack the new metadata and cannot establish a native/Cairo comparison. CSVs produced by the subsequent working
environment-variable patch remain supported.

Run the same workload, sizes, warmup and iteration counts on each renderer. For example, in Linux or Windows
MSYS2/UCRT64 Bash:

```sh
tclsh tests/benchmark/run.tcl -renderer native -antialias default -profile smoke -iterations 7 -warmup 2 -csv-dir results/native
tclsh tests/benchmark/run.tcl -renderer cairo -antialias none -profile smoke -iterations 7 -warmup 2 -csv-dir results/cairo-none
tclsh tests/benchmark/run.tcl -renderer cairo -antialias default -profile smoke -iterations 7 -warmup 2 -csv-dir results/cairo-default
```

Use `-profile standard` for the full comparison after checking the smoke run.  Choose sizes that actually fit the
display. Run the configurations sequentially on an otherwise idle machine. Repeat in alternating order if comparing
small timing differences. Keep each repetition in a separate directory.

Individual benchmarks accept the same options:

```sh
tclsh tests/benchmark/line.tcl -renderer cairo -antialias default -csv results/line-cairo-default.csv
```

Cairo must be enabled in the RBC build; an unavailable renderer raises an error rather than silently timing native
rendering.

## Compare saved CSV files

`compare.tcl` is independent of the benchmark runner and does not load Tk for text reports. It requires Tcllib's `csv`
package. Pass a CSV file or a directory of CSVs with each `-input HOST PATH`. The HOST label identifies a particular
machine and setup, not just its operating system. Use the same label for all renderer runs on that setup, and distinct
labels for Windows and Linux.

```sh
tclsh tests/benchmark/compare.tcl \
    -input Windows-PC results/windows/native \
    -input Windows-PC results/windows/cairo-none \
    -input Windows-PC results/windows/cairo-gray \
    -input Linux-PC results/linux/native \
    -input Linux-PC results/linux/cairo-none \
    -input Linux-PC results/linux/cairo-gray \
    -out results/comparison
```

Outputs:

- `comparison.txt`: tables printed to the terminal and saved to disk.
- `comparison.csv`: normalized, paired measurements with run counts and ratios.
- With `-gui`: an RBC viewer with a workload selector, timing graph and ratio
  graph. It loads Tk and RBC only when requested. Closing it exits the viewer.
- With `-html`: an index and separate ticklecharts HTML files for each workload,
  metric, size and host, plus chart placeholders for Ruff.

Both input schemas are supported: wide line/strip rows and long rows from the other workloads. `-metrics
redraw,axis-remap` limits the report and charts.  `redraw` is the main rendering comparison; remap and creation timings
include other work. Closest-point lookup does not measure rendering.

Matching includes HOST, platform, OS version, architecture, Tcl/Tk/RBC versions, windowing system, workload/case,
requested and actual graph dimensions, item count and metric. Windows and Linux are displayed separately, not averaged.
Different runtime versions or actual sizes produce separate groups. Use the same RBC source revision for all runs: the
CSV records package versions, not Git commits or hardware configuration. Record CPU, display, DPI and build settings
alongside the results when publishing them.

The ratio is **native milliseconds / Cairo milliseconds**: above 1 means Cairo is faster; below 1 means native is
faster. Cairo `default`, `none` and `gray` are separate series using the same native baseline. Repeated rows are
summarized as the median of run medians, with run counts shown; this is not a pooled-sample median or a statistical
significance estimate. Missing counterparts remain blank and have `n/a` ratios. Rounded zero timings are plotted as zero
but have no ratio.  Only available paired, positive timings appear in ratio charts. Native's antialias setting has no
rendering effect and is not a separate series.

## Charts for the manual

Install `ticklecharts` (tested with 3.2.8) and its Tcllib dependencies on the Tcl package path, then add `-html`:

```sh
tclsh tests/benchmark/compare.tcl \
    -input Windows-PC results/windows/native \
    -input Windows-PC results/windows/cairo-gray \
    -input Linux-PC results/linux/native \
    -input Linux-PC results/linux/cairo-gray \
    -metrics redraw -out results/comparison -html -gui
```

Open `results/comparison/index.html` to browse the charts. Each chart is a separate HTML file rendered with ECharts' SVG
renderer. It is still an HTML chart, not a standalone SVG file. The HTML uses ticklecharts' default ECharts CDN, so
viewing it requires network access unless you arrange a local JS copy.  No timing values are fabricated by this script.

`charts.ruff` contains chart descriptions and markers such as `!benchmark_0001_time!`. Copy the desired content into a
Ruff page, generate the manual normally, then substitute the markers in the resulting HTML:

```sh
tclsh tests/benchmark/compare.tcl -embed docs/rbc-Performance.html -out results/comparison
```

Use your actual generated HTML filename. This follows SpiceGenTcl's approach: replace a marker with the generated
ticklecharts HTML after Ruff/Sphinx has finished. `charts.map` maps markers to files; retain it with the HTML files.
Embedding updates the specified page in place and should be repeated after regenerating that page. Chart IDs are
assigned from sorted groups; regenerate and copy the matching `charts.ruff` content when changing the input groups.
These placeholders target HTML output; omit them from nroff sections.

For standalone SVG or PNG files, add `-snapshot svg` or `-snapshot png` and optionally `-browser
/path/to/chromium`. This uses ticklecharts' `SnapShot` API, which requires Chromium/Chrome/Edge and Tcllib's `websocket`
package. For example:

```sh
tclsh tests/benchmark/compare.tcl -input Windows-PC results/native \
    -input Windows-PC results/cairo-gray -metrics redraw \
    -out results/comparison -snapshot svg -browser 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
```

Static files can instead be included with ordinary Ruff image links. Browser snapshot support is optional; text reports
and HTML generation do not require a browser installation.

## Comparison-tool tests

Run these headless tests separately from the Tk widget suite:

```sh
tclsh tests/benchmark/RBC.benchmark.compare.A.test
```

They cover both schemas, quoted/multiline CSV, repeated runs, matching rules, antialias variants, missing pairs,
zero/non-finite timings and ratio arithmetic.

### Native renderer stage 3: clipping

Apply the benchmark additions before changing the renderer. Stage 2, including
its native stroke file move, is the `before` build. Use the same Tcl/Tk, display,
compiler options and benchmark scripts for both builds. Run from an otherwise
idle desktop with the benchmark window visible.

From Linux, run:

```sh
TCLSH=tclsh9.0 bash tests/benchmark/native-stage3.sh linux before 3
```

From Windows MSYS2, run:

```sh
TCLSH=tclsh91 bash tests/benchmark/native-stage3.sh windows before 3
```

After installing the stage 3 implementation, repeat with `after` instead of
`before`. The runner writes into `results/native-stage3/PLATFORM/PHASE/run-N`
and refuses to overwrite an existing run directory. `TCLSH` may also be an
absolute executable path. Each invocation uses the RBC package resolved by
that interpreter; ensure it loads the intended build.

The runner covers line/strip batches, independent error-bar segments, symbols,
bar fills/bevels, many elements, polygon/arc markers and track clipping. The
new `trackclip.tcl` uses the existing common timing, reporting and long-format
CSV machinery. It provides `-tracks`, `-points` (per track), `-cases line,mixed`,
`-layouts fit,scroll`, `-inverted 0,1`, `-sizes`, `-renderer`, `-antialias`,
`-iterations`, `-warmup` and `-csv` options.

Track data deliberately cross the fixed X and Y limits. The mixed case adds
symbols, error bars, raised bars and polygon/arc markers. Every track has its
own Y axis; all tracks share X. Fit mode stresses repeated clip changes and
scroll mode additionally includes partially visible tracks. Points per track
are held fixed, so increasing the track count also increases total data: this
is a before/after workload comparison, not an isolated measurement of clip
setup cost. Data generation and graph construction are outside timed sections.

`trackclip` CSV `count` is the number of tracks; the case identifies geometry,
layout, inversion and points per track. It records `redraw`, `axis-remap`, and
`track-scroll` when the content actually overflows. Track scrolling alternates
between two fixed positions. These are performance measurements, not pixel
correctness tests; XOR and clipping correctness remain regression-test work.

If the separately supplied `compare-revisions.tcl` is present, compare with:

```sh
tclsh9.0 tests/benchmark/compare-revisions.tcl \
    -before results/native-stage3/linux/before \
    -after results/native-stage3/linux/after \
    -metrics redraw,axis-remap,track-scroll \
    -out results/native-stage3/linux-comparison.csv
```

For Windows, substitute `tclsh91` and `windows`. Keep comparisons within each
platform. Investigate large differences with alternating before/after runs;
run ranges are not confidence intervals.
