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
