C calling conventions
=====================

These are shared interpretation rules, not substitutes for a function's own
contract. A specific parameter, return or lifetime description takes precedence.
The same page is a Doxygen reference target, so source comments can link here
with ``@ref rbc_calling_conventions``.

.. doxygenpage:: rbc_calling_conventions

Writing parameter contracts
---------------------------

Use ``@param[in]``, ``@param[out]`` and ``@param[in,out]`` for function-specific
arguments. State units, accepted ranges, NULL handling, ownership and the state
of output arguments on failure. For conventional interpreter and command-vector
arguments, link the common description and document only exceptions locally.
Always describe the concrete meaning of ``clientData``; it has no universal
object type. See :doc:`commenting` for examples and link syntax.
