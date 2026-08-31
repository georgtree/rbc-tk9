/*
 * rbcGraph.h --
 *
 *      TODO: Description
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCGRAPH
#define _RBCGRAPH

#include "rbcInt.h"
#include "rbcBind.h"
#include "rbcChain.h"
#include "rbcPs.h"
#include "rbcTile.h"
#include "rbcVector.h"
/*#include "rbcWin.h"*/

typedef struct GraphStruct Graph;
typedef struct ElementStruct Element;
typedef struct LegendStruct Legend;

#include "rbcGrAxis.h"
#include "rbcGrLegd.h"

#define MARKER_UNDER                                                                                                   \
    1 /* Draw markers designated to lie underneath                                                                     \
       * elements, grids, legend, etc. */
#define MARKER_ABOVE                                                                                                   \
    0 /* Draw markers designated to rest above                                                                         \
       * elements, grids, legend, etc. */

#define PADX 2 /* Padding between labels/titles */
#define PADY 2 /* Padding between labels */

#define MINIMUM_MARGIN 20 /* Minimum margin size */

#define BOUND(x, lo, hi) (((x) > (hi)) ? (hi) : ((x) < (lo)) ? (lo) : (x))

/*
 * -------------------------------------------------------------------
 *
 *     Graph component structure definitions
 *
 * -------------------------------------------------------------------
 */
#define PointInGraph(g, x, y) (((x) <= (g)->right) && ((x) >= (g)->left) && ((y) <= (g)->bottom) && ((y) >= (g)->top))

/*
 * -------------------------------------------------------------------
 *
 * ClassType --
 *
 *    Enumerates the different types of graph elements this program
 *    produces.  An element can be either a line or a bar.
 *
 * -------------------------------------------------------------------
 */
typedef enum {
    CLASS_UNKNOWN,
    CLASS_LINE_ELEMENT,
    CLASS_STRIP_ELEMENT,
    CLASS_BAR_ELEMENT,
    CLASS_BITMAP_MARKER,
    CLASS_IMAGE_MARKER,
    CLASS_LINE_MARKER,
    CLASS_POLYGON_MARKER,
    CLASS_TEXT_MARKER,
    CLASS_WINDOW_MARKER

} ClassType;

/*
 * Mask values used to selectively enable GRAPH or BARCHART entries in
 * the various configuration specs.
 */
#define GRAPH (TK_CONFIG_USER_BIT << 1)
#define STRIPCHART (TK_CONFIG_USER_BIT << 2)
#define BARCHART (TK_CONFIG_USER_BIT << 3)
#define POLAR (TK_CONFIG_USER_BIT << 4)
#define LINE_GRAPHS (GRAPH | STRIPCHART | POLAR)
#define ALL_GRAPHS (GRAPH | BARCHART | STRIPCHART | POLAR)

#define PEN_DELETE_PENDING (1 << 0)
#define ACTIVE_PEN (TK_CONFIG_USER_BIT << 6)
#define NORMAL_PEN (TK_CONFIG_USER_BIT << 7)
#define ALL_PENS (NORMAL_PEN | ACTIVE_PEN)

typedef struct {
    Tk_Anchor anchor;
    int isAuto;
} PolarLabelAnchor;

typedef enum { POLAR_REPRESENTATION_POLAR, POLAR_REPRESENTATION_SMITH } PolarRepresentation;

typedef enum { SMITH_GRID_IMPEDANCE, SMITH_GRID_ADMITTANCE, SMITH_GRID_BOTH } SmithGridMode;

/*
 * -------------------------------------------------------------------
 *
 * FreqInfo --
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    Tcl_Size freq; /* Number of occurrences of x-coordinate */
    Axis2D axes;   /* Indicates which x and y axis are mapped to
                    * the x-value */
    double sum;    /* Sum of the ordinates of each duplicate
                    * abscissa */
    Tcl_Size count;
    double lastY;

} FreqInfo;

/*
 * -------------------------------------------------------------------
 *
 * FreqKey --
 *
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    double value; /* Duplicated abscissa */
    Axis2D axes;  /* Axis mapping of element */
} FreqKey;

/*
 * BarModes --
 *
 *    Bar elements are displayed according to their x-y coordinates.
 *    If two bars have the same abscissa (x-coordinate), the bar
 *    segments will be drawn according to one of the following
 *    modes:
 */

