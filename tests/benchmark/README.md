# RBC rendering benchmarks

These benchmarks are intentionally separate from the normal Tcl test suite.

They measure performance rather than correctness and may process millions of
data points.

## Line and strip benchmark

The same benchmark can exercise either ordinary line elements or strip
elements.

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

## Native versus Cairo

Use `-renderer native|cairo` and `-antialias default|none|gray` on the suite
or any individual benchmark. Defaults are `native` and `default`. The suite
forwards both options to every child process. The widget's effective settings
are recorded in every CSV, including line/strip CSVs. Environment variables
`RBC_BENCH_RENDERER` and `RBC_BENCH_ANTIALIAS` no longer select settings.

The earlier, original benchmark scripts only labeled the renderer in some
reports, without selecting it. CSVs from those scripts lack the new metadata
and cannot establish a native/Cairo comparison. CSVs produced by the subsequent
working environment-variable patch remain supported.

Run the same workload, sizes, warmup and iteration counts on each renderer.
For example, in Linux or Windows MSYS2/UCRT64 Bash:

```sh
tclsh tests/benchmark/run.tcl -renderer native -antialias default -profile smoke -iterations 7 -warmup 2 -csv-dir results/native
tclsh tests/benchmark/run.tcl -renderer cairo -antialias none -profile smoke -iterations 7 -warmup 2 -csv-dir results/cairo-none
tclsh tests/benchmark/run.tcl -renderer cairo -antialias gray -profile smoke -iterations 7 -warmup 2 -csv-dir results/cairo-gray
tclsh tests/benchmark/run.tcl -renderer cairo -antialias default -profile smoke -iterations 7 -warmup 2 -csv-dir results/cairo-default
```

Use `-profile standard` for the full comparison after checking the smoke run.
Choose sizes that actually fit the display. Run the configurations sequentially
on an otherwise idle machine. Repeat in alternating order if comparing small
timing differences. Keep each repetition in a separate directory.

Individual benchmarks accept the same options:

```sh
tclsh tests/benchmark/line.tcl -renderer cairo -antialias default -csv results/line-cairo-default.csv
```

Cairo must be enabled in the RBC build; an unavailable renderer raises an error
rather than silently timing native rendering.

## Compare saved CSV files

`compare.tcl` is independent of the benchmark runner and does not load Tk for
text reports. It requires Tcllib's `csv` package. Pass a CSV file or a directory
of CSVs with each `-input HOST PATH`. The HOST label identifies a particular
machine and setup, not just its operating system. Use the same label for all
renderer runs on that setup, and distinct labels for Windows and Linux.

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

Both input schemas are supported: wide line/strip rows and long rows from the
other workloads. `-metrics redraw,axis-remap` limits the report and charts.
`redraw` is the main rendering comparison; remap and creation timings include
other work. Closest-point lookup does not measure rendering.

Matching includes HOST, platform, OS version, architecture, Tcl/Tk/RBC versions,
windowing system, workload/case, requested and actual graph dimensions, item
count and metric. Windows and Linux are displayed separately, not averaged.
Different runtime versions or actual sizes produce separate groups. Use the
same RBC source revision for all runs: the CSV records package versions, not
Git commits or hardware configuration. Record CPU, display, DPI and build
settings alongside the results when publishing them.

The ratio is **native milliseconds / Cairo milliseconds**: above 1 means Cairo
is faster; below 1 means native is faster. Cairo `default`, `none` and `gray` are separate
series using the same native baseline. Repeated rows are summarized as the
median of run medians, with run counts shown; this is not a pooled-sample median
or a statistical significance estimate. Missing counterparts remain blank and
have `n/a` ratios. Rounded zero timings are plotted as zero but have no ratio.
Only available paired, positive timings appear in ratio charts. Native's
antialias setting has no rendering effect and is not a separate series.

## Charts for the manual

Install `ticklecharts` (tested with 3.2.8) and its Tcllib dependencies on the Tcl
package path, then add `-html`:

```sh
tclsh tests/benchmark/compare.tcl \
    -input Windows-PC results/windows/native \
    -input Windows-PC results/windows/cairo-gray \
    -input Linux-PC results/linux/native \
    -input Linux-PC results/linux/cairo-gray \
    -metrics redraw -out results/comparison -html -gui
```

Open `results/comparison/index.html` to browse the charts. Each chart is a
separate HTML file rendered with ECharts' SVG renderer. It is still an HTML
chart, not a standalone SVG file. The HTML uses ticklecharts' default ECharts
CDN, so viewing it requires network access unless you arrange a local JS copy.
No timing values are fabricated by this script.

`charts.ruff` contains chart descriptions and markers such as
`!benchmark_0001_time!`. Copy the desired content into a Ruff page, generate
the manual normally, then substitute the markers in the resulting HTML:

```sh
tclsh tests/benchmark/compare.tcl -embed docs/rbc-Performance.html -out results/comparison
```

Use your actual generated HTML filename. This follows SpiceGenTcl's approach:
replace a marker with the generated ticklecharts HTML after Ruff/Sphinx has
finished. `charts.map` maps markers to files; retain it with the HTML files.
Embedding updates the specified page in place and should be repeated after
regenerating that page. Chart IDs are assigned from sorted groups; regenerate
and copy the matching `charts.ruff` content when changing the input groups.
These placeholders target HTML output; omit them from nroff sections.

For standalone SVG or PNG files, add `-snapshot svg` or `-snapshot png` and
optionally `-browser /path/to/chromium`. This uses ticklecharts' `SnapShot` API,
which requires Chromium/Chrome/Edge and Tcllib's `websocket` package. For example:

```sh
tclsh tests/benchmark/compare.tcl -input Windows-PC results/native \
    -input Windows-PC results/cairo-gray -metrics redraw \
    -out results/comparison -snapshot svg -browser 'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
```

Static files can instead be included with ordinary Ruff image links. Browser
snapshot support is optional; text reports and HTML generation do not require
a browser installation.

## Comparison-tool tests

Run these headless tests separately from the Tk widget suite:

```sh
tclsh tests/benchmark/compare.test
```

They cover both schemas, quoted/multiline CSV, repeated runs, matching rules,
antialias variants, missing pairs, zero/non-finite timings and ratio arithmetic.
