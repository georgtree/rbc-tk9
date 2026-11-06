# C developer manual

This maintained Sphinx project is independent of the Ruff-generated Tcl manual.
The manual is organized into architecture and conventions; vectors and numerical
operations; graphs and components; rendering and export (native, Cairo, SVG, PDF
and PostScript); images, fonts and windows; and shared infrastructure. Each area
links explanatory chapters with the relevant C reference pages.

Start at `index.rst`. Shared parameter and lifetime terminology lives in
`calling-conventions.rst`; authoring rules are in `commenting.rst`. Existing
chapter filenames and C reference targets are retained when navigation changes.
Coverage is selective; a successful strict build is not a completeness audit.

Install Doxygen, Python 3, Sphinx, the Read the Docs theme and Breathe. From MSYS2 UCRT64:

    pacman -S --needed mingw-w64-ucrt-x86_64-doxygen \
        mingw-w64-ucrt-x86_64-python-sphinx \
        mingw-w64-ucrt-x86_64-python-sphinx_rtd_theme \
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
Package initialization coverage includes interpreter readiness records, retry
boundaries, client-side stubs selection and Windows DLL loader wrappers.

Source documentation is incremental: strict checks validate included comments and links, not documentation coverage of the entire codebase.
See the rendered Building and C documentation conventions chapters for details.

The window/photo internals chapter also records native command argument and
partial-update rules, pointer coordinates, platform geometry queries, and the
legacy colormap and gradient assumptions. Their private helper contracts are
included in the window/photo C reference.

Vector command documentation includes the instance dispatch gates, reversible
read-only state, type queries, expression assignment, and the distinction between
clearing an array cache and changing numerical storage. Mapping and cache helpers
describe their callback checks and nontransactional failure behavior.

Both manuals use the Read the Docs theme. Shared custom styles live in
`docs/theme/rbc.css` and `docs/theme/rbc-layout.css`; generated `_static` copies
should not be edited. Tables and parameter fields wrap within RTD's default
content width; narrow screens stack field labels above descriptions. C declaration
signatures retain their existing layout. The builder stages the stylesheets before
Sphinx runs and verifies the resulting copies. Keep
`docs/.nojekyll` at the Pages publishing root and publish the complete generated
HTML trees, including theme scripts, CSS and fonts. Run `pacman -Syu` before
installing packages if the MSYS2 installation needs updating.

The main contents page holds the subsystem and component hierarchy directly;
there are no separate navigation-only landing pages. `rbc_navigation.py` uses
these headings for expandable sidebar groups. All groups are available from
any page, while declaration entries appear only for the current reference page.
In-page contents lists omit declarations, keeping the main page compact.
`docs/theme/rbc-developer.css` wraps long sidebar names and bounds indentation.
See `building.rst` for navigation maintenance and the optional Sphinx test.
