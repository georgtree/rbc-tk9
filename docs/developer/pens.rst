Pens and style palettes
=======================

Named pens are graph-owned reusable drawing resources. ``rbcGrPen.c`` manages their registry, commands, reference counts
and option lifecycle. Concrete line and bar implementations provide their drawing attributes and derived resources.
Strip and polar elements use the line-pen implementation. Shared ``Pen`` state and declarations live in ``rbcGraph.h``;
palette services are in ``rbcGrElem.c`` and ``rbcGrElem.h``. See :doc:`pen-reference` for selected contracts.

Named and embedded pens
-----------------------

A named pen belongs to the graph's pen table. Successful :c:func:`Rbc_GetPen` lookup checks its type and increments its
reference count. Each acquisition must be paired with :c:func:`Rbc_FreePen`. Lookup maps strip and polar requirements to
the line class; a bar pen is not interchangeable with a line pen.

Elements also have a built-in pen and may select named normal or active pens. Do not apply named-pen reference
operations indiscriminately to an embedded pen. Likewise, an alias to a pen pointer does not itself acquire another
reference. Element pen transactions stage references before committing them; failed preparation must release the staged
acquisitions.

Deletion is deferred while a named pen is referenced. The delete command sets ``PEN_DELETE_PENDING`` and destroys the
record immediately only when its count is zero. Existing users can keep using the retained record, while ordinary name
lookup no longer returns it. Releasing the last reference destroys a pending pen; releasing the last reference to a live
named pen leaves the registry's pen available for later use.

Creation with the name of an in-use delete-pending pen can revive that record when the requested concrete type
matches. It is not necessarily a new C entity. Changing its type is rejected while that old record remains. Code must
not assume that command-level deletion immediately invalidates every pen pointer, or that recreating the name
necessarily creates independent drawing state.

Configuration and redraw
------------------------

Pen initialization uses the named ``Pen`` option-database component and the shared resource-name convention. Explicit
configuration uses ``Tk_SetOptions`` followed by the concrete pen configurator. If that configurator fails, saved Tk
options are restored. Derived resources must be prepared transactionally before replacing the active resources; see
:doc:`configuration`.

The pen configure command requests backing-store and margin redraw when a successfully configured pen has references. A
command naming multiple pens configures them in sequence; a later failure does not roll back earlier pens. The per-pen
option transaction must not be described as a transaction over the entire multi-pen command.

Palette ownership
-----------------

An element palette is a chain of style entries. The first entry represents the normal style and borrows the element's
normal pen. Additional entries acquire named-pen references and carry weight ranges. The palette's first entry must
remain present for style selection, even with no additional styles.

:c:func:`Rbc_FreePalette` releases the counted references and removes entries after the first. Despite its name, it does
not destroy the chain or release that first borrowed pen. The private :c:func:`DestroyPalette` helper performs the full
chain destruction after releasing those entries. Confusing these two operations can either leak a chain or release a
reference the palette never owned.

Palette parsing builds a candidate chain before replacing the live one. An entry that fails to parse is freed before it
has been attached; earlier candidate entries and their references are then released. Styles transactions apply the same
prepare/commit/cleanup ordering as other element resources. See :doc:`elements` for the shared transaction model.

Selecting a style
-----------------

:c:func:`Rbc_StyleMap` allocates one pointer per effective data point and initializes all entries to the normal
style. It considers weights only up to the smaller of the weight length and the point count. Missing weights and
unmatched values therefore leave the normal style selected.

For each weighted point, the palette is searched from last to first. The first matching range wins, so later entries
take precedence where ranges overlap. Membership uses a normalized range comparison with ``DBL_EPSILON`` tolerance near
endpoints, rather than a strict half-open interval. Nonpositive range widths are skipped. Preserve this ordering and
tolerance when changing palette selection; adjacent boundaries and overlapping styles depend on them.

The returned array owns only the pointer storage. Its entries borrow palette styles and acquire no new pen
references. Callers free the array with ``ckfree`` and must not retain it after the underlying palette entries are
replaced. Even the zero-point case returns an allocation so callers can release it unconditionally. Size overflow
follows the allocator's fatal-error convention.

Resource teardown
-----------------

:c:func:`Rbc_ReleasePenTkResources` releases derived resources through the concrete pen destructor before freeing
Tk-managed colors, fonts, borders or bitmaps. It retains pen records and the registry, and guards against repeated
resource release. This pass needs a live Tk window and covers registered named pens; embedded pens belong to element
cleanup.

:c:func:`Rbc_DestroyPens` finally destroys the remaining records and hash table. It does not wait for reference counts,
so element references must already have been released. See :doc:`graph-lifecycle` for graph teardown ordering.

