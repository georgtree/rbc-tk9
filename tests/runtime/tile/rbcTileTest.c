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
#include "rbcTile.c"

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
#ifdef WIN32
/* Exercise the shared Windows blitter with an asymmetric transparent tile.
 * Check the complete destination, including pixels outside the fill bounds. */
static int TilePixelsCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    HDC screen, source, mask, destination;
    HBITMAP sourceBitmap, maskBitmap, destinationBitmap;
    HGDIOBJ oldSource, oldMask, oldDestination;
    Tile tile;
    TileClient client;
    int x, y, ox, oy, transparent, failures = 0;
    (void)data;
    if (objc != 4) {
        Tcl_WrongNumArgs(interp, 1, objv, "xOrigin yOrigin transparent");
        return TCL_ERROR;
    }
    if (Tcl_GetIntFromObj(interp, objv[1], &ox) != TCL_OK ||
        Tcl_GetIntFromObj(interp, objv[2], &oy) != TCL_OK ||
        Tcl_GetBooleanFromObj(interp, objv[3], &transparent) != TCL_OK) return TCL_ERROR;
    memset(&tile, 0, sizeof(tile));
    memset(&client, 0, sizeof(client));
    screen = GetDC(NULL);
    source = CreateCompatibleDC(screen);
    mask = CreateCompatibleDC(screen);
    destination = CreateCompatibleDC(screen);
    sourceBitmap = CreateCompatibleBitmap(screen, 4, 3);
    maskBitmap = CreateBitmap(4, 3, 1, 1, NULL);
    destinationBitmap = CreateCompatibleBitmap(screen, 14, 12);
    ReleaseDC(NULL, screen);
    if (!source || !mask || !destination || !sourceBitmap || !maskBitmap || !destinationBitmap)
        Tcl_Panic("tile pixel test GDI allocation failed");
    oldSource = SelectObject(source, sourceBitmap);
    oldMask = SelectObject(mask, maskBitmap);
    oldDestination = SelectObject(destination, destinationBitmap);
    for (y = 0; y < 3; y++) {
        for (x = 0; x < 4; x++) {
            int opaque = !transparent || (x + 2 * y) % 3 == 0;
            SetPixel(source, x, y, opaque ? RGB(255, 0, 0) : RGB(0, 0, 0));
            SetPixel(mask, x, y, opaque ? RGB(0, 0, 0) : RGB(255, 255, 255));
        }
    }
    for (y = 0; y < 12; y++)
        for (x = 0; x < 14; x++) SetPixel(destination, x, y, RGB(0, 255, 0));
    SetTextColor(destination, RGB(0, 0, 0));
    SetBkColor(destination, RGB(255, 255, 255));
    tile.width = 4;
    tile.height = 3;
    tile.mask = transparent ? (Pixmap)1 : None;
    client.tilePtr = &tile;
    client.xOrigin = ox;
    client.yOrigin = oy;
    TileRegion(source, destination, mask, &client, 2, 2, 9, 7);
    for (y = 0; y < 12; y++) {
        for (x = 0; x < 14; x++) {
            int tx = ((x - ox) % 4 + 4) % 4;
            int ty = ((y - oy) % 3 + 3) % 3;
            int painted = x >= 2 && x < 11 && y >= 2 && y < 9 &&
                (!transparent || (tx + 2 * ty) % 3 == 0);
            COLORREF expected = painted ? RGB(255, 0, 0) : RGB(0, 255, 0);
            if (GetPixel(destination, x, y) != expected) failures++;
        }
    }
    SelectObject(source, oldSource);
    SelectObject(mask, oldMask);
    SelectObject(destination, oldDestination);
    DeleteObject(sourceBitmap);
    DeleteObject(maskBitmap);
    DeleteObject(destinationBitmap);
    DeleteDC(source);
    DeleteDC(mask);
    DeleteDC(destination);
    Tcl_SetObjResult(interp, Tcl_NewIntObj(failures));
    return TCL_OK;
}
#endif
DLLEXPORT int Rbctiletest_Init(Tcl_Interp *interp) {
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL || Tk_InitStubs(interp, "9.0", 0) == NULL) return TCL_ERROR;
    Tcl_CreateObjCommand2(interp, "::rbctiletest", TileTestCmd, NULL, NULL);
#ifdef WIN32
    Tcl_CreateObjCommand2(interp, "::rbctilepixels", TilePixelsCmd, NULL, NULL);
#endif
    return Tcl_PkgProvide(interp, "rbctiletest", "1.0");
}
