Chains and keyed lists
======================

``rbcChain.c`` and ``rbcList.c`` provide private doubly linked containers.
They manage link/node allocations, not the application objects referenced by
``clientData``. They do not provide reference counting, locking, mutation-safe
iterators or callback lifetime protection. Their users supply those policies.

Choosing and owning a container
-------------------------------

A chain has unkeyed links and is useful for display order, client membership
and palettes. A list adds stored keys and linear lookup; it is not a hash table
and does not enforce key uniqueness. Both retain order and use ``Tcl_Size``
counts. Insertion and unlinking with known neighbors take constant time; lookup
and indexed traversal walk the list.

Create allocates the container; Init initializes caller-provided storage.
Init is not cleanup: applying it to a populated container loses its links.
Reset frees linked records but retains an empty container. Destroy additionally
frees the container allocation, so it must not be used on embedded or stack
storage. Reset/Destroy do not invoke destructors for external payloads.

``Rbc_ChainAllocLink`` is a special case: it allocates aligned trailing storage
in the link allocation and points ``clientData`` there. Freeing that link also
frees those inline bytes. Do not free that pointer separately. If the inline
object owns other resources, release them before deleting/resetting the link.
A normal link's external payload remains independently owned.

Keys and node creation
----------------------

Lists use ``TCL_STRING_KEYS``, ``TCL_ONE_WORD_KEYS`` or an integer-array key
length. String and array contents are copied into the variable-sized node;
one-word keys store a pointer-sized value and compare identity, not pointed-to
contents. Reset/delete releases copied keys with the node, but does not free
one-word key targets. Repeated keys are allowed and lookup returns the first
match in current order.

``Rbc_ListCreateNode`` allocates but does not insert a node. It nevertheless
sets ``listPtr`` to the intended list so its key representation is known.
This is a construction state, not ordinary detached state: calling unlink or
delete before first insertion would incorrectly change the list count. Insert
it, or dispose of the never-inserted allocation directly with ``ckfree``.
The Append/Prepend convenience functions perform creation and insertion together.

After normal unlinking, ``listPtr`` is NULL. The node can then be freed with
``Rbc_ListDeleteNode`` or relinked into a list with the same key representation.
The ``Rbc_ListGetKey`` macro dereferences ``listPtr`` and therefore must not be
used on an unlinked node. It is a convenience for string/one-word keys; code
handling array keys must use the stored words with the known key type.

Linking and moving
------------------

LinkBefore/LinkAfter require a detached record and a neighbor belonging to the
destination container. They do not unlink existing membership automatically.
A NULL neighbor has deliberately asymmetric meaning: Before(NULL) appends,
while After(NULL) prepends. The AppendLink/PrependLink and AppendNode/PrependNode
macros express those conventions directly.

Chain links do not store owner identity. ``Rbc_ChainUnlinkLink`` needs the actual
owning chain; passing another chain can corrupt both structures. Unlink clears
neighbor pointers without freeing the link. Delete additionally releases it.
List nodes store their owner after insertion, so unlink needs only the node.
A node must never be linked into two containers at once.

Borrowed pointers and traversal
-------------------------------

Access macros return borrowed links, nodes and payloads. Next/previous/value
macros require a valid record. Length and first/last macros tolerate a NULL
container, but this is not a general NULL-tolerance rule for all operations.
The macros may evaluate arguments more than once; avoid arguments with side
effects.

When deleting only the current chain link, save its successor first:

.. code-block:: c

   for (link = Rbc_ChainFirstLink(chain); link != NULL; link = next) {
       next = Rbc_ChainNextLink(link);
       /* Release any owned payload resources here. */
       Rbc_ChainDeleteLink(chain, link);
   }

This pattern is insufficient if a callback can also delete the saved successor
or destroy the container. The owner must snapshot and preserve appropriate
objects, defer reclamation, or explicitly prohibit such mutation. Preserving a
payload does not preserve its chain link. See :doc:`vectors` and
:doc:`images-tiles` for notification systems that implement protection above
these containers.

Sorting and positional lookup
-----------------------------

``Rbc_ListGetNthNode`` uses a zero-based position, traversing from the head for
positive direction and from the tail otherwise. Invalid positions or a NULL
list return NULL. It does not cache an index.

``Rbc_ListSort`` builds an array of node pointers, calls ``qsort`` and relinks
the same allocations. Saved node addresses remain valid, but neighbor/order
assumptions do not. Its comparator receives pointers to node pointers and must
not mutate membership or invoke reentrant code that does so. Sorting is not
stable for equal comparisons. Failure of the attempted scratch allocation leaves
the list unchanged without an error result; this void helper is not a promise
that sorting succeeded under allocation pressure.

See :doc:`container-reference` for selected lifecycle contracts. The historical
``Rb_ListDeleteNodeByKey`` spelling remains unchanged; it removes only the first
matching node. These documentation changes do not rename private functions or
alter container behavior.

Insertion and lookup contracts
-------------------------------

The C reference includes all creation, insertion and positional-lookup entry
points as well as the private key-search helpers. Allocation uses Tcl's fatal
allocation policy for container and node creation; these functions do not
return an interpreter diagnostic under memory pressure. The attempted scratch
allocation in sorting is a separate, nonfatal policy.

A detached record supplied to an insertion helper must have NULL neighbor
pointers. In particular, insertion into an empty container assigns its head and
tail without clearing arbitrary old link fields. Nodes created by the allocation
helpers and records detached by normal unlinking meet this precondition.
Hand-built records must meet it too. Insertion is not a membership check or a
move operation: callers must unlink existing membership first.

The list's key type determines lookup semantics:

.. list-table::
   :header-rows: 1
   :widths: 25 35 40

   * - Representation
     - Comparison
     - Caller requirement
   * - String
     - First byte, then ``strcmp``
     - Non-NULL NUL-terminated query string
   * - One word
     - Pointer-sized identity
     - Query value need not point to readable storage; NULL is allowed
   * - Integer array
     - ``memcmp`` over ``type * sizeof(int)`` bytes
     - Query has that many readable native ``int`` words

All searches walk from the head and return the first match. Sorting or moving
nodes can therefore change which duplicate is found without changing any key.
Array keys are native-memory representations, not a portable byte encoding.
Lookup returns a borrowed node; it adds no protection against deletion by a
later callback. ``Rb_ListDeleteNodeByKey`` uses this same first-match rule.
