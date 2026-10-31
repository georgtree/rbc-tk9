Implementation overview
=======================

The installed C interface is primarily the vector API in ``rbc.h`` and
``rbcVector.h``, with generated declarations and stubs. Renderer interfaces
are private; adding a renderer declaration does not add a public stub entry.

.. list-table::
   :header-rows: 1
   :widths: 24 76

   * - Layer
     - Responsibility
   * - Commands and configuration
     - Parse Tcl requests, own option transactions and component resources,
       schedule mapping and redraw.
   * - Mapping
     - Convert data through axes and track viewports into display geometry;
       retain source indices needed for picking.
   * - Components
     - Choose painter order, batches and fallback paths; supply mapped geometry
       and borrowed resources.
   * - Renderer interface
     - Dispatch supported operations, manage context lifetime and clip nesting.
   * - Backends
     - Convert geometry to native requests or document primitives, preserving
       backend rounding, batching and resource rules.
   * - Export orchestration
     - Set up documents, remap for requested size, restore screen geometry,
       finalize and deliver output.

``rbcGraph.c`` manages the common widget. Track services live in
``rbcGrTrack.c``; polar work lives in ``rbcGrPolar.c``. Components such as
``rbcGrAxis.c``, ``rbcGrLine.c`` and ``rbcGrMarker.c`` use this infrastructure.

Supporting modules
------------------

``rbcGeometry.c`` contains geometry algorithms without graph state or drawing.
``rbcScroll.c`` parses scroll requests and computes viewports; callers retain
widget mutation and redraw responsibilities. ``rbcConfig.c`` owns common option
conversion and rollback helpers. ``rbcDraw.c`` contains shared Tk/Xlib services
used outside graph rendering too, rather than a universal renderer backend.

``rbcWinDraw.c`` is Windows/Tk compatibility infrastructure used by renderers,
image and tile code. It owns private Tk drawable/DC integration and Xlib
emulation. See :doc:`rendering` for backend boundaries.

Lifetime and callbacks
----------------------

A borrowed pointer does not keep a graph, component, Tcl object or Tk resource
alive. Callers retain that responsibility. Tcl callbacks can reconfigure or
delete the owning widget; call sites invoking user code must preserve the
appropriate state and revalidate it before continuing.

Rendering is synchronous and used on the owning Tk thread. The interface does
not provide worker-thread drawing or context synchronization. An internal
renderer run callback does not authorize arbitrary Tcl reentry.

See :doc:`graph-lifecycle` for widget scheduling, callback cancellation and
the two phases of destruction. See :doc:`vectors` for borrowed numeric storage and observer lifetime.
Detailed component destruction contracts remain with their owning modules.

Object interaction
------------------

See :doc:`bindings` for component hit priority, per-family tag identities,
legend entry contexts and callback lifetime. Generic event dispatch does not
interpret component layouts; graph and legend pickers supply those decisions.
