/**
 * @file rbcList.c
 * @brief Keyed-list search and reclamation.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @defgroup list_internals Keyed-list search and reclamation
 * Private helpers; callers retain storage and lifetime responsibilities.
 */
#include "rbcInt.h"
#include "rbcList.h"
#include <stddef.h>
#include <stdint.h>

static struct Rbc_ListNodeStruct *FindString (struct Rbc_ListStruct *listPtr, const char *key);
static Rbc_ListNode FindOneWord (struct Rbc_ListStruct *listPtr, const char *key);
static Rbc_ListNode FindArray (struct Rbc_ListStruct *listPtr, const char *key);
static void FreeNode (struct Rbc_ListNodeStruct *nodePtr);

/**
 * @brief Find the first node with equal string-key contents.
 * @ingroup list_internals
 *
 * Requires a valid string-key list and non-NULL NUL-terminated key. Checks the
 * first byte before strcmp while traversing head to tail. Returns a borrowed
 * node or NULL without mutation, allocation or retention.
 *
 * @param[in] listPtr Borrowed live list with key representation matching this lookup helper; membership is not
 *   changed.
 * @param[in] key Borrowed non-NULL NUL-terminated string whose contents are compared.
 */
static struct Rbc_ListNodeStruct *FindString(struct Rbc_ListStruct *listPtr, const char *key) {
    register struct Rbc_ListNodeStruct *nodePtr;
    char c;

    c = key[0];
    for (nodePtr = listPtr->headPtr; nodePtr != NULL; nodePtr = nodePtr->nextPtr) {
        if ((c == nodePtr->key.string[0]) && (strcmp(key, nodePtr->key.string) == 0)) {
            return nodePtr;
        }
    }
    return NULL;
}

/**
 * @brief Find the first node with the same pointer-sized key value.
 * @ingroup list_internals
 *
 * Requires a valid one-word-key list. Compares identity, not pointed-to contents;
 * NULL is a valid key value. Returns a borrowed first match or NULL. Does not
 * dereference keys or change membership/payload ownership.
 *
 * @param[in] listPtr Borrowed live list with key representation matching this lookup helper; membership is not
 *   changed.
 * @param[in] key Pointer-sized identity key; NULL is valid and pointed-to memory is never read.
 */
static Rbc_ListNode FindOneWord(struct Rbc_ListStruct *listPtr, const char *key) {
    register struct Rbc_ListNodeStruct *nodePtr;

    for (nodePtr = listPtr->headPtr; nodePtr != NULL; nodePtr = nodePtr->nextPtr) {
        if (key == nodePtr->key.oneWordValue) {
            return nodePtr;
        }
    }
    return NULL;
}

/**
 * @brief Find the first node with equal integer-array key bytes.
 * @ingroup list_internals
 *
 * Requires a valid list whose type is the key's int count and a readable key
 * buffer of that size. Invalid count or byte-size overflow panics. Uses memcmp
 * on copied native int bytes, not text or portable serialization. Returns a
 * borrowed first match or NULL without mutation or allocation.
 *
 * @param[in] listPtr Borrowed live list with key representation matching this lookup helper; membership is not
 *   changed.
 * @param[in] key Borrowed readable bytes for listPtr->type native ints, compared without alignment-dependent
 *   dereferencing.
 */
static Rbc_ListNode FindArray(struct Rbc_ListStruct *listPtr, const char *key) {
    struct Rbc_ListNodeStruct *nodePtr;
    size_t nBytes;

    if (listPtr->type <= TCL_ONE_WORD_KEYS) {
        Tcl_Panic("FindArray: invalid array-key size");
    }
    if ((size_t)listPtr->type > SIZE_MAX / sizeof(int)) {
        Tcl_Panic("FindArray: array-key size overflow");
    }
    nBytes = (size_t)listPtr->type * sizeof(int);
    for (nodePtr = listPtr->headPtr; nodePtr != NULL; nodePtr = nodePtr->nextPtr) {
        if (memcmp(key, nodePtr->key.words, nBytes) == 0) {
            return nodePtr;
        }
    }
    return NULL;
}

