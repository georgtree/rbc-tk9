Typed command dispatch
=======================

:c:func:`Rbc_GetOpIndexFromObj` in ``rbcUtil.c`` selects an operation and validates its argument count. It does not
invoke the callback, own a command object or manage component lifetime. Each component supplies a persistent typed table
whose first member is :c:struct:`Rbc_OpSpecHeader`, followed by its correctly typed callback and any component-specific
fields.

Table layout and lifetime
-------------------------

The first header field is the operation name; a ``NULL`` name terminates the array. The caller passes sizeof of the
complete entry as ``specSize``, not sizeof of the header or callback. The helper uses that byte stride both for Tcl's
indexed-name lookup and to locate the selected header. Keeping dispatch at the caller avoids casting unrelated function
pointer types through a shared generic signature.

``Tcl_GetIndexFromObjStruct`` accepts unique operation-name abbreviations and caches lookup information in the Tcl
object. Tables and their name strings must outlive objects that can reuse that information. Function-local temporary
arrays are not suitable replacements for the usual static const tables.

Argument counts and diagnostics
-------------------------------

``operPos`` identifies the operation word in the complete objc/objv vector. Both ``minArgs`` and ``maxArgs`` count every
word in that vector, including the widget or command name and intervening subcommands. A nonpositive maximum is
unbounded.

For a missing operation word the helper reports a generic subcommand usage. For an unknown or ambiguous operation it
retains Tcl's lookup error. Once an operation is selected, a count error uses its usage suffix and the prefix through
the operation word. ``indexPtr`` is written only after all checks succeed; on error callers must not dispatch using an
old or uninitialized index.

A nested dispatcher that shifts ``objc`` / ``objv`` must define its counts against the vector it actually passes to
lookup. For example, the winop image dispatcher validates its own nested table before removing the image word for the
shared handler. See :doc:`winop-internals`.

Selection versus execution
--------------------------

The helper asserts a non-``NULL`` table/output pointer and an adequate stride in assert-enabled builds. It does not
validate arbitrary memory, ensure callback signatures match, or check an entire table's structural consistency at
runtime. The table declaration and caller form that contract.

Successful selection also says nothing about callback safety. A selected operation can evaluate Tcl, change vector
storage or destroy its widget. The owning command must preserve/revalidate the appropriate resources and return the
handler's status. See :doc:`graph-lifecycle`, :doc:`expressions` and :doc:`bindings` for those separate boundaries.

Maintenance checks
------------------

When adding an operation, test exact names, accepted abbreviations, ambiguity, missing arguments and excess arguments in
the existing component test family. Check nested paths independently. A header or stride change should also pass ``make
test-headers``. Reference documentation describes the common selector; the individual Tcl command manuals remain the
source for user-facing syntax.

Lookup inputs and output
------------------------

:c:func:`Rbc_GetOpIndexFromObj` receives the byte stride of a persistent typed table and a nonnegative operation-word
index. Argument limits count the entire command, including widget and operation words. The returned integer index is
assigned only after both name lookup and arity validation succeed; the caller then dispatches through its own typed
entry.

Operation families and their record types
-----------------------------------------

Most RBC commands that look like Tcl ensembles are a C object command plus one or more dispatch tables. In ``.g axis
configure x -min 0``, ``graphOps`` selects the axis handler at word 1. That handler looks up ``configure`` at word 2 in
its own table. Both count the widget command in the total argument count. There is no minimum-abbreviation field: the
first integer after the name is minimum **argument count**. Tcl resolves a unique prefix against the table passed to
:c:func:`Rbc_GetOpIndexFromObj`.

.. list-table::
   :header-rows: 1
   :widths: 27 32 41

   * - Command family
     - Table (in defining module)
     - Row type or dispatch rule
   * - Graph widget
     - ``graphOps``
     - :c:struct:`GraphOpSpec`; selects the next component or widget handler.
   * - Named axes / margin axes
     - Two function-local arrays both named ``axisOps``
     - :c:struct:`AxisVirtualOpSpec` routes named-axis commands; :c:struct:`AxisOpSpec` passes the selected margin.
   * - Elements, markers and pens
     - ``elemOps``, ``markerOps``, ``penOps``
     - :c:struct:`ElementOpSpec`, :c:struct:`MarkerOpSpec`, :c:struct:`PenOpSpec`.
   * - Grid / crosshairs
     - ``gridOps``, ``xhairOps``
     - :c:struct:`GridOpSpec` and :c:struct:`CrosshairsOpSpec`.
   * - Widget legend / track legend
     - ``legendOps``, ``trackLegendOps``
     - Both use :c:struct:`rbcGrLegendCommands.LegendOpSpec`; sharing a row type does not imply the same set of
       operations.
   * - PostScript
     - ``psOps``
     - :c:struct:`PostScriptOpSpec`; its page and output policy is separate from SVG/PDF.
   * - Vector management / vector instance
     - ``vectorOpCmd``, ``vectorInstOpCmd``
     - :c:struct:`VectorOpSpec` and :c:struct:`rbcVectorCommands.VectorInstOpSpec`.  The latter passes the receiver to
       its handler.
   * - Spline
     - ``splineOps``
     - :c:struct:`SplineOpSpec`.
   * - Window operations / image subcommands
     - ``winOps``, ``imageOps``
     - Both use :c:struct:`WinopOpSpec`; the operation word is at a different index.

Two dispatchers illustrate why a callback should not be assumed from an operation name alone:

* :c:func:`Rbc_TrackOp` indexes an array of :c:struct:`Rbc_OpSpecHeader` directly. There is no function-pointer
  field. The local ``TRACK_OP_*`` enum mirrors the row order; inserting or moving a row requires the corresponding enum
  change.
* :c:func:`Rbc_GraphExportOp` looks up ``cget``, ``configure`` and ``output`` in a null-terminated string array and
  checks the selected form itself.  :c:struct:`rbcGraphExportCommands.Rbc_GraphExportFormat` supplies the concrete
  option table and document lifecycle callbacks.

Not every table of names dispatches commands. In ``rbcGrMarker.c``, ``markerTypeMap`` associates a type name with an
interned marker identifier; ``markerSearchMap`` associates ``enclosed`` and ``overlapping`` with search modes. Neither
record has a usage string or a callback. Likewise, in ``rbcGraph.c``, the local ``barModes`` table maps both ``normal``
and ``infront`` to the same bar-placement selector. ``SnapFormatS`` pairs a format name with the local ``SnapFormatE``
value; the available formats depend on the platform. These records only translate an argument for the operation which
owns them.
