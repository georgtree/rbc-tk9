Package loading and stubs
=========================

The package index, ``generic/rbcInt.c`` and ``generic/rbcStubLib.c`` divide
loading into Tcl-only initialization, Tk command registration and client-side
C API setup. See :doc:`initialization-reference` for public interfaces and private helper contracts.

Loader sequence
---------------

``pkgIndex.tcl.in`` chooses the Tcl 9 or Tcl 8 library filename from the running
Tcl version and registers three package scripts:

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Package
     - Loading work
   * - ``rbc::vector``
     - Loads the shared library with entry-point prefix ``Rbc``. :c:func:`Rbc_Init`
       initializes Tcl stubs and vector commands without requiring Tk.
   * - ``rbc``
     - Requires the exact matching vector package and Tk, invokes
       ``::rbc::_initTk``, sets ``::rbc::library`` and sources ``graph.tcl``
       followed by ``trackgraph.tcl``.
   * - ``rbc::graphtoolbar``
     - Requires the exact matching full package, sources ``graphtoolbar.tcl``
       and provides the toolbar package after the script completes.

The toolbar script prefers installed argparse and uses the bundled Tcl-only
copy when argparse is missing. Full RBC loading does not source the toolbar.
The legacy graph helpers remain part of full loading for compatibility.

A direct ``load`` of the binary establishes the vector package only. Embedders
needing widgets and their Tcl support scripts should require ``rbc`` rather
than treating successful binary loading as full initialization. The private
``_initTk`` command is a loader step, not a replacement public loading API.

Interpreter state and registration
----------------------------------

``RbcInitData`` is associated with the interpreter under ``rbc::init``. Its
five flags record completed vector, graph, winop, spline and EPS initialization.
:c:func:`FreeInitData <rbcInitialization.FreeInitData>` frees this bookkeeping record at interpreter teardown; it does
not own every object created by those subsystems.

:c:func:`Rbc_Init` creates ``::rbc`` if needed, initializes the vector subsystem once,
creates ``::rbc::_initTk`` and exports ``vector``. Vector initialization creates
both ``::rbc::vector`` and ``::rbc::vector::simplify``. Its registry, numerical
function tables and vector cleanup belong to a separate interpreter-associated
``VectorInterpData`` record; see :doc:`vectors`.

``_initTk`` first initializes Tk stubs, then registers the graph constructors,
winop and spline commands, followed by the EPS canvas item. It exports
``barchart``, ``graph``, ``polar``, ``spline``, ``stripchart``, ``trackgraph`` and
``winop``. Graph registration installs component UIDs and constructor commands;
it does not create a graph window. Each later constructor owns its widget
instance; see :doc:`graph-lifecycle`.

Flags are per interpreter, even though the shared library is loaded once in a
process. They do not make vectors transferable between interpreters, and do not
turn process-global stubs pointers or Tk services into thread-safe interfaces.
Use vector and widget objects in their owning interpreter and thread.

Failure boundaries
------------------

Each successful phase sets its readiness flag before the next phase runs.
A later error leaves earlier commands and flags in place. A retry skips those
completed phases; initialization is not one rollback transaction. These flags
also do not detect or recreate commands deliberately removed after loading.

The C Tk initializer provides ``rbc`` before the package script sources its Tcl
files. A sourcing error can therefore leave native commands and the provided
package record installed even though the support script did not finish.
Loading code must report that error; checking only ``package provide rbc`` is
not proof that every support script completed. This is an existing boundary,
not a guarantee of automatic recovery on a subsequent require.

Client stubs and ABI maintenance
--------------------------------

A consumer links the RBC stub library and calls :c:func:`Rbc_VectorInitStubs` for
Tcl-only vector access or :c:func:`Rbc_InitStubs` when it needs the full package.
Both use ``Tcl_PkgRequireEx`` and assign its client data to the consumer's
``rbcStubsPtr``. Both packages publish the same table from ``rbcStubInit.c``.
Successful initialization selects functions; it does not own vectors or retain
the interpreter. See :doc:`vector-reference` for storage ownership and
read-only checks.

The pointer belongs to the linked stub object, not to each interpreter. A
failed initialization leaves its previous value unchanged. Consumers must check
the return value rather than use a previously non-NULL pointer as evidence
that loading succeeded in another interpreter. The helpers do not validate the
table magic or individual slots; a NULL package client-data pointer returns
NULL without adding its own diagnostic.

``generic/rbc.decls`` defines numbered public slots. ``rbcDecls.h`` supplies
the table type, declarations and client dispatch macros; ``rbcStubInit.c``
supplies the implementation addresses. Preserve existing slot numbers and
signatures when extending the ABI, and update generated sections through
``tools/genStubs.tcl``. Require a package version containing every slot used by
a consumer. Private renderer or graph declarations do not become public APIs
merely by being documented; see :doc:`headers` and :doc:`architecture`.

Loader callbacks versus package initialization
----------------------------------------------

On Windows, DllMain accepts every loader notification and DllEntryPoint simply
forwards to it. Neither performs registration, allocation or subsystem cleanup.
Loading the DLL and initializing its Tcl package are distinct steps; none of the
per-interpreter readiness flags is set by the native loader callbacks.

:c:func:`InitTkObjCmd <rbcInitialization.InitTkObjCmd>` assumes that :c:func:`Rbc_Init` has already installed the associated record
and namespace. It checks arity before Tk initialization. Readiness flags guard
registration phases only: namespace exports and package provision still run on
a later call even when all subsystem flags are set. EPS registration is marked
complete after its void initializer returns, unlike phases with a Tcl status.
Deleting associated data or manually reconstructing this private command is not
a supported reset mechanism.

The client stub object likewise owns no subsystem state. Its table pointer is
shared by calls using that linked object, so a successful require in one
interpreter does not establish initialization in another. Always check the
returned version pointer for each initialization attempt. A previous dispatch
table remains selected on failure, including the case where a package exists
but publishes no client data. Native package teardown does not free this borrowed
pointer through :c:func:`FreeInitData <rbcInitialization.FreeInitData>`.
