Bar element C reference
=======================

See :doc:`bars` for configuration boundaries, grouping passes, mapped storage,
rendering and cleanup. These routines belong to ``rbcGrBar.c``. The exported
``Rbc_`` names are internal cross-module services, not additions to the public
vector stubs API. Namespaces below disambiguate private documentation symbols;
they do not change C linkage.

Configuration and pens
-----------------------

.. c:namespace-push:: rbcGrBarConfiguration

.. doxygengroup:: bar_configuration
   :members:

.. c:namespace-pop::

Grouping and placement
-----------------------

.. c:namespace-push:: rbcGrBarLayout

.. doxygengroup:: bar_layout
   :members:

.. c:namespace-pop::

Geometry and lifetime
---------------------

.. c:namespace-push:: rbcGrBarGeometry

.. doxygengroup:: bar_geometry
   :members:

.. c:namespace-pop::

Screen drawing and document output
-----------------------------------

.. c:namespace-push:: rbcGrBarOutput

.. doxygengroup:: bar_output
   :members:

.. c:namespace-pop::
