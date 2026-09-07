# Rbc — Refactored BLT Components for Tcl/Tk 9

Rbc is a Tcl/Tk extension for scientific plotting, interactive data visualization, and numerical data processing. It
provides graph widgets, real and complex vectors, spline interpolation, and window and image operations.

This repository maintains and extends Rbc for Tcl/Tk 9.0. The current package version is **0.5.0**.

- [Source code](https://github.com/georgtree/rbc-tk9)
- [Documentation](https://georgtree.github.io/rbc-tk9/)
- [Changes and modernization](https://georgtree.github.io/rbc-tk9/rbc-tk9-TclTk9Upgrade.html)
- [Issue tracker](https://github.com/georgtree/rbc-tk9/issues)

## Components

- **graph** — Cartesian plots with line elements, symbols, multiple axes, error bars, markers, legends, and PostScript
  output.
- **barchart** — Bar plots with normal, aligned, stacked, and overlapping presentations.
- **stripchart** — Plots using independent line segments, suitable for waveform and continuously updated data displays.
- **polar** — Complex-plane plots with polar or Smith-chart grids. Data coordinates remain Cartesian; elements can use
  real X/Y vectors or complex vectors directly.
- **graphtoolbar** — A TclOO megawidget providing graph interaction, including zooming, panning, crosshairs,
  closest-point information, legend interaction, and context controls.
- **vector** — Real and complex double-precision vectors, with numerical operations and automatic notification of
  attached graph elements.
- **spline** — Natural cubic and shape-preserving quadratic interpolation, including parametric interpolation of complex
  vectors.
- **winop** — Window operations and image-processing utilities.

Graphs redraw automatically when their attached vectors change. Eligible dense line and strip elements support display
decimation, including persistent summaries that can be reused for axis changes, ranged value updates, and monotonic tail
appends.

Rbc contains selected components derived from BLT. It is not a complete replacement for the BLT toolkit.

## Requirements

### Running Rbc

- Tcl and Tk 9.0.
- The Tcl [argparse package](https://github.com/georgtree/argparse).
- A graphical environment supported by the Tk installation.

The supported build environments described here are Linux with X11 Tk and Windows with MSYS2/UCRT64. Tcl/Tk 8.x
compatibility is not a target of this fork.

The current package loader loads `graphtoolbar.tcl` as part of `package require rbc`, so `argparse` is required even
when an application uses only the basic graph or vector commands. Make corresponding adjustements to pkgIndex.tcl file
to exclude it from standard loading sequence.

Rbc initializes Tk when loaded. Tests and documentation generation therefore also need a working graphical display.

### Building Rbc

- A C compiler and linker compatible with the selected Tcl/Tk build.
- GNU Make and a Unix-style shell.
- Tcl/Tk development files, including `tclConfig.sh` and `tkConfig.sh`.
- Matching Tk private headers.
- On Linux, the X11 development headers and libraries.

Rbc still uses a small number of private Tk interfaces. Retain the matching Tk source tree unless the development
installation provides all required private headers. The configure script uses the source location recorded in
`tkConfig.sh` when locating these headers.

Keep the corresponding Tcl source tree available as well when using the provided development targets: the Makefile sets
`TCL_LIBRARY` from the source location recorded in `tclConfig.sh`.

The repository includes a generated `configure` script. Autoconf is needed only when regenerating it after changes to
the build configuration.

## Building from source

Obtain the sources:

```sh
git clone https://github.com/georgtree/rbc-tk9.git
cd rbc-tk9
```

The examples below build in the repository root. Replace the Tcl/Tk paths with paths for your installation or build
trees.

`--with-tcl` and `--with-tk` take the **directories containing** `tclConfig.sh` and `tkConfig.sh`, not the configuration
filenames themselves.

Install `argparse` for the same Tcl interpreter before running Rbc, its tests, or its demos.

### Linux

For example, using Tcl/Tk build trees and a user-local installation prefix:
```sh
./configure \
    --with-tcl=/path/to/tcl9.0/unix \
    --with-tk=/path/to/tk9.0/unix \
    --prefix="$HOME/.local"

make -j4
make test

make install DOC_INSTALL_DIR="$HOME/.local/share/rbc0.5.0/doc"
```

The Tcl/Tk directories can instead refer to installed configuration files, provided that the required headers and source
locations remain accessible.

The selected Tcl/Tk shared libraries must also be discoverable by the system dynamic loader. The Makefile supplies the
build-directory environment for its `test` and `shell` targets.

### Windows with MSYS2/UCRT64

Run the following commands in an **MSYS2 UCRT64 shell**, using Tcl and Tk built with a compatible toolchain and
architecture:
```sh
./configure \
    --with-tcl=/path/to/tcl9.0/win \
    --with-tk=/path/to/tk9.0/win \
    --prefix=/ucrt64

make -j4
make test

make install DOC_INSTALL_DIR=/ucrt64/share/rbc0.5.0/doc
```

Ensure that the selected Tcl/Tk DLLs and compiler runtime DLLs are available through `PATH`.

The UCRT64 shell does not automatically determine Rbc's installation prefix.  Specify `--prefix=/ucrt64` when that is
the intended destination.

### Installation locations

With the default directory layout, installation places:

- The package library, `pkgIndex.tcl`, runtime scripts, and supporting resources in `PREFIX/lib/rbc0.5.0`.
- Demos in `PREFIX/lib/rbc0.5.0/demos`.
- Manual pages in `PREFIX/share/man/mann`.

The current Makefile has a separate `DOC_INSTALL_DIR` setting for HTML documentation. The commands above explicitly set
it to a location under the chosen prefix.

If `--prefix` is omitted, the build system normally inherits the prefix from the selected Tcl configuration.

For other configure options, run:
```sh
./configure --help
```

## Loading the package

Start the Tcl/Tk interpreter associated with your build and run:
```tcl
package require rbc 0.5.0
```

To check the versions in use:
```tcl
puts "Tcl: [info patchlevel]"
puts "Tk:  [package provide Tk]"
puts "Rbc: [package provide rbc]"
```

If Rbc is installed outside Tcl's normal package search locations, add its library directory before loading it:
```tcl
lappend auto_path /path/to/prefix/lib
package require rbc 0.5.0
```

The same applies to `argparse` if it is installed in a separate location.

When installing a prebuilt package, preserve the complete package directory, including `pkgIndex.tcl`, the shared
library, `graphtoolbar.tcl`, PostScript prologs, and bitmap resources. Copying only the DLL or shared library is not
sufficient.

### Using an uninstalled build

From the Rbc build directory:
```sh
make shell
```

This starts the configured Tcl interpreter with the build directory in its package search path. Then load Rbc normally:
```tcl
package require rbc
```

## Running tests

Run the automated test suite from the build directory:
```sh
make test
```

This target builds the package and the public C API test extension, then runs `tests/all.tcl` with the appropriate build
environment.

The suite uses `tcltest`. Pass test selection options through `TESTFLAGS`.

For example, run the line-decimation test file:
```sh
make test TESTFLAGS="-file RBC.graph.element.decimate.A.test"
```

Or select tests by name:
```sh
make test TESTFLAGS="-match RBC.graph.element.decimate.*"
```

On a headless Linux system, an X11 virtual display can be used if Xvfb is installed:
```sh
xvfb-run -a make test
```

Review the test summary and any skipped-test constraints. The `manualtests` directory contains additional visual and
interactive checks that are separate from the automated suite.

## Running demos

From an in-tree build, start the demo browser with:
```sh
make shell SCRIPT=demos/demos.tcl
```

The browser launches individual demos when their thumbnails are selected.

An individual demo can also be started directly:
```sh
make shell SCRIPT=demos/graph1.tcl
```

After installation, use the matching Tcl/Tk 9 interpreter:
```sh
wish9.0 /path/to/prefix/lib/rbc0.5.0/demos/demos.tcl
```

The executable may instead be named `wish`, depending on the Tcl/Tk installation. On Windows, use the corresponding
`wish.exe`.

The demos locate their supporting files relative to their own script directory. Keep the demo scripts, images, bitmaps,
stipples, and helper scripts together.

The current demos cover graphs, bar charts, stripcharts, symbols, spline interpolation, and window/image operations.

## Rendering benchmarks

Performance benchmarks are separate from the correctness tests. They can create large data sets and should be run
deliberately.

In addition to Rbc and `argparse`, the benchmark scripts require the Tcllib packages `csv`, `report`, and
`struct::matrix`.

Run the small smoke profile from an in-tree build:
```sh
make shell SCRIPT="tests/benchmark/run.tcl -profile smoke"
```

Run the standard benchmark suite:
```sh
make shell SCRIPT="tests/benchmark/run.tcl -profile standard"
```

Run a focused line/strip comparison and save CSV results:
```sh
make shell SCRIPT="tests/benchmark/run.tcl -profile standard -benchmarks line,strip -csv-dir benchmark-results"
```

The suite includes line, strip, symbol, error-bar, bar, marker, and mixed workloads. For individual line-benchmark
measurements and options, see the [benchmark README](https://github.com/georgtree/rbc-tk9/blob/main/tests/benchmark/README.md).

## Documentation

The generated HTML documentation is available online:
- [Documentation home](https://georgtree.github.io/rbc-tk9/)
- [Package commands](https://georgtree.github.io/rbc-tk9/rbc-tk9-rbc.html)
- [Graph widget operations](https://georgtree.github.io/rbc-tk9/rbc-tk9-rbc-GRAPHINST.html)
- [graphtoolbar](https://georgtree.github.io/rbc-tk9/rbc-tk9-rbc-graphtoolbar.html)
- [Vector commands](https://georgtree.github.io/rbc-tk9/rbc-tk9-rbc-vector.html)
- [Vector instance operations](https://georgtree.github.io/rbc-tk9/rbc-tk9-rbc-VECINST.html)
- [Spline interpolation](https://georgtree.github.io/rbc-tk9/rbc-tk9-rbc-spline.html)
- [Tcl/Tk 9 modernization and additions](https://georgtree.github.io/rbc-tk9/rbc-tk9-TclTk9Upgrade.html)

The repository also includes generated HTML under `docs/` and manual pages as `docs/*.n`. Open `docs/index.html` to
browse the local HTML documentation.

### Regenerating documentation

Documentation generation requires:

- A working Rbc build and its runtime dependencies.
- The Tcl `ruff` package with Sphinx and nroff output support.
- Tcllib's `fileutil` package.
- Python Sphinx, with `sphinx-build` available through `PATH`.
- `ditaa` and its Java runtime when rendering diagrams that use the
  configured diagram generator.

To build this particular documentation, the modified version of Ruff package was used, that is not availible publicly at
this moment, so resulted documentation could differ from the provided in the current repository.

From an in-tree build, run:

```sh
make doc
```

The generator reads the `.ruff` sources and Tcl API documentation, writes Sphinx sources under `docs/sphinx`, builds
HTML under `docs`, and generates the `.n` manual pages.

Review the generator output and resulting pages before publishing.  The current generator prints Sphinx diagnostics, so
the Make command's exit status alone is not a complete documentation-build check.

`README.md` supplies the shared introduction and build instructions.
`docs/startPage.ruff` reads that file as the documentation preamble.

## Reporting problems

Please use the [issue tracker](https://github.com/georgtree/rbc-tk9/issues).

For build or runtime problems, include:

- The Rbc version or commit.
- Operating system and architecture.
- Tcl/Tk versions and, on Windows, the compiler/MSYS2 environment.
- Configure options and the relevant error output.
- A small Tcl script reproducing the problem, when applicable.

For rendering problems, include a screenshot and the relevant graph, element, pen, or marker options.

## History and acknowledgments

Rbc originated as a refactoring of selected components from version 2.4z of the BLT toolkit, developed by George Howlett
and other contributors.

Samuel Green, Nicholas Hudson, Stanton Sievers, and Jarrod Stormo carried out the original Rbc project at Rose-Hulman
Institute of Technology during 2008–2009. The project adapted commonly used BLT components for Tcl/Tk 8.5 and was
associated with the
[GDAT graphical data-analysis project](https://sourceforge.net/projects/gdat/).

Subsequent work included additional maintenance, demo improvements, and C stubs support. The stubs implementation
includes work by Ashok P. Nadkarni.

Emiliano Gavilán adapted Rbc for Tcl/Tk 9.0. This repository is based on his
[Rbc Tcl/Tk 9 repository](https://chiselapp.com/user/egavilan/repository/rbc-tk9).

George Yashin maintains this fork and its further Tcl/Tk 9 modernization, new functionality, tests, performance
improvements, and documentation.

The project also acknowledges the Tcl/Tk developers, the maintainers of the Tcl Extension Architecture build system, and
the contributors whose code and resources remain part of Rbc.

- [BLT project](https://sourceforge.net/projects/blt/)
- [Upstream Tcl/Tk 9 adaptation](https://chiselapp.com/user/egavilan/repository/rbc-tk9)
- [Current development history](https://github.com/georgtree/rbc-tk9/commits/main/)

## Copyright and licensing

Rbc includes material from several authors and projects. The applicable copyright notices, permission conditions, and
warranty disclaimers are retained in the source distribution.

The main
[license.terms](https://github.com/georgtree/rbc-tk9/blob/main/license.terms)
contains:

- Copyright (c) 2009, Samuel Green, Nicholas Hudson, Stanton Sievers,
  and Jarrod Stormo. All rights reserved.
- Copyright 1998 Lucent Technologies, Inc.
- The associated redistribution conditions and warranty disclaimers.

Additional notices include:

- Copyright (c) 2018 Ashok P. Nadkarni in
  [generic/rbcStubLib.c](https://github.com/georgtree/rbc-tk9/blob/main/generic/rbcStubLib.c).
- Copyright 1989–1992 Regents of the University of California and
  copyright 1991–1997 Bell Labs Innovations for Lucent Technologies in
  [library/rbcGraph.pro](https://github.com/georgtree/rbc-tk9/blob/main/library/rbcGraph.pro).
- Copyright 1991–1997 Bell Labs Innovations for Lucent Technologies in
  [library/rbcCanvEps.pro](https://github.com/georgtree/rbc-tk9/blob/main/library/rbcCanvEps.pro).
- Notices for the Regents of the University of California, Sun Microsystems,
  Scriptics, ActiveState, and other contributors in the bundled
  [Tcl configuration support license](https://github.com/georgtree/rbc-tk9/blob/main/tclconfig/license.terms).
- Ajuba Solutions and ActiveState notices in
  [tclconfig/tcl.m4](https://github.com/georgtree/rbc-tk9/blob/main/tclconfig/tcl.m4),
  and Scriptics and ActiveState notices in
  [Makefile.in](https://github.com/georgtree/rbc-tk9/blob/main/Makefile.in).
- Copyright (C) 1994 X Consortium in the bundled
  [installation helper](https://github.com/georgtree/rbc-tk9/blob/main/tclconfig/install-sh).
- George Yashin's copyright attribution in the generated documentation.

This overview does not replace the full license texts or individual file notices. Preserve the applicable notices,
conditions, and disclaimers when redistributing source or binary packages.

External dependencies retain their own licenses.

