/**
 * @file rbcWinop.c
 * @brief Standalone window commands and photo-operation dispatch; not graph rendering.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @defgroup winop_commands Window and photo command internals
 * Private command/canvas callbacks, not installed C APIs.
 */

#include "rbcInt.h"

#ifndef NO_WINOP

#include "rbcImage.h"
#include <X11/Xutil.h>
#ifndef WIN32
#include <X11/Xproto.h>
#endif
#ifdef WIN32
#include <tkPlatDecls.h>
#endif

static Tcl_ObjCmdProc2 WinopCmd;

static int GetRealizedWindow(Tcl_Interp *interp, const char *string, Tk_Window *tkwinPtr);
static Window StringToWindow(Tcl_Interp *interp, const char *string);

typedef int RbcWinOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

/** @brief Typed command dispatch entry with shared argument validation metadata. */
typedef struct {
    Rbc_OpSpecHeader header; /**< Operation name, total argument bounds and usage suffix. */
    RbcWinOp *proc; /**< Typed command handler for this operation. */
} WinopOpSpec;
#ifndef WIN32
static RbcWinOp ColormapOp;
#endif
static RbcWinOp LowerOp;
static RbcWinOp RaiseOp;
static RbcWinOp MapOp;
static RbcWinOp MoveOp;
static RbcWinOp UnmapOp;
static RbcWinOp ChangesOp;
static RbcWinOp QueryOp;
static RbcWinOp WarpToOp;
static RbcWinOp ConvolveOp;
static RbcWinOp QuantizeOp;
static RbcWinOp ReadJPEGOp;
static RbcWinOp GradientOp;
static RbcWinOp ResampleOp;
static RbcWinOp RotateOp;
static RbcWinOp SnapOp;
static RbcWinOp SubsampleOp;
static RbcWinOp ImageOp;

/**
 * @ingroup winop_commands
 * @brief Resolve a Tk path and ensure its native window exists.
 *
 * Borrow the interpreter and return a borrowed Tk_Window through tkwinPtr on
 * success. Realization does not map the window. Return TCL_ERROR from path
 * lookup without assigning the output.
 *
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] string Borrowed NUL-terminated Tk window pathname.
 * @param[out] tkwinPtr Required output receiving a borrowed realized Tk window only on success.
 */
static int GetRealizedWindow(Tcl_Interp *interp, const char *string, Tk_Window *tkwinPtr) {
    Tk_Window tkwin;

    tkwin = Tk_NameToWindow(interp, string, Tk_MainWindow(interp));
    if (tkwin == NULL) {
        return TCL_ERROR;
    }
    if (Tk_WindowId(tkwin) == None) {
        Tk_MakeWindowExist(tkwin);
    }
    *tkwinPtr = tkwin;
    return TCL_OK;
}

/**
 * @ingroup winop_commands
 * @brief Resolve a Tk path or legacy integer native-window identifier.
 *
 * Tk toplevel paths select their wrapper through Rbc_GetRealWindowId(). Numeric
 * input uses Tcl_GetInt, not a pointer-width parser; Windows wraps that value
 * with Rbc_WinWindowFromHWND(). Return None on lookup or conversion failure.
 * The result does not transfer ownership of the window.
 *
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] string Borrowed NUL-terminated Tk path or native ID text, as described above.
 */
static Window StringToWindow(Tcl_Interp *interp, const char *string) {
    int xid;

    if (string[0] == '.') {
        Tk_Window tkwin;

        if (GetRealizedWindow(interp, string, &tkwin) != TCL_OK) {
            return None;
        }
        if (Tk_IsTopLevel(tkwin)) {
            return Rbc_GetRealWindowId(tkwin);
        } else {
            return Tk_WindowId(tkwin);
        }
    } else if (Tcl_GetInt(interp, string, &xid) == TCL_OK) {
#ifdef WIN32
        return Rbc_WinWindowFromHWND((HWND)INT2PTR(xid));
#else
        return (Window)xid;
#endif
    }
    return None;
}

#ifdef WIN32

/**
 * @ingroup winop_commands
 * @brief Read the outer Windows rectangle of a Tk drawable.
 *
 * Borrow window and resolve its HWND with Tk_GetHWND. Write both dimensions
 * only after GetWindowRect succeeds. Return TCL_ERROR without adding a
 * diagnostic for an invalid handle or failed query; interp is unused. This
 * measures the native outer rectangle, not the Tk client dimensions.
 *
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] window Borrowed Tk-compatible native window identifier.
 * @param[out] widthPtr Required output receiving native window width only on success.
 * @param[out] heightPtr Required output receiving native window height only on success.
 */
static int GetWindowSize(Tcl_Interp *interp, Window window, int *widthPtr, int *heightPtr) {
    HWND hWnd;
    RECT region;

    (void)interp;
    hWnd = Tk_GetHWND(window);
    if (hWnd == NULL) {
        return TCL_ERROR;
    }
    if (!GetWindowRect(hWnd, &region)) {
        return TCL_ERROR;
    }
    *widthPtr = region.right - region.left;
    *heightPtr = region.bottom - region.top;
    return TCL_OK;
}

#else

/**
 * @ingroup winop_commands
 * @brief Record an XGetGeometry protocol error.
 *
 * clientData points to the caller-owned integer flag while the temporary
 * Tk error handler is installed. Set it to TCL_ERROR and return zero to
 * consume the error. Do not retain the flag or event pointer.
 *
 * @param[in,out] clientData Borrowed pointer to the stack-backed integer error flag, set to TCL_ERROR.
 * @param[in] errEventPtr Borrowed X protocol-error event; not retained.
 */
static int XGeometryErrorProc(ClientData clientData, XErrorEvent *errEventPtr) {
    int *errorPtr = clientData;

    *errorPtr = TCL_ERROR;
    return 0;
}

/**
 * @ingroup winop_commands
 * @brief Query X11 drawable dimensions with a scoped protocol-error handler.
 *
 * Borrow the interpreter and its main-window display. Synchronize the X
 * connection before removing the temporary X_GetGeometry error handler, so
 * its stack-backed error flag remains valid. Return TCL_ERROR on query or
 * protocol failure, or dimensions exceeding INT_MAX; only the overflow case
 * sets a diagnostic here. Publish both output dimensions only on success.
 *
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] window Borrowed Tk-compatible native window identifier.
 * @param[out] widthPtr Required output receiving native window width only on success.
 * @param[out] heightPtr Required output receiving native window height only on success.
 */
