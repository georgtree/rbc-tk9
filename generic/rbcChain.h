/**
 * @file rbcChain.h
 * @brief Private linked-container ownership and traversal.
 *
 *      Adds descriptions for the chains used in rbc.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCCHAIN
#define _RBCCHAIN

#include <tcl.h>

#define Rbc_ChainGetLength(c) (((c) == NULL) ? (Tcl_Size)0 : (c)->nLinks)
#define Rbc_ChainFirstLink(c) (((c) == NULL) ? NULL : (c)->headPtr)
#define Rbc_ChainLastLink(c) (((c) == NULL) ? NULL : (c)->tailPtr)
#define Rbc_ChainPrevLink(l) ((l)->prevPtr)
#define Rbc_ChainNextLink(l) ((l)->nextPtr)
#define Rbc_ChainGetValue(l) ((l)->clientData)
#define Rbc_ChainSetValue(l, value) ((l)->clientData = (ClientData)(value))
#define Rbc_ChainAppendLink(c, l) (Rbc_ChainLinkBefore((c), (l), (Rbc_ChainLink *)NULL))
#define Rbc_ChainPrependLink(c, l) (Rbc_ChainLinkAfter((c), (l), (Rbc_ChainLink *)NULL))

typedef struct Rbc_ChainLinkStruct Rbc_ChainLink;

/*
 * A Rbc_ChainLink is the container structure for the Rbc_Chain.
 */

struct Rbc_ChainLinkStruct {
    Rbc_ChainLink *prevPtr; /* Link to the previous link */
    Rbc_ChainLink *nextPtr; /* Link to the next link */
    ClientData clientData;  /* Pointer to the data object */
};

typedef int(Rbc_ChainCompareProc)(Rbc_ChainLink **l1PtrPtr, Rbc_ChainLink **l2PtrPtr);

/*
 * A Rbc_Chain is a doubly chained list structure.
 */

typedef struct {
    Rbc_ChainLink *headPtr; /* Pointer to first element in chain */
    Rbc_ChainLink *tailPtr; /* Pointer to last element in chain */
    Tcl_Size nLinks;             /* Number of elements in chain */
} Rbc_Chain;

/**
 * @brief Initialize caller-owned storage as an empty chain.
 * Does not free existing links. Use Reset to clear an initialized chain.
 *
 * @param[in,out] chainPtr Required caller-owned record to initialize; no existing links are released.
 */
void Rbc_ChainInit(Rbc_Chain *chainPtr);
/**
 * @brief Allocate an initialized empty chain.
 *
 * Uses Tcl allocation; allocation exhaustion is not a recoverable Tcl error.
 * The caller owns the container and eventually uses Rbc_ChainDestroy. No payload
 * or link is created.
 */
Rbc_Chain *Rbc_ChainCreate();
/**
 * @brief Reset and free a heap-allocated chain, accepting NULL.
 * Do not use on stack or embedded storage. External payload ownership remains
 * with the caller.
 *
 * @param[in,out] chainPtr Heap-allocated chain to consume, or NULL; all linked records and the container become
 *   invalid.
 */
void Rbc_ChainDestroy(Rbc_Chain *chainPtr);
/**
 * @brief Allocate a detached link with NULL neighbors and payload.
 *
 * Caller owns the Tcl-allocated record until insertion. Link it once into a
 * chain, or release the detached allocation with ckfree. No external payload is
 * allocated or retained; allocation exhaustion follows Tcl's allocation policy.
 */
Rbc_ChainLink *Rbc_ChainNewLink(void);
/**
 * @brief Allocate a detached zeroed link with aligned trailing storage.
 * With nonzero size, clientData points into the same allocation; do not free
 * it separately. With zero size clientData is NULL. Size overflow panics.
 * Link it into a chain or release the detached allocation with ckfree.
 *
 * @param[in] size Requested trailing payload bytes, excluding the link header and alignment padding; zero
 *   creates a plain link.
 */
Rbc_ChainLink *Rbc_ChainAllocLink(size_t size);
/**
 * @brief Allocate a link and append a borrowed payload pointer.
 *
 * Requires an initialized non-NULL chain. Returns the newly linked record,
 * owned by the chain. Does not copy, retain or release clientData. Insertion
 * increments the count and panics on count overflow.
 *
 * @param[in,out] chainPtr Required initialized destination chain; takes ownership of the newly allocated link.
 * @param[in] clientData Borrowed payload pointer, possibly NULL; no copy, retention or destructor is applied.
 */
