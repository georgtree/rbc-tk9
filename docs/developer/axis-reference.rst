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