static int GetWindowSize(Tcl_Interp *interp, Window window, int *widthPtr, int *heightPtr) {
    Tk_Window tkwin;
    Display *display;
    Tk_ErrorHandler handler;
    Window root;
    Status status;
    int x, y;
    int xError;
    unsigned int width;
    unsigned int height;
    unsigned int borderWidth;
    unsigned int depth;

    tkwin = Tk_MainWindow(interp);
    display = Tk_Display(tkwin);
    xError = TCL_OK;
    handler = Tk_CreateErrorHandler(display, -1, X_GetGeometry, -1, XGeometryErrorProc, &xError);
    status = XGetGeometry(display, window, &root, &x, &y, &width, &height, &borderWidth, &depth);
    /*
     * Force any protocol error generated by XGetGeometry to be
     * delivered while our temporary error handler is installed.
     */
    XSync(display, False);
    Tk_DeleteErrorHandler(handler);
    if ((!status) || (xError != TCL_OK)) {
        return TCL_ERROR;
    }
    if ((width > (unsigned int)INT_MAX) || (height > (unsigned int)INT_MAX)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("window dimensions are too large", -1));
        return TCL_ERROR;
    }
    *widthPtr = (int)width;
    *heightPtr = (int)height;
    return TCL_OK;
}
#endif /*WIN32*/

#ifndef WIN32
/**
 * @ingroup winop_commands
 * @brief Inspect allocated cells in the legacy X11 colormap range.
 *
 * Resolve and realize the Tk path in objv[2]. Probe up to 256 writable
 * cells, release the allocated cells, then query pixel IDs 0 through 255.
 * Return alternating RGB strings and pixel IDs for cells not obtained by
 * the probe. This legacy algorithm assumes an indexed colormap with
 * allocated pixel IDs below 256; it is not a general visual enumerator.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int ColormapOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    register int i;
    Tk_Window tkwin;
#define MAXCOLORS 256
    register XColor *colorPtr;
    XColor colorArr[MAXCOLORS];
    unsigned long int pixelValues[MAXCOLORS];
    int inUse[MAXCOLORS];
    unsigned long int *indexPtr;
    int nFree;
    Tcl_Obj *resultObj;

    if (GetRealizedWindow(interp, Tcl_GetString(objv[2]), &tkwin) != TCL_OK) {
        return TCL_ERROR;
    }
    /* Initially, we assume all color cells are allocated. */
    memset((char *)inUse, 0, sizeof(int) * MAXCOLORS);
    /*
     * Start allocating color cells.  This will tell us which color cells
     * haven't already been allocated in the colormap.  We'll release the
     * cells as soon as we find out how many there are.
     */
    nFree = 0;
    for (indexPtr = pixelValues, i = 0; i < MAXCOLORS; i++, indexPtr++) {
        if (!XAllocColorCells(Tk_Display(tkwin), Tk_Colormap(tkwin), False, NULL, 0, indexPtr, 1)) {
            break;
        }
        inUse[*indexPtr] = TRUE; /* Indicate the cell is unallocated */
        nFree++;
    }
    XFreeColors(Tk_Display(tkwin), Tk_Colormap(tkwin), pixelValues, nFree, 0);
    for (colorPtr = colorArr, i = 0; i < MAXCOLORS; i++, colorPtr++) {
        colorPtr->pixel = i;
    }
    XQueryColors(Tk_Display(tkwin), Tk_Colormap(tkwin), colorArr, MAXCOLORS);
    resultObj = Tcl_NewListObj(0, NULL);
    for (colorPtr = colorArr, i = 0; i < MAXCOLORS; i++, colorPtr++) {
        if (!inUse[colorPtr->pixel]) {
            Tcl_ListObjAppendElement(
                NULL, resultObj,
                Tcl_ObjPrintf("#%02x%02x%02x", (colorPtr->red >> 8), (colorPtr->green >> 8), (colorPtr->blue >> 8)));
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewWideIntObj(colorPtr->pixel));
        }
    }
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}
#endif

/**
 * @ingroup winop_commands
 * @brief Lower each named Tk window in argument order.
 *
 * Resolve objv[2..objc-1] relative to the interpreter main window and
 * request Below stacking with no sibling. Return TCL_ERROR on a failed
 * lookup, retaining any earlier restacking. The Tk_RestackWindow return
 * status is not propagated. An empty window list succeeds.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int LowerOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;
    Tk_Window tkwin;

    for (i = 2; i < objc; i++) {
        tkwin = Tk_NameToWindow(interp, Tcl_GetString(objv[i]), Tk_MainWindow(interp));
        if (NULL == tkwin) {
            return TCL_ERROR;
        }
        Tk_RestackWindow(tkwin, Below, NULL);
    }
    return TCL_OK;
}

/**
 * @ingroup winop_commands
 * @brief Raise each named Tk window in argument order.
 *
 * Resolve objv[2..objc-1] relative to the interpreter main window and
 * request Above stacking with no sibling. Return TCL_ERROR on a failed
 * lookup, retaining any earlier restacking. The Tk_RestackWindow return
 * status is not propagated. An empty window list succeeds.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int RaiseOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;
    Tk_Window tkwin;

    for (i = 2; i < objc; i++) {
        tkwin = Tk_NameToWindow(interp, Tcl_GetString(objv[i]), Tk_MainWindow(interp));
        if (NULL == tkwin) {
            return TCL_ERROR;
        }
        Tk_RestackWindow(tkwin, Above, NULL);
    }
    return TCL_OK;
}

/**
 * @ingroup winop_commands
 * @brief Request native mapping for each path or legacy integer ID.
 *
 * For Tk paths, realize the window, set TK_MAPPED through Tk_FakeWin,
 * and use its client window ID. Numeric arguments are parsed with
 * Tcl_GetInt and cast directly to Window, without StringToWindow. Issue
 * XMapWindow requests in argument order; later conversion errors do not
 * undo earlier requests. This path does not call Tk_MapWindow.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int MapOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;
    Window window;
    Display *display;

    display = Tk_Display(Tk_MainWindow(interp));
    for (i = 2; i < objc; i++) {
        const char *arg = Tcl_GetString(objv[i]);
        if (arg[0] == '.') {
            Tk_Window tkwin;
            Tk_FakeWin *fakePtr;

            if (GetRealizedWindow(interp, arg, &tkwin) != TCL_OK) {
                return TCL_ERROR;
            }
            fakePtr = (Tk_FakeWin *)tkwin;
            fakePtr->flags |= TK_MAPPED;
            window = Tk_WindowId(tkwin);
        } else {
            int xid;

            if (Tcl_GetInt(interp, arg, &xid) != TCL_OK) {
                return TCL_ERROR;
            }
            window = (Window)xid;
        }
        /*TODO: use Tk_MapWindow instead */
        XMapWindow(display, window);
    }
    return TCL_OK;
}

