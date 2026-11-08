/**
 * @file rbcGraph.c
 * @brief Common graph widget creation, configuration, layout and redisplay.
 *
 * Owns the Tk window/command lifecycle and screen frame orchestration.
 * Components retain their mapping and rendering responsibilities; export
 * commands manage their temporary layouts separately.
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

/**
 * @defgroup graph_lifecycle_callbacks Graph lifecycle callbacks
 * @brief Private Tk/Tcl callbacks implementing widget lifetime and redisplay.
 */

#include "rbcGraph.h"
#include "rbcGrTrack.h"
#include "rbcRender.h"
#include "rbcBind.h"
#include "rbcGrElem.h"
#include <X11/Xutil.h>
#include <tcl.h>
#ifdef WIN32
#include <tkPlatDecls.h>
#endif

static Rbc_Uid trackGraphUid;

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
Rbc_Uid rbcArcMarkerUid;
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
#define DEF_GRAPH_REPRESENTATION "polar"

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
#define GRAPH_POLAR_REPRESENTATION_MASK (1u << 15)
#define GRAPH_RENDERER_MASK (1u << 21)
#define GRAPH_TRACK_SEPARATOR_MASK (1u << 22)
#define GRAPH_TRACK_SCROLL_MASK (1u << 23)

#define GRAPH_TRANSACTION_MASK                                                                                         \
    (GRAPH_BAR_MODE_MASK | GRAPH_BAR_WIDTH_MASK | GRAPH_PIXELS_MASK | GRAPH_PADDING_MASK | GRAPH_SHADOW_MASK |         \
     GRAPH_TILE_MASK)

#define GRAPH_INITIALIZE_MASK                                                                                          \
    (GRAPH_TRANSACTION_MASK | GRAPH_TEXT_STYLE_MASK | GRAPH_GC_MASK | GRAPH_GEOMETRY_MASK | GRAPH_INVERT_XY_MASK |     \
     GRAPH_LAYOUT_MASK | GRAPH_BACKING_STORE_MASK | GRAPH_REDRAW_MASK | GRAPH_PLOT_BACKGROUND_MASK |                   \
     GRAPH_POLAR_REPRESENTATION_MASK)



/**
 * @defgroup graph_option_transactions Graph option transactions
 * @brief Retained values, resource ownership and widget option commit ordering.
 */

/** @ingroup selector_graph
 * @brief Selects a widget dimension during staged conversion of graph pixel options. */
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

/** @brief Staged option replacements; commit transfers resources and abort releases uncommitted resources. */
typedef struct {
    unsigned int stagedMask; /**< Bit mask selecting staged replacements to commit or release. */

    int borderWidth; /**< Border thickness in pixels staged for commit or abort. */
    int bottomMarginSize; /**< Bottom margin size in pixels staged for commit or abort. */
    int halo; /**< Picking halo in pixels staged for commit or abort. */
    int reqHeight; /**< Requested widget height in pixels staged for commit or abort. */
    int highlightWidth; /**< Focus-highlight thickness in pixels staged for commit or abort. */
    int leftMarginSize; /**< Left margin size in pixels staged for commit or abort. */
    int plotBorderWidth; /**< Plot border thickness in pixels staged for commit or abort. */
    int rightMarginSize; /**< Right margin size in pixels staged for commit or abort. */
    int topMarginSize; /**< Top margin size in pixels staged for commit or abort. */
    int reqWidth; /**< Requested widget width in pixels staged for commit or abort. */
} GraphPixelTransaction;

/** @ingroup selector_graph
 * @brief Selects the horizontal or vertical plot padding during widget configuration. */
typedef enum { GRAPH_PADDING_OPTION_NONE, GRAPH_PADDING_OPTION_X, GRAPH_PADDING_OPTION_Y } GraphPaddingOption;

#define GRAPH_PADDING_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

/** @brief Staged option replacements; commit transfers resources and abort releases uncommitted resources. */
typedef struct {
    unsigned int stagedMask; /**< Bit mask selecting staged replacements to commit or release. */

    Rbc_Pad padX; /**< Horizontal pixel padding staged for commit or abort. */
    Rbc_Pad padY; /**< Vertical pixel padding staged for commit or abort. */
} GraphPaddingTransaction;

/** @brief Staged option replacements; commit transfers resources and abort releases uncommitted resources. */
typedef struct {
    int staged; /**< Nonzero when the transaction contains a replacement to commit or release. */
    BarMode mode; /**< Validated mode selector staged for commit or abort. */
} GraphBarModeTransaction;

/** @brief Staged option replacements; commit transfers resources and abort releases uncommitted resources. */
typedef struct {
    int staged; /**< Nonzero when the transaction contains a replacement to commit or release. */
    Shadow shadow; /**< Replacement text shadow staged for commit or abort. */
} GraphShadowTransaction;

/** @brief Staged option replacements; commit transfers resources and abort releases uncommitted resources. */
typedef struct {
    int staged; /**< Nonzero when the transaction contains a replacement to commit or release. */
    Rbc_Tile tile; /**< Retained replacement tile token staged for commit or abort. */
} GraphTileTransaction;

static const char *const polarRepresentationNames[] = {"polar", "smith", NULL};
/*
 * Modern graph option table.
 */
static const char *const graphRendererNames[] = {"native", "cairo", NULL};
static const char *const graphAntialiasNames[] = {"default", "none", "gray", "fast", "good", "best", NULL};

/* Cairo-enabled builds use Cairo unless the option database or caller overrides it. */
#ifdef RBC_HAVE_CAIRO
#define DEF_GRAPH_RENDERER "cairo"
#else
#define DEF_GRAPH_RENDERER "native"
#endif

static const char *const trackLayoutNames[] = {"fit", "scroll", NULL};
/** @ingroup component_option_tables
 * @brief Widget options stored in Graph; change masks identify work to schedule after configuration succeeds.
 * @hideinitializer
 */
static const Tk_OptionSpec graphOptionSpecs[] = {
    {TK_OPTION_STRING_TABLE, "-tracklayout", "trackLayout", "TrackLayout", "fit", -1, offsetof(Graph, trackScroll), 0,
     (ClientData)trackLayoutNames, GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-trackscrollcommand", "trackScrollCommand", "TrackScrollCommand", NULL,
     offsetof(Graph, trackScrollCmdObj), -1, TK_OPTION_NULL_OK, NULL, GRAPH_TRACK_SCROLL_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_COLOR, "-trackseparatorcolor", "trackSeparatorColor", "TrackSeparatorColor", NULL, -1,
     offsetof(Graph, trackSeparatorColor), TK_OPTION_NULL_OK, NULL, GRAPH_TRACK_SEPARATOR_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_PIXELS, "-trackseparatorwidth", "trackSeparatorWidth", "TrackSeparatorWidth", "1", -1,
     offsetof(Graph, trackSeparatorWidth), 0, NULL, GRAPH_TRACK_SEPARATOR_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-trackresize", "trackResize", "TrackResize", "0", -1, offsetof(Graph, trackResize), 0, NULL,
     GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING_TABLE, "-antialias", "antialias", "Antialias", "default", -1, offsetof(Graph, antialias), 0,
     (ClientData)graphAntialiasNames, GRAPH_RENDERER_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING_TABLE, "-renderer", "renderer", "Renderer", DEF_GRAPH_RENDERER, -1, offsetof(Graph, renderer), 0,
     (ClientData)graphRendererNames, GRAPH_RENDERER_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_DOUBLE, "-aspect", "aspect", "Aspect", DEF_GRAPH_ASPECT_RATIO, -1, offsetof(Graph, aspect), 0, NULL,
     GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_BORDER, "-background", "background", "Background", DEF_GRAPH_BACKGROUND, -1, offsetof(Graph, border), 0,
     DEF_GRAPH_BG_MONO, GRAPH_GC_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_STRING, "-barmode", "barMode", "BarMode", DEF_GRAPH_BAR_MODE, offsetof(Graph, barModeObjPtr), -1, 0,
     NULL, GRAPH_BAR_MODE_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_DOUBLE, "-barwidth", "barWidth", "BarWidth", DEF_GRAPH_BAR_WIDTH, -1, offsetof(Graph, barWidth), 0, NULL,
     GRAPH_BAR_WIDTH_MASK | GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
    {TK_OPTION_DOUBLE, "-baseline", "baseline", "Baseline", DEF_GRAPH_BAR_BASELINE, -1, offsetof(Graph, baseline), 0,
     NULL, GRAPH_LAYOUT_MASK | GRAPH_REDRAW_MASK},
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
    /* Preserve historical signed values on Tk 9.1 as well as Tk 9.0. */
    {TK_OPTION_PIXELS, "-highlightthickness", "highlightThickness", "HighlightThickness", DEF_GRAPH_HIGHLIGHT_WIDTH,
     offsetof(Graph, highlightWidthObjPtr), -1, TK_OPTION_NEG_OK, NULL,
     GRAPH_PIXELS_MASK | GRAPH_GEOMETRY_MASK | GRAPH_REDRAW_MASK},
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

/** @brief Parsed graph snapshot destination and dimensions. */
typedef struct {
    const char *name; /**< Snapshot destination name. */
    int width; /**< Requested snapshot width in pixels. */
    int height; /**< Requested snapshot height in pixels. */
    int format; /**< Snapshot format selector. */
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
static int UpdateMarginTraces(Graph *graphPtr);

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
static int InitMetaFileHeader(Tcl_Interp *interp, Tk_Window tkwin, int width, int height, APMHEADER *mfhPtr);
static int CreateAPMetaFile(Tcl_Interp *interp, HANDLE hMetaFile, HDC hDC, APMHEADER *mfhPtr, const char *fileName);
#endif

/** @brief Static graph option-name lookup entry. */
typedef struct {
    const char *name; /**< Borrowed option name. */
    int option; /**< Associated option selector. */
} GraphOptionName;

/** @brief Typed command dispatch entry with shared argument validation metadata. */
typedef struct {
    Rbc_OpSpecHeader header; /**< Operation name, total argument bounds and usage suffix. */
    Rbc_GraphOpProc *proc; /**< Typed command handler for this operation. */
} GraphOpSpec;

/**
 * @ingroup graph_option_transactions
 * @brief Classify a canonical graph option through a private map.
 *
 * Borrows the object and map. Unknown or ambiguous names, and options absent
 * from the map, return zero; this helper does not report an interpreter error.
 *
 * @param[in] objPtr Borrowed Tcl option object; no reference is retained.
 * @param[in] optionMap Borrowed array mapping canonical graph options to private selectors.
 * @param[in] nOptions Number of entries accessible in optionMap.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Compare an option spelling with one canonical graph option.
 *
 * Resolves aliases and unambiguous abbreviations through graphOptionSpecs.
 * Returns false if resolution fails; neither argument is retained.
 *
 * @param[in] objPtr Borrowed Tcl option object; no reference is retained.
 * @param[in] optionName Borrowed canonical option name to compare against the resolved spelling.
 */
static int IsGraphOption(Tcl_Obj *objPtr, const char *optionName) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, graphOptionSpecs);
    return ((name != NULL) && (strcmp(name, optionName) == 0));
}

/**
 * @ingroup graph_option_transactions
 * @brief Identify a retained graph distance option.
 *
 * Returns GRAPH_PIXEL_OPTION_NONE for other or unresolved options. The
 * classification is used both for explicit-override masks and ordered parsing.
 *
 * @param[in] objPtr Borrowed Tcl option object; no reference is retained.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Borrow the retained Tcl object for a graph distance.
 *
 * Does not change its reference count. The option must be a valid distance
 * selector; invalid selectors panic rather than returning a recoverable error.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[in] option Valid selector identifying the distance or padding field to access.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Parse one distance into a candidate transaction.
 *
 * Uses the graph window for unit conversion. All distances except highlight
 * thickness require nonnegative values; highlight thickness uses Tk conversion
 * directly. On success sets the selected field and mask bit; on conversion
 * error leaves that candidate unchanged and returns TCL_ERROR.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[in] objPtr Borrowed proposed option value; parsed before replacing the candidate.
 * @param[in] option Valid selector identifying the distance or padding field to access.
 * @param[in,out] transactionPtr Initialized candidate record updated only after successful parsing; retains
 *   prior candidate on failure.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Prepare distance values without changing live numeric fields.
 *
 * Requires an even borrowed option vector in the graph context. Zeroes the
 * transaction, parses unoverridden retained defaults on first configuration,
 * then parses every explicit occurrence in caller order. An invalid earlier
 * occurrence fails even if a later occurrence is valid. Partial candidates
 * on error own no resources and can be discarded.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[out] transactionPtr Caller-owned fresh candidate record, initialized here; success requires commit or
 *   cleanup as described above.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Publish only staged graph distances.
 *
 * Copies selected integers into graph and margin fields, then clears the
 * mask. No conversion, geometry request or redraw occurs here.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] transactionPtr Prepared candidate record consumed into live fields and disarmed; record
 *   storage remains caller-owned.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Identify retained X or Y plot padding.
 *
 * Returns GRAPH_PADDING_OPTION_NONE for other or unresolved options.
 * The input object remains caller-owned.
 *
 * @param[in] objPtr Borrowed Tcl option object; no reference is retained.
 */
static GraphPaddingOption GetGraphPaddingOption(Tcl_Obj *objPtr) {
    static const GraphOptionName optionMap[] = {{"-plotpadx", GRAPH_PADDING_OPTION_X},
                                                {"-plotpady", GRAPH_PADDING_OPTION_Y}};
    return (GraphPaddingOption)GetGraphOptionFromObj(objPtr, optionMap, sizeof(optionMap) / sizeof(optionMap[0]));
}

/**
 * @ingroup graph_option_transactions
 * @brief Borrow the retained Tcl object for plot padding.
 *
 * Returns the X or Y object without retaining it. Invalid selectors panic.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[in] option Valid selector identifying the distance or padding field to access.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Parse one plot-padding candidate.
 *
 * Rbc_GetPadFromObj validates using the graph window. On success stores the
 * padding and sets its mask bit; on error reports TCL_ERROR without publishing
 * any live padding. The selector must be X or Y.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[in] objPtr Borrowed proposed option value; parsed before replacing the candidate.
 * @param[in] option Valid selector identifying the distance or padding field to access.
 * @param[in,out] transactionPtr Initialized candidate record updated only after successful parsing; retains
 *   prior candidate on failure.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Prepare both plot-padding fields in caller order.
 *
 * Zeroes the transaction and requires an even borrowed option vector. First
 * configuration parses defaults only for axes without an explicit override.
 * Every explicit occurrence is validated, including superseded ones. Partial
 * candidates on failure require no resource cleanup.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[out] transactionPtr Caller-owned fresh candidate record, initialized here; success requires commit or
 *   cleanup as described above.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Publish staged plot padding and clear its mask.
 *
 * Unstaged axes keep their current padding. This helper does not request
 * layout or redraw.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] transactionPtr Prepared candidate record consumed into live fields and disarmed; record
 *   storage remains caller-owned.
 */
static void CommitGraphPaddingTransaction(Graph *graphPtr, GraphPaddingTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & GRAPH_PADDING_OPTION_MASK(GRAPH_PADDING_OPTION_X)) {
        graphPtr->padX = transactionPtr->padX;
    }
    if (transactionPtr->stagedMask & GRAPH_PADDING_OPTION_MASK(GRAPH_PADDING_OPTION_Y)) {
        graphPtr->padY = transactionPtr->padY;
    }
    transactionPtr->stagedMask = 0;
}

/**
 * @ingroup graph_option_transactions
 * @brief Parse a graph-wide bar mode with Tcl abbreviation rules.
 *
 * Normal and infront map to MODE_INFRONT. Writes the destination only on
 * success; invalid or ambiguous names return TCL_ERROR with a diagnostic.
 *
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed Tcl option object; no reference is retained.
 * @param[out] modePtr Required output receiving the parsed bar mode only on success.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Validate and retain one candidate bar mode.
 *
 * Updates the transaction only after successful conversion. Neither the
 * input object nor a resource is retained; the live mode is unchanged.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[in] objPtr Borrowed proposed option value; parsed before replacing the candidate.
 * @param[in,out] transactionPtr Initialized candidate record updated only after successful parsing; retains
 *   prior candidate on failure.
 */
static int StageGraphBarMode(Graph *graphPtr, Tcl_Obj *objPtr, GraphBarModeTransaction *transactionPtr) {
    BarMode mode;

    if (GetGraphBarModeFromObj(graphPtr->interp, objPtr, &mode) != TCL_OK) {
        return TCL_ERROR;
    }
    transactionPtr->mode = mode;
    transactionPtr->staged = TRUE;
    return TCL_OK;
}

/**
 * @ingroup graph_option_transactions
 * @brief Prepare the graph-wide mode from defaults and explicit options.
 *
 * Zeroes the transaction. On first configuration, uses the retained default
 * only when no explicit mode occurs. Parses all explicit occurrences in order
 * and stops at the first error; the borrowed option vector must be even.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[out] transactionPtr Caller-owned fresh candidate record, initialized here; success requires commit or
 *   cleanup as described above.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Publish a staged bar mode.
 *
 * Leaves the live mode unchanged when unstaged and clears the staged flag
 * after copying. No mapping or drawing is performed.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] transactionPtr Prepared candidate record consumed into live fields and disarmed; record
 *   storage remains caller-owned.
 */
static void CommitGraphBarModeTransaction(Graph *graphPtr, GraphBarModeTransaction *transactionPtr) {
    if (transactionPtr->staged) {
        graphPtr->mode = transactionPtr->mode;
        transactionPtr->staged = FALSE;
    }
}

/**
 * @ingroup graph_option_transactions
 * @brief Release an owned title-shadow color and reset its value.
 *
 * Accepts a non-NULL Shadow record whose color may be NULL. Clears both
 * color and offset, allowing repeated cleanup of the same record.
 *
 * @param[in,out] shadowPtr Owned shadow value whose color is released and fields reset; record storage remains
 *   caller-owned.
 */
static void FreeGraphShadow(Shadow *shadowPtr) {
    if (shadowPtr->color != NULL) {
        Tk_FreeColor(shadowPtr->color);
    }
    shadowPtr->color = NULL;
    shadowPtr->offset = 0;
}

/**
 * @ingroup graph_option_transactions
 * @brief Acquire a replacement title shadow before discarding a candidate.
 *
 * On parse failure returns TCL_ERROR and keeps the prior candidate. On
 * success releases any previously staged shadow, owns the new color in the
 * transaction and marks it staged. The live title style is not changed.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[in] objPtr Borrowed proposed option value; parsed before replacing the candidate.
 * @param[in,out] transactionPtr Initialized candidate record updated only after successful parsing; retains
 *   prior candidate on failure.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Discard an uncommitted shadow candidate.
 *
 * Releases its color when staged and zeroes the complete transaction.
 * Safe for a zero-initialized or already committed transaction.
 *
 * @param[in,out] transactionPtr Initialized candidate record whose staged resources are released and fields
 *   cleared.
 */
static void FreeGraphShadowTransaction(GraphShadowTransaction *transactionPtr) {
    if (transactionPtr->staged) {
        FreeGraphShadow(&transactionPtr->shadow);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @ingroup graph_option_transactions
 * @brief Prepare an owned shadow candidate from retained options.
 *
 * Requires a fresh transaction and an even borrowed argument vector. First
 * configuration uses the default only if not overridden. Processes explicit
 * occurrences in order. On any conversion error releases all staged ownership
 * and returns TCL_ERROR; success leaves cleanup or commit to the caller.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[out] transactionPtr Caller-owned fresh candidate record, initialized here; success requires commit or
 *   cleanup as described above.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Transfer the shadow candidate to the live title style.
 *
 * An unstaged transaction is a no-op. Otherwise moves ownership to the
 * graph, disarms the transaction and releases the old live shadow color.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] transactionPtr Prepared candidate record consumed into live fields and disarmed; record
 *   storage remains caller-owned.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Acquire a candidate tile, including an explicit empty tile.
 *
 * A NULL or empty object stages NULL. A nonempty name acquires a tile client.
 * Acquisition failure keeps the prior candidate. Success frees any earlier
 * candidate and transfers ownership to the transaction, not the graph.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[in] objPtr Borrowed proposed image name; NULL or an empty object explicitly stages no tile.
 * @param[in,out] transactionPtr Initialized candidate record updated only after successful parsing; retains
 *   prior candidate on failure.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Release any uncommitted tile client and zero the transaction.
 *
 * Safe for a zero-initialized or committed transaction. Does not release
 * the graph live tile.
 *
 * @param[in,out] transactionPtr Initialized candidate record whose staged resources are released and fields
 *   cleared.
 */
static void FreeGraphTileTransaction(GraphTileTransaction *transactionPtr) {
    if (transactionPtr->staged && (transactionPtr->tile != NULL)) {
        Rbc_FreeTile(transactionPtr->tile);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @ingroup graph_option_transactions
 * @brief Prepare a tile client from retained defaults and explicit options.
 *
 * Requires a fresh transaction and an even borrowed option vector. Parses
 * the initial default only if not overridden, then every explicit occurrence
 * in order. Failure releases staged ownership; success requires commit or
 * cleanup by the caller.
 *
 * @param[in] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described above.
 * @param[out] transactionPtr Caller-owned fresh candidate record, initialized here; success requires commit or
 *   cleanup as described above.
 */
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

/**
 * @ingroup graph_option_transactions
 * @brief Install the tile candidate and its graph-change callback.
 *
 * Moves ownership to the graph and disarms the transaction before freeing
 * the previous live tile. A staged NULL clears the tile; an unstaged
 * transaction does nothing.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] transactionPtr Prepared candidate record consumed into live fields and disarmed; record
 *   storage remains caller-owned.
 */
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


/**
 * @defgroup graph_lifecycle_support Graph construction and redraw support
 * @brief Creation dependencies, geometry settling and resource notifications.
 */

/*
 * Expose may arrive while pack/grid is still propagating an ancestor resize.
 * A single idle callback can overtake the next level's geometry callback.
 * Sample the complete Tk ancestry in one idle pass, and present only after an
 * unchanged samples in two subsequent passes. The extra quiet pass allows a
 * queued ttk layout request to reach its manager callback before presentation.
 * No event loop is entered recursively.
 * This coalesces ordinary nested geometry work, not arbitrary future timers or
 * layout changes scheduled by application callbacks.
 */
/**
 * @ingroup graph_lifecycle_support
 * @brief One borrowed window identity and its sampled integer geometry.
 *
 * The window handle is compared with the current ancestry, not retained or
 * dereferenced through this record. The settle owner owns the sample array.
 */
typedef struct {
    Tk_Window window; /**< Borrowed sampled Tk window. */
    int x; /**< Window X position in parent pixels. */
    int y; /**< Window Y position in parent pixels. */
    int width; /**< Window width in pixels. */
    int height; /**< Window height in pixels. */
    int border; /**< Window border width in pixels. */
} GraphGeometrySample;

/**
 * @brief Graph-owned state for coalescing geometry-manager idle work.
 *
 * Owns count sample records; stablePasses counts consecutive unchanged
 * samples. REDRAW_PENDING represents the scheduled work separately.
 */
typedef struct GraphRedrawSettle {
    int count; /**< Number of geometry samples. */
    int stablePasses; /**< Number of consecutive passes with unchanged geometry. */
    GraphGeometrySample *samples; /**< Owned sample array. */
} GraphRedrawSettle;

/**
 * @ingroup graph_lifecycle_support
 * @brief Release the pending ancestry-sampling record.
 *
 * Frees the owned sample array and record, then clears redrawSettle. A NULL
 * record is harmless. Does not cancel an idle call or clear REDRAW_PENDING;
 * the caller must handle scheduling state separately.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 */
static void FreeGraphRedrawSettle(Graph *graphPtr) {
    GraphRedrawSettle *settle = graphPtr->redrawSettle;
    if (settle != NULL) {
        if (settle->samples != NULL) Tcl_Free((char *)settle->samples);
        Tcl_Free((char *)settle);
        graphPtr->redrawSettle = NULL;
    }
}

/**
 * @ingroup graph_lifecycle_support
 * @brief Wait for two unchanged ancestry samples before presenting a frame.
 *
 * Requires the graph-owned redrawSettle record installed by
 * EventuallyRedrawSettledGraph. Samples window identity, position, size and
 * border from the widget through its toplevel; reallocates samples when the
 * ancestry length changes. Changed geometry resets the stable-pass count.
 * Requeues itself until two unchanged passes, then frees samples and delegates
 * to DisplayGraph, which consumes REDRAW_PENDING. A dead window frees samples
 * and clears the pending flag without drawing. Does not enter an event loop.
 *
 * @param[in] clientData Borrowed widget-owned Graph registered for this callback; no new ownership is acquired.
 */
static void SettleGraphRedraw(ClientData clientData) {
    Graph *graphPtr = clientData;
    GraphRedrawSettle *settle = graphPtr->redrawSettle;
    Tk_Window window;
    int count = 0, i = 0, changed = 0;

    if (graphPtr->tkwin == NULL) {
        FreeGraphRedrawSettle(graphPtr);
        graphPtr->flags &= ~REDRAW_PENDING;
        return;
    }
    for (window = graphPtr->tkwin; window != NULL; window = Tk_Parent(window)) {
        count++;
        if (Tk_IsTopLevel(window)) break;
    }
    if (settle->count != count) {
        if (settle->samples != NULL) Tcl_Free((char *)settle->samples);
        settle->samples = (GraphGeometrySample *)Tcl_Alloc(count * sizeof(GraphGeometrySample));
        settle->count = count;
        changed = 1;
    }
    for (window = graphPtr->tkwin; i < count; i++, window = Tk_Parent(window)) {
        GraphGeometrySample *sample = &settle->samples[i];
        if (!changed && (sample->window != window || sample->x != Tk_X(window) ||
                        sample->y != Tk_Y(window) || sample->width != Tk_Width(window) ||
                        sample->height != Tk_Height(window) || sample->border != Tk_Changes(window)->border_width)) {
            changed = 1;
        }
        sample->window = window;
        sample->x = Tk_X(window);
        sample->y = Tk_Y(window);
        sample->width = Tk_Width(window);
        sample->height = Tk_Height(window);
        sample->border = Tk_Changes(window)->border_width;
    }
    if (changed) settle->stablePasses = 0;
    else settle->stablePasses++;
    if (settle->stablePasses < 2) {
        Tcl_DoWhenIdle(SettleGraphRedraw, graphPtr);
        return;
    }
    FreeGraphRedrawSettle(graphPtr);
    /* DisplayGraph consumes REDRAW_PENDING and samples final widget dimensions. */
    DisplayGraph(graphPtr);
}

/**
 * @ingroup graph_lifecycle_support
 * @brief Replace ordinary idle drawing with ancestry-settled drawing.
 *
 * Does nothing for a dead window or an already active settle request. Cancels
 * a pending DisplayGraph call, allocates a fresh sampling record, sets
 * REDRAW_PENDING and schedules SettleGraphRedraw. Also requests external legend
 * redraw. Callers set geometry and drawing invalidation flags themselves.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 */
static void EventuallyRedrawSettledGraph(Graph *graphPtr) {
    if (graphPtr->tkwin == NULL || graphPtr->redrawSettle != NULL) return;
    if (graphPtr->flags & REDRAW_PENDING) {
        Tcl_CancelIdleCall(DisplayGraph, graphPtr);
    }
    graphPtr->redrawSettle = (GraphRedrawSettle *)Tcl_Alloc(sizeof(GraphRedrawSettle));
    graphPtr->redrawSettle->count = 0;
    graphPtr->redrawSettle->stablePasses = 0;
    graphPtr->redrawSettle->samples = NULL;
    graphPtr->flags |= REDRAW_PENDING;
    Tcl_DoWhenIdle(SettleGraphRedraw, graphPtr);
    Rbc_RedrawExternalLegend(graphPtr);
}

void Rbc_EventuallyRedrawGraph(Graph *graphPtr) {
    if (graphPtr->axisFormatActive) {
        graphPtr->axisFormatInvalid = TRUE;
    }
    if ((graphPtr->tkwin != NULL) && !(graphPtr->flags & REDRAW_PENDING)) {
        Tcl_DoWhenIdle(DisplayGraph, graphPtr);
        graphPtr->flags |= REDRAW_PENDING;
        Rbc_RedrawExternalLegend(graphPtr);
    }
}

/**
 * @ingroup graph_lifecycle_support
 * @brief Refresh derived font resources after a Tk world change.
 *
 * Ignores dead or not-yet-configured graphs. Resets title style and height,
 * notifies axes, legends, markers, line and bar elements of font changes, then
 * invalidates geometry and backing pixels and schedules redraw. Does not
 * reparse option objects or deliberately evaluate application scripts.
 *
 * @param[in] clientData Borrowed widget-owned Graph registered for this callback; no new ownership is acquired.
 */
static void GraphWorldChanged(ClientData clientData) {
    Graph *graphPtr = clientData;

    if ((graphPtr->tkwin == NULL) || !graphPtr->optionsConfigured) {
        return;
    }
    Rbc_ResetTextStyle(graphPtr->tkwin, &graphPtr->titleTextStyle);
    if (graphPtr->title != NULL) {
        int width, height;

        Rbc_GetTextExtents(&graphPtr->titleTextStyle, graphPtr->title, &width, &height);
        graphPtr->titleTextStyle.height = Rbc_ClampToInt((Tcl_WideInt)height + 10);
    }
    Rbc_AxisFontsChanged(graphPtr);
    Rbc_LegendFontsChanged(graphPtr);
    Rbc_MarkerFontsChanged(graphPtr);
    Rbc_LineFontsChanged(graphPtr);
    Rbc_BarFontsChanged(graphPtr);
    graphPtr->flags |= RESET_WORLD | REDRAW_WORLD | REDRAW_BACKING_STORE;
    Rbc_EventuallyRedrawGraph(graphPtr);
}

static const Tk_ClassProcs graphClassProcs = {
    sizeof(Tk_ClassProcs), GraphWorldChanged, NULL, NULL
};

/**
 * @ingroup graph_lifecycle_callbacks
 * @brief Translate Tk events into redraw requests and deferred destruction.
 * @param[in] clientData Graph record registered with Tk_CreateEventHandler.
 * @param[in] eventPtr Borrowed event; never retained.
 *
 * Expose/resize use ancestry-settled idle drawing; focus uses ordinary idle
 * drawing. DestroyNotify releases window-dependent resources before clearing
 * tkwin, deletes the widget command, cancels both display idle callbacks and
 * schedules DestroyGraph through Tcl_EventuallyFree.
 */
static void GraphEventProc(ClientData clientData, register XEvent *eventPtr) {
    Graph *graphPtr = clientData;

    if (graphPtr->axisFormatActive &&
        (eventPtr->type == ConfigureNotify || eventPtr->type == DestroyNotify)) {
        graphPtr->axisFormatInvalid = TRUE;
    }
    if (eventPtr->type == Expose) {
        if (eventPtr->xexpose.count == 0) {
            graphPtr->flags |= REDRAW_WORLD;
            EventuallyRedrawSettledGraph(graphPtr);
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
            Rbc_ReleaseElementTkResources(graphPtr);
            Rbc_ReleasePenTkResources(graphPtr);
            ReleaseGraphOptionResources(graphPtr);
            Rbc_DeleteWindowInstanceData(graphPtr->tkwin);
            graphPtr->tkwin = NULL;
            Tcl_DeleteCommandFromToken(graphPtr->interp, graphPtr->cmdToken);
        }
        if (graphPtr->flags & REDRAW_PENDING) {
            Tcl_CancelIdleCall(DisplayGraph, graphPtr);
            Tcl_CancelIdleCall(SettleGraphRedraw, graphPtr);
        }
        FreeGraphRedrawSettle(graphPtr);
        Tcl_EventuallyFree(graphPtr, DestroyGraph);
    } else if (eventPtr->type == ConfigureNotify) {
        graphPtr->flags |= (MAP_WORLD | REDRAW_WORLD);
        EventuallyRedrawSettledGraph(graphPtr);
    }
}

/**
 * @ingroup graph_lifecycle_callbacks
 * @brief Release Tk-dependent resources and destroy a command's live window.
 * @param[in] clientData Graph record owned by the widget lifecycle.
 *
 * No-op once tkwin is NULL. Clear the borrowed window cursor before releasing
 * option resources, then clear tkwin before Tk_DestroyWindow can reenter the
 * destruction path. DestroyNotify schedules final record disposal.
 */
static void GraphInstCmdDeleteProc(ClientData clientData) {
    Graph *graphPtr = clientData;
    if (graphPtr->tkwin != NULL) {
        Tk_Window tkwin;

        tkwin = graphPtr->tkwin;
        /*
         * Tk_DestroyWindow may realize an uncreated window to deliver
         * DestroyNotify. Clear its borrowed cursor before freeing the
         * option-owned cursor, or XCreateWindow can receive a stale XID.
         */
        Tk_UndefineCursor(tkwin);
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
        Rbc_ReleaseElementTkResources(graphPtr);
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

/**
 * @ingroup graph_lifecycle_support
 * @brief Request margin redisplay when the graph tile changes.
 *
 * The tile argument is unused. For a live window sets REDRAW_WORLD and
 * schedules drawing; a dead window is ignored. Does not rebuild a GC or
 * replace the tile client, whose ownership belongs to graph configuration.
 *
 * @param[in] clientData Borrowed widget-owned Graph registered for this callback; no new ownership is acquired.
 * @param[in] tile Borrowed changed tile token; unused by this callback.
 */
static void TileChangedProc(ClientData clientData, Rbc_Tile tile) {
    Graph *graphPtr = clientData;
    if (graphPtr->tkwin != NULL) {
        graphPtr->flags |= REDRAW_WORLD;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
}

/**
 * @ingroup graph_lifecycle_support
 * @brief Associate logical axis chains with physical margins.
 *
 * Borrows the four existing chains. Normal order maps X/Y/X2/Y2 to
 * bottom/left/top/right; inversion maps them to left/bottom/right/top. Does
 * not change chain contents, axis classes, reference counts or axis sites,
 * and does not itself invalidate geometry.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
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

/**
 * @ingroup graph_lifecycle_support
 * @brief Initialize the pen registry and create default active pens.
 *
 * Creates activeLine and activeBar in that order. Returns TCL_ERROR if
 * either creation fails, retaining any already-created pen for graph teardown.
 * Must run once on a new graph; does not roll back the registry locally.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
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

/**
 * @defgroup graph_picking Graph picking callbacks
 * @brief Graph-specific hit priority and binding-key collection.
 */

/**
 * @ingroup graph_picking
 * @brief Append graph binding keys according to the explicit object context.
 * @param[in] table Graph-owned binding table.
 * @param[in] object Borrowed axis, element, marker or track.
 * @param[in] context Encoded GraphBindContext; no shared record prefix is assumed.
 * @param[in,out] list Temporary one-word-key list owned by event dispatch.
 *
 * Axes/elements/markers append name, class and configured tags using separate
 * component token tables. Tracks append the track pointer and private all-track
 * token. Does not evaluate Tcl or take ownership of the appended keys.
 */
static void GraphTags(Rbc_BindTable table, ClientData object, ClientData context, Rbc_List list) {
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
    case GRAPH_BIND_CONTEXT_TRACK:
        Rbc_ListAppend(list, object, 0);
        Rbc_ListAppend(list, (const char *)&graphPtr->tracks, 0); /* Private token for the track "all" tag. */
        break;
    default:
        /*
         * An object returned by PickEntry must always have a valid
         * GraphBindContext.
         */
        break;
    }
}

/**
 * @ingroup graph_picking
 * @brief Hit-test graph components and return a borrowed object/context pair.
 * @param[in] clientData Graph retained by the binding dispatcher or repick caller.
 * @param[in] x Window-relative horizontal pixel coordinate.
 * @param[in] y Window-relative vertical pixel coordinate.
 * @param[out] contextPtr Encoded GraphBindContext on a hit, otherwise NULL.
 * @return Borrowed component or NULL for no hit/layout cancellation.
 *
 * Pending full mapping can invoke axis format callbacks. Local legend rectangles
 * exclude graph hits. Margins select axes; the plot tests above-markers, eligible
 * elements, under-markers and finally track backgrounds. Elements use their own
 * closestProc under the owning track viewport, skipping dirty/pending vector
 * geometry. The caller must retain the graph across callback-capable layout.
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
        if (Rbc_LayoutGraph(graphPtr) != TCL_OK) {
            return NULL;
        }
    }
    /* Local legend rectangles are controls, not element/marker hit targets. */
    if (graphPtr->trackGraph && Rbc_PointInTrackLegend(graphPtr, x, y)) {
        return NULL;
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
                if (elemPtr->track == NULL || (elemPtr->track->right - elemPtr->track->left > 1 &&
                                               elemPtr->track->bottom - elemPtr->track->top > 1 &&
                                               search.x >= elemPtr->track->left && search.x <= elemPtr->track->right &&
                                               search.y >= elemPtr->track->top && search.y <= elemPtr->track->bottom)) {
                    RbcTrackViewport saved;
                    Rbc_EnterTrack(graphPtr, elemPtr->track, &saved);
                    (*elemPtr->procsPtr->closestProc)(graphPtr, elemPtr, &search);
                    Rbc_LeaveTrack(graphPtr, &saved);
                }
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
    /* Backgrounds are last in the picking order. Legends, including their
     * padding, and separators must not turn into background targets. */
    if (graphPtr->trackGraph && !Rbc_PointInLegend(graphPtr, x, y)) {
        RbcTrack *track;
        for (track = graphPtr->tracks; track != NULL; track = track->next) {
            if (track->right > track->left && track->bottom > track->top && x >= track->left && x <= track->right &&
                y >= track->top && y <= track->bottom) {
                *contextPtr = INT2PTR(GRAPH_BIND_CONTEXT_TRACK);
                return track;
            }
        }
    }
    return NULL;
}

/**
 * @ingroup graph_option_transactions
 * @brief Initialize Tk-managed widget options and their database defaults.
 *
 * Uses the actual widget window rather than a named component. Repeated
 * calls after success are harmless. On Tk_InitOptions failure undefines the
 * window cursor before freeing partially initialized options and the table.
 * Success marks resources live but does not construct derived RBC fields.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
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
        /* The cursor option also updates the window attributes. Do not leave
         * an unrealized window referring to a cursor freed by partial cleanup. */
        Tk_UndefineCursor(graphPtr->tkwin);
        Tk_FreeConfigOptions((char *)graphPtr, graphPtr->optionTable, graphPtr->tkwin);
        Tk_DeleteOptionTable(graphPtr->optionTable);
        graphPtr->optionTable = NULL;
        return TCL_ERROR;
    }
    graphPtr->optionsInitialized = TRUE;
    graphPtr->tkResourcesReleased = FALSE;
    return TCL_OK;
}

/**
 * @ingroup graph_option_transactions
 * @brief Clear borrowed configuration arguments and the active option mask.
 *
 * Does not decrement object references: the argument vector is caller-owned.
 * Used before configuration and before restoring or releasing options.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 */
static void ResetGraphOptionContext(Graph *graphPtr) {
    graphPtr->optionMask = 0;
    graphPtr->optionObjc = 0;
    graphPtr->optionObjv = NULL;
}

/**
 * @ingroup graph_option_transactions
 * @brief Apply Tk options and validate derived graph resources.
 *
 * Requires initialized options and an even option/value vector. Exposes the
 * borrowed original arguments only during ConfigureGraph, forcing initialization
 * masks on the first call. Derived conversion failure clears that context
 * and restores Tk saved options. Success frees saved options, marks the graph
 * configured and optionally returns the Tk change mask. A successful explicit
 * Polar aspect disables automatic aspect; a renderer change invalidates mapping
 * and backing pixels. Rollback concerns option resources, not all auxiliary
 * notification state touched by validation.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in] objc Even number of option/value entries in objv; zero is valid.
 * @param[in] objv Borrowed even-length option/value vector, not a widget-command vector; retained only during
 *   configuration.
 * @param[out] maskPtr Optional output for the Tk option change mask on successful configuration.
 */
static int ConfigureGraphOptions(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[], int *maskPtr) {
    Tk_SavedOptions savedOptions;
    Tcl_Size i;
    int aspectSpecified;
    int mask;
    int oldRenderer = graphPtr->renderer;
    assert(graphPtr->optionsInitialized);
    assert(graphPtr->optionTable != NULL);
    assert((objc & 1) == 0);
    aspectSpecified = FALSE;
    if (graphPtr->classUid == rbcPolarElementUid) {
        for (i = 0; i < objc; i += 2) {
            if (IsGraphOption(objv[i], "-aspect")) {
                aspectSpecified = TRUE;
                break;
            }
        }
    }
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
         * Never retain pointers to the caller-owned option vector while
         * restoring the configuration.
         */
        ResetGraphOptionContext(graphPtr);
        return Rbc_RestoreOptionsOnError(graphPtr->interp, &savedOptions);
    }
    if (graphPtr->renderer != oldRenderer) {
        /* Auto-decimation density depends on the selected renderer. */
        graphPtr->flags |= MAP_ALL | REDRAW_BACKING_STORE;
    }
    if (aspectSpecified) {
        /*
         * Once the user explicitly supplies -aspect, retain the normal
         * fixed plot-area aspect semantics.
         */
        graphPtr->polarAutoAspect = FALSE;
    }
    ResetGraphOptionContext(graphPtr);
    graphPtr->optionsConfigured = TRUE;
    Tk_FreeSavedOptions(&savedOptions);
    if (maskPtr != NULL) {
        *maskPtr = mask;
    }
    return TCL_OK;
}

/**
 * @ingroup graph_option_transactions
 * @brief Initialize widget defaults and perform the first derived configuration.
 *
 * Always calls ConfigureGraphOptions, including with zero arguments. Polar
 * uses automatic data-unit aspect only when the option database supplies no
 * aspect; a successful explicit aspect overrides that policy. Returns TCL_ERROR
 * on initialization or configuration failure; creation owns graph cleanup.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in] objc Even number of option/value entries in objv; zero is valid.
 * @param[in] objv Borrowed even-length option/value vector, not a widget-command vector; retained only during
 *   configuration.
 */
static int ConfigureNewGraph(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (InitGraphOptions(graphPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Polar and Smith plots preserve equal physical X/Y data-unit scales
     * by default.  Keep the public -aspect value at the historical Polar
     * default of 1.0, but remember internally that this value was implicit.
     *
     * Tk_InitOptions has already applied any option-database value.  An
     * option-database aspect therefore counts as an explicit fixed aspect.
     *
     * Any creation-time -aspect option is processed below by
     * ConfigureGraphOptions(), which will also disable automatic Polar
     * aspect handling after a successful configuration.
     */
    if (graphPtr->classUid == rbcPolarElementUid) {
        if (Tk_GetOption(graphPtr->tkwin, "aspect", "Aspect") == NULL) {
            graphPtr->aspect = 1.0;
            graphPtr->polarAutoAspect = TRUE;
        } else {
            graphPtr->polarAutoAspect = FALSE;
        }
    }
    return ConfigureGraphOptions(graphPtr, objc, objv, NULL);
}

/**
 * @ingroup graph_option_transactions
 * @brief Release Tk-owned options while the graph window remains valid.
 *
 * No-op before initialization or after release. Clears borrowed transaction
 * context, frees options and deletes the table. Derived GCs, shadow and tile
 * remain owned by the graph destruction path.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
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
    Tk_DeleteOptionTable(graphPtr->optionTable);
    graphPtr->optionTable = NULL;
    graphPtr->optionsInitialized = FALSE;
    graphPtr->tkResourcesReleased = TRUE;
}

/**
 * @ingroup graph_option_transactions
 * @brief Validate retained graph values, then commit derived resources.
 *
 * Requires initialized options and active configuration context. Validates
 * track options, renderer availability and finite aspect, bar width and baseline,
 * then prepares distance, padding, mode, shadow and tile candidates. Reportable
 * conversion errors precede commits; owned candidates are freed on failure.
 * After commit normalizes nonpositive bar width, updates geometry and GCs,
 * reconfigures text and crosshairs, invalidates affected caches and schedules
 * redraw. Tk option rollback belongs to ConfigureGraphOptions; auxiliary track
 * notification bookkeeping is not part of these candidate transactions.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 */
static int ConfigureGraph(Graph *graphPtr) {
    GraphBarModeTransaction barModeTransaction;
    GraphPaddingTransaction paddingTransaction;
    GraphPixelTransaction pixelTransaction;
    GraphShadowTransaction shadowTransaction;
    GraphTileTransaction tileTransaction;
    int barModeTransactionPrepared;
    int paddingTransactionPrepared;
    int pixelTransactionPrepared;
    int shadowTransactionPrepared;
    int tileTransactionPrepared;
    int invertXYModified;
    int layoutModified;
    int plotBackgroundModified;
    int representationModified;
    XColor *colorPtr;
    GC newGC;
    XGCValues gcValues;
    unsigned long gcMask;

    if (Rbc_ConfigureTrackGraph(graphPtr, (graphPtr->optionMask & GRAPH_TRACK_SCROLL_MASK) != 0) != TCL_OK) {
        return TCL_ERROR;
    }
    assert(graphPtr->optionsInitialized);
    assert(graphPtr->optionTable != NULL);
#ifndef RBC_HAVE_CAIRO
    if (graphPtr->renderer == RBC_RENDERER_CAIRO) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("Cairo renderer is not available in this build", -1));
        return TCL_ERROR;
    }
#endif
    memset(&barModeTransaction, 0, sizeof(barModeTransaction));
    memset(&paddingTransaction, 0, sizeof(paddingTransaction));
    memset(&pixelTransaction, 0, sizeof(pixelTransaction));
    memset(&shadowTransaction, 0, sizeof(shadowTransaction));
    memset(&tileTransaction, 0, sizeof(tileTransaction));
    barModeTransactionPrepared = FALSE;
    paddingTransactionPrepared = FALSE;
    pixelTransactionPrepared = FALSE;
    shadowTransactionPrepared = FALSE;
    tileTransactionPrepared = FALSE;
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
    invertXYModified = ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_INVERT_XY_MASK));
    layoutModified = ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_LAYOUT_MASK));
    plotBackgroundModified = ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_PLOT_BACKGROUND_MASK));
    representationModified =
        ((!graphPtr->optionsConfigured) || (graphPtr->optionMask & GRAPH_POLAR_REPRESENTATION_MASK));
    /*
     * Preserve the historical normalisation behaviour for -barwidth.
     */
    if (graphPtr->barWidth <= 0.0) {
        graphPtr->barWidth = 0.1;
    }
    graphPtr->inset = Rbc_ClampToInt((Tcl_WideInt)graphPtr->borderWidth + (Tcl_WideInt)graphPtr->highlightWidth + 1);
    if ((graphPtr->reqHeight != Tk_ReqHeight(graphPtr->tkwin)) ||
        (graphPtr->reqWidth != Tk_ReqWidth(graphPtr->tkwin))) {
        Tk_GeometryRequest(graphPtr->tkwin, graphPtr->reqWidth, graphPtr->reqHeight);
    }
    Tk_SetInternalBorder(graphPtr->tkwin, graphPtr->borderWidth);
    colorPtr = Tk_3DBorderColor(graphPtr->border);
    if (graphPtr->title != NULL) {
        int w, h;

        Rbc_GetTextExtents(&graphPtr->titleTextStyle, graphPtr->title, &w, &h);
        graphPtr->titleTextStyle.height = Rbc_ClampToInt((Tcl_WideInt)h + 10);
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
    if (layoutModified || representationModified) {
        graphPtr->flags |= RESET_WORLD;
    }
    if (plotBackgroundModified || representationModified) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
    }
    if (graphPtr->optionMask & (GRAPH_RENDERER_MASK | GRAPH_TRACK_SEPARATOR_MASK)) {
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
    return TCL_ERROR;
}