typedef enum BarModes {
    MODE_INFRONT, /* Each successive segment is drawn in
                   * front of the previous. */
    MODE_STACKED, /* Each successive segment is drawn
                   * stacked above the previous. */
    MODE_ALIGNED, /* Each successive segment is drawn
                   * aligned to the previous from
                   * right-to-left. */
    MODE_OVERLAP  /* Like "aligned", each successive segment
                   * is drawn from right-to-left. In addition
                   * the segments will overlap each other
                   * by a small amount */
} BarMode;

typedef struct PenStruct Pen;
typedef struct MarkerStruct Marker;

typedef Pen *(PenCreateProc)(void);
typedef int(PenConfigureProc)(Graph *graphPtr, Pen *penPtr);
typedef void(PenDestroyProc)(Graph *graphPtr, Pen *penPtr);

struct PenStruct {
    char *name;
    Rbc_Uid classUid;
    char *typeId;
    unsigned int flags;
    int refCount;
    Tcl_HashEntry *hashPtr;

    const Tk_OptionSpec *optionSpecs;
    Tk_OptionTable optionTable;

    int optionsInitialized;
    int tkResourcesReleased;

    PenConfigureProc *configProc;
    PenDestroyProc *destroyProc;
};

typedef enum { PS_MONO_BACKGROUND, PS_MONO_FOREGROUND } MonoAttribute;

/*
 * PostScript --
 *
 *     Structure contains information specific to the outputting of
 *    PostScript commands to print the graph.
 *
 */
typedef struct {
    /*
     * Modern Tk option state.
     */
    Tk_OptionTable optionTable;

    /*
     * Original Tcl representations.
     */
    Tcl_Obj *colorModeObjPtr;
    Tcl_Obj *heightObjPtr;
    Tcl_Obj *padXObjPtr;
    Tcl_Obj *padYObjPtr;
    Tcl_Obj *paperHeightObjPtr;
    Tcl_Obj *paperWidthObjPtr;
    Tcl_Obj *previewFormatObjPtr;
    Tcl_Obj *widthObjPtr;
    
    /* User configurable fields */

    int decorations; /* If non-zero, print graph with
                      * color background and 3D borders */

    int reqWidth, reqHeight; /* If greater than zero, represents the
                              * requested dimensions of the printed graph */
    int reqPaperWidth;
    int reqPaperHeight;    /* Requested dimensions for the PostScript
                            * page. Can constrain the size of the graph
                            * if the graph (plus padding) is larger than
                            * the size of the page. */
    Rbc_Pad padX, padY;    /* Requested padding on the exterior of the
                            * graph. This forms the bounding box for
                            * the page. */
    PsColorMode colorMode; /* Selects the color mode for PostScript page
                            * (0=monochrome, 1=greyscale, 2=color) */
    char *colorVarName;    /* If non-NULL, is the name of a Tcl array
                            * variable containing X to PostScript color
                            * translations */
    char *fontVarName;     /* If non-NULL, is the name of a Tcl array
                            * variable containing X to PostScript font
                            * translations */
    int landscape;         /* If non-zero, orient the page 90 degrees */
    int center;            /* If non-zero, center the graph on the page */
    int maxpect;           /* If non-zero, indicates to scale the graph
                            * so that it fills the page (maintaining the
                            * aspect ratio of the graph) */
    int addPreview;        /* If non-zero, generate a preview image and
                            * add it to the PostScript output */
    int footer;            /* If non-zero, a footer with the title, date
                            * and user will be added to the PostScript
                            * output outside of the bounding box. */
    int previewFormat;     /* Format of EPS preview:
                            * PS_PREVIEW_WMF, PS_PREVIEW_EPSI, or
                            * PS_PREVIEW_TIFF. */

    /* Computed fields */

    int left, bottom; /* Bounding box of PostScript plot. */
    int right, top;

    double pageScale; /* Scale of page. Set if "-maxpect" option
                       * is set, otherwise 1.0. */
} PostScript;

/*
 * -------------------------------------------------------------------
 *
 * Grid
 *
 *    Contains attributes of describing how to draw grids (at major
 *    ticks) in the graph.  Grids may be mapped to either/both x and
 *    y axis.
 *
 * -------------------------------------------------------------------
 */
