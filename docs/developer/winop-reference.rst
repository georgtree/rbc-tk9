Window/photo command C reference
================================

See :doc:`winop-internals` for ownership, failure and platform boundaries. These are private implementation interfaces.

.. doxygengroup:: winop_commands
   :members:

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in the reference does not make private
layouts part of the installed C API.

.. doxygenstruct:: WinopOpSpec
   :members:
