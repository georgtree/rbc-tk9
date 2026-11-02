/* Private EPS/window/color resource tests; no installed API or production command. */
#include "rbcInt.h"
#include "rbcImage.h"

/* Include the production serializer so capture failure can be injected. */
#ifndef WIN32
static int failPsCapture;
static XImage *TestPsGetImage(Display *display, Drawable drawable, int x, int y,
                             unsigned int width, unsigned int height, unsigned long mask, int format) {
    if (failPsCapture) return NULL;
    return XGetImage(display, drawable, x, y, width, height, mask, format);
}
#define XGetImage TestPsGetImage
#endif
#define ReverseBits PsTestReverseBits
#include "../generic/rbcPs.c"
#undef ReverseBits
#ifndef WIN32
#undef XGetImage
#endif

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

static int PsRasterTestCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    PsToken token;
    const char *op;
    (void)data;
    if (objc != 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "operation");
        return TCL_ERROR;
    }
    op = Tcl_GetString(objv[1]);
    token = Rbc_GetPsToken(interp, Tk_MainWindow(interp));
    Rbc_AppendToPostScript(token, "% sentinel\n", (char *)NULL);
    if (strcmp(op, "wide") == 0) {
        /* Zero rows isolate row-size formatting without a huge allocation. */
        struct ColorImage image;
        image.width = 1073741824;
        image.height = 0;
        image.bits = NULL;
        Rbc_ColorImageToPostScript(token, &image, 0, 0);
    } else if (strcmp(op, "rgb") == 0 || strcmp(op, "grey") == 0) {
        Rbc_ColorImage image = Rbc_CreateColorImage(2, 1);
        Pix32 *pixels = Rbc_ColorImageBits(image);
        pixels[0].rgba.red = 255;
        pixels[0].rgba.green = pixels[0].rgba.blue = 0;
        pixels[1].rgba.red = pixels[1].rgba.green = pixels[1].rgba.blue = 255;
        token->colorMode = strcmp(op, "rgb") == 0 ? PS_MODE_COLOR : PS_MODE_GREYSCALE;
        Rbc_ColorImageToPostScript(token, image, 0, 0);
        Rbc_FreeColorImage(image);
#ifndef WIN32
    } else if (strcmp(op, "failure") == 0) {
        failPsCapture = 1;
        Rbc_BitmapDataToPostScript(token, Tk_Display(Tk_MainWindow(interp)), None, 9, 2);
        failPsCapture = 0;
#endif
    } else if (strcmp(op, "bitmap") == 0) {
        static const char bits[] = {0x01, 0x01, 0x02, 0x00};
        Tk_Window window = Tk_MainWindow(interp);
        Pixmap bitmap = XCreateBitmapFromData(Tk_Display(window),
            RootWindowOfScreen(Tk_Screen(window)), bits, 9, 2);
        if (bitmap == None) {
            Rbc_ReleasePsToken(token);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("could not create test bitmap", -1));
            return TCL_ERROR;
        }
        Rbc_BitmapDataToPostScript(token, Tk_Display(window), bitmap, 9, 2);
        Tk_FreePixmap(Tk_Display(window), bitmap);
    } else {
        Rbc_ReleasePsToken(token);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("unknown raster test operation", -1));
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(Rbc_PostScriptFromToken(token), -1));
    Rbc_ReleasePsToken(token);
    return TCL_OK;
}

static int PsTextTestCmd(ClientData data, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    PsToken token;
    TextStyle style;
    Tk_Window window = Tk_MainWindow(interp);
    double angle;
    Tk_Anchor anchor;
    (void)data;
    if (objc == 2) {
        TextLayout layout;
        int value;
        if (Tcl_GetIntFromObj(interp, objv[1], &value) != TCL_OK) return TCL_ERROR;
        memset(&layout, 0, sizeof(layout));
        layout.nFrags = 1;
        layout.fragArr[0].text = "A";
        layout.fragArr[0].count = 1;
        layout.fragArr[0].width = 7;
        layout.fragArr[0].x = layout.fragArr[0].y = value;
        token = Rbc_GetPsToken(interp, window);
        TextLayoutToPostScript(token, value, value, &layout);
        Tcl_SetObjResult(interp, Tcl_NewStringObj(Rbc_PostScriptFromToken(token), -1));
        Rbc_ReleasePsToken(token);
        return TCL_OK;
    }
    if (objc != 3) {
        Tcl_WrongNumArgs(interp, 1, objv, "offset | angle anchor");
        return TCL_ERROR;
    }
    if (Tcl_GetDoubleFromObj(interp, objv[1], &angle) != TCL_OK ||
        Tk_GetAnchorFromObj(interp, objv[2], &anchor) != TCL_OK) return TCL_ERROR;
    memset(&style, 0, sizeof(style));
    Rbc_InitTextStyle(&style);
    style.font = Tk_GetFont(interp, window, "Helvetica 12");
    if (style.font == NULL) return TCL_ERROR;
    /* The wrappers retain Tk stubs access after the color macros are undefined. */
    style.color = TestGetColor(interp, window, "black");
    if (style.color == NULL) {
        Tk_FreeFont(style.font);
        return TCL_ERROR;
    }
    style.theta = angle;
    style.anchor = anchor;
    token = Rbc_GetPsToken(interp, window);
    Rbc_TextToPostScript(token, "Wide text label", &style, 100, 200);
    Tcl_SetObjResult(interp, Tcl_NewStringObj(Rbc_PostScriptFromToken(token), -1));
    Rbc_ReleasePsToken(token);
    TestFreeColor(style.color);
    Tk_FreeFont(style.font);
    if (style.theta != angle) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("style angle was modified", -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}

DLLEXPORT int Rbcresourcetest_Init(Tcl_Interp *interp) {
    if (Tcl_InitStubs(interp, "9.0", 0) == NULL || Tk_InitStubs(interp, "9.0", 0) == NULL) {
        return TCL_ERROR;
    }
    Rbc_InitEpsCanvasItem(interp);
    Tcl_CreateObjCommand2(interp, "::rbcpstexttest", PsTextTestCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::rbcpsrastertest", PsRasterTestCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::rbcpslinetest", PsLineTestCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::rbccolorpairtest", ColorPairTestCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "::rbcresourcetest", ResourceTestCmd, NULL, NULL);
    return Tcl_PkgProvide(interp, "rbcresourcetest", "1.0");
}
