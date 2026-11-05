C calling conventions
=====================

These are shared interpretation rules, not substitutes for a function's own
contract. A specific parameter, return or lifetime description takes precedence.
The same page is a Doxygen reference target, so source comments can link here
using the relevant section anchor, such as ``@ref rbc_interp_argument``
beside an interpreter parameter. The page-level target
``@ref rbc_calling_conventions`` is available for architectural overviews;
it is not a required See also entry on individual functions.

.. doxygenpage:: rbc_calling_conventions

Writing parameter contracts
---------------------------

Use ``@param[in]``, ``@param[out]`` and ``@param[in,out]`` for function-specific
arguments. State units, accepted ranges, NULL handling, ownership and the state
of output arguments on failure. For conventional interpreter and command-vector
arguments, link the common description and document only exceptions locally.
Always describe the concrete meaning of ``clientData``; it has no universal
object type. See :doc:`commenting` for examples and link syntax.
