Element data C reference
========================

These interfaces are private. See :doc:`elements` for ownership and ordering.

Data slots
----------

.. doxygenfunction:: Rbc_ParseElemVectorObj

.. doxygenfunction:: Rbc_FreeElemVector

Common data transactions
------------------------

.. doxygenfunction:: Rbc_PrepareElemDataTransaction

.. doxygenfunction:: Rbc_ElemDataTransactionPointCount

.. doxygenfunction:: Rbc_CommitElemDataTransaction

.. doxygenfunction:: Rbc_FreeElemDataTransaction

Type dispatch signatures
------------------------

The callback declarations and table are included directly to retain their exact
C signatures. They borrow the graph and element; the table does not provide
lifetime protection or make arbitrary Tcl callback reentry safe.

.. literalinclude:: ../../generic/rbcGrElem.h
   :language: c
   :start-at: typedef void(ElementDrawProc)
   :end-at: } ElementProcs;

Configuration transaction internals
-----------------------------------

These helpers separate candidate preparation from live-field commit and from
retained Tcl option representations. See :doc:`elements` for their differing
failure and cleanup rules.

.. doxygengroup:: element_transaction_internals
   :members:
