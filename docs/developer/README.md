# C developer manual

This maintained Sphinx project is independent of the Ruff-generated Tcl manual.
Coverage includes architecture, header dependencies, renderer contracts, graph
widget lifecycle and redisplay, vector ownership and notifications, binding
dispatch and picking, component option transactions and resource ownership,
and source-comment conventions.

Install Doxygen, Python 3, Sphinx and Breathe. From MSYS2 UCRT64:

    pacman -S --needed mingw-w64-ucrt-x86_64-doxygen \
        mingw-w64-ucrt-x86_64-python-sphinx \
        mingw-w64-ucrt-x86_64-python-breathe

After configuring, run:

    make doc-dev
    make check-doc-dev

Open `docs/developer/html/index.html` in the source tree.
The check target treats Doxygen and Sphinx warnings as failures.
No RBC build, Tk display or Graphviz is required for this first version.

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