/**
 * @ingroup graph_lifecycle_callbacks
 * @brief Dispose of the graph record after outstanding preserves are released.
 * @param[in] dataPtr Graph record transferred to this Tcl_FreeProc.
 *
 * Destroy components in dependency order (markers/elements before tracks,
 * axes and pens), then bindings, legend, drawing resources and the record.
 * Normal window destruction has already released Tk-dependent options and
 * cancelled idle work. The option cleanup fallback requires a valid tkwin.
 * Does not accept NULL and must not be called on an actively used graph.
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
    Rbc_DestroyTracks(graphPtr);
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
    ckfree((char *)graphPtr);
}

/**
 * @ingroup graph_lifecycle_support
 * @brief Allocate a graph widget and install its components and lifecycle.
 *
 * Requires a pathname at objv[1]. Creates the Tk window, zeroed Graph,
 * registries and display lists after creating the anonymous option proxy. Trackgraphs retain
 * a distinct Tk class and trackGraph flag but use the line element class.
 * Installs window instance data, class procedures, widget command and event
 * handler before component configuration. Creates pens, axes, exporters,
 * crosshairs, legend and grid before applying widget options, and creates
 * bindings last. Success returns a widget-owned Graph; failure returns NULL
 * with the interpreter error and destroys the window. Anonymous-window failure
 * destroys the parent before any Graph storage is allocated and preserves the
 * original interpreter error; later failures use the installed event handler.
 *
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed constructor arguments: objv[1] is the pathname, followed by option/value pairs.
 * @param[in] classUid Interned widget/element class identifier selecting the constructor behavior.
 */
