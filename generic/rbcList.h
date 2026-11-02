/**
 * @file rbcList.h
 * @brief Private linked-container ownership and traversal.
 *
 *      Keyed list records and caller-owned payloads.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */
#ifndef _RBCLIST
#define _RBCLIST

#include <tcl.h>

typedef struct Rbc_ListStruct *Rbc_List;
typedef struct Rbc_ListNodeStruct *Rbc_ListNode;

/*
 * A Rbc_ListNode is the container structure for the Rbc_List.
 */
struct Rbc_ListNodeStruct {
    struct Rbc_ListNodeStruct *prevPtr; /* Link to the previous node */
    struct Rbc_ListNodeStruct *nextPtr; /* Link to the next node */
    ClientData clientData;              /* Pointer to the data object */
    struct Rbc_ListStruct *listPtr;     /* List to eventually insert node */
    union {                             /* Key has one of these forms: */
        const char *oneWordValue;       /* One-word value for key. */
        int words[1];                   /* Multiple integer words for key.
                                         * The actual size will be as large
                                         * as necessary for this table's
                                         * keys. */
        char string[4];                 /* String for key.  The actual size
                                         * will be as large as needed to hold
                                         * the key. */
    } key;                              /* MUST BE LAST FIELD IN RECORD!! */
};

typedef int(Rbc_ListCompareProc)(Rbc_ListNode *node1Ptr, Rbc_ListNode *node2Ptr);

/*
 * A Rbc_List is a doubly chained list structure.
 */
struct Rbc_ListStruct {
    struct Rbc_ListNodeStruct *headPtr; /* Pointer to first element in list */
    struct Rbc_ListNodeStruct *tailPtr; /* Pointer to last element in list */
    Tcl_Size nNodes;                    /* Number of node currently in the list. */
    int type;                           /* Type of keys in list. */
};

/**
 * @brief Initialize caller-owned list storage with a key representation.
 *
 * Requires a non-NULL record. Stores type and clears endpoints/count; does not
 * free prior nodes or validate type. Use TCL_STRING_KEYS, TCL_ONE_WORD_KEYS or an
 * integer-array word count greater than TCL_ONE_WORD_KEYS. Reset a populated
 * list before reinitializing it.
 */
void Rbc_ListInit(Rbc_List list, int type);
/**
 * @brief Free linked nodes and copied keys while retaining the list key type.
 * Accepts NULL. Does not free clientData or objects denoted by one-word keys.
 * Nodes allocated but never inserted are not visited.
 */
void Rbc_ListReset(Rbc_List list);
/**
 * @brief Allocate and initialize an empty keyed list.
 *
 * Returns caller-owned Tcl-allocated storage, eventually released through
 * Rbc_ListDestroy. Type has the same preconditions as Rbc_ListInit and is not
 * validated here. Allocation exhaustion is not an interpreter error.
 */
Rbc_List Rbc_ListCreate(int type);
/**
 * @brief Reset and free a heap-allocated keyed list.
 *
 * Accepts NULL. Releases linked nodes and copied keys, then the list itself.
 * Does not free external payloads or one-word key targets. Do not pass stack or
 * embedded storage; use Rbc_ListReset for those records.
 */
void Rbc_ListDestroy(Rbc_List list);
/**
 * @brief Allocate a keyed node for a list without inserting it.
 * String and integer-array keys are copied into the allocation; one-word keys
 * retain only the pointer value. Duplicate keys are permitted. The node already
 * records the intended list: insert it before using ListUnlinkNode/DeleteNode,
 * or free this never-inserted allocation directly with ckfree.
 */
Rbc_ListNode Rbc_ListCreateNode(Rbc_List list, const char *key);
/**
 * @brief Unlink and free an inserted or previously unlinked node.
 * Copied key storage is freed with the record; external clientData and one-word
 * key targets are not freed. Never pass a newly created, never-inserted node.
 */
void Rbc_ListDeleteNode(Rbc_ListNode node);
/**
 * @brief Create a keyed node and insert it at the tail.
 *
 * Requires a valid initialized list and a key matching its representation.
 * Copies string/array keys and stores one-word keys by value. Duplicate keys are
 * allowed. Returns the list-owned node; clientData is borrowed and unchanged.
 * Allocation, invalid key type and count overflow use fatal allocation/panic paths.
 */
