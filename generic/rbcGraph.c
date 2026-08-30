/*
 * rbcGraph.c --
 *
 *      This module implements a graph widget for the rbc toolkit.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/*
 * To do:
 *
 * 2) Update manual pages.
 *
 * 3) Update comments.
 *
 * 5) Surface, contour, and flow graphs
 *
 * 7) Arrows for line markers
 *
 */

#include "rbcGraph.h"
#include "rbcBind.h"
#include "rbcGrElem.h"
#include <X11/Xutil.h>
#include <tcl.h>
#ifdef WIN32
#include <tkPlatDecls.h>
#endif

Rbc_Uid rbcXAxisUid;
Rbc_Uid rbcYAxisUid;
Rbc_Uid rbcBarElementUid;
Rbc_Uid rbcLineElementUid;
Rbc_Uid rbcStripElementUid;
Rbc_Uid rbcPolarElementUid;
Rbc_Uid rbcContourElementUid;
Rbc_Uid rbcLineMarkerUid;
Rbc_Uid rbcBitmapMarkerUid;
Rbc_Uid rbcImageMarkerUid;
Rbc_Uid rbcTextMarkerUid;
Rbc_Uid rbcPolygonMarkerUid;
Rbc_Uid rbcWindowMarkerUid;

#define DEF_GRAPH_ASPECT_RATIO "0.0"
#define DEF_GRAPH_BAR_BASELINE "0.0"
#define DEF_GRAPH_BAR_MODE "normal"
#define DEF_GRAPH_BAR_WIDTH "0.8"
#define DEF_GRAPH_BACKGROUND STD_NORMAL_BACKGROUND
#define DEF_GRAPH_BG_MONO STD_NORMAL_BG_MONO
#define DEF_GRAPH_BORDERWIDTH STD_BORDERWIDTH
#define DEF_GRAPH_BUFFER_ELEMENTS "1"
#define DEF_GRAPH_BUFFER_GRAPH "1"
#define DEF_GRAPH_CURSOR "crosshair"
#define DEF_GRAPH_FONT STD_FONT_LARGE
#define DEF_GRAPH_HALO "2m"
#define DEF_GRAPH_HEIGHT "4i"
#define DEF_GRAPH_HIGHLIGHT_BACKGROUND STD_NORMAL_BACKGROUND
#define DEF_GRAPH_HIGHLIGHT_BG_MONO STD_NORMAL_BG_MONO
#define DEF_GRAPH_HIGHLIGHT_COLOR RGB_BLACK
#define DEF_GRAPH_HIGHLIGHT_WIDTH "2"
#define DEF_GRAPH_INVERT_XY "0"
#define DEF_GRAPH_JUSTIFY "center"
#define DEF_GRAPH_MARGIN "0"
#define DEF_GRAPH_MARGIN_VAR (char *)NULL
#define DEF_GRAPH_PLOT_BACKGROUND RGB_WHITE
#define DEF_GRAPH_PLOT_BG_MONO RGB_WHITE
#define DEF_GRAPH_PLOT_BW_COLOR STD_BORDERWIDTH
#define DEF_GRAPH_PLOT_PADX "8"
#define DEF_GRAPH_PLOT_PADY "8"
#define DEF_GRAPH_PLOT_RELIEF "sunken"
#define DEF_GRAPH_RELIEF "flat"
#define DEF_GRAPH_SHADOW_COLOR (char *)NULL
#define DEF_GRAPH_TAKE_FOCUS ""
#define DEF_GRAPH_TITLE (char *)NULL
#define DEF_GRAPH_TITLE_COLOR STD_NORMAL_FOREGROUND
#define DEF_GRAPH_TITLE_MONO STD_NORMAL_FG_MONO
#define DEF_GRAPH_WIDTH "5i"
#define DEF_GRAPH_DATA (char *)NULL
#define DEF_GRAPH_DATA_COMMAND (char *)NULL
#define DEF_GRAPH_RADIAL_LABEL_ANCHOR "se"
#define DEF_GRAPH_ANGLE_LABEL_ANCHOR "center"
#define DEF_GRAPH_REPRESENTATION "polar"
#define DEF_GRAPH_SMITH_GRID "impedance"
#define DEF_GRAPH_ANGLE_MAJOR_TICKS "0 30 60 90 120 150 180 210 240 270 300 330"
#define DEF_GRAPH_ANGLE_MINOR_TICKS "15 45 75 105 135 165 195 225 255 285 315 345"

/*
 * Graph option conversion and update masks.
 *
 * The conversion masks identify values retained as Tcl objects and parsed
 * transactionally after Tk_SetOptions().  The remaining masks describe the
 * derived resources and widget state affected by an option change.
 */
#define GRAPH_BAR_MODE_MASK (1u << 0)
#define GRAPH_BAR_WIDTH_MASK (1u << 1)
#define GRAPH_PIXELS_MASK (1u << 2)
#define GRAPH_PADDING_MASK (1u << 3)
#define GRAPH_SHADOW_MASK (1u << 4)
#define GRAPH_TILE_MASK (1u << 5)
#define GRAPH_TEXT_STYLE_MASK (1u << 6)
#define GRAPH_GC_MASK (1u << 7)
#define GRAPH_GEOMETRY_MASK (1u << 8)
#define GRAPH_INVERT_XY_MASK (1u << 9)
#define GRAPH_LAYOUT_MASK (1u << 10)
#define GRAPH_BACKING_STORE_MASK (1u << 11)
#define GRAPH_REDRAW_MASK (1u << 12)
#define GRAPH_PLOT_BACKGROUND_MASK (1u << 13)
#define GRAPH_POLAR_LABEL_MASK (1u << 14)
#define GRAPH_POLAR_REPRESENTATION_MASK (1u << 15)
#define GRAPH_SMITH_GRID_MASK (1u << 16)
#define GRAPH_POLAR_ANGLE_TICKS_MASK (1u << 17)


#define GRAPH_TRANSACTION_MASK                                                                                         \
    (GRAPH_BAR_MODE_MASK | GRAPH_BAR_WIDTH_MASK | GRAPH_PIXELS_MASK | GRAPH_PADDING_MASK | GRAPH_SHADOW_MASK |         \
     GRAPH_TILE_MASK)

#define GRAPH_INITIALIZE_MASK                                                                                          \
    (GRAPH_TRANSACTION_MASK | GRAPH_TEXT_STYLE_MASK | GRAPH_GC_MASK | GRAPH_GEOMETRY_MASK | GRAPH_INVERT_XY_MASK |     \
     GRAPH_LAYOUT_MASK | GRAPH_BACKING_STORE_MASK | GRAPH_REDRAW_MASK | GRAPH_PLOT_BACKGROUND_MASK |                   \
     GRAPH_POLAR_REPRESENTATION_MASK | GRAPH_SMITH_GRID_MASK | GRAPH_POLAR_ANGLE_TICKS_MASK)

typedef enum {
    GRAPH_BIND_CONTEXT_AXIS = 1,
    GRAPH_BIND_CONTEXT_ELEMENT,
    GRAPH_BIND_CONTEXT_MARKER
} GraphBindContext;

typedef enum {
    GRAPH_PIXEL_OPTION_NONE,
    GRAPH_PIXEL_OPTION_BORDER_WIDTH,
    GRAPH_PIXEL_OPTION_BOTTOM_MARGIN,
    GRAPH_PIXEL_OPTION_HALO,
    GRAPH_PIXEL_OPTION_HEIGHT,
    GRAPH_PIXEL_OPTION_HIGHLIGHT_WIDTH,
    GRAPH_PIXEL_OPTION_LEFT_MARGIN,
    GRAPH_PIXEL_OPTION_PLOT_BORDER_WIDTH,
    GRAPH_PIXEL_OPTION_RIGHT_MARGIN,
    GRAPH_PIXEL_OPTION_TOP_MARGIN,
    GRAPH_PIXEL_OPTION_WIDTH
} GraphPixelOption;

#define GRAPH_PIXEL_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

typedef struct {
    unsigned int stagedMask;

    int borderWidth;
    int bottomMarginSize;
    int halo;
    int reqHeight;
    int highlightWidth;
    int leftMarginSize;
    int plotBorderWidth;
    int rightMarginSize;
    int topMarginSize;
    int reqWidth;
} GraphPixelTransaction;

typedef enum {
    GRAPH_PADDING_OPTION_NONE,
    GRAPH_PADDING_OPTION_X,
    GRAPH_PADDING_OPTION_Y
} GraphPaddingOption;

#define GRAPH_PADDING_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

typedef struct {
    unsigned int stagedMask;

    Rbc_Pad padX;
    Rbc_Pad padY;
} GraphPaddingTransaction;

typedef struct {
    int staged;
    BarMode mode;
} GraphBarModeTransaction;

typedef struct {
    int staged;
    Shadow shadow;
} GraphShadowTransaction;

typedef struct {
    int staged;
    Rbc_Tile tile;
} GraphTileTransaction;

typedef struct {
    int majorStaged;
    double *majorTicks;
    Tcl_Size nMajorTicks;

    int minorStaged;
    double *minorTicks;
    Tcl_Size nMinorTicks;
} GraphPolarAngleTicksTransaction;

static int SetPolarLabelAnchor(void *clientData, Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **valuePtrPtr,
                               char *widgRec, Tcl_Size offset, char *saveInternalPtr, int flags);
static Tcl_Obj *GetPolarLabelAnchor(void *clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset);
static void RestorePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr, char *saveInternalPtr);
static void FreePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr);
static const Tk_ObjCustomOption polarLabelAnchorOption = {
    "polarLabelAnchor", SetPolarLabelAnchor, GetPolarLabelAnchor, RestorePolarLabelAnchor, FreePolarLabelAnchor, NULL};
static const char *const polarRepresentationNames[] = {"polar", "smith", NULL};
static const char *const smithGridNames[] = {"impedance", "admittance", "both", NULL};
/*
 * Modern graph option table.
 */
static const Tk_OptionSpec graphOptionSpecs[] = {
    {TK_OPTION_CUSTOM, "-anglelabelanchor", "angleLabelAnchor", "AngleLabelAnchor", DEF_GRAPH_ANGLE_LABEL_ANCHOR, -1,
     offsetof(Graph, angleLabelAnchor), 0, &polarLabelAnchorOption, GRAPH_POLAR_LABEL_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-anglemajorticks", "angleMajorTicks", "AngleMajorTicks", DEF_GRAPH_ANGLE_MAJOR_TICKS,
     offsetof(Graph, angleMajorTicksObjPtr), -1, 0, NULL, GRAPH_POLAR_ANGLE_TICKS_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-angleminorticks", "angleMinorTicks", "AngleMinorTicks", DEF_GRAPH_ANGLE_MINOR_TICKS,
     offsetof(Graph, angleMinorTicksObjPtr), -1, 0, NULL, GRAPH_POLAR_ANGLE_TICKS_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_DOUBLE, "-aspect", "aspect", "Aspect", DEF_GRAPH_ASPECT_RATIO, -1, offsetof(Graph, aspect), 0, NULL,
     GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_BORDER, "-background", "background", "Background", DEF_GRAPH_BACKGROUND, -1, offsetof(Graph, border), 0,
     DEF_GRAPH_BG_MONO, GRAPH_GC_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-barmode", "barMode", "BarMode", DEF_GRAPH_BAR_MODE, offsetof(Graph, barModeObjPtr), -1, 0,
     NULL, GRAPH_BAR_MODE_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_DOUBLE, "-barwidth", "barWidth", "BarWidth", DEF_GRAPH_BAR_WIDTH, -1, offsetof(Graph, barWidth), 0, NULL,
     GRAPH_BAR_WIDTH_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_DOUBLE, "-baseline", "baseline", "Baseline", DEF_GRAPH_BAR_BASELINE, -1, offsetof(Graph, baseline), 0,
     NULL, GRAPH_REDRAW_MASK},

    {TK_OPTION_SYNONYM, "-bd", NULL, NULL, NULL, -1, -1, 0, "-borderwidth", 0},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL, NULL, -1, -1, 0, "-background", 0},
    {TK_OPTION_SYNONYM, "-bm", NULL, NULL, NULL, -1, -1, 0, "-bottommargin", 0},

    {TK_OPTION_STRING, "-borderwidth", "borderWidth", "BorderWidth", DEF_GRAPH_BORDERWIDTH,
     offsetof(Graph, borderWidthObjPtr), -1, 0, NULL,
     GRAPH_PIXELS_MASK | GRAPH_GEOMETRY_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-bottommargin", "bottomMargin", "Margin", DEF_GRAPH_MARGIN, offsetof(Graph, bottomMarginObjPtr),
     -1, 0, NULL, GRAPH_PIXELS_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-bottomvariable", "bottomVariable", "BottomVariable", DEF_GRAPH_MARGIN_VAR, -1,
     offsetof(Graph, bottomMargin.varName), TK_OPTION_NULL_OK, NULL, GRAPH_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-bufferelements", "bufferElements", "BufferElements", DEF_GRAPH_BUFFER_ELEMENTS, -1,
     offsetof(Graph, backingStore), 0, NULL, GRAPH_BACKING_STORE_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-buffergraph", "bufferGraph", "BufferGraph", DEF_GRAPH_BUFFER_GRAPH, -1,
     offsetof(Graph, doubleBuffer), 0, NULL, GRAPH_REDRAW_MASK},
    {TK_OPTION_CURSOR, "-cursor", "cursor", "Cursor", DEF_GRAPH_CURSOR, -1, offsetof(Graph, cursor), TK_OPTION_NULL_OK,
     NULL, GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-data", "data", "Data", DEF_GRAPH_DATA, -1, offsetof(Graph, data), 0, NULL, GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-datacommand", "dataCommand", "DataCommand", DEF_GRAPH_DATA_COMMAND, -1,
     offsetof(Graph, dataCmd), 0, NULL, GRAPH_REDRAW_MASK},

    {TK_OPTION_SYNONYM, "-fg", NULL, NULL, NULL, -1, -1, 0, "-foreground", 0},

    {TK_OPTION_FONT, "-font", "font", "Font", DEF_GRAPH_FONT, -1, offsetof(Graph, titleTextStyle.font), 0, NULL,
     GRAPH_TEXT_STYLE_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground", DEF_GRAPH_TITLE_COLOR, -1,
     offsetof(Graph, titleTextStyle.color), 0, DEF_GRAPH_TITLE_MONO,
     GRAPH_TEXT_STYLE_MASK | GRAPH_GC_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-halo", "halo", "Halo", DEF_GRAPH_HALO, offsetof(Graph, haloObjPtr), -1, 0, NULL,
     GRAPH_PIXELS_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-height", "height", "Height", DEF_GRAPH_HEIGHT, offsetof(Graph, heightObjPtr), -1, 0, NULL,
     GRAPH_PIXELS_MASK | GRAPH_GEOMETRY_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_COLOR, "-highlightbackground", "highlightBackground", "HighlightBackground",
     DEF_GRAPH_HIGHLIGHT_BACKGROUND, -1, offsetof(Graph, highlightBgColor), 0, DEF_GRAPH_HIGHLIGHT_BG_MONO,
     GRAPH_REDRAW_MASK},
    {TK_OPTION_COLOR, "-highlightcolor", "highlightColor", "HighlightColor", DEF_GRAPH_HIGHLIGHT_COLOR, -1,
     offsetof(Graph, highlightColor), 0, NULL, GRAPH_REDRAW_MASK},
    {TK_OPTION_PIXELS, "-highlightthickness", "highlightThickness", "HighlightThickness", DEF_GRAPH_HIGHLIGHT_WIDTH,
     offsetof(Graph, highlightWidthObjPtr), -1, 0, NULL, GRAPH_PIXELS_MASK | GRAPH_GEOMETRY_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-invertxy", "invertXY", "InvertXY", DEF_GRAPH_INVERT_XY, -1, offsetof(Graph, inverted), 0,
     NULL, GRAPH_INVERT_XY_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_JUSTIFY, "-justify", "justify", "Justify", DEF_GRAPH_JUSTIFY, -1,
     offsetof(Graph, titleTextStyle.justify), 0, NULL, GRAPH_TEXT_STYLE_MASK | GRAPH_REDRAW_MASK},

    {TK_OPTION_STRING, "-leftmargin", "leftMargin", "Margin", DEF_GRAPH_MARGIN, offsetof(Graph, leftMarginObjPtr), -1,
     0, NULL, GRAPH_PIXELS_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-leftvariable", "leftVariable", "LeftVariable", DEF_GRAPH_MARGIN_VAR, -1,
     offsetof(Graph, leftMargin.varName), TK_OPTION_NULL_OK, NULL, GRAPH_REDRAW_MASK},

    {TK_OPTION_SYNONYM, "-lm", NULL, NULL, NULL, -1, -1, 0, "-leftmargin", 0},

    {TK_OPTION_COLOR, "-plotbackground", "plotBackground", "Background", DEF_GRAPH_PLOT_BACKGROUND, -1,
     offsetof(Graph, plotBg), 0, DEF_GRAPH_PLOT_BG_MONO,
     GRAPH_GC_MASK | GRAPH_BACKING_STORE_MASK | GRAPH_PLOT_BACKGROUND_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-plotborderwidth", "plotBorderWidth", "BorderWidth", DEF_GRAPH_PLOT_BW_COLOR,
     offsetof(Graph, plotBorderWidthObjPtr), -1, 0, NULL, GRAPH_PIXELS_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-plotpadx", "plotPadX", "PlotPad", DEF_GRAPH_PLOT_PADX, offsetof(Graph, plotPadXObjPtr), -1, 0,
     NULL, GRAPH_PADDING_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-plotpady", "plotPadY", "PlotPad", DEF_GRAPH_PLOT_PADY, offsetof(Graph, plotPadYObjPtr), -1, 0,
     NULL, GRAPH_PADDING_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_RELIEF, "-plotrelief", "plotRelief", "Relief", DEF_GRAPH_PLOT_RELIEF, -1, offsetof(Graph, plotRelief), 0,
     NULL, GRAPH_REDRAW_MASK},
    {TK_OPTION_CUSTOM, "-radiallabelanchor", "radialLabelAnchor", "RadialLabelAnchor", DEF_GRAPH_RADIAL_LABEL_ANCHOR,
     -1, offsetof(Graph, radialLabelAnchor), 0, &polarLabelAnchorOption, GRAPH_POLAR_LABEL_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief", DEF_GRAPH_RELIEF, -1, offsetof(Graph, relief), 0, NULL,
     GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING_TABLE, "-representation", "representation", "Representation", DEF_GRAPH_REPRESENTATION, -1,
     offsetof(Graph, representation), 0, (ClientData)polarRepresentationNames,
     GRAPH_POLAR_REPRESENTATION_MASK | GRAPH_REDRAW_MASK},

    {TK_OPTION_STRING, "-rightmargin", "rightMargin", "Margin", DEF_GRAPH_MARGIN, offsetof(Graph, rightMarginObjPtr),
     -1, 0, NULL, GRAPH_PIXELS_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-rightvariable", "rightVariable", "RightVariable", DEF_GRAPH_MARGIN_VAR, -1,
     offsetof(Graph, rightMargin.varName), TK_OPTION_NULL_OK, NULL, GRAPH_REDRAW_MASK},

    {TK_OPTION_SYNONYM, "-rm", NULL, NULL, NULL, -1, -1, 0, "-rightmargin", 0},

    {TK_OPTION_STRING, "-shadow", "shadow", "Shadow", DEF_GRAPH_SHADOW_COLOR, offsetof(Graph, shadowObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, GRAPH_SHADOW_MASK | GRAPH_TEXT_STYLE_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING_TABLE, "-smithgrid", "smithGrid", "SmithGrid", DEF_GRAPH_SMITH_GRID, -1,
     offsetof(Graph, smithGrid), 0, (ClientData)smithGridNames, GRAPH_SMITH_GRID_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-takefocus", "takeFocus", "TakeFocus", DEF_GRAPH_TAKE_FOCUS, -1, offsetof(Graph, takeFocus),
     TK_OPTION_NULL_OK, NULL, GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-tile", "tile", "Tile", NULL, offsetof(Graph, tileObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     GRAPH_TILE_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-title", "title", "Title", DEF_GRAPH_TITLE, -1, offsetof(Graph, title), TK_OPTION_NULL_OK, NULL,
     GRAPH_TEXT_STYLE_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},

    {TK_OPTION_SYNONYM, "-tm", NULL, NULL, NULL, -1, -1, 0, "-topmargin", 0},

    {TK_OPTION_STRING, "-topmargin", "topMargin", "Margin", DEF_GRAPH_MARGIN, offsetof(Graph, topMarginObjPtr), -1, 0,
     NULL, GRAPH_PIXELS_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-topvariable", "topVariable", "TopVariable", DEF_GRAPH_MARGIN_VAR, -1,
     offsetof(Graph, topMargin.varName), TK_OPTION_NULL_OK, NULL, GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-width", "width", "Width", DEF_GRAPH_WIDTH, offsetof(Graph, widthObjPtr), -1, 0, NULL,
     GRAPH_PIXELS_MASK | GRAPH_GEOMETRY_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},

    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

typedef struct {
    const char *name;
    int width, height;
    int format;
} SnapData;

static Tcl_IdleProc DisplayGraph;
static Tcl_FreeProc DestroyGraph;
static Tk_EventProc GraphEventProc;

static Rbc_BindPickProc PickEntry;
static Tcl_ObjCmdProc2 StripchartObjCmd;
static Tcl_ObjCmdProc2 BarchartObjCmd;
static Tcl_ObjCmdProc2 GraphObjCmd;
static Tcl_CmdDeleteProc GraphInstCmdDeleteProc;
static Rbc_TileChangedProc TileChangedProc;

static void AdjustAxisPointers(Graph *graphPtr);
static int InitPens(Graph *graphPtr);
static int InitGraphOptions(Graph *graphPtr);
static void ResetGraphOptionContext(Graph *graphPtr);
static int ConfigureGraphOptions(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[], int *maskPtr);
static int ConfigureNewGraph(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]);
static void ReleaseGraphOptionResources(Graph *graphPtr);
static Graph *CreateGraph(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid classUid);
static int ConfigureGraph(Graph *graphPtr);
static int NewGraph(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid classUid);

