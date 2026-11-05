Vector command C reference
==========================

Selected private handlers and helpers from ``rbcVecCmd.c``. See
:doc:`vector-commands` for dispatch, partial updates and callback limitations.

Parameter directions and interpreter ownership follow :doc:`calling-conventions`.
For list-conversion helpers, ``objc``/``objv`` describe only the value slice;
they do not include a command name. Storage capacities count typed values,
while allocation helpers explicitly identify byte counts. See the ownership
table in :doc:`vector-commands` before releasing a prepared output.

.. c:namespace:: rbcVectorCommands

.. doxygengroup:: vector_command_paths
   :members:

Data records and member contracts
---------------------------------

These records describe stored state and resource ownership. Their presence in
the reference does not make private layouts part of the installed C API.

.. doxygenstruct:: CommandOutput
   :members:

.. doxygenstruct:: NativeFmtValue
   :members:

.. doxygenstruct:: VectorInstOpSpec
   :members:
