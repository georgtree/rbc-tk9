Building this manual
====================

Dependencies
------------

Use Python 3 with Sphinx, the Read the Docs theme and Breathe, and Doxygen. In an MSYS2 UCRT64 shell:

.. code-block:: sh

   pacman -S --needed mingw-w64-ucrt-x86_64-doxygen \
       mingw-w64-ucrt-x86_64-python-sphinx \
       mingw-w64-ucrt-x86_64-python-sphinx_rtd_theme \
       mingw-w64-ucrt-x86_64-python-breathe

Graphviz is optional and is not used by this manual. No compiler, Tk display, installed RBC package, or Cairo library is
needed for extraction.

Targets
-------

After configuring RBC:

.. code-block:: sh

   make doc-dev
   make check-doc-dev

Both publish HTML in ``docs/developer/html`` in the source tree, including when invoked from an out-of-tree build. This
is available to GitHub Pages when it publishes the repository's ``docs`` directory. The check target also fails on
Doxygen or Sphinx warnings. Neither depends on ``binaries`` or changes the existing ``make doc`` pipeline. ``make
clean`` removes the default developer documentation build directory.

These commands also work without configuring:

.. code-block:: sh

   tclsh docs/developer/build.tcl
   tclsh docs/developer/build.tcl --strict --build-dir "_build/developer"

From a Visual Studio developer prompt in ``win``, the corresponding targets are ``nmake -f makefile.vc doc-dev`` and
``check-doc-dev``. That makefile still requires its normal Tcl/Tk build setup.

Override ``DOC_DEV_PYTHON``, ``DOXYGEN``, ``DOC_DEV_BUILD`` or ``DOC_DEV_HTML`` to select executable paths and output
location:

.. code-block:: sh

   make check-doc-dev DOC_DEV_PYTHON=/ucrt64/bin/python DOXYGEN=/ucrt64/bin/doxygen

The runner uses only Tcl core commands. Python remains required by Sphinx and Breathe, and for Sphinx's small
``conf.py`` configuration. The runner invokes Sphinx through the executable selected by ``--python`` (default:
``python``); install Breathe, Sphinx and sphinx-rtd-theme for that interpreter. Do not mix MSYS, UCRT64 and a separate
Windows Python installation accidentally. Paths containing spaces are supported. Each executable override names one
executable, not a command with extra flags.

Generated files and validation
------------------------------

Maintained sources live in ``docs/developer``. Doxygen configuration, XML, warning logs and Sphinx caches live under the
build directory. HTML is published separately under ``docs/developer/html``. Commit that directory to publish the manual
with GitHub Pages. The published pages, assets and ``.nojekyll`` are retained by ``make clean``. With the default paths, clean removes
the entire source and build root ``_build`` directories, the developer source's Python bytecode cache, and Sphinx's ``.doctrees``, ``_sources`` and
``.buildinfo`` from the developer HTML directory. Use ``--html-dir PATH`` for a different HTML destination. XML is
regenerated and all pages rebuilt on each invocation.

``Doxyfile.in`` explicitly lists the two renderer headers and the common graph header/source pair, the public/private
vector headers, and the binding header/source pair. Extraction does not follow includes into Tk or SDK headers.
Reference pages select maintained contracts; undocumented functions are not a coverage target yet. Platform and Cairo
backend implementations are not combined into misleading duplicate entries. Future chapters must select inputs
deliberately.

Static graph callbacks belong to a Doxygen group, so Breathe can render them without globally enabling
implementation-file lookup (which can duplicate header declarations on older Breathe versions). The Doxygen preprocessor
removes only the legacy ``register`` qualifier for Sphinx's C signature parser; the C source and compiler input are
unchanged.

Strict checking validates included comments and references, not whole-project coverage. Undocumented-member warnings are
disabled during this incremental migration; ``EXTRACT_ALL`` remains disabled. Extend the input list and reference pages
as each module is documented.

Developer HTML is not yet installed or included in binary distribution archives. Existing user documentation
installation is unchanged.

Windows backend extraction runs as a second Doxygen project using the same settings template but a Windows-only source
input. Its XML and warnings are stored in ``doxygen-win/xml`` and ``doxygen-win-warnings.log`` below the build
directory. Breathe selects this project explicitly for the Windows reference. Both extraction passes run on either
build host; neither compiles code or requires Windows SDK headers. Separate projects prevent identical X11/Windows
function names and private structure layouts from being merged. Strict mode checks both warning logs and restores both
XML environment variables on exit.

