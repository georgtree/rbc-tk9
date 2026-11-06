/**
 * @file rbcInt.c
 * @brief Interpreter-local package initialization and delayed Tk registration.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @defgroup initialization_private Package initialization helpers
 * Private loader callbacks and readiness bookkeeping, not installed interfaces.
 */
/** @defgroup initialization_windows Windows DLL entry wrappers
 * Loader entry wrappers; Tcl package initialization is a separate phase.
 */

#include "rbcInt.h"

extern RbcStubs rbcStubs;

#ifdef WIN32
/**
 * @ingroup initialization_windows
 * @brief Accept Windows DLL loader notifications without package initialization.
 * @param[in] hInst Borrowed module instance handle supplied by the loader; unused.
 * @param[in] reason Loader notification reason; all reasons are accepted.
 * @param[in] reserved Loader-supplied reserved pointer; may be NULL and is never
 *     dereferenced or retained.
 * @return TRUE; package initialization is deferred to Rbc_Init.
 *
 * Return TRUE for every reason. The instance handle, reason and reserved
 * pointer are unused. No allocation, Tcl/Tk initialization or cleanup runs
 * under this loader callback; Rbc_Init performs package initialization later.
 */
BOOL APIENTRY DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) { return TRUE; }

/**
 * @ingroup initialization_windows
 * @brief Forward the alternate Windows entry point to DllMain.
 * @param[in] hInst Borrowed module instance handle supplied by the loader; unused.
 * @param[in] reason Loader notification reason; all reasons are accepted.
 * @param[in] reserved Loader-supplied reserved pointer; may be NULL and is never
 *     dereferenced or retained.
 * @return TRUE; package initialization is deferred to Rbc_Init.
 *
 * Pass all loader arguments through and return DllMain's BOOL result.
 * This wrapper introduces no additional registration or ownership.
 */
BOOL APIENTRY DllEntryPoint(HINSTANCE hInst, DWORD reason, LPVOID reserved) { return DllMain(hInst, reason, reserved); }
#endif /* WIN32 */

/**
 * @ingroup initialization_private
 * @brief Interpreter-owned record of completed registration phases.
 *
 * Stored as rbc::init associated data and freed by FreeInitData. Flags are
 * set only after a phase succeeds and are retained if a later phase fails.
 * They neither own subsystem objects nor detect commands deleted by callers.
 */
typedef struct {
    int vectorReady; /**< Vector commands and private Tk initializer registered. */
    int graphReady; /**< Graph constructors registered; no widget is created. */
    int winopReady; /**< Window/photo command registration completed. */
    int splineReady; /**< Spline command registration completed. */
    int epsReady; /**< EPS canvas item registration invoked. */
} RbcInitData;

/**
 * @ingroup initialization_private
 * @brief Release the interpreter initialization bookkeeping.
 * @param[in] clientData Owned RbcInitData record being released.
 * @param[in] interp Unused here; see @ref rbc_interp_argument.
 *
 * clientData is the allocated RbcInitData owned by Tcl associated data.
 * Free only that record. Subsystem registries, commands, widgets and vectors
 * have separate deletion paths. interp is unused; no Tcl evaluation occurs.
 */
static void FreeInitData(ClientData clientData, Tcl_Interp *interp) {
    (void)interp;
    ckfree(clientData);
}

/**
 * @ingroup initialization_private
 * @brief Complete the Tk-dependent phases after Tcl-only initialization.
 * @param[in] clientData Unused registration data.
 * @param[in] interp See @ref rbc_interp_argument.
 * @param[in] objc See @ref rbc_command_arguments; must equal one.
 * @param[in] objv See @ref rbc_command_arguments.
 * @return Tcl status from registration, export or package provision.
 *
 * Require the rbc::init associated record and namespace established by Rbc_Init.
 * Accept no arguments beyond the command name; initialize Tk stubs, then run
 * graph, winop, spline and EPS registration in order. Completed phase flags
 * survive later errors, so retries skip completed work. Re-export constructor
 * names on every successful traversal and provide the full package with the
 * shared stubs table. Do not source support scripts or create widgets here.
 * Return the first Tcl error or package-provision status. The EPS initializer
 * has no status return. This is a private loader command, not a reinitialization
 * or command-repair API.
 */
static int InitTkObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    RbcInitData *dataPtr = Tcl_GetAssocData(interp, "rbc::init", NULL);
    Tcl_Namespace *nsPtr;
    const char **cmd;
    static const char *exports[] = {"barchart", "graph", "polar", "spline", "stripchart", "trackgraph", "winop", NULL};

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
