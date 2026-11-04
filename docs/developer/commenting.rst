C documentation conventions
===========================

Use Doxygen comments for maintained contracts and ordinary C comments for
local implementation explanations. Avoid mechanically converting every
historical block into reference documentation.

Where comments belong
---------------------

* Document shared functions at declarations in their owning header.
* Document static helpers beside their definitions when their algorithm or
  invariants merit a reference entry.
* Give documented files an ``@file`` description of their responsibilities.
* Describe record ownership and invariants beside the record definition.
* Keep algorithm rationale near the implementation, without duplicating
  the contract in both header and source.
* Do not hand-edit generated stub declarations.

Contract contents
-----------------

Describe inputs, supported operations, ownership, borrowed-data lifetimes,
failure reporting and mutation. Include coordinates, units, clipping, callbacks,
reentry and threading requirements where relevant. State reference-count rules
for Tcl objects and whether cleanup functions accept NULL. Do not assume these
policies are uniform throughout RBC.

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

Link functions in narrative prose with ``:c:func:`` rather than plain text or
literal markup. For private helpers rendered in a C namespace, use the qualified
target and a short displayed title, for example
``:c:func:`ClearOp <rbcVectorCommands.ClearOp>```.
Use ``@ref`` or ``@see`` for source-comment links. Reserve literal markup for Tcl
commands, options, values, and code fragments. Do not guess a target: verify it
against the generated reference with the strict build.

Use ``@param`` entries for meaningful function-specific inputs and outputs.
For standard ``interp`` and ``objc``/``objv`` semantics, add
``@param[in] interp See @ref rbc_interp_argument.`` and corresponding links
to ``rbc_command_arguments`` for ``objc``/``objv``. Add
``@see @ref rbc_calling_conventions`` for the complete contract. Describe local
exceptions explicitly. Once a function uses ``@param``, document every parameter
to retain Doxygen missing-parameter checks. Always identify
the concrete ``clientData`` type and its owner. Do not infer that a pointer is
borrowed or that an output is unchanged on error without checking the code.

Use ``@see`` or Sphinx references to connect related operations. Use the C
domain for C identifiers, for example :c:func:`Rbc_RenderEnd`.
Prose chapters explain relationships; reference signatures come from headers.

Migration and review
--------------------

Group related modules into a coherent patch, run ``make check-doc-dev``, and review
the rendered pages. Check references, table readability and retained caveats.

The reference includes selected contracts across the documented subsystems.
Undocumented helpers, parameters and record members still need review.
Strict checking validates included documentation, not coverage of all C sources.

Navigation and groups
---------------------

Each chapter and C reference belongs to one subsystem landing page. Keep existing
page filenames and group identifiers stable when reorganizing navigation.
``groups.dox`` adds parent groups to existing Doxygen groups without moving their
members. It is included in both platform documentation projects. Keep those
relationships aligned with the Sphinx subsystem pages; avoid rendering a whole
parent group recursively when its members already have dedicated reference pages.
