Binding dispatch and picking
============================

RBC's component bindings use Tk's binding engine, but RBC determines which
object receives an event and which binding keys apply to it. These private
interfaces are separate from ordinary widget/class bindings and are not part
of the vector stubs API. All picking and script dispatch occurs on the Tk thread.

Responsibilities
----------------

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Module
     - Responsibility
   * - ``rbcBind.c``
     - Register the window event handler, remember pointer/button/focus state,
       synthesize object crossing events, collect keys and call Tk_BindEvent.
   * - ``rbcGraph.c``
     - Pick axes, elements, markers and track backgrounds; translate the explicit
       component context into the component's binding keys.
   * - ``rbcGrLegd.c``
     - Pick global/local legend entries, resolve overlapping legend rectangles,
       and supply keys from the selected legend's own tag table.
   * - Component modules
     - Own hit geometry and tag tokens, expose bind commands, and invalidate
       saved binding targets before deleting their component records.
   * - ``rbcGrTrack.c``
     - Request stationary-pointer repicking after native track layout changes.

The common graph has a binding table for components. The global legend has a
separate table, and track legends use a shared graph-owned local-legend table.
These tables have their own current/focus state; there is no single universal
current object for every kind of interaction.

Objects, contexts and binding keys
----------------------------------

A picker returns a borrowed object pointer and a context value. The generic
binding layer compares both values when deciding whether the pointer has
entered a different target. It does not inspect component record layouts.

For graph components, the context encodes ``GraphBindContext`` (axis, element,
marker or track). For a legend entry, the object is an element and the context
is the owning ``Legend`` pointer. The same element appearing in different
legends therefore does not imply the same binding target.

The tag callback turns the object/context into ordered pointer-identity keys.
These are the keys used by Tk's binding table, not necessarily the addresses of
the picked objects:

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Target
     - Key order and storage
   * - Axis, element or marker
     - Name, class, then configured tags; keys are interned in that component
       family's graph-owned tag table.
   * - Track background
     - Track pointer, then the private all-track token ``&graphPtr->tracks``.
   * - Legend entry
     - Element name, class, then configured tags, interned in the selected
       legend's tag table.
   * - Generic table without a tag callback
     - Tk UID for ``all``, then the item pointer.

Do not replace these keys with arbitrary equal strings: pointer identity is
what joins configuration and event dispatch. Separate token tables also prevent
an element tag from accidentally matching an axis or marker tag with the same
spelling. Deleting an object pointer does not implicitly delete every name/class
binding that its tag callback may have supplied.

Graph hit priority
------------------

``PickEntry`` first updates layout when ``MAP_ALL`` is pending. Layout can invoke
axis-format scripts, so a hit test is not necessarily free of Tcl callbacks.
If layout is cancelled, picking returns no object.

A local legend's visible rectangle excludes ordinary graph hits. Outside the
plot extents, the picker considers axes. Inside the plot, it tests:

1. Markers drawn above elements.
2. Eligible elements, traversing the display list backward and using each
   element's ``closestProc`` to update a common distance search.
3. Markers drawn below elements.
4. Track backgrounds, excluding legend rectangles and the gaps between tracks.

An element must be visible, not plot-hidden, and in normal state. Dirty element
mapping or pending X/Y vector notification excludes it from this search.
Track-owned elements additionally require a usable viewport and a pointer
inside it. ``Rbc_EnterTrack`` and ``Rbc_LeaveTrack`` bracket the element's
closest-point calculation. This preserves shared component algorithms while
using the correct track transformation.

The element search uses the graph halo, guarded against integer overflow, and
``SEARCH_AUTO``. Component ``closestProc`` implementations decide geometric
distance and point/segment details. This chapter does not replace those
algorithms with a generic rectangular hit test.

Legend picking and occlusion
----------------------------

``LegendAtPoint`` resolves raised legends before lowered legends, testing the
global legend then track legends within each pass. External global legends use
their own window. ``LegendContains`` tests the full visible legend rectangle,
including blank padding; ``PickLegendIn`` separately determines whether that
location selects an entry. Thus blank legend padding can occlude a track
background without becoming an element hit.

Local legend hits must also lie inside their track's viewport. Entry lookup
uses reverse element display order and filters for the owning track. One-row
legends use each entry's actual width; multi-row legends use row/column cell
geometry. Keep picking consistent with drawing when changing legend layout.

Pointer transitions and button state
------------------------------------

``BindProc`` receives Tk events and updates the remembered modifier state:

.. list-table::
   :header-rows: 1
   :widths: 28 72

   * - Event
     - Processing order
   * - Button press
     - Repick with the pre-press state, update the logical button state, dispatch.
   * - Button release
     - Dispatch to the existing target, repick with the post-release state,
       restore the borrowed event's original state field.
   * - Motion
     - Repick, then dispatch motion to the resulting current target.
   * - Enter / Leave
     - Repick; object crossing events are synthesized by that operation.
   * - Key press / release
     - Dispatch using current state; an explicit focus object takes precedence.

``PickCurrentItem`` saves a crossing-shaped pointer event for later reuse.
A window Leave means no target and bypasses hit testing. When the target changes
without a button grab, it sends Leave to the old target and Enter to the new
one, using ``NotifyAncestor`` detail.

Before Leave is evaluated, the pending target is stored in ``newItem`` and
``newContext``. If the Leave script deletes that destination, component cleanup
can clear those fields through :c:func:`Rbc_DeleteBindings`; the picker then
avoids Enter on the deleted object. ``REPICK_IN_PROGRESS`` prevents recursive
Leave-transition processing, although a nested call can update the saved event.

In the normal build, moving to another target while a button is held marks
``LEFT_GRABBED_ITEM`` and retains the current target until release. The source
also contains a ``FULLY_SIMULATE_GRAB`` conditional path for additional crossing
behavior; its presence is not a promise that ordinary builds deliver those
extra transitions. Button-grab tracking here uses Button1 through Button5 masks.

The focus/current setter macros in ``rbcBind.h`` only assign pointer/context
pairs. They do not synthesize crossing events, query geometry or preserve the
assigned objects. Use them only when the owning component also maintains the
corresponding lifetime and state invariants.

Dispatch and configuration
--------------------------

``DoEvent`` collects the keys and invokes ``Tk_BindEvent`` synchronously. Up to
32 keys use stack storage; larger lists use a temporary allocated array. RBC
owns these containers, while the owner supplies stable key storage. Tk handles
binding matching and script evaluation. The tag callback itself should collect
keys without evaluating Tcl or destroying the binding table.

:c:func:`Rbc_ConfigureBindingsFromObj` accepts an already validated argument
count: no arguments list sequences, one queries a sequence, and two set a
script. Empty scripts delete a binding; a leading ``+`` appends. Its event-mask
validation allows key, button, motion, crossing and virtual sequences. A newly
created binding with an unsupported mask is deleted, not rolled back to a prior
script. The native handler's switch has no direct VirtualEvent case; accepting
a virtual sequence must not be confused with explicit dispatch of every
synthetically generated virtual event by this module.

Lifetime and deletion
---------------------

The table borrows its owner, window, objects and context values. Creation owns
the Tk binding table and registers an event handler; destruction unregisters
that handler and frees the table immediately. No generic reference count keeps
individual components alive.

``BindProc`` preserves ``bindPtr->clientData``, which graph and legend tables set
to the graph record. Graph final disposal is deferred until that preserve is
released. This keeps graph-owned binding tables alive through ordinary widget
destruction from a binding script. It does not authorize directly destroying
the binding table during active dispatch, nor does it preserve a component
that a script deletes.

Before freeing an object, its owning component calls
:c:func:`Rbc_DeleteBindings` to clear matching current, pending and focus targets.
The operation deletes bindings keyed by that exact pointer, but does not free
objects, evaluate scripts, or immediately repick. The owner manages any separate
tag-key cleanup and determines when geometry is ready for another pick.

:c:func:`Rbc_PickCurrentItem` is synchronous and adds no preserve scope of its
own. Its caller must retain the owner/table and check state after callbacks.
``Rbc_NotifyTrackView`` does so around stationary-pointer repicking, checks the
window again before scrollbar notification, and skips this work during export.
This prevents temporary export geometry from changing the screen's current track.

:c:func:`Rbc_MoveBindingTable` moves only the handler registration and window
pointer. It retains bindings and saved current/focus/pick state; it neither
translates saved coordinates nor repicks. Legend/window management must account
for that when moving between embedded and external presentation.

Validation when changing behavior
---------------------------------

Existing regression coverage includes ``RBC.trackgraph.bind.A.test`` and
``RBC.trackgraph.legend.A.test``, plus graph/barchart/stripchart axis, element,
marker and legend binding files. Exercise overlapping objects, tag isolation,
button grabs, deleted Enter destinations, widget destruction from scripts, and
stationary-pointer reorder/scroll. Include local legend padding and external
legend windows when changing hit priority or event-handler placement.

This documentation update changes no dispatch or hit-test behavior. Future
changes need behavioral tests in those existing component test files.