static void DrawMargins(Graph *graphPtr, Drawable drawable);
static void DrawPlotRegion(Graph *graphPtr, Drawable drawable);
static void UpdateMarginTraces(Graph *graphPtr);

static int XAxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int X2AxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int YAxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int Y2AxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int BarOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int LineOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int ElementOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int ExtentsOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int InsideOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int InvtransformOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int TransformOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static int SnapOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

#ifdef WIN32
static int InitMetaFileHeader(Tk_Window tkwin, int width, int height, APMHEADER *mfhPtr);
static int CreateAPMetaFile(Tcl_Interp *interp, HANDLE hMetaFile, HDC hDC, APMHEADER *mfhPtr, const char *fileName);
#endif

typedef struct {
    const char *name;
    int option;
} GraphOptionName;

typedef struct {
    Rbc_OpSpecHeader header;
    Rbc_GraphOpProc *proc;
} GraphOpSpec;

static int GraphLayoutInt(Tcl_WideInt value) {
    if (value > INT_MAX) {
        return INT_MAX;
    }
    if (value < INT_MIN) {
        return INT_MIN;
    }
    return (int)value;
}

static int SetPolarLabelAnchor(void *clientData, Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj **valuePtrPtr,
                               char *widgRec, Tcl_Size offset, char *saveInternalPtr, int flags) {
    PolarLabelAnchor *anchorPtr;
    PolarLabelAnchor newAnchor;
    const char *string;

    (void)clientData;
    (void)tkwin;
    (void)flags;
    anchorPtr = (PolarLabelAnchor *)(widgRec + offset);
    /*
     * Tk needs the old internal representation for transactional
     * rollback.
     */
    if (saveInternalPtr != NULL) {
        *((PolarLabelAnchor *)saveInternalPtr) = *anchorPtr;
    }
    string = Tcl_GetString(*valuePtrPtr);
    if (strcmp(string, "auto") == 0) {
        newAnchor.anchor = TK_ANCHOR_CENTER;
        newAnchor.isAuto = TRUE;
    } else {
        if (Tk_GetAnchorFromObj(interp, *valuePtrPtr, &newAnchor.anchor) != TCL_OK) {
            return TCL_ERROR;
        }
        newAnchor.isAuto = FALSE;
    }
    *anchorPtr = newAnchor;
    return TCL_OK;
}

static Tcl_Obj *GetPolarLabelAnchor(void *clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset) {
    PolarLabelAnchor *anchorPtr;

    (void)clientData;
    (void)tkwin;
    anchorPtr = (PolarLabelAnchor *)(widgRec + offset);
    if (anchorPtr->isAuto) {
        return Tcl_NewStringObj("auto", -1);
    }
    return Tcl_NewStringObj(Tk_NameOfAnchor(anchorPtr->anchor), -1);
}

static void RestorePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr, char *saveInternalPtr) {
    (void)clientData;
    (void)tkwin;

    *((PolarLabelAnchor *)internalPtr) = *((PolarLabelAnchor *)saveInternalPtr);
}

static void FreePolarLabelAnchor(void *clientData, Tk_Window tkwin, char *internalPtr) {
    (void)clientData;
    (void)tkwin;
    (void)internalPtr;
}

static int GetGraphOptionFromObj(Tcl_Obj *objPtr, const GraphOptionName *optionMap, size_t nOptions) {
    const char *name;
    size_t i;

    name = Rbc_GetCanonicalOptionName(objPtr, graphOptionSpecs);
    if (name == NULL) {
        return 0;
    }
    for (i = 0; i < nOptions; i++) {
        if (strcmp(name, optionMap[i].name) == 0) {
            return optionMap[i].option;
        }
    }
    return 0;
}

static int IsGraphOption(Tcl_Obj *objPtr, const char *optionName) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, graphOptionSpecs);
    return ((name != NULL) && (strcmp(name, optionName) == 0));
}

static GraphPixelOption GetGraphPixelOption(Tcl_Obj *objPtr) {
    static const GraphOptionName optionMap[] = {{"-highlightthickness", GRAPH_PIXEL_OPTION_HIGHLIGHT_WIDTH},
                                                {"-borderwidth", GRAPH_PIXEL_OPTION_BORDER_WIDTH},
                                                {"-bottommargin", GRAPH_PIXEL_OPTION_BOTTOM_MARGIN},
                                                {"-halo", GRAPH_PIXEL_OPTION_HALO},
                                                {"-height", GRAPH_PIXEL_OPTION_HEIGHT},
                                                {"-leftmargin", GRAPH_PIXEL_OPTION_LEFT_MARGIN},
                                                {"-plotborderwidth", GRAPH_PIXEL_OPTION_PLOT_BORDER_WIDTH},
                                                {"-rightmargin", GRAPH_PIXEL_OPTION_RIGHT_MARGIN},
                                                {"-topmargin", GRAPH_PIXEL_OPTION_TOP_MARGIN},
                                                {"-width", GRAPH_PIXEL_OPTION_WIDTH}};
    return (GraphPixelOption)GetGraphOptionFromObj(objPtr, optionMap, sizeof(optionMap) / sizeof(optionMap[0]));
}

static Tcl_Obj *GetGraphPixelObject(Graph *graphPtr, GraphPixelOption option) {
    switch (option) {
    case GRAPH_PIXEL_OPTION_BORDER_WIDTH:
        return graphPtr->borderWidthObjPtr;

    case GRAPH_PIXEL_OPTION_BOTTOM_MARGIN:
        return graphPtr->bottomMarginObjPtr;

    case GRAPH_PIXEL_OPTION_HALO:
        return graphPtr->haloObjPtr;

    case GRAPH_PIXEL_OPTION_HEIGHT:
        return graphPtr->heightObjPtr;

    case GRAPH_PIXEL_OPTION_HIGHLIGHT_WIDTH:
        return graphPtr->highlightWidthObjPtr;

    case GRAPH_PIXEL_OPTION_LEFT_MARGIN:
        return graphPtr->leftMarginObjPtr;

    case GRAPH_PIXEL_OPTION_PLOT_BORDER_WIDTH:
        return graphPtr->plotBorderWidthObjPtr;

    case GRAPH_PIXEL_OPTION_RIGHT_MARGIN:
        return graphPtr->rightMarginObjPtr;

    case GRAPH_PIXEL_OPTION_TOP_MARGIN:
        return graphPtr->topMarginObjPtr;

    case GRAPH_PIXEL_OPTION_WIDTH:
        return graphPtr->widthObjPtr;

    case GRAPH_PIXEL_OPTION_NONE:
    default:
        Tcl_Panic("GetGraphPixelObject called with invalid option");
        return NULL;
    }
}

static int StageGraphPixelOption(Graph *graphPtr, Tcl_Obj *objPtr, GraphPixelOption option,
                                 GraphPixelTransaction *transactionPtr) {
    int value;

    /*
     * All graph distance options in this transaction, except
     * -highlightthickness, require non-negative values.
     */
    if (option == GRAPH_PIXEL_OPTION_HIGHLIGHT_WIDTH) {
        /*
         * Unlike the other graph distance options,
         * -highlightthickness historically accepts negative values.
         */
        if (Tk_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, objPtr, &value) != TCL_OK) {
            return TCL_ERROR;
        }
    } else {
        if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, objPtr, PIXELS_NONNEGATIVE, &value) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    switch (option) {
    case GRAPH_PIXEL_OPTION_BORDER_WIDTH:
        transactionPtr->borderWidth = value;
        break;

    case GRAPH_PIXEL_OPTION_BOTTOM_MARGIN:
        transactionPtr->bottomMarginSize = value;
        break;

    case GRAPH_PIXEL_OPTION_HALO:
        transactionPtr->halo = value;
        break;

    case GRAPH_PIXEL_OPTION_HEIGHT:
        transactionPtr->reqHeight = value;
        break;

    case GRAPH_PIXEL_OPTION_HIGHLIGHT_WIDTH:
        transactionPtr->highlightWidth = value;
        break;

    case GRAPH_PIXEL_OPTION_LEFT_MARGIN:
        transactionPtr->leftMarginSize = value;
        break;

    case GRAPH_PIXEL_OPTION_PLOT_BORDER_WIDTH:
        transactionPtr->plotBorderWidth = value;
        break;

    case GRAPH_PIXEL_OPTION_RIGHT_MARGIN:
        transactionPtr->rightMarginSize = value;
        break;

    case GRAPH_PIXEL_OPTION_TOP_MARGIN:
        transactionPtr->topMarginSize = value;
        break;

    case GRAPH_PIXEL_OPTION_WIDTH:
        transactionPtr->reqWidth = value;
        break;

    case GRAPH_PIXEL_OPTION_NONE:
    default:
        Tcl_Panic("StageGraphPixelOption called with invalid option");
        return TCL_ERROR;
    }

    transactionPtr->stagedMask |= GRAPH_PIXEL_OPTION_MASK(option);

    return TCL_OK;
}

