Legend C reference
==================

See :doc:`legends` for ownership, layout, placement, redraw and command
contracts. These are internal services, not public stubs. The documentation
namespaces below disambiguate static helper names without changing C linkage.

Creation and configuration
--------------------------

.. c:namespace-push:: rbcGrLegendConfiguration

.. doxygengroup:: legend_configuration
   :members:

.. c:namespace-pop::

Measurement and placement
-------------------------

.. c:namespace-push:: rbcGrLegendLayout

.. doxygengroup:: legend_layout
   :members:

.. c:namespace-pop::

Picking and binding identity
----------------------------

.. c:namespace-push:: rbcGrLegendBindings

.. doxygengroup:: legend_bindings
   :members:

.. c:namespace-pop::

Screen and document output
--------------------------

.. c:namespace-push:: rbcGrLegendOutput

.. doxygengroup:: legend_output
   :members:

.. c:namespace-pop::

Windows, scheduling and cleanup
-------------------------------

.. c:namespace-push:: rbcGrLegendLifetime

.. doxygengroup:: legend_lifetime
   :members:

.. c:namespace-pop::

Command handlers
----------------

.. c:namespace-push:: rbcGrLegendCommands

.. doxygengroup:: legend_commands
   :members:

.. c:namespace-pop::
