/*
 * rbcAlloc.C --
 *
 *      TODO: Description
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"

/*
 *----------------------------------------------------------------------
 *
 * RbcCalloc --
 *
 *      TODO: Description
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void *RbcCalloc(size_t nElems, size_t sizeOfElem) {
    char *allocPtr;
    size_t size;

    if ((nElems != 0) && (sizeOfElem > SIZE_MAX / nElems)) {
        Tcl_Panic("RbcCalloc: allocation size overflow");
    }
    size = nElems * sizeOfElem;
    /*
     * Keep the allocation valid even for an empty logical array.
     * This preserves the existing non-NULL, ckalloc-style behaviour.
     */
    if (size == 0) {
        size = 1;
    }
    allocPtr = ckalloc(size);
    memset(allocPtr, 0, size);
    return allocPtr;
}

/*
 *----------------------------------------------------------------------
 *
 * RbcStrdup --
 *
 *      Create a copy of the string from heap storage.
 *
 * Results:
 *      Returns a pointer to the need string copy.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
char *RbcStrdup(const char *string) {
    size_t size;
    char *allocPtr;

    size = strlen(string) + 1;
    allocPtr = ckalloc(size * sizeof(char));
    if (allocPtr != NULL) {
        strcpy(allocPtr, string);
    }
    return allocPtr;
}
