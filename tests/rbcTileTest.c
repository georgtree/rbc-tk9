/* Private tile harness: compile the implementation here to inspect lifecycle
 * state and count actual mask releases, without exporting test-only APIs. */
#include "rbcInt.h"
#include "rbcImage.h"
static Pixmap watchedMask;
static int maskFrees;
static void TestFreePixmap(Display *display, Pixmap pixmap) {
    if (pixmap == watchedMask && watchedMask != None) maskFrees++;
    Tk_FreePixmap(display, pixmap);
}
#ifndef WIN32
static int TestXFreePixmap(Display *display, Pixmap pixmap) {
    if (pixmap == watchedMask && watchedMask != None) maskFrees++;
    return XFreePixmap(display, pixmap);
}
#define XFreePixmap TestXFreePixmap
#endif
#undef Tk_FreePixmap
#define Tk_FreePixmap TestFreePixmap
#include "../generic/rbcTile.c"

typedef struct TileTest TileTest;
typedef struct { TileTest *test; int index; } TestClient;
struct TileTest {
    Tcl_Interp *interp;
    Rbc_Tile clients[3];
    TestClient data[3];
    Tcl_Obj *log;
    const char *action;
    int acted;
};
static void Changed(ClientData data, Rbc_Tile tile);
static void Add(TileTest *test, int i) {
    test->data[i].test = test;
    test->data[i].index = i;
    if (Rbc_GetTile(test->interp, Tk_MainWindow(test->interp), "tilePhoto", &test->clients[i]) != TCL_OK)
        Tcl_Panic("tile test acquisition failed");
    Rbc_SetTileChangedProc(test->clients[i], Changed, &test->data[i]);
}
static void Remove(TileTest *test, int i) {
    Rbc_FreeTile(test->clients[i]);
    test->clients[i] = NULL;
}
static void Changed(ClientData data, Rbc_Tile tile) {
    TestClient *client = data;
    TileTest *test = client->test;
    int i;
    Tcl_ListObjAppendElement(NULL, test->log, Tcl_NewIntObj(client->index));
    if (client->index != 0 || test->acted) return;
    test->acted = 1;
    if (strcmp(test->action, "self") == 0) Remove(test, 0);
    else if (strcmp(test->action, "other") == 0) Remove(test, 1);
    else if (strcmp(test->action, "all") == 0) {
        for (i = 0; i < 3; i++) Remove(test, i);
    } else if (strcmp(test->action, "add") == 0) Add(test, 2);
    else if (strcmp(test->action, "disable") == 0) Rbc_SetTileChangedProc(test->clients[1], NULL, NULL);
    else if (strcmp(test->action, "nested") == 0) {
        Tcl_EvalEx(test->interp, "tilePhoto put blue -to 0 0 1 1", -1, TCL_EVAL_GLOBAL);
        while (Tcl_DoOneEvent(TCL_IDLE_EVENTS | TCL_DONT_WAIT)) {}
    } else if (strcmp(test->action, "delete-interp") == 0) {
        Tcl_DeleteInterp(test->interp);
        for (i = 0; i < 3; i++) test->clients[i] = NULL;
    }
}
static int TileTestCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    TileTest test;
    Tcl_Interp *child;
    int i, result = TCL_ERROR;
    (void)data;
    if (objc != 2) { Tcl_WrongNumArgs(interp, 1, objv, "scenario"); return TCL_ERROR; }
    memset(&test, 0, sizeof(test));
    child = Tcl_CreateInterp();
    /* Tk_Init is not in the stubs table. Load Tk through its package loader. */
    if (Tcl_Init(child) != TCL_OK ||
        Tcl_EvalEx(child, "package require Tk", -1, TCL_EVAL_GLOBAL) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_GetObjResult(child));
        Tcl_DeleteInterp(child);
        return TCL_ERROR;
    }
    Tcl_Preserve(child);
    test.interp = child;
    test.action = Tcl_GetString(objv[1]);
    test.log = Tcl_NewListObj(0, NULL);
    Tcl_IncrRefCount(test.log);
    if (Tcl_EvalEx(child, "wm withdraw .; image create photo tilePhoto -width 2 -height 2; tilePhoto put red -to 0 0 1 1", -1, TCL_EVAL_GLOBAL) != TCL_OK) goto done;
    Add(&test, 0);
    Add(&test, 1);
    watchedMask = test.clients[0]->tilePtr->mask;
    maskFrees = 0;
    if (strcmp(test.action, "mask") == 0 || strcmp(test.action, "pending") == 0) {
        int hadMask = watchedMask != None;
        if (strcmp(test.action, "pending") == 0)
            Tcl_EvalEx(child, "tilePhoto put blue -to 0 0 1 1", -1, TCL_EVAL_GLOBAL);
        Remove(&test, 0);
        Tcl_ListObjAppendElement(NULL, test.log, Tcl_NewIntObj(maskFrees));
        Remove(&test, 1);
        while (Tcl_DoOneEvent(TCL_IDLE_EVENTS | TCL_DONT_WAIT)) {}
        Tcl_ListObjAppendElement(NULL, test.log, Tcl_NewIntObj(maskFrees));
        Tcl_ListObjAppendElement(NULL, test.log, Tcl_NewIntObj(hadMask));
    } else {
        Tcl_EvalEx(child, "tilePhoto put blue -to 0 0 1 1", -1, TCL_EVAL_GLOBAL);
        while (Tcl_DoOneEvent(TCL_IDLE_EVENTS | TCL_DONT_WAIT)) {}
        if (strcmp(test.action, "add") == 0) {
            Tcl_EvalEx(child, "tilePhoto put red -to 0 0 1 1", -1, TCL_EVAL_GLOBAL);
            while (Tcl_DoOneEvent(TCL_IDLE_EVENTS | TCL_DONT_WAIT)) {}
        }
    }
    Tcl_SetObjResult(interp, test.log);
    result = TCL_OK;
done:
    watchedMask = None;
    for (i = 0; i < 3; i++) Remove(&test, i);
    if (!Tcl_InterpDeleted(child)) Tcl_DeleteInterp(child);
    Tcl_Release(child);
    Tcl_DecrRefCount(test.log);
    return result;
}
DLLEXPORT int Rbctiletest_Init(Tcl_Interp *interp) {
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL || Tk_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;
    Tcl_CreateObjCommand2(interp, "::rbctiletest", TileTestCmd, NULL, NULL);
    return Tcl_PkgProvide(interp, "rbctiletest", "1.0");
}