static int PrepareGraphPixelTransaction(Graph *graphPtr, GraphPixelTransaction *transactionPtr) {
    unsigned int explicitMask;
    GraphPixelOption option;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitMask = 0;

    assert((graphPtr->optionObjc & 1) == 0);

    /*
     * Determine which values were explicitly supplied.
     */
    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        option = GetGraphPixelOption(graphPtr->optionObjv[i]);

        if (option != GRAPH_PIXEL_OPTION_NONE) {
            explicitMask |= GRAPH_PIXEL_OPTION_MASK(option);
        }
    }

    /*
     * During initial modern configuration, parse defaults and option
     * database values that were not explicitly overridden.
     */
    if (!graphPtr->optionsConfigured) {
        for (option = GRAPH_PIXEL_OPTION_BORDER_WIDTH; option <= GRAPH_PIXEL_OPTION_WIDTH;
             option = (GraphPixelOption)(option + 1)) {
            Tcl_Obj *objPtr;

            if (explicitMask & GRAPH_PIXEL_OPTION_MASK(option)) {
                continue;
            }

            objPtr = GetGraphPixelObject(graphPtr, option);

            if ((objPtr != NULL) && (StageGraphPixelOption(graphPtr, objPtr, option, transactionPtr) != TCL_OK)) {
                return TCL_ERROR;
            }
        }
    }

    /*
     * Process explicit occurrences in caller order. Therefore an invalid
     * earlier occurrence is not hidden by a later valid occurrence.
     */
    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        option = GetGraphPixelOption(graphPtr->optionObjv[i]);

        if (option == GRAPH_PIXEL_OPTION_NONE) {
            continue;
        }

        if (StageGraphPixelOption(graphPtr, graphPtr->optionObjv[i + 1], option, transactionPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}

static void CommitGraphPixelTransaction(Graph *graphPtr, GraphPixelTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & GRAPH_PIXEL_OPTION_MASK(GRAPH_PIXEL_OPTION_BORDER_WIDTH)) {
        graphPtr->borderWidth = transactionPtr->borderWidth;
    }

    if (transactionPtr->stagedMask & GRAPH_PIXEL_OPTION_MASK(GRAPH_PIXEL_OPTION_BOTTOM_MARGIN)) {
        graphPtr->bottomMargin.reqSize = transactionPtr->bottomMarginSize;
    }

    if (transactionPtr->stagedMask & GRAPH_PIXEL_OPTION_MASK(GRAPH_PIXEL_OPTION_HALO)) {
        graphPtr->halo = transactionPtr->halo;
    }

    if (transactionPtr->stagedMask & GRAPH_PIXEL_OPTION_MASK(GRAPH_PIXEL_OPTION_HIGHLIGHT_WIDTH)) {
        graphPtr->highlightWidth = transactionPtr->highlightWidth;
    }

    if (transactionPtr->stagedMask & GRAPH_PIXEL_OPTION_MASK(GRAPH_PIXEL_OPTION_HEIGHT)) {
        graphPtr->reqHeight = transactionPtr->reqHeight;
    }

    if (transactionPtr->stagedMask & GRAPH_PIXEL_OPTION_MASK(GRAPH_PIXEL_OPTION_LEFT_MARGIN)) {
        graphPtr->leftMargin.reqSize = transactionPtr->leftMarginSize;
    }

    if (transactionPtr->stagedMask & GRAPH_PIXEL_OPTION_MASK(GRAPH_PIXEL_OPTION_PLOT_BORDER_WIDTH)) {
        graphPtr->plotBorderWidth = transactionPtr->plotBorderWidth;
    }

    if (transactionPtr->stagedMask & GRAPH_PIXEL_OPTION_MASK(GRAPH_PIXEL_OPTION_RIGHT_MARGIN)) {
        graphPtr->rightMargin.reqSize = transactionPtr->rightMarginSize;
    }

    if (transactionPtr->stagedMask & GRAPH_PIXEL_OPTION_MASK(GRAPH_PIXEL_OPTION_TOP_MARGIN)) {
        graphPtr->topMargin.reqSize = transactionPtr->topMarginSize;
    }

    if (transactionPtr->stagedMask & GRAPH_PIXEL_OPTION_MASK(GRAPH_PIXEL_OPTION_WIDTH)) {
        graphPtr->reqWidth = transactionPtr->reqWidth;
    }

    transactionPtr->stagedMask = 0;
}

static GraphPaddingOption GetGraphPaddingOption(Tcl_Obj *objPtr) {
    static const GraphOptionName optionMap[] = {{"-plotpadx", GRAPH_PADDING_OPTION_X},
                                                {"-plotpady", GRAPH_PADDING_OPTION_Y}};

    return (GraphPaddingOption)GetGraphOptionFromObj(objPtr, optionMap, sizeof(optionMap) / sizeof(optionMap[0]));
}

static Tcl_Obj *GetGraphPaddingObject(Graph *graphPtr, GraphPaddingOption option) {
    switch (option) {
    case GRAPH_PADDING_OPTION_X:
        return graphPtr->plotPadXObjPtr;

    case GRAPH_PADDING_OPTION_Y:
        return graphPtr->plotPadYObjPtr;

    case GRAPH_PADDING_OPTION_NONE:
    default:
        Tcl_Panic("GetGraphPaddingObject called with invalid option");
        return NULL;
    }
}

static int StageGraphPaddingOption(Graph *graphPtr, Tcl_Obj *objPtr, GraphPaddingOption option,
                                   GraphPaddingTransaction *transactionPtr) {
    Rbc_Pad pad;

    if (Rbc_GetPadFromObj(graphPtr->interp, graphPtr->tkwin, objPtr, &pad) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (option) {
    case GRAPH_PADDING_OPTION_X:
        transactionPtr->padX = pad;
        break;

    case GRAPH_PADDING_OPTION_Y:
        transactionPtr->padY = pad;
        break;

    case GRAPH_PADDING_OPTION_NONE:
    default:
        Tcl_Panic("StageGraphPaddingOption called with invalid option");
        return TCL_ERROR;
    }

    transactionPtr->stagedMask |= GRAPH_PADDING_OPTION_MASK(option);

    return TCL_OK;
}

static int PrepareGraphPaddingTransaction(Graph *graphPtr, GraphPaddingTransaction *transactionPtr) {
    unsigned int explicitMask;
    GraphPaddingOption option;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitMask = 0;

    assert((graphPtr->optionObjc & 1) == 0);

    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        option = GetGraphPaddingOption(graphPtr->optionObjv[i]);

        if (option != GRAPH_PADDING_OPTION_NONE) {
            explicitMask |= GRAPH_PADDING_OPTION_MASK(option);
        }
    }

    if (!graphPtr->optionsConfigured) {
        for (option = GRAPH_PADDING_OPTION_X; option <= GRAPH_PADDING_OPTION_Y;
             option = (GraphPaddingOption)(option + 1)) {
            Tcl_Obj *objPtr;

            if (explicitMask & GRAPH_PADDING_OPTION_MASK(option)) {
                continue;
            }

            objPtr = GetGraphPaddingObject(graphPtr, option);

            if ((objPtr != NULL) && (StageGraphPaddingOption(graphPtr, objPtr, option, transactionPtr) != TCL_OK)) {
                return TCL_ERROR;
            }
        }
    }

    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        option = GetGraphPaddingOption(graphPtr->optionObjv[i]);

        if (option == GRAPH_PADDING_OPTION_NONE) {
            continue;
        }

        if (StageGraphPaddingOption(graphPtr, graphPtr->optionObjv[i + 1], option, transactionPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}

static void CommitGraphPaddingTransaction(Graph *graphPtr, GraphPaddingTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & GRAPH_PADDING_OPTION_MASK(GRAPH_PADDING_OPTION_X)) {
        graphPtr->padX = transactionPtr->padX;
    }

    if (transactionPtr->stagedMask & GRAPH_PADDING_OPTION_MASK(GRAPH_PADDING_OPTION_Y)) {
        graphPtr->padY = transactionPtr->padY;
    }

    transactionPtr->stagedMask = 0;
}

static int GetGraphBarModeFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, BarMode *modePtr) {
    static const struct {
        const char *name;
        BarMode mode;
    } barModes[] = {{"aligned", MODE_ALIGNED}, {"infront", MODE_INFRONT}, {"normal", MODE_INFRONT},
                    {"overlap", MODE_OVERLAP}, {"stacked", MODE_STACKED}, {NULL, (BarMode)0}};

    int index;

    if (Tcl_GetIndexFromObjStruct(interp, objPtr, barModes, sizeof(barModes[0]), "mode", 0, &index) != TCL_OK) {
        return TCL_ERROR;
    }

    *modePtr = barModes[index].mode;

    return TCL_OK;
}

static int StageGraphBarMode(Graph *graphPtr, Tcl_Obj *objPtr, GraphBarModeTransaction *transactionPtr) {
    BarMode mode;

    if (GetGraphBarModeFromObj(graphPtr->interp, objPtr, &mode) != TCL_OK) {
        return TCL_ERROR;
    }

    transactionPtr->mode = mode;
    transactionPtr->staged = TRUE;

    return TCL_OK;
}