typedef struct {
    /*
     * Modern Tk option state.
     */
    Tk_OptionTable optionTable;

    /*
     * Original Tcl option values. These are owned and reference-counted
     * by Tk_InitOptions(), Tk_SetOptions(), and Tk_FreeConfigOptions().
     */
    Tcl_Obj *dashesObjPtr;
    Tcl_Obj *lineWidthObjPtr;
    Tcl_Obj *mapXObjPtr;
    Tcl_Obj *mapYObjPtr;

    /*
     * Derived/internal state.
     */
    GC gc; /* Graphics context for the grid. */
    Axis2D axes;

    int hidden;    /* If non-zero, grid isn't displayed. */
    int minorGrid; /* If non-zero, draw minor grid lines. */

    Rbc_Dashes dashes; /* Parsed dash pattern. */
    int lineWidth;     /* Line width in pixels. */
    XColor *colorPtr;  /* Grid colour. */

    struct GridSegments {
        Segment2D *segments;
        Tcl_Size nSegments;
    } x, y;
} Grid;

/*
 * -------------------------------------------------------------------
 *
 * Crosshairs
 *
 *    Contains the line segments positions and graphics context used
 *    to simulate crosshairs (by XOR-ing) on the graph.
 *
 * -------------------------------------------------------------------
 */
typedef struct CrosshairsStruct Crosshairs;

typedef struct {
    int width, height; /* Extents of the margin */

    int axesOffset;
    int axesTitleLength; /* Width of the widest title to be shown.
                          * Multiple titles are displayed in
                          * another margin. This is the minimum
                          * space requirement. */
    Tcl_Size nAxes;      /* Number of axes to be displayed */
    Rbc_Chain *axes;     /* Extra axes associated with this margin */

    char *varName; /* If non-NULL, name of variable to be
                    * updated when the margin size changes */

    int reqSize; /* Requested size of margin */
    int site;    /* Indicates where margin is located:
                  * left/right/top/bottom. */
} Margin;

#define MARGIN_NONE -1
#define MARGIN_BOTTOM 0
#define MARGIN_LEFT 1
#define MARGIN_TOP 2
#define MARGIN_RIGHT 3

#define rightMargin margins[MARGIN_RIGHT]
#define leftMargin margins[MARGIN_LEFT]
#define topMargin margins[MARGIN_TOP]
#define bottomMargin margins[MARGIN_BOTTOM]

/*
 * -------------------------------------------------------------------
 *
 * Graph --
 *
 *    Top level structure containing everything pertaining to
 *    the graph.
 *
 * -------------------------------------------------------------------
 */
struct GraphStruct {
    unsigned int flags;   /* Flags;  see below for definitions. */
    Tcl_Interp *interp;   /* Interpreter associated with graph */
    Tk_Window tkwin;      /* Window that embodies the graph.  NULL
                           * means that the window has been
                           * destroyed but the data structures
                           * haven't yet been cleaned up. */
    Display *display;     /* Display containing widget; needed,
                           * among other things, to release
                           * resources after tkwin has already gone
                           * away. */
    Tcl_Command cmdToken; /* Token for graph's widget command. */

    /*
     * Modern Tk option state.
     */
    Tk_OptionTable optionTable;

    /*
     * Original Tcl representations for values requiring additional
     * conversion or validation.
     */
    Tcl_Obj *barModeObjPtr;
    Tcl_Obj *borderWidthObjPtr;
    Tcl_Obj *bottomMarginObjPtr;
    Tcl_Obj *haloObjPtr;
    Tcl_Obj *heightObjPtr;
    Tcl_Obj *highlightWidthObjPtr;
    Tcl_Obj *leftMarginObjPtr;
    Tcl_Obj *plotBorderWidthObjPtr;
    Tcl_Obj *plotPadXObjPtr;
    Tcl_Obj *plotPadYObjPtr;
    Tcl_Obj *rightMarginObjPtr;
    Tcl_Obj *shadowObjPtr;
    Tcl_Obj *tileObjPtr;
    Tcl_Obj *topMarginObjPtr;
    Tcl_Obj *widthObjPtr;
    Tcl_Obj *angleMajorTicksObjPtr;
    Tcl_Obj *angleMinorTicksObjPtr;
    Tcl_Obj *angleCommandObjPtr;
    Tcl_Obj *smithRealCommandObjPtr;
    Tcl_Obj *smithImagCommandObjPtr;
    Tcl_Obj *smithRealMajorTicksObjPtr;
    Tcl_Obj *smithRealMinorTicksObjPtr;
    Tcl_Obj *smithImagMajorTicksObjPtr;
    Tcl_Obj *smithImagMinorTicksObjPtr;