/**
 * @ingroup winop_commands
 * @brief Move a named Tk window using screen-distance arguments.
 *
 * Borrow the window resolved from objv[2]. Convert objv[3] and objv[4]
 * with Tk_GetPixelsFromObj before moving; append an axis-specific diagnostic
 * on conversion failure. Tk_MoveWindow uses parent-relative pixel
 * coordinates, not graph coordinates or an explicit toplevel wrapper.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int MoveOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int x, y;
    Tk_Window tkwin;
    //    Window window;
    //    Display *display;

    tkwin = Tk_NameToWindow(interp, Tcl_GetString(objv[2]), Tk_MainWindow(interp));
    //    display = Tk_Display(tkwin);
    //    window = StringToWindow(interp, Tcl_GetString(objv[2]));
    if (NULL == tkwin) {
        return TCL_ERROR;
    }
    if (Tk_GetPixelsFromObj(interp, tkwin, objv[3], &x) != TCL_OK) {
        Rbc_AppendResultStrings(interp, ": bad window x-coordinate", (char *)NULL);
        return TCL_ERROR;
    }
    if (Tk_GetPixelsFromObj(interp, tkwin, objv[4], &y) != TCL_OK) {
        Rbc_AppendResultStrings(interp, ": bad window y-coordinate", (char *)NULL);
        return TCL_ERROR;
    }
    //    XMoveWindow(display, window, x, y);
    Tk_MoveWindow(tkwin, x, y);
    return TCL_OK;
}

/**
 * @ingroup winop_commands
 * @brief Request native unmapping for each path or legacy integer ID.
 *
 * For Tk paths, realize the window, clear TK_MAPPED through Tk_FakeWin,
 * and use its client window ID. Numeric arguments are parsed with
 * Tcl_GetInt and cast directly to Window, without StringToWindow. Issue
 * XUnmapWindow requests in argument order; later conversion errors do not
 * undo earlier requests. This path does not call Tk_UnmapWindow.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int UnmapOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;
    Window window;
    Display *display;

    display = Tk_Display(Tk_MainWindow(interp));
    for (i = 2; i < objc; i++) {
        const char *arg = Tcl_GetString(objv[i]);
        if (arg[0] == '.') {
            Tk_Window tkwin;
            Tk_FakeWin *fakePtr;

            if (GetRealizedWindow(interp, arg, &tkwin) != TCL_OK) {
                return TCL_ERROR;
            }
            fakePtr = (Tk_FakeWin *)tkwin;
            fakePtr->flags &= ~TK_MAPPED;
            window = Tk_WindowId(tkwin);
        } else {
            int xid;

            if (Tcl_GetInt(interp, arg, &xid) != TCL_OK) {
                return TCL_ERROR;
            }
            window = (Window)xid;
        }
        /* TODO: use Tk_UnmapWindow instead */
        //        XMapWindow(display, window);
        XUnmapWindow(display, window);
    }
    return TCL_OK;
}

/**
 * @ingroup winop_commands
 * @brief Request backing-store and save-under attributes for a toplevel.
 *
 * Resolve and realize objv[2]. For a toplevel, obtain its real native
 * window ID and issue XChangeWindowAttributes with WhenMapped backing
 * store and save_under enabled. A child window is a successful no-op.
 * TCL_OK reports request submission, not server acceptance of the hints.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int ChangesOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_Window tkwin;

    if (GetRealizedWindow(interp, Tcl_GetString(objv[2]), &tkwin) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tk_IsTopLevel(tkwin)) {
        XSetWindowAttributes attrs;
        Window window;
        unsigned int mask;

        window = Rbc_GetRealWindowId(tkwin);
        attrs.backing_store = WhenMapped;
        attrs.save_under = True;
        mask = CWBackingStore | CWSaveUnder;
        XChangeWindowAttributes(Tk_Display(tkwin), window, mask, &attrs);
    }
    return TCL_OK;
}

/**
 * @ingroup winop_commands
 * @brief Return the pointer position in root coordinates when available.
 *
 * clientData is the borrowed Tk main window. Query its display and window
 * ID; on success set the Tcl result to \@x,y using root coordinates. A false
 * XQueryPointer result leaves the interpreter result unchanged and still
 * returns TCL_OK. objc and objv are unused.
 *
 * @param[in] clientData Borrowed Tk main window supplied by dispatch.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Unused query argument; direct internal calls may supply zero/NULL.
 * @param[in] objv Unused query argument; direct internal calls may supply zero/NULL.
 */
static int QueryOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int rootX, rootY, childX, childY;
    Window root, child;
    unsigned int mask;
    Tk_Window tkwin = (Tk_Window)clientData;

    /* GetCursorPos */
    if (XQueryPointer(Tk_Display(tkwin), Tk_WindowId(tkwin), &root, &child, &rootX, &rootY, &childX, &childY, &mask)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("@%d,%d", rootX, rootY));
    }
    return TCL_OK;
}