static int PrepareGraphBarModeTransaction(Graph *graphPtr, GraphBarModeTransaction *transactionPtr) {
    int explicitlySet;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitlySet = FALSE;

    assert((graphPtr->optionObjc & 1) == 0);

    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        if (IsGraphOption(graphPtr->optionObjv[i], "-barmode")) {
            explicitlySet = TRUE;
            break;
        }
    }

    if ((!graphPtr->optionsConfigured) && (!explicitlySet) && (graphPtr->barModeObjPtr != NULL)) {
        if (StageGraphBarMode(graphPtr, graphPtr->barModeObjPtr, transactionPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        if (!IsGraphOption(graphPtr->optionObjv[i], "-barmode")) {
            continue;
        }

        if (StageGraphBarMode(graphPtr, graphPtr->optionObjv[i + 1], transactionPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}

static void CommitGraphBarModeTransaction(Graph *graphPtr, GraphBarModeTransaction *transactionPtr) {
    if (transactionPtr->staged) {
        graphPtr->mode = transactionPtr->mode;
        transactionPtr->staged = FALSE;
    }
}

static void FreeGraphShadow(Shadow *shadowPtr) {
    if (shadowPtr->color != NULL) {
        Tk_FreeColor(shadowPtr->color);
    }

    shadowPtr->color = NULL;
    shadowPtr->offset = 0;
}

static int StageGraphShadow(Graph *graphPtr, Tcl_Obj *objPtr, GraphShadowTransaction *transactionPtr) {
    Shadow newShadow;

    newShadow.color = NULL;
    newShadow.offset = 0;

    if (Rbc_GetShadowFromObj(graphPtr->interp, graphPtr->tkwin, objPtr, &newShadow) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Acquire and validate the replacement before releasing a candidate
     * staged by an earlier occurrence.
     */
    if (transactionPtr->staged) {
        FreeGraphShadow(&transactionPtr->shadow);
    }

    transactionPtr->shadow = newShadow;
    transactionPtr->staged = TRUE;

    return TCL_OK;
}

static void FreeGraphShadowTransaction(GraphShadowTransaction *transactionPtr) {
    if (transactionPtr->staged) {
        FreeGraphShadow(&transactionPtr->shadow);
    }

    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

static int PrepareGraphShadowTransaction(Graph *graphPtr, GraphShadowTransaction *transactionPtr) {
    int explicitlySet;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitlySet = FALSE;

    assert((graphPtr->optionObjc & 1) == 0);

    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        if (IsGraphOption(graphPtr->optionObjv[i], "-shadow")) {
            explicitlySet = TRUE;
            break;
        }
    }

    if ((!graphPtr->optionsConfigured) && (!explicitlySet) && (graphPtr->shadowObjPtr != NULL)) {
        if (StageGraphShadow(graphPtr, graphPtr->shadowObjPtr, transactionPtr) != TCL_OK) {
            goto error;
        }
    }

    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        if (!IsGraphOption(graphPtr->optionObjv[i], "-shadow")) {
            continue;
        }

        if (StageGraphShadow(graphPtr, graphPtr->optionObjv[i + 1], transactionPtr) != TCL_OK) {
            goto error;
        }
    }

    return TCL_OK;

error:
    FreeGraphShadowTransaction(transactionPtr);
    return TCL_ERROR;
}

static void CommitGraphShadowTransaction(Graph *graphPtr, GraphShadowTransaction *transactionPtr) {
    Shadow oldShadow;

    if (!transactionPtr->staged) {
        return;
    }

    oldShadow = graphPtr->titleTextStyle.shadow;
    graphPtr->titleTextStyle.shadow = transactionPtr->shadow;

    transactionPtr->shadow.color = NULL;
    transactionPtr->shadow.offset = 0;
    transactionPtr->staged = FALSE;

    FreeGraphShadow(&oldShadow);
}

static int StageGraphTile(Graph *graphPtr, Tcl_Obj *objPtr, GraphTileTransaction *transactionPtr) {
    Rbc_Tile newTile;

    newTile = NULL;

    if ((objPtr != NULL) && (Tcl_GetCharLength(objPtr) > 0)) {
        if (Rbc_GetTile(graphPtr->interp, graphPtr->tkwin, Tcl_GetString(objPtr), &newTile) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    /*
     * Acquire the replacement before releasing a previously staged tile.
     */
    if (transactionPtr->staged && (transactionPtr->tile != NULL)) {
        Rbc_FreeTile(transactionPtr->tile);
    }

    transactionPtr->tile = newTile;
    transactionPtr->staged = TRUE;

    return TCL_OK;
}

static void FreeGraphTileTransaction(GraphTileTransaction *transactionPtr) {
    if (transactionPtr->staged && (transactionPtr->tile != NULL)) {
        Rbc_FreeTile(transactionPtr->tile);
    }

    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

static int PrepareGraphTileTransaction(Graph *graphPtr, GraphTileTransaction *transactionPtr) {
    int explicitlySet;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitlySet = FALSE;

    assert((graphPtr->optionObjc & 1) == 0);

    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        if (IsGraphOption(graphPtr->optionObjv[i], "-tile")) {
            explicitlySet = TRUE;
            break;
        }
    }

    if ((!graphPtr->optionsConfigured) && (!explicitlySet) && (graphPtr->tileObjPtr != NULL)) {
        if (StageGraphTile(graphPtr, graphPtr->tileObjPtr, transactionPtr) != TCL_OK) {
            goto error;
        }
    }

    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        if (!IsGraphOption(graphPtr->optionObjv[i], "-tile")) {
            continue;
        }

        if (StageGraphTile(graphPtr, graphPtr->optionObjv[i + 1], transactionPtr) != TCL_OK) {
            goto error;
        }
    }

    return TCL_OK;

error:
    FreeGraphTileTransaction(transactionPtr);
    return TCL_ERROR;
}

static void CommitGraphTileTransaction(Graph *graphPtr, GraphTileTransaction *transactionPtr) {
    Rbc_Tile oldTile;

    if (!transactionPtr->staged) {
        return;
    }

    oldTile = graphPtr->tile;
    graphPtr->tile = transactionPtr->tile;

    transactionPtr->tile = NULL;
    transactionPtr->staged = FALSE;

    if (graphPtr->tile != NULL) {
        Rbc_SetTileChangedProc(graphPtr->tile, TileChangedProc, graphPtr);
    }

    if (oldTile != NULL) {
        Rbc_FreeTile(oldTile);
    }
}

static int ParsePolarAngleTicks(Tcl_Interp *interp, Tcl_Obj *objPtr, const char *optionName, double **ticksPtr,
                                Tcl_Size *nTicksPtr) {
    Tcl_Obj **objv;
    Tcl_Size objc;
    Tcl_Size i;
    double *ticks;
    size_t bytes;

    *ticksPtr = NULL;
    *nTicksPtr = 0;
    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc == 0) {
        return TCL_OK;
    }
    if ((Tcl_WideUInt)objc > (Tcl_WideUInt)(SIZE_MAX / sizeof(double))) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s contains too many angles", optionName));
        return TCL_ERROR;
    }
    bytes = (size_t)objc * sizeof(double);
    ticks = Tcl_AttemptAlloc(bytes);
    if (ticks == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate polar angular tick array", -1));
        return TCL_ERROR;
    }
    for (i = 0; i < objc; i++) {
        double value;

        if (Tcl_GetDoubleFromObj(interp, objv[i], &value) != TCL_OK) {
            ckfree(ticks);
            return TCL_ERROR;
        }
        if ((!FINITE(value)) || (value < 0.0) || (value >= 360.0)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("%s angle \"%s\" must be finite "
                                                   "and in the range 0 <= angle < 360",
                                                   optionName, Tcl_GetString(objv[i])));
            ckfree(ticks);
            return TCL_ERROR;
        }
        /*
         * Canonicalize negative zero.
         */
        if (value == 0.0) {
            value = 0.0;
        }
        ticks[i] = value;
    }
    *ticksPtr = ticks;
    *nTicksPtr = objc;
    return TCL_OK;
}

static int StageGraphPolarAngleTicks(Graph *graphPtr, Tcl_Obj *objPtr, int major,
                                     GraphPolarAngleTicksTransaction *transactionPtr) {
    double *ticks;
    Tcl_Size nTicks;
    const char *optionName;

    ticks = NULL;
    nTicks = 0;
    optionName = major ? "-anglemajorticks" : "-angleminorticks";
    if (ParsePolarAngleTicks(graphPtr->interp, objPtr, optionName, &ticks, &nTicks) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Only release an earlier staged value after the new
     * value has parsed successfully.
     */
    if (major) {
        if (transactionPtr->majorStaged && (transactionPtr->majorTicks != NULL)) {
            ckfree(transactionPtr->majorTicks);
        }
        transactionPtr->majorTicks = ticks;
        transactionPtr->nMajorTicks = nTicks;
        transactionPtr->majorStaged = TRUE;
    } else {
        if (transactionPtr->minorStaged && (transactionPtr->minorTicks != NULL)) {
            ckfree(transactionPtr->minorTicks);
        }
        transactionPtr->minorTicks = ticks;
        transactionPtr->nMinorTicks = nTicks;
        transactionPtr->minorStaged = TRUE;
    }
    return TCL_OK;
}

static void FreeGraphPolarAngleTicksTransaction(GraphPolarAngleTicksTransaction *transactionPtr) {
    if (transactionPtr->majorTicks != NULL) {
        ckfree(transactionPtr->majorTicks);
    }
    if (transactionPtr->minorTicks != NULL) {
        ckfree(transactionPtr->minorTicks);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

static int PrepareGraphPolarAngleTicksTransaction(Graph *graphPtr, GraphPolarAngleTicksTransaction *transactionPtr) {
    int explicitMajor;
    int explicitMinor;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitMajor = FALSE;
    explicitMinor = FALSE;
    assert((graphPtr->optionObjc & 1) == 0);
    /*
     * Determine whether the caller explicitly supplied either
     * option.  On initial configuration, option-database/default
     * values must also be staged.
     */
    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        if (IsGraphOption(graphPtr->optionObjv[i], "-anglemajorticks")) {
            explicitMajor = TRUE;
        } else if (IsGraphOption(graphPtr->optionObjv[i], "-angleminorticks")) {
            explicitMinor = TRUE;
        }
    }
    if (!graphPtr->optionsConfigured) {
        if ((!explicitMajor) && (graphPtr->angleMajorTicksObjPtr != NULL)) {
            if (StageGraphPolarAngleTicks(graphPtr, graphPtr->angleMajorTicksObjPtr, TRUE, transactionPtr) != TCL_OK) {
                goto error;
            }
        }
        if ((!explicitMinor) && (graphPtr->angleMinorTicksObjPtr != NULL)) {
            if (StageGraphPolarAngleTicks(graphPtr, graphPtr->angleMinorTicksObjPtr, FALSE, transactionPtr) != TCL_OK) {
                goto error;
            }
        }
    }
    /*
     * Process explicit values in caller order so that an invalid
     * earlier duplicate is not hidden by a later valid value.
     */
    for (i = 0; i < graphPtr->optionObjc; i += 2) {
        if (IsGraphOption(graphPtr->optionObjv[i], "-anglemajorticks")) {
            if (StageGraphPolarAngleTicks(graphPtr, graphPtr->optionObjv[i + 1], TRUE, transactionPtr) != TCL_OK) {
                goto error;
            }
        } else if (IsGraphOption(graphPtr->optionObjv[i], "-angleminorticks")) {
            if (StageGraphPolarAngleTicks(graphPtr, graphPtr->optionObjv[i + 1], FALSE, transactionPtr) != TCL_OK) {
                goto error;
            }
        }
    }
    return TCL_OK;

error:
    FreeGraphPolarAngleTicksTransaction(transactionPtr);
    return TCL_ERROR;
}

static void CommitGraphPolarAngleTicksTransaction(Graph *graphPtr, GraphPolarAngleTicksTransaction *transactionPtr) {
    if (transactionPtr->majorStaged) {
        double *oldTicks;

        oldTicks = graphPtr->angleMajorTicks;
        graphPtr->angleMajorTicks = transactionPtr->majorTicks;
        graphPtr->nAngleMajorTicks = transactionPtr->nMajorTicks;
        transactionPtr->majorTicks = NULL;
        transactionPtr->nMajorTicks = 0;
        transactionPtr->majorStaged = FALSE;
        if (oldTicks != NULL) {
            ckfree(oldTicks);
        }
    }
    if (transactionPtr->minorStaged) {
        double *oldTicks;

        oldTicks = graphPtr->angleMinorTicks;
        graphPtr->angleMinorTicks = transactionPtr->minorTicks;
        graphPtr->nAngleMinorTicks = transactionPtr->nMinorTicks;
        transactionPtr->minorTicks = NULL;
        transactionPtr->nMinorTicks = 0;
        transactionPtr->minorStaged = FALSE;
        if (oldTicks != NULL) {
            ckfree(oldTicks);
        }
    }
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_EventuallyRedrawGraph --
 *
 *      Tells the Tk dispatcher to call the graph display routine at
 *      the next idle point.  This request is made only if the window
 *      is displayed and no other redraw request is pending.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The window is eventually redisplayed.
 *
 *--------------------------------------------------------------
 */
void Rbc_EventuallyRedrawGraph(Graph *graphPtr) {
    if ((graphPtr->tkwin != NULL) && !(graphPtr->flags & REDRAW_PENDING)) {
        Tcl_DoWhenIdle(DisplayGraph, graphPtr);
        graphPtr->flags |= REDRAW_PENDING;
    }
}


/*
 *--------------------------------------------------------------
 *
 * GraphEventProc --
 *
 *      This procedure is invoked by the Tk dispatcher for various
 *      events on graphs.
 *
 * Parameters:
 *      ClientData clientData - Graph widget record
 *      register XEvent *eventPtr - Event which triggered call to routine
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      When the window gets deleted, internal structures get
 *      cleaned up.  When it gets exposed, the graph is eventually
 *      redisplayed.
 *
 *--------------------------------------------------------------
 */
static void GraphEventProc(ClientData clientData, register XEvent *eventPtr) {
    Graph *graphPtr = clientData;

    if (eventPtr->type == Expose) {
        if (eventPtr->xexpose.count == 0) {
            graphPtr->flags |= REDRAW_WORLD;
            Rbc_EventuallyRedrawGraph(graphPtr);
        }
    } else if ((eventPtr->type == FocusIn) || (eventPtr->type == FocusOut)) {
        if (eventPtr->xfocus.detail != NotifyInferior) {
            if (eventPtr->type == FocusIn) {
                graphPtr->flags |= GRAPH_FOCUS;
            } else {
                graphPtr->flags &= ~GRAPH_FOCUS;
            }
            graphPtr->flags |= REDRAW_WORLD;
            Rbc_EventuallyRedrawGraph(graphPtr);
        }
    } else if (eventPtr->type == DestroyNotify) {
        if (graphPtr->tkwin != NULL) {
            /*
             * Modern Tk option resources must be released while the
             * component's Tk window is still valid.
             */
            if (graphPtr->gridPtr != NULL) {
                Rbc_DestroyGrid(graphPtr);
            }
            if (graphPtr->crosshairs != NULL) {
                Rbc_DestroyCrosshairs(graphPtr);
            }
            if (graphPtr->postscript != NULL) {
                Rbc_DestroyPostScript(graphPtr);
            }
            if (graphPtr->legend != NULL) {
                Rbc_ReleaseLegendTkResources(graphPtr);
            }
            Rbc_ReleaseMarkerTkResources(graphPtr);
            Rbc_ReleasePenTkResources(graphPtr);
            ReleaseGraphOptionResources(graphPtr);

            Rbc_DeleteWindowInstanceData(graphPtr->tkwin);
            graphPtr->tkwin = NULL;

            Tcl_DeleteCommandFromToken(graphPtr->interp, graphPtr->cmdToken);
        }

        if (graphPtr->flags & REDRAW_PENDING) {
            Tcl_CancelIdleCall(DisplayGraph, graphPtr);
        }

        Tcl_EventuallyFree(graphPtr, DestroyGraph);
    } else if (eventPtr->type == ConfigureNotify) {
        graphPtr->flags |= (MAP_WORLD | REDRAW_WORLD);
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GraphInstCmdDeleteProc --
 *
 *      This procedure is invoked when a widget command is deleted.  If
 *      the widget isn't already in the process of being destroyed,
 *      this command destroys it.
 *
 * Parameters:
 *      ClientData clientData - Pointer to widget record.
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The widget is destroyed.
 *
 *---------------------------------------------------------------------- */
static void GraphInstCmdDeleteProc(ClientData clientData) {
    Graph *graphPtr = clientData;

    if (graphPtr->tkwin != NULL) {
        Tk_Window tkwin;

        tkwin = graphPtr->tkwin;

        if (graphPtr->gridPtr != NULL) {
            Rbc_DestroyGrid(graphPtr);
        }
        if (graphPtr->crosshairs != NULL) {
            Rbc_DestroyCrosshairs(graphPtr);
        }
        if (graphPtr->postscript != NULL) {
            Rbc_DestroyPostScript(graphPtr);
        }
        if (graphPtr->legend != NULL) {
            Rbc_ReleaseLegendTkResources(graphPtr);
        }
        Rbc_ReleaseMarkerTkResources(graphPtr);
        Rbc_ReleasePenTkResources(graphPtr);
        ReleaseGraphOptionResources(graphPtr);

        graphPtr->tkwin = NULL;

#ifdef ITCL_NAMESPACES
        Itk_SetWidgetCommand(tkwin, (Tcl_Command)NULL);
#endif

        Rbc_DeleteWindowInstanceData(tkwin);
        Tk_DestroyWindow(tkwin);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TileChangedProc --
 *
 *      Rebuilds the designated GC with the new tile pixmap.
 *
 * Parameters:
 *      ClientData clientData
 *      Rbc_Tile tile - Not used.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void TileChangedProc(ClientData clientData, Rbc_Tile tile) {
    Graph *graphPtr = clientData;

    if (graphPtr->tkwin != NULL) {
        graphPtr->flags |= REDRAW_WORLD;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
}

/*
 *--------------------------------------------------------------
 *
 * AdjustAxisPointers --
 *
 *      Sets the axis pointers according to whether the axis is
 *      inverted on not.  The axis sites are also reset.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void AdjustAxisPointers(Graph *graphPtr) {
    if (graphPtr->inverted) {
        graphPtr->leftMargin.axes = graphPtr->axisChain[0];
        graphPtr->bottomMargin.axes = graphPtr->axisChain[1];
        graphPtr->rightMargin.axes = graphPtr->axisChain[2];
        graphPtr->topMargin.axes = graphPtr->axisChain[3];
    } else {
        graphPtr->leftMargin.axes = graphPtr->axisChain[1];
        graphPtr->bottomMargin.axes = graphPtr->axisChain[0];
        graphPtr->rightMargin.axes = graphPtr->axisChain[3];
        graphPtr->topMargin.axes = graphPtr->axisChain[2];
    }
}

/*
 *----------------------------------------------------------------------
 *
 * InitPens --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int InitPens(Graph *graphPtr) {
    Tcl_InitHashTable(&graphPtr->penTable, TCL_STRING_KEYS);
    if (Rbc_CreatePen(graphPtr, "activeLine", rbcLineElementUid, 0, NULL) == NULL) {
        return TCL_ERROR;
    }
    if (Rbc_CreatePen(graphPtr, "activeBar", rbcBarElementUid, 0, NULL) == NULL) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GraphTags --
 *
 *      Appends the binding identifiers for the picked graph object.
 *      The object type is supplied explicitly by PickEntry through the
 *      binding context; no common structure layout is assumed.
 *
 * Parameters:
 *      Rbc_BindTable table - Graph binding table.
 *      ClientData object   - Picked Axis, Element, or Marker pointer.
 *      ClientData context  - GraphBindContext value.
 *      Rbc_List list       - Destination binding identifier list.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Appends the object's name, class, and configured bind tags.
 *
 *----------------------------------------------------------------------
 */
void Rbc_GraphTags(Rbc_BindTable table, ClientData object, ClientData context, Rbc_List list) {
    Graph *graphPtr;

    graphPtr = (Graph *)Rbc_GetBindingData(table);
    switch ((GraphBindContext)PTR2INT(context)) {
    case GRAPH_BIND_CONTEXT_AXIS:
        Rbc_AppendAxisBindingTags(graphPtr, (Axis *)object, list);
        break;
    case GRAPH_BIND_CONTEXT_ELEMENT:
        Rbc_AppendElementBindingTags(graphPtr, (Element *)object, list);
        break;
    case GRAPH_BIND_CONTEXT_MARKER:
        Rbc_AppendMarkerBindingTags(graphPtr, (Marker *)object, list);
        break;
    default:
        /*
         * An object returned by PickEntry must always have a valid
         * GraphBindContext.
         */
        break;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PickEntry --
 *
 *      Find the closest point from the set of displayed elements,
 *      searching the display list from back to front.  That way, if
 *      the points from two different elements overlay each other exactly,
 *      the one that's on top (visible) is picked.
 *
 * Parameters:
 *      ClientData clientData
 *      int x
 *      int y
 *      ClientData *contextPtr - Not used.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static ClientData PickEntry(ClientData clientData, int x, int y, ClientData *contextPtr) {
    Graph *graphPtr;
    Rbc_ChainLink *linkPtr;
    Axis *axisPtr;
    Element *elemPtr;
    Marker *markerPtr;
    Extents2D exts;

    graphPtr = clientData;
    *contextPtr = NULL;
    if (graphPtr->flags & MAP_ALL) {
        /*
         * Need to recalculate graph layout first.
         */
        Rbc_LayoutGraph(graphPtr);
    }
    Rbc_GraphExtents(graphPtr, &exts);
    if ((x > exts.right) || (x < exts.left) || (y > exts.bottom) || (y < exts.top)) {
        /*
         * The sample coordinate is in one of the graph margins.
         * Only an axis can be picked there.
         */
        axisPtr = Rbc_NearestAxis(graphPtr, x, y);
        if (axisPtr != NULL) {
            *contextPtr = INT2PTR(GRAPH_BIND_CONTEXT_AXIS);
        }
        return axisPtr;
    }
    /*
     * Search from top to bottom:
     *
     * 1. Markers drawn above elements.
     * 2. Elements, from the back of the display list.
     * 3. Markers drawn below elements.
     */
    markerPtr = Rbc_NearestMarker(graphPtr, x, y, FALSE);
    if (markerPtr != NULL) {
        *contextPtr = INT2PTR(GRAPH_BIND_CONTEXT_MARKER);
        return markerPtr;
    }
    {
        ClosestSearch search;

        search.along = SEARCH_BOTH;
        search.halo = (graphPtr->halo < INT_MAX) ? graphPtr->halo + 1 : INT_MAX;
        search.index = -1;
        search.x = x;
        search.y = y;
        search.dist = (double)search.halo + 1.0;
        search.mode = SEARCH_AUTO;
        for (linkPtr = Rbc_ChainLastLink(graphPtr->elements.displayList); linkPtr != NULL;
             linkPtr = Rbc_ChainPrevLink(linkPtr)) {
            elemPtr = Rbc_ChainGetValue(linkPtr);
            if ((elemPtr->flags & MAP_ITEM) || Rbc_VectorNotifyPending(elemPtr->x.clientId) ||
                Rbc_VectorNotifyPending(elemPtr->y.clientId)) {
                continue;
            }
            if ((!elemPtr->hidden) && (!elemPtr->plotHidden) && (elemPtr->state == STATE_NORMAL)) {
                (*elemPtr->procsPtr->closestProc)(graphPtr, elemPtr, &search);
            }
        }
        if (search.dist <= (double)search.halo) {
            *contextPtr = INT2PTR(GRAPH_BIND_CONTEXT_ELEMENT);
            return search.elemPtr;
        }
    }
    markerPtr = Rbc_NearestMarker(graphPtr, x, y, TRUE);
    if (markerPtr != NULL) {
        *contextPtr = INT2PTR(GRAPH_BIND_CONTEXT_MARKER);
        return markerPtr;
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * InitGraphOptions --
 *
 *      Creates the graph option table and installs its default and
 *      option-database values.
 *
 *      Unlike graph components, the graph is the actual widget record.
 *      Its options are therefore initialized directly with
 *      Tk_InitOptions rather than Rbc_InitComponentOptions.
 *
 *      This function does not construct derived graph resources.
 *      ConfigureGraphOptions performs that work through ConfigureGraph.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record.
 *
 * Results:
 *      TCL_OK if the graph options were initialized successfully.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Creates graphPtr->optionTable and stores Tk-managed option
 *      resources in the Graph record.
 *
 *----------------------------------------------------------------------
 */
static int InitGraphOptions(Graph *graphPtr) {
    if (graphPtr->optionsInitialized) {
        return TCL_OK;
    }

    graphPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, graphOptionSpecs);

    if (graphPtr->optionTable == NULL) {
        return TCL_ERROR;
    }

    /*
     * Graph is the widget record itself, so use the graph window
     * directly for option-database lookup and Tk resource allocation.
     */
    if (Tk_InitOptions(graphPtr->interp, (char *)graphPtr, graphPtr->optionTable, graphPtr->tkwin) != TCL_OK) {
        /*
         * Tk_InitOptions may have allocated resources before detecting
         * an invalid default or option-database value. All option-owned
         * pointer fields were initially NULL, so partial cleanup is safe.
         */
        Tk_FreeConfigOptions((char *)graphPtr, graphPtr->optionTable, graphPtr->tkwin);

        graphPtr->optionTable = NULL;

        return TCL_ERROR;
    }

    graphPtr->optionsInitialized = TRUE;
    graphPtr->tkResourcesReleased = FALSE;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ResetGraphOptionContext --
 *
 *      Clears the temporary context retained while a modern graph
 *      configuration transaction is active.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The graph no longer retains pointers to the caller-owned option
 *      vector.
 *
 *----------------------------------------------------------------------
 */
static void ResetGraphOptionContext(Graph *graphPtr) {
    graphPtr->optionMask = 0;
    graphPtr->optionObjc = 0;
    graphPtr->optionObjv = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureGraphOptions --
 *
 *      Applies graph option/value pairs transactionally and invokes
 *      ConfigureGraph to validate retained values and construct derived
 *      graph resources.
 *
 *      The original option vector and effective Tk type mask are
 *      temporarily retained in the Graph record so ConfigureGraph can
 *      process repeated retained options in caller order.
 *
 * Parameters:
 *      Graph *graphPtr       - Graph widget record.
 *      int objc              - Number of option/value objects.
 *      Tcl_Obj *const objv[] - Option/value objects.
 *      int *maskPtr          - Optional destination for the changed-option
 *                              mask returned by Tk_SetOptions.
 *
 * Results:
 *      TCL_OK if all options and derived resources were configured.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      On success, updates the graph configuration.
 *      On failure, restores all Tk-managed option fields.
 *
 *----------------------------------------------------------------------
 */
static int ConfigureGraphOptions(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[], int *maskPtr) {
    Tk_SavedOptions savedOptions;
    Tcl_Obj *errorObjPtr;
    int mask;

    assert(graphPtr->optionsInitialized);
    assert(graphPtr->optionTable != NULL);
    assert((objc & 1) == 0);

    /*
     * Clear stale transaction context before invoking Tk.
     */
    ResetGraphOptionContext(graphPtr);

    if (Tk_SetOptions(graphPtr->interp, (char *)graphPtr, graphPtr->optionTable, objc, objv, graphPtr->tkwin,
                      &savedOptions, &mask) != TCL_OK) {
        return TCL_ERROR;
    }

    /*
     * Make the changed-option mask and original argument order
     * available to retained-value transactions.
     *
     * The initial configuration must construct every derived resource,
     * even when all effective values came from defaults or the option
     * database and Tk_SetOptions therefore returned a zero mask.
     */
    graphPtr->optionMask = (unsigned int)mask;

    if (!graphPtr->optionsConfigured) {
        graphPtr->optionMask |= GRAPH_INITIALIZE_MASK;
    }

    graphPtr->optionObjc = objc;
    graphPtr->optionObjv = objv;

    if (ConfigureGraph(graphPtr) != TCL_OK) {
        /*
         * Restoring Tk-managed options may alter the interpreter result.
         * Preserve the error produced by ConfigureGraph.
         */
        errorObjPtr = Tcl_GetObjResult(graphPtr->interp);
        Tcl_IncrRefCount(errorObjPtr);

        /*
         * Never retain pointers to the caller-owned option vector while
         * restoring the configuration.
         */
        ResetGraphOptionContext(graphPtr);

        Tk_RestoreSavedOptions(&savedOptions);

        Tcl_SetObjResult(graphPtr->interp, errorObjPtr);
        Tcl_DecrRefCount(errorObjPtr);

        return TCL_ERROR;
    }

    ResetGraphOptionContext(graphPtr);

    graphPtr->optionsConfigured = TRUE;

    Tk_FreeSavedOptions(&savedOptions);

    if (maskPtr != NULL) {
        *maskPtr = mask;
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureNewGraph --
 *
 *      Initializes the modern graph option table and applies the
 *      creation-time option/value pairs.
 *
 *      ConfigureGraphOptions is called even when objc is zero because
 *      Tk_InitOptions may have installed defaults or option-database
 *      values that must be converted into the live graph fields.
 *
 * Parameters:
 *      Graph *graphPtr       - Graph widget record.
 *      int objc              - Number of option/value objects.
 *      Tcl_Obj *const objv[] - Creation-time option/value objects.
 *
 * Results:
 *      TCL_OK if the option table and derived graph resources were
 *      initialized successfully.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Initializes the modern graph option lifecycle and configures the
 *      graph.
 *
 *----------------------------------------------------------------------
 */
static int ConfigureNewGraph(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (InitGraphOptions(graphPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Polar plots are square by default.  Tk_InitOptions has already
     * applied any option-database value, so only install the Polar
     * class default when the option database did not specify aspect.
     *
     * Any explicit creation-time -aspect option is applied below by
     * ConfigureGraphOptions() and therefore takes precedence.
     */
    if ((graphPtr->classUid == rbcPolarElementUid) && (Tk_GetOption(graphPtr->tkwin, "aspect", "Aspect") == NULL)) {
        graphPtr->aspect = 1.0;
    }
    return ConfigureGraphOptions(graphPtr, objc, objv, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * ReleaseGraphOptionResources --
 *
 *      Releases resources owned directly by the modern Tk graph option
 *      table.
 *
 *      Derived graph resources, including graphics contexts, the title
 *      shadow, and the tile, remain the responsibility of the existing
 *      graph destruction path.
 *
 *      This function must be called while graphPtr->tkwin is still
 *      valid.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Releases Tk-managed graph option resources. Repeated calls and
 *      calls before graph option initialization are harmless.
 *
 *----------------------------------------------------------------------
 */
static void ReleaseGraphOptionResources(Graph *graphPtr) {
    if ((!graphPtr->optionsInitialized) || graphPtr->tkResourcesReleased) {
        return;
    }

    assert(graphPtr->optionTable != NULL);
    assert(graphPtr->tkwin != NULL);

    /*
     * Do not retain pointers to configuration arguments during
     * destruction.
     */
    ResetGraphOptionContext(graphPtr);

    Tk_FreeConfigOptions((char *)graphPtr, graphPtr->optionTable, graphPtr->tkwin);

    graphPtr->optionsInitialized = FALSE;
    graphPtr->tkResourcesReleased = TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureGraph --
 *
 *      Allocates resources for the graph.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *
 * Results:
 *      TCL_OK if the graph was configured successfully.
 *      TCL_ERROR if a retained option value or replacement resource
 *      could not be prepared.
 *
 * Side effects:
 *      Configuration information, such as text string, colors, font,
 *      etc. get set for graphPtr;  old resources get freed, if there
 *      were any.  The graph is redisplayed.
 *
 *----------------------------------------------------------------------
 */
static int ConfigureGraph(Graph *graphPtr) {
    GraphBarModeTransaction barModeTransaction;
    GraphPaddingTransaction paddingTransaction;
    GraphPixelTransaction pixelTransaction;
    GraphShadowTransaction shadowTransaction;
    GraphTileTransaction tileTransaction;
    GraphPolarAngleTicksTransaction polarAngleTicksTransaction;
    int barModeTransactionPrepared;
    int paddingTransactionPrepared;
    int pixelTransactionPrepared;
    int shadowTransactionPrepared;
    int tileTransactionPrepared;
    int polarAngleTicksTransactionPrepared;
    int invertXYModified;
    int layoutModified;
    int plotBackgroundModified;
    int polarLabelsModified;
    int representationModified;
    int smithGridModified;
    int polarAngleTicksModified;
    XColor *colorPtr;
    GC newGC;
    XGCValues gcValues;
    unsigned long gcMask;

    assert(graphPtr->optionsInitialized);
    assert(graphPtr->optionTable != NULL);    
    memset(&barModeTransaction, 0, sizeof(barModeTransaction));
    memset(&paddingTransaction, 0, sizeof(paddingTransaction));
    memset(&pixelTransaction, 0, sizeof(pixelTransaction));
    memset(&shadowTransaction, 0, sizeof(shadowTransaction));
    memset(&tileTransaction, 0, sizeof(tileTransaction));
    memset(&polarAngleTicksTransaction, 0, sizeof(polarAngleTicksTransaction));
    barModeTransactionPrepared = FALSE;
    paddingTransactionPrepared = FALSE;
    pixelTransactionPrepared = FALSE;
    shadowTransactionPrepared = FALSE;
    tileTransactionPrepared = FALSE;
    polarAngleTicksTransactionPrepared = FALSE;
    /*
     * TK_OPTION_DOUBLE accepts the numeric value itself, but these
     * options participate directly in layout and graph-coordinate
     * arithmetic and must therefore be finite.
     */
    if (!FINITE(graphPtr->aspect)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-aspect must be a finite value", -1));
        goto error;
    }
    if (!FINITE(graphPtr->barWidth)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-barwidth must be a finite value", -1));
        goto error;
    }
    if (!FINITE(graphPtr->baseline)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-baseline must be a finite value", -1));
        goto error;
    }
    if ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_PIXELS_MASK)) {
        if (PrepareGraphPixelTransaction(graphPtr, &pixelTransaction) != TCL_OK) {
            goto error;
        }
        pixelTransactionPrepared = TRUE;
    }
    if ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_PADDING_MASK)) {
        if (PrepareGraphPaddingTransaction(graphPtr, &paddingTransaction) != TCL_OK) {
            goto error;
        }
        paddingTransactionPrepared = TRUE;
    }
    if ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_BAR_MODE_MASK)) {
        if (PrepareGraphBarModeTransaction(graphPtr, &barModeTransaction) != TCL_OK) {
            goto error;
        }
        barModeTransactionPrepared = TRUE;
    }
    if ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_SHADOW_MASK)) {
        if (PrepareGraphShadowTransaction(graphPtr, &shadowTransaction) != TCL_OK) {
            goto error;
        }
        shadowTransactionPrepared = TRUE;
    }
    if ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_TILE_MASK)) {
        if (PrepareGraphTileTransaction(graphPtr, &tileTransaction) != TCL_OK) {
            goto error;
        }
        tileTransactionPrepared = TRUE;
    }
    if ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_POLAR_ANGLE_TICKS_MASK)) {
        if (PrepareGraphPolarAngleTicksTransaction(graphPtr, &polarAngleTicksTransaction) != TCL_OK) {
            goto error;
        }
        polarAngleTicksTransactionPrepared = TRUE;
    }
    /*
     * No operation below this point can report a configuration error.
     */
    if (pixelTransactionPrepared) {
        CommitGraphPixelTransaction(graphPtr, &pixelTransaction);
    }
    if (paddingTransactionPrepared) {
        CommitGraphPaddingTransaction(graphPtr, &paddingTransaction);
    }
    if (barModeTransactionPrepared) {
        CommitGraphBarModeTransaction(graphPtr, &barModeTransaction);
    }
    if (shadowTransactionPrepared) {
        CommitGraphShadowTransaction(graphPtr, &shadowTransaction);
    }
    if (tileTransactionPrepared) {
        CommitGraphTileTransaction(graphPtr, &tileTransaction);
    }
    if (polarAngleTicksTransactionPrepared) {
        CommitGraphPolarAngleTicksTransaction(graphPtr, &polarAngleTicksTransaction);
    }

    invertXYModified = ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_INVERT_XY_MASK));
    layoutModified = ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_LAYOUT_MASK));
    plotBackgroundModified = ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_PLOT_BACKGROUND_MASK));
    polarLabelsModified = ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_POLAR_LABEL_MASK));
    representationModified =
        ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_POLAR_REPRESENTATION_MASK));
    smithGridModified = ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_SMITH_GRID_MASK));
    polarAngleTicksModified = ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_POLAR_ANGLE_TICKS_MASK));
    /*
     * Preserve the historical normalisation behaviour for -barwidth.
     */
    if (graphPtr->barWidth <= 0.0) {
        graphPtr->barWidth = 0.1;
    }
    graphPtr->inset = GraphLayoutInt((Tcl_WideInt)graphPtr->borderWidth + (Tcl_WideInt)graphPtr->highlightWidth + 1);
    if ((graphPtr->reqHeight != Tk_ReqHeight(graphPtr->tkwin)) ||
        (graphPtr->reqWidth != Tk_ReqWidth(graphPtr->tkwin))) {
        Tk_GeometryRequest(graphPtr->tkwin, graphPtr->reqWidth, graphPtr->reqHeight);
    }
    Tk_SetInternalBorder(graphPtr->tkwin, graphPtr->borderWidth);
    colorPtr = Tk_3DBorderColor(graphPtr->border);
    if (graphPtr->title != NULL) {
        int w, h;

        Rbc_GetTextExtents(&graphPtr->titleTextStyle, graphPtr->title, &w, &h);
        graphPtr->titleTextStyle.height = GraphLayoutInt((Tcl_WideInt)h + 10);
    } else {
        graphPtr->titleTextStyle.width = graphPtr->titleTextStyle.height = 0;
    }
    /*
     * Create GCs for interior and exterior regions, and a background
     * GC for clearing the margins with XFillRectangle
     */

    /* Margin GC */
    gcValues.foreground = graphPtr->titleTextStyle.color->pixel;
    gcValues.background = colorPtr->pixel;
    gcMask = (GCForeground | GCBackground);
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (graphPtr->drawGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->drawGC);
    }
    graphPtr->drawGC = newGC;
    /* Plot fill GC (Background = Foreground) */
    gcValues.foreground = graphPtr->plotBg->pixel;
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (graphPtr->plotFillGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->plotFillGC);
    }
    graphPtr->plotFillGC = newGC;
    /* Margin fill GC (Background = Foreground) */
    gcValues.foreground = colorPtr->pixel;
    gcValues.background = graphPtr->titleTextStyle.color->pixel;
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (graphPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->fillGC);
    }
    graphPtr->fillGC = newGC;
    if (graphPtr->tile != NULL) {
        Rbc_SetTileChangedProc(graphPtr->tile, TileChangedProc, graphPtr);
    }
    Rbc_ResetTextStyle(graphPtr->tkwin, &graphPtr->titleTextStyle);
    if (invertXYModified) {
        /*
         * If the -inverted option changed, we need to readjust the pointers
         * to the axes and recompute the their scales.
         */
        AdjustAxisPointers(graphPtr);
        graphPtr->flags |= RESET_AXES;
    }
    if ((!graphPtr->backingStore) && (graphPtr->backPixmap != None)) {
        /*
         * Free the pixmap if we're not buffering the display of elements
         * anymore.
         */
        Tk_FreePixmap(graphPtr->display, graphPtr->backPixmap);
        graphPtr->backPixmap = None;
    }
    /*
     * Reconfigure the crosshairs, just in case the background color of
     * the plotarea has been changed.
     */
    Rbc_ConfigureCrosshairs(graphPtr);
    /*
     *  Update the layout of the graph (and redraw the elements) if
     *  any of the following graph options which affect the size of
     *    the plotting area has changed.
     *
     *        -aspect
     *      -borderwidth, -plotborderwidth
     *        -font, -title
     *        -width, -height
     *        -invertxy
     *        -bottommargin, -leftmargin, -rightmargin, -topmargin,
     *        -barmode, -barwidth
     */
    if (layoutModified || representationModified || smithGridModified || polarAngleTicksModified) {
        graphPtr->flags |= RESET_WORLD;
    }
    if (plotBackgroundModified || polarLabelsModified || representationModified || smithGridModified ||
        polarAngleTicksModified) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    graphPtr->flags |= REDRAW_WORLD;
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;

