# C developer manual

This maintained Sphinx project is independent of the Ruff-generated Tcl manual.
Coverage includes package initialization and client stubs, architecture, header dependencies,
renderer contracts, capability-specific screen dispatch and fallback order, native batching/clip ownership, Cairo surface and pattern ownership, graph
widget lifecycle and redisplay, vector ownership and notifications, expression evaluation and
callback boundaries, recursive expression parsing, temporary-type adapters, reduction formulas and complex scalar kernels, FFT workspace and output ownership, private FFT commit helpers, spline interpolation and smoothing,
quadratic interval selection and parametric cubic factorization, line
decimation and cache maintenance, bar grouping and error positioning, area fills and uncertainty
bands, polar data adapters, sampled polar/Smith grid geometry, viewport-aware labels and formatter cancellation, binding dispatch and picking, component option
transactions and resource ownership, scrolling and viewport contracts, geometry and clipping
contracts and private subdivision helpers, chain/list ownership, insertion and keyed lookup, image and tile ownership, text layout, document font ownership, sfnt/CFF metadata parsing, Unicode/Symbol lookup and PDF font resources,
export orchestration, SVG definitions and image encoding, PDF resource serialization and document
ownership, PDF/PostScript drawing primitives and shared export capture, crosshair presentation and XOR configuration, legend ownership and layout, grid inheritance,
tick-list transactions and output, pen and
palette ownership, named-pen dispatch and revival, palette staging and range defaults, marker lifecycle, element data and dispatch, shared configuration transactions and retained coordinate representations, element data observation, extrema and value-label formatting, axis and track mapping, shared
GC/window services, standalone window/photo command dispatch, EPS canvas parsing and preview
ownership, allocation and UID ownership, typed operation dispatch, shared value conversion and legacy option-adapter ownership, dictionary ordering, stdio and fatal diagnostics, numeric
compatibility, and source-comment conventions.

Install Doxygen, Python 3, Sphinx and Breathe. From MSYS2 UCRT64:

    pacman -S --needed mingw-w64-ucrt-x86_64-doxygen \
        mingw-w64-ucrt-x86_64-python-sphinx \
        mingw-w64-ucrt-x86_64-python-breathe

After configuring, run:

    make doc-dev
    make check-doc-dev

Open `docs/developer/html/index.html` in the source tree.
The check target treats Doxygen and Sphinx warnings as failures.
No RBC build, Tk display or Graphviz is required for documentation generation.

Without configure, from the source root:

    tclsh docs/developer/build.tcl --strict

The Tcl runner requires no additional Tcl packages. Python is used only for
Sphinx and Breathe; their small Sphinx configuration remains in `conf.py`. Use
`--doxygen PATH`, `--python PATH` and `--build-dir PATH`, or make variables `DOXYGEN`, `DOC_DEV_PYTHON` and
`DOC_DEV_BUILD` / `DOC_DEV_HTML`, to override executables or output location. Paths may contain
spaces; executable values must not include extra command-line flags.
The MSVC makefile also provides `doc-dev` and `check-doc-dev`.

Commit `docs/developer/html/` with your other generated documentation to
publish it at `developer/html/` on GitHub Pages. XML, logs and caches remain
under `_build/developer/`. Generated HTML is retained by `make clean`. Sphinx does not copy RST sources
or add source links to the published pages.
Use `--html-dir PATH` to override the published HTML destination.
Developer HTML is not yet included in installation or binary archives.
Source documentation is incremental: strict checks validate included comments and links, not documentation coverage of the entire codebase.
See the rendered Building and C documentation conventions chapters for details.