Rbc_ListNode Rbc_ListAppend(Rbc_List list, const char *key, ClientData clientData);
/**
 * @brief Create a keyed node and insert it at the head.
 *
 * Uses the same key-copy and payload-ownership rules as Rbc_ListAppend.
 * Duplicate keys are allowed and the new node becomes the first matching node
 * for lookup. Requires a valid list/key; returns a list-owned record.
 */
Rbc_ListNode Rbc_ListPrepend(Rbc_List list, const char *key, ClientData clientData);
/**
 * @brief Insert a node after a neighbor, or prepend for NULL.
 *
 * Accepts a newly created node or a normally unlinked node with NULL neighbors.
 * Its key representation must match the destination list. Any non-NULL neighbor
 * must belong to that list; an already inserted node must first be unlinked.
 * Sets the node owner, neighbors, endpoints and count without allocation or
 * callbacks. Count overflow panics; no membership or key-type check is performed.
 */
void Rbc_ListLinkAfter(Rbc_List list, Rbc_ListNode node, Rbc_ListNode afterNode);
/**
 * @brief Insert a node before a neighbor, or append for NULL.
 *
 * Requires the same detached/construction state and compatible key representation
 * as Rbc_ListLinkAfter. Does not unlink prior membership or validate the neighbor.
 * Sets owner and links, increments count, and panics on count overflow. Node
 * payload and copied-key storage remain unchanged.
 */
void Rbc_ListLinkBefore(Rbc_List list, Rbc_ListNode node, Rbc_ListNode beforeNode);
/**
 * @brief Detach an inserted node without freeing it or its payload.
 * Clears owner, next and previous pointers. A previously unlinked node is a
 * no-op, but a newly created, never-inserted node is not valid input: its owner
 * field is already set although the list count does not include it.
 */
void Rbc_ListUnlinkNode(Rbc_ListNode node);
/**
 * @brief Return the borrowed first node matching a key, or NULL.
 * Searches linearly in list order; duplicate keys are not rejected. A NULL
 * list returns NULL. The node remains owned by its container/caller.
 */
Rbc_ListNode Rbc_ListGetNode(Rbc_List list, const char *key);
/**
 * @brief Delete only the first matching node in current list order.
 *
 * A NULL list or absent key is a no-op. Otherwise unlinks and frees the node
 * and copied key, leaving clientData and one-word key targets owned by the caller.
 * Uses the historical Rb_ spelling. Does not call a payload destructor.
 */
void Rb_ListDeleteNodeByKey(Rbc_List list, const char *key);
/**
 * @brief Return a borrowed node at a zero-based position.
 *
 * Traverses from head when direction is positive, from tail otherwise.
 * Returns NULL for a NULL list, negative position or position at/above length.
 * Linear traversal does not mutate the list or retain the returned record.
 */
Rbc_ListNode Rbc_ListGetNthNode(Rbc_List list, Tcl_Size position, int direction);
/**
 * @brief Reorder existing nodes with qsort without moving their allocations.
 * The comparator receives pointers to node pointers. It must not mutate the
 * list, delete nodes or reenter code that does so. Equal-key ordering is not
 * stable. NULL/short lists are unchanged; scratch-allocation failure silently
 * leaves the list unchanged. Payload ownership is unaffected.
 */
void Rbc_ListSort(Rbc_List list, Rbc_ListCompareProc *proc);

#define Rbc_ListGetLength(list) (((list) == NULL) ? (Tcl_Size)0 : ((struct Rbc_ListStruct *)list)->nNodes)
#define Rbc_ListFirstNode(list) (((list) == NULL) ? NULL : ((struct Rbc_ListStruct *)list)->headPtr)
#define Rbc_ListLastNode(list) (((list) == NULL) ? NULL : ((struct Rbc_ListStruct *)list)->tailPtr)
#define Rbc_ListPrevNode(node) ((node)->prevPtr)
#define Rbc_ListNextNode(node) ((node)->nextPtr)
#define Rbc_ListGetKey(node)                                                                                           \
    (((node)->listPtr->type == TCL_STRING_KEYS) ? (node)->key.string : (node)->key.oneWordValue)
#define Rbc_ListGetValue(node) ((node)->clientData)
#define Rbc_ListSetValue(node, value) ((node)->clientData = (ClientData)(value))
#define Rbc_ListAppendNode(list, node) (Rbc_ListLinkBefore((list), (node), (Rbc_ListNode)NULL))
#define Rbc_ListPrependNode(list, node) (Rbc_ListLinkAfter((list), (node), (Rbc_ListNode)NULL))

#endif /* _RBCLIST */
