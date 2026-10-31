Pens and style palettes
=======================

Named pens are graph-owned reusable drawing resources. ``rbcGrPen.c`` manages
their registry, commands, reference counts and option lifecycle. Concrete line
and bar implementations provide their drawing attributes and derived resources.
Strip and polar elements use the line-pen implementation. Shared ``Pen`` state
and declarations live in ``rbcGraph.h``; palette services are in ``rbcGrElem.c``
and ``rbcGrElem.h``. See :doc:`pen-reference` for selected contracts.

Named and embedded pens
-----------------------

A named pen belongs to the graph's pen table. Successful ``Rbc_GetPen`` lookup
checks its type and increments its reference count. Each acquisition must be
paired with ``Rbc_FreePen``. Lookup maps strip and polar requirements to the
line class; a bar pen is not interchangeable with a line pen.

Elements also have a built-in pen and may select named normal or active pens.
Do not apply named-pen reference operations indiscriminately to an embedded
pen. Likewise, an alias to a pen pointer does not itself acquire another
reference. Element pen transactions stage references before committing them;
failed preparation must release the staged acquisitions.

Deletion is deferred while a named pen is referenced. The delete command sets
``PEN_DELETE_PENDING`` and destroys the record immediately only when its count
is zero. Existing users can keep using the retained record, while ordinary
name lookup no longer returns it. Releasing the last reference destroys a
pending pen; releasing the last reference to a live named pen leaves the
registry's pen available for later use.

Creation with the name of an in-use delete-pending pen can revive that record
when the requested concrete type matches. It is not necessarily a new C entity.
Changing its type is rejected while that old record remains. Code must not
assume that command-level deletion immediately invalidates every pen pointer,
or that recreating the name necessarily creates independent drawing state.

Configuration and redraw
------------------------

Pen initialization uses the named ``Pen`` option-database component and the
shared resource-name convention. Explicit configuration uses ``Tk_SetOptions``
followed by the concrete pen configurator. If that configurator fails, saved
Tk options are restored. Derived resources must be prepared transactionally
before replacing the active resources; see :doc:`configuration`.

The pen configure command requests backing-store and margin redraw when a
successfully configured pen has references. A command naming multiple pens
configures them in sequence; a later failure does not roll back earlier pens.
The per-pen option transaction must not be described as a transaction over the
entire multi-pen command.

Palette ownership
-----------------

An element palette is a chain of style entries. The first entry represents the
normal style and borrows the element's normal pen. Additional entries acquire
named-pen references and carry weight ranges. The palette's first entry must
remain present for style selection, even with no additional styles.

``Rbc_FreePalette`` releases the counted references and removes entries after
the first. Despite its name, it does not destroy the chain or release that
first borrowed pen. The private ``DestroyPalette`` helper performs the full
chain destruction after releasing those entries. Confusing these two operations
can either leak a chain or release a reference the palette never owned.

Palette parsing builds a candidate chain before replacing the live one.
An entry that fails to parse is freed before it has been attached; earlier
candidate entries and their references are then released. Styles transactions
apply the same prepare/commit/cleanup ordering as other element resources.
See :doc:`elements` for the shared transaction model.

Selecting a style
-----------------

``Rbc_StyleMap`` allocates one pointer per effective data point and initializes
all entries to the normal style. It considers weights only up to the smaller
of the weight length and the point count. Missing weights and unmatched values
therefore leave the normal style selected.

For each weighted point, the palette is searched from last to first. The first
matching range wins, so later entries take precedence where ranges overlap.
Membership uses a normalized range comparison with ``DBL_EPSILON`` tolerance
near endpoints, rather than a strict half-open interval. Nonpositive range
widths are skipped. Preserve this ordering and tolerance when changing palette
selection; adjacent boundaries and overlapping styles depend on them.

The returned array owns only the pointer storage. Its entries borrow palette
styles and acquire no new pen references. Callers free the array with ``ckfree``
and must not retain it after the underlying palette entries are replaced.
Even the zero-point case returns an allocation so callers can release it
unconditionally. Size overflow follows the allocator's fatal-error convention.

Resource teardown
-----------------

``Rbc_ReleasePenTkResources`` releases derived resources through the concrete
pen destructor before freeing Tk-managed colors, fonts, borders or bitmaps.
It retains pen records and the registry, and guards against repeated resource
release. This pass needs a live Tk window and covers registered named pens;
embedded pens belong to element cleanup.

``Rbc_DestroyPens`` finally destroys the remaining records and hash table.
It does not wait for reference counts, so element references must already have
been released. See :doc:`graph-lifecycle` for graph teardown ordering.

Useful regression cases include several elements sharing one pen, deletion
while referenced, compatible revival, rejected type changes, failed style
replacement, overlapping ranges, exact range boundaries and empty data. These
exercise ownership and selection behavior that a simple rendered sample cannot
fully demonstrate.
