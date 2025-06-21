/*
 * rbcWindow.c --
 *
 *      This module implements additional window functionality for
 *      the rbc toolkit, such as transparent Tk windows,
 *      and reparenting Tk windows.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"

#ifdef WIN32
/*
 *----------------------------------------------------------------------
 *
 * GetWindowHandle --
 *
 *      Returns the XID for the Tk_Window given.  Starting in Tk 8.0,
 *      the toplevel widgets are wrapped by another window.
 *      Currently there's no way to get at that window, other than
 *      what is done here: query the X window hierarchy and grab the
 *      parent.
 *
 * Parameters:
 *      Tk_Window tkwin
 *
 * Results:
 *      Returns the X Window ID of the widget.  If it's a toplevel,
 *      then the XID of the wrapper is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static HWND GetWindowHandle(Tk_Window tkwin) {
    HWND hWnd;
    Window window;

    window = Tk_WindowId(tkwin);
    if (window == None) {
        Tk_MakeWindowExist(tkwin);
    }
    hWnd = Tk_GetHWND(Tk_WindowId(tkwin));
    if (Tk_IsTopLevel(tkwin)) {
        hWnd = GetParent(hWnd);
    }
    return hWnd;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetRealWindowId --
 *
 *      Returns the XID for the Tk_Window given.  Starting in Tk 8.0,
 *      the toplevel widgets are wrapped by another window.
 *      Currently there's no way to get at that window, other than
 *      what is done here: query the X window hierarchy and grab the
 *      parent.
 *
 * Parameters:
 *      Tk_Window tkwin
 *
 * Results:
 *      Returns the X Window ID of the widget.  If it's a toplevel,
 *      then the XID of the wrapper is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Window Rbc_GetRealWindowId(Tk_Window tkwin) { return (Window)GetWindowHandle(tkwin); }

#else /* ! WIN32 */

/*
 *--------------------------------------------------------------
 *
 * rbcGetParent --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      Window window
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static Window rbcGetParent(Display *display, Window window) {
    Window root, parent;
    Window *dummy;
    unsigned int count;

    if (XQueryTree(display, window, &root, &parent, &dummy, &count) > 0) {
        XFree(dummy);
        return parent;
    }
    return None;
}

/*
 *--------------------------------------------------------------
 *
 * GetWindowId --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static Window GetWindowId(Tk_Window tkwin) {
    Window window;

    Tk_MakeWindowExist(tkwin);
    window = Tk_WindowId(tkwin);
    if (Tk_IsTopLevel(tkwin)) {
        Window parent;

        parent = rbcGetParent(Tk_Display(tkwin), window);
        if (parent != None) {
            window = parent;
        }
        window = parent;
    }
    return window;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetRealWindowId --
 *
 *      Returns the XID for the Tk_Window given.  Starting in Tk 8.0,
 *      the toplevel widgets are wrapped by another window.
 *      Currently there's no way to get at that window, other than
 *      what is done here: query the X window hierarchy and grab the
 *      parent.
 *
 * Parameters:
 *      Tk_Window tkwin
 *
 * Results:
 *      Returns the X Window ID of the widget.  If it's a toplevel, then
 *      the XID of the wrapper is returned.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Window Rbc_GetRealWindowId(Tk_Window tkwin) { return GetWindowId(tkwin); }

#endif /* WIN32 */

/*    Instance data handling
 *    Replace use of Tk internals with a per interp hashtable.
 */
#define INST_DATA_KEY "Rbc Window instance data"

static void AssocDataCleanup(void *assocData, Tcl_Interp *interp) {
    Tcl_DeleteHashTable((Tcl_HashTable *)assocData);
    ckfree(assocData);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_SetWindowInstanceData --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin
 *      ClientData instanceData
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_SetWindowInstanceData(Tk_Window tkwin, ClientData instanceData) {
    /*
        TkWindow *winPtr = (TkWindow *)tkwin;

        winPtr->instanceData = instanceData;
    */
    Tcl_Interp *interp = Tk_Interp(tkwin);
    Tcl_HashTable *assocData = Tcl_GetAssocData(interp, INST_DATA_KEY, NULL);
    Tcl_HashEntry *entryPtr;
    int isNew;

    if (!assocData) {
        assocData = (Tcl_HashTable *)ckalloc(sizeof(Tcl_HashTable));
        Tcl_InitHashTable(assocData, TCL_ONE_WORD_KEYS);
        Tcl_SetAssocData(interp, INST_DATA_KEY, AssocDataCleanup, assocData);
    }

    entryPtr = Tcl_CreateHashEntry(assocData, tkwin, &isNew);
    Tcl_SetHashValue(entryPtr, instanceData);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_GetWindowInstanceData --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
ClientData Rbc_GetWindowInstanceData(Tk_Window tkwin) {
    /*
        TkWindow *winPtr = (TkWindow *)tkwin;

        return winPtr->instanceData;
    */
    Tcl_Interp *interp = Tk_Interp(tkwin);
    Tcl_HashTable *assocData = Tcl_GetAssocData(interp, INST_DATA_KEY, NULL);
    Tcl_HashEntry *entryPtr;

    entryPtr = Tcl_FindHashEntry(assocData, tkwin);
    if (!entryPtr)
        return NULL;
    return Tcl_GetHashValue(entryPtr);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_DeleteWindowInstanceData --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
void Rbc_DeleteWindowInstanceData(Tk_Window tkwin) {
    /* body originally empty */
    Tcl_Interp *interp = Tk_Interp(tkwin);
    Tcl_HashTable *assocData = Tcl_GetAssocData(interp, INST_DATA_KEY, NULL);
    Tcl_HashEntry *entryPtr;

    entryPtr = Tcl_FindHashEntry(assocData, tkwin);
    if (entryPtr)
        Tcl_DeleteHashEntry(entryPtr);
}
