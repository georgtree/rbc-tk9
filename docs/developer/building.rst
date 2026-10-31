Building this manual
====================

Dependencies
------------

Use Python 3 with Sphinx and Breathe, and Doxygen. In an MSYS2 UCRT64 shell:

.. code-block:: sh

   pacman -S --needed mingw-w64-ucrt-x86_64-doxygen \
       mingw-w64-ucrt-x86_64-python-sphinx \
       mingw-w64-ucrt-x86_64-python-breathe

Graphviz is optional and is not used by this manual. No compiler, Tk
display, installed RBC package, or Cairo library is needed for extraction.

Targets
-------

After configuring RBC:

.. code-block:: sh

   make doc-dev
   make check-doc-dev

Both publish HTML in ``docs/developer/html`` in the source tree, including
when invoked from an out-of-tree build. This is available to GitHub Pages
when it publishes the repository's ``docs`` directory.
The check target also fails on Doxygen or Sphinx warnings. Neither depends on
``binaries`` or changes the existing ``make doc`` pipeline.
``make clean`` removes the default developer documentation build directory.

These commands also work without configuring:

.. code-block:: sh

   tclsh docs/developer/build.tcl
   tclsh docs/developer/build.tcl --strict --build-dir "_build/developer"

From a Visual Studio developer prompt in ``win``, the corresponding targets
are ``nmake -f makefile.vc doc-dev`` and ``check-doc-dev``. That makefile still
requires its normal Tcl/Tk build setup.

Override ``DOC_DEV_PYTHON``, ``DOXYGEN``, ``DOC_DEV_BUILD`` or ``DOC_DEV_HTML`` to select
executable paths and output location:

.. code-block:: sh

   make check-doc-dev DOC_DEV_PYTHON=/ucrt64/bin/python DOXYGEN=/ucrt64/bin/doxygen

The runner uses only Tcl core commands. Python remains required by Sphinx and
Breathe, and for Sphinx's small ``conf.py`` configuration. The runner invokes
Sphinx through the executable selected by ``--python`` (default: ``python``); install
Breathe and Sphinx for that interpreter. Do not mix MSYS, UCRT64 and a separate Windows
Python installation accidentally. Paths containing spaces are supported.
Each executable override names one executable, not a command with extra flags.

Generated files and validation
------------------------------

Maintained sources live in ``docs/developer``. Doxygen configuration, XML,
warning logs and Sphinx caches live under the build directory. HTML is published
separately under ``docs/developer/html``. Commit that directory to publish the
manual with GitHub Pages. It is retained by ``make clean``. Use
``--html-dir PATH`` for a different HTML destination. XML is regenerated
and all pages rebuilt on each invocation.

``Doxyfile.in`` explicitly lists the two renderer headers and the common graph
header/source pair. Extraction does not follow includes into Tk or SDK headers.
Reference pages select maintained contracts; undocumented functions are not a
coverage target yet. Platform and Cairo backend implementations are not combined
into misleading duplicate entries. Future chapters must select inputs deliberately.

Static graph callbacks belong to a Doxygen group, so Breathe can render them
without globally enabling implementation-file lookup (which can duplicate
header declarations on older Breathe versions). The Doxygen preprocessor removes
only the legacy ``register`` qualifier for Sphinx's C signature parser; the C
source and compiler input are unchanged.

Strict checking validates included comments and references, not whole-project
coverage. Undocumented-member warnings are disabled during this incremental
migration; ``EXTRACT_ALL`` remains disabled. Extend the input list and reference
pages as each module is documented.

Developer HTML is not yet installed or included in binary distribution archives.
Existing user documentation installation is unchanged.
