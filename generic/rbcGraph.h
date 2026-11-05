/*
 * rbcGraph.h --
 *
 *      Shared graph widget records and component interfaces.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/**
 * @file rbcGraph.h
 * @brief Private graph widget state and component interfaces.
 *
 * Graph, barchart, stripchart, polar and trackgraph share this infrastructure.
 * These declarations are not part of the installed vector API. Calls belong
 * on the owning Tk thread; borrowed records do not keep widgets alive.
 */

#ifndef _RBCGRAPH
#define _RBCGRAPH

#include "rbcInt.h"
#include "rbcBind.h"
#include "rbcChain.h"
#include "rbcPs.h"
#include "rbcExport.h"
#include "rbcTile.h"
#include "rbcVector.h"
/*#include "rbcWin.h"*/

typedef enum { RBC_RENDERER_NATIVE, RBC_RENDERER_CAIRO } Rbc_Renderer;

typedef enum {
    RBC_ANTIALIAS_DEFAULT, RBC_ANTIALIAS_NONE, RBC_ANTIALIAS_GRAY,
    RBC_ANTIALIAS_FAST, RBC_ANTIALIAS_GOOD, RBC_ANTIALIAS_BEST
} Rbc_Antialias;

typedef enum {
    GRAPH_BIND_CONTEXT_AXIS = 1, GRAPH_BIND_CONTEXT_ELEMENT, GRAPH_BIND_CONTEXT_MARKER, GRAPH_BIND_CONTEXT_TRACK
} GraphBindContext;