Doxygen callback declaration compatibility
------------------------------------------

Some Doxygen versions place a function-pointer member's closing parenthesis in its XML type instead of its argument
string. Breathe then emits invalid C such as ``void(*) text (...)``. The Tcl build driver normalizes that generated XML
before Sphinx runs, producing ``void (*text)(...)`` while retaining member contracts and link targets. Source
declarations and already-compatible XML are unchanged; no warning suppression or dependency upgrade is required.

The helper's regression checks run without Tk, RBC or documentation tools::

    tclsh tests/build/RBC.build.doxygenCompat.A.test

Theme and publishing
--------------------

Both manuals use ``sphinx_rtd_theme`` with expandable navigation and RTD's default content width. Shared styles live in
``docs/theme/rbc.css`` and ``docs/theme/rbc-layout.css``. Edit these sources, not the generated ``_static``
copies. Tables wrap within the content column; field lists use bounded columns and stack labels above descriptions on
narrow screens. These rules do not change C declaration signature layout. Images retain their natural sizes unless they
need to shrink to fit, and expandable examples retain their highlighted controls.

The developer-only ``docs/theme/rbc-developer.css`` also bounds deep sidebar indentation and wraps long C identifiers.

Both builders copy the maintained stylesheets before Sphinx computes asset hashes and verify the resulting
copies. Missing sources or mismatched output copies fail the build rather than publishing stale styles.

Keep ``docs/.nojekyll`` at the GitHub Pages publishing root. The developer build also emits
``docs/developer/html/.nojekyll`` so its output can be hosted independently. Publish each complete HTML output tree,
including ``_static`` CSS, JavaScript and fonts. The theme requires no Read the Docs hosting service. Existing page
names and C reference targets are unchanged by the theme.

After a full MSYS2 update (``pacman -Syu``), verify the selected interpreter::

   python -m sphinx --version
   python -c "import sphinx_rtd_theme, breathe; print(sphinx_rtd_theme.__file__)"

Use UCRT64 packages together; do not install the theme into a different Python from the one selected by
``DOC_DEV_PYTHON``. For non-MSYS2 environments, install ``sphinx``, ``breathe`` and ``sphinx-rtd-theme`` into the Python
environment used by the documentation builder.

Sidebar hierarchy
-----------------

``index.rst`` is the single source of navigation grouping. Subsystem and component headings contain ordinary toctrees
pointing directly to substantive chapters and reference pages. Add or move a chapter there; no navigation-only landing
page is needed. Existing substantive chapter URLs and C anchors remain unchanged. Obsolete generated ``manual-*.html``
landing pages can be removed from an existing output directory; fresh output contains none.

The local Sphinx extension ``rbc_navigation.py`` builds the sidebar from these headings. Group labels link to their
section on the main contents page; RTD's expand/collapse buttons expose their children without changing pages.
``collapse_navigation = False`` and ``navigation_depth = -1`` retain the full group hierarchy. Only declarations
belonging to the current reference page appear in the sidebar. In-page toctrees omit declarations everywhere, including
on the main contents page. Function/structure documentation and cross-reference anchors are unaffected.

Keep ``toc_object_entries = True``. Do not install the earlier ``omit_c_objects_from_navigation`` or
``contextual_sidebar`` hooks alongside this extension: the former removes declarations needed on reference pages and the
latter replaces the grouped sidebar. Breathe can populate declaration TOC entries independently of Sphinx's setting;
filtering is deliberately performed on resolved navigation trees, not on the C definitions or shared inventory.

The build runner remains Tcl. The Python module is a Sphinx extension, using Sphinx's toctree resolver to retain
reference sections and relative links. The optional regression test requires Sphinx and the RTD theme, but not Doxygen,
Breathe, Tk or RBC::

    tclsh tests/build/RBC.build.docNavigation.A.test

It checks main-page grouping and declaration suppression, full inactive groups, current-page
functions/structures/members, and preserved declaration anchors. Use ``RBC_DOC_PYTHON`` to select another Python
executable for this test.
