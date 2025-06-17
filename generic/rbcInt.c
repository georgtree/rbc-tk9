/*
 * rbcInt.c --
 *
 *      This file constructs the basic functionality of the
 *      rbc commands.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcInt.h"

extern RbcStubs rbcStubs;

#ifndef RBC_LIBRARY
#ifdef WIN32
#define RBC_LIBRARY  "c:/Program Files/Tcl/lib/rbc"
#else
#define RBC_LIBRARY "unknown"
#endif
#endif

#ifdef WIN32
/*
 *----------------------------------------------------------------------
 *
 * DllMain --
 *
 *      This wrapper function is used by Windows to invoke the
 *      initialization code for the DLL.
 *
 * Results:
 *      Returns TRUE;
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
BOOL APIENTRY
DllMain(
    HINSTANCE hInst,          /* Library instance handle. */
    DWORD reason,            /* Reason this function is being called. */
    LPVOID reserved)        /* Not used. */
{
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * DllEntryPoint --
 *
 *      TODO: Description
 *
 * Results:
 *      Returns the result from DllMain.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
BOOL APIENTRY
DllEntryPoint(hInst, reason, reserved)
    HINSTANCE hInst;            /* Library instance handle. */
    DWORD reason;               /* Reason this function is being called. */
    LPVOID reserved;            /* Not used. */
{
    return DllMain(hInst, reason, reserved);
}
#endif /* WIN32 */

/*
 * -----------------------------------------------------------------------
 *
 * Rbc_Init --
 *
 *      This procedure is invoked to initialize the "rbc" commands.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Creates the new commands and adds a new entry into a global Tcl
 *      associative array.
 *
 * ------------------------------------------------------------------------
 */
int DLLEXPORT
Rbc_Init (interp)
    Tcl_Interp *interp; /* Base interpreter to return results to. */
{
    Tcl_Namespace *nsPtr;
    const char **cmd;

   if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
    return TCL_ERROR;
    }

    if (Tk_InitStubs(interp, TK_VERSION, 0) == NULL) {
    return TCL_ERROR;
    }

    static const char *ExportList[] = {
    "barchart", "graph", "spline", "stripchart", "vector",
    "winop", NULL
    };

    nsPtr = Tcl_CreateNamespace(interp, "rbc", NULL, NULL);
    if (nsPtr == NULL) {
        return TCL_ERROR;
    }

    for ( cmd = ExportList; *cmd != NULL ; cmd++ ) {
    if (Tcl_Export(interp, nsPtr, *cmd, 0) != TCL_OK) {
        return TCL_ERROR;
    }
    }

    Rbc_VectorInit(interp);
    Rbc_GraphInit(interp);
    Rbc_WinopInit(interp);
    Rbc_InitEpsCanvasItem(interp);
    Rbc_SplineInit(interp);

    Tcl_PkgProvideEx(interp, PACKAGE_NAME, PACKAGE_VERSION,
             (ClientData) &rbcStubs);

    return TCL_OK;
}