error:
    if (shadowTransactionPrepared) {
        FreeGraphShadowTransaction(&shadowTransaction);
    }
    if (tileTransactionPrepared) {
        FreeGraphTileTransaction(&tileTransaction);
    }
    if (polarAngleTicksTransactionPrepared) {
        FreeGraphPolarAngleTicksTransaction(&polarAngleTicksTransaction);
    }
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyGraph --
 *
 *      This procedure is invoked by Tcl_EventuallyFree or Tcl_Release
 *      to clean up the internal structure of a graph at a safe time
 *      (when no-one is using it anymore).
 *
 * Parameters:
 *      DestroyData dataPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Everything associated with the widget is freed up.
 *
 *----------------------------------------------------------------------
 */
static void DestroyGraph(DestroyData dataPtr) {
    Graph *graphPtr = (Graph *)dataPtr;

    /*
     * A creation failure may reach the concrete destructor before the
     * window destruction callbacks have released the graph options.
     */
    if (graphPtr->optionsInitialized) {
        assert(graphPtr->tkwin != NULL);
        ReleaseGraphOptionResources(graphPtr);
    }
    /*
     * Destroy the individual components of the graph: elements, markers,
     * X and Y axes, legend, display lists etc.
     */
    Rbc_DestroyMarkers(graphPtr);
    Rbc_DestroyElements(graphPtr);
    if (graphPtr->gridPtr != NULL) {
        Rbc_DestroyGrid(graphPtr);
    }
    Rbc_DestroyAxes(graphPtr);
    Rbc_DestroyPens(graphPtr);

    if (graphPtr->postscript != NULL) {
        Rbc_DestroyPostScript(graphPtr);
    }
    if (graphPtr->crosshairs != NULL) {
        Rbc_DestroyCrosshairs(graphPtr);
    }
    if (graphPtr->bindTable != NULL) {
        Rbc_DestroyBindingTable(graphPtr->bindTable);
    }
    if (graphPtr->legend != NULL) {
        Rbc_DestroyLegend(graphPtr);
    }
    /* Release allocated X resources and memory. */
    if (graphPtr->drawGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->drawGC);
    }
    if (graphPtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->fillGC);
    }
    if (graphPtr->plotFillGC != NULL) {
        Tk_FreeGC(graphPtr->display, graphPtr->plotFillGC);
    }
    Rbc_FreeTextStyle(graphPtr->display, &graphPtr->titleTextStyle);
    if (graphPtr->backPixmap != None) {
        Tk_FreePixmap(graphPtr->display, graphPtr->backPixmap);
    }
    if (graphPtr->freqArr != NULL) {
        ckfree((char *)graphPtr->freqArr);
    }
    if (graphPtr->nStacks > 0) {
        Tcl_DeleteHashTable(&graphPtr->freqTable);
    }
    if (graphPtr->tile != NULL) {
        Rbc_FreeTile(graphPtr->tile);
    }
    if (graphPtr->angleMajorTicks != NULL) {
        ckfree(graphPtr->angleMajorTicks);
        graphPtr->angleMajorTicks = NULL;
    }
    if (graphPtr->angleMinorTicks != NULL) {
        ckfree(graphPtr->angleMinorTicks);
        graphPtr->angleMinorTicks = NULL;
    }
    ckfree((char *)graphPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * CreateGraph --
 *
 *      This procedure creates and initializes a new widget.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *      Rbc_Uid classUid
 *
 * Results:
 *      The return value is a pointer to a structure describing
 *      the new widget.  If an error occurred, then the return
 *      value is NULL and an error message is left in the
 *      interpreter result.
 *
 * Side effects:
 *      Memory is allocated, a Tk_Window is created, etc.
 *
 *----------------------------------------------------------------------
 */
static Graph *CreateGraph(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid classUid) {
    Graph *graphPtr;
    Tk_Window tkwin;
    const char *pathName = Tcl_GetString(objv[1]);

    tkwin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp), pathName, NULL);
    if (tkwin == NULL) {
        return NULL;
    }
    graphPtr = RbcCalloc(1, sizeof(Graph));
    assert(graphPtr);
    /*
     * Initialize the graph data structure.
     */
    graphPtr->tkwin = tkwin;
    graphPtr->display = Tk_Display(tkwin);
    graphPtr->interp = interp;
    graphPtr->classUid = classUid;
    /*
     * Graph option lifecycle state.
     */
    graphPtr->optionTable = NULL;
    ResetGraphOptionContext(graphPtr);
    graphPtr->optionsConfigured = FALSE;
    graphPtr->optionsInitialized = FALSE;
    graphPtr->tkResourcesReleased = FALSE;
    graphPtr->cmdToken = NULL;
    graphPtr->backingStore = TRUE;
    graphPtr->doubleBuffer = TRUE;
    graphPtr->highlightWidth = 2;
    graphPtr->plotRelief = TK_RELIEF_SUNKEN;
    graphPtr->relief = TK_RELIEF_FLAT;
    graphPtr->flags = RESET_WORLD;
    graphPtr->nextMarkerId = 1;
    graphPtr->padLeft = graphPtr->padRight = 8;
    graphPtr->padTop = graphPtr->padBottom = 8;
    graphPtr->bottomMargin.site = MARGIN_BOTTOM;
    graphPtr->leftMargin.site = MARGIN_LEFT;
    graphPtr->topMargin.site = MARGIN_TOP;
    graphPtr->rightMargin.site = MARGIN_RIGHT;
    Rbc_InitTextStyle(&graphPtr->titleTextStyle);
    Tcl_InitHashTable(&graphPtr->axes.table, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graphPtr->axes.tagTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graphPtr->elements.table, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graphPtr->elements.tagTable, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graphPtr->markers.table, TCL_STRING_KEYS);
    Tcl_InitHashTable(&graphPtr->markers.tagTable, TCL_STRING_KEYS);
    graphPtr->elements.displayList = Rbc_ChainCreate();
    graphPtr->markers.displayList = Rbc_ChainCreate();
    graphPtr->axes.displayList = Rbc_ChainCreate();
    if (classUid == rbcLineElementUid) {
        Tk_SetClass(tkwin, "Graph");
    } else if (classUid == rbcBarElementUid) {
        Tk_SetClass(tkwin, "Barchart");
    } else if (classUid == rbcStripElementUid) {
        Tk_SetClass(tkwin, "Stripchart");
    } else if (classUid == rbcPolarElementUid) {
        Tk_SetClass(tkwin, "Polar");
    }
    Rbc_SetWindowInstanceData(tkwin, graphPtr);
    /*
     * Establish the normal Tk widget lifecycle before performing any
     * initialization that may fail.  In particular, initial configuration
     * may request geometry changes and schedule redisplay.
     */
    graphPtr->cmdToken =
        Tcl_CreateObjCommand2(interp, pathName, Rbc_GraphInstCmdProc, graphPtr, GraphInstCmdDeleteProc);