typedef struct RbcTrack RbcTrack;

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
    Graph *graphPtr;
    RbcTrack *track; /* Non-NULL for an independently configured track grid. */
    int optionsConfigured;
    Tcl_Size optionObjc;
    Tcl_Obj *const *optionObjv;
    /*
     * Original Tcl option values. These are owned and reference-counted
     * by Tk_InitOptions(), Tk_SetOptions(), and Tk_FreeConfigOptions().
     */
    Tcl_Obj *angleMajorTicksObjPtr;
    Tcl_Obj *angleMinorTicksObjPtr;
    Tcl_Obj *angleCommandObjPtr;
    Tcl_Obj *smithRealCommandObjPtr;
    Tcl_Obj *smithImagCommandObjPtr;
    Tcl_Obj *smithRealMajorTicksObjPtr;
    Tcl_Obj *smithRealMinorTicksObjPtr;
    Tcl_Obj *smithImagMajorTicksObjPtr;
    Tcl_Obj *smithImagMinorTicksObjPtr;
    Tcl_Obj *dashesObjPtr;
    Tcl_Obj *lineWidthObjPtr;
    Tcl_Obj *mapXObjPtr;
    Tcl_Obj *mapYObjPtr;
    /*
     * Derived/internal state.
     */
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
    char *varName;       /* If non-NULL, name of variable to be
                          * updated when the margin size changes */
    int reqSize;         /* Requested size of margin */
    int site;            /* Indicates where margin is located:
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
    int axisFormatActive;          /* Synchronous label callback is running. */
    int axisFormatInvalid;         /* Callback invalidated the current geometry. */
    struct GraphRedrawSettle *redrawSettle; /* Pending exposure/resize geometry samples. */
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
     * Anonymous Tk window used to perform option-database lookup for
     * graph pseudo-components without creating a temporary child window
     * for every element, marker, pen, etc.
     */
    Tk_Window optionProxy;
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
    int inset;                /* Sum of focus highlight and 3-D
                               * border.  Indicates how far to
                               * offset the graph from outside
                               * edge of the window. */
    int borderWidth;          /* Width of the exterior border */
    int relief;               /* Relief of the exterior border */
    Tk_3DBorder border;       /* 3-D border used to delineate the plot
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
    int width, height;       /* Size of graph window or exported
                              * page */
    Tcl_HashTable penTable;  /* Table of pens */
    struct Component {
        Tcl_HashTable table;    /* Hash table of ids. */
        Rbc_Chain *displayList; /* Display list. */
        Tcl_HashTable tagTable; /* Table of bind tags. */
    } elements, markers, axes;
    Rbc_Uid classUid; /* Default element type */
    Rbc_BindTable bindTable;
    int nextMarkerId;        /* Tracks next marker identifier available */
    Rbc_Chain *axisChain[4]; /* Chain of axes for each of the
                              * margins.  They're separate from the
                              * margin structures to make it easier
                              * to invert the X-Y axes by simply
                              * switching chain pointers.
                              */
    Margin margins[4];
    int pdfWidth, pdfHeight, pdfDecorations, pdfEmbedFonts; /* Independent PDF output options. */
    int svgWidth, svgHeight, svgDecorations, svgEmbedFonts; /* Independent SVG output options. */
    PostScript *postscript; /* PostScript options: see rbcGrPS.c */
    Legend *legend;         /* Legend information: see rbcGrLegd.c */
    Rbc_BindTable trackLegendBindings; /* Shared event routing for local legends. */
    Crosshairs *crosshairs; /* Crosshairs information: see rbcGrHairs.c */
    Grid *gridPtr;          /* Grid attribute information */
    int halo;               /* Maximum distance allowed between points
                             * when searching for a point */
    int inverted;           /* If non-zero, indicates the x and y axis
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
    GC plotFillGC;       /* Used to fill the plotting area with a
                          * solid background color. The fill color
                          * is stored in "plotBg". */
    double aspect;       /* If non-zero, force plot to conform to aspect ratio W/H */
    int polarAutoAspect; /* If non-zero, Polar/Smith layout preserves
                          * equal physical X/Y data-unit scales. */
    /*
     * Polar-specific presentation options.
     */
    PolarRepresentation representation;
    XColor *trackSeparatorColor; /* NULL disables visible track dividers. */
    int trackSeparatorWidth;
    int trackScroll;         /* Preserve requested track sizes instead of fitting. */
    Tcl_Obj *trackScrollCmdObj;
    double trackOffset, trackExtent;
    double trackFirst, trackLast; /* Last notified scrollbar fractions. */
    int trackResize;         /* Enable the optional Trackgraph class drag bindings. */
    int trackGraph;          /* Named, vertically stacked plot regions. */
    RbcTrack *tracks;
    RbcTrack *activeTrack;
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
    int antialias;                         /* Rbc_Antialias; applies to Cairo geometry. */
    struct Rbc_RenderTarget *renderTarget; /* Active, synchronous marker drawing target. */
    int renderer;                          /* Rbc_Renderer selected by -renderer. */
    int doubleBuffer;                      /* If non-zero, draw the graph into a pixmap
                                            * first to reduce flashing. */
    int backingStore;                      /* If non-zero, cache elements by drawing
                                            * them into a pixmap */
    Pixmap backPixmap;                     /* Pixmap used to cache elements
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
    int backWidth, backHeight;             /* Size of element backing store pixmap. */
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
#define GRAPH_EXPORT (1 << 14) /* 0x4000 */
/*
 * Native view or element geometry changed. Retained until the next
 * completed on-screen display queues <<RbcGraphChanged>>.
 *
 * Marker-only updates must not set this flag.
 */
#define GRAPH_CHANGED (1 << 15)

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
/**
 * @brief Map a data value to a horizontal widget coordinate.
 * @param[in] graphPtr Borrowed graph with current ranges and layout.
 * @param[in] axisPtr Borrowed axis selecting the range, log scale and direction.
 * @param[in] x Data value, before logarithmic conversion.
 * @return Double-precision pixel coordinate, or NAN for nonfinite input,
 * invalid logarithmic input or failed normalization.
 *
 * Uses the owning track's full world extent when the axis has a track;
 * otherwise uses the graph offset and range. Does not clip, round, update
 * layout, or execute Tcl. Descending direction is applied after normalization.
 */
