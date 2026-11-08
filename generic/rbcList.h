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

/**
 * @brief Inline keyed-list storage selected by Rbc_ListStruct::type.
 *
 * String and integer-array keys extend the node allocation beyond these
 * placeholder arrays. One-word keys store only a borrowed pointer value.
 * Keep the containing key member last in Rbc_ListNodeStruct.
 */
union Rbc_ListKey {
    const char *oneWordValue; /**< Borrowed one-word key; only the pointer value is stored. */
    int words[1]; /**< Inline integer-array key; allocation extends to the list's key word count. */
    char string[4]; /**< Inline NUL-terminated string key; allocation extends to fit the copied key. */
};

/** @brief List link with inline or pointer key storage selected by the list key type. */
struct Rbc_ListNodeStruct {
    struct Rbc_ListNodeStruct *prevPtr; /**< Link to the previous node */
    struct Rbc_ListNodeStruct *nextPtr; /**< Link to the next node */
    ClientData clientData; /**< Pointer to the data object */
    struct Rbc_ListStruct *listPtr; /**< List to eventually insert node */
    union Rbc_ListKey key; /**< Inline key storage; must remain last because allocation extends it for long keys. */
};

typedef int(Rbc_ListCompareProc)(Rbc_ListNode *node1Ptr, Rbc_ListNode *node2Ptr);

/** @brief Owned list container; node payload ownership remains with the caller. */
struct Rbc_ListStruct {
    struct Rbc_ListNodeStruct *headPtr; /**< Pointer to first element in list */
    struct Rbc_ListNodeStruct *tailPtr; /**< Pointer to last element in list */
    Tcl_Size nNodes; /**< Number of node currently in the list. */
    int type; /**< Type of keys in list. */
};

/**
 * @brief Initialize caller-owned list storage with a key representation.
 *
 * Requires a non-NULL record. Stores type and clears endpoints/count; does not
 * free prior nodes or validate type. Use TCL_STRING_KEYS, TCL_ONE_WORD_KEYS or an
 * integer-array word count greater than TCL_ONE_WORD_KEYS. Reset a populated
 * list before reinitializing it.
 *
 * @param[in,out] list Required caller-owned record; initialization does not release existing nodes.
 * @param[in] type TCL_STRING_KEYS, TCL_ONE_WORD_KEYS, or an int-array word count greater than
 *   TCL_ONE_WORD_KEYS; not validated here.
 */
void Rbc_ListInit(Rbc_List list, int type);
/**
 * @brief Free linked nodes and copied keys while retaining the list key type.
 * Accepts NULL. Does not free clientData or objects denoted by one-word keys.
 * Nodes allocated but never inserted are not visited.
 *
 * @param[in,out] list Initialized list to clear, or NULL; retains the container and key type but frees linked
 *   nodes.
 */
void Rbc_ListReset(Rbc_List list);
/**
 * @brief Allocate and initialize an empty keyed list.
 *
 * Returns caller-owned Tcl-allocated storage, eventually released through
 * Rbc_ListDestroy. Type has the same preconditions as Rbc_ListInit and is not
 * validated here. Allocation exhaustion is not an interpreter error.
 *
 * @param[in] type TCL_STRING_KEYS, TCL_ONE_WORD_KEYS, or an int-array word count greater than
 *   TCL_ONE_WORD_KEYS; not validated here.
 */
Rbc_List Rbc_ListCreate(int type);
/**
 * @brief Reset and free a heap-allocated keyed list.
 *
 * Accepts NULL. Releases linked nodes and copied keys, then the list itself.
 * Does not free external payloads or one-word key targets. Do not pass stack or
 * embedded storage; use Rbc_ListReset for those records.
 *
 * @param[in,out] list Heap-allocated list to consume, or NULL; linked nodes and container storage become
 *   invalid.
 */
void Rbc_ListDestroy(Rbc_List list);
/**
 * @brief Allocate a keyed node for a list without inserting it.
 * String and integer-array keys are copied into the allocation; one-word keys
 * retain only the pointer value. Duplicate keys are permitted. The node already
 * records the intended list: insert it before using ListUnlinkNode/DeleteNode,
 * or free this never-inserted allocation directly with ckfree.
 *
 * @param[in] list Required initialized list supplying key type and initial owner identity; no node is inserted.
 * @param[in] key Key matching the list type: readable NUL-terminated string, pointer-sized identity, or
 *   readable native int array; only identity keys may be NULL.
 */
Rbc_ListNode Rbc_ListCreateNode(Rbc_List list, const char *key);
/**
 * @brief Unlink and free an inserted or previously unlinked node.
 * Copied key storage is freed with the record; external clientData and one-word
 * key targets are not freed. Never pass a newly created, never-inserted node.
 *
 * @param[in,out] node Required inserted or normally unlinked node to consume; never pass a newly created,
 *   never-inserted node.
 */