    /*
     * Temporary context for a modern option transaction.
     */
    unsigned int optionMask;
    Tcl_Size optionObjc;
    Tcl_Obj *const *optionObjv;

    int optionsConfigured;

    int optionsInitialized;
    int tkResourcesReleased;

    char *data; /* This value isn't used in C code.
                 * It may be used in Tcl bindings to
                 * associate extra data. */

    Tk_Cursor cursor;

    int inset; /* Sum of focus highlight and 3-D
                * border.  Indicates how far to
                * offset the graph from outside
                * edge of the window. */

    int borderWidth;    /* Width of the exterior border */
    int relief;         /* Relief of the exterior border */
    Tk_3DBorder border; /* 3-D border used to delineate the plot
                         * surface and outer edge of window */

    int highlightWidth;       /* Width in pixels of highlight to draw
                               * around widget when it has the focus.
                               * <= 0 means don't draw a highlight. */
    XColor *highlightBgColor; /* Color for drawing traversal highlight
                               * area when highlight is off. */
    XColor *highlightColor;   /* Color for drawing traversal highlight. */

    char *title;
    int titleX, titleY;
    TextStyle titleTextStyle; /* Graph title */

    char *takeFocus;

    int reqWidth, reqHeight; /* Requested size of graph window */
    int width, height;       /* Size of graph window or PostScript
                              * page */

    Tcl_HashTable penTable; /* Table of pens */

    struct Component {
        Tcl_HashTable table;    /* Hash table of ids. */
        Rbc_Chain *displayList; /* Display list. */
        Tcl_HashTable tagTable; /* Table of bind tags. */
    } elements, markers, axes;

    Rbc_Uid classUid; /* Default element type */

    Rbc_BindTable bindTable;
    int nextMarkerId; /* Tracks next marker identifier available */

    Rbc_Chain *axisChain[4]; /* Chain of axes for each of the
                              * margins.  They're separate from the
                              * margin structures to make it easier
                              * to invert the X-Y axes by simply
                              * switching chain pointers.
                              */
    Margin margins[4];

    PostScript *postscript; /* PostScript options: see rbcGrPS.c */
    Legend *legend;         /* Legend information: see rbcGrLegd.c */
    Crosshairs *crosshairs; /* Crosshairs information: see rbcGrHairs.c */
    Grid *gridPtr;          /* Grid attribute information */

    int halo;     /* Maximum distance allowed between points
                   * when searching for a point */
    int inverted; /* If non-zero, indicates the x and y axis
                   * positions should be inverted. */
    Rbc_Tile tile;
    GC drawGC;           /* Used for drawing on the margins. This
                          * includes the axis lines */
    GC fillGC;           /* Used to fill the background of the
                          * margins. The fill is governed by
                          * the background color or the tiled
                          * pixmap. */
    int plotBorderWidth; /* Width of interior 3-D border. */
    int plotRelief;      /* 3-d effect: TK_RELIEF_RAISED etc. */
    XColor *plotBg;      /* Color of plotting surface */

    GC plotFillGC; /* Used to fill the plotting area with a
                    * solid background color. The fill color
                    * is stored in "plotBg". */

    /* If non-zero, force plot to conform to aspect ratio W/H */
    double aspect;

    /*
     * Polar-specific presentation options.
     */
    PolarRepresentation representation;
    SmithGridMode smithGrid;
    PolarLabelAnchor radialLabelAnchor;
    PolarLabelAnchor angleLabelAnchor;
    double *angleMajorTicks;
    Tcl_Size nAngleMajorTicks;
    double *angleMinorTicks;
    Tcl_Size nAngleMinorTicks;
    double *smithRealMajorTicks;
    Tcl_Size nSmithRealMajorTicks;
    double *smithRealMinorTicks;
    Tcl_Size nSmithRealMinorTicks;
    double *smithImagMajorTicks;
    Tcl_Size nSmithImagMajorTicks;
    double *smithImagMinorTicks;
    Tcl_Size nSmithImagMinorTicks;

    int left, right; /* Coordinates of plot bounding box. */
    int top, bottom;