#ifdef ITCL_NAMESPACES
    Itk_SetWidgetCommand(graphPtr->tkwin, graphPtr->cmdToken);
#endif
    Tk_CreateEventHandler(graphPtr->tkwin, ExposureMask | StructureNotifyMask | FocusChangeMask, GraphEventProc,
                          graphPtr);
    /*
     * Create the graph's component objects before configuring the graph.
     * ConfigureGraph requires several of these components, particularly
     * the crosshairs, to already exist.
     */
    if (InitPens(graphPtr) != TCL_OK) {
        goto error;
    }
    if (Rbc_DefaultAxes(graphPtr) != TCL_OK) {
        goto error;
    }
    AdjustAxisPointers(graphPtr);
    if (Rbc_CreatePostScript(graphPtr) != TCL_OK) {
        goto error;
    }
    if (Rbc_CreateCrosshairs(graphPtr) != TCL_OK) {
        goto error;
    }
    if (Rbc_CreateLegend(graphPtr) != TCL_OK) {
        goto error;
    }
    if (Rbc_CreateGrid(graphPtr) != TCL_OK) {
        goto error;
    }
    /*
     * Initialise the modern graph option table, apply defaults,
     * option-database values and creation-time arguments, and construct
     * all derived graph resources.
     *
     * The event handler is already installed, so geometry and redraw
     * activity generated during initial configuration participates in
     * the normal widget event lifecycle.
     */
    if (ConfigureNewGraph(graphPtr, objc - 2, objv + 2) != TCL_OK) {
        goto error;
    }
    graphPtr->bindTable = Rbc_CreateBindingTable(interp, tkwin, graphPtr, PickEntry, Rbc_GraphTags);
    return graphPtr;

error:
    /*
     * The widget command and event handler are already installed.
     * Destroy the Tk window and let GraphEventProc perform the normal
     * resource-release and deferred Graph destruction sequence.
     */
    Tk_DestroyWindow(graphPtr->tkwin);
    return NULL;
}

/* Widget sub-commands */
/*
 *----------------------------------------------------------------------
 *
 * XAxisOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int XAxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int margin;

    margin = (graphPtr->inverted) ? MARGIN_LEFT : MARGIN_BOTTOM;
    return Rbc_AxisOp(graphPtr, margin, objc, objv);
}

/*
 *----------------------------------------------------------------------
 *
 * X2AxisOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int X2AxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int margin;

    margin = (graphPtr->inverted) ? MARGIN_RIGHT : MARGIN_TOP;
    return Rbc_AxisOp(graphPtr, margin, objc, objv);
}

/*
 *----------------------------------------------------------------------
 *
 * YAxisOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int YAxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int margin;

    margin = (graphPtr->inverted) ? MARGIN_BOTTOM : MARGIN_LEFT;
    return Rbc_AxisOp(graphPtr, margin, objc, objv);
}

/*
 *----------------------------------------------------------------------
 *
 * Y2AxisOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int Y2AxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int margin;

    margin = (graphPtr->inverted) ? MARGIN_TOP : MARGIN_RIGHT;
    return Rbc_AxisOp(graphPtr, margin, objc, objv);
}

/*
 *----------------------------------------------------------------------
 *
 * BarOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int BarOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_ElementOp(graphPtr, interp, objc, objv, rbcBarElementUid);
}

/*
 *----------------------------------------------------------------------
 *
 * LineOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int LineOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_ElementOp(graphPtr, interp, objc, objv, rbcLineElementUid);
}

/*
 *----------------------------------------------------------------------
 *
 * ElementOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ElementOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_ElementOp(graphPtr, interp, objc, objv, graphPtr->classUid);
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ConfigureOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *infoObjPtr;

    assert(graphPtr->optionTable != NULL);
    assert(graphPtr->optionsInitialized);

    if (objc == 2) {
        infoObjPtr = Tk_GetOptionInfo(interp, (char *)graphPtr, graphPtr->optionTable, NULL, graphPtr->tkwin);

        if (infoObjPtr == NULL) {
            return TCL_ERROR;
        }

        Tcl_SetObjResult(interp, infoObjPtr);

        return TCL_OK;
    }

    if (objc == 3) {
        infoObjPtr = Tk_GetOptionInfo(interp, (char *)graphPtr, graphPtr->optionTable, objv[2], graphPtr->tkwin);

        if (infoObjPtr == NULL) {
            return TCL_ERROR;
        }

        Tcl_SetObjResult(interp, infoObjPtr);

        return TCL_OK;
    }

    return ConfigureGraphOptions(graphPtr, objc - 2, objv + 2, NULL);
}

/*
 *----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc - Not used.
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int CgetOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *valueObjPtr;

    assert(graphPtr->optionTable != NULL);
    assert(graphPtr->optionsInitialized);

    valueObjPtr = Tk_GetOptionValue(interp, (char *)graphPtr, graphPtr->optionTable, objv[2], graphPtr->tkwin);

    if (valueObjPtr == NULL) {
        return TCL_ERROR;
    }

    Tcl_SetObjResult(interp, valueObjPtr);

    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * ExtentsOp --
 *
 *      Reports the size of one of several items within the graph.
 *      The following are valid items:
 *
 *        "bottommargin"    Height of the bottom margin
 *        "leftmargin"      Width of the left margin
 *        "legend"          x y w h of the legend
 *        "plotarea"        x y w h of the plotarea
 *        "plotheight"      Height of the plot area
 *        "rightmargin"     Width of the right margin
 *        "topmargin"       Height of the top margin
 *        "plotwidth"       Width of the plot area
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc - Not used.
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Always returns TCL_OK.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int ExtentsOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const char *const extentOps[] = {"plotheight", "plotwidth",   "plotarea",  "legend",
                                            "leftmargin", "rightmargin", "topmargin", "bottommargin"};
    static enum {
        EXT_PLOTHEIGHT,
        EXT_PLOTWIDTH,
        EXT_PLOTAREA,
        EXT_LEGEND,
        EXT_LEFTMARGIN,
        EXT_RIGHTMARGIN,
        EXT_TOPMARGIN,
        EXT_BOTTOMMARGIN
    } index;

    if (Tcl_GetIndexFromObj(interp, objv[2], extentOps, "extent item", 0, &index) != TCL_OK) {
        return TCL_ERROR;
    }

    switch (index) {
    case EXT_PLOTHEIGHT:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graphPtr->bottom - graphPtr->top + 1));
        break;
    case EXT_PLOTWIDTH:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graphPtr->right - graphPtr->left + 1));
        break;
    case EXT_PLOTAREA:
        Tcl_SetObjResult(interp,
                         Tcl_ObjPrintf("%d %d %d %d", graphPtr->left, graphPtr->top,
                                       graphPtr->right - graphPtr->left + 1, graphPtr->bottom - graphPtr->top + 1));
        break;
    case EXT_LEGEND:
        Tcl_SetObjResult(interp,
                         Tcl_ObjPrintf("%d %d %d %d", Rbc_LegendX(graphPtr->legend), Rbc_LegendY(graphPtr->legend),
                                       Rbc_LegendWidth(graphPtr->legend), Rbc_LegendHeight(graphPtr->legend)));
        break;
    case EXT_LEFTMARGIN:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graphPtr->leftMargin.width));
        break;
    case EXT_RIGHTMARGIN:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graphPtr->rightMargin.width));
        break;
    case EXT_TOPMARGIN:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graphPtr->topMargin.height));
        break;
    case EXT_BOTTOMMARGIN:
        Tcl_SetObjResult(interp, Tcl_NewIntObj(graphPtr->bottomMargin.height));
        break;
    }

    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * InsideOp --
 *
 *      Returns true of false whether the given point is inside
 *      the plotting area (defined by left,bottom right, top).
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Always returns TCL_OK.  The interpreter result will contain
 *      the boolean string representation.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int InsideOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int x, y;
    Extents2D exts;
    int result;

    if (Tk_GetPixelsFromObj(interp, graphPtr->tkwin, objv[2], &x) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tk_GetPixelsFromObj(interp, graphPtr->tkwin, objv[3], &y) != TCL_OK) {
        return TCL_ERROR;
    }
    Rbc_GraphExtents(graphPtr, &exts);
    result = PointInRegion(&exts, x, y);
    Tcl_SetObjResult(interp, Tcl_NewBooleanObj(result));
    return TCL_OK;
}

/*
 * -------------------------------------------------------------------------
 *
 * InvtransformOp --
 *
 *      This procedure returns a list of the graph coordinate
 *      values corresponding with the given window X and Y
 *      coordinate positions.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc - Not used.
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result.  If an error occurred while
 *      parsing the window positions, TCL_ERROR is returned, and
 *      the interpreter result will contain the error message.
 *      Otherwise, the interpreter result will contain a Tcl list
 *      of the x and y coordinates.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ------------------------------------------------------------------------
 */
