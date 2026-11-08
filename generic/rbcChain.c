/*
 * rbcChain.c --
 *
 * The module implements a generic linked list package.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcChain.h"
#include "rbcInt.h"

#ifndef ALIGN
#define ALIGN(a) (((size_t)a + (sizeof(double) - 1)) & (~(sizeof(double) - 1)))
#endif /* ALIGN */

/* Contract and ownership are documented in rbcChain.h. */
Rbc_Chain *Rbc_ChainCreate() {
    Rbc_Chain *chainPtr;

    chainPtr = (Rbc_Chain *)ckalloc(sizeof(Rbc_Chain));
    if (chainPtr != NULL) {
        Rbc_ChainInit(chainPtr);
    }
    return chainPtr;
}

void Rbc_ChainInit(Rbc_Chain *chainPtr) {
    chainPtr->nLinks = 0;
    chainPtr->headPtr = chainPtr->tailPtr = NULL;
}

/* Contract and ownership are documented in rbcChain.h. */
void Rbc_ChainLinkAfter(Rbc_Chain *chainPtr, Rbc_ChainLink *linkPtr, Rbc_ChainLink *afterPtr) {
    if (chainPtr->headPtr == NULL) {
        chainPtr->tailPtr = chainPtr->headPtr = linkPtr;
    } else {
        if (afterPtr == NULL) {
            /* Prepend to the front of the chain */
            linkPtr->nextPtr = chainPtr->headPtr;
            linkPtr->prevPtr = NULL;
            chainPtr->headPtr->prevPtr = linkPtr;
            chainPtr->headPtr = linkPtr;
        } else {
            linkPtr->nextPtr = afterPtr->nextPtr;
            linkPtr->prevPtr = afterPtr;
            if (afterPtr == chainPtr->tailPtr) {
                chainPtr->tailPtr = linkPtr;
            } else {
                afterPtr->nextPtr->prevPtr = linkPtr;
            }
            afterPtr->nextPtr = linkPtr;
        }
    }
    if (chainPtr->nLinks == TCL_SIZE_MAX) {
        Tcl_Panic("Rbc_ChainLinkAfter: chain length overflow");
    }
    chainPtr->nLinks++;
}

/* Contract and ownership are documented in rbcChain.h. */
void Rbc_ChainLinkBefore(Rbc_Chain *chainPtr, Rbc_ChainLink *linkPtr, Rbc_ChainLink *beforePtr) {
    if (chainPtr->headPtr == NULL) {
        chainPtr->tailPtr = chainPtr->headPtr = linkPtr;
    } else {
        if (beforePtr == NULL) {
            /* Append to the end of the chain. */
            linkPtr->nextPtr = NULL;
            linkPtr->prevPtr = chainPtr->tailPtr;
            chainPtr->tailPtr->nextPtr = linkPtr;
            chainPtr->tailPtr = linkPtr;
        } else {
            linkPtr->prevPtr = beforePtr->prevPtr;
            linkPtr->nextPtr = beforePtr;
            if (beforePtr == chainPtr->headPtr) {
                chainPtr->headPtr = linkPtr;
            } else {
                beforePtr->prevPtr->nextPtr = linkPtr;
            }
            beforePtr->prevPtr = linkPtr;
        }
    }
    if (chainPtr->nLinks == TCL_SIZE_MAX) {
        Tcl_Panic("Rbc_ChainLinkBefore: chain length overflow");
    }
    chainPtr->nLinks++;
}

/* Contract and ownership are documented in rbcChain.h. */
Rbc_ChainLink *Rbc_ChainNewLink() {
    Rbc_ChainLink *linkPtr;

    linkPtr = (Rbc_ChainLink *)ckalloc(sizeof(Rbc_ChainLink));
    assert(linkPtr);
    linkPtr->clientData = NULL;
    linkPtr->nextPtr = linkPtr->prevPtr = NULL;
    return linkPtr;
}

void Rbc_ChainReset(Rbc_Chain *chainPtr) {
    if (chainPtr != NULL) {
        Rbc_ChainLink *oldPtr;

        Rbc_ChainLink *linkPtr = chainPtr->headPtr;
        while (linkPtr != NULL) {
            oldPtr = linkPtr;
            linkPtr = linkPtr->nextPtr;
            ckfree((char *)oldPtr);
        }
        Rbc_ChainInit(chainPtr);
    }
}

void Rbc_ChainDestroy(Rbc_Chain *chainPtr) {
    if (chainPtr != NULL) {
        Rbc_ChainReset(chainPtr);
        ckfree((char *)chainPtr);
    }
}

void Rbc_ChainUnlinkLink(Rbc_Chain *chainPtr, Rbc_ChainLink *linkPtr) {
    /* Indicates if the link is actually removed from the chain. */
    int unlinked;

    unlinked = FALSE;
    if (chainPtr->headPtr == linkPtr) {
        chainPtr->headPtr = linkPtr->nextPtr;
        unlinked = TRUE;
    }
    if (chainPtr->tailPtr == linkPtr) {
        chainPtr->tailPtr = linkPtr->prevPtr;
        unlinked = TRUE;
    }
    if (linkPtr->nextPtr != NULL) {
        linkPtr->nextPtr->prevPtr = linkPtr->prevPtr;
        unlinked = TRUE;
    }
    if (linkPtr->prevPtr != NULL) {
        linkPtr->prevPtr->nextPtr = linkPtr->nextPtr;
        unlinked = TRUE;
    }
    if (unlinked) {
        assert(chainPtr->nLinks > 0);
        chainPtr->nLinks--;
    }
    linkPtr->prevPtr = linkPtr->nextPtr = NULL;
}

void Rbc_ChainDeleteLink(Rbc_Chain *chainPtr, Rbc_ChainLink *linkPtr) {
    Rbc_ChainUnlinkLink(chainPtr, linkPtr);
    ckfree((char *)linkPtr);
}

/* Contract and ownership are documented in rbcChain.h. */
Rbc_ChainLink *Rbc_ChainAppend(Rbc_Chain *chainPtr, ClientData clientData) {
    Rbc_ChainLink *linkPtr;

    linkPtr = Rbc_ChainNewLink();
    Rbc_ChainLinkBefore(chainPtr, linkPtr, (Rbc_ChainLink *)NULL);
    Rbc_ChainSetValue(linkPtr, clientData);
    return linkPtr;
}

/* Contract and ownership are documented in rbcChain.h. */
Rbc_ChainLink *Rbc_ChainPrepend(Rbc_Chain *chainPtr, ClientData clientData) {
    Rbc_ChainLink *linkPtr;

    linkPtr = Rbc_ChainNewLink();
    Rbc_ChainLinkAfter(chainPtr, linkPtr, (Rbc_ChainLink *)NULL);
    Rbc_ChainSetValue(linkPtr, clientData);
    return linkPtr;
}

Rbc_ChainLink *Rbc_ChainAllocLink(size_t extraSize) {
    Rbc_ChainLink *linkPtr;
    size_t linkSize;
    size_t totalSize;

    linkSize = ALIGN(sizeof(*linkPtr));
    if (extraSize > SIZE_MAX - linkSize) {
        Tcl_Panic("Rbc_ChainAllocLink: allocation size overflow");
    }
    totalSize = linkSize + extraSize;
    linkPtr = Rbc_Calloc(1, totalSize);
    if (extraSize > 0) {
        /*
         * Point clientData at the aligned storage immediately after
         * the normal link structure.
         */
        linkPtr->clientData = (ClientData)((char *)linkPtr + linkSize);
    }
    return linkPtr;
}
