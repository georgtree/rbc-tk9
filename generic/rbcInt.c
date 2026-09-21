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

#ifdef WIN32
/*
 *----------------------------------------------------------------------
 *
 * DllMain --
 *
 *      This wrapper function is used by Windows to invoke the
 *      initialization code for the DLL.
 *
 * Parameters:
 *      HINSTANCE hInst - Library instance handle.
 *      DWORD reason - Reason this function is being called.
 *      LPVOID reserved - Not used.
 *
 * Results:
 *      Returns TRUE;
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
BOOL APIENTRY DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) { return TRUE; }

/*
 *----------------------------------------------------------------------
 *
 * DllEntryPoint --
 *
 *      TODO: Description
 *
 * Parameters:
 *      HINSTANCE hInst - Library instance handle.
 *      DWORD reason - Reason this function is being called.
 *      LPVOID reserved - Not used.
 *
 * Results:
 *      Returns the result from DllMain.
 *
 * Side effects:
 *      None.
 *
 *----------------------------------------------------------------------
 */
BOOL APIENTRY DllEntryPoint(HINSTANCE hInst, DWORD reason, LPVOID reserved) { return DllMain(hInst, reason, reserved); }
#endif /* WIN32 */

/* Initialization state belongs to the interpreter, not to the shared library. */
typedef struct {
    int vectorReady;
    int graphReady;
    int winopReady;
    int splineReady;
    int epsReady;
} RbcInitData;

static void FreeInitData(ClientData clientData, Tcl_Interp *interp) {
    (void)interp;
    ckfree(clientData);
}

static int InitTkObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    RbcInitData *dataPtr = Tcl_GetAssocData(interp, "rbc::init", NULL);
    Tcl_Namespace *nsPtr;
    const char **cmd;
    static const char *exports[] = {"barchart", "graph", "polar", "spline", "stripchart", "winop", NULL};

    (void)clientData;
    if (objc != 1) {
        Tcl_WrongNumArgs(interp, 1, objv, NULL);
        return TCL_ERROR;
    }
    if (Tk_InitStubs(interp, TK_VERSION, 0) == NULL) {
        return TCL_ERROR;
    }
    if (!dataPtr->graphReady) {
        if (Rbc_GraphInit(interp) != TCL_OK) {
            return TCL_ERROR;
        }
        dataPtr->graphReady = TRUE;
    }
    if (!dataPtr->winopReady) {
        if (Rbc_WinopInit(interp) != TCL_OK) {
            return TCL_ERROR;
        }
        dataPtr->winopReady = TRUE;
    }
    if (!dataPtr->splineReady) {
        if (Rbc_SplineInit(interp) != TCL_OK) {
            return TCL_ERROR;
        }
        dataPtr->splineReady = TRUE;
    }
    if (!dataPtr->epsReady) {
        Rbc_InitEpsCanvasItem(interp);
        dataPtr->epsReady = TRUE;
    }
    nsPtr = Tcl_FindNamespace(interp, "::rbc", NULL, TCL_GLOBAL_ONLY);
    for (cmd = exports; *cmd != NULL; cmd++) {
        if (Tcl_Export(interp, nsPtr, *cmd, 0) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    return Tcl_PkgProvideEx(interp, PACKAGE_NAME, PACKAGE_VERSION, (ClientData)&rbcStubs);
}

/* Tcl-only entry point. Both package loaders share this one vector state. */
int DLLEXPORT Rbc_Init(Tcl_Interp *interp) {
    RbcInitData *dataPtr;
    Tcl_Namespace *nsPtr;

    if (Tcl_InitStubs(interp, TCL_VERSION, 0) == NULL) {
        return TCL_ERROR;
    }
    dataPtr = Tcl_GetAssocData(interp, "rbc::init", NULL);
    if (dataPtr == NULL) {
        dataPtr = (RbcInitData *)Tcl_Alloc(sizeof(*dataPtr));
        memset(dataPtr, 0, sizeof(*dataPtr));
        Tcl_SetAssocData(interp, "rbc::init", FreeInitData, dataPtr);
    }
    nsPtr = Tcl_FindNamespace(interp, "::rbc", NULL, TCL_GLOBAL_ONLY);
    if (nsPtr == NULL) {
        nsPtr = Tcl_CreateNamespace(interp, "::rbc", NULL, NULL);
        if (nsPtr == NULL) {
            return TCL_ERROR;
        }
    }
    if (!dataPtr->vectorReady) {
        if (Rbc_VectorInit(interp) != TCL_OK) {
            return TCL_ERROR;
        }
        Tcl_CreateObjCommand2(interp, "::rbc::_initTk", InitTkObjCmd, NULL, NULL);
        dataPtr->vectorReady = TRUE;
    }
    if (Tcl_Export(interp, nsPtr, "vector", 0) != TCL_OK) {
        return TCL_ERROR;
    }
    return Tcl_PkgProvideEx(interp, "rbc::vector", PACKAGE_VERSION, (ClientData)&rbcStubs);
}
