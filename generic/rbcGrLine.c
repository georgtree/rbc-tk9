/*
 * rbcGrLine.c --
 *
 *      This module implements line graph and stripchart elements for
 *      the rbc graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */
#include "rbcGraph.h"
#include "rbcChain.h"
#include <X11/Xutil.h>

#include "rbcGrElem.h"

#define COLOR_DEFAULT (XColor *)1
#define PATTERN_SOLID ((Pixmap)1)

/* Draw line segments for only those
 * data points whose abscissas are
 * monotonically increasing in
 * order */
#define PEN_INCREASING 1

/* Lines will be drawn between only
 * those points whose abscissas are
 * decreasing in order */
#define PEN_DECREASING 2

#define PEN_BOTH_DIRECTIONS (PEN_INCREASING | PEN_DECREASING)
/* Lines will be drawn between points regardless of the ordering of
 * the abscissas */

#define BROKEN_TRACE(dir, last, next)                                                                                  \
    (((((dir) & PEN_DECREASING) == 0) && ((next) < (last))) || ((((dir) & PEN_INCREASING) == 0) && ((next) > (last))))

#define DRAW_SYMBOL(linePtr) (((linePtr)->symbolCounter % (linePtr)->symbolInterval) == 0)

typedef enum {
    PEN_SMOOTH_NONE,      /* Line segments */
    PEN_SMOOTH_STEP,      /* Step-and-hold */
    PEN_SMOOTH_NATURAL,   /* Natural cubic spline */
    PEN_SMOOTH_QUADRATIC, /* Quadratic spline */
    PEN_SMOOTH_CATROM,    /* Catrom parametric spline */
    PEN_SMOOTH_LAST       /* Sentinel */
} Smoothing;

typedef struct {
    char *name;
    Smoothing value;
} SmoothingInfo;

static SmoothingInfo smoothingInfo[] = {{"linear", PEN_SMOOTH_NONE},         {"step", PEN_SMOOTH_STEP},
                                        {"natural", PEN_SMOOTH_NATURAL},     {"cubic", PEN_SMOOTH_NATURAL},
                                        {"quadratic", PEN_SMOOTH_QUADRATIC}, {"catrom", PEN_SMOOTH_CATROM},
                                        {(char *)NULL, PEN_SMOOTH_LAST}};

typedef struct {
    Point2D *screenPts; /* Array of transformed coordinates */
    int nScreenPts;     /* Number of coordinates */
    int *dataToStyle;   /* Index of pen styles  */
    int *indices;       /* Maps segments/traces to data points */
} MapInfo;

/*
 * Symbol types for line elements
 */
typedef enum {
    SYMBOL_NONE,
    SYMBOL_SQUARE,
    SYMBOL_CIRCLE,
    SYMBOL_DIAMOND,
    SYMBOL_PLUS,
    SYMBOL_CROSS,
    SYMBOL_SPLUS,
    SYMBOL_SCROSS,
    SYMBOL_TRIANGLE,
    SYMBOL_ARROW,
    SYMBOL_BITMAP
} SymbolType;

typedef struct {
    SymbolType type;      /* Type of symbol to be drawn/printed */
    int size;             /* Requested size of symbol in pixels */
    XColor *outlineColor; /* Outline color */
    int outlineWidth;     /* Width of the outline */
    GC outlineGC;         /* Outline graphics context */
    XColor *fillColor;    /* Normal fill color */
    GC fillGC;            /* Fill graphics context */

    /* The last two fields are used only for bitmap symbols. */
    Pixmap bitmap; /* Bitmap to determine foreground/background
                    * pixels of the symbol */
    Pixmap mask;   /* Bitmap representing the transparent
                    * pixels of the symbol */
} Symbol;

typedef struct {
    int start;          /* Index into the X-Y coordinate
                         * arrays indicating where trace
                         * starts. */
    int nScreenPts;     /* Number of points in the continuous
                         * trace */
    Point2D *screenPts; /* Array of screen coordinates
                         * (malloc-ed) representing the
                         * trace. */
    int *symbolToData;  /* Reverse mapping of screen
                         * coordinate indices back to their
                         * data coordinates */
} LineTrace;

typedef struct {
    char *name;         /* Name of pen style. If the pen was
                         * statically allocated the name will
                         * be NULL. */
    Rbc_Uid classUid;   /* Type of pen */
    char *typeId;       /* String token identifying the type
                         * of pen */
    unsigned int flags; /* Indicates if the pen element is
                         * active or normal */
    int refCount;       /* Reference count for elements using
                         * this pen. */
    Tcl_HashEntry *hashPtr;
    Tk_ConfigSpec *configSpecs; /* Configuration specifications */
    PenConfigureProc *configProc;
    PenDestroyProc *destroyProc;

    /* Symbol attributes. */
    Symbol symbol; /* Element symbol type */

    /* LineTrace attributes. */
    int traceWidth;         /* Width of the line segments. If
                             * lineWidth is 0, no line will be
                             * drawn, only symbols. */
    Rbc_Dashes traceDashes; /* Dash on-off list value */
    XColor *traceColor;     /* Line segment color */
    XColor *traceOffColor;  /* Line segment dash gap color */
    GC traceGC;             /* Line segment graphics context */

    /* Error bar attributes. */
    int errorBarShow;      /* Describes which error bars to
                            * display: none, x, y, or * both. */
    int errorBarLineWidth; /* Width of the error bar segments. */
    int errorBarCapWidth;  /* Width of the cap on error bars. */
    XColor *errorBarColor; /* Color of the error bar. */
    GC errorBarGC;         /* Error bar graphics context. */

    /* Show value attributes. */
    int valueShow;        /* Indicates whether to display data
                           * value.  Values are x, y, both, or
                           * none. */
    char *valueFormat;    /* A printf format string. */
    TextStyle valueStyle; /* Text attributes (color, font,
                           * rotation, etc.) of the value. */
} LinePen;

typedef struct {
    Weight weight; /* Weight range where this pen is valid. */

    LinePen *penPtr; /* Pen used to draw symbols, traces, error
                      * bars, segments, etc. */

    Segment2D *xErrorBars; /* Point to start of this pen's X-error bar
                            * segments in the element's array. */
    Segment2D *yErrorBars; /* Point to start of this pen's Y-error bar
                            * segments in the element's array. */
    int xErrorBarCnt;      /* # of error bars for this pen. */
    int yErrorBarCnt;      /* # of error bars for this pen. */

    int errorBarCapWidth; /* Length of the cap ends on each
                           * error bar. */

    int symbolSize; /* Size of the pen's symbol scaled to the
                     * current graph size. */

    /* Graph specific data. */

    Point2D *symbolPts; /* Points to start of array for this pen. */

    int nSymbolPts; /* # of points for this pen. */

    /* The last two fields are used only for stripcharts. */

    Segment2D *strips; /* Points to start of the line segments
                        * for this pen. */

    int nStrips; /* # of line segments for this pen. */

} LinePenStyle;

typedef struct {
    char *name; /* Identifier used to refer the
                 * element. Used in the "insert",
                 * "delete", or "show", operations. */

    Rbc_Uid classUid; /* Type of element */

    Graph *graphPtr; /* Graph widget of element*/

    unsigned int flags; /* Indicates if the entire element is
                         * active, or if coordinates need to
                         * be calculated */

    char **tags;

    int hidden; /* If non-zero, don't display the
                 * element. */

    Tcl_HashEntry *hashPtr;

    char *label; /* Label displayed in legend */

    int labelRelief; /* Relief of label in legend. */

    Axis2D axes;

    ElemVector x, y, w; /* Contains array of numeric values */

    ElemVector xError;      /* Relative/symmetric X error values. */
    ElemVector yError;      /* Relative/symmetric Y error values. */
    ElemVector xHigh, xLow; /* Absolute/asymmetric X-coordinate high/low
                error values. */
    ElemVector yHigh, yLow; /* Absolute/asymmetric Y-coordinate high/low
                error values. */

    int *activeIndices; /* Array of indices (malloc-ed) that
                         * indicate the data points are active
                         * (drawn with "active" colors). */

    int nActiveIndices; /* Number of active data points.
                         * Special case: if < 0 then all data
                         * points are drawn active. */

    ElementProcs *procsPtr;
    Tk_ConfigSpec *configSpecs; /* Configuration specifications */

    Segment2D *xErrorBars; /* Point to start of this pen's X-error bar
                            * segments in the element's array. */
    Segment2D *yErrorBars; /* Point to start of this pen's Y-error bar
                            * segments in the element's array. */
    int xErrorBarCnt;      /* # of error bars for this pen. */
    int yErrorBarCnt;      /* # of error bars for this pen. */

    int *xErrorToData; /* Maps individual error bar segments back
                        * to the data point associated with it. */
    int *yErrorToData; /* Maps individual error bar segments back
                        * to the data point associated with it. */

    int errorBarCapWidth; /* Length of cap on error bars */

    LinePen *activePenPtr; /* Pen to draw "active" elements. */
    LinePen *normalPenPtr; /* Pen to draw elements normally. */

    Rbc_Chain *palette; /* Array of pen styles: pens are associated
                         * with specific ranges of data.*/

    /* Symbol scaling */
    int scaleSymbols; /* If non-zero, the symbols will scale
                       * in size as the graph is zoomed
                       * in/out.  */

    double xRange, yRange; /* Initial X-axis and Y-axis ranges:
                            * used to scale the size of element's
                            * symbol. */

    int state;
    /*
     * Line specific configurable attributes
     */
    LinePen builtinPen;

    /* Line smoothing */
    Smoothing reqSmooth; /* Requested smoothing function to use
                          * for connecting the data points */

    Smoothing smooth; /* Smoothing function used. */

    double rTolerance; /* Tolerance to reduce the number of
                        * points displayed. */
    /*
     * Drawing related data structures.
     */

    /* Area-under-curve fill attributes. */
    XColor *fillFgColor;
    XColor *fillBgColor;
    GC fillGC;

    Rbc_Tile fillTile;  /* Tile for fill area. */
    Pixmap fillStipple; /* Stipple for fill area. */

    int nFillPts;
    Point2D *fillPts; /* Array of points used to draw
                       * polygon to fill area under the
                       * curve */

    /* Symbol points */
    Point2D *symbolPts; /* Holds the screen coordinates of all
                         * the data points for the element. */
    int nSymbolPts;     /* Number of points */

    int *symbolToData; /* Contains indices of data points.
                        * It's first used to map pens to the
                        * visible points to sort them by pen
                        * style, and later to find data
                        * points from the index of a visible
                        * point. */

    /* Active symbol points */
    Point2D *activePts; /* Array of indices representing the
                         * "active" points. */
    int nActivePts;     /* Number of indices in the above array. */

    int *activeToData; /* Contains indices of data points.
                        * It's first used to map pens to the
                        * visible points to sort them by pen
                        * style, and later to find data
                        * points from the index of a visible
                        * point. */

    int reqMaxSymbols;
    int symbolInterval;
    int symbolCounter;

    /* X-Y graph-specific fields */

    int penDir; /* Indicates if a change in the pen
                 * direction should be considered a
                 * retrace (line segment is not
                 * drawn). */

    Rbc_Chain *traces; /* List of traces (a trace is a series
                        * of contiguous line segments).  New
                        * traces are generated when either
                        * the next segment changes the pen
                        * direction, or the end point is
                        * clipped by the plotting area. */

    /* Stripchart-specific fields */

    Segment2D *strips; /* Holds the the line segments of the
                        * element trace. The segments are
                        * grouped by pen style. */
    int nStrips;       /* Number of line segments to be drawn. */
    int *stripToData;  /* Pen to visible line segment mapping. */

} Line;

static Tk_OptionParseProc StringToPattern;
static Tk_OptionPrintProc PatternToString;
static Tk_OptionParseProc StringToSmooth;
static Tk_OptionPrintProc SmoothToString;
static Tk_OptionParseProc StringToPenDir;
static Tk_OptionPrintProc PenDirToString;
static Tk_OptionParseProc StringToSymbol;
static Tk_OptionPrintProc SymbolToString;

static Tk_CustomOption patternOption = {StringToPattern, PatternToString, (ClientData)0};
static Tk_CustomOption smoothOption = {StringToSmooth, SmoothToString, (ClientData)0};
static Tk_CustomOption stylesOption = {Rbc_StringToStyles, Rbc_StylesToString, (ClientData)sizeof(LinePenStyle)};
static Tk_CustomOption penDirOption = {StringToPenDir, PenDirToString, (ClientData)0};
static Tk_CustomOption symbolOption = {StringToSymbol, SymbolToString, (ClientData)0};
extern Tk_CustomOption rbcColorOption;
extern Tk_CustomOption rbcDashesOption;
extern Tk_CustomOption rbcDataOption;
extern Tk_CustomOption rbcDataPairsOption;
extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcListOption;
extern Tk_CustomOption rbcLinePenOption;
extern Tk_CustomOption rbcShadowOption;
extern Tk_CustomOption rbcXAxisOption;
extern Tk_CustomOption rbcYAxisOption;
extern Tk_CustomOption rbcTileOption;
extern Tk_CustomOption rbcFillOption;
extern Tk_CustomOption rbcStateOption;

#define DEF_LINE_ACTIVE_PEN "activeLine"
#define DEF_LINE_AXIS_X "x"
#define DEF_LINE_AXIS_Y "y"
#define DEF_LINE_DASHES (char *)NULL
#define DEF_LINE_DATA (char *)NULL
#define DEF_LINE_FILL_COLOR "defcolor"
#define DEF_LINE_FILL_MONO "defcolor"
#define DEF_LINE_HIDE "no"
#define DEF_LINE_LABEL (char *)NULL
#define DEF_LINE_LABEL_RELIEF "flat"
#define DEF_LINE_MAX_SYMBOLS "0"
#define DEF_LINE_OFFDASH_COLOR (char *)NULL
#define DEF_LINE_OFFDASH_MONO (char *)NULL
#define DEF_LINE_OUTLINE_COLOR "defcolor"
#define DEF_LINE_OUTLINE_MONO "defcolor"
#define DEF_LINE_OUTLINE_WIDTH "1"
#define DEF_LINE_PATTERN (char *)NULL
#define DEF_LINE_PATTERN_BG "white"
#define DEF_LINE_PATTERN_FG "black"
#define DEF_LINE_PATTERN_TILE (char *)NULL
#define DEF_LINE_PEN_COLOR RGB_NAVYBLUE
#define DEF_LINE_PEN_DIRECTION "both"
#define DEF_LINE_PEN_MONO RGB_BLACK
#define DEF_LINE_PEN_WIDTH "1"
#define DEF_LINE_PIXELS "0.125i"
#define DEF_LINE_REDUCE "0.0"
#define DEF_LINE_SCALE_SYMBOLS "yes"
#define DEF_LINE_SMOOTH "linear"
#define DEF_LINE_STATE "normal"
#define DEF_LINE_STIPPLE (char *)NULL
#define DEF_LINE_STYLES ""
#define DEF_LINE_SYMBOL "circle"
#define DEF_LINE_TAGS "all"
#define DEF_LINE_X_DATA (char *)NULL
#define DEF_LINE_Y_DATA (char *)NULL

#define DEF_LINE_ERRORBAR_COLOR "defcolor"
#define DEF_LINE_ERRORBAR_LINE_WIDTH "1"
#define DEF_LINE_ERRORBAR_CAP_WIDTH "1"
#define DEF_LINE_SHOW_ERRORBARS "both"

#define DEF_PEN_ACTIVE_COLOR RGB_BLUE
#define DEF_PEN_ACTIVE_MONO RGB_BLACK
#define DEF_PEN_DASHES (char *)NULL
#define DEF_PEN_FILL_COLOR "defcolor"
#define DEF_PEN_FILL_MONO "defcolor"
#define DEF_PEN_LINE_WIDTH "1"
#define DEF_PEN_NORMAL_COLOR RGB_NAVYBLUE
#define DEF_PEN_NORMAL_MONO RGB_BLACK
#define DEF_PEN_OFFDASH_COLOR (char *)NULL
#define DEF_PEN_OFFDASH_MONO (char *)NULL
#define DEF_PEN_OUTLINE_COLOR "defcolor"
#define DEF_PEN_OUTLINE_MONO "defcolor"
#define DEF_PEN_OUTLINE_WIDTH "1"
#define DEF_PEN_PIXELS "0.125i"
#define DEF_PEN_SYMBOL "circle"
#define DEF_PEN_TYPE "line"
#define DEF_PEN_VALUE_ANCHOR "s"
#define DEF_PEN_VALUE_COLOR RGB_BLACK
#define DEF_PEN_VALUE_FONT STD_FONT_SMALL
#define DEF_PEN_VALUE_FORMAT "%g"
#define DEF_PEN_VALUE_ROTATE (char *)NULL
#define DEF_PEN_VALUE_SHADOW (char *)NULL
#define DEF_PEN_SHOW_VALUES "no"

