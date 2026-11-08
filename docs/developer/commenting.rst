C documentation conventions
===========================

Use Doxygen comments for maintained contracts and ordinary C comments for local implementation explanations. Avoid
mechanically converting every historical block into reference documentation.

Where comments belong
---------------------

* Document shared functions at declarations in their owning header.
* Document static helpers beside their definitions when their algorithm or invariants merit a reference entry.
* Give documented files an ``@file`` description of their responsibilities.
* Describe record ownership and invariants beside the record definition.
* Keep algorithm rationale near the implementation, without duplicating the contract in both header and source.
* Do not hand-edit generated stub declarations.

C function names
----------------

Use ``Rbc_`` followed by PascalCase for RBC-owned functions shared between C translation units, including private
interfaces. Public C functions use the same prefix; public API status is determined by installed headers and the stubs
interface, not by the spelling alone. Keep public symbols and stub entries stable.

Use unprefixed PascalCase for file-local ``static`` functions, including callbacks. Shared ``static inline`` helpers
in headers retain the ``Rbc_`` prefix because they form a shared source interface.

Keep externally prescribed names for platform entry points such as ``DllMain`` and ``DllEntryPoint``, and for
conditional compatibility replacements such as ``strcasecmp``, ``strncasecmp`` and the legacy ``strtolower`` fallback.
RBC-owned adapters around Tcl/Tk facilities use ``Rbc_`` rather than claiming the ``Tcl_`` or ``Tk_`` namespace.

When renaming private functions, update declarations, definitions, callers, Doxygen references and Sphinx C-domain
links together, then run the strict developer-manual build.

Contract contents
-----------------

Describe inputs, supported operations, ownership, borrowed-data lifetimes, failure reporting and mutation. Include
coordinates, units, clipping, callbacks, reentry and threading requirements where relevant. State reference-count rules
for Tcl objects and whether cleanup functions accept NULL. Do not assume these policies are uniform throughout RBC.

An illustrative contract (not an existing function):

.. code-block:: c

   /**
    * @brief Create mapped geometry for one component.
    * @param[in] graphPtr Borrowed graph, valid throughout the call.
    * @param[out] result Receives caller-owned storage on success.
    * @return TCL_OK on success; TCL_ERROR with an interpreter diagnostic.
    *
    * Coordinates are widget pixels. On failure, result is unchanged.
    * Release successful output with the matching destructor.
    */

Link functions in narrative prose with ``:c:func:`` rather than plain text or literal markup. For private helpers
rendered in a C namespace, use the qualified target and a short displayed title, for example ``:c:func:`ClearOp
<rbcVectorCommands.ClearOp>```.

Use ``@ref`` or ``@see`` for source-comment links. Reserve literal markup for Tcl commands, options, values, and code
fragments. Do not guess a target: verify it against the generated reference with the strict build.

Use ``@param`` entries for meaningful function-specific inputs and outputs. For standard ``interp`` and
``objc``/``objv`` semantics, add ``@param[in] interp See @ref rbc_interp_argument.`` and corresponding links to
``rbc_command_arguments`` for ``objc``/``objv``. Put the link beside the parameter it explains, and describe local
exceptions explicitly. Do not append a blanket ``@see @ref rbc_calling_conventions`` to every function: an already
complete geometry or numeric parameter description does not benefit from it. Once a function uses ``@param``, document
every parameter to retain Doxygen missing-parameter checks. Always identify the concrete ``clientData`` type and its
owner. Do not infer that a pointer is borrowed or that an output is unchanged on error without checking the code.

Use ``@see`` or Sphinx references to connect related operations. Link a shared lifetime section only when it explains a
concrete callback, reentry or ownership requirement in the local contract. A pointer argument alone is not sufficient
reason to add a lifetime link. Use the C domain for C identifiers, for example :c:func:`Rbc_RenderEnd`. Prose chapters
explain relationships; reference signatures come from headers.

Migration and review
--------------------

Group related modules into a coherent patch, run ``make check-doc-dev``, and review the rendered pages. Check
references, table readability and retained caveats.

The reference includes selected contracts across the documented subsystems. Undocumented helpers, parameters and record
members still need review. Strict checking validates included documentation, not coverage of all C sources.

Navigation and groups
---------------------

Each chapter and C reference belongs to one subsystem landing page. Keep existing page filenames and group identifiers
stable when reorganizing navigation. ``groups.dox`` adds parent groups to existing Doxygen groups without moving their
members. It is included in both platform documentation projects. Keep those relationships aligned with the Sphinx
subsystem pages; avoid rendering a whole parent group recursively when its members already have dedicated reference
pages.

Extraction checks for declarations and platform branches
--------------------------------------------------------

A comment can exist in source without producing the intended function entry. In particular, function-typedef
declarations for argument-free marker constructors were extracted as variables. Use explicit matching prototypes for
these helpers and check that their Doxygen member kind is function. Keep parameter names in forward declarations
consistent with definitions so generated signatures and parameter descriptions agree.

Windows-only helpers require the Windows XML input list and an explicit ``:project: rbc-win`` reference when the normal
project cannot see their branch. Do not enable WIN32 globally in the default project to expose one function. Private
declarations with no implementation or caller should be removed rather than documented as supported interfaces. Public
installed headers and stubs require a separate compatibility review.

The byte-comparison fallbacks use an RBC_DOXYGEN branch so their contracts remain visible even when the configured
platform has native replacements. This selector is for extraction only and is not enabled in ordinary compilation.

Contract and member comment placement
-------------------------------------

Keep one authoritative function contract: normally the declaring header for shared interfaces, and the definition for
file-local helpers. Remove obsolete implementation banners when the header already describes the contract. Keep
implementation comments that explain an algorithm, platform distinction or resource transition; do not copy the header
contract into a second location.

Document each structure's purpose and ownership, then attach field descriptions using Doxygen member comments (``/**<
... */`` or a preceding documentation block). Include units, valid counts, optional/sentinel values and borrowed versus
owned resources where relevant. Ordinary adjacent comments do not automatically become member documentation. Check that
the structure is actually included in the reference and that its members appear in the rendered output.

File headers
------------

Use one Doxygen file comment at the top of a module: its actual filename,
a brief statement of the module's responsibility, then the existing copyright
and license notice. Retain attribution and redistribution terms when merging
an older banner. Give each ``@defgroup`` its own comment, outside the file
comment, so a group description cannot become the brief for the whole file.
