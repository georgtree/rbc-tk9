/*
 * rbcList.c --
 *
 *      The module implements generic linked lists.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"
#include "rbcList.h"

static struct Rbc_ListNodeStruct *FindString (struct Rbc_ListStruct *listPtr, const char *key);
static Rbc_ListNode FindOneWord (struct Rbc_ListStruct *listPtr, const char *key);
static Rbc_ListNode FindArray (struct Rbc_ListStruct *listPtr, const char *key);
static void FreeNode (struct Rbc_ListNodeStruct *nodePtr);

/*
 *--------------------------------------------------------------
 *
 * FindString --
 *
 *      TODO: Description
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr - List to search
 *      const char *key - Key to match
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * FindOneWord --
 *
 *      TODO: Description
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr - List to search
 *      const char *key - Key to match
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * FindArray --
 *
 *      TODO: Description
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr - List to search
 *      const char *key - Key to match
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static Rbc_ListNode FindArray(struct Rbc_ListStruct *listPtr, const char *key) {
    register struct Rbc_ListNodeStruct *nodePtr;
    int nBytes;

    nBytes = sizeof(int) * listPtr->type;
    for (nodePtr = listPtr->headPtr; nodePtr != NULL; nodePtr = nodePtr->nextPtr) {
        if (memcmp(key, nodePtr->key.words, nBytes) == 0) {
            return nodePtr;
        }
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeNode --
 *
 *      Free the memory allocated for the node.
 *
 * Parameters:
 *      struct Rbc_ListNodeStruct *nodePtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void FreeNode(struct Rbc_ListNodeStruct *nodePtr) { ckfree((char *)nodePtr); }

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListCreate --
 *
 *      Creates a new linked list structure and initializes its pointers
 *
 * Parameters:
 *      int type
 *
 * Results:
 *      Returns a pointer to the newly created list structure.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Rbc_List Rbc_ListCreate(int type) {
    struct Rbc_ListStruct *listPtr;

    listPtr = (struct Rbc_ListStruct *)ckalloc(sizeof(struct Rbc_ListStruct));
    if (listPtr != NULL) {
        Rbc_ListInit(listPtr, type);
    }
    return listPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListCreateNode --
 *
 *      Creates a list node holder.  This routine does not insert
 *      the node into the list, nor does it no attempt to maintain
 *      consistency of the keys.  For example, more than one node
 *      may use the same key.
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr
 *      const char *key - Unique key to reference object
 *
 * Results:
 *      The return value is the pointer to the newly created node.
 *
 * Side Effects:
 *      The key is not copied, only the Uid is kept.  It is assumed
 *      this key will not change in the life of the node.
 *
 *----------------------------------------------------------------------
 */
