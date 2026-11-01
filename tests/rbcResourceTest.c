/* Private EPS/window resource tests; no installed API or production command. */
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

DLLEXPORT int Rbcresourcetest_Init(Tcl_Interp *interp) {
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL || Tk_InitStubs(interp, "9.0", 0) == NULL) {
        return TCL_ERROR;
    }
    Rbc_InitEpsCanvasItem(interp);
    Tcl_CreateObjCommand2(interp, "::rbcresourcetest", ResourceTestCmd, NULL, NULL);
    return Tcl_PkgProvide(interp, "rbcresourcetest", "1.0");
}