Useful regression cases include several elements sharing one pen, deletion while referenced, compatible revival,
rejected type changes, failed style replacement, overlapping ranges, exact range boundaries and empty data. These
exercise ownership and selection behavior that a simple rendered sample cannot fully demonstrate.

Creation and command boundaries
-------------------------------

:c:func:`rbcGrPen.Rbc_CreatePen` returns a graph-owned pointer without acquiring a counted reference. It scans explicit
option pairs for ``-type`` before selecting the concrete constructor; the accepted preliminary prefixes have three
through five bytes, and the last recognized occurrence determines construction. Explicit ``bar``, ``line`` and
``strip`` types are accepted by this scan; strip uses line storage. A supplied polar or strip default class is likewise
normalized to line. Subsequent Tk configuration still validates the options.

A new record is registered before initialization. :c:func:`rbcGrPen.InitPenOptions` creates its option table and
initializes the named Pen component, freeing partially initialized Tk resources if initialization fails. Explicit
options pass through :c:func:`rbcGrPen.ConfigurePenOptions`; with no explicit options the concrete configurator still
runs to build derived resources from defaults. Failure destroys a newly created record and removes its registry entry.

A compatible delete-pending record remains hidden during revival configuration. Only successful configuration clears
its pending flag. Failure keeps the old record pending and its existing users retain their references and drawing
state. A later valid create can retry revival; releasing the last reference instead destroys the pending record
normally. This visibility commit is separate from the Tk option transaction, which restores the record's option values.

.. list-table::
   :header-rows: 1
   :widths: 24 40 36

   * - Operation
     - Lookup/result
     - Mutation boundary
   * - cget / type
     - Borrowed lookup, value or normalized class result
     - No reference acquisition or mutation
   * - configure query
     - First named pen only
     - Zero options returns all metadata; one option returns its metadata
   * - configure update
     - Names checked before applying option pairs
     - Per-pen commit; later failure retains earlier changes
   * - create
     - Original name object returned on success
     - New allocation or compatible retained-record revival
   * - delete
     - Names processed in argument order
     - Earlier deletions survive later failure
   * - names
     - Visible names matching any supplied glob
     - Hash traversal order; each name appears at most once

Configure identifies the start of options by the first argument beginning with a dash. Querying multiple names does not
assemble multiple results: the first query returns immediately. Deleting the same name twice fails at the second lookup,
since pending names are hidden. Neither lookup nor listing acquires a reference that could keep a pen alive after
command processing.

The dispatcher checks subcommand and argument-count bounds. It does not add an independent graph/pen preservation
boundary around concrete configuration. Callers and concrete configurators remain responsible for callback lifetimes;
ordinary option rollback must not be used as evidence of callback safety.

Candidate palettes and default ranges
-------------------------------------

:c:func:`ParseStylesObj` clears its output pointer before doing any work. It always constructs the reserved normal-style
entry, including for NULL or empty style lists, and borrows the element's current normal pen there. Additional entries
use the concrete record size supplied by the caller, which must accommodate :c:type:`PenStyle`. The first additional
entry defaults to range 0 through 1, the second to 1 through 2, and so on.

:c:func:`GetPenStyleFromObj` accepts exactly one or three list elements. The three-item form converts both endpoints
before acquiring a named pen, so invalid numeric input cannot leak that reference. It uses Tcl numeric conversion rather
than expression evaluation. A one-item specification retains the initialized default range; three items replace it using
``SetWeight``. Endpoint ordering is not rejected: ``SetRange`` uses DBL_EPSILON when max is not greater than min. This
compatibility behavior differs from rejecting reversed ranges or treating them as empty. Keep it distinct from the style
mapper's check for a stored nonpositive range.

Palette pen compatibility is checked against the element's class. The ``penType`` argument retained by
:c:func:`StageElemStyles` is currently unused; it does not override that class check. Stage helpers acquire/parse a
successor before releasing the earlier candidate. This matters for repeated options and for delete-pending pens whose
last reference could otherwise disappear.

:c:func:`StageElemPen` similarly records NULL as an explicitly staged choice: normal NULL selects the built-in pen at
commit, whereas active NULL disables the active pen. An unsuccessful lookup retains the previous candidate. The outer
transaction decides whether to continue, commit, or release all staged values.

An unattached palette entry that fails parsing is freed separately. Entries already attached are cleaned through
:c:func:`DestroyPalette`, which releases counted style pens and destroys the chain without releasing the borrowed first
pen. Neither staging nor cleanup implicitly replaces the live element palette.