double Rbc_HMap(Graph *graphPtr, Axis *axisPtr, double x);
/**
 * @brief Map a data value to a vertical widget coordinate.
 * @param[in] graphPtr Borrowed graph with current ranges and layout.
 * @param[in] axisPtr Borrowed axis selecting the range, log scale and direction.
 * @param[in] y Data value, before logarithmic conversion.
 * @return Double-precision pixel coordinate, or NAN for nonfinite input,
 * invalid logarithmic input or failed normalization.
 *
 * Uses the owning track's full world extent when the axis has a track;
 * otherwise uses the graph offset and range. Does not clip, round, update
 * layout, or execute Tcl. Descending direction is applied after normalization.
 */
double Rbc_VMap(Graph *graphPtr, Axis *axisPtr, double y);

int Rbc_PointInLegend(Graph *graphPtr, int x, int y);

/**
 * @brief Map widget pixels back to logical data coordinates.
 * @param[in] graphPtr Borrowed graph with current ranges and layout.
 * @param[in] x Horizontal widget coordinate.
 * @param[in] y Vertical widget coordinate.
 * @param[in] pairPtr Borrowed pair containing non-NULL X and Y axes.
 * @return Point2D in data coordinates, undoing inversion and axis direction.
 *
 * Coordinates outside the plot are extrapolated rather than clipped. Log
 * conversion returns NAN outside the supported DBL_MIN to DBL_MAX interval.
 * The caller must check results for finiteness. Does not update layout or
 * execute Tcl, and does not acquire ownership of graph or axes.
 */
Point2D Rbc_InvMap2D(Graph *graphPtr, double x, double y, Axis2D *pairPtr);
/**
 * @brief Map a data-coordinate pair to widget pixels without clipping.
 * @param[in] graphPtr Borrowed graph with current ranges and layout.
 * @param[in] x X data value.
 * @param[in] y Y data value.
 * @param[in] pairPtr Borrowed pair containing non-NULL X and Y axes.
 * @return Point2D in widget coordinates; invalid components may be NAN.
 *
 * Inversion swaps which screen direction receives each logical axis. Axis
 * ownership selects track geometry without entering a temporary track scope.
 * Does not update layout, round coordinates, or execute Tcl.
 */
Point2D Rbc_Map2D(Graph *graphPtr, double x, double y, Axis2D *pairPtr);
Graph *Rbc_GetGraphFromWindowData(Tk_Window tkwin);
void Rbc_ReleaseLegendTkResources(Graph *graphPtr);
void Rbc_RedrawExternalLegend(Graph *graphPtr);
void Rbc_ComputeStacks(Graph *graphPtr);
void Rbc_AxisFontsChanged(Graph *graphPtr);
void Rbc_LegendFontsChanged(Graph *graphPtr);
void Rbc_MarkerFontsChanged(Graph *graphPtr);
void Rbc_LineFontsChanged(Graph *graphPtr);
void Rbc_BarFontsChanged(Graph *graphPtr);
void Rbc_ConfigureCrosshairs(Graph *graphPtr);
void Rbc_DestroyAxes(Graph *graphPtr);
void Rbc_DestroyCrosshairs(Graph *graphPtr);
/**
 * @brief Destroy global and local grid resources while the Tk window is live.
 * @param graphPtr Owning graph with valid track list and Tk window context.
 *
 * Clears graphPtr->gridPtr, destroys each track's local grid and cached
 * segments, then destroys the global grid. Frees axis references, Tk options,
 * private GC, segment/tick arrays and the grid records.
 */
void Rbc_DestroyGrid(Graph *graphPtr);
void Rbc_DestroyElements(Graph *graphPtr);
void Rbc_ReleaseElementTkResources(Graph *graphPtr);
/**
 * @brief Destroy all marker records and their registry infrastructure.
 * @param graphPtr Owning graph being torn down.
 *
 * Removes bindings and display-list entries, cleans remaining resources through
 * the class-specific path, then destroys marker and tag hash tables and the
 * display list. Release Tk-dependent resources before the window disappears.
 * This whole-registry destructor is not a repeatable resource-release pass.
 */