/**
 * @brief Free a node allocation and its inline copied key.
 * @ingroup list_internals
 *
 * Caller must already have removed the node from membership, or be disposing
 * of a never-inserted record. Does not unlink, adjust list count or free payloads
 * or one-word key targets. All pointers into the node become invalid.
 *
 * @param[in,out] nodePtr Owned detached node allocation; freed without unlinking or releasing its borrowed
 *   payload.
 */
static void FreeNode(struct Rbc_ListNodeStruct *nodePtr) { ckfree((char *)nodePtr); }

/* Contract and ownership are documented in rbcList.h. */
Rbc_List Rbc_ListCreate(int type) {
    struct Rbc_ListStruct *listPtr;

    listPtr = (struct Rbc_ListStruct *)ckalloc(sizeof(struct Rbc_ListStruct));
    if (listPtr != NULL) {
        Rbc_ListInit(listPtr, type);
    }
    return listPtr;
}

Rbc_ListNode Rbc_ListCreateNode(struct Rbc_ListStruct *listPtr, const char *key) {
    struct Rbc_ListNodeStruct *nodePtr;
    size_t baseSize;
    size_t keySize;
    size_t totalSize;

    baseSize = offsetof(struct Rbc_ListNodeStruct, key);
    if (listPtr->type == TCL_STRING_KEYS) {
        size_t length;

        length = strlen(key);
        if (length == SIZE_MAX) {
            Tcl_Panic("Rbc_ListCreateNode: string-key size overflow");
        }
        keySize = length + 1;
    } else if (listPtr->type == TCL_ONE_WORD_KEYS) {
        keySize = sizeof(nodePtr->key.oneWordValue);
    } else {
        if (listPtr->type <= TCL_ONE_WORD_KEYS) {
            Tcl_Panic("Rbc_ListCreateNode: invalid key type");
        }
        if ((size_t)listPtr->type > SIZE_MAX / sizeof(int)) {
            Tcl_Panic("Rbc_ListCreateNode: array-key size overflow");
        }
        keySize = (size_t)listPtr->type * sizeof(int);
    }
    if (keySize > SIZE_MAX - baseSize) {
        Tcl_Panic("Rbc_ListCreateNode: allocation size overflow");
    }
    totalSize = baseSize + keySize;
    nodePtr = Rbc_Calloc(1, totalSize);
    nodePtr->clientData = NULL;
    nodePtr->nextPtr = NULL;
    nodePtr->prevPtr = NULL;
    nodePtr->listPtr = listPtr;
    switch (listPtr->type) {
    case TCL_STRING_KEYS:
        memcpy(nodePtr->key.string, key, keySize);
        break;
    case TCL_ONE_WORD_KEYS:
        nodePtr->key.oneWordValue = key;
        break;
    default:
        memcpy(nodePtr->key.words, key, keySize);
        break;
    }
    return nodePtr;
}

void Rbc_ListReset(struct Rbc_ListStruct *listPtr) {
    if (listPtr != NULL) {
        struct Rbc_ListNodeStruct *oldPtr;
        struct Rbc_ListNodeStruct *nodePtr = listPtr->headPtr;
        while (nodePtr != NULL) {
            oldPtr = nodePtr;
            nodePtr = nodePtr->nextPtr;
            FreeNode(oldPtr);
        }
        Rbc_ListInit(listPtr, listPtr->type);
    }
}

/* Contract and ownership are documented in rbcList.h. */
void Rbc_ListDestroy(struct Rbc_ListStruct *listPtr) {
    if (listPtr != NULL) {
        Rbc_ListReset(listPtr);
        ckfree((char *)listPtr);
    }
}

/* Contract and ownership are documented in rbcList.h. */
void Rbc_ListInit(struct Rbc_ListStruct *listPtr, int type) {
    listPtr->nNodes = 0;
    listPtr->headPtr = listPtr->tailPtr = NULL;
    listPtr->type = type;
}