static int InvtransformOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    double x, y;
    Point2D point;
    Axis2D axes;
    Tcl_Obj *resultObj[2];

    if (Tcl_ExprDoubleObj(interp, objv[2], &x) != TCL_OK || Tcl_ExprDoubleObj(interp, objv[3], &y) != TCL_OK) {
        return TCL_ERROR;
    }
    if (graphPtr->flags & RESET_AXES) {
        Rbc_ResetAxes(graphPtr);
    }
    /* Perform the reverse transformation, converting from window
     * coordinates to graph data coordinates.  Note that the point is
     * always mapped to the bottom and left axes (which may not be
     * what the user wants).  */

    /*  Pick the first pair of axes */
    axes.x = Rbc_GetFirstAxis(graphPtr->axisChain[0]);
    axes.y = Rbc_GetFirstAxis(graphPtr->axisChain[1]);
    point = Rbc_InvMap2D(graphPtr, x, y, &axes);

    resultObj[0] = Tcl_NewDoubleObj(point.x);
    resultObj[1] = Tcl_NewDoubleObj(point.y);
    Tcl_SetObjResult(interp, Tcl_NewListObj(2, resultObj));
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * TransformOp --
 *
 *      This procedure returns a list of the window coordinates
 *      corresponding with the given graph x and y coordinates.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      int objc - Not used.
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result. The interpreter result contains
 *      the list of the graph coordinates. If an error occurred
 *      while parsing the window positions, TCL_ERROR is returned,
 *      then the interpreter result will contain an error message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -------------------------------------------------------------------------
 */
static int TransformOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    double x, y;
    Point2D point;
    Axis2D axes;
    Tcl_Obj *resultObj[2];

    if (Tcl_ExprDoubleObj(interp, objv[2], &x) != TCL_OK || Tcl_ExprDoubleObj(interp, objv[3], &y) != TCL_OK) {
        return TCL_ERROR;
    }
    if (graphPtr->flags & RESET_AXES) {
        Rbc_ResetAxes(graphPtr);
    }
    /*
     * Perform the transformation from window to graph coordinates.
     * Note that the points are always mapped onto the bottom and left
     * axes (which may not be the what the user wants).
     */
    axes.x = Rbc_GetFirstAxis(graphPtr->axisChain[0]);
    axes.y = Rbc_GetFirstAxis(graphPtr->axisChain[1]);

    point = Rbc_Map2D(graphPtr, x, y, &axes);

    resultObj[0] = Tcl_NewIntObj(ROUND(point.x));
    resultObj[1] = Tcl_NewIntObj(ROUND(point.y));
    Tcl_SetObjResult(interp, Tcl_NewListObj(2, resultObj));
    return TCL_OK;
}