Rbc_ListNode Rbc_ListCreateNode(struct Rbc_ListStruct *listPtr, const char *key) {
    register struct Rbc_ListNodeStruct *nodePtr;
    int keySize;

    if (listPtr->type == TCL_STRING_KEYS) {
        keySize = strlen(key) + 1;
    } else if (listPtr->type == TCL_ONE_WORD_KEYS) {
        keySize = sizeof(int);
    } else {
        keySize = sizeof(int) * listPtr->type;
    }
    nodePtr = RbcCalloc(1, sizeof(struct Rbc_ListNodeStruct) + keySize - 4);
    assert(nodePtr);
    nodePtr->clientData = NULL;
    nodePtr->nextPtr = nodePtr->prevPtr = NULL;
    nodePtr->listPtr = listPtr;
    switch (listPtr->type) {
    case TCL_STRING_KEYS:
        strcpy(nodePtr->key.string, key);
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListReset --
 *
 *      Removes all the entries from a list, removing pointers to the
 *      objects and keys (not the objects or keys themselves).  The
 *      node counter is reset to zero.
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr - List to clear
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_ListReset(struct Rbc_ListStruct *listPtr) {
    if (listPtr != NULL) {
        register struct Rbc_ListNodeStruct *oldPtr;
        register struct Rbc_ListNodeStruct *nodePtr = listPtr->headPtr;

        while (nodePtr != NULL) {
            oldPtr = nodePtr;
            nodePtr = nodePtr->nextPtr;
            FreeNode(oldPtr);
        }
        Rbc_ListInit(listPtr, listPtr->type);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListDestroy
 *
 *     Frees all list structures
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr
 *
 * Results:
 *      Returns a pointer to the newly created list structure.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_ListDestroy(struct Rbc_ListStruct *listPtr) {
    if (listPtr != NULL) {
        Rbc_ListReset(listPtr);
        ckfree((char *)listPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListInit --
 *
 *      Initializes a linked list.
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr
 *      int type
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_ListInit(struct Rbc_ListStruct *listPtr, int type) {
    listPtr->nNodes = 0;
    listPtr->headPtr = listPtr->tailPtr = NULL;
    listPtr->type = type;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListLinkAfter --
 *
 *      Inserts an node following a given node.
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr
 *      struct Rbc_ListNodeStruct *nodePtr
 *      struct Rbc_ListNodeStruct *afterPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_ListLinkAfter(struct Rbc_ListStruct *listPtr, struct Rbc_ListNodeStruct *nodePtr,
                       struct Rbc_ListNodeStruct *afterPtr) {
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListLinkBefore --
 *
 *      Inserts an node preceding a given node.
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr - List to contain new node
 *      struct Rbc_ListNodeStruct *nodePtr - New node to be inserted
 *      struct Rbc_ListNodeStruct *beforePtr - Node to link before
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_ListLinkBefore(struct Rbc_ListStruct *listPtr, struct Rbc_ListNodeStruct *nodePtr,
                        struct Rbc_ListNodeStruct *beforePtr) {
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListUnlinkNode --
 *
 *      Unlinks an node from the given list. The node itself is
 *      not deallocated, but only removed from the list.
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr - List to contain new node
 *
 * Results:
 *      None.
 *
 *----------------------------------------------------------------------
 */
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
        nodePtr->listPtr = NULL;
        listPtr->nNodes--;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListGetNode --
 *
 *      Find the first node matching the key given.
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr - List to search
 *      const char *key - Key to match
 *
 * Results:
 *      Returns the pointer to the node.  If no node matching
 *      the key given is found, then NULL is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListDeleteNode --
 *
 *      Unlinks and deletes the given node.
 *
 * Parameters:
 *      struct Rbc_ListNodeStruct *nodePtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_ListDeleteNode(struct Rbc_ListNodeStruct *nodePtr) {
    Rbc_ListUnlinkNode(nodePtr);
    FreeNode(nodePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rb_ListDeleteNodeByKey --
 *
 *      Find the node and free the memory allocated for the node.
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr
 *      const char *key
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rb_ListDeleteNodeByKey(struct Rbc_ListStruct *listPtr, const char *key) {
    struct Rbc_ListNodeStruct *nodePtr;

    nodePtr = Rbc_ListGetNode(listPtr, key);
    if (nodePtr != NULL) {
        Rbc_ListDeleteNode(nodePtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ListAppend --
 *
 *      TODO: Description
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr
 *      const char *key
 *      ClientData clientData
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Rbc_ListNode Rbc_ListAppend(struct Rbc_ListStruct *listPtr, const char *key, ClientData clientData) {
    struct Rbc_ListNodeStruct *nodePtr;

    nodePtr = Rbc_ListCreateNode(listPtr, key);
    Rbc_ListSetValue(nodePtr, clientData);
    Rbc_ListAppendNode(listPtr, nodePtr);
    return nodePtr;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_ListPrepend --
 *
 *      TODO: Description
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr
 *      const char *key
 *      ClientData clientData
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Rbc_ListNode Rbc_ListPrepend(struct Rbc_ListStruct *listPtr, const char *key, ClientData clientData) {
    struct Rbc_ListNodeStruct *nodePtr;

    nodePtr = Rbc_ListCreateNode(listPtr, key);
    Rbc_ListSetValue(nodePtr, clientData);
    Rbc_ListPrependNode(listPtr, nodePtr);
    return nodePtr;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListGetNthNode --
 *
 *      Find the node based upon a given position in list.
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr - List to traverse
 *      int position - Index of node to select from front or back of the list.
 *      int direction
 *
 * Results:
 *      Returns the pointer to the node, if that numbered element
 *      exists. Otherwise NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Rbc_ListNode Rbc_ListGetNthNode(struct Rbc_ListStruct *listPtr, int position, int direction) {
    register struct Rbc_ListNodeStruct *nodePtr;

    if (listPtr != NULL) {
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
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ListSort --
 *
 *      Find the node based upon a given position in list.
 *
 * Parameters:
 *      struct Rbc_ListStruct *listPtr - List to traverse
 *      Rbc_ListCompareProc *proc
 *
 * Results:
 *      Returns the pointer to the node, if that numbered element
 *      exists. Otherwise NULL.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_ListSort(struct Rbc_ListStruct *listPtr, Rbc_ListCompareProc *proc) {
    struct Rbc_ListNodeStruct **nodeArr;
    register struct Rbc_ListNodeStruct *nodePtr;
    register int i;

    if (listPtr->nNodes < 2) {
        return;
    }
    nodeArr = (struct Rbc_ListNodeStruct **)ckalloc(sizeof(Rbc_List) * (listPtr->nNodes + 1));
    if (nodeArr == NULL) {
        return; /* Out of memory. */
    }
    i = 0;
    for (nodePtr = listPtr->headPtr; nodePtr != NULL; nodePtr = nodePtr->nextPtr) {
        nodeArr[i++] = nodePtr;
    }
    qsort((char *)nodeArr, listPtr->nNodes, sizeof(struct Rbc_ListNodeStruct *), (QSortCompareProc *)proc);

    /* Rethread the list. */
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
    ckfree((char *)nodeArr);
}