Rbc_ChainLink *Rbc_ChainAppend(Rbc_Chain *chainPtr, ClientData clientData);
/**
 * @brief Allocate a link and prepend a borrowed payload pointer.
 *
 * Requires an initialized non-NULL chain. Returns the newly linked record,
 * owned by the chain. clientData remains independently owned. Allocation/count
 * failure is not reported through an interpreter.
 *
 * @param[in,out] chainPtr Required initialized destination chain; takes ownership of the newly allocated link.
 * @param[in] clientData Borrowed payload pointer, possibly NULL; no copy, retention or destructor is applied.
 */
Rbc_ChainLink *Rbc_ChainPrepend(Rbc_Chain *chainPtr, ClientData clientData);
/**
 * @brief Free all linked records and leave an empty initialized chain.
 * NULL is harmless. External clientData is not freed. Inline storage created
 * by ChainAllocLink is part of each freed record; run any payload cleanup first.
 *
 * @param[in,out] chainPtr Initialized chain to clear, or NULL; container storage remains owned by the caller.
 */
void Rbc_ChainReset(Rbc_Chain *chainPtr);
/**
 * @brief Insert a detached link after a neighbor, or prepend for NULL.
 *
 * Requires a valid chain, a detached link with NULL neighbor pointers, and a
 * non-NULL neighbor belonging to that chain. Does not validate membership or
 * unlink an already inserted link. Updates endpoints, neighbors and count without
 * allocation or callbacks; count overflow panics. Payload ownership is unchanged.
 *
 * @param[in,out] chainPtr Required initialized destination chain; count and neighbors are updated.
 * @param[in,out] linkPtr Detached link with NULL neighbors; ownership of the record transfers to the chain.
 * @param[in] afterLinkPtr Borrowed neighbor in this chain, or NULL to prepend.
 */
void Rbc_ChainLinkAfter(Rbc_Chain *chainPtr, Rbc_ChainLink *linkPtr, Rbc_ChainLink *afterLinkPtr);
/**
 * @brief Insert a detached link before a neighbor, or append for NULL.
 *
 * Requires a valid chain, a detached link with NULL neighbor pointers, and a
 * non-NULL neighbor belonging to that chain. No membership validation or automatic
 * unlinking occurs. Updates endpoints, neighbors and count without allocation;
 * count overflow panics. The chain owns the inserted record, not its payload.
 *
 * @param[in,out] chainPtr Required initialized destination chain; count and neighbors are updated.
 * @param[in,out] linkPtr Detached link with NULL neighbors; ownership of the record transfers to the chain.
 * @param[in] beforeLinkPtr Borrowed neighbor in this chain, or NULL to append.
 */
void Rbc_ChainLinkBefore(Rbc_Chain *chainPtr, Rbc_ChainLink *linkPtr, Rbc_ChainLink *beforeLinkPtr);
/**
 * @brief Detach a link without freeing its record or payload.
 * Supply its actual chain; links do not store owner identity and this helper
 * cannot validate membership. Clears next/previous pointers. The detached link
 * may be reinserted or freed by the caller.
 *
 * @param[in,out] chainPtr Required actual owning chain; membership is not checked.
 * @param[in,out] linkPtr Required member link; detached with NULL neighbors and returned to caller ownership.
 */
void Rbc_ChainUnlinkLink(Rbc_Chain *chainPtr, Rbc_ChainLink *linkPtr);
/**
 * @brief Unlink and free a link record, including any inline trailing storage.
 * Requires a valid link and its actual chain. Does not free external clientData.
 * All borrowed pointers into the link become invalid.
 *
 * @param[in,out] chainPtr Required actual owning chain; membership is not checked.
 * @param[in,out] linkPtr Required member link; allocation and inline payload are consumed; external payload
 *   ownership is unchanged.
 */
void Rbc_ChainDeleteLink(Rbc_Chain *chainPtr, Rbc_ChainLink *linkPtr);

#endif /* _RBCCHAIN */
