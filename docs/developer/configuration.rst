Component configuration and resources
=====================================

Configuration belongs to the component that owns the record. Shared helpers
in ``rbcConfig.c`` provide name lookup, option-database initialization and
rollback; they do not own graph scheduling or component-derived resources.
Their private declarations are in ``rbcInt.h``. The similarly named
``rbcConfig.h`` contains legacy configuration macros, not this helper interface.
See :doc:`configuration-reference` for the extracted contracts.

Resource identity
-----------------

A component's command name and its option-database resource name serve different
purposes. ``Rbc_GetComponentResourceName`` creates an allocated resource-name
copy with only the first byte lowercased. Callers release the copy with
``ckfree``; the component retains its original command identity. This preserves
legacy child-name matching rather than applying Unicode case folding.

``Rbc_InitComponentOptions`` receives the resource name and class separately.
It first looks for an existing real child of the graph with that name. When
found, initialization uses that child's existing class. Otherwise the helper
reuses the graph's anonymous option proxy, setting its name and class for this
lookup. A NULL proxy falls back to the parent window's option hierarchy.
Explicit command arguments are applied separately after initialization.

The proxy is not a visible component widget and has no registered Tk pathname.
A temporary pathname supplies meaningful initialization errors and is cleared
before returning. The helper refreshes the parent's option-database cache
before changing the proxy identity, so an intervening option-database update
is not lost when Tk rebuilds the lookup stack. These Tk window details stay
inside the helper; callers own the proxy lifetime.

Do not infer a separate option-database component from every Tcl subcommand.
For example, a track has a named ``Track`` component, whereas a local grid
materializes inherited grid settings through its own initialization path.
Local-grid queries use a temporary view without creating a persistent local
grid or freezing its inherited defaults.

Transactions
------------

Tk-managed values and derived resources have separate commit points. A typical
component configuration follows this sequence:

1. Clear stale option masks and borrowed argument pointers.
2. Call ``Tk_SetOptions`` with a ``Tk_SavedOptions`` record. On failure, return
   its error: Tk has already restored the option values.
3. Validate relationships and prepare replacements for derived resources.
   Keep old active resources until all fallible work succeeds.
4. On validation failure, clear borrowed transaction context and call
   ``Rbc_RestoreOptionsOnError``. It consumes the saved transaction while
   preserving the original interpreter result, error code and error information.
5. On success, commit prepared resources and component relationships, clear
   borrowed context and call ``Tk_FreeSavedOptions`` to discard old option values.
   Schedule mapping or redraw through the owning component's normal path.

The rollback helper does not undo arbitrary C mutations, restore graphics
contexts or reverse callbacks. Calling it after committing a derived resource
cannot make that resource transactional. Likewise, do not free saved options
again after rollback. Initialization failure and destruction have their own
cleanup paths; a saved configuration transaction is not a general destructor.

``ConfigureElementOptions`` in ``rbcGrElem.c`` is a concrete example. It exposes
the changed-option mask and borrowed original argument vector to the concrete
element configurator. For trackgraphs it first validates that the requested
track exists and that the mapped Y axis belongs to it. Only successful
configuration commits the element's track pointer. Both success and failure
clear the borrowed arguments before releasing or restoring saved options.
Graph configuration additionally stages resources such as tiles and title
shadows; consult its implementation rather than assuming that Tk rollback
covers those resources.

Lookup and configure metadata
-----------------------------

Ordinary Tk option records use Tk's query machinery. Custom component commands
can share a concrete ``Tk_OptionSpec`` array between initialization, lookup and
metadata without constructing a temporary live record for each query.
``Rbc_GetComponentOptionIndex`` accepts unique abbreviations through Tcl's
indexed-name lookup. Its array contains no synonyms and must outlive option-name
objects that cache references to it.

``Rbc_GetComponentOptionInfo`` combines one specification with a caller-supplied
live value to produce the usual five fields: option name, resource name,
resource class, default and current value. The returned list owns a reference
to the value, without taking away any reference already held by the caller.

``Rbc_GetCanonicalOptionName`` serves configuration bookkeeping for full Tk
specification tables, including chained tables and synonym entries. It returns
a borrowed specification string, with no interpreter diagnostic on failure.
It is not a value parser and does not replace ``Tk_SetOptions`` validation.

Cleanup boundaries
------------------

Tk option resources must be released with the matching option table while the
required Tk window context is still available. Derived resources, observers,
bindings and record storage remain with the owning component. Avoid freeing
the same resource both through Tk and through manual component cleanup.

For example, ``ReleaseGraphOptionResources`` uses initialization and release
flags to guard repeated cleanup. It clears configuration argument context,
calls ``Tk_FreeConfigOptions``, releases the option table, and clears the table
pointer. Graphics contexts, the title shadow and tile are handled separately.
See :doc:`graph-lifecycle` for cleanup around window destruction and deferred
record release.

When changing a component, check failed initialization as well as failed
reconfiguration. Review custom option set, restore and free procedures together:
they form one ownership protocol. Existing tests should cover rejected values
without losing the previous live configuration, resource-database defaults,
metadata consistency and destruction after partial setup.