static Graph *CreateGraph(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid classUid) {
    Graph *graphPtr;
    Tk_Window tkwin, optionProxy;
    const char *pathName = Tcl_GetString(objv[1]);
    tkwin = Tk_CreateWindowFromPath(interp, Tk_MainWindow(interp), pathName, NULL);
    if (tkwin == NULL) {
        return NULL;
    }
    /* Complete fallible window setup before allocating the Graph registries.
     * No widget command or destruction handler owns a Graph at this point. */
    if (classUid == trackGraphUid) {
        Tk_SetClass(tkwin, "Trackgraph");
    } else if (classUid == rbcLineElementUid) {
        Tk_SetClass(tkwin, "Graph");
    } else if (classUid == rbcBarElementUid) {
        Tk_SetClass(tkwin, "Barchart");
    } else if (classUid == rbcStripElementUid) {
        Tk_SetClass(tkwin, "Stripchart");
    } else if (classUid == rbcPolarElementUid) {
        Tk_SetClass(tkwin, "Polar");
    }
    optionProxy = Tk_CreateAnonymousWindow(interp, tkwin, NULL);
    if (optionProxy == NULL) {
        Tcl_InterpState state = Tcl_SaveInterpState(interp, TCL_ERROR);
        Tk_DestroyWindow(tkwin);
        Tcl_RestoreInterpState(interp, state);
        return NULL;
    }
    graphPtr = Rbc_Calloc(1, sizeof(Graph));
    assert(graphPtr);
    /*
     * Initialize the graph data structure.
     */
    graphPtr->tkwin = tkwin;
    graphPtr->display = Tk_Display(tkwin);
    graphPtr->interp = interp;
    graphPtr->trackGraph = (classUid == trackGraphUid);
    if (graphPtr->trackGraph) {
        classUid = rbcLineElementUid;
    }
    graphPtr->classUid = classUid;
    /*
     * Graph option lifecycle state.
     */
    graphPtr->optionTable = NULL;
    graphPtr->optionProxy = optionProxy;
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
    Rbc_SetWindowInstanceData(tkwin, graphPtr);
    Tk_SetClassProcs(tkwin, &graphClassProcs, graphPtr);
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
    if (Rbc_InitSvgOptions(graphPtr) != TCL_OK || Rbc_InitPdfOptions(graphPtr) != TCL_OK) {
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
    graphPtr->bindTable = Rbc_CreateBindingTable(interp, tkwin, graphPtr, PickEntry, GraphTags);
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

/**
 * @defgroup graph_commands_drawing Graph commands and screen composition
 * @brief Widget routing, cached geometry queries and snapshot drawing.
 */
/**
 * @ingroup graph_commands_drawing
 * @brief Dispatch the X margin-axis operation.
 *
 * Uses the bottom margin normally and the left margin when inverted.
 * Borrows the original argument vector and returns Rbc_AxisOp status unchanged;
 * axis lookup, mutation and result construction belong to that dispatcher.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int XAxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int margin;

    margin = (graphPtr->inverted) ? MARGIN_LEFT : MARGIN_BOTTOM;
    return Rbc_AxisOp(graphPtr, margin, objc, objv);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Dispatch the secondary X margin-axis operation.
 *
 * Uses the top margin normally and the right margin when inverted.
 * Borrows the original argument vector and returns Rbc_AxisOp status unchanged;
 * axis lookup, mutation and result construction belong to that dispatcher.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int X2AxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int margin;

    margin = (graphPtr->inverted) ? MARGIN_RIGHT : MARGIN_TOP;
    return Rbc_AxisOp(graphPtr, margin, objc, objv);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Dispatch the Y margin-axis operation.
 *
 * Uses the left margin normally and the bottom margin when inverted.
 * Borrows the original argument vector and returns Rbc_AxisOp status unchanged;
 * axis lookup, mutation and result construction belong to that dispatcher.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int YAxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int margin;

    margin = (graphPtr->inverted) ? MARGIN_BOTTOM : MARGIN_LEFT;
    return Rbc_AxisOp(graphPtr, margin, objc, objv);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Dispatch the secondary Y margin-axis operation.
 *
 * Uses the right margin normally and the top margin when inverted.
 * Borrows the original argument vector and returns Rbc_AxisOp status unchanged;
 * axis lookup, mutation and result construction belong to that dispatcher.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int Y2AxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int margin;

    margin = (graphPtr->inverted) ? MARGIN_TOP : MARGIN_RIGHT;
    return Rbc_AxisOp(graphPtr, margin, objc, objv);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Dispatch a bar operation through the shared element interface.
 *
 * Passes rbcBarElementUid as the element class with the original borrowed
 * arguments. Returns the element dispatcher status unchanged; does not acquire
 * an element reference or perform drawing itself.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int BarOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_ElementOp(graphPtr, interp, objc, objv, rbcBarElementUid);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Dispatch a strip operation through the shared element interface.
 *
 * Passes rbcStripElementUid as the element class with the original borrowed
 * arguments. Returns the element dispatcher status unchanged; does not acquire
 * an element reference or perform drawing itself.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int StripOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_ElementOp(graphPtr, interp, objc, objv, rbcStripElementUid);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Dispatch a line operation through the shared element interface.
 *
 * Passes rbcLineElementUid as the element class with the original borrowed
 * arguments. Returns the element dispatcher status unchanged; does not acquire
 * an element reference or perform drawing itself.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int LineOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_ElementOp(graphPtr, interp, objc, objv, rbcLineElementUid);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Dispatch a default element operation through the shared element interface.
 *
 * Passes graphPtr->classUid as the element class with the original borrowed
 * arguments. Returns the element dispatcher status unchanged; does not acquire
 * an element reference or perform drawing itself.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int ElementOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return Rbc_ElementOp(graphPtr, interp, objc, objv, graphPtr->classUid);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Query option metadata or configure the graph.
 *
 * With two arguments returns all Tk option descriptions; with three returns
 * one description. Longer calls delegate option/value pairs to
 * ConfigureGraphOptions. Requires initialized options; returns TCL_ERROR on
 * lookup or configuration failure.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
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

/**
 * @ingroup graph_commands_drawing
 * @brief Return one Tk-managed option value.
 *
 * Requires initialized options and dispatcher-validated arity. Installs the
 * value as the interpreter result, or propagates a Tk lookup error. Does not
 * convert retained values into derived graph fields.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
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

/**
 * @ingroup graph_commands_drawing
 * @brief Report cached plot, margin or legend geometry.
 *
 * Accepts Tcl unambiguous abbreviations. Plot area and legend return x, y,
 * width and height; other selectors return one integer. Plot dimensions use
 * inclusive endpoints. Does not perform layout; invalid selectors return
 * TCL_ERROR rather than unconditional success.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
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

/**
 * @ingroup graph_commands_drawing
 * @brief Test a window point against the graph plotting extents.
 *
 * Converts both coordinates through Tk screen-distance parsing and returns
 * a Tcl boolean. Conversion errors propagate. Uses current graph extents
 * without laying out pending geometry; it does not select an individual track.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
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

/**
 * @ingroup graph_commands_drawing
 * @brief Map a window point to the first logical X/Y axis pair.
 *
 * Evaluates both coordinates as Tcl expressions, synchronizes element
 * vectors and resets dirty ranges, then returns two doubles. Does not perform
 * full margin layout. Checks widget validity after each expression before
 * accessing graph resources, and rejects nonfinite input coordinates.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int InvtransformOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    double x, y;
    Point2D point;
    Axis2D axes;
    Tcl_Obj *resultObj[2];

    if (Tcl_ExprDoubleObj(interp, objv[2], &x) != TCL_OK || !Rbc_GraphCallbackValid(graphPtr)) {
        return TCL_ERROR;
    }
    if (Tcl_ExprDoubleObj(interp, objv[3], &y) != TCL_OK || !Rbc_GraphCallbackValid(graphPtr)) {
        return TCL_ERROR;
    }
    if (!FINITE(x) || !FINITE(y)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("coordinates must be finite", -1));
        return TCL_ERROR;
    }
    Rbc_SyncElementVectors(graphPtr);
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

/**
 * @ingroup graph_commands_drawing
 * @brief Map a data point to rounded integer window coordinates.
 *
 * Evaluates Tcl expressions, synchronizes element vectors and resets dirty
 * ranges before selecting the first logical X/Y axis pair. Checks validity
 * after each expression and rejects nonfinite input or out-of-range rounded
 * output before integer conversion. Does not perform full layout. Trackgraphs
 * are rejected by the enclosing dispatcher.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int TransformOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    double x, y;
    Point2D point;
    Axis2D axes;
    Tcl_Obj *resultObj[2];

    if (Tcl_ExprDoubleObj(interp, objv[2], &x) != TCL_OK || !Rbc_GraphCallbackValid(graphPtr)) {
        return TCL_ERROR;
    }
    if (Tcl_ExprDoubleObj(interp, objv[3], &y) != TCL_OK || !Rbc_GraphCallbackValid(graphPtr)) {
        return TCL_ERROR;
    }
    if (!FINITE(x) || !FINITE(y)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("coordinates must be finite", -1));
        return TCL_ERROR;
    }
    Rbc_SyncElementVectors(graphPtr);
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
    point.x = round(point.x);
    point.y = round(point.y);
    if (!FINITE(point.x) || !FINITE(point.y) || point.x < INT_MIN ||
        point.x > INT_MAX || point.y < INT_MIN || point.y > INT_MAX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("transformed coordinates are out of range", -1));
        return TCL_ERROR;
    }
    resultObj[0] = Tcl_NewIntObj((int)point.x);
    resultObj[1] = Tcl_NewIntObj((int)point.y);
    Tcl_SetObjResult(interp, Tcl_NewListObj(2, resultObj));
    return TCL_OK;
}

#ifdef WIN32
/** @defgroup graph_snapshot_windows Windows metafile snapshots
 * Private placeable-WMF encoding and native snapshot file ownership.
 */
/**
 * @ingroup graph_snapshot_windows
 * @brief Initialize a packed 22-byte placeable-WMF header.
 *
 * Converts width and height independently from pixels to 1440 units per inch.
 * Rejects invalid screen metrics and dimensions that cannot fit positive signed
 * 16-bit bounds before writing the output. The checksum XORs ten little-endian
 * 16-bit words, excluding the checksum itself; no unaligned integer reads occur.
 * Returns TCL_OK or TCL_ERROR with an interpreter diagnostic.
 *
 * @param[in,out] interp Borrowed result interpreter; see @ref rbc_interp_argument.
 * @param[in] tkwin Borrowed live window supplying screen pixel and millimeter dimensions.
 * @param[in] width Positive snapshot width in pixels.
 * @param[in] height Positive snapshot height in pixels.
 * @param[out] mfhPtr Required packed header storage, assigned only on success.
 */
static int InitMetaFileHeader(Tcl_Interp *interp, Tk_Window tkwin, int width, int height, APMHEADER *mfhPtr) {
    Screen *screen = Tk_Screen(tkwin);
    double right, bottom;
    const unsigned char *bytes;
    unsigned int checksum = 0;
    int i;

    if (width <= 0 || height <= 0 || WidthOfScreen(screen) <= 0 || HeightOfScreen(screen) <= 0 ||
        WidthMMOfScreen(screen) <= 0 || HeightMMOfScreen(screen) <= 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("invalid screen or snapshot dimensions for WMF", -1));
        return TCL_ERROR;
    }
    right = ((double)width * 1440.0 * WidthMMOfScreen(screen)) / (25.4 * WidthOfScreen(screen));
    bottom = ((double)height * 1440.0 * HeightMMOfScreen(screen)) / (25.4 * HeightOfScreen(screen));
    if (!FINITE(right) || !FINITE(bottom) || right < 1.0 || bottom < 1.0 ||
        right > SHRT_MAX || bottom > SHRT_MAX) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("snapshot dimensions exceed placeable WMF bounds", -1));
        return TCL_ERROR;
    }
    memset(mfhPtr, 0, sizeof(*mfhPtr));
    mfhPtr->key = 0x9ac6cdd7L;
    mfhPtr->inch = 1440;
    mfhPtr->bbox.Right = (SHORT)right;
    mfhPtr->bbox.Bottom = (SHORT)bottom;
    bytes = (const unsigned char *)mfhPtr;
    for (i = 0; i < 20; i += 2) {
        checksum ^= (unsigned int)bytes[i] | ((unsigned int)bytes[i + 1] << 8);
    }
    mfhPtr->checksum = (WORD)checksum;
    return TCL_OK;
}