void Rbc_DestroyMarkers(Graph *graphPtr);
/**
 * @brief Release marker resources while the graph's Tk window is still live.
 * @param graphPtr Borrowed graph with initialized marker registry.
 *
 * Releases derived resources, common coordinates, tags and axis references
 * before Tk option resources. Already released or uninitialized
 * option records are skipped. Marker records and registry entries remain for
 * later destruction. Window-marker cleanup destroys its current child.
 */
void Rbc_ReleaseMarkerTkResources(Graph *graphPtr);
void Rbc_DestroyPostScript(Graph *graphPtr);
void Rbc_DrawAxes(Graph *graphPtr, Drawable drawable);
void Rbc_DrawAxisLimits(Graph *graphPtr, Drawable drawable);
void Rbc_DrawElements(Graph *graphPtr, Drawable drawable);
void Rbc_DrawActiveElements(Graph *graphPtr, Drawable drawable);
/**
 * @brief Paint mapped graph content and decorations into a borrowed drawable.
 * @param[in,out] graphPtr Live graph with current layout and drawing resources.
 * @param[in] drawable Compatible window or pixmap in widget coordinates.
 * @param[in] backingStore Nonzero to reuse/update the graph-owned plot pixmap.
 *
 * Does not perform layout or present the frame with crosshairs. May allocate
 * or replace the backing pixmap and consume REDRAW_BACKING_STORE. Polar label
 * formatting can cancel drawing; callers must check Rbc_GraphCallbackValid()
 * before presenting the drawable. The caller retains graph lifetime across
 * callbacks. Partial drawing on cancellation is not rolled back.
 */
void Rbc_DrawGraph(Graph *graphPtr, Drawable drawable, int backingStore);
/**
 * @brief Draw prepared grid segments into a borrowed screen drawable.
 * @param graphPtr Borrowed graph with current grid mapping and resources.
 * @param drawable Compatible window or pixmap.
 *
 * Trackgraphs dispatch before testing global visibility, so each effective
 * track grid controls its own visibility. Uses renderer services and native
 * fallback as needed. Does not remap geometry or own the drawable.
 */
void Rbc_DrawGrid(Graph *graphPtr, Drawable drawable);
void Rbc_SmithLabelsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr);
/**
 * @brief Draw one marker layer and synchronize embedded child visibility.
 * @param graphPtr Borrowed live graph with current mapped marker geometry.
 * @param drawable Borrowed compatible screen drawable.
 * @param under Layer selector: 1 below elements, 0 above elements.
 *
 * Synchronizes windows from both layers even when drawing only one layer.
 * Track drawing preserves painter order with per-track clipping; ordinary
 * graphs may batch compatible adjacent primitives. Window markers manage
 * real children rather than paint them into the supplied drawable.
 */
void Rbc_DrawMarkers(Graph *graphPtr, Drawable drawable, int under);
void Rbc_InitFreqTable(Graph *graphPtr);
/**
 * @brief Update pending axis ranges, margins and mapped component geometry.
 * @param[in,out] graphPtr Borrowed graph, kept alive by the caller.
 * @return TCL_OK on completion; TCL_ERROR on recursive layout or callback
 * cancellation, with an interpreter diagnostic and RBC GRAPH CALLBACK code.
 *
 * Axis label callbacks may execute Tcl and destroy or reconfigure the widget.
 * Stop immediately on error before using cached component pointers. This is
 * not a transaction: some layout work may already have occurred. Small plot
 * ranges can leave mapping pending even when TCL_OK is returned. Does not
 * draw, schedule a redraw, or acquire a Tcl_Preserve scope for the caller.
 */
int Rbc_LayoutGraph(Graph *graphPtr);
/**
 * @brief Test window existence and axis-format cancellation state.
 * @param[in] graphPtr Non-NULL graph whose storage is still alive.
 * @return TRUE if the window exists and axisFormatInvalid is false; otherwise
 * FALSE with an interpreter diagnostic and RBC GRAPH CALLBACK error code.
 *
 * This is not a general pointer-validity or component-lifetime test. A
 * Tcl_Preserve scope can retain the graph record after its Tk resources have
 * been released; it cannot make a destroyed widget usable again.
 */
int Rbc_GraphCallbackValid(Graph *graphPtr);
void Rbc_LayoutMargins(Graph *graphPtr);
/**
 * @brief Coalesce a graph redraw request into the Tcl idle queue.
 * @param[in,out] graphPtr Non-NULL graph record, valid throughout the call.
 *
 * If its window exists and REDRAW_PENDING is clear, queue DisplayGraph, set
 * REDRAW_PENDING and request external-legend redraw. An unmapped window can
 * still be queued. During axis formatting, mark the current layout invalid.
 * Does not set geometry or backing-store dirty flags: callers set those
 * separately. Window destruction cancels pending graph display callbacks.
 */
void Rbc_EventuallyRedrawGraph(Graph *graphPtr);
void Rbc_ResetAxes(Graph *graphPtr);
void Rbc_NotifyAxisChanges(Graph *graphPtr);
void Rbc_ResetStacks(Graph *graphPtr);
void Rbc_GraphExtents(Graph *graphPtr, Extents2D *extsPtr);
/**
 * @brief Temporarily erase currently visible XOR crosshairs.
 * @param graphPtr Live graph with initialized crosshairs and their old geometry.
 *
 * When not hidden, erases on a mapped window using the current GC and segments.
 * Does not change -hide. Call before overwriting geometry in direct redraws.
 */
void Rbc_DisableCrosshairs(Graph *graphPtr);
/**
 * @brief Restore XOR crosshairs when requested and drawable on screen.
 * @param graphPtr Live graph with initialized crosshairs and current segments.
 *
 * Honors hidden state and draws only when the window is mapped, hairs are not
 * already visible, and the hotspot is inside the plot. Does not change -hide.
 */
void Rbc_EnableCrosshairs(Graph *graphPtr);
/**
 * @brief Test whether a plot-only frame copy could leave crosshair pixels behind.
 * @param graphPtr Borrowed graph with old crosshair geometry still intact.
 * @return Nonzero for relevant wide strokes or visible old endpoints outside
 * the current plot; zero when no full-frame copy is needed by this test.
 *
 * Call before updating segments. The caller must request margin drawing as
 * well as a full copy so the entire source frame contains initialized pixels.
 */
int Rbc_CrosshairsNeedFullRedraw(Graph *graphPtr);
/**
 * @brief Compose crosshairs into a completed temporary frame and present once.
 * @param graphPtr Live mapped graph with current layout and copy GC.
 * @param drawable Temporary compatible pixmap containing a completed frame
 * without crosshairs; never the window, reusable backing store or export target.
 *
 * Updates segments, adds requested visible hairs, copies the frame to the
 * window, then commits visible state. DRAW_MARGINS selects full-frame copying;
 * otherwise only the plot is copied. No Tcl callbacks or event processing may
 * intervene between geometry update and state commit. Do not separately erase
 * or enable window hairs around this call. Does not free the pixmap.
 */
void Rbc_PresentGraphWithCrosshairs(Graph *graphPtr, Drawable drawable);
void Rbc_MapAxes(Graph *graphPtr);
void Rbc_MapElements(Graph *graphPtr);
/**
 * @brief Map pending marker geometry in display-list order.
 * @param graphPtr Borrowed graph with prepared axes and plot geometry.
 *
 * Skips hidden markers and markers without coordinates. MAP_ALL or MAP_ITEM
 * selects mapping; each class mapper runs in a paired owning-track viewport
 * scope and MAP_ITEM is cleared afterward. Does not draw or update axes.
 */
void Rbc_MapMarkers(Graph *graphPtr);
/**
 * @brief Rebuild grid segment caches from prepared axes and plot geometry.
 * @param graphPtr Borrowed graph with initialized grid and current layout.
 *
 * Releases old global segments. Polar and Smith representations delegate to
 * their specialized mappers. Trackgraphs rebuild per-track caches using local
 * settings or inherited defaults; ordinary graphs rebuild global X/Y segments.
 * Does not draw, schedule layout or use hidden state to suppress mapping.
 */
