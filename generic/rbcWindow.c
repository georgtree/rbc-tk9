/**
 * @file rbcWindow.c
 * @brief Native window identifiers and borrowed interpreter-local payloads.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @defgroup window_helpers Window association and X11 lookup internals
 */
/** @defgroup window_windows Windows native handle lookup
 */

#include "rbcInt.h"

#ifdef WIN32
#include <tkPlatDecls.h>
/**
 * @brief Return the client HWND or its immediate parent for a toplevel.
 * @ingroup window_windows
 *
 * Realizes the Tk window if needed, then converts its drawable with
 * Tk_GetHWND. Toplevels use GetParent; a NULL result is returned unchanged.
 * Does not walk ancestors, map the window, retain the handle or install
 * destruction hooks. Requires a live Tk window on its owning thread.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread; may be realized to obtain a native ID, but is
 *   not retained.
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

/**
 * @brief Return a native client or toplevel-wrapper identifier.
 *
 * May realize the borrowed Tk window. X11 queries the toplevel parent; Windows
 * returns a raw HWND cast to Window. It is not a portable Tk drawable token.
 * No ownership transfers. X11 keeps the realized client if parent lookup fails;
 * callers must still distinguish native wrapper IDs from Tk drawable tokens.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread; may be realized to obtain a native ID, but is
 *   not retained.
 * @ingroup window_windows
 */
Window Rbc_GetRealWindowId(Tk_Window tkwin) { return (Window)GetWindowHandle(tkwin); }

#else /* ! WIN32 */

/**
 * @brief Query an X window parent and release the returned child array.
 * @ingroup window_helpers
 *
 * Returns None when XQueryTree reports failure. Owns the query child array
 * only on success and releases it with XFree before returning. The returned
 * parent identifier is borrowed; no window is realized or retained here.
 *
 * @param[in] display Borrowed live X display owning the supplied drawable and resources.
 * @param[in] window Existing X window ID on display; queried without realizing or retaining it.
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

/**
 * @brief Realize a Tk client and select its immediate X11 parent for a toplevel.
 * @ingroup window_helpers
 *
 * Non-toplevel windows retain their client ID. For toplevels, a successful
 * non-None parent replaces it; failed lookup retains the client ID. This
 * does not guarantee a particular window-manager decoration ancestor.
 * Requires a live window and does not change ownership or map it explicitly.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread; may be realized to obtain a native ID, but is
 *   not retained.
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
    }
    return window;
}

/**
 * @brief Return a native client or toplevel-wrapper identifier.
 *
 * May realize the borrowed Tk window. X11 queries the toplevel parent; Windows
 * returns a raw HWND cast to Window. It is not a portable Tk drawable token.
 * No ownership transfers. X11 keeps the realized client if parent lookup fails;
 * callers must still distinguish native wrapper IDs from Tk drawable tokens.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread; may be realized to obtain a native ID, but is
 *   not retained.
 */
Window Rbc_GetRealWindowId(Tk_Window tkwin) { return GetWindowId(tkwin); }

#endif /* WIN32 */

/*    Instance data handling
 *    Replace use of Tk internals with a per interp hashtable.
 */
#define INST_DATA_KEY "Rbc Window instance data"

/**
 * @brief Release the interpreter-owned association table and its allocation.
 * @ingroup window_helpers
 *
 * Registered as a Tcl associated-data delete callback. Hash entries contain
 * borrowed Tk_Window keys and borrowed payloads; neither is dereferenced or
 * released. The interp argument is unused. Requires the table allocation
 * installed by Rbc_SetWindowInstanceData, not a caller-owned hash table.
 *
 * @param[in,out] assocData Owned hash-table allocation installed as interpreter associated data; entries and
 *   allocation are released, payloads remain borrowed.
 * @param[in] interp Unused interpreter-delete callback argument; no interpreter result or variables are
 *   accessed.
 */
static void AssocDataCleanup(void *assocData, Tcl_Interp *interp) {
    Tcl_DeleteHashTable((Tcl_HashTable *)assocData);
    ckfree(assocData);
}

/**
 * @brief Associate a borrowed payload with a Tk window in its interpreter.
 *
 * Lazily allocate a one-word-key table as Tcl associated data. Replacing a value
 * does not free the old payload. The caller must remove the entry before the
 * window/payload becomes invalid; interpreter cleanup frees only the table.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 * @param[in] instanceData Borrowed payload stored without a reference or destructor; replacement does not
 *   release the previous payload.
 */
void Rbc_SetWindowInstanceData(Tk_Window tkwin, ClientData instanceData) {
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

/**
 * @brief Look up a borrowed per-window payload.
 *
 * Return NULL for a missing entry or an absent interpreter table. Lookup does
 * not allocate a table and does not retain the payload or Tk window. Require
 * a live Tk_Window even when no association exists.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 */
ClientData Rbc_GetWindowInstanceData(Tk_Window tkwin) {
    Tcl_Interp *interp = Tk_Interp(tkwin);
    Tcl_HashTable *assocData = Tcl_GetAssocData(interp, INST_DATA_KEY, NULL);
    Tcl_HashEntry *entryPtr;

    if (assocData == NULL) {
        return NULL;
    }
    entryPtr = Tcl_FindHashEntry(assocData, tkwin);
    if (!entryPtr)
        return NULL;
    return Tcl_GetHashValue(entryPtr);
}

/**
 * @brief Remove a per-window association without freeing its payload.
 *
 * Require a live window. An absent table or missing entry is a no-op and does
 * not allocate storage. The window owner is responsible for removing its
 * association during teardown.
 *
 * @param[in] tkwin Borrowed live Tk window on its owning thread, supplying the interpreter/display and native
 *   resource context.
 */
void Rbc_DeleteWindowInstanceData(Tk_Window tkwin) {
    Tcl_Interp *interp = Tk_Interp(tkwin);
    Tcl_HashTable *assocData = Tcl_GetAssocData(interp, INST_DATA_KEY, NULL);
    Tcl_HashEntry *entryPtr;

    if (assocData == NULL) {
        return;
    }
    entryPtr = Tcl_FindHashEntry(assocData, tkwin);
    if (entryPtr) {
        Tcl_DeleteHashEntry(entryPtr);
    }
}