/**
 * @ingroup graph_snapshot_windows
 * @brief Convert a borrowed enhanced metafile and write a placeable WMF file.
 *
 * Validates both native conversion calls and the global-memory lock before
 * opening the destination. Owns and releases the temporary memory and file;
 * does not consume the metafile or reference DC. Conversion failure leaves an
 * existing destination untouched. Write failure may leave a partial file.
 * Returns TCL_OK after both writes, or TCL_ERROR with a native diagnostic.
 *
 * @param[in,out] interp Borrowed result interpreter; see @ref rbc_interp_argument.
 * @param[in] hMetaFile Borrowed non-NULL enhanced metafile to convert.
 * @param[in] hDC Borrowed reference DC used for MM_ANISOTROPIC conversion.
 * @param[in] mfhPtr Borrowed initialized packed placeable header.
 * @param[in] fileName Borrowed NUL-terminated native ANSI pathname for CreateFileA.
 */
static int CreateAPMetaFile(Tcl_Interp *interp, HANDLE hMetaFile, HDC hDC, APMHEADER *mfhPtr, const char *fileName) {
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMem = NULL;
    LPVOID buffer = NULL;
    int result = TCL_ERROR;
    DWORD count, nBytes;

    nBytes = GetWinMetaFileBits(hMetaFile, 0, NULL, MM_ANISOTROPIC, hDC);
    if (nBytes == 0) {
        Rbc_AppendResultStrings(interp, "can't size metafile bits: ", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    hMem = GlobalAlloc(GHND, nBytes);
    if (hMem == NULL) {
        Rbc_AppendResultStrings(interp, "can't allocate metafile memory: ", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    buffer = GlobalLock(hMem);
    if (buffer == NULL) {
        Rbc_AppendResultStrings(interp, "can't lock metafile memory: ", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    if (GetWinMetaFileBits(hMetaFile, nBytes, buffer, MM_ANISOTROPIC, hDC) != nBytes) {
        Rbc_AppendResultStrings(interp, "can't get complete metafile bits: ", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    hFile = CreateFileA(fileName, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        Rbc_AppendResultStrings(interp, "can't create metafile \"", fileName, "\": ", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    if (!WriteFile(hFile, (LPVOID)mfhPtr, sizeof(*mfhPtr), &count, NULL) || count != sizeof(*mfhPtr)) {
        Rbc_AppendResultStrings(interp, "can't write metafile header: ", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    if (!WriteFile(hFile, buffer, nBytes, &count, NULL) || count != nBytes) {
        Rbc_AppendResultStrings(interp, "can't write metafile bits: ", Rbc_LastError(), (char *)NULL);
        goto error;
    }
    result = TCL_OK;
error:
    if (hFile != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile);
    }
    if (buffer != NULL) {
        GlobalUnlock(hMem);
    }
    if (hMem != NULL) {
        GlobalFree(hMem);
    }
    return result;
}
#endif /*WIN32*/

/**
 * @ingroup graph_commands_drawing
 * @brief Render the graph to a photo or a Windows metafile target.
 *
 * Parses paired format and integer size options; sizes below two use the
 * current Tk window dimension. Temporarily assigns graph dimensions, requests
 * layout and draws without the element backing store. Photo output owns a
 * temporary pixmap and releases it after capture or callback cancellation.
 * Windows output owns a recording DC and metafile; CLIPBOARD requests transfer
 * of the metafile to the system. The enclosing SnapOp restores screen dimensions
 * and invalidates temporary geometry on every return, including capture errors.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int SnapGraph(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
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
    graphPtr->flags |= LAYOUT_NEEDED | MAP_WORLD;
    if (Rbc_LayoutGraph(graphPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    Tk_MakeWindowExist(graphPtr->tkwin);
    drawable = Tk_WindowId(graphPtr->tkwin);
    if (data.format == FORMAT_PHOTO) {
        drawable =
            Tk_GetPixmap(graphPtr->display, drawable, graphPtr->width, graphPtr->height, Tk_Depth(graphPtr->tkwin));
#ifdef WIN32
        assert(drawable != None);
#endif
        graphPtr->flags |= RESET_WORLD;
        Rbc_DrawGraph(graphPtr, drawable, noBackingStore);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            Tk_FreePixmap(graphPtr->display, drawable);
            return TCL_ERROR;
        }
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
        if (hRefDC == NULL) {
            Rbc_AppendResultStrings(interp, "can't acquire metafile reference DC: ", Rbc_LastError(), (char *)NULL);
            return TCL_ERROR;
        }
        Tcl_DStringInit(&dString);
        Tcl_DStringAppend(&dString, "RBC Graph ", -1);
        Tcl_DStringAppend(&dString, RBC_VERSION, -1);
        Tcl_DStringAppend(&dString, "\0", 1);
        Tcl_DStringAppend(&dString, Tk_PathName(graphPtr->tkwin), -1);
        Tcl_DStringAppend(&dString, "\0", 1);
        title = Tcl_DStringValue(&dString);
        hDC = CreateEnhMetaFileA(hRefDC, NULL, NULL, title);
        Tcl_DStringFree(&dString);
        if (hDC == NULL) {
            ReleaseDC(hWnd, hRefDC);
            Rbc_AppendResultStrings(interp, "can't create metafile: ", Rbc_LastError(), (char *)NULL);
            return TCL_ERROR;
        }
        metaDrawable = Rbc_WinCreateDrawableFromDC(hDC);
        if (Rbc_LayoutGraph(graphPtr) != TCL_OK) {
            Rbc_WinFreeDrawableFromDC(metaDrawable);
            hMetaFile = CloseEnhMetaFile(hDC);
            DeleteEnhMetaFile(hMetaFile);
            ReleaseDC(hWnd, hRefDC);
            return TCL_ERROR;
        }
        graphPtr->flags |= RESET_WORLD;
        Rbc_DrawGraph(graphPtr, metaDrawable, FALSE);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            Rbc_WinFreeDrawableFromDC(metaDrawable);
            hMetaFile = CloseEnhMetaFile(hDC);
            DeleteEnhMetaFile(hMetaFile);
            ReleaseDC(hWnd, hRefDC);
            return TCL_ERROR;
        }
        Rbc_WinFreeDrawableFromDC(metaDrawable);
        hMetaFile = CloseEnhMetaFile(hDC);
        if (hMetaFile == NULL) {
            ReleaseDC(hWnd, hRefDC);
            Rbc_AppendResultStrings(interp, "can't finish metafile: ", Rbc_LastError(), (char *)NULL);
            return TCL_ERROR;
        }
        result = TCL_ERROR;
        if (strcmp(data.name, "CLIPBOARD") == 0) {
            if (!OpenClipboard(hWnd)) {
                Rbc_AppendResultStrings(interp, "can't open clipboard: ", Rbc_LastError(), (char *)NULL);
            } else {
                if (!EmptyClipboard() || SetClipboardData(CF_ENHMETAFILE, hMetaFile) == NULL) {
                    Rbc_AppendResultStrings(interp, "can't set clipboard metafile: ", Rbc_LastError(), (char *)NULL);
                } else {
                    hMetaFile = NULL; /* The clipboard now owns the recording. */
                    result = TCL_OK;
                }
                CloseClipboard();
            }
        } else if (data.format == FORMAT_WMF) {
            APMHEADER mfh;

            assert(sizeof(mfh) == 22);
            if (InitMetaFileHeader(interp, graphPtr->tkwin, data.width, data.height, &mfh) == TCL_OK) {
                result = CreateAPMetaFile(interp, hMetaFile, hRefDC, &mfh, data.name);
            }
        } else {
            HENHMETAFILE hMetaFile2;

            hMetaFile2 = CopyEnhMetaFileA(hMetaFile, data.name);
            if (hMetaFile2 != NULL) {
                result = TCL_OK;
                DeleteEnhMetaFile(hMetaFile2);
            } else {
                Rbc_AppendResultStrings(interp, "can't copy metafile: ", Rbc_LastError(), (char *)NULL);
            }
        }
        if (hMetaFile != NULL) {
            DeleteEnhMetaFile(hMetaFile);
        }
        ReleaseDC(hWnd, hRefDC);
#endif /*WIN32*/
    } else {
        Rbc_AppendResultStrings(interp, "bad snapshot format", (char *)NULL);
        return TCL_ERROR;
    }
    return result;
}

/**
 * @ingroup graph_commands_drawing
 * @brief Capture a snapshot and restore screen geometry on every return path.
 *
 * Keep pre-existing dirty and idle-request flags. Restoration preserves the
 * capture error when capture failed, and reports layout failure otherwise.
 * A deleted window is never used for restoration or redraw scheduling.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int SnapOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int width = graphPtr->width, height = graphPtr->height;
    unsigned int flags = graphPtr->flags;
    int result;
    Tcl_InterpState state;

    graphPtr->flags |= GRAPH_EXPORT;
    result = SnapGraph(graphPtr, interp, objc, objv);
    state = Tcl_SaveInterpState(interp, result);
    graphPtr->width = width;
    graphPtr->height = height;
    graphPtr->flags |= flags | LAYOUT_NEEDED | MAP_WORLD | REDRAW_BACKING_STORE;
    if (!(flags & GRAPH_EXPORT)) {
        graphPtr->flags &= ~GRAPH_EXPORT;
    }
    if (graphPtr->tkwin != NULL) {
        if (!graphPtr->axisFormatInvalid) {
            int restored = Rbc_LayoutGraph(graphPtr);
            if (result == TCL_OK && restored != TCL_OK) {
                Tcl_DiscardInterpState(state);
                state = Tcl_SaveInterpState(interp, restored);
                result = restored;
            }
        }
        if (graphPtr->tkwin != NULL) {
            Rbc_EventuallyRedrawGraph(graphPtr);
        }
    }
    Tcl_RestoreInterpState(interp, state);
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
                                       {{"svg", 3, 0, "oper ?args?"}, Rbc_SvgOp},
                                       {{"pdf", 3, 0, "oper ?args?"}, Rbc_PdfOp},
                                       {{"track", 2, 0, "oper ?args?"}, Rbc_TrackOp},
                                       {{"strip", 2, 0, "oper ?args?"}, StripOp},
                                       {{"transform", 4, 4, "x y"}, TransformOp},
                                       {{"x2axis", 2, 0, "oper ?args?"}, X2AxisOp},
                                       {{"xaxis", 2, 0, "oper ?args?"}, XAxisOp},
                                       {{"y2axis", 2, 0, "oper ?args?"}, Y2AxisOp},
                                       {{"yaxis", 2, 0, "oper ?args?"}, YAxisOp},
                                       {{NULL, 0, 0, NULL}, NULL}

};

/**
 * @ingroup graph_commands_drawing
 * @brief Resolve a widget operation and validate its argument count.
 *
 * Uses the shared operation-table dispatcher at argument one. Returns a
 * borrowed static procedure pointer, or NULL with a Tcl diagnostic. Does not
 * execute the operation or retain any arguments.
 *
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static Rbc_GraphOpProc *GetGraphOpFromObj(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    int index;

    if (Rbc_GetOpIndexFromObj(interp, graphOps, (Tcl_Size)sizeof(graphOps[0]), RBC_OP_ARG1, objc, objv, &index) !=
        TCL_OK) {
        return NULL;
    }
    return graphOps[index].proc;
}

/**
 * @ingroup graph_commands_drawing
 * @brief Dispatch a graph widget command under graph preservation.
 *
 * Rejects widget-wide transforms for trackgraphs and recursive snapshot or
 * document output during axis formatting. Preserves the Graph allocation
 * across the selected handler, then releases it and returns the handler status.
 * Preservation does not keep the Tk window or separately owned components alive;
 * handlers crossing Tcl callbacks still need validity checks.
 *
 * @param[in] clientData Borrowed widget-owned Graph registered for this callback; no new ownership is acquired.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
int Rbc_GraphInstCmdProc(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_GraphOpProc *proc;
    int result;
    
    Graph *graphPtr = clientData;
    proc = GetGraphOpFromObj(interp, objc, objv);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    if (graphPtr->trackGraph && (proc == TransformOp || proc == InvtransformOp)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("use track transform or track invtransform to select a track", -1));
        return TCL_ERROR;
    }
    /* Export and snapshot setup changes dimensions before requesting layout.
     * Reject recursion before that setup can disturb an outer formatter. */
    if (graphPtr->axisFormatActive) {
        int renders = proc == SnapOp;
        if ((proc == Rbc_SvgOp || proc == Rbc_PdfOp || proc == Rbc_PostScriptOp) && objc > 2) {
            Tcl_Size length;
            const char *name = Tcl_GetStringFromObj(objv[2], &length);
            renders = length > 0 && length <= 6 && strncmp(name, "output", (size_t)length) == 0;
        }
        if (renders) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("recursive graph rendering in axis label callback", -1));
            Tcl_SetErrorCode(interp, "RBC", "GRAPH", "CALLBACK", NULL);
            return TCL_ERROR;
        }
    }
    Tcl_Preserve(graphPtr);
    result = proc(graphPtr, interp, objc, objv);
    Tcl_Release(graphPtr);
    return result;
}

/**
 * @ingroup graph_commands_drawing
 * @brief Create a widget and return its Tk pathname.
 *
 * Requires a pathname argument, then delegates construction to CreateGraph.
 * On failure propagates TCL_ERROR; on success installs a new pathname string
 * as the result. The created graph belongs to the widget lifecycle.
 *
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed constructor arguments: objv[1] is the pathname, followed by option/value pairs.
 * @param[in] classUid Interned widget/element class identifier selecting the constructor behavior.
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

/**
 * @ingroup graph_commands_drawing
 * @brief Create a graph widget through the common constructor.
 *
 * Passes rbcLineElementUid to NewGraph and returns its Tcl status unchanged.
 * The clientData argument is unused; construction owns the new widget resources.
 *
 * @param[in] clientData Unused constructor command data.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int GraphObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return NewGraph(interp, objc, objv, rbcLineElementUid);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Create a barchart widget through the common constructor.
 *
 * Passes rbcBarElementUid to NewGraph and returns its Tcl status unchanged.
 * The clientData argument is unused; construction owns the new widget resources.
 *
 * @param[in] clientData Unused constructor command data.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int BarchartObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return NewGraph(interp, objc, objv, rbcBarElementUid);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Create a stripchart widget through the common constructor.
 *
 * Passes rbcStripElementUid to NewGraph and returns its Tcl status unchanged.
 * The clientData argument is unused; construction owns the new widget resources.
 *
 * @param[in] clientData Unused constructor command data.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int StripchartObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return NewGraph(interp, objc, objv, rbcStripElementUid);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Create a polar widget through the common constructor.
 *
 * Passes rbcPolarElementUid to NewGraph and returns its Tcl status unchanged.
 * The clientData argument is unused; construction owns the new widget resources.
 *
 * @param[in] clientData Unused constructor command data.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int PolarObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return NewGraph(interp, objc, objv, rbcPolarElementUid);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Fill one positive-sized margin rectangle.
 *
 * Borrows the drawable and graph resources. Uses the tile when present,
 * otherwise the margin fill GC, through the screen renderer. Nonpositive
 * width or height is a no-op; this helper does not set tile origin.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in] drawable Borrowed compatible destination drawable; caller retains ownership and presentation
 *   responsibility.
 * @param[in] x Left margin rectangle coordinate in window pixels.
 * @param[in] y Top margin rectangle coordinate in window pixels.
 * @param[in] width Rectangle width in pixels; nonpositive values draw nothing.
 * @param[in] height Rectangle height in pixels; nonpositive values draw nothing.
 */
static void FillMarginRectangle(Graph *graphPtr, Drawable drawable, int x, int y, int width, int height) {
    if ((width <= 0) || (height <= 0)) {
        return;
    }
    if (graphPtr->tile != NULL) {
        Rbc_RenderScreenRectangle(graphPtr->tkwin, drawable, NULL, graphPtr->tile, x, y, width, height);
    } else {
        Rbc_RenderScreenRectangle(graphPtr->tkwin, drawable, graphPtr->fillGC, NULL, x, y, width, height);
    }
}

/**
 * @ingroup graph_commands_drawing
 * @brief Paint margins, the plot border, margin legend, title and axes.
 *
 * Sets tile origin to the widget origin, fills four surrounding rectangles,
 * then draws decorations in that order. Uses current mapped geometry and
 * borrowed resources. Opens and closes a screen-text context for the title;
 * does not recompute layout or present the drawable.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in] drawable Borrowed compatible destination drawable; caller retains ownership and presentation
 *   responsibility.
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
        Rbc_RenderScreenBorder(graphPtr->tkwin, drawable, graphPtr->border, x, y, width, height, graphPtr->plotBorderWidth,
                            graphPtr->plotRelief, 0);
    }
    if (Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_MARGIN) {
        Rbc_DrawLegend(graphPtr->legend, drawable);
    }
    if (graphPtr->title != NULL) {
        Rbc_RenderContext *text = Rbc_RenderBeginScreenText(graphPtr->tkwin, drawable);
        Rbc_RenderText(text, graphPtr->title, &graphPtr->titleTextStyle, graphPtr->titleX, graphPtr->titleY);
        Rbc_RenderEnd(text);
    }
    Rbc_DrawAxes(graphPtr, drawable);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Compose the cacheable plot layer in drawing order.
 *
 * Clears the inclusive plot rectangle, draws track separators and grids,
 * then Polar or Smith labels. After checking callback validity, draws under
 * markers, lowered local/global legends, axis limits and ordinary elements.
 * Borrows the drawable and mapped geometry. A void return does not indicate
 * callback success; callers must check Rbc_GraphCallbackValid.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 * @param[in] drawable Borrowed compatible destination drawable; caller retains ownership and presentation
 *   responsibility.
 */
static void DrawPlotRegion(Graph *graphPtr, Drawable drawable) {
    /* Clear the background of the plotting area. */
    Rbc_RenderScreenRectangle(graphPtr->tkwin, drawable, graphPtr->plotFillGC, NULL, graphPtr->left, graphPtr->top,
                              graphPtr->right - graphPtr->left + 1, graphPtr->bottom - graphPtr->top + 1);
    Rbc_DrawTrackSeparators(graphPtr, drawable, NULL);
    /* Draw the elements, markers, legend, and axis limits. */
    if (graphPtr->trackGraph || !graphPtr->gridPtr->hidden) {
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
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        return;
    }
    Rbc_DrawMarkers(graphPtr, drawable, MARKER_UNDER);
    Rbc_DrawTrackLegends(graphPtr, drawable, FALSE);
    if ((Rbc_LegendSite(graphPtr->legend) & LEGEND_IN_PLOT) && (!Rbc_LegendIsRaised(graphPtr->legend))) {
        Rbc_DrawLegend(graphPtr->legend, drawable);
    }
    Rbc_DrawAxisLimits(graphPtr, drawable);
    Rbc_DrawElements(graphPtr, drawable);
}

/* Layout callers must stop on cancellation before using component pointers. */
int Rbc_GraphCallbackValid(Graph *graphPtr) {
    if (graphPtr->tkwin == NULL || graphPtr->axisFormatInvalid) {
        Tcl_SetObjResult(graphPtr->interp,
                         Tcl_NewStringObj("graph changed during axis label callback", -1));
        Tcl_SetErrorCode(graphPtr->interp, "RBC", "GRAPH", "CALLBACK", NULL);
        return FALSE;
    }
    return TRUE;
}


int Rbc_LayoutGraph(Graph *graphPtr) {
    if (graphPtr->axisFormatActive) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("recursive graph layout in axis label callback", -1));
        Tcl_SetErrorCode(graphPtr->interp, "RBC", "GRAPH", "CALLBACK", NULL);
        return TCL_ERROR;
    }
    graphPtr->axisFormatInvalid = FALSE;
    Rbc_SyncElementVectors(graphPtr);
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        return TCL_ERROR;
    }
    /*
     * Record native view invalidation before mapping consumes the flags.
     * Layout may also be requested by a transform or picking operation,
     * so retain this state until DisplayGraph completes.
     *
     * Document export uses a temporary layout and must not announce
     * that layout as an on-screen view change.
     */
    if (!(graphPtr->flags & GRAPH_EXPORT) && (graphPtr->flags & (MAP_WORLD | LAYOUT_NEEDED))) {
        graphPtr->flags |= GRAPH_CHANGED;
    }
    if (graphPtr->flags & RESET_AXES) {
        Rbc_ResetAxes(graphPtr);
    }
    if (graphPtr->flags & LAYOUT_NEEDED) {
        Rbc_LayoutMargins(graphPtr);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return TCL_ERROR;
        }
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
        Rbc_MapTrackLegends(graphPtr);
        graphPtr->flags &= ~(MAP_ALL);
    }
    return TCL_OK;
}

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
            if (!Rbc_GraphCallbackValid(graphPtr)) {
                return;
            }
            graphPtr->flags &= ~REDRAW_BACKING_STORE;
        }
        /* Copy the pixmap to the one used for drawing the entire graph. */
        Rbc_RenderScreenCopyArea(graphPtr->tkwin, graphPtr->backPixmap, drawable, graphPtr->drawGC, graphPtr->left, graphPtr->top,
                  (graphPtr->right - graphPtr->left + 1), (graphPtr->bottom - graphPtr->top + 1), graphPtr->left,
                  graphPtr->top);
    } else {
        DrawPlotRegion(graphPtr, drawable);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return;
        }
    }
    /* Draw markers above elements */
    Rbc_DrawMarkers(graphPtr, drawable, MARKER_ABOVE);
    Rbc_DrawActiveElements(graphPtr, drawable);
    if (graphPtr->flags & DRAW_MARGINS) {
        DrawMargins(graphPtr, drawable);
    }
    Rbc_DrawTrackLegends(graphPtr, drawable, TRUE);
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
            Rbc_RenderScreenBorder(graphPtr->tkwin, drawable, graphPtr->border, graphPtr->highlightWidth,
                                graphPtr->highlightWidth, Rbc_ClampToInt(width), Rbc_ClampToInt(height),
                                graphPtr->borderWidth, graphPtr->relief, 0);
        }
    }
    /* Draw focus highlight ring. */
    if ((graphPtr->highlightWidth > 0) && (graphPtr->flags & GRAPH_FOCUS)) {
        GC gc;

        gc = Tk_GCForColor(graphPtr->highlightColor, drawable);
        Rbc_RenderScreenFocus(graphPtr->tkwin, drawable, gc, graphPtr->highlightWidth);
    }
}