void Rbc_MapGrid(Graph *graphPtr);
void Rbc_MapPolarGrid(Graph *graphPtr, Grid *gridPtr);
void Rbc_MapSmithGrid(Graph *graphPtr, Grid *gridPtr);
void Rbc_DrawPolarLabels(Graph *graphPtr, Drawable drawable);
void Rbc_DrawSmithLabels(Graph *graphPtr, Drawable drawable);
void Rbc_PolarLabelsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr);
/**
 * @brief Update segment lengths from current graph plot bounds.
 * @param graphPtr Graph with initialized crosshairs and prepared plot geometry.
 *
 * Leaves hotspot coordinates unchanged. Does not erase, draw or change visible
 * state. Call only when old geometry is no longer needed to erase old pixels.
 */
void Rbc_UpdateCrosshairs(Graph *graphPtr);
/**
 * @brief Destroy the graph's remaining registered pens and pen hash table.
 * @param graphPtr Owning graph during final teardown.
 *
 * Does not wait for reference counts: element references must already have
 * been released. Tk-dependent resources must be released before the window
 * disappears. This destroys registry infrastructure and is not repeatable.
 */
void Rbc_DestroyPens(Graph *graphPtr);
/**
 * @brief Release registered pen resources while the graph window is live.
 * @param graphPtr Graph with initialized pen registry and live Tk window.
 *
 * Releases derived resources before Tk-owned option resources. Skips records
 * not initialized or already released; leaves records and registry entries
 * intact for later destruction. Does not release element-embedded pens.
 */
void Rbc_ReleasePenTkResources(Graph *graphPtr);
/**
 * @brief Acquire a reference to a named pen of a compatible concrete type.
 * @param graphPtr Owning graph and interpreter for diagnostics.
 * @param name Pen name to resolve; delete-pending pens are not found.
 * @param classUid Required element class; strip and polar use line pens.
 * @param penPtrPtr Receives the referenced pen on success.
 * @return TCL_OK after incrementing refCount, or TCL_ERROR without acquisition.
 *
 * Pair each successful acquisition with Rbc_FreePen. The returned pointer is
 * graph-owned and must not outlive graph teardown.
 */
int Rbc_GetPen(Graph *graphPtr, const char *name, Rbc_Uid classUid, Pen **penPtrPtr);
Pen *Rbc_BarPen(const char *penName);
Pen *Rbc_LinePen(const char *penName);
Pen *Rbc_CreatePen(Graph *graphPtr, const char *penName, Rbc_Uid classUid, Tcl_Size nOpts, Tcl_Obj *const options[]);
/**
 * @brief Release one acquired named-pen reference.
 * @param graphPtr Owning graph.
 * @param penPtr Non-NULL pen for which the caller owns a counted reference.
 *
 * Decrements refCount and destroys the pen only when it reaches zero and
 * PEN_DELETE_PENDING is set. Does not request deletion of a live named pen.
 * Do not use for an uncounted embedded pen or a palette's borrowed first entry.
 */
void Rbc_FreePen(Graph *graphPtr, Pen *penPtr);
int Rbc_VirtualAxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_AxisOp(Graph *graphPtr, int margin, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_ElementOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], Rbc_Uid classUid);
int Rbc_GridOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_CrosshairsOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_MarkerOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_PenOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_PostScriptOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_GraphUpdateNeeded(Graph *graphPtr);
int Rbc_DefaultAxes(Graph *graphPtr);
Axis *Rbc_GetFirstAxis(Rbc_Chain *chainPtr);
void Rbc_GetAxisSegments(Graph *graphPtr, Axis *axisPtr, int minorGrid, Segment2D **segPtrPtr, Tcl_Size *nSegmentsPtr);
Marker *Rbc_NearestMarker(Graph *graphPtr, int x, int y, int under);
Axis *Rbc_NearestAxis(Graph *graphPtr, int x, int y);