/* Contract and ownership are documented in rbcList.h. */
void Rbc_ListLinkAfter(struct Rbc_ListStruct *listPtr, struct Rbc_ListNodeStruct *nodePtr,
                       struct Rbc_ListNodeStruct *afterPtr) {
    if (listPtr->nNodes == TCL_SIZE_MAX) {
        Tcl_Panic("Rbc_ListLinkAfter: list length overflow");
    }
    if (listPtr->headPtr == NULL) {
        listPtr->tailPtr = listPtr->headPtr = nodePtr;
    } else {
        if (afterPtr == NULL) {
            /* Prepend to the front of the list */
            nodePtr->nextPtr = listPtr->headPtr;
            nodePtr->prevPtr = NULL;
            listPtr->headPtr->prevPtr = nodePtr;
            listPtr->headPtr = nodePtr;
        } else {
            nodePtr->nextPtr = afterPtr->nextPtr;
            nodePtr->prevPtr = afterPtr;
            if (afterPtr == listPtr->tailPtr) {
                listPtr->tailPtr = nodePtr;
            } else {
                afterPtr->nextPtr->prevPtr = nodePtr;
            }
            afterPtr->nextPtr = nodePtr;
        }
    }
    nodePtr->listPtr = listPtr;
    listPtr->nNodes++;
}

/* Contract and ownership are documented in rbcList.h. */
void Rbc_ListLinkBefore(struct Rbc_ListStruct *listPtr, struct Rbc_ListNodeStruct *nodePtr,
                        struct Rbc_ListNodeStruct *beforePtr) {
    if (listPtr->nNodes == TCL_SIZE_MAX) {
        Tcl_Panic("Rbc_ListLinkBefore: list length overflow");
    }
    if (listPtr->headPtr == NULL) {
        listPtr->tailPtr = listPtr->headPtr = nodePtr;
    } else {
        if (beforePtr == NULL) {
            /* Append onto the end of the list */
            nodePtr->nextPtr = NULL;
            nodePtr->prevPtr = listPtr->tailPtr;
            listPtr->tailPtr->nextPtr = nodePtr;
            listPtr->tailPtr = nodePtr;
        } else {
            nodePtr->prevPtr = beforePtr->prevPtr;
            nodePtr->nextPtr = beforePtr;
            if (beforePtr == listPtr->headPtr) {
                listPtr->headPtr = nodePtr;
            } else {
                beforePtr->prevPtr->nextPtr = nodePtr;
            }
            beforePtr->prevPtr = nodePtr;
        }
    }
    nodePtr->listPtr = listPtr;
    listPtr->nNodes++;
}

void Rbc_ListUnlinkNode(struct Rbc_ListNodeStruct *nodePtr) {
    struct Rbc_ListStruct *listPtr;

    listPtr = nodePtr->listPtr;
    if (listPtr != NULL) {
        if (listPtr->headPtr == nodePtr) {
            listPtr->headPtr = nodePtr->nextPtr;
        }
        if (listPtr->tailPtr == nodePtr) {
            listPtr->tailPtr = nodePtr->prevPtr;
        }
        if (nodePtr->nextPtr != NULL) {
            nodePtr->nextPtr->prevPtr = nodePtr->prevPtr;
        }
        if (nodePtr->prevPtr != NULL) {
            nodePtr->prevPtr->nextPtr = nodePtr->nextPtr;
        }
        assert(listPtr->nNodes > 0);
        listPtr->nNodes--;
        nodePtr->listPtr = NULL;
        nodePtr->prevPtr = NULL;
        nodePtr->nextPtr = NULL;
    }
}

Rbc_ListNode Rbc_ListGetNode(struct Rbc_ListStruct *listPtr, const char *key) {
    if (listPtr != NULL) {
        switch (listPtr->type) {
        case TCL_STRING_KEYS:
            return FindString(listPtr, key);
        case TCL_ONE_WORD_KEYS:
            return FindOneWord(listPtr, key);
        default:
            return FindArray(listPtr, key);
        }
    }
    return NULL;
}

