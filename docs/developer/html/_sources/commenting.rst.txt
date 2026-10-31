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

Use ``@see`` or Sphinx references to connect related operations. Use the C
domain for C identifiers, for example :c:func:`Rbc_RenderEnd`.
Prose chapters explain relationships; reference signatures come from headers.

Migration and review
--------------------

Document one coherent module at a time, run ``make check-doc-dev``, and review
the rendered pages. Check references, table readability and retained caveats.

The reference includes renderer contracts and selected graph lifecycle
functions. Some parameters and record members still need more detail.
Strict checking validates included documentation, not coverage of all C sources.