static Tk_ConfigSpec lineElemConfigSpecs[] = {
    {TK_CONFIG_CUSTOM, "-activepen", "activePen", "ActivePen", DEF_LINE_ACTIVE_PEN, offsetof(Line, activePenPtr),
     TK_CONFIG_NULL_OK, &rbcLinePenOption},
    {TK_CONFIG_CUSTOM, "-areapattern", "areaPattern", "AreaPattern", DEF_LINE_PATTERN, offsetof(Line, fillStipple),
     TK_CONFIG_NULL_OK, &patternOption},
    {TK_CONFIG_COLOR, "-areaforeground", "areaForeground", "areaForeground", DEF_LINE_PATTERN_FG,
     offsetof(Line, fillFgColor), TK_CONFIG_NULL_OK},
    {TK_CONFIG_COLOR, "-areabackground", "areaBackground", "areaBackground", DEF_LINE_PATTERN_BG,
     offsetof(Line, fillBgColor), TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-areatile", "areaTile", "AreaTile", DEF_LINE_PATTERN_TILE, offsetof(Line, fillTile),
     TK_CONFIG_NULL_OK, &rbcTileOption},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags", DEF_LINE_TAGS, offsetof(Line, tags), TK_CONFIG_NULL_OK,
     &rbcListOption},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_LINE_PEN_COLOR, offsetof(Line, builtinPen.traceColor),
     TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_LINE_PEN_MONO, offsetof(Line, builtinPen.traceColor),
     TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_CUSTOM, "-dashes", "dashes", "Dashes", DEF_LINE_DASHES, offsetof(Line, builtinPen.traceDashes),
     TK_CONFIG_NULL_OK, &rbcDashesOption},
    {TK_CONFIG_CUSTOM, "-data", "data", "Data", DEF_LINE_DATA, 0, 0, &rbcDataPairsOption},
    {TK_CONFIG_CUSTOM, "-errorbarcolor", "errorBarColor", "ErrorBarColor", DEF_LINE_ERRORBAR_COLOR,
     offsetof(Line, builtinPen.errorBarColor), 0, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-errorbarwidth", "errorBarWidth", "ErrorBarWidth", DEF_LINE_ERRORBAR_LINE_WIDTH,
     offsetof(Line, builtinPen.errorBarLineWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-errorbarcap", "errorBarCap", "ErrorBarCap", DEF_LINE_ERRORBAR_CAP_WIDTH,
     offsetof(Line, builtinPen.errorBarCapWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-fill", "fill", "Fill", DEF_LINE_FILL_COLOR, offsetof(Line, builtinPen.symbol.fillColor),
     TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-fill", "fill", "Fill", DEF_LINE_FILL_MONO, offsetof(Line, builtinPen.symbol.fillColor),
     TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY, &rbcColorOption},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_LINE_HIDE, offsetof(Line, hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_STRING, "-label", "label", "Label", (char *)NULL, offsetof(Line, label), TK_CONFIG_NULL_OK},
    {TK_CONFIG_RELIEF, "-labelrelief", "labelRelief", "LabelRelief", DEF_LINE_LABEL_RELIEF, offsetof(Line, labelRelief),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "LineWidth", DEF_LINE_PEN_WIDTH,
     offsetof(Line, builtinPen.traceWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_LINE_AXIS_X, offsetof(Line, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_LINE_AXIS_Y, offsetof(Line, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_CUSTOM, "-maxsymbols", "maxSymbols", "MaxSymbols", DEF_LINE_MAX_SYMBOLS, offsetof(Line, reqMaxSymbols),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-offdash", "offDash", "OffDash", DEF_LINE_OFFDASH_COLOR,
     offsetof(Line, builtinPen.traceOffColor), TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-offdash", "offDash", "OffDash", DEF_LINE_OFFDASH_MONO,
     offsetof(Line, builtinPen.traceOffColor), TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-outline", "outline", "Outline", DEF_LINE_OUTLINE_COLOR,
     offsetof(Line, builtinPen.symbol.outlineColor), TK_CONFIG_COLOR_ONLY, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-outline", "outline", "Outline", DEF_LINE_OUTLINE_MONO,
     offsetof(Line, builtinPen.symbol.outlineColor), TK_CONFIG_MONO_ONLY, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-outlinewidth", "outlineWidth", "OutlineWidth", DEF_LINE_OUTLINE_WIDTH,
     offsetof(Line, builtinPen.symbol.outlineWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-pen", "pen", "Pen", (char *)NULL, offsetof(Line, normalPenPtr), TK_CONFIG_NULL_OK,
     &rbcLinePenOption},
    {TK_CONFIG_CUSTOM, "-pixels", "pixels", "Pixels", DEF_LINE_PIXELS, offsetof(Line, builtinPen.symbol.size),
     GRAPH | STRIPCHART, &rbcDistanceOption},
    {TK_CONFIG_DOUBLE, "-reduce", "reduce", "Reduce", DEF_LINE_REDUCE, offsetof(Line, rTolerance),
     GRAPH | STRIPCHART | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-scalesymbols", "scaleSymbols", "ScaleSymbols", DEF_LINE_SCALE_SYMBOLS,
     offsetof(Line, scaleSymbols), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-showerrorbars", "showErrorBars", "ShowErrorBars", DEF_LINE_SHOW_ERRORBARS,
     offsetof(Line, builtinPen.errorBarShow), TK_CONFIG_DONT_SET_DEFAULT, &rbcFillOption},
    {TK_CONFIG_CUSTOM, "-showvalues", "showValues", "ShowValues", DEF_PEN_SHOW_VALUES,
     offsetof(Line, builtinPen.valueShow), TK_CONFIG_DONT_SET_DEFAULT, &rbcFillOption},
    {TK_CONFIG_CUSTOM, "-smooth", "smooth", "Smooth", DEF_LINE_SMOOTH, offsetof(Line, reqSmooth),
     TK_CONFIG_DONT_SET_DEFAULT, &smoothOption},
    {TK_CONFIG_CUSTOM, "-state", "state", "State", DEF_LINE_STATE, offsetof(Line, state), TK_CONFIG_DONT_SET_DEFAULT,
     &rbcStateOption},
    {TK_CONFIG_CUSTOM, "-styles", "styles", "Styles", DEF_LINE_STYLES, offsetof(Line, palette), TK_CONFIG_NULL_OK,
     &stylesOption},
    {TK_CONFIG_CUSTOM, "-symbol", "symbol", "Symbol", DEF_LINE_SYMBOL, offsetof(Line, builtinPen.symbol),
     TK_CONFIG_DONT_SET_DEFAULT, &symbolOption},
    {TK_CONFIG_CUSTOM, "-trace", "trace", "Trace", DEF_LINE_PEN_DIRECTION, offsetof(Line, penDir),
     TK_CONFIG_DONT_SET_DEFAULT, &penDirOption},
    {TK_CONFIG_ANCHOR, "-valueanchor", "valueAnchor", "ValueAnchor", DEF_PEN_VALUE_ANCHOR,
     offsetof(Line, builtinPen.valueStyle.anchor), 0},
    {TK_CONFIG_COLOR, "-valuecolor", "valueColor", "ValueColor", DEF_PEN_VALUE_COLOR,
     offsetof(Line, builtinPen.valueStyle.color), 0},
    {TK_CONFIG_FONT, "-valuefont", "valueFont", "ValueFont", DEF_PEN_VALUE_FONT,
     offsetof(Line, builtinPen.valueStyle.font), 0},
    {TK_CONFIG_STRING, "-valueformat", "valueFormat", "ValueFormat", DEF_PEN_VALUE_FORMAT,
     offsetof(Line, builtinPen.valueFormat), TK_CONFIG_NULL_OK},
    {TK_CONFIG_DOUBLE, "-valuerotate", "valueRotate", "ValueRotate", DEF_PEN_VALUE_ROTATE,
     offsetof(Line, builtinPen.valueStyle.theta), 0},
    {TK_CONFIG_CUSTOM, "-valueshadow", "valueShadow", "ValueShadow", DEF_PEN_VALUE_SHADOW,
     offsetof(Line, builtinPen.valueStyle.shadow), 0, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-weights", "weights", "Weights", (char *)NULL, offsetof(Line, w), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-x", "xData", "XData", (char *)NULL, offsetof(Line, x), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xdata", "xData", "XData", (char *)NULL, offsetof(Line, x), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xerror", "xError", "XError", (char *)NULL, offsetof(Line, xError), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xhigh", "xHigh", "XHigh", (char *)NULL, offsetof(Line, xHigh), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xlow", "xLow", "XLow", (char *)NULL, offsetof(Line, xLow), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-y", "yData", "YData", (char *)NULL, offsetof(Line, y), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-ydata", "yData", "YData", (char *)NULL, offsetof(Line, y), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-yerror", "yError", "YError", (char *)NULL, offsetof(Line, yError), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-yhigh", "yHigh", "YHigh", (char *)NULL, offsetof(Line, yHigh), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-ylow", "yLow", "YLow", (char *)NULL, offsetof(Line, yLow), 0, &rbcDataOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}};

static Tk_ConfigSpec stripElemConfigSpecs[] = {
    {TK_CONFIG_CUSTOM, "-activepen", "activePen", "ActivePen", DEF_LINE_ACTIVE_PEN, offsetof(Line, activePenPtr),
     TK_CONFIG_NULL_OK, &rbcLinePenOption},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags", DEF_LINE_TAGS, offsetof(Line, tags), TK_CONFIG_NULL_OK,
     &rbcListOption},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_LINE_PEN_COLOR, offsetof(Line, builtinPen.traceColor),
     TK_CONFIG_COLOR_ONLY},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_LINE_PEN_MONO, offsetof(Line, builtinPen.traceColor),
     TK_CONFIG_MONO_ONLY},
    {TK_CONFIG_CUSTOM, "-dashes", "dashes", "Dashes", DEF_LINE_DASHES, offsetof(Line, builtinPen.traceDashes),
     TK_CONFIG_NULL_OK, &rbcDashesOption},
    {TK_CONFIG_CUSTOM, "-data", "data", "Data", DEF_LINE_DATA, 0, 0, &rbcDataPairsOption},
    {TK_CONFIG_CUSTOM, "-errorbarcolor", "errorBarColor", "ErrorBarColor", DEF_LINE_ERRORBAR_COLOR,
     offsetof(Line, builtinPen.errorBarColor), 0, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-errorbarwidth", "errorBarWidth", "ErrorBarWidth", DEF_LINE_ERRORBAR_LINE_WIDTH,
     offsetof(Line, builtinPen.errorBarLineWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-errorbarcap", "errorBarCap", "ErrorBarCap", DEF_LINE_ERRORBAR_CAP_WIDTH,
     offsetof(Line, builtinPen.errorBarCapWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-fill", "fill", "Fill", DEF_LINE_FILL_COLOR, offsetof(Line, builtinPen.symbol.fillColor),
     TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-fill", "fill", "Fill", DEF_LINE_FILL_MONO, offsetof(Line, builtinPen.symbol.fillColor),
     TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY, &rbcColorOption},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_LINE_HIDE, offsetof(Line, hidden), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_STRING, "-label", "label", "Label", (char *)NULL, offsetof(Line, label), TK_CONFIG_NULL_OK},
    {TK_CONFIG_RELIEF, "-labelrelief", "labelRelief", "LabelRelief", DEF_LINE_LABEL_RELIEF, offsetof(Line, labelRelief),
     TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "LineWidth", DEF_LINE_PEN_WIDTH,
     offsetof(Line, builtinPen.traceWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-mapx", "mapX", "MapX", DEF_LINE_AXIS_X, offsetof(Line, axes.x), 0, &rbcXAxisOption},
    {TK_CONFIG_CUSTOM, "-mapy", "mapY", "MapY", DEF_LINE_AXIS_Y, offsetof(Line, axes.y), 0, &rbcYAxisOption},
    {TK_CONFIG_CUSTOM, "-maxsymbols", "maxSymbols", "MaxSymbols", DEF_LINE_MAX_SYMBOLS, offsetof(Line, reqMaxSymbols),
     TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-offdash", "offDash", "OffDash", DEF_LINE_OFFDASH_COLOR,
     offsetof(Line, builtinPen.traceOffColor), TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-offdash", "offDash", "OffDash", DEF_LINE_OFFDASH_MONO,
     offsetof(Line, builtinPen.traceOffColor), TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-outline", "outline", "Outline", DEF_LINE_OUTLINE_COLOR,
     offsetof(Line, builtinPen.symbol.outlineColor), TK_CONFIG_COLOR_ONLY, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-outline", "outline", "Outline", DEF_LINE_OUTLINE_MONO,
     offsetof(Line, builtinPen.symbol.outlineColor), TK_CONFIG_MONO_ONLY, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-outlinewidth", "outlineWidth", "OutlineWidth", DEF_LINE_OUTLINE_WIDTH,
     offsetof(Line, builtinPen.symbol.outlineWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-pen", "pen", "Pen", (char *)NULL, offsetof(Line, normalPenPtr), TK_CONFIG_NULL_OK,
     &rbcLinePenOption},
    {TK_CONFIG_CUSTOM, "-pixels", "pixels", "Pixels", DEF_LINE_PIXELS, offsetof(Line, builtinPen.symbol.size), 0,
     &rbcDistanceOption},
    {TK_CONFIG_BOOLEAN, "-scalesymbols", "scaleSymbols", "ScaleSymbols", DEF_LINE_SCALE_SYMBOLS,
     offsetof(Line, scaleSymbols), TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-showerrorbars", "showErrorBars", "ShowErrorBars", DEF_LINE_SHOW_ERRORBARS,
     offsetof(Line, builtinPen.errorBarShow), TK_CONFIG_DONT_SET_DEFAULT, &rbcFillOption},
    {TK_CONFIG_CUSTOM, "-showvalues", "showValues", "ShowValues", DEF_PEN_SHOW_VALUES,
     offsetof(Line, builtinPen.valueShow), TK_CONFIG_DONT_SET_DEFAULT, &rbcFillOption},
    {TK_CONFIG_CUSTOM, "-smooth", "smooth", "Smooth", DEF_LINE_SMOOTH, offsetof(Line, reqSmooth),
     TK_CONFIG_DONT_SET_DEFAULT, &smoothOption},
    {TK_CONFIG_CUSTOM, "-styles", "styles", "Styles", DEF_LINE_STYLES, offsetof(Line, palette), TK_CONFIG_NULL_OK,
     &stylesOption},
    {TK_CONFIG_CUSTOM, "-symbol", "symbol", "Symbol", DEF_LINE_SYMBOL, offsetof(Line, builtinPen.symbol),
     TK_CONFIG_DONT_SET_DEFAULT, &symbolOption},
    {TK_CONFIG_ANCHOR, "-valueanchor", "valueAnchor", "ValueAnchor", DEF_PEN_VALUE_ANCHOR,
     offsetof(Line, builtinPen.valueStyle.anchor), 0},
    {TK_CONFIG_COLOR, "-valuecolor", "valueColor", "ValueColor", DEF_PEN_VALUE_COLOR,
     offsetof(Line, builtinPen.valueStyle.color), 0},
    {TK_CONFIG_FONT, "-valuefont", "valueFont", "ValueFont", DEF_PEN_VALUE_FONT,
     offsetof(Line, builtinPen.valueStyle.font), 0},
    {TK_CONFIG_STRING, "-valueformat", "valueFormat", "ValueFormat", DEF_PEN_VALUE_FORMAT,
     offsetof(Line, builtinPen.valueFormat), TK_CONFIG_NULL_OK},
    {TK_CONFIG_DOUBLE, "-valuerotate", "valueRotate", "ValueRotate", DEF_PEN_VALUE_ROTATE,
     offsetof(Line, builtinPen.valueStyle.theta), 0},
    {TK_CONFIG_CUSTOM, "-valueshadow", "valueShadow", "ValueShadow", DEF_PEN_VALUE_SHADOW,
     offsetof(Line, builtinPen.valueStyle.shadow), 0, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-weights", "weights", "Weights", (char *)NULL, offsetof(Line, w), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-x", "xData", "XData", (char *)NULL, offsetof(Line, x), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xdata", "xData", "XData", (char *)NULL, offsetof(Line, x), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-y", "yData", "YData", (char *)NULL, offsetof(Line, y), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xerror", "xError", "XError", (char *)NULL, offsetof(Line, xError), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-ydata", "yData", "YData", (char *)NULL, offsetof(Line, y), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-yerror", "yError", "YError", (char *)NULL, offsetof(Line, yError), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xhigh", "xHigh", "XHigh", (char *)NULL, offsetof(Line, xHigh), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-xlow", "xLow", "XLow", (char *)NULL, offsetof(Line, xLow), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-yhigh", "yHigh", "YHigh", (char *)NULL, offsetof(Line, xHigh), 0, &rbcDataOption},
    {TK_CONFIG_CUSTOM, "-ylow", "yLow", "YLow", (char *)NULL, offsetof(Line, yLow), 0, &rbcDataOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}};

static Tk_ConfigSpec linePenConfigSpecs[] = {
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_PEN_ACTIVE_COLOR, offsetof(LinePen, traceColor),
     TK_CONFIG_COLOR_ONLY | ACTIVE_PEN},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_PEN_ACTIVE_MONO, offsetof(LinePen, traceColor),
     TK_CONFIG_MONO_ONLY | ACTIVE_PEN},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_PEN_NORMAL_COLOR, offsetof(LinePen, traceColor),
     TK_CONFIG_COLOR_ONLY | NORMAL_PEN},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_PEN_NORMAL_MONO, offsetof(LinePen, traceColor),
     TK_CONFIG_MONO_ONLY | NORMAL_PEN},
    {TK_CONFIG_CUSTOM, "-dashes", "dashes", "Dashes", DEF_PEN_DASHES, offsetof(LinePen, traceDashes),
     TK_CONFIG_NULL_OK | ALL_PENS, &rbcDashesOption},
    {TK_CONFIG_CUSTOM, "-errorbarcolor", "errorBarColor", "ErrorBarColor", DEF_LINE_ERRORBAR_COLOR,
     offsetof(LinePen, errorBarColor), ALL_PENS, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-errorbarwidth", "errorBarWidth", "ErrorBarWidth", DEF_LINE_ERRORBAR_LINE_WIDTH,
     offsetof(LinePen, errorBarLineWidth), ALL_PENS | TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-errorbarcap", "errorBarCap", "ErrorBarCap", DEF_LINE_ERRORBAR_CAP_WIDTH,
     offsetof(LinePen, errorBarCapWidth), TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-fill", "fill", "Fill", DEF_PEN_FILL_COLOR, offsetof(LinePen, symbol.fillColor),
     TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY | ALL_PENS, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-fill", "fill", "Fill", DEF_PEN_FILL_MONO, offsetof(LinePen, symbol.fillColor),
     TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY | ALL_PENS, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "LineWidth", (char *)NULL, offsetof(LinePen, traceWidth),
     ALL_PENS | TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-offdash", "offDash", "OffDash", DEF_PEN_OFFDASH_COLOR, offsetof(LinePen, traceOffColor),
     TK_CONFIG_NULL_OK | TK_CONFIG_COLOR_ONLY | ALL_PENS, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-offdash", "offDash", "OffDash", DEF_PEN_OFFDASH_MONO, offsetof(LinePen, traceOffColor),
     TK_CONFIG_NULL_OK | TK_CONFIG_MONO_ONLY | ALL_PENS, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-outline", "outline", "Outline", DEF_PEN_OUTLINE_COLOR, offsetof(LinePen, symbol.outlineColor),
     TK_CONFIG_COLOR_ONLY | ALL_PENS, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-outline", "outline", "Outline", DEF_PEN_OUTLINE_MONO, offsetof(LinePen, symbol.outlineColor),
     TK_CONFIG_MONO_ONLY | ALL_PENS, &rbcColorOption},
    {TK_CONFIG_CUSTOM, "-outlinewidth", "outlineWidth", "OutlineWidth", DEF_PEN_OUTLINE_WIDTH,
     offsetof(LinePen, symbol.outlineWidth), TK_CONFIG_DONT_SET_DEFAULT | ALL_PENS, &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-pixels", "pixels", "Pixels", DEF_PEN_PIXELS, offsetof(LinePen, symbol.size), ALL_PENS,
     &rbcDistanceOption},
    {TK_CONFIG_CUSTOM, "-showerrorbars", "showErrorBars", "ShowErrorBars", DEF_LINE_SHOW_ERRORBARS,
     offsetof(LinePen, errorBarShow), TK_CONFIG_DONT_SET_DEFAULT, &rbcFillOption},
    {TK_CONFIG_CUSTOM, "-showvalues", "showValues", "ShowValues", DEF_PEN_SHOW_VALUES, offsetof(LinePen, valueShow),
     ALL_PENS | TK_CONFIG_DONT_SET_DEFAULT, &rbcFillOption},
    {TK_CONFIG_CUSTOM, "-symbol", "symbol", "Symbol", DEF_PEN_SYMBOL, offsetof(LinePen, symbol),
     TK_CONFIG_DONT_SET_DEFAULT | ALL_PENS, &symbolOption},
    {TK_CONFIG_STRING, "-type", (char *)NULL, (char *)NULL, DEF_PEN_TYPE, offsetof(Pen, typeId),
     ALL_PENS | TK_CONFIG_NULL_OK},
    {TK_CONFIG_ANCHOR, "-valueanchor", "valueAnchor", "ValueAnchor", DEF_PEN_VALUE_ANCHOR,
     offsetof(LinePen, valueStyle.anchor), ALL_PENS},
    {TK_CONFIG_COLOR, "-valuecolor", "valueColor", "ValueColor", DEF_PEN_VALUE_COLOR,
     offsetof(LinePen, valueStyle.color), ALL_PENS},
    {TK_CONFIG_FONT, "-valuefont", "valueFont", "ValueFont", DEF_PEN_VALUE_FONT, offsetof(LinePen, valueStyle.font),
     ALL_PENS},
    {TK_CONFIG_STRING, "-valueformat", "valueFormat", "ValueFormat", DEF_PEN_VALUE_FORMAT,
     offsetof(LinePen, valueFormat), ALL_PENS | TK_CONFIG_NULL_OK},
    {TK_CONFIG_DOUBLE, "-valuerotate", "valueRotate", "ValueRotate", DEF_PEN_VALUE_ROTATE,
     offsetof(LinePen, valueStyle.theta), ALL_PENS},
    {TK_CONFIG_CUSTOM, "-valueshadow", "valueShadow", "ValueShadow", DEF_PEN_VALUE_SHADOW,
     offsetof(LinePen, valueStyle.shadow), ALL_PENS, &rbcShadowOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}};

typedef double(DistanceProc)(int x, int y, Point2D *p, Point2D *q, Point2D *t);

/* Forward declarations */
static PenConfigureProc ConfigurePen;
static PenDestroyProc DestroyPen;
static ElementClosestProc ClosestLine;
static ElementConfigProc ConfigureLine;
static ElementDestroyProc DestroyLine;
static ElementDrawProc DrawActiveLine;
static ElementDrawProc DrawNormalLine;
static ElementDrawSymbolProc DrawSymbol;
static ElementExtentsProc GetLineExtents;
static ElementToPostScriptProc ActiveLineToPostScript;
static ElementToPostScriptProc NormalLineToPostScript;
static ElementSymbolToPostScriptProc SymbolToPostScript;
static ElementMapProc MapLine;
static DistanceProc DistanceToY;
static DistanceProc DistanceToX;
static DistanceProc DistanceToLine;
static Rbc_TileChangedProc TileChangedProc;

INLINE static int Round(register double x);
static int StringToBitmap(Tcl_Interp *interp, Tk_Window tkwin, Symbol *symbolPtr, const char *string);
static char *NameOfSymbol(Symbol *symbolPtr);
static char *NameOfSmooth(Smoothing value);
static char *NameOfPenDir(int penDir);
static void ClearPalette(Rbc_Chain *palette);
static void InitPen(LinePen *penPtr);
static int ScaleSymbol(Element *elemPtr, int normalSize);
static void GetScreenPoints(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void ReducePoints(MapInfo *mapPtr, double tolerance);
static void GenerateSteps(MapInfo *mapPtr);
static void GenerateSpline(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void GenerateParametricSpline(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void MapSymbols(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void MapActiveSymbols(Graph *graphPtr, Line *linePtr);
static void MapStrip(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void MergePens(Line *linePtr, PenStyle **dataToStyle);
INLINE static int OutCode(Extents2D *extsPtr, Point2D *p);
static int ClipSegment(Extents2D *extsPtr, register int code1, register int code2, register Point2D *p,
                       register Point2D *q);
static void SaveTrace(Line *linePtr, int start, int length, MapInfo *mapPtr);
static void FreeTraces(Line *linePtr);
static void MapTraces(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void MapFillArea(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void ResetLine(Line *linePtr);
static int ClosestTrace(Graph *graphPtr, Line *linePtr, ClosestSearch *searchPtr, DistanceProc *distProc);
static int ClosestStrip(Graph *graphPtr, Line *linePtr, ClosestSearch *searchPtr, DistanceProc *distProc);
static void ClosestPoint(Line *linePtr, ClosestSearch *searchPtr);
static void DrawCircles(Display *display, Drawable drawable, Line *linePtr, LinePen *penPtr, int nSymbolPts,
                        Point2D *symbolPts, int radius);
static void DrawSquares(Display *display, Drawable drawable, Line *linePtr, LinePen *penPtr, int nSymbolPts,
                        register Point2D *symbolPts, int r);
static void DrawSymbols(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, int size, int nSymbolPts,
                        Point2D *symbolPts);
static void DrawTraces(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr);
static void DrawValues(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, int nSymbolPts,
                       Point2D *symbolPts, int *pointToData);
static void GetSymbolPostScriptInfo(Graph *graphPtr, PsToken psToken, LinePen *penPtr, int size);
static void SymbolsToPostScript(Graph *graphPtr, PsToken psToken, LinePen *penPtr, int size, int nSymbolPts,
                                Point2D *symbolPts);
static void SetLineAttributes(PsToken psToken, LinePen *penPtr);
static void TracesToPostScript(PsToken psToken, Line *linePtr, LinePen *penPtr);
static void ValuesToPostScript(PsToken psToken, Line *linePtr, LinePen *penPtr, int nSymbolPts, Point2D *symbolPts,
                               int *pointToData);

#ifdef WIN32
MODULE_SCOPE const int tkpWinRopModes[];
#endif

/*
 *----------------------------------------------------------------------
 *
 * Round --
 *
 *      TODO: Description
 *
 * Parameters:
 *      register double x
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
INLINE static int Round(register double x) { return (int)(x + ((x < 0.0) ? -0.5 : 0.5)); }

/*
 * ----------------------------------------------------------------------
 *     Custom configuration option (parse and print) routines
 * ----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * StringToBitmap --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Tk_Window tkwin
 *      Symbol *symbolPtr
 *      const char *string
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToBitmap(Tcl_Interp *interp, Tk_Window tkwin, Symbol *symbolPtr, const char *string) {
    Pixmap bitmap, mask;
    const char **elemArr;
    Tcl_Size nElems;
    int result;

    if (Tcl_SplitList(interp, string, &nElems, &elemArr) != TCL_OK) {
        return TCL_ERROR;
    }

    if (nElems > 2) {
        Tcl_AppendResult(interp, "too many elements in bitmap list \"", string, "\": should be \"bitmap mask\"",
                         (char *)NULL);
        result = TCL_ERROR;
        goto error;
    }
    mask = None;
    bitmap = Tk_GetBitmap(interp, tkwin, Tk_GetUid(elemArr[0]));
    if (bitmap == None) {
        result = TCL_BREAK;
        Tcl_ResetResult(interp);
        goto error;
    }
    if ((nElems > 1) && (elemArr[1][0] != '\0')) {
        mask = Tk_GetBitmap(interp, tkwin, Tk_GetUid(elemArr[1]));
        if (mask == None) {
            Tk_FreeBitmap(Tk_Display(tkwin), bitmap);
            result = TCL_ERROR;
            goto error;
        }
    }
    ckfree((char *)elemArr);
    if (symbolPtr->bitmap != None) {
        Tk_FreeBitmap(Tk_Display(tkwin), symbolPtr->bitmap);
    }
    symbolPtr->bitmap = bitmap;
    if (symbolPtr->mask != None) {
        Tk_FreeBitmap(Tk_Display(tkwin), symbolPtr->mask);
    }
    symbolPtr->mask = mask;
    return TCL_OK;
error:
    ckfree((char *)elemArr);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * PatternToString --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Offset of field in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *PatternToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                   Tcl_FreeProc **freeProcPtr) {
    Pixmap stipple = *(Pixmap *)(widgRec + offset);

    if (stipple == None) {
        return "";
    }
    if (stipple == PATTERN_SOLID) {
        return "solid";
    }
    return Tk_NameOfBitmap(Tk_Display(tkwin), stipple);
}

/*
 *----------------------------------------------------------------------
 *
 * StringToPattern --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing field
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Offset of field in record
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToPattern(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string,
                           char *widgRec, Tcl_Size offset) {
    Pixmap *stipplePtr = (Pixmap *)(widgRec + offset);
    Pixmap stipple;

    if ((string == NULL) || (string[0] == '\0')) {
        stipple = None;
    } else if (strcmp(string, "solid") == 0) {
        stipple = PATTERN_SOLID;
    } else {
        stipple = Tk_GetBitmap(interp, tkwin, Tk_GetUid(string));
        if (stipple == None) {
            return TCL_ERROR;
        }
    }
    if ((*stipplePtr != None) && (*stipplePtr != PATTERN_SOLID)) {
        Tk_FreeBitmap(Tk_Display(tkwin), *stipplePtr);
    }
    *stipplePtr = stipple;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NameOfSymbol --
 *
 *      Converts the symbol value into its string representation.
 *
 * Parameters:
 *      Symbol *symbolPtr
 *
 * Results:
 *      The static string representing the symbol type is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static char *NameOfSymbol(Symbol *symbolPtr) {
    switch (symbolPtr->type) {
    case SYMBOL_NONE:
        return "none";
    case SYMBOL_SQUARE:
        return "square";
    case SYMBOL_CIRCLE:
        return "circle";
    case SYMBOL_DIAMOND:
        return "diamond";
    case SYMBOL_PLUS:
        return "plus";
    case SYMBOL_CROSS:
        return "cross";
    case SYMBOL_SPLUS:
        return "splus";
    case SYMBOL_SCROSS:
        return "scross";
    case SYMBOL_TRIANGLE:
        return "triangle";
    case SYMBOL_ARROW:
        return "arrow";
    case SYMBOL_BITMAP:
        return "bitmap";
    }
    return NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToSymbol --
 *
 *      Convert the string representation of a line style or symbol name
 *      into its numeric form.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing symbol type
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Offset of symbol type field in record
 *
 * Results:
 *      The return value is a standard Tcl result.  The symbol type is
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToSymbol(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                          Tcl_Size offset) {
    Symbol *symbolPtr = (Symbol *)(widgRec + offset);
    unsigned int length;
    char c;

    c = string[0];
    length = strlen(string);
    if (c == '\0') {
        symbolPtr->type = SYMBOL_NONE;
    } else if ((c == 'n') && (strncmp(string, "none", length) == 0)) {
        symbolPtr->type = SYMBOL_NONE;
    } else if ((c == 'c') && (length > 1) && (strncmp(string, "circle", length) == 0)) {
        symbolPtr->type = SYMBOL_CIRCLE;
    } else if ((c == 's') && (length > 1) && (strncmp(string, "square", length) == 0)) {
        symbolPtr->type = SYMBOL_SQUARE;
    } else if ((c == 'd') && (strncmp(string, "diamond", length) == 0)) {
        symbolPtr->type = SYMBOL_DIAMOND;
    } else if ((c == 'p') && (strncmp(string, "plus", length) == 0)) {
        symbolPtr->type = SYMBOL_PLUS;
    } else if ((c == 'c') && (length > 1) && (strncmp(string, "cross", length) == 0)) {
        symbolPtr->type = SYMBOL_CROSS;
    } else if ((c == 's') && (length > 1) && (strncmp(string, "splus", length) == 0)) {
        symbolPtr->type = SYMBOL_SPLUS;
    } else if ((c == 's') && (length > 1) && (strncmp(string, "scross", length) == 0)) {
        symbolPtr->type = SYMBOL_SCROSS;
    } else if ((c == 't') && (strncmp(string, "triangle", length) == 0)) {
        symbolPtr->type = SYMBOL_TRIANGLE;
    } else if ((c == 'a') && (strncmp(string, "arrow", length) == 0)) {
        symbolPtr->type = SYMBOL_ARROW;
    } else {
        int result;

        result = StringToBitmap(interp, tkwin, symbolPtr, string);
        if (result != TCL_OK) {
            if (result != TCL_ERROR) {
                Tcl_AppendResult(interp, "bad symbol \"", string,
                                 "\": should be \"none\", \"circle\", \"square\", \"diamond\", \"plus\", \
\"cross\", \"splus\", \"scross\", \"triangle\", \"arrow\" \
or the name of a bitmap",
                                 (char *)NULL);
            }
            return TCL_ERROR;
        }
        symbolPtr->type = SYMBOL_BITMAP;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * SymbolToString --
 *
 *      Convert the symbol value into a string.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Offset of symbol type field in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The string representing the symbol type or line style is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *SymbolToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                  Tcl_FreeProc **freeProcPtr) {
    Symbol *symbolPtr = (Symbol *)(widgRec + offset);
    char *result;

    if (symbolPtr->type == SYMBOL_BITMAP) {
        Tcl_DString dString;

        Tcl_DStringInit(&dString);
        Tcl_DStringAppendElement(&dString, Tk_NameOfBitmap(Tk_Display(tkwin), symbolPtr->bitmap));
        Tcl_DStringAppendElement(&dString,
                                 (symbolPtr->mask == None) ? "" : Tk_NameOfBitmap(Tk_Display(tkwin), symbolPtr->mask));
        result = RbcStrdup(Tcl_DStringValue(&dString));
        Tcl_DStringFree(&dString);
        *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    } else {
        result = NameOfSymbol(symbolPtr);
    }
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * NameOfSmooth --
 *
 *      Converts the smooth value into its string representation.
 *
 * Parameters:
 *      Smoothing value
 *
 * Results:
 *      The static string representing the smooth type is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static char *NameOfSmooth(Smoothing value) {
    if ((value < 0) || (value >= PEN_SMOOTH_LAST)) {
        return "unknown smooth value";
    }
    return smoothingInfo[value].name;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToSmooth --
 *
 *      Convert the string representation of a line style or smooth name
 *      into its numeric form.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing smooth type
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Offset of smooth type field in record
 *
 * Results:
 *      The return value is a standard Tcl result.  The smooth type is
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToSmooth(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                          Tcl_Size offset) {
    Smoothing *valuePtr = (Smoothing *)(widgRec + offset);
    register SmoothingInfo *siPtr;

    for (siPtr = smoothingInfo; siPtr->name != NULL; siPtr++) {
        if (strcmp(string, siPtr->name) == 0) {
            *valuePtr = siPtr->value;
            return TCL_OK;
        }
    }
    Tcl_AppendResult(interp, "bad smooth value \"", string, "\": should be \
linear, step, natural, or quadratic",
                     (char *)NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * SmoothToString --
 *
 *      Convert the smooth value into a string.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Offset of smooth type field in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The string representing the smooth type or line style is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *SmoothToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                  Tcl_FreeProc **freeProcPtr) {
    int smooth = *(int *)(widgRec + offset);

    return NameOfSmooth(smooth);
}

/*
 *----------------------------------------------------------------------
 *
 * StringToPenDir --
 *
 *      Convert the string representation of a line style or symbol name
 *      into its numeric form.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing pen direction
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Offset of pen direction field in record 
 *
 * Results:
 *      The return value is a standard Tcl result.  The symbol type is
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToPenDir(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                          Tcl_Size offset) {
    int *penDirPtr = (int *)(widgRec + offset);
    unsigned int length;
    char c;

    c = string[0];
    length = strlen(string);
    if ((c == 'i') && (strncmp(string, "increasing", length) == 0)) {
        *penDirPtr = PEN_INCREASING;
    } else if ((c == 'd') && (strncmp(string, "decreasing", length) == 0)) {
        *penDirPtr = PEN_DECREASING;
    } else if ((c == 'b') && (strncmp(string, "both", length) == 0)) {
        *penDirPtr = PEN_BOTH_DIRECTIONS;
    } else {
        Tcl_AppendResult(interp, "bad trace value \"", string,
                         "\" : should be \"increasing\", \"decreasing\", or \"both\"", (char *)NULL);
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NameOfPenDir --
 *
 *      Convert the pen direction into a string.
 *
 * Parameters:
 *      int penDir - Direction for pen drawing between points
 *
 * Results:
 *      The static string representing the pen direction is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static char *NameOfPenDir(int penDir) {
    switch (penDir) {
    case PEN_INCREASING:
        return "increasing";
    case PEN_DECREASING:
        return "decreasing";
    case PEN_BOTH_DIRECTIONS:
        return "both";
    default:
        return "unknown trace direction";
    }
}

/*
 *----------------------------------------------------------------------
 *
 * PenDirToString --
 *
 *      Convert the pen direction into a string.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Offset of pen direction field in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The string representing the pen drawing direction is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *PenDirToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                  Tcl_FreeProc **freeProcPtr) {
    int penDir = *(int *)(widgRec + offset);

    return NameOfPenDir(penDir);
}

/*
 *----------------------------------------------------------------------
 *
 * ClearPalette --
 *
 *      Clear the number of points and segments, in case there are no
 *      segments or points
 *
 * Parameters:
 *      Rbc_Chain *palette
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void ClearPalette(Rbc_Chain *palette) {
    register LinePenStyle *stylePtr;
    Rbc_ChainLink *linkPtr;

    for (linkPtr = Rbc_ChainFirstLink(palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        stylePtr->nStrips = stylePtr->nSymbolPts = 0;
        stylePtr->xErrorBarCnt = stylePtr->yErrorBarCnt = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigurePen --
 *
 *      Sets up the appropriate configuration parameters in the GC.
 *      It is assumed the parameters have been previously set by
 *      a call to Tk_ConfigureWidget.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Pen *penPtr
 *
 * Results:
 *      The return value is a standard Tcl result.  If TCL_ERROR is
 *      returned, then the interpreter result contains an error message.
 *
 * Side effects:
 *      Configuration information such as line width, line style, color
 *      etc. get set in a new GC.
 *
 *----------------------------------------------------------------------
 */
static int ConfigurePen(Graph *graphPtr, Pen *penPtr) {
    LinePen *lpPtr = (LinePen *)penPtr;
    unsigned long gcMask;
    GC newGC;
    XGCValues gcValues;
    XColor *colorPtr;

    Rbc_ResetTextStyle(graphPtr->tkwin, &(lpPtr->valueStyle));
    /*
     * Set the outline GC for this pen: GCForeground is outline color.
     * GCBackground is the fill color (only used for bitmap symbols).
     */
    gcMask = (GCLineWidth | GCForeground);
    colorPtr = lpPtr->symbol.outlineColor;
    if (colorPtr == COLOR_DEFAULT) {
        colorPtr = lpPtr->traceColor;
    }
    gcValues.foreground = colorPtr->pixel;
    if (lpPtr->symbol.type == SYMBOL_BITMAP) {
        colorPtr = lpPtr->symbol.fillColor;
        if (colorPtr == COLOR_DEFAULT) {
            colorPtr = lpPtr->traceColor;
        }
        /*
         * Set a clip mask if either
         *    1) no background color was designated or
         *    2) a masking bitmap was specified.
         *
         * These aren't necessarily the bitmaps we'll be using for
         * clipping. But this makes it unlikely that anyone else will
         * be sharing this GC when we set the clip origin (at the time
         * the bitmap is drawn).
         */
        if (colorPtr != NULL) {
            gcValues.background = colorPtr->pixel;
            gcMask |= GCBackground;
            if (lpPtr->symbol.mask != None) {
                gcValues.clip_mask = lpPtr->symbol.mask;
                gcMask |= GCClipMask;
            }
        } else {
            gcValues.clip_mask = lpPtr->symbol.bitmap;
            gcMask |= GCClipMask;
        }
    }
    gcValues.line_width = LineWidth(lpPtr->symbol.outlineWidth);
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (lpPtr->symbol.outlineGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->symbol.outlineGC);
    }
    lpPtr->symbol.outlineGC = newGC;

    /* Fill GC for symbols: GCForeground is fill color */

    gcMask = (GCLineWidth | GCForeground);
    colorPtr = lpPtr->symbol.fillColor;
    if (colorPtr == COLOR_DEFAULT) {
        colorPtr = lpPtr->traceColor;
    }
    newGC = NULL;
    if (colorPtr != NULL) {
        gcValues.foreground = colorPtr->pixel;
        newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    }
    if (lpPtr->symbol.fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->symbol.fillGC);
    }
    lpPtr->symbol.fillGC = newGC;

    /* Line segments */

    gcMask = (GCLineWidth | GCForeground | GCLineStyle | GCCapStyle | GCJoinStyle);
    gcValues.cap_style = CapButt;
    gcValues.join_style = JoinRound;
    gcValues.line_style = LineSolid;
    gcValues.line_width = LineWidth(lpPtr->traceWidth);

    colorPtr = lpPtr->traceOffColor;
    if (colorPtr == COLOR_DEFAULT) {
        colorPtr = lpPtr->traceColor;
    }
    if (colorPtr != NULL) {
        gcMask |= GCBackground;
        gcValues.background = colorPtr->pixel;
    }
    gcValues.foreground = lpPtr->traceColor->pixel;
    if (LineIsDashed(lpPtr->traceDashes)) {
        gcValues.line_width = lpPtr->traceWidth;
        gcValues.line_style = (colorPtr == NULL) ? LineOnOffDash : LineDoubleDash;
    }
    newGC = Rbc_GetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
    if (lpPtr->traceGC != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, lpPtr->traceGC);
    }
    if (LineIsDashed(lpPtr->traceDashes)) {
        lpPtr->traceDashes.offset = lpPtr->traceDashes.values[0] / 2;
        Rbc_SetDashes(graphPtr->display, newGC, &(lpPtr->traceDashes));
    }
    lpPtr->traceGC = newGC;

    gcMask = (GCLineWidth | GCForeground);
    colorPtr = lpPtr->errorBarColor;
    if (colorPtr == COLOR_DEFAULT) {
        colorPtr = lpPtr->traceColor;
    }
    gcValues.line_width = LineWidth(lpPtr->errorBarLineWidth);
    gcValues.foreground = colorPtr->pixel;
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (lpPtr->errorBarGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->errorBarGC);
    }
    lpPtr->errorBarGC = newGC;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyPen --
 *
 *      Release memory and resources allocated for the style.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Pen *penPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Everything associated with the pen style is freed up.
 *
 *----------------------------------------------------------------------
 */
static void DestroyPen(Graph *graphPtr, Pen *penPtr) {
    LinePen *lpPtr = (LinePen *)penPtr;

    Rbc_FreeTextStyle(graphPtr->display, &(lpPtr->valueStyle));
    if (lpPtr->symbol.outlineGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->symbol.outlineGC);
    }
    if (lpPtr->symbol.fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->symbol.fillGC);
    }
    if (lpPtr->errorBarGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->errorBarGC);
    }
    if (lpPtr->traceGC != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, lpPtr->traceGC);
    }
    if (lpPtr->symbol.bitmap != None) {
        Tk_FreeBitmap(graphPtr->display, lpPtr->symbol.bitmap);
        lpPtr->symbol.bitmap = None;
    }
    if (lpPtr->symbol.mask != None) {
        Tk_FreeBitmap(graphPtr->display, lpPtr->symbol.mask);
        lpPtr->symbol.mask = None;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * InitPen --
 *
 *      TODO: Description
 *
 * Parameters:
 *      LinePen *penPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void InitPen(LinePen *penPtr) {
    Rbc_InitTextStyle(&penPtr->valueStyle);
    penPtr->configProc = ConfigurePen;
    penPtr->configSpecs = linePenConfigSpecs;
    penPtr->destroyProc = DestroyPen;
    penPtr->errorBarLineWidth = 1;
    penPtr->errorBarShow = SHOW_BOTH;
    penPtr->flags = NORMAL_PEN;
    penPtr->name = "";
    penPtr->symbol.bitmap = penPtr->symbol.mask = None;
    penPtr->symbol.outlineColor = penPtr->symbol.fillColor = COLOR_DEFAULT;
    penPtr->symbol.outlineWidth = penPtr->traceWidth = 1;
    penPtr->symbol.type = SYMBOL_CIRCLE;
    penPtr->valueShow = SHOW_NONE;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LinePen --
 *
 *      TODO: Description
 *
 * Parameters:
 *      char *penName
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Pen *Rbc_LinePen(char *penName) {
    LinePen *penPtr;

    penPtr = RbcCalloc(1, sizeof(LinePen));
    assert(penPtr);
    InitPen(penPtr);
    penPtr->name = RbcStrdup(penName);
    if (strcmp(penName, "activeLine") == 0) {
        penPtr->flags = ACTIVE_PEN;
    }
    return (Pen *)penPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 *  In this section, the routines deal with building and filling
 *  the element's data structures with transformed screen
 *  coordinates.  They are triggered from TranformLine which is
 *  called whenever the data or coordinates axes have changed and
 *  new screen coordinates need to be calculated.
 *
 * ----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * ScaleSymbol --
 *
 *      Returns the scaled size for the line element. Scaling depends
 *      upon when the base line ranges for the element were set and
 *      the current range of the graph.
 *
 * Parameters:
 *      Element *elemPtr
 *      int normalSize
 *
 * Results:
 *      The new size of the symbol, after considering how much the
 *      graph has been scaled, is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ScaleSymbol(Element *elemPtr, int normalSize) {
    int maxSize;
    double scale;
    int newSize;

    scale = 1.0;
    if (elemPtr->scaleSymbols) {
        double xRange, yRange;

        xRange = (elemPtr->axes.x->max - elemPtr->axes.x->min);
        yRange = (elemPtr->axes.y->max - elemPtr->axes.y->min);
        if (elemPtr->flags & SCALE_SYMBOL) {
            /* Save the ranges as a baseline for future scaling. */
            elemPtr->xRange = xRange;
            elemPtr->yRange = yRange;
            elemPtr->flags &= ~SCALE_SYMBOL;
        } else {
            double xScale, yScale;

            /* Scale the symbol by the smallest change in the X or Y axes */
            xScale = elemPtr->xRange / xRange;
            yScale = elemPtr->yRange / yRange;
            scale = MIN(xScale, yScale);
        }
    }
    newSize = Round(normalSize * scale);

    /*
     * Don't let the size of symbols go unbounded. Both X and Win32
     * drawing routines assume coordinates to be a signed short int.
     */
    maxSize = (int)MIN(elemPtr->graphPtr->hRange, elemPtr->graphPtr->vRange);
    if (newSize > maxSize) {
        newSize = maxSize;
    }

    /* Make the symbol size odd so that its center is a single pixel. */
    newSize |= 0x01;
    return newSize;
}

/*
 *----------------------------------------------------------------------
 *
 * GetScreenPoints --
 *
 *      Generates a coordinate array of transformed screen coordinates
 *      from the data points.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Line *linePtr
 *      MapInfo *mapPtr
 *
 * Results:
 *      The transformed screen coordinates are returned.
 *
 * Side effects:
 *      Memory is allocated for the coordinate array.
 *
 *----------------------------------------------------------------------
 */
static void GetScreenPoints(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    double *x, *y;
    register int i, n;
    register int count;
    register Point2D *screenPts;
    register int *indices;

    n = NumberOfPoints(linePtr);
    x = linePtr->x.valueArr;
    y = linePtr->y.valueArr;
    screenPts = (Point2D *)ckalloc(sizeof(Point2D) * n);
    assert(screenPts);
    indices = (int *)ckalloc(sizeof(int) * n);
    assert(indices);

    count = 0; /* Count the valid screen coordinates */
    if (graphPtr->inverted) {
        for (i = 0; i < n; i++) {
            if ((FINITE(x[i])) && (FINITE(y[i]))) {
                screenPts[count].x = Rbc_HMap(graphPtr, linePtr->axes.y, y[i]);
                screenPts[count].y = Rbc_VMap(graphPtr, linePtr->axes.x, x[i]);
                indices[count] = i;
                count++;
            }
        }
    } else {
        for (i = 0; i < n; i++) {
            if ((FINITE(x[i])) && (FINITE(y[i]))) {
                screenPts[count].x = Rbc_HMap(graphPtr, linePtr->axes.x, x[i]);
                screenPts[count].y = Rbc_VMap(graphPtr, linePtr->axes.y, y[i]);
                indices[count] = i;
                count++;
            }
        }
    }
    mapPtr->screenPts = screenPts;
    mapPtr->nScreenPts = count;
    mapPtr->indices = indices;
}

/*
 *----------------------------------------------------------------------
 *
 * ReducePoints --
 *
 *      Generates a coordinate array of transformed screen coordinates
 *      from the data points.
 *
 * Parameters:
 *      MapInfo *mapPtr
 *      double tolerance
 *
 * Results:
 *      The transformed screen coordinates are returned.
 *
 * Side effects:
 *      Memory is allocated for the coordinate array.
 *
 *----------------------------------------------------------------------
 */
static void ReducePoints(MapInfo *mapPtr, double tolerance) {
    register int i, k, n;
    Point2D *screenPts;
    int *indices, *simple;

    simple = (int *)ckalloc(sizeof(int) * mapPtr->nScreenPts);
    indices = (int *)ckalloc(sizeof(int) * mapPtr->nScreenPts);
    screenPts = (Point2D *)ckalloc(sizeof(Point2D) * mapPtr->nScreenPts);
    n = Rbc_SimplifyLine(mapPtr->screenPts, 0, mapPtr->nScreenPts - 1, tolerance, simple);
    for (i = 0; i < n; i++) {
        k = simple[i];
        screenPts[i] = mapPtr->screenPts[k];
        indices[i] = mapPtr->indices[k];
    }
#ifdef notdef
    if (n < mapPtr->nScreenPts) {
        fprintf(stderr, "reduced from %d to %d\n", mapPtr->nScreenPts, n);
    }
#endif
    ckfree((char *)mapPtr->screenPts);
    ckfree((char *)mapPtr->indices);
    ckfree((char *)simple);
    mapPtr->screenPts = screenPts;
    mapPtr->indices = indices;
    mapPtr->nScreenPts = n;
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateSteps --
 *
 *      Resets the coordinate and pen index arrays adding new points
 *      for step-and-hold type smoothing.
 *
 * Parameters:
 *      MapInfo *mapPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The temporary arrays for screen coordinates and pen indices
 *      are updated.
 *
 *----------------------------------------------------------------------
 */
static void GenerateSteps(MapInfo *mapPtr) {
    int newSize;
    register int i, count;
    Point2D *screenPts;
    int *indices;

    newSize = ((mapPtr->nScreenPts - 1) * 2) + 1;
    screenPts = (Point2D *)ckalloc(newSize * sizeof(Point2D));
    assert(screenPts);
    indices = (int *)ckalloc(sizeof(int) * newSize);
    assert(indices);

    screenPts[0] = mapPtr->screenPts[0];
    indices[0] = 0;

    count = 1;
    for (i = 1; i < mapPtr->nScreenPts; i++) {
        screenPts[count + 1] = mapPtr->screenPts[i];

        /* Hold last y-coordinate, use new x-coordinate */
        screenPts[count].x = screenPts[count + 1].x;
        screenPts[count].y = screenPts[count - 1].y;

        /* Use the same style for both the hold and the step points */
        indices[count] = indices[count + 1] = mapPtr->indices[i];
        count += 2;
    }
    ckfree((char *)mapPtr->screenPts);
    ckfree((char *)mapPtr->indices);
    mapPtr->indices = indices;
    mapPtr->screenPts = screenPts;
    mapPtr->nScreenPts = newSize;
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateSpline --
 *
 *      Computes a spline based upon the data points, returning a new
 *      (larger) coordinate array or points.
 *
 * Parameters:
 *      Graph *graphPtr;
 *      Line *linePtr
 *      MapInfo *mapPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The temporary arrays for screen coordinates and data indices
 *      are updated based upon spline.
 *
 * FIXME:  Can't interpolate knots along the Y-axis.   Need to break
 *       up point array into interchangable X and Y vectors earlier.
 *       Pass extents (left/right or top/bottom) as parameters.
 *
 *----------------------------------------------------------------------
 */
static void GenerateSpline(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    int extra;
    register int i, j, count;
    Point2D *origPts, *intpPts;
    int *indices;
    int nIntpPts, nOrigPts;
    int result;
    int x;

    nOrigPts = mapPtr->nScreenPts;
    origPts = mapPtr->screenPts;
    assert(mapPtr->nScreenPts > 0);
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {
        if (origPts[j].x <= origPts[i].x) {
            return; /* Points are not monotonically increasing */
        }
    }
    if (((origPts[0].x > (double)graphPtr->right)) || ((origPts[mapPtr->nScreenPts - 1].x < (double)graphPtr->left))) {
        return; /* All points are clipped */
    }
    /*
     * The spline is computed in screen coordinates instead of data
     * points so that we can select the abscissas of the interpolated
     * points from each pixel horizontally across the plotting area.
     */
    extra = (graphPtr->right - graphPtr->left) + 1;
    if (extra < 1) {
        return;
    }
    nIntpPts = nOrigPts + extra + 1;
    intpPts = (Point2D *)ckalloc(nIntpPts * sizeof(Point2D));
    assert(intpPts);

    indices = (int *)ckalloc(sizeof(int) * nIntpPts);
    assert(indices);

    /* Populate the x2 array with both the original X-coordinates and
     * extra X-coordinates for each horizontal pixel that the line
     * segment contains. */
    count = 0;
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {

        /* Add the original x-coordinate */
        intpPts[count].x = origPts[i].x;

        /* Include the starting offset of the point in the offset array */
        indices[count] = mapPtr->indices[i];
        count++;

        /* Is any part of the interval (line segment) in the plotting
         * area?  */
        if ((origPts[j].x >= (double)graphPtr->left) || (origPts[i].x <= (double)graphPtr->right)) {
            int last;

            x = (int)(origPts[i].x + 1.0);

            /*
             * Since the line segment may be partially clipped on the
             * left or right side, the points to interpolate are
             * always interior to the plotting area.
             *
             *           left                right
             *      x1----|--------------------------|---x2
             *
             * Pick the max of the starting X-coordinate and the
             * left edge and the min of the last X-coordinate and
             * the right edge.
             */
            x = MAX(x, graphPtr->left);
            last = (int)MIN(origPts[j].x, graphPtr->right);

            /* Add the extra x-coordinates to the interval. */
            while (x < last) {
                indices[count] = mapPtr->indices[i];
                intpPts[count++].x = (double)x;
                x++;
            }
        }
    }
    nIntpPts = count;
    result = FALSE;
    if (linePtr->smooth == PEN_SMOOTH_NATURAL) {
        result = Rbc_NaturalSpline(origPts, nOrigPts, intpPts, nIntpPts);
    } else if (linePtr->smooth == PEN_SMOOTH_QUADRATIC) {
        result = Rbc_QuadraticSpline(origPts, nOrigPts, intpPts, nIntpPts);
    }
    if (!result) {
        /* The spline interpolation failed.  We'll fallback to the
         * current coordinates and do no smoothing (standard line
         * segments).  */
        linePtr->smooth = PEN_SMOOTH_NONE;
        ckfree((char *)intpPts);
        ckfree((char *)indices);
    } else {
        ckfree((char *)mapPtr->screenPts);
        ckfree((char *)mapPtr->indices);
        mapPtr->indices = indices;
        mapPtr->screenPts = intpPts;
        mapPtr->nScreenPts = nIntpPts;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GenerateParametricSpline --
 *
 *      Computes a spline based upon the data points, returning a new
 *      (larger) coordinate array or points.
 *
 * Parameters:
 *      Graph *graphPtr;
 *      Line *linePtr
 *      MapInfo *mapPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The temporary arrays for screen coordinates and data indices
 *      are updated based upon spline.
 *
 * FIXME:  Can't interpolate knots along the Y-axis.   Need to break
 *       up point array into interchangable X and Y vectors earlier.
 *       Pass extents (left/right or top/bottom) as parameters.
 *
 *----------------------------------------------------------------------
 */
static void GenerateParametricSpline(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    Extents2D exts;
    Point2D *origPts, *intpPts;
    Point2D p, q;
    double dist;
    int *indices;
    int nIntpPts, nOrigPts;
    int result;
    register int i, j, count;

    nOrigPts = mapPtr->nScreenPts;
    origPts = mapPtr->screenPts;
    assert(mapPtr->nScreenPts > 0);

    Rbc_GraphExtents(graphPtr, &exts);

    /*
     * Populate the x2 array with both the original X-coordinates and
     * extra X-coordinates for each horizontal pixel that the line
     * segment contains.
     */
    count = 1;
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {
        p = origPts[i];
        q = origPts[j];
        count++;
        if (Rbc_LineRectClip(&exts, &p, &q)) {
            count += (int)(hypot(q.x - p.x, q.y - p.y) * 0.5);
        }
    }
    nIntpPts = count;
    intpPts = (Point2D *)ckalloc(nIntpPts * sizeof(Point2D));
    assert(intpPts);

    indices = (int *)ckalloc(sizeof(int) * nIntpPts);
    assert(indices);

    /*
     * FIXME: This is just plain wrong.  The spline should be computed
     *        and evaluated in separate steps.  This will mean breaking
     *          up this routine since the catrom coefficients can be
     *          independently computed for original data point.  This
     *          also handles the problem of allocating enough points
     *          since evaluation is independent of the number of points
     *        to be evalualted.  The interpolated
     *          line segments should be clipped, not the original segments.
     */
    count = 0;
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {
        p = origPts[i];
        q = origPts[j];

        dist = hypot(q.x - p.x, q.y - p.y);
        /* Add the original x-coordinate */
        intpPts[count].x = (double)i;
        intpPts[count].y = 0.0;

        /* Include the starting offset of the point in the offset array */
        indices[count] = mapPtr->indices[i];
        count++;

        /* Is any part of the interval (line segment) in the plotting
         * area?  */

        if (Rbc_LineRectClip(&exts, &p, &q)) {
            double distP, distQ;

            distP = hypot(p.x - origPts[i].x, p.y - origPts[i].y);
            distQ = hypot(q.x - origPts[i].x, q.y - origPts[i].y);
            distP += 2.0;
            while (distP <= distQ) {
                /* Point is indicated by its interval and parameter t. */
                intpPts[count].x = (double)i;
                intpPts[count].y = distP / dist;
                indices[count] = mapPtr->indices[i];
                count++;
                distP += 2.0;
            }
        }
    }
    intpPts[count].x = (double)i;
    intpPts[count].y = 0.0;
    indices[count] = mapPtr->indices[i];
    count++;
    nIntpPts = count;
    result = FALSE;
    if (linePtr->smooth == PEN_SMOOTH_NATURAL) {
        result = Rbc_NaturalParametricSpline(origPts, nOrigPts, &exts, FALSE, intpPts, nIntpPts);
    } else if (linePtr->smooth == PEN_SMOOTH_CATROM) {
        result = Rbc_CatromParametricSpline(origPts, nOrigPts, intpPts, nIntpPts);
    }
    if (!result) {
        /* The spline interpolation failed.  We'll fallback to the
         * current coordinates and do no smoothing (standard line
         * segments).  */
        linePtr->smooth = PEN_SMOOTH_NONE;
        ckfree((char *)intpPts);
        ckfree((char *)indices);
    } else {
        ckfree((char *)mapPtr->screenPts);
        ckfree((char *)mapPtr->indices);
        mapPtr->indices = indices;
        mapPtr->screenPts = intpPts;
        mapPtr->nScreenPts = nIntpPts;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MapSymbols --
 *
 *      Creates two arrays of points and pen indices, filled with
 *      the screen coordinates of the visible
 *
 * Parameters:
 *      Graph *graphPtr;
 *      Line *linePtr
 *      MapInfo *mapPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed and allocated for the index array.
 *
 *----------------------------------------------------------------------
 */
static void MapSymbols(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    Extents2D exts;
    Point2D *symbolPts;
    int *indices;
    register int i, count;

    symbolPts = (Point2D *)ckalloc(sizeof(Point2D) * mapPtr->nScreenPts);
    assert(symbolPts);

    indices = (int *)ckalloc(sizeof(int) * mapPtr->nScreenPts);
    assert(indices);

    Rbc_GraphExtents(graphPtr, &exts);
    count = 0; /* Count the number of visible points */

    for (i = 0; i < mapPtr->nScreenPts; i++) {
        if (PointInRegion(&exts, mapPtr->screenPts[i].x, mapPtr->screenPts[i].y)) {
            symbolPts[count].x = mapPtr->screenPts[i].x;
            symbolPts[count].y = mapPtr->screenPts[i].y;
            indices[count] = mapPtr->indices[i];
            count++;
        }
    }
    linePtr->symbolPts = symbolPts;
    linePtr->nSymbolPts = count;
    linePtr->symbolToData = indices;
}

/*
 *----------------------------------------------------------------------
 *
 * MapActiveSymbols --
 *
 *      Creates an array of points of the active graph coordinates.
 *
 * Parameters:
 *      Graph *graphPtr;
 *      Line *linePtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed and allocated for the active point array.
 *
 *----------------------------------------------------------------------
 */
static void MapActiveSymbols(Graph *graphPtr, Line *linePtr) {
    Extents2D exts;
    double x, y;
    int count;
    Point2D *activePts;
    register int i;
    int pointIndex;
    int nPoints;
    int *activeToData;

    if (linePtr->activePts != NULL) {
        ckfree((char *)linePtr->activePts);
        linePtr->activePts = NULL;
    }
    if (linePtr->activeToData != NULL) {
        ckfree((char *)linePtr->activeToData);
        linePtr->activeToData = NULL;
    }
    Rbc_GraphExtents(graphPtr, &exts);
    activePts = (Point2D *)ckalloc(sizeof(Point2D) * linePtr->nActiveIndices);
    assert(activePts);
    activeToData = (int *)ckalloc(sizeof(int) * linePtr->nActiveIndices);
    nPoints = NumberOfPoints(linePtr);
    count = 0; /* Count the visible active points */
    for (i = 0; i < linePtr->nActiveIndices; i++) {
        pointIndex = linePtr->activeIndices[i];
        if (pointIndex >= nPoints) {
            continue; /* Index not available */
        }
        x = linePtr->x.valueArr[pointIndex];
        y = linePtr->y.valueArr[pointIndex];
        activePts[count] = Rbc_Map2D(graphPtr, x, y, &(linePtr->axes));
        activeToData[count] = pointIndex;
        if (PointInRegion(&exts, activePts[count].x, activePts[count].y)) {
            count++;
        }
    }
    if (count > 0) {
        linePtr->activePts = activePts;
        linePtr->activeToData = activeToData;
    } else {
        /* No active points were visible. */
        ckfree((char *)activePts);
        ckfree((char *)activeToData);
    }
    linePtr->nActivePts = count;
    linePtr->flags &= ~ACTIVE_PENDING;
}

/*
 *----------------------------------------------------------------------
 *
 * MapStrip --
 *
 *      Creates an array of line segments of the graph coordinates.
 *
 * Parameters:
 *      Graph *graphPtr;
 *      Line *linePtr
 *      MapInfo *mapPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is  allocated for the line segment array.
 *
 *----------------------------------------------------------------------
 */
static void MapStrip(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    Extents2D exts;
    Segment2D *strips;
    int *indices, *indexPtr;
    register Point2D *endPtr, *pointPtr;
    register Segment2D *segPtr;
    register int count;

    indices = (int *)ckalloc(sizeof(int) * mapPtr->nScreenPts);
    assert(indices);

    /*
     * Create array to hold points for line segments (not polyline
     * coordinates).  So allocate twice the number of points.
     */
    segPtr = strips = (Segment2D *)ckalloc(mapPtr->nScreenPts * sizeof(Segment2D));
    assert(strips);

    Rbc_GraphExtents(graphPtr, &exts);
    count = 0; /* Count the number of segments. */
    indexPtr = mapPtr->indices;
    for (pointPtr = mapPtr->screenPts, endPtr = mapPtr->screenPts + (mapPtr->nScreenPts - 1); pointPtr < endPtr;
         pointPtr++, indexPtr++) {
        segPtr->p = pointPtr[0];
        segPtr->q = pointPtr[1];
        if (Rbc_LineRectClip(&exts, &segPtr->p, &segPtr->q)) {
            segPtr++;
            indices[count] = *indexPtr;
            count++;
        }
    }
    linePtr->stripToData = indices;
    linePtr->nStrips = count;
    linePtr->strips = strips;
}

/*
 *----------------------------------------------------------------------
 *
 * MergePens --
 *
 *      Reorders the both arrays of points and segments to merge pens.
 *
 * Parameters:
 *      Line *linePtr
 *      PenStyle **dataToStyle
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The old arrays are freed and new ones allocated containing
 *      the reordered points and segments.
 *
 *----------------------------------------------------------------------
 */
static void MergePens(Line *linePtr, PenStyle **dataToStyle) {
    LinePenStyle *stylePtr;
    register int i;
    Rbc_ChainLink *linkPtr;

    if (Rbc_ChainGetLength(linePtr->palette) < 2) {
        linkPtr = Rbc_ChainFirstLink(linePtr->palette);
        stylePtr = Rbc_ChainGetValue(linkPtr);
        stylePtr->nStrips = linePtr->nStrips;
        stylePtr->strips = linePtr->strips;
        stylePtr->nSymbolPts = linePtr->nSymbolPts;
        stylePtr->symbolPts = linePtr->symbolPts;
        stylePtr->xErrorBarCnt = linePtr->xErrorBarCnt;
        stylePtr->yErrorBarCnt = linePtr->yErrorBarCnt;
        stylePtr->xErrorBars = linePtr->xErrorBars;
        stylePtr->yErrorBars = linePtr->yErrorBars;
        stylePtr->errorBarCapWidth = linePtr->errorBarCapWidth;
        return;
    }

    /* We have more than one style. Group line segments and points of
     * like pen styles.  */

    if (linePtr->nStrips > 0) {
        Segment2D *strips;
        int *stripToData;
        register Segment2D *segPtr;
        register int *indexPtr;
        int dataIndex;

        strips = (Segment2D *)ckalloc(linePtr->nStrips * sizeof(Segment2D));
        stripToData = (int *)ckalloc(linePtr->nStrips * sizeof(int));
        assert(strips && stripToData);
        segPtr = strips, indexPtr = stripToData;
        for (linkPtr = Rbc_ChainFirstLink(linePtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->strips = segPtr;
            for (i = 0; i < linePtr->nStrips; i++) {
                dataIndex = linePtr->stripToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *segPtr++ = linePtr->strips[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->nStrips = segPtr - stylePtr->strips;
        }
        ckfree((char *)linePtr->strips);
        linePtr->strips = strips;
        ckfree((char *)linePtr->stripToData);
        linePtr->stripToData = stripToData;
    }
    if (linePtr->nSymbolPts > 0) {
        int *indexPtr;
        register Point2D *symbolPts, *pointPtr;
        register int *symbolToData;
        int dataIndex;

        symbolPts = (Point2D *)ckalloc(linePtr->nSymbolPts * sizeof(Point2D));
        symbolToData = (int *)ckalloc(linePtr->nSymbolPts * sizeof(int));
        assert(symbolPts && symbolToData);
        pointPtr = symbolPts, indexPtr = symbolToData;
        for (linkPtr = Rbc_ChainFirstLink(linePtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->symbolPts = pointPtr;
            for (i = 0; i < linePtr->nSymbolPts; i++) {
                dataIndex = linePtr->symbolToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *pointPtr++ = linePtr->symbolPts[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->nSymbolPts = pointPtr - stylePtr->symbolPts;
        }
        ckfree((char *)linePtr->symbolPts);
        linePtr->symbolPts = symbolPts;
        ckfree((char *)linePtr->symbolToData);
        linePtr->symbolToData = symbolToData;
    }
    if (linePtr->xErrorBarCnt > 0) {
        Segment2D *xErrorBars, *segPtr;
        int *xErrorToData, *indexPtr;
        int dataIndex;

        xErrorBars = (Segment2D *)ckalloc(linePtr->xErrorBarCnt * sizeof(Segment2D));
        xErrorToData = (int *)ckalloc(linePtr->xErrorBarCnt * sizeof(int));
        assert(xErrorBars);
        segPtr = xErrorBars, indexPtr = xErrorToData;
        for (linkPtr = Rbc_ChainFirstLink(linePtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->xErrorBars = segPtr;
            for (i = 0; i < linePtr->xErrorBarCnt; i++) {
                dataIndex = linePtr->xErrorToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *segPtr++ = linePtr->xErrorBars[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->xErrorBarCnt = segPtr - stylePtr->xErrorBars;
        }
        ckfree((char *)linePtr->xErrorBars);
        linePtr->xErrorBars = xErrorBars;
        ckfree((char *)linePtr->xErrorToData);
        linePtr->xErrorToData = xErrorToData;
    }
    if (linePtr->yErrorBarCnt > 0) {
        Segment2D *errorBars, *segPtr;
        int *errorToData, *indexPtr;
        int dataIndex;

        errorBars = (Segment2D *)ckalloc(linePtr->yErrorBarCnt * sizeof(Segment2D));
        errorToData = (int *)ckalloc(linePtr->yErrorBarCnt * sizeof(int));
        assert(errorBars);
        segPtr = errorBars, indexPtr = errorToData;
        for (linkPtr = Rbc_ChainFirstLink(linePtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->yErrorBars = segPtr;
            for (i = 0; i < linePtr->yErrorBarCnt; i++) {
                dataIndex = linePtr->yErrorToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *segPtr++ = linePtr->yErrorBars[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->yErrorBarCnt = segPtr - stylePtr->yErrorBars;
        }
        ckfree((char *)linePtr->yErrorBars);
        linePtr->yErrorBars = errorBars;
        ckfree((char *)linePtr->yErrorToData);
        linePtr->yErrorToData = errorToData;
    }
}

#define CLIP_TOP (1 << 0)
#define CLIP_BOTTOM (1 << 1)
#define CLIP_RIGHT (1 << 2)
#define CLIP_LEFT (1 << 3)

/*
 *----------------------------------------------------------------------
 *
 * OutCode --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Extents2D *extsPtr
 *      Point2D *p
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
INLINE static int OutCode(Extents2D *extsPtr, Point2D *p) {
    int code;

    code = 0;
    if (p->x > extsPtr->right) {
        code |= CLIP_RIGHT;
    } else if (p->x < extsPtr->left) {
        code |= CLIP_LEFT;
    }
    if (p->y > extsPtr->bottom) {
        code |= CLIP_BOTTOM;
    } else if (p->y < extsPtr->top) {
        code |= CLIP_TOP;
    }
    return code;
}

/*
 *----------------------------------------------------------------------
 *
 * ClipSegment --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Extents2D *extsPtr
 *      register int code1
 *      register int code2
 *      register Point2D *p
 *      register Point2D *q
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ClipSegment(Extents2D *extsPtr, register int code1, register int code2, register Point2D *p,
                       register Point2D *q) {
    int inside, outside;

    inside = ((code1 | code2) == 0);
    outside = ((code1 & code2) != 0);

    /*
     * In the worst case, we'll clip the line segment against each of
     * the four sides of the bounding rectangle.
     */
    while ((!outside) && (!inside)) {
        if (code1 == 0) {
            Point2D *tmp;
            int code;

            /* Swap pointers and out codes */
            tmp = p, p = q, q = tmp;
            code = code1, code1 = code2, code2 = code;
        }
        if (code1 & CLIP_LEFT) {
            p->y += (q->y - p->y) * (extsPtr->left - p->x) / (q->x - p->x);
            p->x = extsPtr->left;
        } else if (code1 & CLIP_RIGHT) {
            p->y += (q->y - p->y) * (extsPtr->right - p->x) / (q->x - p->x);
            p->x = extsPtr->right;
        } else if (code1 & CLIP_BOTTOM) {
            p->x += (q->x - p->x) * (extsPtr->bottom - p->y) / (q->y - p->y);
            p->y = extsPtr->bottom;
        } else if (code1 & CLIP_TOP) {
            p->x += (q->x - p->x) * (extsPtr->top - p->y) / (q->y - p->y);
            p->y = extsPtr->top;
        }
        code1 = OutCode(extsPtr, p);

        inside = ((code1 | code2) == 0);
        outside = ((code1 & code2) != 0);
    }
    return (!inside);
}

/*
 *----------------------------------------------------------------------
 *
 * SaveTrace --
 *
 *      Creates a new trace and inserts it into the line's
 *      list of traces.
 *
 * Parameters:
 *      Line *linePtr
 *      int start - Starting index of the trace in data point array. Used to figure out closest point
 *      int length - Number of points forming the trace
 *      MapInfo *mapPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void SaveTrace(Line *linePtr, int start, int length, MapInfo *mapPtr) {
    LineTrace *tracePtr;
    Point2D *screenPts;
    int *indices;
    register int i, j;

    tracePtr = (LineTrace *)ckalloc(sizeof(LineTrace));
    assert(tracePtr);
    screenPts = (Point2D *)ckalloc(sizeof(Point2D) * length);
    assert(screenPts);
    indices = (int *)ckalloc(sizeof(int) * length);
    assert(indices);

    /* Copy the screen coordinates of the trace into the point array */

    if (mapPtr->indices != NULL) {
        for (i = 0, j = start; i < length; i++, j++) {
            screenPts[i].x = mapPtr->screenPts[j].x;
            screenPts[i].y = mapPtr->screenPts[j].y;
            indices[i] = mapPtr->indices[j];
        }
    } else {
        for (i = 0, j = start; i < length; i++, j++) {
            screenPts[i].x = mapPtr->screenPts[j].x;
            screenPts[i].y = mapPtr->screenPts[j].y;
            indices[i] = j;
        }
    }
    tracePtr->nScreenPts = length;
    tracePtr->screenPts = screenPts;
    tracePtr->symbolToData = indices;
    tracePtr->start = start;
    if (linePtr->traces == NULL) {
        linePtr->traces = Rbc_ChainCreate();
    }
    Rbc_ChainAppend(linePtr->traces, tracePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * FreeTraces --
 *
 *      Deletes all the traces for the line.
 *
 * Parameters:
 *      Line *linePtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void FreeTraces(Line *linePtr) {
    Rbc_ChainLink *linkPtr;
    LineTrace *tracePtr;

    for (linkPtr = Rbc_ChainFirstLink(linePtr->traces); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        tracePtr = Rbc_ChainGetValue(linkPtr);
        ckfree((char *)tracePtr->symbolToData);
        ckfree((char *)tracePtr->screenPts);
        ckfree((char *)tracePtr);
    }
    Rbc_ChainDestroy(linePtr->traces);
    linePtr->traces = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * MapTraces --
 *
 *      Creates an array of line segments of the graph coordinates.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Line *linePtr
 *      MapInfo *mapPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is  allocated for the line segment array.
 *
 *----------------------------------------------------------------------
 */
static void MapTraces(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    int start, count;
    int code1, code2;
    Point2D *p, *q;
    Point2D s;
    Extents2D exts;
    register int i;
    int broken, offscreen;

    Rbc_GraphExtents(graphPtr, &exts);
    count = 1;
    code1 = OutCode(&exts, mapPtr->screenPts);
    p = mapPtr->screenPts;
    q = p + 1;
    for (i = 1; i < mapPtr->nScreenPts; i++, p++, q++) {
        code2 = OutCode(&exts, q);
        if (code2 != 0) {
            /* Save the coordinates of the last point, before clipping */
            s = *q;
        }
        broken = BROKEN_TRACE(linePtr->penDir, p->x, q->x);
        offscreen = ClipSegment(&exts, code1, code2, p, q);
        if (broken || offscreen) {

            /*
             * The last line segment is either totally clipped by the plotting
             * area or the x-direction is wrong, breaking the trace.  Either
             * way, save information about the last trace (if one exists),
             * discarding the current line segment
             */

            if (count > 1) {
                start = i - count;
                SaveTrace(linePtr, start, count, mapPtr);
                count = 1;
            }
        } else {
            count++; /* Add the point to the trace. */
            if (code2 != 0) {

                /*
                 * If the last point is clipped, this means that the trace is
                 * broken after this point.  Restore the original coordinate
                 * (before clipping) after saving the trace.
                 */

                start = i - (count - 1);
                SaveTrace(linePtr, start, count, mapPtr);
                mapPtr->screenPts[i] = s;
                count = 1;
            }
        }
        code1 = code2;
    }
    if (count > 1) {
        start = i - count;
        SaveTrace(linePtr, start, count, mapPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MapFillArea --
 *
 *      Creates an array of points that represent a polygon that fills
 *      the area under the element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Line *linePtr
 *      MapInfo *mapPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is  allocated for the polygon point array.
 *
 *----------------------------------------------------------------------
 */
static void MapFillArea(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    Point2D *origPts, *clipPts;
    Extents2D exts;
    double maxY;
    register int i, n;

    if (linePtr->fillPts != NULL) {
        ckfree((char *)linePtr->fillPts);
        linePtr->fillPts = NULL;
        linePtr->nFillPts = 0;
    }
    if (mapPtr->nScreenPts < 3) {
        return;
    }
    n = mapPtr->nScreenPts + 3;
    Rbc_GraphExtents(graphPtr, &exts);

    maxY = (double)graphPtr->bottom;
    origPts = (Point2D *)ckalloc(sizeof(Point2D) * n);
    for (i = 0; i < mapPtr->nScreenPts; i++) {
        origPts[i].x = mapPtr->screenPts[i].x + 1;
        origPts[i].y = mapPtr->screenPts[i].y;
        if (origPts[i].y > maxY) {
            maxY = origPts[i].y;
        }
    }
    /* Add edges to make (if necessary) the polygon fill to the bottom
     * of plotting window */
    origPts[i].x = origPts[i - 1].x;
    origPts[i].y = maxY;
    i++;
    origPts[i].x = origPts[0].x;
    origPts[i].y = maxY;
    i++;
    origPts[i] = origPts[0];

    clipPts = (Point2D *)ckalloc(sizeof(Point2D) * n * 3);
    assert(clipPts);
    n = Rbc_PolyRectClip(&exts, origPts, n - 1, clipPts);

    ckfree((char *)origPts);
    if (n < 3) {
        ckfree((char *)clipPts);
    } else {
        linePtr->fillPts = clipPts;
        linePtr->nFillPts = n;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ResetLine --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Line *linePtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void ResetLine(Line *linePtr) {
    FreeTraces(linePtr);
    ClearPalette(linePtr->palette);
    if (linePtr->symbolPts != NULL) {
        ckfree((char *)linePtr->symbolPts);
    }
    if (linePtr->symbolToData != NULL) {
        ckfree((char *)linePtr->symbolToData);
    }
    if (linePtr->strips != NULL) {
        ckfree((char *)linePtr->strips);
    }
    if (linePtr->stripToData != NULL) {
        ckfree((char *)linePtr->stripToData);
    }
    if (linePtr->activePts != NULL) {
        ckfree((char *)linePtr->activePts);
    }
    if (linePtr->activeToData != NULL) {
        ckfree((char *)linePtr->activeToData);
    }
    if (linePtr->xErrorBars != NULL) {
        ckfree((char *)linePtr->xErrorBars);
    }
    if (linePtr->xErrorToData != NULL) {
        ckfree((char *)linePtr->xErrorToData);
    }
    if (linePtr->yErrorBars != NULL) {
        ckfree((char *)linePtr->yErrorBars);
    }
    if (linePtr->yErrorToData != NULL) {
        ckfree((char *)linePtr->yErrorToData);
    }
    linePtr->xErrorBars = linePtr->yErrorBars = linePtr->strips = NULL;
    linePtr->symbolPts = linePtr->activePts = NULL;
    linePtr->stripToData = linePtr->symbolToData = linePtr->xErrorToData = linePtr->yErrorToData =
        linePtr->activeToData = NULL;
    linePtr->nActivePts = linePtr->nSymbolPts = linePtr->nStrips = linePtr->xErrorBarCnt = linePtr->yErrorBarCnt = 0;
}

/*
 *----------------------------------------------------------------------
 *
 * MapLine --
 *
 *      Calculates the actual window coordinates of the line element.
 *      The window coordinates are saved in an allocated point array.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      Element *elemPtr - Element component record
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is (re)allocated for the point array.
 *
 *----------------------------------------------------------------------
 */
static void MapLine(Graph *graphPtr, Element *elemPtr) {
    Line *linePtr = (Line *)elemPtr;
    MapInfo mapInfo;
    int size, nPoints;
    PenStyle **dataToStyle;
    Rbc_ChainLink *linkPtr;
    LinePenStyle *stylePtr;

    ResetLine(linePtr);
    nPoints = NumberOfPoints(linePtr);
    if (nPoints < 1) {
        return; /* No data points */
    }
    GetScreenPoints(graphPtr, linePtr, &mapInfo);
    MapSymbols(graphPtr, linePtr, &mapInfo);

    if ((linePtr->flags & ACTIVE_PENDING) && (linePtr->nActiveIndices > 0)) {
        MapActiveSymbols(graphPtr, linePtr);
    }
    /*
     * Map connecting line segments if they are to be displayed.
     */
    if ((nPoints > 1) && ((graphPtr->classUid == rbcStripElementUid) || (linePtr->builtinPen.traceWidth > 0))) {
        linePtr->smooth = linePtr->reqSmooth;

        /*
         * Do smoothing if necessary.  This can extend the coordinate array,
         * so both mapInfo.points and mapInfo.nPoints may change.
         */

        switch (linePtr->smooth) {
        case PEN_SMOOTH_STEP:
            GenerateSteps(&mapInfo);
            break;

        case PEN_SMOOTH_NATURAL:
        case PEN_SMOOTH_QUADRATIC:
            if (mapInfo.nScreenPts < 3) {
                /* Can't interpolate with less than three points. */
                linePtr->smooth = PEN_SMOOTH_NONE;
            } else {
                GenerateSpline(graphPtr, linePtr, &mapInfo);
            }
            break;

        case PEN_SMOOTH_CATROM:
            if (mapInfo.nScreenPts < 3) {
                /* Can't interpolate with less than three points. */
                linePtr->smooth = PEN_SMOOTH_NONE;
            } else {
                GenerateParametricSpline(graphPtr, linePtr, &mapInfo);
            }
            break;

        default:
            break;
        }
        if (linePtr->rTolerance > 0.0) {
            ReducePoints(&mapInfo, linePtr->rTolerance);
        }
        if ((linePtr->fillTile != NULL) || (linePtr->fillStipple != None)) {
            MapFillArea(graphPtr, linePtr, &mapInfo);
        }
        if (graphPtr->classUid == rbcStripElementUid) {
            MapStrip(graphPtr, linePtr, &mapInfo);
        } else {
            MapTraces(graphPtr, linePtr, &mapInfo);
        }
    }
    ckfree((char *)mapInfo.screenPts);
    ckfree((char *)mapInfo.indices);

    /* Set the symbol size of all the pen styles. */
    for (linkPtr = Rbc_ChainFirstLink(linePtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        size = ScaleSymbol(elemPtr, stylePtr->penPtr->symbol.size);
        stylePtr->symbolSize = size;
        stylePtr->errorBarCapWidth =
            (stylePtr->penPtr->errorBarCapWidth > 0) ? stylePtr->penPtr->errorBarCapWidth : (int)(size * 0.6666666);
        stylePtr->errorBarCapWidth /= 2;
    }
    dataToStyle = Rbc_StyleMap((Element *)linePtr);
    if (((linePtr->yHigh.nValues > 0) && (linePtr->yLow.nValues > 0)) ||
        ((linePtr->xHigh.nValues > 0) && (linePtr->xLow.nValues > 0)) || (linePtr->xError.nValues > 0) ||
        (linePtr->yError.nValues > 0)) {
        Rbc_MapErrorBars(graphPtr, (Element *)linePtr, dataToStyle);
    }
    MergePens(linePtr, dataToStyle);
    ckfree((char *)dataToStyle);
}

/*
 *----------------------------------------------------------------------
 *
 * DistanceToLine --
 *
 *      TODO: Description
 *
 * Parameters:
 *      int x - Sample X coordinate.
 *      int y - Sample Y coordinate.
 *      Point2D *p - End point of the line segment.
 *      Point2D *q - End point of the line segment.
 *      Point2D *t - (out) Point on line segment.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static double DistanceToLine(int x, int y, Point2D *p, Point2D *q, Point2D *t) {
    double right, left, top, bottom;

    *t = Rbc_GetProjection(x, y, p, q);
    if (p->x > q->x) {
        right = p->x, left = q->x;
    } else {
        left = p->x, right = q->x;
    }
    if (p->y > q->y) {
        bottom = p->y, top = q->y;
    } else {
        top = p->y, bottom = q->y;
    }
    if (t->x > right) {
        t->x = right;
    } else if (t->x < left) {
        t->x = left;
    }
    if (t->y > bottom) {
        t->y = bottom;
    } else if (t->y < top) {
        t->y = top;
    }
    return hypot((t->x - x), (t->y - y));
}

/*
 *----------------------------------------------------------------------
 *
 * DistanceToX --
 *
 *      TODO: Description
 *
 * Parameters:
 *      int x - Sample X coordinate.
 *      int y - Sample Y coordinate.
 *      Point2D *p - End point of the line segment.
 *      Point2D *q - End point of the line segment.
 *      Point2D *t - (out) Point on line segment.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static double DistanceToX(int x, int y, Point2D *p, Point2D *q, Point2D *t) {
    double dx, dy;
    double dist;

    if (p->x > q->x) {
        if ((x > p->x) || (x < q->x)) {
            return DBL_MAX; /* X-coordinate outside line segment. */
        }
    } else {
        if ((x > q->x) || (x < p->x)) {
            return DBL_MAX; /* X-coordinate outside line segment. */
        }
    }
    dx = p->x - q->x;
    dy = p->y - q->y;
    t->x = (double)x;
    if (FABS(dx) < DBL_EPSILON) {
        double d1, d2;
        /* Same X-coordinate indicates a vertical line.  Pick the
         * closest end point. */
        d1 = p->y - y;
        d2 = q->y - y;
        if (FABS(d1) < FABS(d2)) {
            t->y = p->y, dist = d1;
        } else {
            t->y = q->y, dist = d2;
        }
    } else if (FABS(dy) < DBL_EPSILON) {
        /* Horizontal line. */
        t->y = p->y, dist = p->y - y;
    } else {
        double m, b;

        m = dy / dx;
        b = p->y - (m * p->x);
        t->y = (x * m) + b;
        dist = y - t->y;
    }
    return FABS(dist);
}

/*
 *----------------------------------------------------------------------
 *
 * DistanceToY --
 *
 *      TODO: Description
 *
 * Parameters:
 *      int x - Sample X coordinate.
 *      int y - Sample Y coordinate.
 *      Point2D *p - End point of the line segment.
 *      Point2D *q - End point of the line segment.
 *      Point2D *t - (out) Point on line segment.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static double DistanceToY(int x, int y, Point2D *p, Point2D *q, Point2D *t) {
    double dx, dy;
    double dist;

    if (p->y > q->y) {
        if ((y > p->y) || (y < q->y)) {
            return DBL_MAX;
        }
    } else {
        if ((y > q->y) || (y < p->y)) {
            return DBL_MAX;
        }
    }
    dx = p->x - q->x;
    dy = p->y - q->y;
    t->y = y;
    if (FABS(dy) < DBL_EPSILON) {
        double d1, d2;

        /* Save Y-coordinate indicates an horizontal line. Pick the
         * closest end point. */
        d1 = p->x - x;
        d2 = q->x - x;
        if (FABS(d1) < FABS(d2)) {
            t->x = p->x, dist = d1;
        } else {
            t->x = q->x, dist = d2;
        }
    } else if (FABS(dx) < DBL_EPSILON) {
        /* Vertical line. */
        t->x = p->x, dist = p->x - x;
    } else {
        double m, b;

        m = dy / dx;
        b = p->y - (m * p->x);
        t->x = (y - b) / m;
        dist = x - t->x;
    }
    return FABS(dist);
}

/*
 *----------------------------------------------------------------------
 *
 * ClosestTrace --
 *
 *      Find the line segment closest to the given window coordinate
 *      in the element.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      Line *linePtr - Line element record
 *      ClosestSearch *searchPtr - Info about closest point in element
 *      DistanceProc *distProc
 *
 * Results:
 *      If a new minimum distance is found, the information regarding
 *      it is returned via searchPtr.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ClosestTrace(Graph *graphPtr, Line *linePtr, ClosestSearch *searchPtr, DistanceProc *distProc) {
    Rbc_ChainLink *linkPtr;
    Point2D closest, b;
    LineTrace *tracePtr;
    double dist, minDist;
    register Point2D *pointPtr, *endPtr;
    int i;

    i = -1;
    closest.x = closest.y = 0; /* Suppress compiler warning. */
    minDist = searchPtr->dist;
    for (linkPtr = Rbc_ChainFirstLink(linePtr->traces); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        tracePtr = Rbc_ChainGetValue(linkPtr);
        for (pointPtr = tracePtr->screenPts, endPtr = tracePtr->screenPts + (tracePtr->nScreenPts - 1);
             pointPtr < endPtr; pointPtr++) {
            dist = (*distProc)(searchPtr->x, searchPtr->y, pointPtr, pointPtr + 1, &b);
            if (dist < minDist) {
                closest = b;
                i = tracePtr->symbolToData[pointPtr - tracePtr->screenPts];
                minDist = dist;
            }
        }
    }
    if (minDist < searchPtr->dist) {
        searchPtr->dist = minDist;
        searchPtr->elemPtr = (Element *)linePtr;
        searchPtr->index = i;
        searchPtr->point = Rbc_InvMap2D(graphPtr, closest.x, closest.y, &(linePtr->axes));
        return TRUE;
    }
    return FALSE;
}

/*
 *----------------------------------------------------------------------
 *
 * ClosestStrip --
 *
 *      Find the line segment closest to the given window coordinate
 *      in the element.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      Line *linePtr - Line element record
 *      ClosestSearch *searchPtr - Info about closest point in element
 *      DistanceProc *distProc
 *
 * Results:
 *      If a new minimum distance is found, the information regarding
 *      it is returned via searchPtr.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ClosestStrip(Graph *graphPtr, Line *linePtr, ClosestSearch *searchPtr, DistanceProc *distProc) {
    Point2D closest, b;
    double dist, minDist;
    int count;
    int i;
    register Segment2D *s;

    i = 0;
    closest.x = closest.y = 0; /* compiler warning */
    minDist = searchPtr->dist;
    s = linePtr->strips;
    for (count = 0; count < linePtr->nStrips; count++, s++) {
        dist = (*distProc)(searchPtr->x, searchPtr->y, &(s->p), &(s->q), &b);
        if (dist < minDist) {
            closest = b;
            i = linePtr->stripToData[count];
            minDist = dist;
        }
    }
    if (minDist < searchPtr->dist) {
        searchPtr->dist = minDist;
        searchPtr->elemPtr = (Element *)linePtr;
        searchPtr->index = i;
        searchPtr->point = Rbc_InvMap2D(graphPtr, closest.x, closest.y, &(linePtr->axes));
        return TRUE;
    }
    return FALSE;
}

/*
 *----------------------------------------------------------------------
 *
 * ClosestPoint --
 *
 *      Find the element whose data point is closest to the given screen
 *      coordinate.
 *
 * Parameters:
 *      Line *linePtr - Line element that we are looking at
 *      ClosestSearch *searchPtr - Assorted information related to searching for the closest point
 *
 * Results:
 *      If a new minimum distance is found, the information regarding
 *      it is returned via searchPtr.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void ClosestPoint(Line *linePtr, ClosestSearch *searchPtr) {
    double dist, minDist;
    double dx, dy;
    int count, i;
    register Point2D *pointPtr;

    minDist = searchPtr->dist;
    i = 0;

    /*
     * Instead of testing each data point in graph coordinates, look at
     * the array of mapped screen coordinates. The advantages are
     *   1) only examine points that are visible (unclipped), and
     *   2) the computed distance is already in screen coordinates.
     */
    pointPtr = linePtr->symbolPts;
    for (count = 0; count < linePtr->nSymbolPts; count++, pointPtr++) {
        dx = (double)(searchPtr->x - pointPtr->x);
        dy = (double)(searchPtr->y - pointPtr->y);
        if (searchPtr->along == SEARCH_BOTH) {
            dist = hypot(dx, dy);
        } else if (searchPtr->along == SEARCH_X) {
            dist = dx;
        } else if (searchPtr->along == SEARCH_Y) {
            dist = dy;
        } else {
            /* This can't happen */
            continue;
        }
        if (dist < minDist) {
            i = linePtr->symbolToData[count];
            minDist = dist;
        }
    }
    if (minDist < searchPtr->dist) {
        searchPtr->elemPtr = (Element *)linePtr;
        searchPtr->dist = minDist;
        searchPtr->index = i;
        searchPtr->point.x = linePtr->x.valueArr[i];
        searchPtr->point.y = linePtr->y.valueArr[i];
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetLineExtents --
 *
 *      Retrieves the range of the line element
 *
 * Parameters:
 *      Element *elemPtr
 *      Extents2D *extsPtr
 *
 * Results:
 *      Returns the number of data points in the element.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void GetLineExtents(Element *elemPtr, Extents2D *extsPtr) {
    int nPoints;

    extsPtr->top = extsPtr->left = DBL_MAX;
    extsPtr->bottom = extsPtr->right = -DBL_MAX;

    nPoints = NumberOfPoints(elemPtr);
    if (nPoints < 1) {
        return;
    }
    extsPtr->right = elemPtr->x.max;
    if ((elemPtr->x.min <= 0.0) && (elemPtr->axes.x->logScale)) {
        extsPtr->left = Rbc_FindElemVectorMinimum(&elemPtr->x, DBL_MIN);
    } else {
        extsPtr->left = elemPtr->x.min;
    }
    extsPtr->bottom = elemPtr->y.max;
    if ((elemPtr->y.min <= 0.0) && (elemPtr->axes.y->logScale)) {
        extsPtr->top = Rbc_FindElemVectorMinimum(&elemPtr->y, DBL_MIN);
    } else {
        extsPtr->top = elemPtr->y.min;
    }

    /* Correct the data limits for error bars */

    if (elemPtr->xError.nValues > 0) {
        register int i;
        double x;

        nPoints = MIN(elemPtr->xError.nValues, nPoints);
        for (i = 0; i < nPoints; i++) {
            x = elemPtr->x.valueArr[i] + elemPtr->xError.valueArr[i];
            if (x > extsPtr->right) {
                extsPtr->right = x;
            }
            x = elemPtr->x.valueArr[i] - elemPtr->xError.valueArr[i];
            if (elemPtr->axes.x->logScale) {
                if (x < 0.0) {
                    x = -x; /* Mirror negative values, instead
                             * of ignoring them. */
                }
                if ((x > DBL_MIN) && (x < extsPtr->left)) {
                    extsPtr->left = x;
                }
            } else if (x < extsPtr->left) {
                extsPtr->left = x;
            }
        }
    } else {
        if ((elemPtr->xHigh.nValues > 0) && (elemPtr->xHigh.max > extsPtr->right)) {
            extsPtr->right = elemPtr->xHigh.max;
        }
        if (elemPtr->xLow.nValues > 0) {
            double left;

            if ((elemPtr->xLow.min <= 0.0) && (elemPtr->axes.x->logScale)) {
                left = Rbc_FindElemVectorMinimum(&elemPtr->xLow, DBL_MIN);
            } else {
                left = elemPtr->xLow.min;
            }
            if (left < extsPtr->left) {
                extsPtr->left = left;
            }
        }
    }

    if (elemPtr->yError.nValues > 0) {
        register int i;
        double y;

        nPoints = MIN(elemPtr->yError.nValues, nPoints);
        for (i = 0; i < nPoints; i++) {
            y = elemPtr->y.valueArr[i] + elemPtr->yError.valueArr[i];
            if (y > extsPtr->bottom) {
                extsPtr->bottom = y;
            }
            y = elemPtr->y.valueArr[i] - elemPtr->yError.valueArr[i];
            if (elemPtr->axes.y->logScale) {
                if (y < 0.0) {
                    y = -y; /* Mirror negative values, instead
                             * of ignoring them. */
                }
                if ((y > DBL_MIN) && (y < extsPtr->left)) {
                    extsPtr->top = y;
                }
            } else if (y < extsPtr->top) {
                extsPtr->top = y;
            }
        }
    } else {
        if ((elemPtr->yHigh.nValues > 0) && (elemPtr->yHigh.max > extsPtr->bottom)) {
            extsPtr->bottom = elemPtr->yHigh.max;
        }
        if (elemPtr->yLow.nValues > 0) {
            double top;

            if ((elemPtr->yLow.min <= 0.0) && (elemPtr->axes.y->logScale)) {
                top = Rbc_FindElemVectorMinimum(&elemPtr->yLow, DBL_MIN);
            } else {
                top = elemPtr->yLow.min;
            }
            if (top < extsPtr->top) {
                extsPtr->top = top;
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TileChangedProc
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
    Line *linePtr = clientData;
    Graph *graphPtr;

    graphPtr = linePtr->graphPtr;
    if (graphPtr->tkwin != NULL) {
        graphPtr->flags |= REDRAW_WORLD;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureLine --
 *
 *      Sets up the appropriate configuration parameters in the GC.
 *      It is assumed the parameters have been previously set by
 *      a call to Tk_ConfigureWidget.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Element *elemPtr
 *
 * Results:
 *      The return value is a standard Tcl result.  If TCL_ERROR is
 *      returned, then the interpreter result contains an error message.
 *
 * Side effects:
 *      Configuration information such as line width, line style, color
 *      etc. get set in a new GC.
 *
 *----------------------------------------------------------------------
 */
static int ConfigureLine(Graph *graphPtr, Element *elemPtr) {
    Line *linePtr = (Line *)elemPtr;
    unsigned long gcMask;
    XGCValues gcValues;
    GC newGC;
    Rbc_ChainLink *linkPtr;

    if (ConfigurePen(graphPtr, (Pen *)&(linePtr->builtinPen)) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Point to the static normal/active pens if no external pens have
     * been selected.
     */
    if (linePtr->normalPenPtr == NULL) {
        linePtr->normalPenPtr = &(linePtr->builtinPen);
    }
    linkPtr = Rbc_ChainFirstLink(linePtr->palette);
    if (linkPtr != NULL) {
        LinePenStyle *stylePtr;

        stylePtr = Rbc_ChainGetValue(linkPtr);
        stylePtr->penPtr = linePtr->normalPenPtr;
    }
    if (linePtr->fillTile != NULL) {
        Rbc_SetTileChangedProc(linePtr->fillTile, TileChangedProc, linePtr);
    }
    /*
     * Set the outline GC for this pen: GCForeground is outline color.
     * GCBackground is the fill color (only used for bitmap symbols).
     */
    gcMask = 0;
    if (linePtr->fillFgColor != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = linePtr->fillFgColor->pixel;
    }
    if (linePtr->fillBgColor != NULL) {
        gcMask |= GCBackground;
        gcValues.background = linePtr->fillBgColor->pixel;
    }
    if ((linePtr->fillStipple != None) && (linePtr->fillStipple != PATTERN_SOLID)) {
        gcMask |= (GCStipple | GCFillStyle);
        gcValues.stipple = linePtr->fillStipple;
        gcValues.fill_style = (linePtr->fillBgColor == NULL) ? FillStippled : FillOpaqueStippled;
    }
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (linePtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, linePtr->fillGC);
    }
    linePtr->fillGC = newGC;

    if (Rbc_ConfigModified(graphPtr->interp, linePtr->configSpecs, "-scalesymbols", (char *)NULL)) {
        linePtr->flags |= (MAP_ITEM | SCALE_SYMBOL);
    }
    if (Rbc_ConfigModified(graphPtr->interp, linePtr->configSpecs, "-pixels", "-trace", "-*data", "-smooth", "-map*",
                           "-label", "-hide", "-x", "-y", "-areapattern", (char *)NULL)) {
        linePtr->flags |= MAP_ITEM;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * ClosestLine --
 *
 *      Find the closest point or line segment (if interpolated) to
 *      the given window coordinate in the line element.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      Element *elemPtr - Element to examine
 *      ClosestSearch *searchPtr - Info about closest point in element
 *
 * Results:
 *      Returns the distance of the closest point among other
 *      information.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void ClosestLine(Graph *graphPtr, Element *elemPtr, ClosestSearch *searchPtr) {
    Line *linePtr = (Line *)elemPtr;
    int mode;

    mode = searchPtr->mode;
    if (mode == SEARCH_AUTO) {
        LinePen *penPtr = linePtr->normalPenPtr;

        mode = SEARCH_POINTS;
        if ((NumberOfPoints(linePtr) > 1) && (penPtr->traceWidth > 0)) {
            mode = SEARCH_TRACES;
        }
    }
    if (mode == SEARCH_POINTS) {
        ClosestPoint(linePtr, searchPtr);
    } else {
        DistanceProc *distProc;
        int found;

        if (searchPtr->along == SEARCH_X) {
            distProc = DistanceToX;
        } else if (searchPtr->along == SEARCH_Y) {
            distProc = DistanceToY;
        } else {
            distProc = DistanceToLine;
        }
        if (elemPtr->classUid == rbcStripElementUid) {
            found = ClosestStrip(graphPtr, linePtr, searchPtr, distProc);
        } else {
            found = ClosestTrace(graphPtr, linePtr, searchPtr, distProc);
        }
        if ((!found) && (searchPtr->along != SEARCH_BOTH)) {
            ClosestPoint(linePtr, searchPtr);
        }
    }
}

/*
 * XDrawLines() points: XMaxRequestSize(dpy) - 3
 * XFillPolygon() points:  XMaxRequestSize(dpy) - 4
 * XDrawSegments() segments:  (XMaxRequestSize(dpy) - 3) / 2
 * XDrawRectangles() rectangles:  (XMaxRequestSize(dpy) - 3) / 2
 * XFillRectangles() rectangles:  (XMaxRequestSize(dpy) - 3) / 2
 * XDrawArcs() or XFillArcs() arcs:  (XMaxRequestSize(dpy) - 3) / 3
 */

#define MAX_DRAWLINES(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWPOLYGON(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWSEGMENTS(d) Rbc_MaxRequestSize(d, sizeof(XSegment))
#define MAX_DRAWRECTANGLES(d) Rbc_MaxRequestSize(d, sizeof(XRectangle))
#define MAX_DRAWARCS(d) Rbc_MaxRequestSize(d, sizeof(XArc))

#ifdef WIN32

/*
 *----------------------------------------------------------------------
 *
 * DrawCircles --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      Line *linePtr
 *      LinePen *penPtr
 *      int nSymbolPts
 *      Point2D *symbolPts
 *      int radius
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DrawCircles(Display *display, Drawable drawable, Line *linePtr, LinePen *penPtr, int nSymbolPts,
                        Point2D *symbolPts, int radius) {
    HBRUSH brush, oldBrush;
    HPEN pen, oldPen;
    HDC dc;
    TkWinDCState state;
    register Point2D *pointPtr, *endPtr;

    if (drawable == None) {
        return; /* Huh? */
    }
    if ((penPtr->symbol.fillGC == NULL) && (penPtr->symbol.outlineWidth == 0)) {
        return;
    }
    dc = TkWinGetDrawableDC(display, drawable, &state);
    /* SetROP2(dc, tkpWinRopModes[penPtr->symbol.fillGC->function]); */
    if (penPtr->symbol.fillGC != NULL) {
        brush = CreateSolidBrush(penPtr->symbol.fillGC->foreground);
    } else {
        brush = GetStockBrush(NULL_BRUSH);
    }
    if (penPtr->symbol.outlineWidth > 0) {
        pen = Rbc_GCToPen(dc, penPtr->symbol.outlineGC);
    } else {
        pen = GetStockPen(NULL_PEN);
    }
    oldPen = SelectPen(dc, pen);
    oldBrush = SelectBrush(dc, brush);
    for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
        Ellipse(dc, (int)pointPtr->x - radius, (int)pointPtr->y - radius, (int)pointPtr->x + radius + 1,
                (int)pointPtr->y + radius + 1);
    }
    DeleteBrush(SelectBrush(dc, oldBrush));
    DeletePen(SelectPen(dc, oldPen));
    TkWinReleaseDrawableDC(drawable, dc, &state);
}

#else

/*
 *----------------------------------------------------------------------
 *
 * DrawCircles --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      Line *linePtr
 *      LinePen *penPtr
 *      int nSymbolPts
 *      Point2D *symbolPts
 *      int radius
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DrawCircles(Display *display, Drawable drawable, Line *linePtr, LinePen *penPtr, int nSymbolPts,
                        Point2D *symbolPts, int radius) {
    register int i;
    XArc *arcArr; /* Array of arcs (circle) */
    register XArc *arcPtr;
    int reqSize, nArcs;
    int s;
    int count;
    register Point2D *pointPtr, *endPtr;

    s = radius + radius;
    arcArr = (XArc *)ckalloc(nSymbolPts * sizeof(XArc));
    arcPtr = arcArr;

    if (linePtr->symbolInterval > 0) {
        count = 0;
        for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
            if (DRAW_SYMBOL(linePtr)) {
                arcPtr->x = (short int)pointPtr->x - radius;
                arcPtr->y = (short int)pointPtr->y - radius;
                arcPtr->width = arcPtr->height = (unsigned short)s;
                arcPtr->angle1 = 0;
                arcPtr->angle2 = 23040;
                arcPtr++, count++;
            }
            linePtr->symbolCounter++;
        }
    } else {
        count = nSymbolPts;
        for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
            arcPtr->x = (short int)pointPtr->x - radius;
            arcPtr->y = (short int)pointPtr->y - radius;
            arcPtr->width = arcPtr->height = (unsigned short)s;
            arcPtr->angle1 = 0;
            arcPtr->angle2 = 23040;
            arcPtr++;
        }
    }
    reqSize = MAX_DRAWARCS(display);
    for (i = 0; i < count; i += reqSize) {
        nArcs = ((i + reqSize) > count) ? (count - i) : reqSize;
        if (penPtr->symbol.fillGC != NULL) {
            XFillArcs(display, drawable, penPtr->symbol.fillGC, arcArr + i, nArcs);
        }
        if (penPtr->symbol.outlineWidth > 0) {
            XDrawArcs(display, drawable, penPtr->symbol.outlineGC, arcArr + i, nArcs);
        }
    }
    ckfree((char *)arcArr);
}

#endif

/*
 *----------------------------------------------------------------------
 *
 * DrawSquares --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      Line *linePtr
 *      LinePen *penPtr
 *      int nSymbolPts
 *      register Point2D *symbolPts
 *      int r
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DrawSquares(Display *display, Drawable drawable, Line *linePtr, LinePen *penPtr, int nSymbolPts,
                        register Point2D *symbolPts, int r) {
    XRectangle *rectArr;
    register Point2D *pointPtr, *endPtr;
    register XRectangle *rectPtr;
    int reqSize, nRects;
    int s;
    register int i;
    int count;

    s = r + r;
    rectArr = (XRectangle *)ckalloc(nSymbolPts * sizeof(XRectangle));
    rectPtr = rectArr;

    if (linePtr->symbolInterval > 0) {
        count = 0;
        for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
            if (DRAW_SYMBOL(linePtr)) {
                rectPtr->x = (short int)(pointPtr->x - r);
                rectPtr->y = (short int)(pointPtr->y - r);
                rectPtr->width = rectPtr->height = (unsigned short)s;
                rectPtr++, count++;
            }
            linePtr->symbolCounter++;
        }
    } else {
        count = nSymbolPts;
        for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
            rectPtr->x = (short int)(pointPtr->x - r);
            rectPtr->y = (short int)(pointPtr->y - r);
            rectPtr->width = rectPtr->height = (unsigned short)s;
            rectPtr++;
        }
    }
    reqSize = MAX_DRAWRECTANGLES(display);
    for (i = 0; i < count; i += reqSize) {
        nRects = ((i + reqSize) > count) ? (count - i) : reqSize;
        if (penPtr->symbol.fillGC != NULL) {
            XFillRectangles(display, drawable, penPtr->symbol.fillGC, rectArr + i, nRects);
        }
        if (penPtr->symbol.outlineWidth > 0) {
            XDrawRectangles(display, drawable, penPtr->symbol.outlineGC, rectArr + i, nRects);
        }
    }
    ckfree((char *)rectArr);
}

/*
 * -----------------------------------------------------------------
 *
 * DrawSymbols --
 *
 *      Draw the symbols centered at the each given x,y coordinate
 *      in the array of points.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      Drawable drawable - Pixmap or window to draw into
 *      Line *linePtr
 *      LinePen *penPtr
 *      int size - Size of element
 *      int nSymbolPts - Number of coordinates in array
 *      Point2D *symbolPts - Array of x,y coordinates for line
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Draws a symbol at each coordinate given.  If active,
 *      only those coordinates which are currently active are
 *      drawn.
 *
 * -----------------------------------------------------------------
 */
static void DrawSymbols(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, int size, int nSymbolPts,
                        Point2D *symbolPts) {
    XPoint pattern[13]; /* Template for polygon symbols */
    int r1, r2;
    register int i, n;
    int count;
    register Point2D *pointPtr, *endPtr;
#define SQRT_PI 1.77245385090552
#define S_RATIO 0.886226925452758

    if (size < 3) {
        if (penPtr->symbol.fillGC != NULL) {
            XPoint *points;

            points = (XPoint *)ckalloc(nSymbolPts * sizeof(XPoint));
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                points[count].x = (short int)pointPtr->x;
                points[count].y = (short int)pointPtr->y;
                count++;
            }
            XDrawPoints(graphPtr->display, drawable, penPtr->symbol.fillGC, points, nSymbolPts, CoordModeOrigin);
            ckfree((char *)points);
        }
        return;
    }
    r1 = (int)ceil(size * 0.5);
    r2 = (int)ceil(size * S_RATIO * 0.5);

    switch (penPtr->symbol.type) {
    case SYMBOL_NONE:
        break;

    case SYMBOL_SQUARE:
        DrawSquares(graphPtr->display, drawable, linePtr, penPtr, nSymbolPts, symbolPts, r2);
        break;

    case SYMBOL_CIRCLE:
        DrawCircles(graphPtr->display, drawable, linePtr, penPtr, nSymbolPts, symbolPts, r1);
        break;

    case SYMBOL_SPLUS:
    case SYMBOL_SCROSS: {
        XSegment *segArr; /* Array of line segments (splus, scross) */
        register XSegment *segPtr;
        int reqSize, nSegs, chunk;

        if (penPtr->symbol.type == SYMBOL_SCROSS) {
            r2 = Round(r2 * M_SQRT1_2);
            pattern[3].y = pattern[2].x = pattern[0].x = pattern[0].y = -r2;
            pattern[3].x = pattern[2].y = pattern[1].y = pattern[1].x = r2;
        } else {
            pattern[0].y = pattern[1].y = pattern[2].x = pattern[3].x = 0;
            pattern[0].x = pattern[2].y = -r2;
            pattern[1].x = pattern[3].y = r2;
        }
        segArr = (XSegment *)ckalloc(nSymbolPts * 2 * sizeof(XSegment));
        segPtr = segArr;
        if (linePtr->symbolInterval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(linePtr)) {
                    segPtr->x1 = pattern[0].x + (short int)pointPtr->x;
                    segPtr->y1 = pattern[0].y + (short int)pointPtr->y;
                    segPtr->x2 = pattern[1].x + (short int)pointPtr->x;
                    segPtr->y2 = pattern[1].y + (short int)pointPtr->y;
                    segPtr++;
                    segPtr->x1 = pattern[2].x + (short int)pointPtr->x;
                    segPtr->y1 = pattern[2].y + (short int)pointPtr->y;
                    segPtr->x2 = pattern[3].x + (short int)pointPtr->x;
                    segPtr->y2 = pattern[3].y + (short int)pointPtr->y;
                    segPtr++;
                    count++;
                }
                linePtr->symbolCounter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                segPtr->x1 = pattern[0].x + (short int)pointPtr->x;
                segPtr->y1 = pattern[0].y + (short int)pointPtr->y;
                segPtr->x2 = pattern[1].x + (short int)pointPtr->x;
                segPtr->y2 = pattern[1].y + (short int)pointPtr->y;
                segPtr++;
                segPtr->x1 = pattern[2].x + (short int)pointPtr->x;
                segPtr->y1 = pattern[2].y + (short int)pointPtr->y;
                segPtr->x2 = pattern[3].x + (short int)pointPtr->x;
                segPtr->y2 = pattern[3].y + (short int)pointPtr->y;
                segPtr++;
            }
        }
        nSegs = count * 2;
        /* Always draw skinny symbols regardless of the outline width */
        reqSize = MAX_DRAWSEGMENTS(graphPtr->display);
        for (i = 0; i < nSegs; i += reqSize) {
            chunk = ((i + reqSize) > nSegs) ? (nSegs - i) : reqSize;
            XDrawSegments(graphPtr->display, drawable, penPtr->symbol.outlineGC, segArr + i, chunk);
        }
        ckfree((char *)segArr);
    } break;

    case SYMBOL_PLUS:
    case SYMBOL_CROSS: {
        XPoint *polygon;
        register XPoint *p;
        int d; /* Small delta for cross/plus thickness */

        d = (r2 / 3);

        /*
         *
         *          2   3       The plus/cross symbol is a closed polygon
         *                      of 12 points. The diagram to the left
         *    0,12  1   4    5  represents the positions of the points
         *           x,y        which are computed below. The extra
         *     11  10   7    6  (thirteenth) point connects the first and
         *                      last points.
         *          9   8
         */

        pattern[0].x = pattern[11].x = pattern[12].x = -r2;
        pattern[2].x = pattern[1].x = pattern[10].x = pattern[9].x = -d;
        pattern[3].x = pattern[4].x = pattern[7].x = pattern[8].x = d;
        pattern[5].x = pattern[6].x = r2;
        pattern[2].y = pattern[3].y = -r2;
        pattern[0].y = pattern[1].y = pattern[4].y = pattern[5].y = pattern[12].y = -d;
        pattern[11].y = pattern[10].y = pattern[7].y = pattern[6].y = d;
        pattern[9].y = pattern[8].y = r2;

        if (penPtr->symbol.type == SYMBOL_CROSS) {
            double dx, dy;

            /* For the cross symbol, rotate the points by 45 degrees. */
            for (n = 0; n < 12; n++) {
                dx = (double)pattern[n].x * M_SQRT1_2;
                dy = (double)pattern[n].y * M_SQRT1_2;
                pattern[n].x = Round(dx - dy);
                pattern[n].y = Round(dx + dy);
            }
            pattern[12] = pattern[0];
        }
        polygon = (XPoint *)ckalloc(nSymbolPts * 13 * sizeof(XPoint));
        p = polygon;
        if (linePtr->symbolInterval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(linePtr)) {
                    for (n = 0; n < 13; n++) {
                        p->x = pattern[n].x + (short int)pointPtr->x;
                        p->y = pattern[n].y + (short int)pointPtr->y;
                        p++;
                    }
                    count++;
                }
                linePtr->symbolCounter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                for (n = 0; n < 13; n++) {
                    p->x = pattern[n].x + (short int)pointPtr->x;
                    p->y = pattern[n].y + (short int)pointPtr->y;
                    p++;
                }
            }
        }
        if (penPtr->symbol.fillGC != NULL) {
            for (p = polygon, i = 0; i < count; i++, p += 13) {
                XFillPolygon(graphPtr->display, drawable, penPtr->symbol.fillGC, p, 13, Complex, CoordModeOrigin);
            }
        }
        if (penPtr->symbol.outlineWidth > 0) {
            for (p = polygon, i = 0; i < count; i++, p += 13) {
                XDrawLines(graphPtr->display, drawable, penPtr->symbol.outlineGC, p, 13, CoordModeOrigin);
            }
        }
        ckfree((char *)polygon);
    } break;

    case SYMBOL_DIAMOND: {
        XPoint *polygon;
        register XPoint *p;

        /*
         *
         *                      The plus symbol is a closed polygon
         *            1         of 4 points. The diagram to the left
         *                      represents the positions of the points
         *       0,4 x,y  2     which are computed below. The extra
         *                      (fifth) point connects the first and
         *            3         last points.
         *
         */
        pattern[1].y = pattern[0].x = -r1;
        pattern[2].y = pattern[3].x = pattern[0].y = pattern[1].x = 0;
        pattern[3].y = pattern[2].x = r1;
        pattern[4] = pattern[0];

        polygon = (XPoint *)ckalloc(nSymbolPts * 5 * sizeof(XPoint));
        p = polygon;
        if (linePtr->symbolInterval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(linePtr)) {
                    for (n = 0; n < 5; n++, p++) {
                        p->x = pattern[n].x + (short int)pointPtr->x;
                        p->y = pattern[n].y + (short int)pointPtr->y;
                    }
                    count++;
                }
                linePtr->symbolCounter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                for (n = 0; n < 5; n++, p++) {
                    p->x = pattern[n].x + (short int)pointPtr->x;
                    p->y = pattern[n].y + (short int)pointPtr->y;
                }
            }
        }
        if (penPtr->symbol.fillGC != NULL) {
            for (p = polygon, i = 0; i < count; i++, p += 5) {
                XFillPolygon(graphPtr->display, drawable, penPtr->symbol.fillGC, p, 5, Convex, CoordModeOrigin);
            }
        }
        if (penPtr->symbol.outlineWidth > 0) {
            for (p = polygon, i = 0; i < count; i++, p += 5) {
                XDrawLines(graphPtr->display, drawable, penPtr->symbol.outlineGC, p, 5, CoordModeOrigin);
            }
        }
        ckfree((char *)polygon);
    } break;

    case SYMBOL_TRIANGLE:
    case SYMBOL_ARROW: {
        XPoint *polygon;
        register XPoint *p;
        double b;
        int b2, h1, h2;
#define H_RATIO 1.1663402261671607
#define B_RATIO 1.3467736870885982
#define TAN30 0.57735026918962573
#define COS30 0.86602540378443871

        b = Round(size * B_RATIO * 0.7);
        b2 = Round(b * 0.5);
        h2 = Round(TAN30 * b2);
        h1 = Round(b2 / COS30);
        /*
         *
         *                      The triangle symbol is a closed polygon
         *           0,3         of 3 points. The diagram to the left
         *                      represents the positions of the points
         *           x,y        which are computed below. The extra
         *                      (fourth) point connects the first and
         *      2           1   last points.
         *
         */

        if (penPtr->symbol.type == SYMBOL_ARROW) {
            pattern[3].x = pattern[0].x = 0;
            pattern[3].y = pattern[0].y = h1;
            pattern[1].x = b2;
            pattern[2].y = pattern[1].y = -h2;
            pattern[2].x = -b2;
        } else {
            pattern[3].x = pattern[0].x = 0;
            pattern[3].y = pattern[0].y = -h1;
            pattern[1].x = b2;
            pattern[2].y = pattern[1].y = h2;
            pattern[2].x = -b2;
        }
        polygon = (XPoint *)ckalloc(nSymbolPts * 4 * sizeof(XPoint));
        p = polygon;
        if (linePtr->symbolInterval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(linePtr)) {
                    for (n = 0; n < 4; n++) {
                        p->x = pattern[n].x + (short int)pointPtr->x;
                        p->y = pattern[n].y + (short int)pointPtr->y;
                        p++;
                    }
                    count++;
                }
                linePtr->symbolCounter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                for (n = 0; n < 4; n++) {
                    p->x = pattern[n].x + (short int)pointPtr->x;
                    p->y = pattern[n].y + (short int)pointPtr->y;
                    p++;
                }
            }
        }
        if (penPtr->symbol.fillGC != NULL) {
            for (p = polygon, i = 0; i < count; i++, p += 4) {
                XFillPolygon(graphPtr->display, drawable, penPtr->symbol.fillGC, p, 4, Convex, CoordModeOrigin);
            }
        }
        if (penPtr->symbol.outlineWidth > 0) {
            for (p = polygon, i = 0; i < count; i++, p += 4) {
                XDrawLines(graphPtr->display, drawable, penPtr->symbol.outlineGC, p, 4, CoordModeOrigin);
            }
        }
        ckfree((char *)polygon);
    } break;
    case SYMBOL_BITMAP: {
        Pixmap bitmap, mask;
        int width, height, bmWidth, bmHeight;
        double scale, sx, sy;
        int dx, dy;
        register int x, y;

        Tk_SizeOfBitmap(graphPtr->display, penPtr->symbol.bitmap, &width, &height);
        mask = None;

        /*
         * Compute the size of the scaled bitmap.  Stretch the
         * bitmap to fit a nxn bounding box.
         */
        sx = (double)size / (double)width;
        sy = (double)size / (double)height;
        scale = MIN(sx, sy);
        bmWidth = (int)(width * scale);
        bmHeight = (int)(height * scale);

        XSetClipMask(graphPtr->display, penPtr->symbol.outlineGC, None);
        if (penPtr->symbol.mask != None) {
            mask = Rbc_ScaleBitmap(graphPtr->tkwin, penPtr->symbol.mask, width, height, bmWidth, bmHeight);
            XSetClipMask(graphPtr->display, penPtr->symbol.outlineGC, mask);
        }
        bitmap = Rbc_ScaleBitmap(graphPtr->tkwin, penPtr->symbol.bitmap, width, height, bmWidth, bmHeight);
        if (penPtr->symbol.fillGC == NULL) {
            XSetClipMask(graphPtr->display, penPtr->symbol.outlineGC, bitmap);
        }
        dx = bmWidth / 2;
        dy = bmHeight / 2;
        if (linePtr->symbolInterval > 0) {
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(linePtr)) {
                    x = (int)pointPtr->x - dx;
                    y = (int)pointPtr->y - dy;
                    if ((penPtr->symbol.fillGC == NULL) || (mask != None)) {
                        XSetClipOrigin(graphPtr->display, penPtr->symbol.outlineGC, x, y);
                    }
                    XCopyPlane(graphPtr->display, bitmap, drawable, penPtr->symbol.outlineGC, 0, 0, bmWidth, bmHeight,
                               x, y, 1);
                }
                linePtr->symbolCounter++;
            }
        } else {
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                x = (int)pointPtr->x - dx;
                y = (int)pointPtr->y - dy;
                if ((penPtr->symbol.fillGC == NULL) || (mask != None)) {
                    XSetClipOrigin(graphPtr->display, penPtr->symbol.outlineGC, x, y);
                }
                XCopyPlane(graphPtr->display, bitmap, drawable, penPtr->symbol.outlineGC, 0, 0, bmWidth, bmHeight, x, y,
                           1);
            }
        }
        Tk_FreePixmap(graphPtr->display, bitmap);
        if (mask != None) {
            Tk_FreePixmap(graphPtr->display, mask);
        }
    } break;
    }
}

/*
 * -----------------------------------------------------------------
 *
 * DrawSymbol --
 *
 *      Draw the symbol centered at the each given x,y coordinate.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      Drawable drawable - Pixmap or window to draw into
 *      Element *elemPtr - Line element information
 *      int x - Center position of symbol
 *      int y - Center position of symbol
 *      int size - Size of symbol.
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Draws a symbol at the coordinate given.
 *
 * -----------------------------------------------------------------
 */
static void DrawSymbol(Graph *graphPtr, Drawable drawable, Element *elemPtr, int x, int y, int size) {
    Line *linePtr = (Line *)elemPtr;
    LinePen *penPtr = linePtr->normalPenPtr;

    if (penPtr->traceWidth > 0) {
        /*
         * Draw an extra line offset by one pixel from the previous to
         * give a thicker appearance.  This is only for the legend
         * entry.  This routine is never called for drawing the actual
         * line segments.
         */
        XDrawLine(graphPtr->display, drawable, penPtr->traceGC, x - size, y, x + size, y);
        XDrawLine(graphPtr->display, drawable, penPtr->traceGC, x - size, y + 1, x + size, y + 1);
    }
    if (penPtr->symbol.type != SYMBOL_NONE) {
        Point2D point;

        point.x = x, point.y = y;
        DrawSymbols(graphPtr, drawable, linePtr, linePtr->normalPenPtr, size, 1, &point);
    }
}
#ifdef WIN32

/*
 *----------------------------------------------------------------------
 *
 * DrawTraces --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *      Line *linePtr
 *      LinePen *penPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DrawTraces(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr) {
    Rbc_ChainLink *linkPtr;
    HBRUSH brush, oldBrush;
    HDC dc;
    HPEN pen, oldPen;
    POINT *points;
    TkWinDCState state;
    LineTrace *tracePtr;
    int j;
    int nPoints, remaining;
    register POINT *p;
    register int count;

    /*
     * Depending if the line is wide (> 1 pixel), arbitrarily break
     * the line in sections of 100 points.  This bit of weirdness has
     * to do with wide geometric pens.  The longer the polyline, the
     * slower it draws.  The trade off is that we lose dash and cap
     * uniformity for unbearably slow polyline draws.
     */
    if (penPtr->traceGC->line_width > 1) {
        nPoints = 100;
    } else {
        nPoints = Rbc_MaxRequestSize(graphPtr->display, sizeof(POINT)) - 1;
    }
    points = (POINT *)ckalloc((nPoints + 1) * sizeof(POINT));

    dc = TkWinGetDrawableDC(graphPtr->display, drawable, &state);

    pen = Rbc_GCToPen(dc, penPtr->traceGC);
    oldPen = SelectPen(dc, pen);
    brush = CreateSolidBrush(penPtr->traceGC->foreground);
    oldBrush = SelectBrush(dc, brush);
    SetROP2(dc, tkpWinRopModes[penPtr->traceGC->function]);

    for (linkPtr = Rbc_ChainFirstLink(linePtr->traces); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        tracePtr = Rbc_ChainGetValue(linkPtr);

        /*
         * If the trace has to be split into separate XDrawLines
         * calls, then the end point of the current trace is also the
         * starting point of the new split.
         */

        /* Step 1. Convert and draw the first section of the trace.
         *       It may contain the entire trace. */

        for (p = points, count = 0; count < MIN(nPoints, tracePtr->nScreenPts); count++, p++) {
            p->x = (int)tracePtr->screenPts[count].x;
            p->y = (int)tracePtr->screenPts[count].y;
        }
        Polyline(dc, points, count);

        /* Step 2. Next handle any full-size chunks left. */

        while ((count + nPoints) < tracePtr->nScreenPts) {
            /* Start with the last point of the previous trace. */
            points[0].x = points[nPoints - 1].x;
            points[0].y = points[nPoints - 1].y;

            for (p = points + 1, j = 0; j < nPoints; j++, count++, p++) {
                p->x = (int)tracePtr->screenPts[count].x;
                p->y = (int)tracePtr->screenPts[count].y;
            }
            Polyline(dc, points, nPoints + 1);
        }

        /* Step 3. Convert and draw the remaining points. */

        remaining = tracePtr->nScreenPts - count;
        if (remaining > 0) {
            /* Start with the last point of the previous trace. */
            points[0].x = points[nPoints - 1].x;
            points[0].y = points[nPoints - 1].y;

            for (p = points + 1; count < tracePtr->nScreenPts; count++, p++) {
                p->x = (int)tracePtr->screenPts[count].x;
                p->y = (int)tracePtr->screenPts[count].y;
            }
            Polyline(dc, points, remaining + 1);
        }
    }
    ckfree((char *)points);
    DeletePen(SelectPen(dc, oldPen));
    DeleteBrush(SelectBrush(dc, oldBrush));
    TkWinReleaseDrawableDC(drawable, dc, &state);
}

#else

/*
 *----------------------------------------------------------------------
 *
 * DrawTraces --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *      Line *linePtr
 *      LinePen *penPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DrawTraces(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr) {
    Rbc_ChainLink *linkPtr;
    LineTrace *tracePtr;
    XPoint *points;
    int j;
    int nPoints, remaining;
    register XPoint *p;
    register int count;

    nPoints = Rbc_MaxRequestSize(graphPtr->display, sizeof(XPoint)) - 1;
    points = (XPoint *)ckalloc((nPoints + 1) * sizeof(XPoint));

    for (linkPtr = Rbc_ChainFirstLink(linePtr->traces); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        int n;

        tracePtr = Rbc_ChainGetValue(linkPtr);

        /*
         * If the trace has to be split into separate XDrawLines
         * calls, then the end point of the current trace is also the
         * starting point of the new split.
         */
        /* Step 1. Convert and draw the first section of the trace.
         *       It may contain the entire trace. */

        n = MIN(nPoints, tracePtr->nScreenPts);
        for (p = points, count = 0; count < n; count++, p++) {
            p->x = (short int)tracePtr->screenPts[count].x;
            p->y = (short int)tracePtr->screenPts[count].y;
        }
        XDrawLines(graphPtr->display, drawable, penPtr->traceGC, points, count, CoordModeOrigin);

        /* Step 2. Next handle any full-size chunks left. */

        while ((count + nPoints) < tracePtr->nScreenPts) {
            /* Start with the last point of the previous trace. */
            points[0].x = points[nPoints - 1].x;
            points[0].y = points[nPoints - 1].y;

            for (p = points + 1, j = 0; j < nPoints; j++, count++, p++) {
                p->x = (short int)tracePtr->screenPts[count].x;
                p->y = (short int)tracePtr->screenPts[count].y;
            }
            XDrawLines(graphPtr->display, drawable, penPtr->traceGC, points, nPoints + 1, CoordModeOrigin);
        }

        /* Step 3. Convert and draw the remaining points. */

        remaining = tracePtr->nScreenPts - count;
        if (remaining > 0) {
            /* Start with the last point of the previous trace. */
            points[0].x = points[nPoints - 1].x;
            points[0].y = points[nPoints - 1].y;
            for (p = points + 1; count < tracePtr->nScreenPts; count++, p++) {
                p->x = (short int)tracePtr->screenPts[count].x;
                p->y = (short int)tracePtr->screenPts[count].y;
            }
            XDrawLines(graphPtr->display, drawable, penPtr->traceGC, points, remaining + 1, CoordModeOrigin);
        }
    }
    ckfree((char *)points);
}
#endif /* WIN32 */

/*
 *----------------------------------------------------------------------
 *
 * DrawValues --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable
 *      Line *linePtr
 *      LinePen *penPtr
 *      int nSymbolPts
 *      Point2D *symbolPts
 *      int *pointToData
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DrawValues(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, int nSymbolPts,
                       Point2D *symbolPts, int *pointToData) {
    Point2D *pointPtr, *endPtr;
    int count;
    char string[TCL_DOUBLE_SPACE * 2 + 2];
    char *fmt;
    double x, y;

    fmt = penPtr->valueFormat;
    if (fmt == NULL) {
        fmt = "%g";
    }
    count = 0;
    for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
        x = linePtr->x.valueArr[pointToData[count]];
        y = linePtr->y.valueArr[pointToData[count]];
        count++;
        if (penPtr->valueShow == SHOW_X) {
            sprintf(string, fmt, x);
        } else if (penPtr->valueShow == SHOW_Y) {
            sprintf(string, fmt, y);
        } else if (penPtr->valueShow == SHOW_BOTH) {
            sprintf(string, fmt, x);
            strcat(string, ",");
            sprintf(string + strlen(string), fmt, y);
        }
        Rbc_DrawText(graphPtr->tkwin, drawable, string, &(penPtr->valueStyle), (int)pointPtr->x, (int)pointPtr->y);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawActiveLine --
 *
 *      Draws the connected line(s) representing the element. If the
 *      line is made up of non-line symbols and the line width
 *      parameter has been set (linewidth > 0), the element will also
 *      be drawn as a line (with the linewidth requested).  The line
 *      may consist of separate line segments.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      Drawable drawable - Pixmap or window to draw into
 *      Element *elemPtr - Element to be drawn
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      X drawing commands are output.
 *
 *----------------------------------------------------------------------
 */
static void DrawActiveLine(Graph *graphPtr, Drawable drawable, Element *elemPtr) {
    Line *linePtr = (Line *)elemPtr;
    LinePen *penPtr = linePtr->activePenPtr;
    int symbolSize;

    if (penPtr == NULL) {
        return;
    }
    symbolSize = ScaleSymbol(elemPtr, linePtr->activePenPtr->symbol.size);

    /*
     * nActiveIndices
     *      > 0        Some points are active.  Uses activeArr.
     *      < 0        All points are active.
     *    == 0        No points are active.
     */
    if (linePtr->nActiveIndices > 0) {
        if (linePtr->flags & ACTIVE_PENDING) {
            MapActiveSymbols(graphPtr, linePtr);
        }
        if (penPtr->symbol.type != SYMBOL_NONE) {
            DrawSymbols(graphPtr, drawable, linePtr, penPtr, symbolSize, linePtr->nActivePts, linePtr->activePts);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            DrawValues(graphPtr, drawable, linePtr, penPtr, linePtr->nActivePts, linePtr->activePts,
                       linePtr->activeToData);
        }
    } else if (linePtr->nActiveIndices < 0) {
        if (penPtr->traceWidth > 0) {
            if (linePtr->nStrips > 0) {
                Rbc_Draw2DSegments(graphPtr->display, drawable, penPtr->traceGC, linePtr->strips, linePtr->nStrips);
            } else if (Rbc_ChainGetLength(linePtr->traces) > 0) {
                DrawTraces(graphPtr, drawable, linePtr, penPtr);
            }
        }
        if (penPtr->symbol.type != SYMBOL_NONE) {
            DrawSymbols(graphPtr, drawable, linePtr, penPtr, symbolSize, linePtr->nSymbolPts, linePtr->symbolPts);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            DrawValues(graphPtr, drawable, linePtr, penPtr, linePtr->nSymbolPts, linePtr->symbolPts,
                       linePtr->symbolToData);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawNormalLine --
 *
 *      Draws the connected line(s) representing the element. If the
 *      line is made up of non-line symbols and the line width parameter
 *      has been set (linewidth > 0), the element will also be drawn as
 *      a line (with the linewidth requested).  The line may consist of
 *      separate line segments.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      Drawable drawable - Pixmap or window to draw into
 *      Element *elemPtr - Element to be drawn
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      X drawing commands are output.
 *
 *----------------------------------------------------------------------
 */
static void DrawNormalLine(Graph *graphPtr, Drawable drawable, Element *elemPtr) {
    Line *linePtr = (Line *)elemPtr;
    LinePen *penPtr;
    Rbc_ChainLink *linkPtr;
    register LinePenStyle *stylePtr;
    unsigned int count;

    /* Fill area under the curve */
    if (linePtr->fillPts != NULL) {
        XPoint *points;
        Point2D *endPtr, *pointPtr;

        points = (XPoint *)ckalloc(sizeof(XPoint) * linePtr->nFillPts);
        count = 0;
        for (pointPtr = linePtr->fillPts, endPtr = linePtr->fillPts + linePtr->nFillPts; pointPtr < endPtr;
             pointPtr++) {
            points[count].x = (short int)pointPtr->x;
            points[count].y = (short int)pointPtr->y;
            count++;
        }
        if (linePtr->fillTile != NULL) {
            Rbc_SetTileOrigin(graphPtr->tkwin, linePtr->fillTile, 0, 0);
            Rbc_TilePolygon(graphPtr->tkwin, drawable, linePtr->fillTile, points, linePtr->nFillPts);
        } else if (linePtr->fillStipple != None) {
            XFillPolygon(graphPtr->display, drawable, linePtr->fillGC, points, linePtr->nFillPts, Complex,
                         CoordModeOrigin);
        }
        ckfree((char *)points);
    }

    /* Lines: stripchart segments or graph traces. */

    if (linePtr->nStrips > 0) {
        for (linkPtr = Rbc_ChainFirstLink(linePtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            penPtr = stylePtr->penPtr;
            if ((stylePtr->nStrips > 0) && (penPtr->errorBarLineWidth > 0)) {
                Rbc_Draw2DSegments(graphPtr->display, drawable, penPtr->traceGC, stylePtr->strips, stylePtr->nStrips);
            }
        }
    } else if ((Rbc_ChainGetLength(linePtr->traces) > 0) && (linePtr->normalPenPtr->traceWidth > 0)) {
        DrawTraces(graphPtr, drawable, linePtr, linePtr->normalPenPtr);
    }

    if (linePtr->reqMaxSymbols > 0) {
        int total;

        total = 0;
        for (linkPtr = Rbc_ChainFirstLink(linePtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            total += stylePtr->nSymbolPts;
        }
        linePtr->symbolInterval = total / linePtr->reqMaxSymbols;
        linePtr->symbolCounter = 0;
    }

    /* Symbols, error bars, values. */

    count = 0;
    for (linkPtr = Rbc_ChainFirstLink(linePtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        penPtr = stylePtr->penPtr;
        if ((stylePtr->xErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_X)) {
            Rbc_Draw2DSegments(graphPtr->display, drawable, penPtr->errorBarGC, stylePtr->xErrorBars,
                               stylePtr->xErrorBarCnt);
        }
        if ((stylePtr->yErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_Y)) {
            Rbc_Draw2DSegments(graphPtr->display, drawable, penPtr->errorBarGC, stylePtr->yErrorBars,
                               stylePtr->yErrorBarCnt);
        }
        if ((stylePtr->nSymbolPts > 0) && (penPtr->symbol.type != SYMBOL_NONE)) {
            DrawSymbols(graphPtr, drawable, linePtr, penPtr, stylePtr->symbolSize, stylePtr->nSymbolPts,
                        stylePtr->symbolPts);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            DrawValues(graphPtr, drawable, linePtr, penPtr, stylePtr->nSymbolPts, stylePtr->symbolPts,
                       linePtr->symbolToData + count);
        }
        count += stylePtr->nSymbolPts;
    }
    linePtr->symbolInterval = 0;
}

/*
 * -----------------------------------------------------------------
 *
 * GetSymbolPostScriptInfo --
 *
 *      Set up the PostScript environment with the macros and
 *      attributes needed to draw the symbols of the element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *      LinePen *penPtr
 *      int size
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
static void GetSymbolPostScriptInfo(Graph *graphPtr, PsToken psToken, LinePen *penPtr, int size) {
    XColor *outlineColor, *fillColor, *defaultColor;

    /* Set line and foreground attributes */
    outlineColor = penPtr->symbol.outlineColor;
    fillColor = penPtr->symbol.fillColor;
    defaultColor = penPtr->traceColor;

    if (fillColor == COLOR_DEFAULT) {
        fillColor = defaultColor;
    }
    if (outlineColor == COLOR_DEFAULT) {
        outlineColor = defaultColor;
    }
    if (penPtr->symbol.type == SYMBOL_NONE) {
        Rbc_LineAttributesToPostScript(psToken, defaultColor, penPtr->traceWidth + 2, &(penPtr->traceDashes), CapButt,
                                       JoinMiter);
    } else {
        Rbc_LineWidthToPostScript(psToken, penPtr->symbol.outlineWidth);
        Rbc_LineDashesToPostScript(psToken, (Rbc_Dashes *)NULL);
    }

    /*
     * Build a PostScript procedure to draw the symbols.  For bitmaps,
     * paint both the bitmap and its mask. Otherwise fill and stroke
     * the path formed already.
     */
    Rbc_AppendToPostScript(psToken, "\n/DrawSymbolProc {\n", (char *)NULL);
    switch (penPtr->symbol.type) {
    case SYMBOL_NONE:
        break; /* Do nothing */
    case SYMBOL_BITMAP: {
        int width, height;
        double sx, sy, scale;

        /*
         * Compute how much to scale the bitmap.  Don't let the
         * scaled bitmap exceed the bounding square for the
         * symbol.
         */
        Tk_SizeOfBitmap(graphPtr->display, penPtr->symbol.bitmap, &width, &height);
        sx = (double)size / (double)width;
        sy = (double)size / (double)height;
        scale = MIN(sx, sy);

        if ((penPtr->symbol.mask != None) && (fillColor != NULL)) {
            Rbc_AppendToPostScript(psToken, "\n  % Bitmap mask is \"",
                                   Tk_NameOfBitmap(graphPtr->display, penPtr->symbol.mask), "\"\n\n  ", (char *)NULL);
            Rbc_BackgroundToPostScript(psToken, fillColor);
            Rbc_BitmapToPostScript(psToken, graphPtr->display, penPtr->symbol.mask, scale, scale);
        }
        Rbc_AppendToPostScript(psToken, "\n  % Bitmap symbol is \"",
                               Tk_NameOfBitmap(graphPtr->display, penPtr->symbol.bitmap), "\"\n\n  ", (char *)NULL);
        Rbc_ForegroundToPostScript(psToken, outlineColor);
        Rbc_BitmapToPostScript(psToken, graphPtr->display, penPtr->symbol.bitmap, scale, scale);
    } break;
    default:
        if (fillColor != NULL) {
            Rbc_AppendToPostScript(psToken, "  ", (char *)NULL);
            Rbc_BackgroundToPostScript(psToken, fillColor);
            Rbc_AppendToPostScript(psToken, "  Fill\n", (char *)NULL);
        }
        if ((outlineColor != NULL) && (penPtr->symbol.outlineWidth > 0)) {
            Rbc_AppendToPostScript(psToken, "  ", (char *)NULL);
            Rbc_ForegroundToPostScript(psToken, outlineColor);
            Rbc_AppendToPostScript(psToken, "  stroke\n", (char *)NULL);
        }
        break;
    }
    Rbc_AppendToPostScript(psToken, "} def\n\n", (char *)NULL);
}

/*
 * -----------------------------------------------------------------
 *
 * SymbolsToPostScript --
 *
 *      Draw a symbol centered at the given x,y window coordinate
 *      based upon the element symbol type and size.
 *
 *      Most notable problem is the round-off errors generated when
 *      calculating the centered position of the symbol.
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *      LinePen *penPtr
 *      int size
 *      int nSymbolPts
 *      Point2D *symbolPts
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
static void SymbolsToPostScript(Graph *graphPtr, PsToken psToken, LinePen *penPtr, int size, int nSymbolPts,
                                Point2D *symbolPts) {
    double symbolSize;
    register Point2D *pointPtr, *endPtr;
    static char *symbolMacros[] = {
        "Li", "Sq", "Ci", "Di", "Pl", "Cr", "Sp", "Sc", "Tr", "Ar", "Bm", (char *)NULL,
    };
    GetSymbolPostScriptInfo(graphPtr, psToken, penPtr, size);

    symbolSize = (double)size;
    switch (penPtr->symbol.type) {
    case SYMBOL_SQUARE:
    case SYMBOL_CROSS:
    case SYMBOL_PLUS:
    case SYMBOL_SCROSS:
    case SYMBOL_SPLUS:
        symbolSize = (double)Round(size * S_RATIO);
        break;
    case SYMBOL_TRIANGLE:
    case SYMBOL_ARROW:
        symbolSize = (double)Round(size * 0.7);
        break;
    case SYMBOL_DIAMOND:
        symbolSize = (double)Round(size * M_SQRT1_2);
        break;

    default:
        break;
    }
    for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
        Rbc_FormatToPostScript(psToken, "%g %g %g %s\n", pointPtr->x, pointPtr->y, symbolSize,
                               symbolMacros[penPtr->symbol.type]);
    }
}

/*
 * -----------------------------------------------------------------
 *
 * SymbolToPostScript --
 *
 *      Draw the symbol centered at the each given x,y coordinate.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      PsToken psToken
 *      Element *elemPtr - Line element information
 *      double x - Center position of symbol
 *      double y - Center position of symbol
 *      int size - Size of element
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Draws a symbol at the coordinate given.
 *
 * -----------------------------------------------------------------
 */
static void SymbolToPostScript(Graph *graphPtr, PsToken psToken, Element *elemPtr, double x, double y, int size) {
    Line *linePtr = (Line *)elemPtr;
    LinePen *penPtr = linePtr->normalPenPtr;

    if (penPtr->traceWidth > 0) {
        /*
         * Draw an extra line offset by one pixel from the previous to
         * give a thicker appearance.  This is only for the legend
         * entry.  This routine is never called for drawing the actual
         * line segments.
         */
        Rbc_LineAttributesToPostScript(psToken, penPtr->traceColor, penPtr->traceWidth + 2, &(penPtr->traceDashes),
                                       CapButt, JoinMiter);
        Rbc_FormatToPostScript(psToken, "%g %g %d Li\n", x, y, size + size);
    }
    if (penPtr->symbol.type != SYMBOL_NONE) {
        Point2D point;

        point.x = x, point.y = y;
        SymbolsToPostScript(graphPtr, psToken, penPtr, size, 1, &point);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * SetLineAttributes --
 *
 *      TODO: Description
 *
 * Parameters:
 *      PsToken psToken
 *      LinePen *penPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void SetLineAttributes(PsToken psToken, LinePen *penPtr) {
    /* Set the attributes of the line (color, dashes, linewidth) */
    Rbc_LineAttributesToPostScript(psToken, penPtr->traceColor, penPtr->traceWidth, &(penPtr->traceDashes), CapButt,
                                   JoinMiter);
    if ((LineIsDashed(penPtr->traceDashes)) && (penPtr->traceOffColor != NULL)) {
        Rbc_AppendToPostScript(psToken, "/DashesProc {\n  gsave\n    ", (char *)NULL);
        Rbc_BackgroundToPostScript(psToken, penPtr->traceOffColor);
        Rbc_AppendToPostScript(psToken, "    ", (char *)NULL);
        Rbc_LineDashesToPostScript(psToken, (Rbc_Dashes *)NULL);
        Rbc_AppendToPostScript(psToken, "stroke\n  grestore\n} def\n", (char *)NULL);
    } else {
        Rbc_AppendToPostScript(psToken, "/DashesProc {} def\n", (char *)NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * TracesToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      PsToken psToken
 *      Line *linePtr
 *      LinePen *penPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void TracesToPostScript(PsToken psToken, Line *linePtr, LinePen *penPtr) {
    Rbc_ChainLink *linkPtr;
    LineTrace *tracePtr;
    register Point2D *pointPtr, *endPtr;
    int count;

    SetLineAttributes(psToken, penPtr);
    for (linkPtr = Rbc_ChainFirstLink(linePtr->traces); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        tracePtr = Rbc_ChainGetValue(linkPtr);
        if (tracePtr->nScreenPts <= 0) {
            continue;
        }
#define PS_MAXPATH                                                                                                     \
    1500 /* Maximum number of components in a PostScript                                                               \
          * (level 1) path. */
        pointPtr = tracePtr->screenPts;
        Rbc_FormatToPostScript(psToken, " newpath %g %g moveto\n", pointPtr->x, pointPtr->y);
        pointPtr++;
        count = 0;
        for (endPtr = tracePtr->screenPts + (tracePtr->nScreenPts - 1); pointPtr < endPtr; pointPtr++) {
            Rbc_FormatToPostScript(psToken, " %g %g lineto\n", pointPtr->x, pointPtr->y);
            if ((count % PS_MAXPATH) == 0) {
                Rbc_FormatToPostScript(psToken, "DashesProc stroke\n newpath  %g %g moveto\n", pointPtr->x,
                                       pointPtr->y);
            }
            count++;
        }
        Rbc_FormatToPostScript(psToken, " %g %g lineto\n", pointPtr->x, pointPtr->y);
        Rbc_AppendToPostScript(psToken, "DashesProc stroke\n", (char *)NULL);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ValuesToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      PsToken psToken
 *      Line *linePtr
 *      LinePen *penPtr
 *      int nSymbolPts
 *      Point2D *symbolPts
 *      int *pointToData
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void ValuesToPostScript(PsToken psToken, Line *linePtr, LinePen *penPtr, int nSymbolPts, Point2D *symbolPts,
                               int *pointToData) {
    Point2D *pointPtr, *endPtr;
    int count;
    char string[TCL_DOUBLE_SPACE * 2 + 2];
    char *fmt;
    double x, y;

    fmt = penPtr->valueFormat;
    if (fmt == NULL) {
        fmt = "%g";
    }
    count = 0;
    for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
        x = linePtr->x.valueArr[pointToData[count]];
        y = linePtr->y.valueArr[pointToData[count]];
        count++;
        if (penPtr->valueShow == SHOW_X) {
            sprintf(string, fmt, x);
        } else if (penPtr->valueShow == SHOW_Y) {
            sprintf(string, fmt, y);
        } else if (penPtr->valueShow == SHOW_BOTH) {
            sprintf(string, fmt, x);
            strcat(string, ",");
            sprintf(string + strlen(string), fmt, y);
        }
        Rbc_TextToPostScript(psToken, string, &(penPtr->valueStyle), pointPtr->x, pointPtr->y);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ActiveLineToPostScript --
 *
 *      Generates PostScript commands to draw as "active" the points
 *      (symbols) and or line segments (trace) representing the
 *      element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *      Element *elemPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      PostScript pen width, dashes, and color settings are changed.
 *
 *----------------------------------------------------------------------
 */
static void ActiveLineToPostScript(Graph *graphPtr, PsToken psToken, Element *elemPtr) {
    Line *linePtr = (Line *)elemPtr;
    LinePen *penPtr = linePtr->activePenPtr;
    int symbolSize;

    if (penPtr == NULL) {
        return;
    }
    symbolSize = ScaleSymbol(elemPtr, penPtr->symbol.size);
    if (linePtr->nActiveIndices > 0) {
        if (linePtr->flags & ACTIVE_PENDING) {
            MapActiveSymbols(graphPtr, linePtr);
        }
        if (penPtr->symbol.type != SYMBOL_NONE) {
            SymbolsToPostScript(graphPtr, psToken, penPtr, symbolSize, linePtr->nActivePts, linePtr->activePts);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            ValuesToPostScript(psToken, linePtr, penPtr, linePtr->nActivePts, linePtr->activePts,
                               linePtr->activeToData);
        }
    } else if (linePtr->nActiveIndices < 0) {
        if (penPtr->traceWidth > 0) {
            if (linePtr->nStrips > 0) {
                SetLineAttributes(psToken, penPtr);
                Rbc_2DSegmentsToPostScript(psToken, linePtr->strips, linePtr->nStrips);
            }
            if (Rbc_ChainGetLength(linePtr->traces) > 0) {
                TracesToPostScript(psToken, linePtr, (LinePen *)penPtr);
            }
        }
        if (penPtr->symbol.type != SYMBOL_NONE) {
            SymbolsToPostScript(graphPtr, psToken, penPtr, symbolSize, linePtr->nSymbolPts, linePtr->symbolPts);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            ValuesToPostScript(psToken, linePtr, penPtr, linePtr->nSymbolPts, linePtr->symbolPts,
                               linePtr->symbolToData);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * NormalLineToPostScript --
 *
 *      Similar to the DrawLine procedure, prints PostScript related
 *      commands to form the connected line(s) representing the element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *      Element *elemPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      PostScript pen width, dashes, and color settings are changed.
 *
 *----------------------------------------------------------------------
 */
static void NormalLineToPostScript(Graph *graphPtr, PsToken psToken, Element *elemPtr) {
    Line *linePtr = (Line *)elemPtr;
    register LinePenStyle *stylePtr;
    Rbc_ChainLink *linkPtr;
    LinePen *penPtr;
    unsigned int count;
    XColor *colorPtr;

    /* Draw fill area */
    if (linePtr->fillPts != NULL) {
        /* Create a path to use for both the polygon and its outline. */
        Rbc_PathToPostScript(psToken, linePtr->fillPts, linePtr->nFillPts);
        Rbc_AppendToPostScript(psToken, "closepath\n", (char *)NULL);

        /* If the background fill color was specified, draw the
         * polygon in a solid fashion with that color.  */
        if (linePtr->fillBgColor != NULL) {
            Rbc_BackgroundToPostScript(psToken, linePtr->fillBgColor);
            Rbc_AppendToPostScript(psToken, "Fill\n", (char *)NULL);
        }
        Rbc_ForegroundToPostScript(psToken, linePtr->fillFgColor);
        if (linePtr->fillTile != NULL) {
            /* TBA: Transparent tiling is the hard part. */
        } else if ((linePtr->fillStipple != None) && (linePtr->fillStipple != PATTERN_SOLID)) {
            /* Draw the stipple in the foreground color. */
            Rbc_StippleToPostScript(psToken, graphPtr->display, linePtr->fillStipple);
        } else {
            Rbc_AppendToPostScript(psToken, "Fill\n", (char *)NULL);
        }
    }
    /* Draw lines */
    if (linePtr->nStrips > 0) {
        for (linkPtr = Rbc_ChainFirstLink(linePtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            penPtr = stylePtr->penPtr;
            if ((stylePtr->nStrips > 0) && (penPtr->traceWidth > 0)) {
                SetLineAttributes(psToken, penPtr);
                Rbc_2DSegmentsToPostScript(psToken, stylePtr->strips, stylePtr->nStrips);
            }
        }
    } else if ((Rbc_ChainGetLength(linePtr->traces) > 0) && (linePtr->normalPenPtr->traceWidth > 0)) {
        TracesToPostScript(psToken, linePtr, linePtr->normalPenPtr);
    }

    /* Draw symbols, error bars, values. */

    count = 0;
    for (linkPtr = Rbc_ChainFirstLink(linePtr->palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        penPtr = stylePtr->penPtr;
        colorPtr = penPtr->errorBarColor;
        if (colorPtr == COLOR_DEFAULT) {
            colorPtr = penPtr->traceColor;
        }
        if ((stylePtr->xErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_X)) {
            Rbc_LineAttributesToPostScript(psToken, colorPtr, penPtr->errorBarLineWidth, NULL, CapButt, JoinMiter);
            Rbc_2DSegmentsToPostScript(psToken, stylePtr->xErrorBars, stylePtr->xErrorBarCnt);
        }
        if ((stylePtr->yErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_Y)) {
            Rbc_LineAttributesToPostScript(psToken, colorPtr, penPtr->errorBarLineWidth, NULL, CapButt, JoinMiter);
            Rbc_2DSegmentsToPostScript(psToken, stylePtr->yErrorBars, stylePtr->yErrorBarCnt);
        }
        if ((stylePtr->nSymbolPts > 0) && (stylePtr->penPtr->symbol.type != SYMBOL_NONE)) {
            SymbolsToPostScript(graphPtr, psToken, penPtr, stylePtr->symbolSize, stylePtr->nSymbolPts,
                                stylePtr->symbolPts);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            ValuesToPostScript(psToken, linePtr, penPtr, stylePtr->nSymbolPts, stylePtr->symbolPts,
                               linePtr->symbolToData + count);
        }
        count += stylePtr->nSymbolPts;
    }
}

#define FreeVector(v)                                                                                                  \
    if ((v).clientId != NULL) {                                                                                        \
        Rbc_FreeVectorId((v).clientId);                                                                                \
    } else if ((v).valueArr != NULL) {                                                                                 \
        ckfree((char *)(v).valueArr);                                                                                  \
    }

/*
 *----------------------------------------------------------------------
 *
 * DestroyLine --
 *
 *      Release memory and resources allocated for the line element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Element *elemPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Everything associated with the line element is freed up.
 *
 *----------------------------------------------------------------------
 */
static void DestroyLine(Graph *graphPtr, Element *elemPtr) {
    Line *linePtr = (Line *)elemPtr;

    if (linePtr->normalPenPtr != &(linePtr->builtinPen)) {
        Rbc_FreePen(graphPtr, (Pen *)linePtr->normalPenPtr);
    }
    DestroyPen(graphPtr, (Pen *)&(linePtr->builtinPen));
    if (linePtr->activePenPtr != NULL) {
        Rbc_FreePen(graphPtr, (Pen *)linePtr->activePenPtr);
    }

    FreeVector(linePtr->w);
    FreeVector(linePtr->x);
    FreeVector(linePtr->xHigh);
    FreeVector(linePtr->xLow);
    FreeVector(linePtr->xError);
    FreeVector(linePtr->y);
    FreeVector(linePtr->yHigh);
    FreeVector(linePtr->yLow);
    FreeVector(linePtr->yError);

    ResetLine(linePtr);
    if (linePtr->palette != NULL) {
        Rbc_FreePalette(graphPtr, linePtr->palette);
        Rbc_ChainDestroy(linePtr->palette);
    }
    if (linePtr->tags != NULL) {
        ckfree((char *)linePtr->tags);
    }
    if (linePtr->activeIndices != NULL) {
        ckfree((char *)linePtr->activeIndices);
    }
    if (linePtr->fillPts != NULL) {
        ckfree((char *)linePtr->fillPts);
    }
    if (linePtr->fillTile != NULL) {
        Rbc_FreeTile(linePtr->fillTile);
    }
    if ((linePtr->fillStipple != None) && (linePtr->fillStipple != PATTERN_SOLID)) {
        Tk_FreeBitmap(graphPtr->display, linePtr->fillStipple);
    }
    if (linePtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, linePtr->fillGC);
    }
}

static ElementProcs lineProcs = {
    ClosestLine,            /* Finds the closest element/data point */
    ConfigureLine,          /* Configures the element. */
    DestroyLine,            /* Destroys the element. */
    DrawActiveLine,         /* Draws active element */
    DrawNormalLine,         /* Draws normal element */
    DrawSymbol,             /* Draws the element symbol. */
    GetLineExtents,         /* Find the extents of the element's data. */
    ActiveLineToPostScript, /* Prints active element. */
    NormalLineToPostScript, /* Prints normal element. */
    SymbolToPostScript,     /* Prints the line's symbol. */
    MapLine                 /* Compute element's screen coordinates. */
};

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LineElement --
 *
 *      Allocate memory and initialize methods for the new line element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      char *name
 *      Rbc_Uid classUid
 *
 * Results:
 *      The pointer to the newly allocated element structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the line element structure.
 *
 *----------------------------------------------------------------------
 */
Element *Rbc_LineElement(Graph *graphPtr, char *name, Rbc_Uid classUid) {
    register Line *linePtr;

    linePtr = RbcCalloc(1, sizeof(Line));
    assert(linePtr);
    linePtr->procsPtr = &lineProcs;
    if (classUid == rbcLineElementUid) {
        linePtr->configSpecs = lineElemConfigSpecs;
    } else {
        linePtr->configSpecs = stripElemConfigSpecs;
    }

    /* By default an element's name and label are the same. */
    linePtr->label = RbcStrdup(name);
    linePtr->name = RbcStrdup(name);

    linePtr->classUid = classUid;
    linePtr->flags = SCALE_SYMBOL;
    linePtr->graphPtr = graphPtr;
    linePtr->labelRelief = TK_RELIEF_FLAT;
    linePtr->normalPenPtr = &linePtr->builtinPen;
    linePtr->palette = Rbc_ChainCreate();
    linePtr->penDir = PEN_BOTH_DIRECTIONS;
    linePtr->reqSmooth = PEN_SMOOTH_NONE;
    InitPen(linePtr->normalPenPtr);
    return (Element *)linePtr;
}