void Rbc_ListDeleteNode(struct Rbc_ListNodeStruct *nodePtr) {
    Rbc_ListUnlinkNode(nodePtr);
    FreeNode(nodePtr);
}

/* Contract and ownership are documented in rbcList.h. */
void Rbc_ListDeleteNodeByKey(struct Rbc_ListStruct *listPtr, const char *key) {
    struct Rbc_ListNodeStruct *nodePtr;

    nodePtr = Rbc_ListGetNode(listPtr, key);
    if (nodePtr != NULL) {
        Rbc_ListDeleteNode(nodePtr);
    }
}

/* Contract and ownership are documented in rbcList.h. */
Rbc_ListNode Rbc_ListAppend(struct Rbc_ListStruct *listPtr, const char *key, ClientData clientData) {
    struct Rbc_ListNodeStruct *nodePtr;

    nodePtr = Rbc_ListCreateNode(listPtr, key);
    Rbc_ListSetValue(nodePtr, clientData);
    Rbc_ListAppendNode(listPtr, nodePtr);
    return nodePtr;
}

/* Contract and ownership are documented in rbcList.h. */
Rbc_ListNode Rbc_ListPrepend(struct Rbc_ListStruct *listPtr, const char *key, ClientData clientData) {
    struct Rbc_ListNodeStruct *nodePtr;

    nodePtr = Rbc_ListCreateNode(listPtr, key);
    Rbc_ListSetValue(nodePtr, clientData);
    Rbc_ListPrependNode(listPtr, nodePtr);
    return nodePtr;
}

/* Contract and ownership are documented in rbcList.h. */
Rbc_ListNode Rbc_ListGetNthNode(struct Rbc_ListStruct *listPtr, Tcl_Size position, int direction) {
    struct Rbc_ListNodeStruct *nodePtr;

    if ((listPtr == NULL) || (position < 0) || (position >= listPtr->nNodes)) {
        return NULL;
    }
    if (direction > 0) {
        for (nodePtr = listPtr->headPtr; nodePtr != NULL; nodePtr = nodePtr->nextPtr) {
            if (position == 0) {
                return nodePtr;
            }
            position--;
        }
    } else {
        for (nodePtr = listPtr->tailPtr; nodePtr != NULL; nodePtr = nodePtr->prevPtr) {
            if (position == 0) {
                return nodePtr;
            }
            position--;
        }
    }
    return NULL;
}

void Rbc_ListSort(struct Rbc_ListStruct *listPtr, Rbc_ListCompareProc *proc) {
    struct Rbc_ListNodeStruct **nodeArr;
    struct Rbc_ListNodeStruct *nodePtr;
    Tcl_Size i;

    if ((listPtr == NULL) || (listPtr->nNodes < 2)) {
        return;
    }
    if (listPtr->nNodes > (Tcl_Size)(SIZE_MAX / sizeof(*nodeArr))) {
        Tcl_Panic("Rbc_ListSort: node-array size overflow");
    }
    nodeArr = Tcl_AttemptAlloc((size_t)listPtr->nNodes * sizeof(*nodeArr));
    if (nodeArr == NULL) {
        return;
    }
    i = 0;
    for (nodePtr = listPtr->headPtr; nodePtr != NULL; nodePtr = nodePtr->nextPtr) {
        assert(i < listPtr->nNodes);
        nodeArr[i++] = nodePtr;
    }
    assert(i == listPtr->nNodes);
    qsort(nodeArr, (size_t)listPtr->nNodes, sizeof(*nodeArr), (QSortCompareProc *)proc);
    nodePtr = nodeArr[0];
    listPtr->headPtr = nodePtr;
    nodePtr->prevPtr = NULL;
    for (i = 1; i < listPtr->nNodes; i++) {
        nodePtr->nextPtr = nodeArr[i];
        nodePtr->nextPtr->prevPtr = nodePtr;
        nodePtr = nodePtr->nextPtr;
    }
    listPtr->tailPtr = nodePtr;
    nodePtr->nextPtr = NULL;
    ckfree(nodeArr);
}