/* Legends share element binding identifiers; axis and marker tag interning
 * is private to the corresponding component implementation. */
ClientData Rbc_MakeElementTag(Graph *graphPtr, char *tagName);

void Rbc_AppendAxisBindingTags(Graph *graphPtr, Axis *axisPtr, Rbc_List list);
void Rbc_AppendElementBindingTags(Graph *graphPtr, Element *elemPtr, Rbc_List list);
void Rbc_AppendMarkerBindingTags(Graph *graphPtr, Marker *markerPtr, Rbc_List list);

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
extern Rbc_Uid rbcArcMarkerUid;
extern Rbc_Uid rbcWindowMarkerUid;
extern Rbc_Uid rbcXAxisUid;
extern Rbc_Uid rbcYAxisUid;

int Rbc_InitSvgOptions(Graph *graphPtr);
int Rbc_InitPdfOptions(Graph *graphPtr);
int Rbc_PdfOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
int Rbc_SvgOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);

/**
 * @brief Static private policy for a named SVG/PDF graph export component.
 * @ingroup graph_export_commands
 *
 * Strings and option specifications are borrowed for the command lifetime.
 * All option entries must be concrete TK_OPTION_PIXELS or TK_OPTION_BOOLEAN
 * values stored as native Graph ints; offsets are trusted, not range checked.
 * documentProc returns a new object at reference count zero on success and
 * leaves delivery, geometry restoration and context cleanup to the caller.
 * freeProc releases backend-owned state only, may be NULL, and must accept a
 * context with no backend allocation. Shared cleanup releases fonts/storage.
 * Encoding and translation select Tcl channel policy for file delivery.
 */
typedef struct {
    const char *name, *className, *label, *optionLabel, *operationLabel;
    const Tk_OptionSpec *options;
    Rbc_ExportBackend backend;
    int widthOffset, heightOffset, decorationsOffset, embedFontsOffset;
    const char *encoding, *translation;
    int (*documentProc)(Graph *, Rbc_ExportContext *, Tcl_Obj **);
    void (*freeProc)(Rbc_ExportContext *);
} Rbc_GraphExportFormat;

int Rbc_InitGraphExportOptions(Graph *graphPtr, const Rbc_GraphExportFormat *format);
int Rbc_GraphExportOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[],
                      const Rbc_GraphExportFormat *format);

void Rbc_ExportBeginGraph(Graph *graphPtr);
void Rbc_ExportEndGraph(Graph *graphPtr);
int Rbc_ExportGraph(Graph *graphPtr, Rbc_ExportContext *exportPtr);
/**
 * @brief Export eligible markers from one layer in display-list order.
 * @param graphPtr Borrowed graph with export layout already mapped.
 * @param exportPtr Borrowed initialized export context.
 * @param under Layer selector: 1 below elements, 0 above elements.
 *
 * Skips missing exporters, absent coordinates, hidden markers and markers
 * associated with an existing hidden element. Non-window track markers use
 * a track viewport and renderer clip; window export follows its own capture
 * path. Does not perform layout or own the export context.
 */
void Rbc_MarkersExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, int under);
void Rbc_ElementsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr);
void Rbc_ActiveElementsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr);
void Rbc_LegendExport(Legend *legendPtr, Rbc_ExportContext *exportPtr);
/**
 * @brief Emit prepared grid segments through an initialized export context.
 * @param graphPtr Borrowed graph mapped for the export dimensions.
 * @param exportPtr Borrowed export context owned by the caller.
 *
 * Track grids use their effective styles, visibility and per-track clips.
 * Ordinary graphs skip a hidden global grid. Does not perform layout or
 * finalize the document.
 */
void Rbc_GridExport(Graph *graphPtr, Rbc_ExportContext *exportPtr);
void Rbc_AxesExport(Graph *graphPtr, Rbc_ExportContext *exportPtr);
void Rbc_AxisLimitsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr);

#endif /* _RBCGRAPH */