/**
 * @ingroup graph_commands_drawing
 * @brief Publish cached margin dimensions to global Tcl variables.
 *
 * Writes left/right widths and top/bottom heights for configured variables.
 * Copies names and sizes before invoking traces so configuration changes cannot
 * invalidate borrowed option strings. Stops writes and returns false if a trace
 * destroys the widget or cancels layout. Assignment errors remain ignored for
 * compatibility. The display wrapper preserves graph and interpreter storage.
 *
 * @param[in,out] graphPtr Borrowed widget-owned graph; required configuration or mapping state is described
 *   above.
 */
static int UpdateMarginTraces(Graph *graphPtr) {
    Tcl_Obj *names[4] = {NULL, NULL, NULL, NULL};
    int sizes[4];
    int i, valid = TRUE;

    /* Traces may replace option strings or destroy the widget. Snapshot all
     * names before the first write, and never borrow them across a trace. */
    for (i = 0; i < 4; i++) {
        Margin *marginPtr = graphPtr->margins + i;
        if (marginPtr->varName != NULL) {
            names[i] = Tcl_NewStringObj(marginPtr->varName, -1);
            Tcl_IncrRefCount(names[i]);
            sizes[i] = ((marginPtr->site == MARGIN_LEFT) || (marginPtr->site == MARGIN_RIGHT))
                           ? marginPtr->width : marginPtr->height;
        }
    }
    for (i = 0; i < 4; i++) {
        if (names[i] != NULL) {
            Tcl_Obj *value = Tcl_NewIntObj(sizes[i]);
            Tcl_IncrRefCount(value);
            Tcl_ObjSetVar2(graphPtr->interp, names[i], NULL, value, TCL_GLOBAL_ONLY);
            Tcl_DecrRefCount(value);
            if (graphPtr->tkwin == NULL || graphPtr->axisFormatInvalid) {
                valid = FALSE;
                break;
            }
        }
    }
    for (i = 0; i < 4; i++) {
        if (names[i] != NULL) {
            Tcl_DecrRefCount(names[i]);
        }
    }
    return valid;
}