void Rbc_ListDeleteNode(Rbc_ListNode node);
/**
 * @brief Create a keyed node and insert it at the tail.
 *
 * Requires a valid initialized list and a key matching its representation.
 * Copies string/array keys and stores one-word keys by value. Duplicate keys are
 * allowed. Returns the list-owned node; clientData is borrowed and unchanged.
 * Allocation, invalid key type and count overflow use fatal allocation/panic paths.
 *
 * @param[in,out] list Required initialized destination list; owns the new node on return.
 * @param[in] key Key matching the list type: readable NUL-terminated string, pointer-sized identity, or
 *   readable native int array; only identity keys may be NULL.
 * @param[in] clientData Borrowed payload pointer, possibly NULL; no retention or destructor is installed.
 */
Rbc_ListNode Rbc_ListAppend(Rbc_List list, const char *key, ClientData clientData);
/**
 * @brief Create a keyed node and insert it at the head.
 *
 * Uses the same key-copy and payload-ownership rules as Rbc_ListAppend.
 * Duplicate keys are allowed and the new node becomes the first matching node
 * for lookup. Requires a valid list/key; returns a list-owned record.
 *
 * @param[in,out] list Required initialized destination list; owns the new node on return.
 * @param[in] key Key matching the list type: readable NUL-terminated string, pointer-sized identity, or
 *   readable native int array; only identity keys may be NULL.
 * @param[in] clientData Borrowed payload pointer, possibly NULL; no retention or destructor is installed.
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
 *
 * @param[in,out] list Required initialized destination with a key type matching the node allocation.
 * @param[in,out] node Newly created or normally unlinked node with NULL neighbors; destination takes ownership.
 * @param[in] afterNode Borrowed neighbor in the destination list, or NULL to prepend.
 */
void Rbc_ListLinkAfter(Rbc_List list, Rbc_ListNode node, Rbc_ListNode afterNode);
/**
 * @brief Insert a node before a neighbor, or append for NULL.
 *
 * Requires the same detached/construction state and compatible key representation
 * as Rbc_ListLinkAfter. Does not unlink prior membership or validate the neighbor.
 * Sets owner and links, increments count, and panics on count overflow. Node
 * payload and copied-key storage remain unchanged.
 *
 * @param[in,out] list Required initialized destination with a key type matching the node allocation.
 * @param[in,out] node Newly created or normally unlinked node with NULL neighbors; destination takes ownership.
 * @param[in] beforeNode Borrowed neighbor in the destination list, or NULL to append.
 */
void Rbc_ListLinkBefore(Rbc_List list, Rbc_ListNode node, Rbc_ListNode beforeNode);
/**
 * @brief Detach an inserted node without freeing it or its payload.
 * Clears owner, next and previous pointers. A previously unlinked node is a
 * no-op, but a newly created, never-inserted node is not valid input: its owner
 * field is already set although the list count does not include it.
 *
 * @param[in,out] node Required inserted or normally unlinked node; clears membership without freeing it.
 *   Never-inserted nodes are invalid.
 */
void Rbc_ListUnlinkNode(Rbc_ListNode node);
/**
 * @brief Return the borrowed first node matching a key, or NULL.
 * Searches linearly in list order; duplicate keys are not rejected. A NULL
 * list returns NULL. The node remains owned by its container/caller.
 *
 * @param[in] list Initialized list, or NULL for no match; membership is unchanged.
 * @param[in] key Key matching the list type: readable NUL-terminated string, pointer-sized identity, or
 *   readable native int array; only identity keys may be NULL.
 */
Rbc_ListNode Rbc_ListGetNode(Rbc_List list, const char *key);
/**
 * @brief Delete only the first matching node in current list order.
 *
 * A NULL list or absent key is a no-op. Otherwise unlinks and frees the node
 * and copied key, leaving clientData and one-word key targets owned by the caller.
 * Uses the historical Rb_ spelling. Does not call a payload destructor.
 *
 * @param[in,out] list Initialized list, or NULL for no match; the first matching node is removed and freed.
 * @param[in] key Key matching the list type: readable NUL-terminated string, pointer-sized identity, or
 *   readable native int array; only identity keys may be NULL.
 */
void Rbc_ListDeleteNodeByKey(Rbc_List list, const char *key);
/**
 * @brief Return a borrowed node at a zero-based position.
 *
 * Traverses from head when direction is positive, from tail otherwise.
 * Returns NULL for a NULL list, negative position or position at/above length.
 * Linear traversal does not mutate the list or retain the returned record.
 *
 * @param[in] list Borrowed initialized list, or NULL to return NULL.
 * @param[in] position Zero-based traversal position; negative or out-of-range values return NULL.
 * @param[in] direction Positive starts at the head; zero or negative starts at the tail.
 */
Rbc_ListNode Rbc_ListGetNthNode(Rbc_List list, Tcl_Size position, int direction);
/**
 * @brief Reorder existing nodes with qsort without moving their allocations.
 * The comparator receives pointers to node pointers. It must not mutate the
 * list, delete nodes or reenter code that does so. Equal-key ordering is not
 * stable. NULL/short lists are unchanged; scratch-allocation failure silently
 * leaves the list unchanged. Payload ownership is unaffected.
 *
 * @param[in,out] list Initialized list to reorder, or NULL; NULL and lists shorter than two nodes are
 *   unchanged.
 * @param[in] proc Comparator receiving pointers to node pointers; required for a nontrivial sort and must not
 *   mutate or destroy list state.
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