    Rbc_Pad padX;        /* Vertical padding for plotarea */
    int vRange, vOffset; /* Vertical axis range and offset from the
                          * left side of the graph window. Used to
                          * transform coordinates to vertical
                          * axes. */
    Rbc_Pad padY;        /* Horizontal padding for plotarea */
    int hRange, hOffset; /* Horizontal axis range and offset from
                          * the top of the graph window. Used to
                          * transform horizontal axes */
    double vScale, hScale;

    int doubleBuffer;          /* If non-zero, draw the graph into a pixmap
                                * first to reduce flashing. */
    int backingStore;          /* If non-zero, cache elements by drawing
                                * them into a pixmap */
    Pixmap backPixmap;         /* Pixmap used to cache elements
                                * displayed.  If *backingStore* is
                                * non-zero, each element is drawn
                                * into this pixmap before it is
                                * copied onto the screen.  The pixmap
                                * then acts as a cache (only the
                                * pixmap is redisplayed if the none
                                * of elements have changed). This is
                                * done so that markers can be redrawn
                                * quickly over elements without
                                * redrawing each element. */
    int backWidth, backHeight; /* Size of element backing store pixmap. */

    /*
     * barchart specific information
     */
    double baseline;         /* Baseline from bar chart.  */
    double barWidth;         /* Default width of each bar in graph units.
                              * The default width is 1.0 units. */
    BarMode mode;            /* Mode describing how to display bars
                              * with the same x-coordinates. Mode can
                              * be "stack", "align", or "normal" */
    FreqInfo *freqArr;       /* Contains information about duplicate
                              * x-values in bar elements (malloc-ed).
                              * This information can also be accessed
                              * by the frequency hash table */
    Tcl_HashTable freqTable; /* */
    Tcl_Size nStacks;        /* Number of entries in frequency array.
                              * If zero, indicates nothing special needs
                              * to be done for "stack" or "align" modes */
    char *dataCmd;           /* New data callback? */
};

/*
 * Bit flags definitions:
 *
 *     All kinds of state information kept here.  All these
 *    things happen when the window is available to draw into
 *    (DisplayGraph). Need the window width and height before
 *    we can calculate graph layout (i.e. the screen coordinates
 *    of the axes, elements, titles, etc). But we want to do this
 *    only when we have to, not every time the graph is redrawn.
 *
 *    Same goes for maintaining a pixmap to double buffer graph
 *    elements.  Need to mark when the pixmap needs to updated.
 *
 *
 *    MAP_ITEM        Indicates that the element/marker/axis
 *                configuration has changed such that
 *                its layout of the item (i.e. its
 *                position in the graph window) needs
 *                to be recalculated.
 *
 *    MAP_ALL            Indicates that the layout of the axes and
 *                all elements and markers and the graph need
 *                to be recalculated. Otherwise, the layout
 *                of only those markers and elements that
 *                have changed will be reset.
 *
 *    GET_AXIS_GEOMETRY    Indicates that the size of the axes needs
 *                to be recalculated.
 *
 *    RESET_AXES        Flag to call to Rbc_ResetAxes routine.
 *                This routine recalculates the scale offset
 *                (used for mapping coordinates) of each axis.
 *                If an axis limit has changed, then it sets
 *                flags to re-layout and redraw the entire
 *                graph.  This needs to happend before the axis
 *                can compute transformations between graph and
 *                screen coordinates.
 *
 *    LAYOUT_NEEDED
 *
 *    REDRAW_BACKING_STORE    If set, redraw all elements into the pixmap
 *                used for buffering elements.
 *
 *    REDRAW_PENDING        Non-zero means a DoWhenIdle handler has
 *                already been queued to redraw this window.
 *
 *    DRAW_LEGEND        Non-zero means redraw the legend. If this is
 *                the only DRAW_* flag, the legend display
 *                routine is called instead of the graph
 *                display routine.
 *
 *    DRAW_MARGINS        Indicates that the margins bordering
 *                the plotting area need to be redrawn.
 *                The possible reasons are:
 *
 *                1) an axis configuration changed
 *                2) an axis limit changed
 *                3) titles have changed
 *                4) window was resized.
 *
 *    GRAPH_FOCUS
 */