#ifdef WIN32
/*
 *----------------------------------------------------------------------
 *
 * InitMetaFileHeader --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin
 *      int width
 *      int height
 *      APMHEADER *mfhPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int InitMetaFileHeader(Tk_Window tkwin, int width, int height, APMHEADER *mfhPtr) {
    unsigned int *p;
    unsigned int sum;
    Screen *screen;
#define MM_INCH 25.4
    double dpiX, dpiY;

    mfhPtr->key = 0x9ac6cdd7L;
    mfhPtr->hmf = 0;
    mfhPtr->inch = 1440;

    screen = Tk_Screen(tkwin);
    dpiX = (WidthOfScreen(screen) * MM_INCH) / WidthMMOfScreen(screen);
    dpiY = (HeightOfScreen(screen) * MM_INCH) / HeightMMOfScreen(screen);

    mfhPtr->bbox.Left = mfhPtr->bbox.Top = 0;
    mfhPtr->bbox.Bottom = (SHORT)((width * 1440) / dpiX);
    mfhPtr->bbox.Right = (SHORT)((height * 1440) / dpiY);
    mfhPtr->reserved = 0;
    sum = 0;
    for (p = (unsigned int *)mfhPtr; p < (unsigned int *)&(mfhPtr->checksum); p++) {
        sum ^= *p;
    }
    mfhPtr->checksum = sum;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CreateAPMetaFile --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      HANDLE hMetaFile
 *      HDC hDC
 *      APMHEADER *mfhPtr
 *      const char *fileName
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int CreateAPMetaFile(Tcl_Interp *interp, HANDLE hMetaFile, HDC hDC, APMHEADER *mfhPtr, const char *fileName) {
    HANDLE hFile;
    HANDLE hMem;
    LPVOID buffer;
    int result;
    DWORD count, nBytes;

    result = TCL_ERROR;
    hMem = NULL;
    hFile = CreateFileA(fileName,                     /* File path */
                        GENERIC_WRITE,                /* Access mode */
                        0,                            /* No sharing. */
                        NULL,                         /* Security attributes */
                        CREATE_ALWAYS,                /* Overwrite any existing file */
                        FILE_ATTRIBUTE_NORMAL, NULL); /* No template file */
    if (hFile == INVALID_HANDLE_VALUE) {
        Rbc_AppendResultStrings(interp, "can't create metafile \"", fileName, "\":", Rbc_LastError(), (char *)NULL);
        return TCL_ERROR;
    }
    if ((!WriteFile(hFile, (LPVOID)mfhPtr, sizeof(APMHEADER), &count, NULL)) || (count != sizeof(APMHEADER))) {
        Rbc_AppendResultStrings(interp, "can't create metafile header to \"", fileName, "\":", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    nBytes = GetWinMetaFileBits(hMetaFile, 0, NULL, MM_ANISOTROPIC, hDC);
    hMem = GlobalAlloc(GHND, nBytes);
    if (hMem == NULL) {
        Rbc_AppendResultStrings(interp, "can't create allocate global memory:", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    buffer = (LPVOID)GlobalLock(hMem);
    if (!GetWinMetaFileBits(hMetaFile, nBytes, buffer, MM_ANISOTROPIC, hDC)) {
        Rbc_AppendResultStrings(interp, "can't get metafile bits:", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    if ((!WriteFile(hFile, buffer, nBytes, &count, NULL)) || (count != nBytes)) {
        Rbc_AppendResultStrings(interp, "can't write metafile bits:", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    result = TCL_OK;
error:
    CloseHandle(hFile);
    if (hMem != NULL) {
        GlobalUnlock(hMem);
        GlobalFree(hMem);
    }
    return result;
}
#endif /*WIN32*/

/*
 * --------------------------------------------------------------------------
 *
 * SnapOp --
 *
 *      Snaps a picture of the graph and stores it in the specified image
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tcl_Interp *interp
 *      Tcl_Size objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result. The interpreter result contains
 *      the list of the graph coordinates. If an error occurred
 *      while parsing the window positions, TCL_ERROR is returned,
 *      then the interpreter result will contain an error message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -------------------------------------------------------------------------
 */
static int SnapOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int result;
    Pixmap drawable;
    int noBackingStore = 0;
    SnapData data;
    enum SnapFormatE { FORMAT_PHOTO, FORMAT_EMF, FORMAT_WMF };
    static const struct SnapFormatS {
        const char *name;
        enum SnapFormatE format;
    } snapFormatOpts[] = {
#ifdef WIN32
        {"emf", FORMAT_EMF},
#endif
        {"photo", FORMAT_PHOTO},
#ifdef WIN32
        {"wmf", FORMAT_WMF},
#endif
        {NULL, 0}};
    static const char *const optNames[] = {"-format", "-height", "-width", NULL};
    enum { OPT_FORMAT, OPT_HEIGHT, OPT_WIDTH };
    Tcl_Size i;
    int index;
    int optidx;

    if ((objc % 2) != 1) {
        Tcl_WrongNumArgs(interp, 2, objv, "name ?-option value ...?");
        return TCL_ERROR;
    }

    /* .g snap name ?switches? */
    data.height = 0;
    data.width = 0;
    data.format = FORMAT_PHOTO;
    data.name = Tcl_GetString(objv[2]);

    if (objc > 3) {
        for (i = 3; i < objc; i += 2) {
            if (Tcl_GetIndexFromObj(interp, objv[i], optNames, "option", 0, &index) != TCL_OK) {
                return TCL_ERROR;
            }
            switch (index) {
            case OPT_FORMAT:
                if (Tcl_GetIndexFromObjStruct(interp, objv[i + 1], snapFormatOpts, sizeof(struct SnapFormatS), "format",
                                              0, &optidx) != TCL_OK) {
                    return TCL_ERROR;
                }
                data.format = snapFormatOpts[optidx].format;
                break;
            case OPT_HEIGHT:
                if (Tcl_GetIntFromObj(interp, objv[i + 1], &data.height)) {
                    return TCL_ERROR;
                }
                break;
            case OPT_WIDTH:
                if (Tcl_GetIntFromObj(interp, objv[i + 1], &data.width)) {
                    return TCL_ERROR;
                }
                break;
            }
        }
    }

    if (data.width < 2) {
        data.width = Tk_Width(graphPtr->tkwin);
    }
    if (data.height < 2) {
        data.height = Tk_Height(graphPtr->tkwin);
    }
    /* Always re-compute the layout of the graph before snapping the photo. */
    graphPtr->width = data.width;
    graphPtr->height = data.height;
    Rbc_LayoutGraph(graphPtr);

    drawable = Tk_WindowId(graphPtr->tkwin);
    if (data.format == FORMAT_PHOTO) {
        drawable =
            Tk_GetPixmap(graphPtr->display, drawable, graphPtr->width, graphPtr->height, Tk_Depth(graphPtr->tkwin));
#ifdef WIN32
        assert(drawable != None);
#endif
        graphPtr->flags |= RESET_WORLD;
        Rbc_DrawGraph(graphPtr, drawable, noBackingStore);
        result = Rbc_SnapPhoto(interp, graphPtr->tkwin, drawable, 0, 0, data.width, data.height, data.width,
                               data.height, data.name, 1.0);
        Tk_FreePixmap(graphPtr->display, drawable);
#ifdef WIN32
    } else if ((data.format == FORMAT_WMF) || (data.format == FORMAT_EMF)) {
        Drawable metaDrawable;
        HWND hWnd;
        HDC hRefDC;
        HDC hDC;
        HENHMETAFILE hMetaFile;
        Tcl_DString dString;
        char *title;

        hWnd = Tk_GetHWND(drawable);
        hRefDC = GetDC(hWnd);

        Tcl_DStringInit(&dString);
        Tcl_DStringAppend(&dString, "RBC Graph ", -1);
        Tcl_DStringAppend(&dString, RBC_VERSION, -1);
        Tcl_DStringAppend(&dString, "\0", -1);
        Tcl_DStringAppend(&dString, Tk_PathName(graphPtr->tkwin), -1);
        Tcl_DStringAppend(&dString, "\0", -1);
        title = Tcl_DStringValue(&dString);
        hDC = CreateEnhMetaFileA(hRefDC, NULL, NULL, title);
        Tcl_DStringFree(&dString);

        if (hDC == NULL) {
            ReleaseDC(hWnd, hRefDC);
            Rbc_AppendResultStrings(interp, "can't create metafile: ", Rbc_LastError(), (char *)NULL);
            return TCL_ERROR;
        }

        metaDrawable = Rbc_WinCreateDrawableFromDC(hDC);

        Rbc_LayoutGraph(graphPtr);
        graphPtr->flags |= RESET_WORLD;
        Rbc_DrawGraph(graphPtr, metaDrawable, FALSE);
        Rbc_WinFreeDrawableFromDC(metaDrawable);
        hMetaFile = CloseEnhMetaFile(hDC);
        if (strcmp(data.name, "CLIPBOARD") == 0) {
            OpenClipboard(hWnd);
            EmptyClipboard();
            SetClipboardData(CF_ENHMETAFILE, hMetaFile);
            CloseClipboard();
            result = TCL_OK;
        } else {
            result = TCL_ERROR;
            if (data.format == FORMAT_WMF) {
                APMHEADER mfh;

                assert(sizeof(mfh) == 22);
                InitMetaFileHeader(graphPtr->tkwin, data.width, data.height, &mfh);
                result = CreateAPMetaFile(interp, hMetaFile, hRefDC, &mfh, data.name);
            } else {
                HENHMETAFILE hMetaFile2;

                hMetaFile2 = CopyEnhMetaFileA(hMetaFile, data.name);
                if (hMetaFile2 != NULL) {
                    result = TCL_OK;
                    DeleteEnhMetaFile(hMetaFile2);
                }
            }
            DeleteEnhMetaFile(hMetaFile);
        }
        ReleaseDC(hWnd, hRefDC);
#endif /*WIN32*/
    } else {
        Rbc_AppendResultStrings(interp, "bad snapshot format", (char *)NULL);
        return TCL_ERROR;
    }
    graphPtr->flags = MAP_WORLD;
    Rbc_EventuallyRedrawGraph(graphPtr);
    return result;
}

static const GraphOpSpec graphOps[] = {{{"axis", 2, 0, "oper ?args?"}, Rbc_VirtualAxisOp},
                                      {{"bar", 2, 0, "oper ?args?"}, BarOp},
                                      {{"cget", 3, 3, "option"}, CgetOp},
                                      {{"configure", 2, 0, "?option value?..."}, ConfigureOp},
                                      {{"crosshairs", 2, 0, "oper ?args?"}, Rbc_CrosshairsOp},
                                      {{"element", 2, 0, "oper ?args?"}, ElementOp},
                                      {{"extents", 3, 3, "item"}, ExtentsOp},
                                      {{"grid", 2, 0, "oper ?args?"}, Rbc_GridOp},
                                      {{"inside", 4, 4, "winX winY"}, InsideOp},
                                      {{"invtransform", 4, 4, "winX winY"}, InvtransformOp},
                                      {{"legend", 2, 0, "oper ?args?"}, Rbc_LegendOp},
                                      {{"line", 2, 0, "oper ?args?"}, LineOp},
                                      {{"marker", 2, 0, "oper ?args?"}, Rbc_MarkerOp},
                                      {{"pen", 2, 0, "oper ?args?"}, Rbc_PenOp},
                                      {{"postscript", 2, 0, "oper ?args?"}, Rbc_PostScriptOp},
                                      {{"snap", 3, 0, "name ?-option value ...?"}, SnapOp},
                                      {{"transform", 4, 4, "x y"}, TransformOp},
                                      {{"x2axis", 2, 0, "oper ?args?"}, X2AxisOp},
                                      {{"xaxis", 2, 0, "oper ?args?"}, XAxisOp},
                                      {{"y2axis", 2, 0, "oper ?args?"}, Y2AxisOp},
                                      {{"yaxis", 2, 0, "oper ?args?"}, YAxisOp},
                                      {{NULL, 0, 0, NULL}, NULL}

};

static Rbc_GraphOpProc *GetGraphOpFromObj(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, graphOps, (Tcl_Size)sizeof(graphOps[0]), RBC_OP_ARG1, objc, objv, &index) !=
        TCL_OK) {
        return NULL;
    }

    return graphOps[index].proc;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GraphInstCmdProc --
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
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GraphInstCmdProc(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_GraphOpProc *proc;
    int result;
    Graph *graphPtr = clientData;

    proc = GetGraphOpFromObj(interp, objc, objv);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    Tcl_Preserve(graphPtr);
    result = proc(graphPtr, interp, objc, objv);
    Tcl_Release(graphPtr);
    return result;
}

/*
 * --------------------------------------------------------------------------
 *
 * NewGraph --
 *
 *      Creates a new window and Tcl command representing an
 *      instance of a graph widget.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *      Rbc_Uid classUid
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 * --------------------------------------------------------------------------
 */
static int NewGraph(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid classUid) {
    Graph *graphPtr;
    if (objc < 2) {
        Tcl_WrongNumArgs(interp, 1, objv, "pathName ?-option value ...?");
        return TCL_ERROR;
    }
    graphPtr = CreateGraph(interp, objc, objv, classUid);
    if (graphPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewStringObj(Tk_PathName(graphPtr->tkwin), -1));
    return TCL_OK;
}

/*
 * --------------------------------------------------------------------------
 *
 * GraphCmd --
 *
 *      Creates a new window and Tcl command representing an
 *      instance of a graph widget.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 * --------------------------------------------------------------------------
 */
static int GraphObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return NewGraph(interp, objc, objv, rbcLineElementUid);
}

/*
 *--------------------------------------------------------------
 *
 * BarchartCmd --
 *
 *      Creates a new window and Tcl command representing an
 *      instance of a barchart widget.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *--------------------------------------------------------------
 */
static int BarchartObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return NewGraph(interp, objc, objv, rbcBarElementUid);
}

/*
 *--------------------------------------------------------------
 *
 * StripchartCmd --
 *
 *      Creates a new window and Tcl command representing an
 *      instance of a barchart widget.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.
 *
 * Side effects:
 *      See the user documentation.
 *
 *--------------------------------------------------------------
 */
static int StripchartObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return NewGraph(interp, objc, objv, rbcStripElementUid);
}

static int PolarObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return NewGraph(interp, objc, objv, rbcPolarElementUid);
}

static void FillMarginRectangle(Graph *graphPtr, Drawable drawable, int x, int y, int width, int height) {
    if ((width <= 0) || (height <= 0)) {
        return;
    }
    if (graphPtr->tile != NULL) {
        Rbc_TileRectangle(graphPtr->tkwin, drawable, graphPtr->tile, x, y, width, height);
    } else {
        XFillRectangle(graphPtr->display, drawable, graphPtr->fillGC, x, y, (unsigned int)width, (unsigned int)height);
    }
}



/*
 * -----------------------------------------------------------------------
 *
 * DrawMargins --
 *
 *      Draws the exterior region of the graph (axes, ticks, titles, etc)
 *      onto a pixmap. The interior region is defined by the given
 *      rectangle structure.
 *
 *      ---------------------------------
 *          |                               |
 *          |           rectArr[0]          |
 *          |                               |
 *      ---------------------------------
 *          |     |top           right|     |
 *          |     |                   |     |
 *          |     |                   |     |
 *          | [1] |                   | [2] |
 *          |     |                   |     |
 *          |     |                   |     |
 *          |     |                   |     |
 *          |     |                   |     |
 *          |     |                   |     |
 *          |     |left         bottom|     |
 *      ---------------------------------
 *          |                               |
 *          |          rectArr[3]           |
 *          |                               |
 *      ---------------------------------
 *
 *          X coordinate axis
 *          Y coordinate axis
 *          legend
 *          interior border
 *          exterior border
 *          titles (X and Y axis, graph)
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *
 * Returns:
 *      None.
 *
 * Side Effects:
 *      Exterior of graph is displayed in its window.
 *
 * -----------------------------------------------------------------------
 */
static void DrawMargins(Graph *graphPtr, Drawable drawable) {
    int middleHeight;

    /*
     * Draw the four outer rectangles surrounding the plotting
     * surface.  Keep graph geometry as int all the way to the
     * native drawing boundary.
     */
    if (graphPtr->tile != NULL) {
        Rbc_SetTileOrigin(graphPtr->tkwin, graphPtr->tile, 0, 0);
    }
    middleHeight = graphPtr->bottom - graphPtr->top;
    /* Top margin. */
    FillMarginRectangle(graphPtr, drawable, 0, 0, graphPtr->width, graphPtr->top);
    /* Left margin. */
    FillMarginRectangle(graphPtr, drawable, 0, graphPtr->top, graphPtr->left, middleHeight);
    /* Right margin. */
    FillMarginRectangle(graphPtr, drawable, graphPtr->right, graphPtr->top, graphPtr->width - graphPtr->right,
                        middleHeight);
    /* Bottom margin. */
    FillMarginRectangle(graphPtr, drawable, 0, graphPtr->bottom, graphPtr->width, graphPtr->height - graphPtr->bottom);
    /*
     * Draw the 3D border around the plotting area.
     */
    if (graphPtr->plotBorderWidth > 0) {
        int x, y;
        int width, height;
        x = graphPtr->left - graphPtr->plotBorderWidth;
        y = graphPtr->top - graphPtr->plotBorderWidth;
        width = (graphPtr->right - graphPtr->left) + (2 * graphPtr->plotBorderWidth);
        height = (graphPtr->bottom - graphPtr->top) + (2 * graphPtr->plotBorderWidth);
        Rbc_Draw3DRectangle(graphPtr->tkwin, drawable, graphPtr->border, x, y, width, height, graphPtr->plotBorderWidth,
                            graphPtr->plotRelief);
    }
    if (Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_MARGIN) {
        Rbc_DrawLegend(graphPtr->legend, drawable);
    }
    if (graphPtr->title != NULL) {
        Rbc_DrawText(graphPtr->tkwin, drawable, graphPtr->title, &graphPtr->titleTextStyle, graphPtr->titleX,
                     graphPtr->titleY);
    }
    Rbc_DrawAxes(graphPtr, drawable);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawPlotRegion --
 *
 *      Draws the contents of the plotting area.  This consists of
 *      the elements, markers (draw under elements), axis limits,
 *      grid lines, and possibly the legend.  Typically, the output
 *      will be cached into a backing store pixmap, so that redraws
 *      can occur quickly.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DrawPlotRegion(Graph *graphPtr, Drawable drawable) {
    /* Clear the background of the plotting area. */
    XFillRectangle(graphPtr->display, drawable, graphPtr->plotFillGC, graphPtr->left, graphPtr->top,
                   graphPtr->right - graphPtr->left + 1, graphPtr->bottom - graphPtr->top + 1);

    /* Draw the elements, markers, legend, and axis limits. */

    if (!graphPtr->gridPtr->hidden) {
        Rbc_DrawGrid(graphPtr, drawable);
    }
    if (graphPtr->classUid == rbcPolarElementUid) {
        switch (graphPtr->representation) {
        case POLAR_REPRESENTATION_POLAR:
            Rbc_DrawPolarLabels(graphPtr, drawable);
            break;
        case POLAR_REPRESENTATION_SMITH:
            Rbc_DrawSmithLabels(graphPtr, drawable);
            break;
        }
    }
    Rbc_DrawMarkers(graphPtr, drawable, MARKER_UNDER);
    if ((Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_PLOT) && (!Rbc_LegendIsRaised(graphPtr->legend))) {
        Rbc_DrawLegend(graphPtr->legend, drawable);
    }
    Rbc_DrawAxisLimits(graphPtr, drawable);
    Rbc_DrawElements(graphPtr, drawable);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LayoutGraph --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_LayoutGraph(Graph *graphPtr) {
    if (graphPtr->flags & RESET_AXES) {
        Rbc_ResetAxes(graphPtr);
    }
    if (graphPtr->flags & LAYOUT_NEEDED) {
        Rbc_LayoutMargins(graphPtr);
        graphPtr->flags &= ~LAYOUT_NEEDED;
    }
    /* Compute coordinate transformations for graph components */
    if ((graphPtr->vRange > 1) && (graphPtr->hRange > 1)) {
        if (graphPtr->flags & MAP_WORLD) {
            Rbc_MapAxes(graphPtr);
        }
        Rbc_MapElements(graphPtr);
        Rbc_MapMarkers(graphPtr);
        Rbc_MapGrid(graphPtr);
        graphPtr->flags &= ~(MAP_ALL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DrawGraph --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *      int backingStore - If non-zero, use backing store for plotting area. 
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_DrawGraph(Graph *graphPtr, Drawable drawable, int backingStore) {
    if (backingStore) {
        /*
         * Create another pixmap to save elements if one doesn't
         * already exist or the size of the window has changed.
         */
        if ((graphPtr->backPixmap == None) || (graphPtr->backWidth != graphPtr->width) ||
            (graphPtr->backHeight != graphPtr->height)) {

            if (graphPtr->backPixmap != None) {
                Tk_FreePixmap(graphPtr->display, graphPtr->backPixmap);
            }
            graphPtr->backPixmap = Tk_GetPixmap(graphPtr->display, Tk_WindowId(graphPtr->tkwin), graphPtr->width,
                                                graphPtr->height, Tk_Depth(graphPtr->tkwin));
            graphPtr->backWidth = graphPtr->width;
            graphPtr->backHeight = graphPtr->height;
            graphPtr->flags |= REDRAW_BACKING_STORE;
        }
        if (graphPtr->flags & REDRAW_BACKING_STORE) {
            /* The backing store is new or out-of-date. */
            DrawPlotRegion(graphPtr, graphPtr->backPixmap);
            graphPtr->flags &= ~REDRAW_BACKING_STORE;
        }
        /* Copy the pixmap to the one used for drawing the entire graph. */
        XCopyArea(graphPtr->display, graphPtr->backPixmap, drawable, graphPtr->drawGC, graphPtr->left, graphPtr->top,
                  (graphPtr->right - graphPtr->left + 1), (graphPtr->bottom - graphPtr->top + 1), graphPtr->left,
                  graphPtr->top);
    } else {
        DrawPlotRegion(graphPtr, drawable);
    }
    /* Draw markers above elements */
    Rbc_DrawMarkers(graphPtr, drawable, MARKER_ABOVE);
    Rbc_DrawActiveElements(graphPtr, drawable);
    if (graphPtr->flags & DRAW_MARGINS) {
        DrawMargins(graphPtr, drawable);
    }
    if ((Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_PLOT) && (Rbc_LegendIsRaised(graphPtr->legend))) {
        Rbc_DrawLegend(graphPtr->legend, drawable);
    }
    /* Draw 3D border just inside of the focus highlight ring. */
    if ((graphPtr->borderWidth > 0) && (graphPtr->relief != TK_RELIEF_FLAT)) {
        Tcl_WideInt width;
        Tcl_WideInt height;

        width = (Tcl_WideInt)graphPtr->width - (2 * (Tcl_WideInt)graphPtr->highlightWidth);
        height = (Tcl_WideInt)graphPtr->height - (2 * (Tcl_WideInt)graphPtr->highlightWidth);
        if ((width > 0) && (height > 0)) {
            Rbc_Draw3DRectangle(graphPtr->tkwin, drawable, graphPtr->border, graphPtr->highlightWidth,
                                graphPtr->highlightWidth, GraphLayoutInt(width), GraphLayoutInt(height),
                                graphPtr->borderWidth, graphPtr->relief);
        }
    }
    /* Draw focus highlight ring. */
    if ((graphPtr->highlightWidth > 0) && (graphPtr->flags & GRAPH_FOCUS)) {
        GC gc;

        gc = Tk_GCForColor(graphPtr->highlightColor, drawable);
        Tk_DrawFocusHighlight(graphPtr->tkwin, gc, graphPtr->highlightWidth, drawable);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * UpdateMarginTraces --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void UpdateMarginTraces(Graph *graphPtr) {
    Margin *marginPtr;
    int size;
    register int i;

    for (i = 0; i < 4; i++) {
        marginPtr = graphPtr->margins + i;
        if (marginPtr->varName != NULL) { /* Trigger variable traces */
            if ((marginPtr->site == MARGIN_LEFT) || (marginPtr->site == MARGIN_RIGHT)) {
                size = marginPtr->width;
            } else {
                size = marginPtr->height;
            }
            Tcl_SetVar(graphPtr->interp, marginPtr->varName, Rbc_Itoa(size), TCL_GLOBAL_ONLY);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DisplayGraph --
 *
 *      This procedure is invoked to display a graph widget.
 *
 * Parameters:
 *      ClientData clientData
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Commands are output to X to display the graph in its
 *      current mode.
 *
 *----------------------------------------------------------------------
 */
static void DisplayGraph(ClientData clientData) {
    Graph *graphPtr = clientData;
    Pixmap drawable;

    graphPtr->flags &= ~REDRAW_PENDING;
    if (graphPtr->tkwin == NULL) {
        return; /* Window destroyed (should not get here) */
    }
    if (Rbc_GraphUpdateNeeded(graphPtr)) {
        /*
         * One of the elements of the graph has a vector notification
         * pending.  This means that the vector will eventually notify
         * the graph that its data has changed.  Since the graph uses
         * the actual vector (not a copy) we need to keep in-sync.
         * Therefore don't draw right now but wait until we've been
         * notified before redrawing.
         */
        return;
    }
    {
        int width, height;

        width = Tk_Width(graphPtr->tkwin);
        height = Tk_Height(graphPtr->tkwin);
        if ((width != graphPtr->width) || (height != graphPtr->height)) {
            graphPtr->flags |= RESET_WORLD;
        }
        graphPtr->width = width;
        graphPtr->height = height;
    }
    Rbc_LayoutGraph(graphPtr);
    Rbc_UpdateCrosshairs(graphPtr);
    if (!Tk_IsMapped(graphPtr->tkwin)) {
        /* The graph's window isn't displayed, so don't bother
         * drawing anything.  By getting this far, we've at least
         * computed the coordinates of the graph's new layout.  */
        return;
    }

    /* Disable crosshairs before redisplaying to the screen */
    Rbc_DisableCrosshairs(graphPtr);
    /*
     * Create a pixmap the size of the window for double buffering.
     */
    if (graphPtr->doubleBuffer) {
        drawable = Tk_GetPixmap(graphPtr->display, Tk_WindowId(graphPtr->tkwin), graphPtr->width, graphPtr->height,
                                Tk_Depth(graphPtr->tkwin));
    } else {
        drawable = Tk_WindowId(graphPtr->tkwin);
    }
#ifdef WIN32
    assert(drawable != None);
#endif
    Rbc_DrawGraph(graphPtr, drawable, graphPtr->backingStore && graphPtr->doubleBuffer);
    if (graphPtr->flags & DRAW_MARGINS) {
        XCopyArea(graphPtr->display, drawable, Tk_WindowId(graphPtr->tkwin), graphPtr->drawGC, 0, 0, graphPtr->width,
                  graphPtr->height, 0, 0);
    } else {
        XCopyArea(graphPtr->display, drawable, Tk_WindowId(graphPtr->tkwin), graphPtr->drawGC, graphPtr->left,
                  graphPtr->top, (graphPtr->right - graphPtr->left + 1), (graphPtr->bottom - graphPtr->top + 1),
                  graphPtr->left, graphPtr->top);
    }
    if (graphPtr->doubleBuffer) {
        Tk_FreePixmap(graphPtr->display, drawable);
    }
    Rbc_EnableCrosshairs(graphPtr);
    graphPtr->flags &= ~RESET_WORLD;
    UpdateMarginTraces(graphPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GraphInit --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GraphInit(Tcl_Interp *interp) {
    rbcBarElementUid = (Rbc_Uid)Tk_GetUid("BarElement");
    rbcLineElementUid = (Rbc_Uid)Tk_GetUid("LineElement");
    rbcStripElementUid = (Rbc_Uid)Tk_GetUid("StripElement");
    rbcPolarElementUid = (Rbc_Uid)Tk_GetUid("PolarElement");    
    rbcContourElementUid = (Rbc_Uid)Tk_GetUid("ContourElement");

    rbcLineMarkerUid = (Rbc_Uid)Tk_GetUid("LineMarker");
    rbcBitmapMarkerUid = (Rbc_Uid)Tk_GetUid("BitmapMarker");
    rbcImageMarkerUid = (Rbc_Uid)Tk_GetUid("ImageMarker");
    rbcTextMarkerUid = (Rbc_Uid)Tk_GetUid("TextMarker");
    rbcPolygonMarkerUid = (Rbc_Uid)Tk_GetUid("PolygonMarker");
    rbcWindowMarkerUid = (Rbc_Uid)Tk_GetUid("WindowMarker");

    rbcXAxisUid = (Rbc_Uid)Tk_GetUid("X");
    rbcYAxisUid = (Rbc_Uid)Tk_GetUid("Y");

    Tcl_CreateObjCommand2(interp, "rbc::graph", GraphObjCmd, (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
    Tcl_CreateObjCommand2(interp, "rbc::barchart", BarchartObjCmd, (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
    Tcl_CreateObjCommand2(interp, "rbc::stripchart", StripchartObjCmd, (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
    Tcl_CreateObjCommand2(interp, "rbc::polar", PolarObjCmd, (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetGraphFromWindowData --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Graph *Rbc_GetGraphFromWindowData(Tk_Window tkwin) {
    Graph *graphPtr;

    while (tkwin != NULL) {
        graphPtr = (Graph *)Rbc_GetWindowInstanceData(tkwin);
        if (graphPtr != NULL) {
            return graphPtr;
        }
        tkwin = Tk_Parent(tkwin);
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GraphType --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GraphType(Graph *graphPtr) {
    if (graphPtr->classUid == rbcLineElementUid) {
        return GRAPH;
    } else if (graphPtr->classUid == rbcBarElementUid) {
        return BARCHART;
    } else if (graphPtr->classUid == rbcStripElementUid) {
        return STRIPCHART;
    } else if (graphPtr->classUid == rbcPolarElementUid) {
        return POLAR;
    }
    return 0;
}