/**
 * @ingroup winop_commands
 * @brief Optionally warp the pointer, then query its root position.
 *
 * With no target, delegate directly to QueryOp. An \@x,y target is parsed
 * by Rbc_GetXY and uses the main window screen root; otherwise resolve and
 * realize a Tk path, reject an unmapped window, and target its integer
 * center. Native warping can generate pointer events; no Tcl event loop
 * is run here. Return parsing/lookup errors or the subsequent query status.
 *
 * @param[in] clientData Borrowed Tk main window supplied by dispatch.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int WarpToOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_Window tkwin, mainWindow;
    mainWindow = (Tk_Window)clientData;

    if (objc > 2) {
        const char *arg = Tcl_GetString(objv[2]);
        if (arg[0] == '@') {
            int x, y;
            Window root;

            if (Rbc_GetXY(interp, mainWindow, arg, &x, &y) != TCL_OK) {
                return TCL_ERROR;
            }
            root = RootWindow(Tk_Display(mainWindow), Tk_ScreenNumber(mainWindow));
            XWarpPointer(Tk_Display(mainWindow), None, root, 0, 0, 0, 0, x, y);
        } else {
            if (GetRealizedWindow(interp, arg, &tkwin) != TCL_OK) {
                return TCL_ERROR;
            }
            if (!Tk_IsMapped(tkwin)) {
                Rbc_AppendResultStrings(interp, "can't warp to unmapped window \"", Tk_PathName(tkwin), "\"", (char *)NULL);
                return TCL_ERROR;
            }
            XWarpPointer(Tk_Display(tkwin), None, Tk_WindowId(tkwin), 0, 0, 0, 0, Tk_Width(tkwin) / 2,
                         Tk_Height(tkwin) / 2);
        }
    }
    return QueryOp(clientData, interp, 0, NULL);
}

/*
 * This is a temporary home for these image routines.  They will be
 * moved when a new image type is created for them.
 */

/**
 * @ingroup winop_commands
 * @brief Convolve an existing photo into an existing destination photo.
 *
 * Require a nonempty square list of finite kernel values with a finite sum.
 * Own and free the kernel and copied color buffers locally; photo handles are
 * borrowed. The source is copied before destination publication. Parse failures
 * return TCL_ERROR. Photo publication uses the void color-image helper and is
 * not an atomic transaction with rollback.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int ConvolveOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_PhotoHandle srcPhoto, destPhoto;
    Rbc_ColorImage srcImage, destImage;
    Filter2D filter;
    Tcl_Size nValues;
    Tcl_Obj **valueArr;
    double *kernel;
    double value, sum;
    Tcl_Size i;
    int dim;
    
    int result = TCL_ERROR;
    const char *srcName = Tcl_GetString(objv[2]);
    const char *destName = Tcl_GetString(objv[3]);
    srcPhoto = Tk_FindPhoto(interp, srcName);
    if (srcPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "source image \"", srcName, "\" doesn't", " exist or is not a photo image",
                         (char *)NULL);
        return TCL_ERROR;
    }
    destPhoto = Tk_FindPhoto(interp, destName);
    if (destPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "destination image \"", destName, "\" doesn't", " exist or is not a photo image",
                         (char *)NULL);
        return TCL_ERROR;
    }
    if (Tcl_ListObjGetElements(interp, objv[4], &nValues, &valueArr) != TCL_OK) {
        return TCL_ERROR;
    }
    kernel = NULL;
    if (nValues == 0) {
        Rbc_AppendResultStrings(interp, "empty kernel", (char *)NULL);
        goto error;
    }
    if (nValues > INT_MAX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("convolution kernel is too large", -1));
        goto error;
    }
    dim = (int)sqrt((double)nValues);
    if (((Tcl_Size)dim * (Tcl_Size)dim) != nValues) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("kernel must be square", -1));
        goto error;
    }
    kernel = RbcCalloc((size_t)nValues, sizeof(*kernel));
    sum = 0.0;
    for (i = 0; i < nValues; i++) {
        if (Tcl_GetDoubleFromObj(interp, valueArr[i], &value) != TCL_OK) {
            goto error;
        }
        if (!FINITE(value)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("convolution kernel values must be finite", -1));
            goto error;
        }
        kernel[i] = value;
        sum += value;
    }
    if (!FINITE(sum)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("convolution kernel sum is not finite", -1));
        goto error;
    }
    filter.kernel = kernel;
    filter.support = dim * 0.5;
    filter.sum = (sum == 0.0) ? 1.0 : sum;
    filter.scale = 1.0 / nValues;
    srcImage = Rbc_PhotoToColorImage(srcPhoto);
    destImage = Rbc_ConvolveColorImage(srcImage, &filter);
    Rbc_FreeColorImage(srcImage);
    Rbc_ColorImageToPhoto(interp, destImage, destPhoto);
    Rbc_FreeColorImage(destImage);
    result = TCL_OK;
error:
    if (kernel != NULL) {
        ckfree(kernel);
    }
    return result;
}

/**
 * @ingroup winop_commands
 * @brief Quantize a copied photo with a palette of one to 256 colors.
 *
 * Borrow both photo handles and free both color buffers after the quantizer
 * returns. Resize the destination to the source dimensions before parsing the
 * optional color count; an error therefore need not leave it unchanged.
 * Source dimensions of one pixel or less are rejected.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int QuantizeOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_PhotoHandle srcPhoto, destPhoto;
    Tk_PhotoImageBlock src, dest;
    Rbc_ColorImage srcImage, destImage;
    int nColors;
    int result;
    
    const char *srcName = Tcl_GetString(objv[2]);
    const char *destName = Tcl_GetString(objv[3]);
    srcPhoto = Tk_FindPhoto(interp, srcName);
    if (srcPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "source image \"", srcName, "\" doesn't", " exist or is not a photo image",
                         (char *)NULL);
        return TCL_ERROR;
    }
    Tk_PhotoGetImage(srcPhoto, &src);
    if ((src.width <= 1) || (src.height <= 1)) {
        Rbc_AppendResultStrings(interp, "source image \"", srcName, "\" is empty", (char *)NULL);
        return TCL_ERROR;
    }
    destPhoto = Tk_FindPhoto(interp, destName);
    if (destPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "destination image \"", destName, "\" doesn't", " exist or is not a photo image",
                         (char *)NULL);
        return TCL_ERROR;
    }
    Tk_PhotoGetImage(destPhoto, &dest);
    if ((dest.width != src.width) || (dest.height != src.height)) {
        Tk_PhotoSetSize(interp, destPhoto, src.width, src.height);
    }
    nColors = 1;
    if (objc > 4) {
        if (Tcl_GetIntFromObj(interp, objv[4], &nColors) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if ((nColors < 1) || (nColors > 256)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad number of colors \"%d\": "
                                               "must be between 1 and 256",
                                               nColors));
        return TCL_ERROR;
    }
    srcImage = Rbc_PhotoToColorImage(srcPhoto);
    destImage = Rbc_PhotoToColorImage(destPhoto);
    result = Rbc_QuantizeColorImage(srcImage, destImage, nColors);
    if (result == TCL_OK) {
        Rbc_ColorImageToPhoto(interp, destImage, destPhoto);
    }
    Rbc_FreeColorImage(srcImage);
    Rbc_FreeColorImage(destImage);
    return result;
}

/**
 * @ingroup winop_commands
 * @brief Decode a JPEG file into an existing photo when enabled.
 *
 * The direct and normalized nested dispatch forms place the file name
 * in objv[2] and destination photo name in objv[3]. Borrow the photo handle
 * and return Rbc_JPEGToPhoto status. Without HAVE_JPEGLIB_H, return TCL_ERROR
 * with the unsupported diagnostic without looking up the photo.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int ReadJPEGOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
#if HAVE_JPEGLIB_H
    Tk_PhotoHandle photo; /* The photo image to write into. */
    const char *name = Tcl_GetString(objv[3]);

    photo = Tk_FindPhoto(interp, name);
    if (photo == NULL) {
        Rbc_AppendResultStrings(interp, "image \"", name, "\" doesn't", " exist or is not a photo image", (char *)NULL);
        return TCL_ERROR;
    }
    return Rbc_JPEGToPhoto(interp, Tcl_GetString(objv[2]), photo);
#else
    Rbc_AppendResultStrings(interp, "JPEG support not compiled", (char *)NULL);
    return TCL_ERROR;
#endif
}

/**
 * @ingroup winop_commands
 * @brief Fill an existing photo with a legacy procedural gradient.
 *
 * Borrow the destination photo and use its current dimensions. Acquire
 * both Tk colors, create temporary color pixels, and select the type from
 * objv[5]. Release both color references and pixel storage on completion
 * or an invalid type. Linear, radial and rectangular modes consume random
 * values; blank writes opaque white but still validates both colors.
 * Publication uses the void Rbc_ColorImageToPhoto helper, so TCL_OK does
 * not propagate lower-level photo-write status.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int GradientOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_PhotoHandle photo;
    Tk_PhotoImageBlock src;
    XColor *leftPtr, *rightPtr;
    Tk_Window tkwin;
    double range[3];
    double left[3];
    Pix32 *destPtr;
    Rbc_ColorImage destImage = NULL;
    int result = TCL_ERROR;
    const char *name = Tcl_GetString(objv[2]);

    static const char *gradientTypes[] = {"linear", "radial", "rectangular", "blank", NULL};
    enum gradienType { GT_LINEAR, GT_RADIAL, GT_RECTANGULAR, GT_BLANK };
    int index;

    tkwin = Tk_MainWindow(interp);
    photo = Tk_FindPhoto(interp, name);
    if (photo == NULL) {
        Rbc_AppendResultStrings(interp, "source image \"", name, "\" doesn't", " exist or is not a photo image", (char *)NULL);
        return TCL_ERROR;
    }
    Tk_PhotoGetImage(photo, &src);
    leftPtr = Tk_AllocColorFromObj(interp, tkwin, objv[3]);
    if (leftPtr == NULL) {
        return TCL_ERROR;
    }
    rightPtr = Tk_AllocColorFromObj(interp, tkwin, objv[4]);
    if (rightPtr == NULL) {
        Tk_FreeColorFromObj(tkwin, objv[3]);
        return TCL_ERROR;
    }
    left[0] = (double)(leftPtr->red >> 8);
    left[1] = (double)(leftPtr->green >> 8);
    left[2] = (double)(leftPtr->blue >> 8);
    range[0] = (double)((rightPtr->red - leftPtr->red) / 257.0);
    range[1] = (double)((rightPtr->green - leftPtr->green) / 257.0);
    range[2] = (double)((rightPtr->blue - leftPtr->blue) / 257.0);
    destImage = Rbc_CreateColorImage(src.width, src.height);
    destPtr = Rbc_ColorImageBits(destImage);
    if (Tcl_GetIndexFromObj(interp, objv[5], gradientTypes, "gradient type", 0, &index) != TCL_OK) {
        goto done;
    }
#define CLAMP(c) ((((c) < 0.0) ? 0.0 : ((c) > 1.0) ? 1.0 : (c)))
    //    if (strcmp(argv[5], "linear") == 0) {
    switch (index) {
    case GT_LINEAR: {
        register int x, y;
        double t;

        for (y = 0; y < src.height; y++) {
            for (x = 0; x < src.width; x++) {
                t = (double)x * (Rbc_RandomDouble() * 0.10 - 0.05);
                t = CLAMP(t);
                destPtr->rgba.red = (unsigned char)(left[0] + t * range[0]);
                destPtr->rgba.green = (unsigned char)(left[1] + t * range[1]);
                destPtr->rgba.blue = (unsigned char)(left[2] + t * range[2]);
                destPtr->rgba.alpha = (unsigned char)-1;
                destPtr++;
            }
        }
    } break;
        //    } else if (strcmp(argv[5], "radial") == 0) {
    case GT_RADIAL: {
        register int x, y;
        register double dx, dy;
        double dy2;
        double t;
        double midX, midY;

        midX = midY = 0.5;
        for (y = 0; y < src.height; y++) {
            dy = (y / (double)src.height) - midY;
            dy2 = dy * dy;
            for (x = 0; x < src.width; x++) {
                dx = (x / (double)src.width) - midX;
                t = 1.0 - (double)sqrt(dx * dx + dy2);
                t += t * (Rbc_RandomDouble() * 0.10 - 0.05);
                t = CLAMP(t);
                destPtr->rgba.red = (unsigned char)(left[0] + t * range[0]);
                destPtr->rgba.green = (unsigned char)(left[1] + t * range[1]);
                destPtr->rgba.blue = (unsigned char)(left[2] + t * range[2]);
                destPtr->rgba.alpha = (unsigned char)-1;
                destPtr++;
            }
        }
    } break;
        //    } else if (strcmp(argv[5], "rectangular") == 0) {
    case GT_RECTANGULAR: {
        register int x, y;
        register double dx, dy;
        double t, px, py;
        double midX, midY;
        double cosTheta, sinTheta;
        double angle;

        angle = M_PI_2 * -0.3;
        cosTheta = cos(angle);
        sinTheta = sin(angle);
        midX = 0.5, midY = 0.5;
        for (y = 0; y < src.height; y++) {
            dy = (y / (double)src.height) - midY;
            for (x = 0; x < src.width; x++) {
                dx = (x / (double)src.width) - midX;
                px = dx * cosTheta - dy * sinTheta;
                py = dx * sinTheta + dy * cosTheta;
                t = FABS(px) + FABS(py);
                t += t * (Rbc_RandomDouble() * 0.10 - 0.05);
                t = CLAMP(t);
                destPtr->rgba.red = (unsigned char)(left[0] + t * range[0]);
                destPtr->rgba.green = (unsigned char)(left[1] + t * range[1]);
                destPtr->rgba.blue = (unsigned char)(left[2] + t * range[2]);
                destPtr->rgba.alpha = (unsigned char)-1;
                destPtr++;
            }
        }
    }; break;
        //    } else if (strcmp(argv[5], "blank") == 0) {
    case GT_BLANK: {
        register int x, y;

        for (y = 0; y < src.height; y++) {
            for (x = 0; x < src.width; x++) {
                destPtr->rgba.red = (unsigned char)0xFF;
                destPtr->rgba.green = (unsigned char)0xFF;
                destPtr->rgba.blue = (unsigned char)0xFF;
                destPtr->rgba.alpha = (unsigned char)-1;
                destPtr++;
            }
        }
    } break;
    }
    Rbc_ColorImageToPhoto(interp, destImage, photo);
    result = TCL_OK;
    
done:
    if (destImage != NULL) {
        Rbc_FreeColorImage(destImage);
    }
    Tk_FreeColorFromObj(tkwin, objv[3]);
    Tk_FreeColorFromObj(tkwin, objv[4]);
    return result;
}

/**
 * @ingroup winop_commands
 * @brief Resize a photo according to the destination photo dimensions.
 *
 * Borrow existing photos and filter descriptors. A destination dimension of
 * one pixel or less causes a source-size copy; equal dimensions also copy.
 * The default filter is none. A vertical filter is parsed only when the first
 * filter resolves to a non-NULL descriptor. Lower photo-write return values
 * are not uniformly propagated by this legacy command.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int ResampleOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_PhotoHandle srcPhoto, destPhoto;
    Tk_PhotoImageBlock src, dest;
    ResampleFilter *filterPtr, *vertFilterPtr, *horzFilterPtr;
    Tcl_Obj *filterObj;
    const char *srcName = Tcl_GetString(objv[2]);
    const char *destName = Tcl_GetString(objv[3]);

    srcPhoto = Tk_FindPhoto(interp, srcName);
    if (srcPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "source image \"", srcName, "\" doesn't", " exist or is not a photo image",
                         (char *)NULL);
        return TCL_ERROR;
    }
    destPhoto = Tk_FindPhoto(interp, destName);
    if (destPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "destination image \"", destName, "\" doesn't", " exist or is not a photo image",
                         (char *)NULL);
        return TCL_ERROR;
    }
    filterObj = (objc > 4) ? objv[4] : Tcl_NewStringObj("none", -1);
    if (Rbc_GetResampleFilterFromObj(interp, filterObj, &filterPtr) != TCL_OK) {
        Tcl_BounceRefCount(filterObj);
        return TCL_ERROR;
    }
    Tcl_BounceRefCount(filterObj);
    vertFilterPtr = horzFilterPtr = filterPtr;
    if ((filterPtr != NULL) && (objc > 5)) {
        if (Rbc_GetResampleFilterFromObj(interp, objv[5], &filterPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        vertFilterPtr = filterPtr;
    }
    Tk_PhotoGetImage(srcPhoto, &src);
    if ((src.width <= 1) || (src.height <= 1)) {
        Rbc_AppendResultStrings(interp, "source image \"", srcName, "\" is empty", (char *)NULL);
        return TCL_ERROR;
    }
    Tk_PhotoGetImage(destPhoto, &dest);
    if ((dest.width <= 1) || (dest.height <= 1)) {
        Tk_PhotoSetSize(interp, destPhoto, src.width, src.height);
        goto copyImage;
    }
    if ((src.width == dest.width) && (src.height == dest.height)) {
    copyImage:
        /* Source and destination image sizes are the same. Don't
         * resample. Simply make copy of image */
        dest.width = src.width;
        dest.height = src.height;
        dest.pixelPtr = src.pixelPtr;
        dest.pixelSize = src.pixelSize;
        dest.pitch = src.pitch;
        dest.offset[0] = src.offset[0];
        dest.offset[1] = src.offset[1];
        dest.offset[2] = src.offset[2];
        Tk_PhotoPutBlock(interp, destPhoto, &dest, 0, 0, dest.width, dest.height, TK_PHOTO_COMPOSITE_SET);
        return TCL_OK;
    }
    if (filterPtr == NULL) {
        Rbc_ResizePhoto(interp, srcPhoto, 0, 0, src.width, src.height, destPhoto);
    } else {
        Rbc_ResamplePhoto(interp, srcPhoto, 0, 0, src.width, src.height, destPhoto, horzFilterPtr, vertFilterPtr);
    }
    return TCL_OK;
}

/**
 * @ingroup winop_commands
 * @brief Rotate a copied photo by a finite Tcl expression angle in degrees.
 *
 * Validate names, preserve the interpreter across angle evaluation, then
 * reacquire both photo handles before accessing pixels. Reject deleted photos
 * or destroyed Tk state; same-name replacements are used. Allocate and release
 * source/rotated buffers locally. Photo publication is not a rollback transaction.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int RotateOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_PhotoHandle srcPhoto, destPhoto;
    Rbc_ColorImage srcImage, destImage;
    double theta;
    int result;
    const char *srcName = Tcl_GetString(objv[2]);
    const char *destName = Tcl_GetString(objv[3]);

    srcPhoto = Tk_FindPhoto(interp, srcName);
    if (srcPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "image \"", srcName, "\" doesn't", " exist or is not a photo image", (char *)NULL);
        return TCL_ERROR;
    }
    destPhoto = Tk_FindPhoto(interp, destName);
    if (destPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "destination image \"", destName, "\" doesn't", " exist or is not a photo image",
                         (char *)NULL);
        return TCL_ERROR;
    }
    /* The angle may execute Tcl that deletes/replaces either photo, destroys
     * Tk, or deletes the interpreter. Do not keep handles across evaluation. */
    Tcl_Preserve(interp);
    result = Tcl_ExprDoubleObj(interp, objv[4], &theta);
    if (result == TCL_OK && (Tcl_InterpDeleted(interp) || Tk_MainWindow(interp) == NULL)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Tk was destroyed while evaluating rotation angle", -1));
        result = TCL_ERROR;
    }
    Tcl_Release(interp);
    if (result != TCL_OK) {
        return result;
    }
    if (!FINITE(theta)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("rotation angle must be finite", -1));
        return TCL_ERROR;
    }
    /* Fetch strings again too: callbacks may have changed object representations. */
    srcName = Tcl_GetString(objv[2]);
    destName = Tcl_GetString(objv[3]);
    srcPhoto = Tk_FindPhoto(interp, srcName);
    if (srcPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "image \"", srcName, "\" doesn't exist or is not a photo image", (char *)NULL);
        return TCL_ERROR;
    }
    destPhoto = Tk_FindPhoto(interp, destName);
    if (destPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "destination image \"", destName,
                               "\" doesn't exist or is not a photo image", (char *)NULL);
        return TCL_ERROR;
    }
    srcImage = Rbc_PhotoToColorImage(srcPhoto);
    destImage = Rbc_RotateColorImage(srcImage, theta);
    Rbc_ColorImageToPhoto(interp, destImage, destPhoto);
    Rbc_FreeColorImage(srcImage);
    Rbc_FreeColorImage(destImage);
    return TCL_OK;
}

/**
 * @ingroup winop_commands
 * @brief Capture a native window into a named photo, optionally resizing it.
 *
 * For a Tk path, capture the client drawable and its Tk dimensions rather than
 * the window-manager wrapper. Numeric input uses StringToWindow() and native
 * geometry. Borrow all windows and delegate capture and destination lookup to
 * Rbc_SnapPhoto(); return its Tcl status. This is screen capture, not graph
 * remapping or vector-document export.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int SnapOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_Window tkwin;
    int width, height, destWidth, destHeight;
    Window window;
    const char *wname = Tcl_GetString(objv[2]);

    tkwin = Tk_MainWindow(interp);
    if (wname[0] == '.') {
        if (GetRealizedWindow(interp, wname, &tkwin) != TCL_OK) {
            return TCL_ERROR;
        }
        /*
         * Capture the Tk client window, not its window-manager wrapper.
         * On Windows Rbc_GetRealWindowId returns a raw HWND, which must
         * not be passed to Tk drawable functions.
         */
        window = Tk_WindowId(tkwin);
        width = Tk_Width(tkwin);
        height = Tk_Height(tkwin);
    } else {
        window = StringToWindow(interp, wname);
        if (window == None) {
            return TCL_ERROR;
        }
        if (GetWindowSize(interp, window, &width, &height) != TCL_OK) {
            Rbc_AppendResultStrings(interp, "can't get window geometry of \"", wname, "\"", (char *)NULL);
            return TCL_ERROR;
        }
    }
    destWidth = width, destHeight = height;
    if ((objc > 4) && (Rbc_GetPixelsFromObj(interp, tkwin, objv[4], PIXELS_POSITIVE, &destWidth) != TCL_OK)) {
        return TCL_ERROR;
    }
    if ((objc > 5) && (Rbc_GetPixelsFromObj(interp, tkwin, objv[5], PIXELS_POSITIVE, &destHeight) != TCL_OK)) {
        return TCL_ERROR;
    }
    return Rbc_SnapPhoto(interp, tkwin, window, 0, 0, width, height, destWidth, destHeight, Tcl_GetString(objv[3]),
                         GAMMA);
}

/**
 * @ingroup winop_commands
 * @brief Resize a validated source-photo rectangle into a destination photo.
 *
 * Require nonnegative origin, positive extent and a rectangle inside the source.
 * An effectively empty destination takes the region dimensions. Default to the
 * box filter; none selects the unfiltered resize path. Photos and filters are
 * borrowed, and the command neither creates nor deletes the destination photo.
 *
 * @param[in] clientData Borrowed Tk main window supplied by dispatch.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int SubsampleOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_Window tkwin;
    Tk_PhotoHandle srcPhoto, destPhoto;
    Tk_PhotoImageBlock src, dest;
    ResampleFilter *filterPtr, *vertFilterPtr, *horzFilterPtr;
    Tcl_Obj *filterObj;
    int flag;
    int x, y;
    int width, height;
    
    const char *srcName = Tcl_GetString(objv[2]);
    const char *destName = Tcl_GetString(objv[3]);
    srcPhoto = Tk_FindPhoto(interp, srcName);
    if (srcPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "source image \"", srcName, "\" doesn't", " exist or is not a photo image",
                         (char *)NULL);
        return TCL_ERROR;
    }
    destPhoto = Tk_FindPhoto(interp, destName);
    if (destPhoto == NULL) {
        Rbc_AppendResultStrings(interp, "destination image \"", destName, "\" doesn't", " exist or is not a photo image",
                         (char *)NULL);
        return TCL_ERROR;
    }
    tkwin = (Tk_Window)clientData;
    flag = PIXELS_NONNEGATIVE;
    if (Rbc_GetPixelsFromObj(interp, tkwin, objv[4], flag, &x) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Rbc_GetPixelsFromObj(interp, tkwin, objv[5], flag, &y) != TCL_OK) {
        return TCL_ERROR;
    }
    flag = PIXELS_POSITIVE;
    if (Rbc_GetPixelsFromObj(interp, tkwin, objv[6], flag, &width) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Rbc_GetPixelsFromObj(interp, tkwin, objv[7], flag, &height) != TCL_OK) {
        return TCL_ERROR;
    }
    filterObj = (objc > 8) ? objv[8] : Tcl_NewStringObj("box", -1);
    if (Rbc_GetResampleFilterFromObj(interp, filterObj, &filterPtr) != TCL_OK) {
        Tcl_BounceRefCount(filterObj);
        return TCL_ERROR;
    }
    Tcl_BounceRefCount(filterObj);
    vertFilterPtr = horzFilterPtr = filterPtr;
    if ((filterPtr != NULL) && (objc > 9)) {
        if (Rbc_GetResampleFilterFromObj(interp, objv[9], &filterPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        vertFilterPtr = filterPtr;
    }
    Tk_PhotoGetImage(srcPhoto, &src);
    Tk_PhotoGetImage(destPhoto, &dest);
    if ((src.width <= 1) || (src.height <= 1)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("source image \"%s\" is empty", srcName));
        return TCL_ERROR;
    }
    if ((x > src.width) || (y > src.height) || (width > (src.width - x)) || (height > (src.height - y))) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("nonsensical dimensions for subregion: "
                                               "x=%d y=%d width=%d height=%d",
                                               x, y, width, height));
        return TCL_ERROR;
    }
    if ((dest.width <= 1) || (dest.height <= 1)) {
        Tk_PhotoSetSize(interp, destPhoto, width, height);
    }
    if (filterPtr == NULL) {
        Rbc_ResizePhoto(interp, srcPhoto, x, y, width, height, destPhoto);
    } else {
        Rbc_ResamplePhoto(interp, srcPhoto, x, y, width, height, destPhoto, horzFilterPtr, vertFilterPtr);
    }
    return TCL_OK;
}

static const WinopOpSpec imageOps[] = {{{"convolve", 6, 6, "srcPhoto destPhoto filter"}, ConvolveOp},
                                       {{"gradient", 7, 7, "photo left right type"}, GradientOp},
                                       {{"readjpeg", 5, 5, "fileName photoName"}, ReadJPEGOp},
                                       {{"resample", 5, 7, "srcPhoto destPhoto ?horzFilter vertFilter?"}, ResampleOp},
                                       {{"rotate", 6, 6, "srcPhoto destPhoto angle"}, RotateOp},
                                       {{"snap", 5, 7, "window photoName ?width height?"}, SnapOp},
                                       {{"subsample", 9, 11,
                                         "srcPhoto destPhoto x y width height "
                                         "?horzFilter? ?vertFilter?"},
                                        SubsampleOp},
                                       {{NULL, 0, 0, NULL}, NULL}};

/**
 * @ingroup winop_commands
 * @brief Dispatch nested winop image operations using direct-operation indices.
 *
 * Validate against imageOps, then pass objc - 1 and objv + 1 so shared handlers
 * see source and destination at their original argument positions. Tables for
 * direct and nested operations are intentionally not identical.
 *
 * @param[in] clientData Unused dispatch data; nested dispatch obtains the current main window.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int ImageOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;
    (void)clientData;
    if (Rbc_GetOpIndexFromObj(interp, imageOps, (Tcl_Size)sizeof(imageOps[0]), RBC_OP_ARG2, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Remove the "image" word so the selected operation sees the same
     * argument positions as its direct rbc::winop form.
     */
    return imageOps[index].proc(Tk_MainWindow(interp), interp, objc - 1, objv + 1);
}

static const WinopOpSpec winOps[] = {{{"changes", 3, 3, "window"}, ChangesOp},
#ifndef WIN32
                                     {{"colormap", 3, 3, "window"}, ColormapOp},
#endif
                                     {{"convolve", 5, 5, "srcPhoto destPhoto filter"}, ConvolveOp},
                                     {{"image", 2, 0, "args"}, ImageOp},
                                     {{"lower", 2, 0, "window ?window?..."}, LowerOp},
                                     {{"map", 2, 0, "window ?window?..."}, MapOp},
                                     {{"move", 5, 5, "window x y"}, MoveOp},
                                     {{"quantize", 4, 5, "srcPhoto destPhoto ?nColors?"}, QuantizeOp},
                                     {{"query", 2, 2, ""}, QueryOp},
                                     {{"raise", 2, 0, "window ?window?..."}, RaiseOp},
                                     {{"readjpeg", 4, 4, "fileName photoName"}, ReadJPEGOp},
                                     {{"resample", 4, 6, "srcPhoto destPhoto ?horzFilter vertFilter?"}, ResampleOp},
                                     {{"snap", 4, 6, "window photoName ?width height?"}, SnapOp},
                                     {{"subsample", 8, 10,
                                       "srcPhoto destPhoto x y width height "
                                       "?horzFilter? ?vertFilter?"},
                                      SubsampleOp},
                                     {{"unmap", 2, 0, "window ?window?..."}, UnmapOp},
                                     {{"warpto", 2, 3, "?window?"}, WarpToOp},
                                     {{NULL, 0, 0, NULL}, NULL}};

/**
 * @ingroup winop_commands
 * @brief Dispatch a stateless winop request with the current Tk main window.
 *
 * Validate operation and arity through winOps before calling its handler.
 * No per-command heap object or command deletion callback is maintained.
 *
 * @param[in] clientData Dispatch client data; unused by this handler.
 * @param[in,out] interp Borrowed interpreter receiving command results and error diagnostics.
 * @param[in] objc Number of borrowed command arguments, including command and operation names.
 * @param[in] objv Borrowed Tcl argument array; operation-specific positions are described above.
 */
static int WinopCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    (void)clientData;
    if (Rbc_GetOpIndexFromObj(interp, winOps, (Tcl_Size)sizeof(winOps[0]), RBC_OP_ARG1, objc, objv, &index) != TCL_OK) {
        return TCL_ERROR;
    }
    return winOps[index].proc(Tk_MainWindow(interp), interp, objc, objv);
}

/**
 * @ingroup winop_commands
 * @brief Register the rbc::winop Tcl object command.
 *
 * Returns TCL_OK. Registers the static dispatcher without client data or a delete
 * callback; does not create a window.
 * @param[in,out] interp Borrowed interpreter with the RBC namespace and Tk initialized; see @ref
 *   rbc_interp_argument.
 */
int Rbc_WinopInit(Tcl_Interp *interp) {
    Tcl_CreateObjCommand2(interp, "rbc::winop", WinopCmd, NULL, NULL);
    return TCL_OK;
}

#endif /* NO_WINOP */