#define MAP_ITEM (1 << 0)          /* 0x0001 */
#define MAP_ALL (1 << 1)           /* 0x0002 */
#define GET_AXIS_GEOMETRY (1 << 2) /* 0x0004 */
#define RESET_AXES (1 << 3)        /* 0x0008 */
#define LAYOUT_NEEDED (1 << 4)     /* 0x0010 */

#define REDRAW_PENDING (1 << 8)        /* 0x0100 */
#define DRAW_LEGEND (1 << 9)           /* 0x0200 */
#define DRAW_MARGINS (1 << 10)         /* 0x0400 */
#define REDRAW_BACKING_STORE (1 << 11) /* 0x0800 */

#define GRAPH_FOCUS (1 << 12)  /* 0x1000 */
#define DATA_CHANGED (1 << 13) /* 0x2000 */

#define MAP_WORLD (MAP_ALL | RESET_AXES | GET_AXIS_GEOMETRY)
#define REDRAW_WORLD (DRAW_MARGINS | DRAW_LEGEND)
#define RESET_WORLD (REDRAW_WORLD | MAP_WORLD)

/*
 * ---------------------- Forward declarations ------------------------
 */

typedef int Rbc_GraphOpProc(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

Tcl_ObjCmdProc2 Rbc_GraphInstCmdProc;
int Rbc_CreatePostScript(Graph *graphPtr);
int Rbc_CreateCrosshairs(Graph *graphPtr);
int Rbc_CreateGrid(Graph *graphPtr);
double Rbc_InvHMap(Graph *graphPtr, Axis *axisPtr, double x);
double Rbc_InvVMap(Graph *graphPtr, Axis *axisPtr, double x);
double Rbc_HMap(Graph *graphPtr, Axis *axisPtr, double x);
double Rbc_VMap(Graph *graphPtr, Axis *axisPtr, double y);
Point2D Rbc_InvMap2D(Graph *graphPtr, double x, double y, Axis2D *pairPtr);
Point2D Rbc_Map2D(Graph *graphPtr, double x, double y, Axis2D *pairPtr);
Graph *Rbc_GetGraphFromWindowData(Tk_Window tkwin);
int Rbc_LineRectClip(const Extents2D *extsPtr, const Point2D *p, const Point2D *q, Segment2D *clippedPtr);
Tcl_Size Rbc_PolyRectClip(const Extents2D *extsPtr, const Point2D *inputPts, Tcl_Size nInputPts, Point2D *outputPts,
                          Tcl_Size outputCapacity);
void Rbc_ReleaseLegendTkResources(Graph *graphPtr);

void Rbc_ComputeStacks(Graph *graphPtr);
void Rbc_ConfigureCrosshairs(Graph *graphPtr);
void Rbc_DestroyAxes(Graph *graphPtr);
void Rbc_DestroyCrosshairs(Graph *graphPtr);
void Rbc_DestroyGrid(Graph *graphPtr);
void Rbc_DestroyElements(Graph *graphPtr);
void Rbc_DestroyMarkers(Graph *graphPtr);
void Rbc_ReleaseMarkerTkResources(Graph *graphPtr);
void Rbc_DestroyPostScript(Graph *graphPtr);
void Rbc_DrawAxes(Graph *graphPtr, Drawable drawable);
void Rbc_DrawAxisLimits(Graph *graphPtr, Drawable drawable);
void Rbc_DrawElements(Graph *graphPtr, Drawable drawable);
void Rbc_DrawActiveElements(Graph *graphPtr, Drawable drawable);
void Rbc_DrawGraph(Graph *graphPtr, Drawable drawable, int backingStore);
void Rbc_DrawGrid(Graph *graphPtr, Drawable drawable);
void Rbc_SmithLabelsToPostScript(Graph *graphPtr, PsToken psToken);
void Rbc_DrawMarkers(Graph *graphPtr, Drawable drawable, int under);
void Rbc_Draw2DSegments(Display *display, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size nSegments);
void Rbc_InitFreqTable(Graph *graphPtr);
void Rbc_LayoutGraph(Graph *graphPtr);
void Rbc_LayoutMargins(Graph *graphPtr);
void Rbc_EventuallyRedrawGraph(Graph *graphPtr);
void Rbc_ResetAxes(Graph *graphPtr);
void Rbc_ResetStacks(Graph *graphPtr);
void Rbc_GraphExtents(Graph *graphPtr, Extents2D *extsPtr);
void Rbc_DisableCrosshairs(Graph *graphPtr);
void Rbc_EnableCrosshairs(Graph *graphPtr);
void Rbc_MapAxes(Graph *graphPtr);
void Rbc_MapElements(Graph *graphPtr);
void Rbc_MapMarkers(Graph *graphPtr);
void Rbc_MapGrid(Graph *graphPtr);
void Rbc_MapPolarGrid(Graph *graphPtr, Grid *gridPtr);
void Rbc_MapSmithGrid(Graph *graphPtr, Grid *gridPtr);
void Rbc_DrawPolarLabels(Graph *graphPtr, Drawable drawable);
void Rbc_DrawSmithLabels(Graph *graphPtr, Drawable drawable);
void Rbc_PolarLabelsToPostScript(Graph *graphPtr, PsToken psToken);
void Rbc_UpdateCrosshairs(Graph *graphPtr);
void Rbc_DestroyPens(Graph *graphPtr);
void Rbc_ReleasePenTkResources(Graph *graphPtr);
int Rbc_GetPen(Graph *graphPtr, const char *name, Rbc_Uid classUid, Pen **penPtrPtr);
Pen *Rbc_BarPen(const char *penName);
Pen *Rbc_LinePen(const char *penName);
Pen *Rbc_CreatePen(Graph *graphPtr, const char *penName, Rbc_Uid classUid, Tcl_Size nOpts, Tcl_Obj *const options[]);
void Rbc_FreePen(Graph *graphPtr, Pen *penPtr);

int Rbc_VirtualAxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_AxisOp(Graph *graphPtr, int margin, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_ElementOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid classUid);
int Rbc_GridOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_CrosshairsOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_MarkerOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_PenOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_PointInPolygon(const Point2D *samplePtr, const Point2D *points, Tcl_Size nPoints);
int Rbc_RegionInPolygon(const Extents2D *extsPtr, const Point2D *points, Tcl_Size nPoints, int enclosed);
int Rbc_PointInSegments(const Point2D *samplePtr, const Segment2D *segments, Tcl_Size nSegments, double halo);
int Rbc_PostScriptOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_GraphUpdateNeeded(Graph *graphPtr);
int Rbc_DefaultAxes(Graph *graphPtr);
Axis *Rbc_GetFirstAxis(Rbc_Chain *chainPtr);
void Rbc_GetAxisSegments(Graph *graphPtr, Axis *axisPtr, Segment2D **segPtrPtr, Tcl_Size *nSegmentsPtr);
double Rbc_GetClosestPointOnSegment(const Point2D *samplePtr, const Point2D *p, const Point2D *q, Point2D *closestPtr);
Marker *Rbc_NearestMarker(Graph *graphPtr, int x, int y, int under);
Axis *Rbc_NearestAxis(Graph *graphPtr, int x, int y);

typedef ClientData(MakeTagProc)(Graph *graphPtr, char *tagName);
MakeTagProc Rbc_MakeElementTag;
MakeTagProc Rbc_MakeMarkerTag;
MakeTagProc Rbc_MakeAxisTag;

void Rbc_AppendAxisBindingTags(Graph *graphPtr, Axis *axisPtr, Rbc_List list);
void Rbc_AppendElementBindingTags(Graph *graphPtr, Element *elemPtr, Rbc_List list);
void Rbc_AppendMarkerBindingTags(Graph *graphPtr, Marker *markerPtr, Rbc_List list);
Rbc_BindTagProc Rbc_GraphTags;

int Rbc_GraphType(Graph *graphPtr);

/* ---------------------- Global declarations ------------------------ */

extern Rbc_Uid rbcBarElementUid;
extern Rbc_Uid rbcLineElementUid;
extern Rbc_Uid rbcStripElementUid;
extern Rbc_Uid rbcPolarElementUid;
extern Rbc_Uid rbcLineMarkerUid;
extern Rbc_Uid rbcBitmapMarkerUid;
extern Rbc_Uid rbcImageMarkerUid;
extern Rbc_Uid rbcTextMarkerUid;
extern Rbc_Uid rbcPolygonMarkerUid;
extern Rbc_Uid rbcWindowMarkerUid;
extern Rbc_Uid rbcXAxisUid;
extern Rbc_Uid rbcYAxisUid;

#endif /* _RBCGRAPH */
