Vector edit C reference
=======================

These private handlers are implemented in ``rbcVecCmd.c``. Their runtime limitations are described in
:doc:`vector-operations`.

Instance handlers receive the full command argument array: command name at ``objv[0]``, operation at ``objv[1]``, then
operation arguments. The dispatcher retains the receiver and interpreter; callbacks can still destroy their logical
state. See :doc:`calling-conventions` and :doc:`vector-commands`.

.. doxygengroup:: vector_edit_paths
   :members:
