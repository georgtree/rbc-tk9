/*
 * rbcWinop.c --
 *
 *      This module implements simple window commands for the rbc toolkit.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
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

typedef struct {
    Rbc_OpSpecHeader header;
    RbcWinOp *proc;
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

/*
 *--------------------------------------------------------------
 *
 * GetRealizedWindow --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      const char *string
 *      Tk_Window *tkwinPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * StringToWindow --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      const char *string
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * GetWindowSize --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Window window
 *      int *widthPtr
 *      int *heightPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * XGeometryErrorProc --
 *
 *      Flags errors generated from XGetGeometry calls to the X server.
 *
 * Parameters:
 *      ClientData clientData
 *      XErrorEvent *errEventPtr
 *
 * Results:
 *      Always returns 0.
 *
 * Side Effects:
 *      Sets a flag, indicating an error occurred.
 *
 *----------------------------------------------------------------------
 */
static int XGeometryErrorProc(ClientData clientData, XErrorEvent *errEventPtr) {
    int *errorPtr = clientData;

    *errorPtr = TCL_ERROR;
    return 0;
}

/*
 *--------------------------------------------------------------
 *
 * GetWindowSize --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Window window
 *      int *widthPtr
 *      int *heightPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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
/*
 *--------------------------------------------------------------
 *
 * ColormapOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * LowerOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * RaiseOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * MapOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * MoveOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * UnmapOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * ChangesOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * QueryOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * WarpToOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * ConvolveOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * QuantizeOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * ReadJPEGOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * GradientOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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
                destPtr->Red = (unsigned char)(left[0] + t * range[0]);
                destPtr->Green = (unsigned char)(left[1] + t * range[1]);
                destPtr->Blue = (unsigned char)(left[2] + t * range[2]);
                destPtr->Alpha = (unsigned char)-1;
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
                destPtr->Red = (unsigned char)(left[0] + t * range[0]);
                destPtr->Green = (unsigned char)(left[1] + t * range[1]);
                destPtr->Blue = (unsigned char)(left[2] + t * range[2]);
                destPtr->Alpha = (unsigned char)-1;
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
                destPtr->Red = (unsigned char)(left[0] + t * range[0]);
                destPtr->Green = (unsigned char)(left[1] + t * range[1]);
                destPtr->Blue = (unsigned char)(left[2] + t * range[2]);
                destPtr->Alpha = (unsigned char)-1;
                destPtr++;
            }
        }
    }; break;
        //    } else if (strcmp(argv[5], "blank") == 0) {
    case GT_BLANK: {
        register int x, y;

        for (y = 0; y < src.height; y++) {
            for (x = 0; x < src.width; x++) {
                destPtr->Red = (unsigned char)0xFF;
                destPtr->Green = (unsigned char)0xFF;
                destPtr->Blue = (unsigned char)0xFF;
                destPtr->Alpha = (unsigned char)-1;
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

/*
 *--------------------------------------------------------------
 *
 * ResampleOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * RotateOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int RotateOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_PhotoHandle srcPhoto, destPhoto;
    Rbc_ColorImage srcImage, destImage;
    double theta;
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
    if (Tcl_ExprDoubleObj(interp, objv[4], &theta) != TCL_OK) {
        return TCL_ERROR;
    }
    if (!FINITE(theta)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("rotation angle must be finite", -1));
        return TCL_ERROR;
    }
    srcImage = Rbc_PhotoToColorImage(srcPhoto);
    destImage = Rbc_RotateColorImage(srcImage, theta);

    Rbc_ColorImageToPhoto(interp, destImage, destPhoto);
    Rbc_FreeColorImage(srcImage);
    Rbc_FreeColorImage(destImage);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * SnapOp --
 *
 *      Snaps a picture of a window and stores it in a
 *      designated photo image.  The window must be completely
 *      visible or the snap will fail.
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result. The interpreter result contains
 *      the list of the graph coordinates. If an error occurred
 *      while parsing the window positions, TCL_ERROR is
 *      returned, then the interpreter result will contain an error
 *      message.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int SnapOp(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tk_Window tkwin;
    int width, height, destWidth, destHeight;
    Window window;
    const char *wname = Tcl_GetString(objv[2]);

    tkwin = Tk_MainWindow(interp);
    window = StringToWindow(interp, wname);
    if (window == None) {
        return TCL_ERROR;
    }
    if (GetWindowSize(interp, window, &width, &height) != TCL_OK) {
        Rbc_AppendResultStrings(interp, "can't get window geometry of \"", wname, "\"", (char *)NULL);
        return TCL_ERROR;
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

/*
 *--------------------------------------------------------------
 *
 * SubsampleOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * ImageOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
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

/*
 *--------------------------------------------------------------
 *
 * WinopCmd --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int WinopCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    (void)clientData;
    if (Rbc_GetOpIndexFromObj(interp, winOps, (Tcl_Size)sizeof(winOps[0]), RBC_OP_ARG1, objc, objv, &index) != TCL_OK) {
        return TCL_ERROR;
    }
    return winOps[index].proc(Tk_MainWindow(interp), interp, objc, objv);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_WinopInit --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int Rbc_WinopInit(Tcl_Interp *interp) {
    Tcl_CreateObjCommand2(interp, "rbc::winop", WinopCmd, NULL, NULL);
    return TCL_OK;
}

#endif /* NO_WINOP */
