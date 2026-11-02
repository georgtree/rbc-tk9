/* Private EPS/window/color resource tests; no installed API or production command. */
#include "rbcInt.h"
#include "rbcImage.h"

static int previewFrees;
static void TestFreeImage(Tk_Image image) {
    previewFrees++;
    Tk_FreeImage(image);
}
#undef Tk_FreeImage
#define Tk_FreeImage TestFreeImage
#include "../generic/rbcCanvEps.c"
#undef Tk_FreeImage

#ifndef WIN32
static int failParentQuery;
static Status TestQueryTree(Display *display, Window window, Window *root, Window *parent,
                            Window **children, unsigned int *count) {
    if (failParentQuery) {
        return 0;
    }
    return XQueryTree(display, window, root, parent, children, count);
}
#define XQueryTree TestQueryTree
#endif
#include "../generic/rbcWindow.c"
#ifndef WIN32
#undef XQueryTree
#endif

/* Count actual Tk references, not just pair contents, including failed parses. */
static int colorGets, colorFrees;
static XColor *TestGetColor(Tcl_Interp *interp, Tk_Window window, Tk_Uid name) {
    XColor *color = Tk_GetColor(interp, window, name);
    if (color != NULL) colorGets++;
    return color;
}
static void TestFreeColor(XColor *color) {
    colorFrees++;
    Tk_FreeColor(color);
}
#undef Tk_GetColor
#undef Tk_FreeColor
#define Tk_GetColor TestGetColor
#define Tk_FreeColor TestFreeColor
#include "../generic/rbcGrMisc.c"
#undef Tk_GetColor
#undef Tk_FreeColor

static int ColorPairTestCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    ColorPair pair;
    int allowDefault, code, unchanged, hasError;
    Tcl_Obj *result;
    (void)data;
    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "foreground background allowDefault");
        return TCL_ERROR;
    }
    if (Tcl_GetBooleanFromObj(interp, objv[3], &allowDefault) != TCL_OK) return TCL_ERROR;
    pair.fgColor = pair.bgColor = COLOR_DEFAULT;
    colorGets = colorFrees = 0;
    code = GetColorPair(interp, Tk_MainWindow(interp), Tcl_GetString(objv[1]),
                       Tcl_GetString(objv[2]), &pair, allowDefault);
    unchanged = pair.fgColor == COLOR_DEFAULT && pair.bgColor == COLOR_DEFAULT;
    hasError = Tcl_GetCharLength(Tcl_GetObjResult(interp)) > 0;
    if (code == TCL_OK) Rbc_FreeColorPair(&pair);
    result = Tcl_NewListObj(0, NULL);
    Tcl_ListObjAppendElement(NULL, result, Tcl_NewIntObj(code));
    Tcl_ListObjAppendElement(NULL, result, Tcl_NewIntObj(colorGets));
    Tcl_ListObjAppendElement(NULL, result, Tcl_NewIntObj(colorFrees));
    Tcl_ListObjAppendElement(NULL, result, Tcl_NewBooleanObj(code == TCL_OK || unchanged));
    Tcl_ListObjAppendElement(NULL, result, Tcl_NewBooleanObj(hasError));
    Tcl_SetObjResult(interp, result);
    return TCL_OK;
}

static int ResourceTestCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    const char *op;
    Tk_Window window = Tk_MainWindow(interp);
    static int first, second;
    int valid;
    (void)data;
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "operation");
        return TCL_ERROR;
    }
    op = Tcl_GetString(objv[1]);
    if (strcmp(op, "reset") == 0) {
        previewFrees = 0;
    } else if (strcmp(op, "frees") == 0) {
        Tcl_SetObjResult(interp, Tcl_NewIntObj(previewFrees));
    } else if (strcmp(op, "missing") == 0) {
        /* This private test interpreter has not installed a window association. */
        valid = Tcl_GetAssocData(interp, INST_DATA_KEY, NULL) == NULL;
        valid = valid && Rbc_GetWindowInstanceData(window) == NULL;
        Rbc_DeleteWindowInstanceData(window);
        valid = valid && Tcl_GetAssocData(interp, INST_DATA_KEY, NULL) == NULL;
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(valid));
    } else if (strcmp(op, "roundtrip") == 0) {
        Rbc_SetWindowInstanceData(window, &first);
        valid = Rbc_GetWindowInstanceData(window) == &first;
        Rbc_SetWindowInstanceData(window, &second);
        valid = valid && Rbc_GetWindowInstanceData(window) == &second;
        Rbc_DeleteWindowInstanceData(window);
        Rbc_DeleteWindowInstanceData(window);
        valid = valid && Rbc_GetWindowInstanceData(window) == NULL;
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(valid));
#ifndef WIN32
    } else if (strcmp(op, "parent") == 0) {
        Tk_MakeWindowExist(window);
        failParentQuery = 1;
        valid = Rbc_GetRealWindowId(window) == Tk_WindowId(window);
        failParentQuery = 0;
        Tcl_SetObjResult(interp, Tcl_NewBooleanObj(valid));
#endif
    } else {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown resource test operation", -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/* Exercise the production serializer without adding an installed command. */
static int PsLineTestCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int count, i;
    XPoint *points = NULL;
    PsToken token;
    (void)data;
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "pointCount");
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[1], &count) != TCL_OK) return TCL_ERROR;
    if (count < -1 || count > 1502) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("pointCount must be between -1 and 1502", -1));
        return TCL_ERROR;
    }
    if (count > 0) {
        /* Exact allocation exposes the one-point over-read to sanitizers. */
        points = (XPoint *)ckalloc((size_t)count * sizeof(XPoint));
        for (i = 0; i < count; i++) {
            points[i].x = (short)i;
            points[i].y = (short)(i + 1);
        }
    }
    token = Rbc_GetPsToken(interp, Tk_MainWindow(interp));
    Rbc_AppendToPostScript(token, "% sentinel\n", (char *)NULL);
    Rbc_LineToPostScript(token, points, count);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(Rbc_PostScriptFromToken(token), -1));
    Rbc_ReleasePsToken(token);
    if (points != NULL) ckfree(points);
    return TCL_OK;
}

DLLEXPORT int Rbcresourcetest_Init(Tcl_Interp *interp) {
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL || Tk_InitStubs(interp, "9.0", 0) == NULL) {
        return TCL_ERROR;
    }
    Rbc_InitEpsCanvasItem(interp);
    Tcl_CreateObjCommand2(interp, "::rbcpslinetest", PsLineTestCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::rbccolorpairtest", ColorPairTestCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::rbcresourcetest", ResourceTestCmd, NULL, NULL);
    return Tcl_PkgProvide(interp, "rbcresourcetest", "1.0");
}