/**
 * @ingroup graph_lifecycle_callbacks
 * @brief Build one screen frame after the idle request is consumed.
 * @param[in] clientData Graph retained by the DisplayGraph wrapper.
 *
 * Clear REDRAW_PENDING even when drawing is deferred for vector notifications
 * or skipped during axis formatting. Sample Tk dimensions and lay out before
 * testing whether the window is mapped. Buffered drawing composes crosshairs
 * before presentation; direct drawing uses their erase/update/enable cycle.
 * Successful frames queue view-change events and update margin/view callbacks.
 */
static void DisplayGraphFrame(ClientData clientData) {
    Graph *graphPtr = clientData;
    Pixmap drawable;

    graphPtr->flags &= ~REDRAW_PENDING;
    if (graphPtr->tkwin == NULL || graphPtr->axisFormatActive) {
        return; /* Destroyed, or a formatter entered a nested event loop. */
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
    if (Rbc_LayoutGraph(graphPtr) != TCL_OK) {
        return;
    }
    if (!Tk_IsMapped(graphPtr->tkwin)) {
        Rbc_UpdateCrosshairs(graphPtr);
        return;
    }
    if (graphPtr->doubleBuffer) {
        /*
         * Check the old segment geometry before updating it.
         * A full copy may be needed to remove old crosshair pixels
         * outside the current plotting area.
         *
         * Request margin rendering as well, so every pixel in a
         * full-widget copy has been initialized.
         */
        if (Rbc_CrosshairsNeedFullRedraw(graphPtr)) {
            graphPtr->flags |= DRAW_MARGINS;
        }
        drawable = Tk_GetPixmap(graphPtr->display, Tk_WindowId(graphPtr->tkwin), graphPtr->width, graphPtr->height,
                                Tk_Depth(graphPtr->tkwin));
#ifdef WIN32
        assert(drawable != None);
#endif
        /*
         * Leave the visible window untouched while preparing the
         * complete frame. The element backing store contains only
         * graph content, never crosshairs.
         */
        Rbc_DrawGraph(graphPtr, drawable, graphPtr->backingStore);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            Tk_FreePixmap(graphPtr->display, drawable);
            return;
        }
        /*
         * Compose the hairs into the final pixmap and present once.
         * Do not erase or enable window crosshairs around this call.
         */
        Rbc_PresentGraphWithCrosshairs(graphPtr, drawable);
        Tk_FreePixmap(graphPtr->display, drawable);
    } else {
        /*
         * Direct rendering retains the existing XOR lifecycle.
         * Erase using the old endpoints before updating geometry.
         */
        Rbc_DisableCrosshairs(graphPtr);
        Rbc_UpdateCrosshairs(graphPtr);
        drawable = Tk_WindowId(graphPtr->tkwin);
        Rbc_DrawGraph(graphPtr, drawable, FALSE);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return;
        }
        Rbc_EnableCrosshairs(graphPtr);
    }
    graphPtr->flags &= ~RESET_WORLD;
    /*
     * Notify after the native display has consumed the mapped geometry.
     * Tk_SendVirtualEvent queues the event; it does not execute Tcl
     * bindings synchronously here.
     *
     * Keep GRAPH_CHANGED pending while the plot is too small to map.
     */
    if ((graphPtr->flags & GRAPH_CHANGED) && !(graphPtr->flags & GRAPH_EXPORT) && (graphPtr->hRange > 1) &&
        (graphPtr->vRange > 1)) {
        graphPtr->flags &= ~GRAPH_CHANGED;
        Tk_SendVirtualEvent(graphPtr->tkwin, "RbcGraphChanged", NULL);
    }
    if (!(graphPtr->flags & GRAPH_EXPORT) && (graphPtr->hRange > 1) && (graphPtr->vRange > 1)) {
        Rbc_NotifyAxisChanges(graphPtr);
    }
    if (!UpdateMarginTraces(graphPtr)) {
        return;
    }
    Rbc_NotifyTrackView(graphPtr);
}

/**
 * @ingroup graph_lifecycle_callbacks
 * @brief Run idle drawing with graph/interpreter lifetime and result protection.
 * @param[in] clientData Graph record belonging to the pending idle request.
 *
 * Preserve both records and save the interpreter state around DisplayGraphFrame.
 * Restore the interpreter state before releasing the records; do not use them
 * after release. Preservation protects storage, not Tk resources or components
 * that callbacks may destroy. Frame code must still validate callback state.
 */
static void DisplayGraph(ClientData clientData) {
    Graph *graphPtr = clientData;
    Tcl_Interp *interp = graphPtr->interp;
    Tcl_InterpState state;
    Tcl_Preserve(graphPtr);
    Tcl_Preserve(interp);
    state = Tcl_SaveInterpState(interp, TCL_OK);
    DisplayGraphFrame(clientData);
    Tcl_RestoreInterpState(interp, state);
    Tcl_Release(graphPtr);
    Tcl_Release(interp);
}

/**
 * @ingroup graph_commands_drawing
 * @brief Create a trackgraph widget through the common constructor.
 *
 * Passes trackGraphUid to NewGraph and returns its Tcl status unchanged.
 * The clientData argument is unused; construction owns the new widget resources.
 *
 * @param[in] clientData Unused constructor command data.
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
 * @param[in] objc Number of borrowed objv entries; see @ref rbc_command_arguments.
 * @param[in] objv Borrowed argument vector; command-specific positions are described above. See @ref
 *   rbc_command_arguments.
 */
static int TrackgraphObjCmd(void *clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    return NewGraph(interp, objc, objv, trackGraphUid);
}

/**
 * @ingroup graph_lifecycle_support
 * @brief Intern component class identifiers and register graph constructors.
 *
 * Creates graph, barchart, stripchart, polar and trackgraph Tcl commands
 * in the interpreter. Initializes class identifiers including the contour
 * identifier, but does not register a contour widget constructor. Returns
 * TCL_OK; no graph widget is created here.
 *
 * @param[in,out] interp Borrowed interpreter for results and diagnostics; see @ref rbc_interp_argument.
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
    rbcArcMarkerUid = (Rbc_Uid)Tk_GetUid("ArcMarker");
    rbcPolygonMarkerUid = (Rbc_Uid)Tk_GetUid("PolygonMarker");
    rbcWindowMarkerUid = (Rbc_Uid)Tk_GetUid("WindowMarker");
    rbcXAxisUid = (Rbc_Uid)Tk_GetUid("X");
    rbcYAxisUid = (Rbc_Uid)Tk_GetUid("Y");
    Tcl_CreateObjCommand2(interp, "rbc::graph", GraphObjCmd, (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
    trackGraphUid = (Rbc_Uid)Tk_GetUid("Trackgraph");
    Tcl_CreateObjCommand2(interp, "rbc::trackgraph", TrackgraphObjCmd, NULL, NULL);
    Tcl_CreateObjCommand2(interp, "rbc::barchart", BarchartObjCmd, (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
    Tcl_CreateObjCommand2(interp, "rbc::stripchart", StripchartObjCmd, (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
    Tcl_CreateObjCommand2(interp, "rbc::polar", PolarObjCmd, (ClientData)NULL, (Tcl_CmdDeleteProc *)NULL);
    return TCL_OK;
}

/**
 * @ingroup graph_lifecycle_support
 * @brief Find graph instance data on a window or its ancestors.
 *
 * Walks Tk_Parent until non-NULL RBC window instance data is found. Returns
 * a borrowed Graph pointer, or NULL when the chain is empty. Does not retain
 * the graph or verify a component class; callers must supply a window chain
 * whose RBC instance data follows the graph convention.
 *
 * @param[in] tkwin Borrowed starting window, or NULL for an empty ancestry chain.
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
