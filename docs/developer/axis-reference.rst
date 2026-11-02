Axis control C reference
========================

These implementation contracts cover registry lifetime, configuration entry
points and command dispatch in ``rbcGrAxis.c``. See :doc:`axes` for ownership
and error boundaries and :doc:`mapping-reference` for coordinate helpers.
Namespaces below disambiguate private handler names without changing linkage.

Registry and lifetime
---------------------

.. c:namespace-push:: rbcGrAxisRegistry

.. doxygengroup:: axis_registry
   :members:

.. c:namespace-pop::

Configuration entry points
--------------------------

.. c:namespace-push:: rbcGrAxisConfiguration

.. doxygengroup:: axis_configuration
   :members:

.. c:namespace-pop::

Commands and bindings
---------------------

.. c:namespace-push:: rbcGrAxisCommands

.. doxygengroup:: axis_commands
   :members:

.. c:namespace-pop::

Option transaction internals
----------------------------

.. c:namespace-push:: rbcGrAxisTransactions

.. doxygengroup:: axis_transactions
   :members:

.. c:namespace-pop::

Ranges, coordinates and tick sweeps
-----------------------------------

.. c:namespace-push:: rbcGrAxisRanges

.. doxygengroup:: axis_ranges
   :members:

.. c:namespace-pop::

Labels, geometry and presentation
---------------------------------

.. c:namespace-push:: rbcGrAxisPresentation

.. doxygengroup:: axis_presentation
   :members:

.. c:namespace-pop::

Layout, queries, scrolling and notifications
--------------------------------------------

.. c:namespace-push:: rbcGrAxisLayoutControl

.. doxygengroup:: axis_layout_control
   :members:

.. c:namespace-pop::
