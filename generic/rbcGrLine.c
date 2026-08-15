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
#include <stdint.h>
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
    Point2D *screenPts;  /* Array of transformed coordinates */
    Tcl_Size nScreenPts; /* Number of coordinates */
    Tcl_Size *indices;   /* Maps segments/traces to data points */
    /*
     * Optional parallel array.  A non-zero entry at i means that no
     * line segment may be drawn from screenPts[i - 1] to
     * screenPts[i].
     *
     * NULL means that the entire mapped point array is continuous.
     */
    unsigned char *breakBefore;
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
    SymbolType type;
    Pixmap bitmap;
    Pixmap mask;
} ParsedSymbol;

typedef struct {
    Tcl_Size start;         /* Index into the X-Y coordinate
                             * arrays indicating where trace
                             * starts. */
    Tcl_Size nScreenPts;    /* Number of points in the continuous
                             * trace */
    Point2D *screenPts;     /* Array of screen coordinates
                             * (malloc-ed) representing the
                             * trace. */
    Tcl_Size *symbolToData; /* Reverse mapping of screen
                             * coordinate indices back to their
                             * data coordinates */
} LineTrace;

typedef struct {
    /*
     * Common pen state. This must remain the first member.
     */
    Pen core;

    /*
     * Original Tcl representations for manually derived values.
     */
    Tcl_Obj *dashesObjPtr;
    Tcl_Obj *errorBarColorObjPtr;
    Tcl_Obj *errorBarWidthObjPtr;
    Tcl_Obj *errorBarCapObjPtr;
    Tcl_Obj *fillObjPtr;
    Tcl_Obj *lineWidthObjPtr;
    Tcl_Obj *offDashObjPtr;
    Tcl_Obj *outlineObjPtr;
    Tcl_Obj *outlineWidthObjPtr;
    Tcl_Obj *pixelsObjPtr;
    Tcl_Obj *showErrorBarsObjPtr;
    Tcl_Obj *showValuesObjPtr;
    Tcl_Obj *symbolObjPtr;
    Tcl_Obj *valueRotateObjPtr;
    Tcl_Obj *valueShadowObjPtr;
    
    Symbol symbol;

    int traceWidth;
    Rbc_Dashes traceDashes;
    XColor *traceColor;
    XColor *traceOffColor;
    GC traceGC;

    int errorBarShow;
    int errorBarLineWidth;
    int errorBarCapWidth;
    XColor *errorBarColor;
    GC errorBarGC;

    int valueShow;
    char *valueFormat;
    TextStyle valueStyle;
} LinePen;

#define LINE_PEN_FROM_CORE(penPtr) ((LinePen *)((char *)(penPtr) - offsetof(LinePen, core)))

#define LINE_PEN_CORE_OFFSET(member) (offsetof(LinePen, core) + offsetof(Pen, member))

typedef struct {
    Weight weight; /* Weight range where this pen is valid. */

    LinePen *penPtr; /* Pen used to draw symbols, traces, error
                      * bars, segments, etc. */

    Segment2D *xErrorBars; /* Point to start of this pen's X-error bar
                            * segments in the element's array. */
    Segment2D *yErrorBars; /* Point to start of this pen's Y-error bar
                            * segments in the element's array. */
    Tcl_Size xErrorBarCnt;      /* # of error bars for this pen. */
    Tcl_Size yErrorBarCnt;      /* # of error bars for this pen. */

    int errorBarCapWidth; /* Length of the cap ends on each
                           * error bar. */

    int symbolSize; /* Size of the pen's symbol scaled to the
                     * current graph size. */

    /* Graph specific data. */

    Point2D *symbolPts; /* Points to start of array for this pen. */

    Tcl_Size nSymbolPts; /* # of points for this pen. */

    /* The last two fields are used only for stripcharts. */

    Segment2D *strips; /* Points to start of the line segments
                        * for this pen. */

    Tcl_Size nStrips; /* # of line segments for this pen. */

} LinePenStyle;

typedef struct {
    Element core;
    /*
     * Line specific configurable attributes
     */
    LinePen builtinPen;


    /*
     * Original Tcl representations for line-element options that
     * require validation or conversion after Tk_SetOptions.
     */
    Tcl_Obj *areaPatternObjPtr;
    Tcl_Obj *areaTileObjPtr;
    Tcl_Obj *maxSymbolsObjPtr;
    Tcl_Obj *smoothObjPtr;
    Tcl_Obj *traceObjPtr;
    
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

    Tcl_Size nFillPts;
    Point2D *fillPts; /* Array of points used to draw
                       * polygon to fill area under the
                       * curve */

    /* Symbol points */
    Point2D *symbolPts; /* Holds the screen coordinates of all
                         * the data points for the element. */
    Tcl_Size nSymbolPts;     /* Number of points */

    Tcl_Size *symbolToData; /* Contains indices of data points.
                        * It's first used to map pens to the
                        * visible points to sort them by pen
                        * style, and later to find data
                        * points from the index of a visible
                        * point. */

    /* Active symbol points */
    Point2D *activePts; /* Array of indices representing the
                         * "active" points. */
    Tcl_Size nActivePts;     /* Number of indices in the above array. */

    Tcl_Size *activeToData; /* Contains indices of data points.
                        * It's first used to map pens to the
                        * visible points to sort them by pen
                        * style, and later to find data
                        * points from the index of a visible
                        * point. */

    int reqMaxSymbols;
    Tcl_Size symbolInterval;
    Tcl_Size symbolCounter;

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
    Tcl_Size nStrips;       /* Number of line segments to be drawn. */
    Tcl_Size *stripToData;  /* Pen to visible line segment mapping. */

} Line;

_Static_assert(offsetof(Line, core) == 0, "Element core must be the first Line member");

#define LINE_FROM_CORE(elemPtr) ((Line *)((char *)(elemPtr) - offsetof(Line, core)))

#define LINE_CORE_OFFSET(member) (offsetof(Line, core) + offsetof(Element, member))
#define LINE_BUILTIN_PEN_OFFSET(member) (offsetof(Line, builtinPen) + offsetof(LinePen, member))

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

/*
 * Line and strip-element option conversion masks.
 *
 * These typeMask bits describe work that ConfigureLine must perform
 * after Tk_SetOptions has stored the new option values.
 */
#define LINE_ELEM_PEN_MASK (1 << 0)
#define LINE_ELEM_TAGS_MASK (1 << 1)
#define LINE_ELEM_DATA_MASK (1 << 2)
#define LINE_ELEM_AXES_MASK (1 << 3)
#define LINE_ELEM_STATE_MASK (1 << 4)
#define LINE_ELEM_STYLES_MASK (1 << 5)
#define LINE_ELEM_BUILTIN_PEN_MASK (1 << 6)
#define LINE_ELEM_AREA_MASK (1 << 7)
#define LINE_ELEM_SMOOTH_MASK (1 << 8)
#define LINE_ELEM_TRACE_MASK (1 << 9)
#define LINE_ELEM_MAP_ITEM_MASK (1 << 10)
#define LINE_ELEM_SCALE_SYMBOL_MASK (1 << 11)
#define LINE_ELEM_MAX_SYMBOLS_MASK (1 << 12)

#define LINE_ELEM_SCALAR_MASK (LINE_ELEM_MAX_SYMBOLS_MASK | LINE_ELEM_SMOOTH_MASK | LINE_ELEM_TRACE_MASK)

typedef enum {
    LINE_SCALAR_OPTION_NONE,
    LINE_SCALAR_OPTION_MAX_SYMBOLS,
    LINE_SCALAR_OPTION_SMOOTH,
    LINE_SCALAR_OPTION_TRACE
} LineScalarOption;

#define LINE_SCALAR_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

typedef struct {
    unsigned int stagedMask;

    int maxSymbols;
    Smoothing smooth;
    int penDir;
} LineScalarTransaction;

typedef enum {
    LINE_AREA_OPTION_NONE,
    LINE_AREA_OPTION_PATTERN,
    LINE_AREA_OPTION_TILE
} LineAreaOption;

#define LINE_AREA_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

typedef struct {
    unsigned int stagedMask;

    Pixmap stipple;
    Rbc_Tile tile;
    GC gc;
} LineAreaTransaction;

/*
 * Options present only for graph line elements.
 */
#define LINE_ELEMENT_AREA_OPTION_ENTRIES                                      \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-areapattern", "areaPattern", "AreaPattern",                         \
        DEF_LINE_PATTERN,                                                     \
        offsetof(Line, areaPatternObjPtr),                                    \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_AREA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_COLOR,                                                      \
        "-areaforeground", "areaForeground", "areaForeground",                 \
        DEF_LINE_PATTERN_FG,                                                  \
        -1,                                                                   \
        offsetof(Line, fillFgColor),                                          \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_AREA_MASK                                                   \
    },                                                                        \
    {                                                                         \
        TK_OPTION_COLOR,                                                      \
        "-areabackground", "areaBackground", "areaBackground",                 \
        DEF_LINE_PATTERN_BG,                                                  \
        -1,                                                                   \
        offsetof(Line, fillBgColor),                                          \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_AREA_MASK                                                   \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-areatile", "areaTile", "AreaTile",                                  \
        DEF_LINE_PATTERN_TILE,                                                \
        offsetof(Line, areaTileObjPtr),                                       \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_AREA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },

#define LINE_ELEMENT_REDUCE_OPTION_ENTRY                                      \
    {                                                                         \
        TK_OPTION_DOUBLE,                                                     \
        "-reduce", "reduce", "Reduce",                                        \
        DEF_LINE_REDUCE,                                                      \
        -1,                                                                   \
        offsetof(Line, rTolerance),                                           \
        TK_OPTION_DONT_SET_DEFAULT,                                           \
        NULL,                                                                 \
        LINE_ELEM_MAP_ITEM_MASK                                               \
    },

#define LINE_ELEMENT_STATE_OPTION_ENTRY                                       \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-state", "state", "State",                                           \
        DEF_LINE_STATE,                                                       \
        LINE_CORE_OFFSET(stateObjPtr),                                        \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_STATE_MASK                                                  \
    },

#define LINE_ELEMENT_TRACE_OPTION_ENTRY                                       \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-trace", "trace", "Trace",                                           \
        DEF_LINE_PEN_DIRECTION,                                               \
        offsetof(Line, traceObjPtr),                                          \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_TRACE_MASK | LINE_ELEM_MAP_ITEM_MASK                        \
    },

#define LINE_ELEMENT_NO_OPTION_ENTRIES

#define LINE_ELEMENT_OPTION_ENTRIES(AREA_ENTRIES, REDUCE_ENTRY,               \
                                    STATE_ENTRY, TRACE_ENTRY)                  \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-activepen", "activePen", "ActivePen",                               \
        DEF_LINE_ACTIVE_PEN,                                                  \
        LINE_CORE_OFFSET(activePenObjPtr),                                    \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_PEN_MASK                                                    \
    },                                                                        \
    AREA_ENTRIES                                                              \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-bindtags", "bindTags", "BindTags",                                  \
        DEF_LINE_TAGS,                                                        \
        LINE_CORE_OFFSET(bindTagsObjPtr),                                     \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_TAGS_MASK                                                   \
    },                                                                        \
    {                                                                         \
        TK_OPTION_COLOR,                                                      \
        "-color", "color", "Color",                                           \
        DEF_LINE_PEN_COLOR,                                                   \
        -1,                                                                   \
        LINE_BUILTIN_PEN_OFFSET(traceColor),                                  \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-dashes", "dashes", "Dashes",                                        \
        DEF_LINE_DASHES,                                                      \
        LINE_BUILTIN_PEN_OFFSET(dashesObjPtr),                                \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-data", "data", "Data",                                              \
        DEF_LINE_DATA,                                                        \
        LINE_CORE_OFFSET(dataObjPtr),                                         \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-errorbarcolor", "errorBarColor", "ErrorBarColor",                   \
        DEF_LINE_ERRORBAR_COLOR,                                              \
        LINE_BUILTIN_PEN_OFFSET(errorBarColorObjPtr),                         \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-errorbarwidth", "errorBarWidth", "ErrorBarWidth",                   \
        DEF_LINE_ERRORBAR_LINE_WIDTH,                                         \
        LINE_BUILTIN_PEN_OFFSET(errorBarWidthObjPtr),                         \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-errorbarcap", "errorBarCap", "ErrorBarCap",                         \
        DEF_LINE_ERRORBAR_CAP_WIDTH,                                          \
        LINE_BUILTIN_PEN_OFFSET(errorBarCapObjPtr),                           \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK | LINE_ELEM_MAP_ITEM_MASK                  \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-fill", "fill", "Fill",                                              \
        DEF_LINE_FILL_COLOR,                                                  \
        LINE_BUILTIN_PEN_OFFSET(fillObjPtr),                                  \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_BOOLEAN,                                                    \
        "-hide", "hide", "Hide",                                              \
        DEF_LINE_HIDE,                                                        \
        -1,                                                                   \
        LINE_CORE_OFFSET(hidden),                                             \
        TK_OPTION_DONT_SET_DEFAULT,                                           \
        NULL,                                                                 \
        LINE_ELEM_MAP_ITEM_MASK                                               \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-label", "label", "Label",                                           \
        DEF_LINE_LABEL,                                                       \
        -1,                                                                   \
        LINE_CORE_OFFSET(label),                                              \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_MAP_ITEM_MASK                                               \
    },                                                                        \
    {                                                                         \
        TK_OPTION_RELIEF,                                                     \
        "-labelrelief", "labelRelief", "LabelRelief",                         \
        DEF_LINE_LABEL_RELIEF,                                                \
        -1,                                                                   \
        LINE_CORE_OFFSET(labelRelief),                                        \
        TK_OPTION_DONT_SET_DEFAULT,                                           \
        NULL,                                                                 \
        LINE_ELEM_MAP_ITEM_MASK                                               \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-linewidth", "lineWidth", "LineWidth",                               \
        DEF_LINE_PEN_WIDTH,                                                   \
        LINE_BUILTIN_PEN_OFFSET(lineWidthObjPtr),                             \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-mapx", "mapX", "MapX",                                              \
        DEF_LINE_AXIS_X,                                                      \
        LINE_CORE_OFFSET(mapXObjPtr),                                         \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_AXES_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-mapy", "mapY", "MapY",                                              \
        DEF_LINE_AXIS_Y,                                                      \
        LINE_CORE_OFFSET(mapYObjPtr),                                         \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_AXES_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-maxsymbols", "maxSymbols", "MaxSymbols",                            \
        DEF_LINE_MAX_SYMBOLS,                                                 \
        offsetof(Line, maxSymbolsObjPtr),                                     \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_MAX_SYMBOLS_MASK | LINE_ELEM_MAP_ITEM_MASK                  \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-offdash", "offDash", "OffDash",                                     \
        DEF_LINE_OFFDASH_COLOR,                                               \
        LINE_BUILTIN_PEN_OFFSET(offDashObjPtr),                               \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-outline", "outline", "Outline",                                     \
        DEF_LINE_OUTLINE_COLOR,                                               \
        LINE_BUILTIN_PEN_OFFSET(outlineObjPtr),                               \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-outlinewidth", "outlineWidth", "OutlineWidth",                      \
        DEF_LINE_OUTLINE_WIDTH,                                               \
        LINE_BUILTIN_PEN_OFFSET(outlineWidthObjPtr),                          \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-pen", "pen", "Pen",                                                 \
        NULL,                                                                 \
        LINE_CORE_OFFSET(normalPenObjPtr),                                    \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_PEN_MASK | LINE_ELEM_MAP_ITEM_MASK                          \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-pixels", "pixels", "Pixels",                                        \
        DEF_LINE_PIXELS,                                                      \
        LINE_BUILTIN_PEN_OFFSET(pixelsObjPtr),                                \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK | LINE_ELEM_MAP_ITEM_MASK                  \
    },                                                                        \
    REDUCE_ENTRY                                                              \
    {                                                                         \
        TK_OPTION_BOOLEAN,                                                    \
        "-scalesymbols", "scaleSymbols", "ScaleSymbols",                      \
        DEF_LINE_SCALE_SYMBOLS,                                               \
        -1,                                                                   \
        LINE_CORE_OFFSET(scaleSymbols),                                       \
        TK_OPTION_DONT_SET_DEFAULT,                                           \
        NULL,                                                                 \
        LINE_ELEM_SCALE_SYMBOL_MASK                                           \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-showerrorbars", "showErrorBars", "ShowErrorBars",                   \
        DEF_LINE_SHOW_ERRORBARS,                                              \
        LINE_BUILTIN_PEN_OFFSET(showErrorBarsObjPtr),                         \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-showvalues", "showValues", "ShowValues",                            \
        DEF_PEN_SHOW_VALUES,                                                  \
        LINE_BUILTIN_PEN_OFFSET(showValuesObjPtr),                            \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-smooth", "smooth", "Smooth",                                        \
        DEF_LINE_SMOOTH,                                                      \
        offsetof(Line, smoothObjPtr),                                         \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_SMOOTH_MASK | LINE_ELEM_MAP_ITEM_MASK                       \
    },                                                                        \
    STATE_ENTRY                                                               \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-styles", "styles", "Styles",                                        \
        DEF_LINE_STYLES,                                                      \
        LINE_CORE_OFFSET(stylesObjPtr),                                       \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_STYLES_MASK | LINE_ELEM_MAP_ITEM_MASK                       \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-symbol", "symbol", "Symbol",                                        \
        DEF_LINE_SYMBOL,                                                      \
        LINE_BUILTIN_PEN_OFFSET(symbolObjPtr),                                \
        -1,                                                                   \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    TRACE_ENTRY                                                               \
    {                                                                         \
        TK_OPTION_ANCHOR,                                                     \
        "-valueanchor", "valueAnchor", "ValueAnchor",                         \
        DEF_PEN_VALUE_ANCHOR,                                                 \
        -1,                                                                   \
        LINE_BUILTIN_PEN_OFFSET(valueStyle.anchor),                           \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_COLOR,                                                      \
        "-valuecolor", "valueColor", "ValueColor",                            \
        DEF_PEN_VALUE_COLOR,                                                  \
        -1,                                                                   \
        LINE_BUILTIN_PEN_OFFSET(valueStyle.color),                            \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_FONT,                                                       \
        "-valuefont", "valueFont", "ValueFont",                               \
        DEF_PEN_VALUE_FONT,                                                   \
        -1,                                                                   \
        LINE_BUILTIN_PEN_OFFSET(valueStyle.font),                             \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-valueformat", "valueFormat", "ValueFormat",                         \
        DEF_PEN_VALUE_FORMAT,                                                 \
        -1,                                                                   \
        LINE_BUILTIN_PEN_OFFSET(valueFormat),                                 \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_DOUBLE,                                                     \
        "-valuerotate", "valueRotate", "ValueRotate",                         \
        "0.0",                                                                \
        LINE_BUILTIN_PEN_OFFSET(valueRotateObjPtr),                           \
        LINE_BUILTIN_PEN_OFFSET(valueStyle.theta),                            \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-valueshadow", "valueShadow", "ValueShadow",                         \
        DEF_PEN_VALUE_SHADOW,                                                 \
        LINE_BUILTIN_PEN_OFFSET(valueShadowObjPtr),                           \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_BUILTIN_PEN_MASK                                            \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-weights", "weights", "Weights",                                     \
        NULL,                                                                 \
        LINE_CORE_OFFSET(weightsObjPtr),                                      \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-x", "xData", "XData",                                               \
        DEF_LINE_X_DATA,                                                      \
        LINE_CORE_OFFSET(xObjPtr),                                            \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_SYNONYM,                                                    \
        "-xdata", NULL, NULL, NULL,                                           \
        -1, -1,                                                               \
        0,                                                                    \
        "-x",                                                                 \
        0                                                                     \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-xerror", "xError", "XError",                                        \
        NULL,                                                                 \
        LINE_CORE_OFFSET(xErrorObjPtr),                                       \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-xhigh", "xHigh", "XHigh",                                           \
        NULL,                                                                 \
        LINE_CORE_OFFSET(xHighObjPtr),                                        \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-xlow", "xLow", "XLow",                                              \
        NULL,                                                                 \
        LINE_CORE_OFFSET(xLowObjPtr),                                         \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-y", "yData", "YData",                                               \
        DEF_LINE_Y_DATA,                                                      \
        LINE_CORE_OFFSET(yObjPtr),                                            \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_SYNONYM,                                                    \
        "-ydata", NULL, NULL, NULL,                                           \
        -1, -1,                                                               \
        0,                                                                    \
        "-y",                                                                 \
        0                                                                     \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-yerror", "yError", "YError",                                        \
        NULL,                                                                 \
        LINE_CORE_OFFSET(yErrorObjPtr),                                       \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-yhigh", "yHigh", "YHigh",                                           \
        NULL,                                                                 \
        LINE_CORE_OFFSET(yHighObjPtr),                                        \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    },                                                                        \
    {                                                                         \
        TK_OPTION_STRING,                                                     \
        "-ylow", "yLow", "YLow",                                              \
        NULL,                                                                 \
        LINE_CORE_OFFSET(yLowObjPtr),                                         \
        -1,                                                                   \
        TK_OPTION_NULL_OK,                                                    \
        NULL,                                                                 \
        LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK                         \
    }

static const Tk_OptionSpec lineElemOptionSpecs[] = {
    LINE_ELEMENT_OPTION_ENTRIES(
        LINE_ELEMENT_AREA_OPTION_ENTRIES,
        LINE_ELEMENT_REDUCE_OPTION_ENTRY,
        LINE_ELEMENT_STATE_OPTION_ENTRY,
        LINE_ELEMENT_TRACE_OPTION_ENTRY),

    {
        TK_OPTION_END,
        NULL, NULL, NULL, NULL,
        0, 0, 0,
        NULL,
        0
    }
};

static const Tk_OptionSpec stripElemOptionSpecs[] = {
    LINE_ELEMENT_OPTION_ENTRIES(
        LINE_ELEMENT_NO_OPTION_ENTRIES,
        LINE_ELEMENT_NO_OPTION_ENTRIES,
        LINE_ELEMENT_NO_OPTION_ENTRIES,
        LINE_ELEMENT_NO_OPTION_ENTRIES),

    {
        TK_OPTION_END,
        NULL, NULL, NULL, NULL,
        0, 0, 0,
        NULL,
        0
    }
};

#define LINE_PEN_OPTION_ENTRIES(DEFAULT_COLOR)                         \
    {                                                                 \
        TK_OPTION_COLOR,                                              \
        "-color", "color", "Color",                                   \
        DEFAULT_COLOR,                                                \
        -1,                                                           \
        offsetof(LinePen, traceColor),                                \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-dashes", "dashes", "Dashes",                                \
        DEF_PEN_DASHES,                                               \
        offsetof(LinePen, dashesObjPtr),                              \
        -1,                                                           \
        TK_OPTION_NULL_OK,                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-errorbarcolor", "errorBarColor", "ErrorBarColor",           \
        DEF_LINE_ERRORBAR_COLOR,                                      \
        offsetof(LinePen, errorBarColorObjPtr),                       \
        -1,                                                           \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-errorbarwidth", "errorBarWidth", "ErrorBarWidth",           \
        DEF_LINE_ERRORBAR_LINE_WIDTH,                                 \
        offsetof(LinePen, errorBarWidthObjPtr),                       \
        -1,                                                           \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-errorbarcap", "errorBarCap", "ErrorBarCap",                 \
        DEF_LINE_ERRORBAR_CAP_WIDTH,                                  \
        offsetof(LinePen, errorBarCapObjPtr),                         \
        -1,                                                           \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-fill", "fill", "Fill",                                      \
        DEF_PEN_FILL_COLOR,                                           \
        offsetof(LinePen, fillObjPtr),                                \
        -1,                                                           \
        TK_OPTION_NULL_OK,                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-linewidth", "lineWidth", "LineWidth",                       \
        DEF_PEN_LINE_WIDTH,                                           \
        offsetof(LinePen, lineWidthObjPtr),                           \
        -1,                                                           \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-offdash", "offDash", "OffDash",                             \
        DEF_PEN_OFFDASH_COLOR,                                        \
        offsetof(LinePen, offDashObjPtr),                             \
        -1,                                                           \
        TK_OPTION_NULL_OK,                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-outline", "outline", "Outline",                             \
        DEF_PEN_OUTLINE_COLOR,                                        \
        offsetof(LinePen, outlineObjPtr),                             \
        -1,                                                           \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-outlinewidth", "outlineWidth", "OutlineWidth",              \
        DEF_PEN_OUTLINE_WIDTH,                                        \
        offsetof(LinePen, outlineWidthObjPtr),                        \
        -1,                                                           \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-pixels", "pixels", "Pixels",                                \
        DEF_PEN_PIXELS,                                               \
        offsetof(LinePen, pixelsObjPtr),                              \
        -1,                                                           \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-showerrorbars", "showErrorBars", "ShowErrorBars",           \
        DEF_LINE_SHOW_ERRORBARS,                                      \
        offsetof(LinePen, showErrorBarsObjPtr),                       \
        -1,                                                           \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-showvalues", "showValues", "ShowValues",                    \
        DEF_PEN_SHOW_VALUES,                                          \
        offsetof(LinePen, showValuesObjPtr),                          \
        -1,                                                           \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-symbol", "symbol", "Symbol",                                \
        DEF_PEN_SYMBOL,                                               \
        offsetof(LinePen, symbolObjPtr),                              \
        -1,                                                           \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-type", NULL, NULL,                                          \
        DEF_PEN_TYPE,                                                 \
        -1,                                                           \
        LINE_PEN_CORE_OFFSET(typeId),                                 \
        TK_OPTION_NULL_OK,                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_ANCHOR,                                             \
        "-valueanchor", "valueAnchor", "ValueAnchor",                 \
        DEF_PEN_VALUE_ANCHOR,                                         \
        -1,                                                           \
        offsetof(LinePen, valueStyle.anchor),                         \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_COLOR,                                              \
        "-valuecolor", "valueColor", "ValueColor",                    \
        DEF_PEN_VALUE_COLOR,                                          \
        -1,                                                           \
        offsetof(LinePen, valueStyle.color),                          \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_FONT,                                               \
        "-valuefont", "valueFont", "ValueFont",                       \
        DEF_PEN_VALUE_FONT,                                           \
        -1,                                                           \
        offsetof(LinePen, valueStyle.font),                           \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-valueformat", "valueFormat", "ValueFormat",                 \
        DEF_PEN_VALUE_FORMAT,                                         \
        -1,                                                           \
        offsetof(LinePen, valueFormat),                               \
        TK_OPTION_NULL_OK,                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_DOUBLE,                                             \
        "-valuerotate", "valueRotate", "ValueRotate",                 \
        "0.0",                                                        \
        offsetof(LinePen, valueRotateObjPtr),                         \
        offsetof(LinePen, valueStyle.theta),                          \
        0,                                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_STRING,                                             \
        "-valueshadow", "valueShadow", "ValueShadow",                 \
        DEF_PEN_VALUE_SHADOW,                                         \
        offsetof(LinePen, valueShadowObjPtr),                         \
        -1,                                                           \
        TK_OPTION_NULL_OK,                                            \
        NULL,                                                         \
        0                                                             \
    },                                                                \
    {                                                                 \
        TK_OPTION_END,                                                \
        NULL, NULL, NULL, NULL,                                       \
        0, 0, 0, NULL, 0                                             \
    }

static const Tk_OptionSpec normalLinePenOptionSpecs[] = {LINE_PEN_OPTION_ENTRIES(DEF_PEN_NORMAL_COLOR)};

static const Tk_OptionSpec activeLinePenOptionSpecs[] = {LINE_PEN_OPTION_ENTRIES(DEF_PEN_ACTIVE_COLOR)};

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
static void ClearPalette(Rbc_Chain *palette);
static void InitPen(LinePen *penPtr, const Tk_OptionSpec *optionSpecs, unsigned int flags);
static int ScaleSymbol(Element *elemPtr, int normalSize);
static void GetScreenPoints(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void ReducePoints(MapInfo *mapPtr, double tolerance);
static void GenerateSteps(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void GenerateSpline(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void GenerateParametricSpline(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void MapSymbols(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void MapActiveSymbols(Graph *graphPtr, Line *linePtr);
static void MapStrip(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void MergePens(Line *linePtr, PenStyle **dataToStyle);
INLINE static int OutCode(Extents2D *extsPtr, Point2D *p);
static int ClipSegment(Extents2D *extsPtr, register int code1, register int code2, register Point2D *p,
                       register Point2D *q);
static void SaveTrace(Line *linePtr, Tcl_Size start, Tcl_Size length, MapInfo *mapPtr);
static void FreeTraces(Line *linePtr);
static void MapTraces(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void MapFillArea(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void ResetLine(Line *linePtr);
static int ClosestTrace(Graph *graphPtr, Line *linePtr, ClosestSearch *searchPtr, DistanceProc *distProc);
static int ClosestStrip(Graph *graphPtr, Line *linePtr, ClosestSearch *searchPtr, DistanceProc *distProc);
static void ClosestPoint(Line *linePtr, ClosestSearch *searchPtr);
static void DrawCircles(Display *display, Drawable drawable, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts,
                        Point2D *symbolPts, int radius);
static void DrawSquares(Display *display, Drawable drawable, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts,
                        register Point2D *symbolPts, int r);
static void DrawSymbols(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, int size,
                        Tcl_Size nSymbolPts, Point2D *symbolPts);
static void DrawTraces(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr);
static void DrawValues(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts,
                       Point2D *symbolPts, const Tcl_Size *pointToData);
static void GetSymbolPostScriptInfo(Graph *graphPtr, PsToken psToken, LinePen *penPtr, int size);
static void SymbolsToPostScript(Graph *graphPtr, PsToken psToken, LinePen *penPtr, int size, Tcl_Size nSymbolPts,
                                Point2D *symbolPts);
static void SetLineAttributes(PsToken psToken, LinePen *penPtr);
static void TracesToPostScript(PsToken psToken, Line *linePtr, LinePen *penPtr);
static void ValuesToPostScript(PsToken psToken, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts, Point2D *symbolPts,
                               const Tcl_Size *pointToData);
static int GetDrawablePolygonPointCount(Display *display, Tcl_Size nPoints);

#ifdef WIN32
MODULE_SCOPE const int tkpWinRopModes[];
#endif

static int IsLinePenPrefix(const char *string, Tcl_Size length, const char *fullName) {
    Tcl_Size fullLength;

    fullLength = (Tcl_Size)strlen(fullName);

    return ((length > 0) && (length <= fullLength) && (strncmp(string, fullName, (size_t)length) == 0));
}

static int GetLinePenColorFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, int allowNull,
                                  XColor **colorPtrPtr) {
    const char *string;
    Tcl_Size length;
    XColor *colorPtr;

    if ((objPtr == NULL) || (Tcl_GetCharLength(objPtr) == 0)) {
        if (allowNull) {
            *colorPtrPtr = NULL;
            return TCL_OK;
        }

        Tcl_SetObjResult(interp, Tcl_NewStringObj("color value may not be empty", -1));

        return TCL_ERROR;
    }

    string = Tcl_GetStringFromObj(objPtr, &length);

    if (IsLinePenPrefix(string, length, "defcolor")) {
        *colorPtrPtr = COLOR_DEFAULT;
        return TCL_OK;
    }

    colorPtr = Tk_GetColor(interp, tkwin, Tk_GetUid(string));

    if (colorPtr == NULL) {
        return TCL_ERROR;
    }

    *colorPtrPtr = colorPtr;
    return TCL_OK;
}

static void FreeLinePenColor(XColor *colorPtr) {
    if ((colorPtr != NULL) && (colorPtr != COLOR_DEFAULT)) {
        Tk_FreeColor(colorPtr);
    }
}

static int GetDrawablePolygonPointCount(Display *display, Tcl_Size nPoints) {
    if ((nPoints < 3) || (nPoints > (Tcl_Size)INT_MAX)) {
        return 0;
    }
#ifndef WIN32
    {
        int maxPoints;
        maxPoints = Rbc_MaxRequestSize(display, sizeof(XPoint));
        if ((maxPoints < 3) || (nPoints > (Tcl_Size)maxPoints)) {
            return 0;
        }
    }
#else
    (void)display;
#endif
    return (int)nPoints;
}

static int GetLineArrayByteCount(Tcl_Size count, size_t elementSize, size_t *bytesPtr) {
    if ((count < 0) || (elementSize == 0) || ((uintmax_t)count > (uintmax_t)(SIZE_MAX / elementSize))) {
        return TCL_ERROR;
    }
    *bytesPtr = (size_t)count * elementSize;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GetLineScalarOption --
 *
 *      Determines whether an option represents one of the
 *      line-specific scalar options handled transactionally.
 *
 *      Tk_SetOptions has already rejected ambiguous or unknown option
 *      abbreviations. This function recovers the canonical identity
 *      from the original option/value vector.
 *
 *----------------------------------------------------------------------
 */
static LineScalarOption GetLineScalarOption(Element *elemPtr, Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, elemPtr->optionSpecs);
    if (name == NULL) {
        return LINE_SCALAR_OPTION_NONE;
    }
    if (strcmp(name, "-maxsymbols") == 0) {
        return LINE_SCALAR_OPTION_MAX_SYMBOLS;
    }
    if (strcmp(name, "-smooth") == 0) {
        return LINE_SCALAR_OPTION_SMOOTH;
    }
    if (strcmp(name, "-trace") == 0) {
        return LINE_SCALAR_OPTION_TRACE;
    }
    return LINE_SCALAR_OPTION_NONE;
}

/*
 *----------------------------------------------------------------------
 *
 * GetLineAreaOption --
 *
 *      Determines whether an option represents a manually converted
 *      line-area option.
 *
 *      Tk_SetOptions has already rejected ambiguous and unknown option
 *      abbreviations. This helper recovers the canonical option
 *      identity from the original option/value vector.
 *
 *----------------------------------------------------------------------
 */
static LineAreaOption GetLineAreaOption(Element *elemPtr, Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, elemPtr->optionSpecs);
    if (name == NULL) {
        return LINE_AREA_OPTION_NONE;
    }
    if (strcmp(name, "-areapattern") == 0) {
        return LINE_AREA_OPTION_PATTERN;
    }
    if (strcmp(name, "-areatile") == 0) {
        return LINE_AREA_OPTION_TILE;
    }
    return LINE_AREA_OPTION_NONE;
}

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
 * GetPatternFromString --
 *
 *      Parses an area-pattern name and acquires any required bitmap
 *      resource without modifying the live line element.
 *
 *----------------------------------------------------------------------
 */
static int GetPatternFromString(Tcl_Interp *interp, Tk_Window tkwin, const char *string, Pixmap *stipplePtr) {
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

    *stipplePtr = stipple;

    return TCL_OK;
}

static int GetPatternFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, Pixmap *stipplePtr) {
    const char *string;

    string = (objPtr == NULL) ? NULL : Tcl_GetString(objPtr);

    return GetPatternFromString(interp, tkwin, string, stipplePtr);
}

static void FreeLinePattern(Display *display, Pixmap stipple) {
    if ((stipple != None) && (stipple != PATTERN_SOLID)) {
        Tk_FreeBitmap(display, stipple);
    }
}

static int StageLineAreaPattern(Graph *graphPtr, Tcl_Obj *objPtr, LineAreaTransaction *transactionPtr) {
    Pixmap newStipple;
    unsigned int mask;

    if (GetPatternFromObj(graphPtr->interp, graphPtr->tkwin, objPtr, &newStipple) != TCL_OK) {
        return TCL_ERROR;
    }

    mask = LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_PATTERN);

    /*
     * Resolve the replacement before releasing an earlier staged
     * candidate.
     */
    if (transactionPtr->stagedMask & mask) {
        FreeLinePattern(graphPtr->display, transactionPtr->stipple);
    }

    transactionPtr->stipple = newStipple;
    transactionPtr->stagedMask |= mask;

    return TCL_OK;
}

static int StageLineAreaTile(Graph *graphPtr, Tcl_Obj *objPtr, LineAreaTransaction *transactionPtr) {
    const char *name;
    Rbc_Tile newTile;
    unsigned int mask;

    newTile = NULL;

    if (objPtr != NULL) {
        name = Tcl_GetString(objPtr);

        if (name[0] != '\0') {
            if (Rbc_GetTile(graphPtr->interp, graphPtr->tkwin, name, &newTile) != TCL_OK) {
                return TCL_ERROR;
            }
        }
    }

    mask = LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_TILE);

    /*
     * Acquire the replacement before releasing an earlier staged tile.
     */
    if ((transactionPtr->stagedMask & mask) && (transactionPtr->tile != NULL)) {
        Rbc_FreeTile(transactionPtr->tile);
    }

    transactionPtr->tile = newTile;
    transactionPtr->stagedMask |= mask;

    return TCL_OK;
}

static GC CreateLineFillGC(Graph *graphPtr, Line *linePtr, Pixmap stipple) {
    unsigned long gcMask;
    XGCValues gcValues;

    gcMask = 0;
    memset(&gcValues, 0, sizeof(gcValues));

    if (linePtr->fillFgColor != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = linePtr->fillFgColor->pixel;
    }

    if (linePtr->fillBgColor != NULL) {
        gcMask |= GCBackground;
        gcValues.background = linePtr->fillBgColor->pixel;
    }

    if ((stipple != None) && (stipple != PATTERN_SOLID)) {
        gcMask |= GCStipple | GCFillStyle;

        gcValues.stipple = stipple;
        gcValues.fill_style = (linePtr->fillBgColor == NULL) ? FillStippled : FillOpaqueStippled;
    }

    return Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
}

static void FreeLineAreaTransaction(Graph *graphPtr, LineAreaTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_PATTERN)) {
        FreeLinePattern(graphPtr->display, transactionPtr->stipple);
    }

    if (transactionPtr->tile != NULL) {
        Rbc_FreeTile(transactionPtr->tile);
    }

    if (transactionPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, transactionPtr->gc);
    }

    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * PrepareLineAreaTransaction --
 *
 *      Parses area-pattern and tile options and constructs a
 *      replacement fill GC without modifying the live line element.
 *
 *      Area foreground and background colors are already managed
 *      transactionally by Tk_SetOptions.
 *
 *----------------------------------------------------------------------
 */
static int PrepareLineAreaTransaction(Graph *graphPtr, Element *elemPtr, Line *linePtr,
                                      LineAreaTransaction *transactionPtr) {
    unsigned int explicitMask;
    Pixmap effectiveStipple;
    int i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));

    explicitMask = 0;

    assert((elemPtr->optionObjc & 1) == 0);

    /*
     * Determine which manually converted area options were supplied
     * explicitly.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        LineAreaOption option;

        option = GetLineAreaOption(elemPtr, elemPtr->optionObjv[i]);

        if (option != LINE_AREA_OPTION_NONE) {
            explicitMask |= LINE_AREA_OPTION_MASK(option);
        }
    }

    /*
     * During initial modern configuration, process effective defaults
     * and option-database values not explicitly overridden.
     */
    if (!elemPtr->optionsConfigured) {
        if (!(explicitMask & LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_PATTERN)) && (linePtr->areaPatternObjPtr != NULL)) {
            if (StageLineAreaPattern(graphPtr, linePtr->areaPatternObjPtr, transactionPtr) != TCL_OK) {
                goto error;
            }
        }

        if (!(explicitMask & LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_TILE)) && (linePtr->areaTileObjPtr != NULL)) {
            if (StageLineAreaTile(graphPtr, linePtr->areaTileObjPtr, transactionPtr) != TCL_OK) {
                goto error;
            }
        }
    }

    /*
     * Process explicit occurrences in their original order.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        LineAreaOption option;
        Tcl_Obj *valueObjPtr;

        option = GetLineAreaOption(elemPtr, elemPtr->optionObjv[i]);

        valueObjPtr = elemPtr->optionObjv[i + 1];

        switch (option) {
        case LINE_AREA_OPTION_PATTERN:
            if (StageLineAreaPattern(graphPtr, valueObjPtr, transactionPtr) != TCL_OK) {
                goto error;
            }
            break;

        case LINE_AREA_OPTION_TILE:
            if (StageLineAreaTile(graphPtr, valueObjPtr, transactionPtr) != TCL_OK) {
                goto error;
            }
            break;

        case LINE_AREA_OPTION_NONE:
            break;
        }
    }

    /*
     * Build the replacement GC from the effective stipple and the
     * colors currently installed by Tk_SetOptions.
     */
    if (transactionPtr->stagedMask & LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_PATTERN)) {
        effectiveStipple = transactionPtr->stipple;
    } else {
        effectiveStipple = linePtr->fillStipple;
    }

    transactionPtr->gc = CreateLineFillGC(graphPtr, linePtr, effectiveStipple);

    return TCL_OK;

error:
    FreeLineAreaTransaction(graphPtr, transactionPtr);

    return TCL_ERROR;
}

static void CommitLineAreaTransaction(Graph *graphPtr, Line *linePtr, LineAreaTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_PATTERN)) {
        Pixmap oldStipple;

        oldStipple = linePtr->fillStipple;

        linePtr->fillStipple = transactionPtr->stipple;

        transactionPtr->stipple = None;

        FreeLinePattern(graphPtr->display, oldStipple);
    }

    if (transactionPtr->stagedMask & LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_TILE)) {
        Rbc_Tile oldTile;

        oldTile = linePtr->fillTile;

        linePtr->fillTile = transactionPtr->tile;

        transactionPtr->tile = NULL;

        if (linePtr->fillTile != NULL) {
            Rbc_SetTileChangedProc(linePtr->fillTile, TileChangedProc, linePtr);
        }

        if (oldTile != NULL) {
            Rbc_FreeTile(oldTile);
        }
    } else if (linePtr->fillTile != NULL) {
        /*
         * Ensure that an existing tile also has its callback after the
         * initial modern configuration.
         */
        Rbc_SetTileChangedProc(linePtr->fillTile, TileChangedProc, linePtr);
    }

    if (transactionPtr->gc != NULL) {
        GC oldGC;

        oldGC = linePtr->fillGC;

        linePtr->fillGC = transactionPtr->gc;

        transactionPtr->gc = NULL;

        if (oldGC != NULL) {
            Tk_FreeGC(graphPtr->display, oldGC);
        }
    }

    transactionPtr->stagedMask = 0;
}

static void FreeParsedSymbol(Display *display, ParsedSymbol *symbolPtr) {
    if (symbolPtr->bitmap != None) {
        Tk_FreeBitmap(display, symbolPtr->bitmap);

        symbolPtr->bitmap = None;
    }

    if (symbolPtr->mask != None) {
        Tk_FreeBitmap(display, symbolPtr->mask);

        symbolPtr->mask = None;
    }
}

static int GetSymbolFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, ParsedSymbol *symbolPtr) {
    const char *string;
    Tcl_Size length;

    symbolPtr->type = SYMBOL_NONE;
    symbolPtr->bitmap = None;
    symbolPtr->mask = None;

    string = Tcl_GetStringFromObj(objPtr, &length);

    if (length == 0) {
        return TCL_OK;
    }

    if (IsLinePenPrefix(string, length, "none")) {
        symbolPtr->type = SYMBOL_NONE;
        return TCL_OK;
    }

    if ((length > 1) && IsLinePenPrefix(string, length, "circle")) {
        symbolPtr->type = SYMBOL_CIRCLE;
        return TCL_OK;
    }

    if ((length > 1) && IsLinePenPrefix(string, length, "square")) {
        symbolPtr->type = SYMBOL_SQUARE;
        return TCL_OK;
    }

    if (IsLinePenPrefix(string, length, "diamond")) {
        symbolPtr->type = SYMBOL_DIAMOND;
        return TCL_OK;
    }

    if (IsLinePenPrefix(string, length, "plus")) {
        symbolPtr->type = SYMBOL_PLUS;
        return TCL_OK;
    }

    if ((length > 1) && IsLinePenPrefix(string, length, "cross")) {
        symbolPtr->type = SYMBOL_CROSS;
        return TCL_OK;
    }

    if ((length > 1) && IsLinePenPrefix(string, length, "splus")) {
        symbolPtr->type = SYMBOL_SPLUS;
        return TCL_OK;
    }

    if ((length > 1) && IsLinePenPrefix(string, length, "scross")) {
        symbolPtr->type = SYMBOL_SCROSS;
        return TCL_OK;
    }

    if (IsLinePenPrefix(string, length, "triangle")) {
        symbolPtr->type = SYMBOL_TRIANGLE;
        return TCL_OK;
    }

    if (IsLinePenPrefix(string, length, "arrow")) {
        symbolPtr->type = SYMBOL_ARROW;
        return TCL_OK;
    }

    /*
     * Otherwise, interpret the value as:
     *
     *     bitmap ?mask?
     */
    {
        Tcl_Obj **objv;
        Tcl_Size objc;
        Pixmap bitmap;
        Pixmap mask;

        if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
            return TCL_ERROR;
        }

        if ((objc < 1) || (objc > 2)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad symbol \"%s\": should be a symbol name "
                                                   "or \"bitmap ?mask?\"",
                                                   string));

            return TCL_ERROR;
        }

        bitmap = Tk_GetBitmap(interp, tkwin, Tk_GetUid(Tcl_GetString(objv[0])));

        if (bitmap == None) {
            Tcl_ResetResult(interp);

            Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad symbol \"%s\": should be "
                                                   "\"none\", \"circle\", \"square\", "
                                                   "\"diamond\", \"plus\", \"cross\", "
                                                   "\"splus\", \"scross\", \"triangle\", "
                                                   "\"arrow\", or the name of a bitmap",
                                                   string));

            return TCL_ERROR;
        }

        mask = None;

        if ((objc == 2) && (Tcl_GetCharLength(objv[1]) > 0)) {
            mask = Tk_GetBitmap(interp, tkwin, Tk_GetUid(Tcl_GetString(objv[1])));

            if (mask == None) {
                Tk_FreeBitmap(Tk_Display(tkwin), bitmap);

                return TCL_ERROR;
            }
        }

        symbolPtr->type = SYMBOL_BITMAP;
        symbolPtr->bitmap = bitmap;
        symbolPtr->mask = mask;
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * GetSmoothFromString --
 *
 *      Parses a line smoothing name without modifying a widget
 *      record.
 *
 *----------------------------------------------------------------------
 */
static int GetSmoothFromString(Tcl_Interp *interp, const char *string, Smoothing *valuePtr) {
    SmoothingInfo *siPtr;

    for (siPtr = smoothingInfo; siPtr->name != NULL; siPtr++) {
        if (strcmp(string, siPtr->name) == 0) {
            *valuePtr = siPtr->value;
            return TCL_OK;
        }
    }

    Rbc_AppendResultStrings(interp, "bad smooth value \"", string, "\": should be linear, step, natural, or quadratic",
                     (char *)NULL);

    return TCL_ERROR;
}

static int GetSmoothFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Smoothing *valuePtr) {
    return GetSmoothFromString(interp, Tcl_GetString(objPtr), valuePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * GetPenDirFromString --
 *
 *      Parses a trace-direction value without modifying a widget
 *      record.
 *
 *----------------------------------------------------------------------
 */
static int GetPenDirFromString(Tcl_Interp *interp, const char *string, int *penDirPtr) {
    size_t length;
    char c;

    c = string[0];
    length = strlen(string);

    if ((c == 'i') && (length <= strlen("increasing")) && (strncmp(string, "increasing", length) == 0)) {
        *penDirPtr = PEN_INCREASING;
    } else if ((c == 'd') && (length <= strlen("decreasing")) && (strncmp(string, "decreasing", length) == 0)) {
        *penDirPtr = PEN_DECREASING;
    } else if ((c == 'b') && (length <= strlen("both")) && (strncmp(string, "both", length) == 0)) {
        *penDirPtr = PEN_BOTH_DIRECTIONS;
    } else {
        Rbc_AppendResultStrings(interp, "bad trace value \"", string,
                         "\" : should be \"increasing\", \"decreasing\", or \"both\"", (char *)NULL);

        return TCL_ERROR;
    }

    return TCL_OK;
}

static int GetPenDirFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *penDirPtr) {
    return GetPenDirFromString(interp, Tcl_GetString(objPtr), penDirPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * StageLineScalarOption --
 *
 *      Parses one line-specific scalar option into temporary
 *      transaction storage without modifying the live line element.
 *
 *----------------------------------------------------------------------
 */
static int StageLineScalarOption(Graph *graphPtr, Tcl_Obj *objPtr, LineScalarOption option,
                                 LineScalarTransaction *transactionPtr) {
    int intValue;
    Smoothing smoothValue;

    switch (option) {
    case LINE_SCALAR_OPTION_MAX_SYMBOLS:
        if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, objPtr, PIXELS_NONNEGATIVE, &intValue) != TCL_OK) {
            return TCL_ERROR;
        }

        transactionPtr->maxSymbols = intValue;
        break;

    case LINE_SCALAR_OPTION_SMOOTH:
        if (GetSmoothFromObj(graphPtr->interp, objPtr, &smoothValue) != TCL_OK) {
            return TCL_ERROR;
        }

        transactionPtr->smooth = smoothValue;
        break;

    case LINE_SCALAR_OPTION_TRACE:
        if (GetPenDirFromObj(graphPtr->interp, objPtr, &intValue) != TCL_OK) {
            return TCL_ERROR;
        }

        transactionPtr->penDir = intValue;
        break;

    case LINE_SCALAR_OPTION_NONE:
    default:
        Tcl_Panic("StageLineScalarOption called with invalid option");

        return TCL_ERROR;
    }

    transactionPtr->stagedMask |= LINE_SCALAR_OPTION_MASK(option);

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * PrepareLineScalarTransaction --
 *
 *      Parses the line-specific scalar options involved in the current
 *      modern configuration without modifying the live element.
 *
 *      Explicit repeated occurrences are processed in caller order, so
 *      an invalid earlier occurrence is not hidden by a valid final
 *      retained object value.
 *
 *----------------------------------------------------------------------
 */
static int PrepareLineScalarTransaction(Graph *graphPtr, Element *elemPtr, Line *linePtr,
                                        LineScalarTransaction *transactionPtr) {
    unsigned int explicitMask;
    int i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));

    explicitMask = 0;

    assert((elemPtr->optionObjc & 1) == 0);

    /*
     * Determine which scalar options were explicitly supplied.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        LineScalarOption option;

        option = GetLineScalarOption(elemPtr, elemPtr->optionObjv[i]);

        if (option != LINE_SCALAR_OPTION_NONE) {
            explicitMask |= LINE_SCALAR_OPTION_MASK(option);
        }
    }

    /*
     * During the initial modern configuration, parse effective default
     * and option-database values that were not explicitly overridden.
     */
    if (!elemPtr->optionsConfigured) {
        if (!(explicitMask & LINE_SCALAR_OPTION_MASK(LINE_SCALAR_OPTION_MAX_SYMBOLS)) &&
            (linePtr->maxSymbolsObjPtr != NULL)) {
            if (StageLineScalarOption(graphPtr, linePtr->maxSymbolsObjPtr, LINE_SCALAR_OPTION_MAX_SYMBOLS,
                                      transactionPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }

        if (!(explicitMask & LINE_SCALAR_OPTION_MASK(LINE_SCALAR_OPTION_SMOOTH)) && (linePtr->smoothObjPtr != NULL)) {
            if (StageLineScalarOption(graphPtr, linePtr->smoothObjPtr, LINE_SCALAR_OPTION_SMOOTH, transactionPtr) !=
                TCL_OK) {
                return TCL_ERROR;
            }
        }

        /*
         * Strip elements have no -trace table entry, so traceObjPtr
         * remains NULL for them.
         */
        if (!(explicitMask & LINE_SCALAR_OPTION_MASK(LINE_SCALAR_OPTION_TRACE)) && (linePtr->traceObjPtr != NULL)) {
            if (StageLineScalarOption(graphPtr, linePtr->traceObjPtr, LINE_SCALAR_OPTION_TRACE, transactionPtr) !=
                TCL_OK) {
                return TCL_ERROR;
            }
        }
    }

    /*
     * Process explicit occurrences in their original order.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        LineScalarOption option;

        option = GetLineScalarOption(elemPtr, elemPtr->optionObjv[i]);

        if (option == LINE_SCALAR_OPTION_NONE) {
            continue;
        }

        if (StageLineScalarOption(graphPtr, elemPtr->optionObjv[i + 1], option, transactionPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CommitLineScalarTransaction --
 *
 *      Commits successfully parsed line-specific scalar values.
 *
 *----------------------------------------------------------------------
 */
static void CommitLineScalarTransaction(Line *linePtr, LineScalarTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & LINE_SCALAR_OPTION_MASK(LINE_SCALAR_OPTION_MAX_SYMBOLS)) {
        linePtr->reqMaxSymbols = transactionPtr->maxSymbols;
    }

    if (transactionPtr->stagedMask & LINE_SCALAR_OPTION_MASK(LINE_SCALAR_OPTION_SMOOTH)) {
        linePtr->reqSmooth = transactionPtr->smooth;
    }

    if (transactionPtr->stagedMask & LINE_SCALAR_OPTION_MASK(LINE_SCALAR_OPTION_TRACE)) {
        linePtr->penDir = transactionPtr->penDir;
    }

    transactionPtr->stagedMask = 0;
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
    LinePen *lpPtr;
    ParsedSymbol newSymbol;
    Rbc_Dashes newDashes;
    Shadow newShadow;
    XColor *newErrorBarColor;
    XColor *newFillColor;
    XColor *newOffDashColor;
    XColor *newOutlineColor;
    XColor *colorPtr;
    int newErrorBarLineWidth;
    int newErrorBarCapWidth;
    int newTraceWidth;
    int newOutlineWidth;
    int newSymbolSize;
    int newErrorBarShow;
    int newValueShow;
    GC newOutlineGC;
    GC newFillGC;
    GC newTraceGC;
    GC newErrorBarGC;
    GC newValueGC;
    XGCValues gcValues;
    unsigned long gcMask;
    assert(penPtr->optionSpecs != NULL);
    lpPtr = LINE_PEN_FROM_CORE(penPtr);
    newErrorBarColor = NULL;
    newFillColor = NULL;
    newOffDashColor = NULL;
    newOutlineColor = NULL;
    newShadow.color = NULL;
    newShadow.offset = 0;
    newSymbol.type = SYMBOL_NONE;
    newSymbol.bitmap = None;
    newSymbol.mask = None;
    newOutlineGC = NULL;
    newFillGC = NULL;
    newTraceGC = NULL;
    newErrorBarGC = NULL;
    newValueGC = NULL;

    if (Rbc_ValidateValueFormat(graphPtr->interp, lpPtr->valueFormat) != TCL_OK) {
        goto error;
    }
    /*
     * Parse all derived fields first.
     */
    if (Rbc_GetDashesFromObj(graphPtr->interp, lpPtr->dashesObjPtr, &newDashes) != TCL_OK) {
        goto error;
    }
    if (GetLinePenColorFromObj(graphPtr->interp, graphPtr->tkwin, lpPtr->errorBarColorObjPtr, FALSE,
                               &newErrorBarColor) != TCL_OK) {
        goto error;
    }
    if (GetLinePenColorFromObj(graphPtr->interp, graphPtr->tkwin, lpPtr->fillObjPtr, TRUE, &newFillColor) != TCL_OK) {
        goto error;
    }
    if (GetLinePenColorFromObj(graphPtr->interp, graphPtr->tkwin, lpPtr->offDashObjPtr, TRUE, &newOffDashColor) !=
        TCL_OK) {
        goto error;
    }
    if (GetLinePenColorFromObj(graphPtr->interp, graphPtr->tkwin, lpPtr->outlineObjPtr, FALSE, &newOutlineColor) !=
        TCL_OK) {
        goto error;
    }
    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, lpPtr->errorBarWidthObjPtr, PIXELS_NONNEGATIVE,
                             &newErrorBarLineWidth) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, lpPtr->errorBarCapObjPtr, PIXELS_NONNEGATIVE,
                             &newErrorBarCapWidth) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, lpPtr->lineWidthObjPtr, PIXELS_NONNEGATIVE,
                             &newTraceWidth) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, lpPtr->outlineWidthObjPtr, PIXELS_NONNEGATIVE,
                             &newOutlineWidth) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, lpPtr->pixelsObjPtr, PIXELS_NONNEGATIVE,
                             &newSymbolSize) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetFillFromObj(graphPtr->interp, lpPtr->showErrorBarsObjPtr, &newErrorBarShow) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetFillFromObj(graphPtr->interp, lpPtr->showValuesObjPtr, &newValueShow) != TCL_OK) {
        goto error;
    }
    if (Rbc_GetShadowFromObj(graphPtr->interp, graphPtr->tkwin, lpPtr->valueShadowObjPtr, &newShadow) != TCL_OK) {
        goto error;
    }
    if (GetSymbolFromObj(graphPtr->interp, graphPtr->tkwin, lpPtr->symbolObjPtr, &newSymbol) != TCL_OK) {
        goto error;
    }
    /*
     * Text GC.
     */
    gcMask = GCFont;
    gcValues.font = Tk_FontId(lpPtr->valueStyle.font);
    if (lpPtr->valueStyle.color != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = lpPtr->valueStyle.color->pixel;
    }
    newValueGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    /*
     * Symbol outline GC.
     */
    gcMask = GCLineWidth | GCForeground;
    colorPtr = newOutlineColor;
    if (colorPtr == COLOR_DEFAULT) {
        colorPtr = lpPtr->traceColor;
    }
    gcValues.foreground = colorPtr->pixel;
    gcValues.line_width = LineWidth(newOutlineWidth);
    if (newSymbol.type == SYMBOL_BITMAP) {
        colorPtr = newFillColor;
        if (colorPtr == COLOR_DEFAULT) {
            colorPtr = lpPtr->traceColor;
        }
        if (colorPtr != NULL) {
            gcValues.background = colorPtr->pixel;
            gcMask |= GCBackground;
            if (newSymbol.mask != None) {
                gcValues.clip_mask = newSymbol.mask;
                gcMask |= GCClipMask;
            }
        } else {
            gcValues.clip_mask = newSymbol.bitmap;
            gcMask |= GCClipMask;
        }
    }
    newOutlineGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    /*
     * Symbol fill GC.
     */
    colorPtr = newFillColor;
    if (colorPtr == COLOR_DEFAULT) {
        colorPtr = lpPtr->traceColor;
    }
    if (colorPtr != NULL) {
        gcMask = GCLineWidth | GCForeground;
        gcValues.line_width = LineWidth(newOutlineWidth);
        gcValues.foreground = colorPtr->pixel;
        newFillGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    }
    /*
     * Trace GC.
     */
    gcMask = GCLineWidth | GCForeground | GCLineStyle | GCCapStyle | GCJoinStyle;
    gcValues.cap_style = CapButt;
    gcValues.join_style = JoinRound;
    gcValues.line_style = LineSolid;
    gcValues.line_width = LineWidth(newTraceWidth);
    colorPtr = newOffDashColor;
    if (colorPtr == COLOR_DEFAULT) {
        colorPtr = lpPtr->traceColor;
    }
    if (colorPtr != NULL) {
        gcMask |= GCBackground;
        gcValues.background = colorPtr->pixel;
    }
    gcValues.foreground = lpPtr->traceColor->pixel;
    if (LineIsDashed(newDashes)) {
        gcValues.line_style = (colorPtr == NULL) ? LineOnOffDash : LineDoubleDash;
    }
    newTraceGC = Rbc_GetPrivateGC(graphPtr->tkwin, gcMask, &gcValues);
    if (LineIsDashed(newDashes)) {
        newDashes.offset = newDashes.values[0] / 2;
        Rbc_SetDashes(graphPtr->display, newTraceGC, &newDashes);
    }
    /*
     * Error-bar GC.
     */
    colorPtr = newErrorBarColor;
    if (colorPtr == COLOR_DEFAULT) {
        colorPtr = lpPtr->traceColor;
    }
    gcMask = GCLineWidth | GCForeground;
    gcValues.line_width = LineWidth(newErrorBarLineWidth);
    gcValues.foreground = colorPtr->pixel;
    newErrorBarGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    /*
     * Commit derived colours.
     */
    FreeLinePenColor(lpPtr->errorBarColor);
    FreeLinePenColor(lpPtr->symbol.fillColor);
    FreeLinePenColor(lpPtr->traceOffColor);
    FreeLinePenColor(lpPtr->symbol.outlineColor);
    lpPtr->errorBarColor = newErrorBarColor;
    lpPtr->symbol.fillColor = newFillColor;
    lpPtr->traceOffColor = newOffDashColor;
    lpPtr->symbol.outlineColor = newOutlineColor;
    newErrorBarColor = NULL;
    newFillColor = NULL;
    newOffDashColor = NULL;
    newOutlineColor = NULL;
    /*
     * Commit shadow.
     */
    if (lpPtr->valueStyle.shadow.color != NULL) {
        Tk_FreeColor(lpPtr->valueStyle.shadow.color);
    }
    lpPtr->valueStyle.shadow = newShadow;
    newShadow.color = NULL;
    /*
     * Commit symbol bitmap resources.
     */
    if (lpPtr->symbol.bitmap != None) {
        Tk_FreeBitmap(graphPtr->display, lpPtr->symbol.bitmap);
    }
    if (lpPtr->symbol.mask != None) {
        Tk_FreeBitmap(graphPtr->display, lpPtr->symbol.mask);
    }
    lpPtr->symbol.type = newSymbol.type;
    lpPtr->symbol.bitmap = newSymbol.bitmap;
    lpPtr->symbol.mask = newSymbol.mask;
    newSymbol.bitmap = None;
    newSymbol.mask = None;
    /*
     * Commit scalar derived fields.
     */
    lpPtr->traceDashes = newDashes;
    lpPtr->traceWidth = newTraceWidth;
    lpPtr->errorBarLineWidth = newErrorBarLineWidth;
    lpPtr->errorBarCapWidth = newErrorBarCapWidth;
    lpPtr->symbol.outlineWidth = newOutlineWidth;
    lpPtr->symbol.size = newSymbolSize;
    lpPtr->errorBarShow = newErrorBarShow;
    lpPtr->valueShow = newValueShow;
    /*
     * Commit GCs.
     */
    if (lpPtr->valueStyle.gc != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->valueStyle.gc);
    }
    if (lpPtr->symbol.outlineGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->symbol.outlineGC);
    }
    if (lpPtr->symbol.fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->symbol.fillGC);
    }
    if (lpPtr->traceGC != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, lpPtr->traceGC);
    }
    if (lpPtr->errorBarGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->errorBarGC);
    }
    lpPtr->valueStyle.gc = newValueGC;
    lpPtr->symbol.outlineGC = newOutlineGC;
    lpPtr->symbol.fillGC = newFillGC;
    lpPtr->traceGC = newTraceGC;
    lpPtr->errorBarGC = newErrorBarGC;
    return TCL_OK;

error:
    FreeLinePenColor(newErrorBarColor);
    FreeLinePenColor(newFillColor);
    FreeLinePenColor(newOffDashColor);
    FreeLinePenColor(newOutlineColor);
    if (newShadow.color != NULL) {
        Tk_FreeColor(newShadow.color);
    }
    FreeParsedSymbol(graphPtr->display, &newSymbol);
    return TCL_ERROR;
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
    LinePen *lpPtr;

    lpPtr = LINE_PEN_FROM_CORE(penPtr);

    Rbc_FreeTextStyle(graphPtr->display, &lpPtr->valueStyle);

    lpPtr->valueStyle.gc = NULL;

    if (lpPtr->symbol.outlineGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->symbol.outlineGC);

        lpPtr->symbol.outlineGC = NULL;
    }

    if (lpPtr->symbol.fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->symbol.fillGC);

        lpPtr->symbol.fillGC = NULL;
    }

    if (lpPtr->errorBarGC != NULL) {
        Tk_FreeGC(graphPtr->display, lpPtr->errorBarGC);

        lpPtr->errorBarGC = NULL;
    }

    if (lpPtr->traceGC != NULL) {
        Rbc_FreePrivateGC(graphPtr->display, lpPtr->traceGC);

        lpPtr->traceGC = NULL;
    }

    if (lpPtr->symbol.bitmap != None) {
        Tk_FreeBitmap(graphPtr->display, lpPtr->symbol.bitmap);

        lpPtr->symbol.bitmap = None;
    }

    if (lpPtr->symbol.mask != None) {
        Tk_FreeBitmap(graphPtr->display, lpPtr->symbol.mask);

        lpPtr->symbol.mask = None;
    }

    /*
     * These resources are manually derived from retained option objects
     * and are not released by Tk_FreeConfigOptions.
     */
    FreeLinePenColor(lpPtr->errorBarColor);
    FreeLinePenColor(lpPtr->symbol.fillColor);
    FreeLinePenColor(lpPtr->traceOffColor);
    FreeLinePenColor(lpPtr->symbol.outlineColor);

    lpPtr->errorBarColor = NULL;
    lpPtr->symbol.fillColor = NULL;
    lpPtr->traceOffColor = NULL;
    lpPtr->symbol.outlineColor = NULL;

    if (lpPtr->valueStyle.shadow.color != NULL) {
        Tk_FreeColor(lpPtr->valueStyle.shadow.color);

        lpPtr->valueStyle.shadow.color = NULL;
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
static void InitPen(LinePen *penPtr, const Tk_OptionSpec *optionSpecs, unsigned int flags) {
    Pen *corePtr;

    assert(optionSpecs != NULL);

    corePtr = &penPtr->core;

    Rbc_InitTextStyle(&penPtr->valueStyle);

    corePtr->optionSpecs = optionSpecs;
    corePtr->optionTable = NULL;

    corePtr->optionsInitialized = FALSE;
    corePtr->tkResourcesReleased = FALSE;

    corePtr->configProc = ConfigurePen;
    corePtr->destroyProc = DestroyPen;
    corePtr->flags = flags;
    corePtr->name = "";

    penPtr->errorBarLineWidth = 1;
    penPtr->errorBarShow = SHOW_BOTH;

    penPtr->symbol.bitmap = None;
    penPtr->symbol.mask = None;
    penPtr->symbol.outlineColor = COLOR_DEFAULT;
    penPtr->symbol.fillColor = COLOR_DEFAULT;
    penPtr->symbol.outlineWidth = 1;

    penPtr->traceWidth = 1;
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
Pen *Rbc_LinePen(const char *penName) {
    const Tk_OptionSpec *optionSpecs;
    unsigned int flags;
    LinePen *penPtr;
    Pen *corePtr;

    if (strcmp(penName, "activeLine") == 0) {
        optionSpecs = activeLinePenOptionSpecs;
        flags = ACTIVE_PEN;
    } else {
        optionSpecs = normalLinePenOptionSpecs;
        flags = NORMAL_PEN;
    }

    penPtr = RbcCalloc(1, sizeof(LinePen));
    assert(penPtr != NULL);

    InitPen(penPtr, optionSpecs, flags);

    corePtr = &penPtr->core;
    corePtr->name = RbcStrdup(penName);

    return corePtr;
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
    double scale;
    double scaledSize;
    int maxSize;
    int newSize;

    scale = 1.0;
    if (elemPtr->scaleSymbols) {
        double xRange;
        double yRange;

        xRange = elemPtr->axes.x->max - elemPtr->axes.x->min;
        yRange = elemPtr->axes.y->max - elemPtr->axes.y->min;
        if (elemPtr->flags & SCALE_SYMBOL) {
            /*
             * Save the current ranges as the baseline for future
             * symbol scaling.
             */
            elemPtr->xRange = xRange;
            elemPtr->yRange = yRange;
            elemPtr->flags &= ~SCALE_SYMBOL;
        } else if (FINITE(xRange) && FINITE(yRange) && FINITE(elemPtr->xRange) && FINITE(elemPtr->yRange) &&
                   (xRange > 0.0) && (yRange > 0.0) && (elemPtr->xRange > 0.0) && (elemPtr->yRange > 0.0)) {
            double xScale;
            double yScale;

            /*
             * Scale the symbol by the smaller change in the X and Y
             * axis ranges.
             */
            xScale = elemPtr->xRange / xRange;
            yScale = elemPtr->yRange / yRange;
            scale = MIN(xScale, yScale);
            if ((!FINITE(scale)) || (scale <= 0.0)) {
                scale = 1.0;
            }
        }
    }
    /*
     * The symbol must never exceed the drawable graph range.  Keep
     * the scaling calculation in double and narrow only after the
     * result has been bounded.
     */
    maxSize = MIN(elemPtr->graphPtr->hRange, elemPtr->graphPtr->vRange);
    if (maxSize < 1) {
        maxSize = 1;
    }
    scaledSize = (double)normalSize * scale;
    if ((!FINITE(scaledSize)) || (scaledSize >= (double)maxSize)) {
        newSize = maxSize;
    } else if (scaledSize <= 1.0) {
        newSize = 1;
    } else {
        /*
         * scaledSize is now known to be in the representable int
         * range, because maxSize itself is int.
         */
        newSize = Round(scaledSize);
        if (newSize < 1) {
            newSize = 1;
        } else if (newSize > maxSize) {
            newSize = maxSize;
        }
    }
    /*
     * Make the symbol size odd so its center is a single pixel.
     *
     * The old code did:
     *
     *     newSize |= 0x01;
     *
     * after applying maxSize.  If maxSize was even that could make
     * the final symbol one pixel larger than the graph range.
     */
    if ((newSize & 0x01) == 0) {
        if (newSize < maxSize) {
            newSize++;
        } else if (newSize > 1) {
            newSize--;
        } else {
            newSize = 1;
        }
    }
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
    const double *x;
    const double *y;
    Point2D *screenPts;
    Tcl_Size *indices;
    unsigned char *breakBefore;
    Tcl_Size nDataPoints;
    Tcl_Size nScreenPoints;
    Tcl_Size i;
    Tcl_Size count;
    size_t pointBytes;
    size_t indexBytes;
    int hasBreaks;

    mapPtr->screenPts = NULL;
    mapPtr->indices = NULL;
    mapPtr->breakBefore = NULL;
    mapPtr->nScreenPts = 0;
    nDataPoints = NumberOfPoints(&linePtr->core);
    x = linePtr->core.x.valueArr;
    y = linePtr->core.y.valueArr;
    /*
     * This is only an allocation upper bound.  Some finite raw data
     * may still be outside the domain of a logarithmic axis.
     */
    nScreenPoints = 0;
    for (i = 0; i < nDataPoints; i++) {
        if (FINITE(x[i]) && FINITE(y[i])) {
            nScreenPoints++;
        }
    }
    if (nScreenPoints == 0) {
        return;
    }
    if ((GetLineArrayByteCount(nScreenPoints, sizeof(*screenPts), &pointBytes) != TCL_OK) ||
        (GetLineArrayByteCount(nScreenPoints, sizeof(*indices), &indexBytes) != TCL_OK)) {
        return;
    }
    screenPts = Tcl_AttemptAlloc(pointBytes);
    if (screenPts == NULL) {
        return;
    }
    indices = Tcl_AttemptAlloc(indexBytes);
    if (indices == NULL) {
        ckfree(screenPts);
        return;
    }
    count = 0;
    for (i = 0; i < nDataPoints; i++) {
        Point2D point;

        if ((!FINITE(x[i])) || (!FINITE(y[i]))) {
            continue;
        }
        point = Rbc_Map2D(graphPtr, x[i], y[i], &linePtr->core.axes);
        /*
         * Non-positive data on a logarithmic axis intentionally maps
         * to a non-finite coordinate.  It represents a break in the
         * data rather than a point to draw.
         */
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        screenPts[count] = point;
        indices[count] = i;
        count++;
    }
    if (count == 0) {
        ckfree(screenPts);
        ckfree(indices);
        return;
    }
    /*
     * Detect discontinuities while indices still refer directly to
     * the original data array.  Later smoothing and reduction destroy
     * that simple relationship.
     */
    hasBreaks = FALSE;
    for (i = 1; i < count; i++) {
        if (indices[i] != indices[i - 1] + 1) {
            hasBreaks = TRUE;
            break;
        }
    }
    breakBefore = NULL;
    if (hasBreaks) {
        size_t breakBytes;

        if (GetLineArrayByteCount(count, sizeof(*breakBefore), &breakBytes) != TCL_OK) {
            ckfree(screenPts);
            ckfree(indices);
            return;
        }
        breakBefore = Tcl_AttemptAlloc(breakBytes);
        if (breakBefore == NULL) {
            ckfree(screenPts);
            ckfree(indices);
            return;
        }
        breakBefore[0] = FALSE;
        for (i = 1; i < count; i++) {
            breakBefore[i] = (indices[i] != indices[i - 1] + 1);
        }
    }
    mapPtr->screenPts = screenPts;
    mapPtr->indices = indices;
    mapPtr->breakBefore = breakBefore;
    mapPtr->nScreenPts = count;
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
    Point2D *screenPts;
    Tcl_Size *indices;
    unsigned char *breakBefore;
    Tcl_Size *simple;
    Tcl_Size nPoints;
    Tcl_Size n;
    Tcl_Size i;
    Tcl_Size runStart;
    size_t simpleBytes;
    size_t pointBytes;
    size_t indexBytes;

    if ((mapPtr == NULL) || (mapPtr->screenPts == NULL) || (mapPtr->indices == NULL) || (mapPtr->nScreenPts < 2)) {
        return;
    }
    nPoints = mapPtr->nScreenPts;
    if (GetLineArrayByteCount(nPoints, sizeof(*simple), &simpleBytes) != TCL_OK) {
        return;
    }
    simple = Tcl_AttemptAlloc(simpleBytes);
    if (simple == NULL) {
        return;
    }
    n = 0;
    runStart = 0;
    for (i = 1; i <= nPoints; i++) {
        if ((i == nPoints) || ((mapPtr->breakBefore != NULL) && mapPtr->breakBefore[i])) {
            Tcl_Size runCount;

            runCount = Rbc_SimplifyLine(mapPtr->screenPts, runStart, i - 1, tolerance, simple + n);
            if ((runCount <= 0) || (runCount > nPoints - n)) {
                ckfree(simple);
                return;
            }
            n += runCount;
            runStart = i;
        }
    }
    if (n == nPoints) {
        ckfree(simple);
        return;
    }
    if ((n <= 0) || (GetLineArrayByteCount(n, sizeof(*screenPts), &pointBytes) != TCL_OK) ||
        (GetLineArrayByteCount(n, sizeof(*indices), &indexBytes) != TCL_OK)) {
        ckfree(simple);
        return;
    }
    screenPts = Tcl_AttemptAlloc(pointBytes);
    if (screenPts == NULL) {
        ckfree(simple);
        return;
    }
    indices = Tcl_AttemptAlloc(indexBytes);
    if (indices == NULL) {
        ckfree(screenPts);
        ckfree(simple);
        return;
    }
    breakBefore = NULL;
    if (mapPtr->breakBefore != NULL) {
        size_t breakBytes;

        if (GetLineArrayByteCount(n, sizeof(*breakBefore), &breakBytes) != TCL_OK) {
            ckfree(screenPts);
            ckfree(indices);
            ckfree(simple);
            return;
        }
        breakBefore = Tcl_AttemptAlloc(breakBytes);
        if (breakBefore == NULL) {
            ckfree(screenPts);
            ckfree(indices);
            ckfree(simple);
            return;
        }
    }
    for (i = 0; i < n; i++) {
        Tcl_Size k;

        k = simple[i];
        screenPts[i] = mapPtr->screenPts[k];
        indices[i] = mapPtr->indices[k];
        if (breakBefore != NULL) {
            breakBefore[i] = mapPtr->breakBefore[k];
        }
    }
    ckfree(simple);
    ckfree(mapPtr->screenPts);
    ckfree(mapPtr->indices);
    if (mapPtr->breakBefore != NULL) {
        ckfree(mapPtr->breakBefore);
    }
    mapPtr->screenPts = screenPts;
    mapPtr->indices = indices;
    mapPtr->breakBefore = breakBefore;
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
static void GenerateSteps(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    Point2D *screenPts;
    Tcl_Size *indices;
    unsigned char *breakBefore;
    Tcl_Size i;
    Tcl_Size count;
    Tcl_Size newSize;
    size_t pointBytes;
    size_t indexBytes;

    if (mapPtr->nScreenPts < 2) {
        return;
    }
    if (mapPtr->nScreenPts > (TCL_SIZE_MAX / 2) + 1) {
        linePtr->smooth = PEN_SMOOTH_NONE;
        return;
    }
    newSize = (mapPtr->nScreenPts - 1) * 2 + 1;
    if ((GetLineArrayByteCount(newSize, sizeof(*screenPts), &pointBytes) != TCL_OK) ||
        (GetLineArrayByteCount(newSize, sizeof(*indices), &indexBytes) != TCL_OK)) {
        linePtr->smooth = PEN_SMOOTH_NONE;
        return;
    }
    screenPts = Tcl_AttemptAlloc(pointBytes);
    if (screenPts == NULL) {
        linePtr->smooth = PEN_SMOOTH_NONE;
        return;
    }
    indices = Tcl_AttemptAlloc(indexBytes);
    if (indices == NULL) {
        ckfree(screenPts);
        linePtr->smooth = PEN_SMOOTH_NONE;
        return;
    }
    breakBefore = NULL;
    if (mapPtr->breakBefore != NULL) {
        size_t breakBytes;
        if (GetLineArrayByteCount(newSize, sizeof(*breakBefore), &breakBytes) != TCL_OK) {
            ckfree(screenPts);
            ckfree(indices);
            linePtr->smooth = PEN_SMOOTH_NONE;
            return;
        }
        breakBefore = Tcl_AttemptAlloc(breakBytes);
        if (breakBefore == NULL) {
            ckfree(screenPts);
            ckfree(indices);
            linePtr->smooth = PEN_SMOOTH_NONE;
            return;
        }
    }
    screenPts[0] = mapPtr->screenPts[0];
    indices[0] = mapPtr->indices[0];
    if (breakBefore != NULL) {
        breakBefore[0] = FALSE;
    }
    count = 1;
    for (i = 1; i < mapPtr->nScreenPts; i++) {
        /*
         * Do not manufacture a step segment across a missing-data
         * boundary.
         */
        if ((mapPtr->breakBefore != NULL) && mapPtr->breakBefore[i]) {
            screenPts[count] = mapPtr->screenPts[i];
            indices[count] = mapPtr->indices[i];
            breakBefore[count] = TRUE;
            count++;
            continue;
        }
        /*
         * Step-and-hold changes data X first while holding data Y
         * constant, then changes data Y.
         *
         * Normally data X maps to screen X, so the intermediate
         * point is:
         *
         *     (next.x, previous.y)
         *
         * With -invertxy, data X maps to screen Y instead, so it is:
         *
         *     (previous.x, next.y)
         */
        screenPts[count] = screenPts[count - 1];
        if (graphPtr->inverted) {
            screenPts[count].y = mapPtr->screenPts[i].y;
        } else {
            screenPts[count].x = mapPtr->screenPts[i].x;
        }
        screenPts[count + 1] = mapPtr->screenPts[i];
        indices[count] = mapPtr->indices[i];
        indices[count + 1] = mapPtr->indices[i];
        if (breakBefore != NULL) {
            breakBefore[count] = FALSE;
            breakBefore[count + 1] = FALSE;
        }
        count += 2;
    }
    ckfree(mapPtr->screenPts);
    ckfree(mapPtr->indices);
    if (mapPtr->breakBefore != NULL) {
        ckfree(mapPtr->breakBefore);
    }
    mapPtr->screenPts = screenPts;
    mapPtr->indices = indices;
    mapPtr->breakBefore = breakBefore;
    mapPtr->nScreenPts = count;
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
 *----------------------------------------------------------------------
 */
static void GenerateSpline(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    Tcl_Size extra;
    Tcl_WideInt span;
    Point2D *origPts;
    Point2D *splinePts;
    Point2D *intpPts;
    Tcl_Size *indices;
    Tcl_Size capacity;
    Tcl_Size count;
    Tcl_Size i;
    Tcl_Size j;
    Tcl_Size nOrigPts;
    Tcl_Size nIntpPts;
    size_t splineBytes;
    double plotMin;
    double plotMax;
    double firstU;
    double nextU;
    int reversed;
    int result;
    int sample;

    nOrigPts = mapPtr->nScreenPts;
    origPts = mapPtr->screenPts;
    assert(nOrigPts > 0);
    if (nOrigPts < 3) {
        return;
    }
    /*
     * Natural and quadratic splines are functions of data X.
     *
     * Normally data X maps to screen X.  For an inverted graph it
     * maps to screen Y instead.  Select that screen coordinate as the
     * spline abscissa.
     */
    if (graphPtr->inverted) {
        plotMin = (double)graphPtr->top;
        plotMax = (double)graphPtr->bottom;
        span = (Tcl_WideInt)graphPtr->bottom - (Tcl_WideInt)graphPtr->top + 1;
        firstU = origPts[0].y;
        nextU = origPts[1].y;
    } else {
        plotMin = (double)graphPtr->left;
        plotMax = (double)graphPtr->right;
        span = (Tcl_WideInt)graphPtr->right - (Tcl_WideInt)graphPtr->left + 1;
        firstU = origPts[0].x;
        nextU = origPts[1].x;
    }
    /*
     * The spline routines require strictly increasing abscissas.
     * Screen coordinates may instead decrease when the corresponding
     * axis is descending, or simply because data X is mapped to the
     * vertical screen direction.  Reflect the spline coordinate when
     * necessary rather than reversing the point array.  This preserves
     * data and index order.
     */
    if (nextU > firstU) {
        reversed = FALSE;
    } else if (nextU < firstU) {
        reversed = TRUE;
    } else {
        return;
    }
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {
        double u;
        double v;

        if (graphPtr->inverted) {
            u = origPts[i].y;
            v = origPts[j].y;
        } else {
            u = origPts[i].x;
            v = origPts[j].x;
        }
        if ((!reversed && (v <= u)) || (reversed && (v >= u))) {
            return;
        }
    }
    /*
     * Reflect the visible spline-coordinate interval too, so that both
     * source and interpolation coordinates use the same increasing
     * canonical coordinate system.
     */
    if (reversed) {
        double tmp;

        tmp = plotMin;
        plotMin = -plotMax;
        plotMax = -tmp;
        firstU = -firstU;
        if (graphPtr->inverted) {
            nextU = -origPts[nOrigPts - 1].y;
        } else {
            nextU = -origPts[nOrigPts - 1].x;
        }
    } else {
        if (graphPtr->inverted) {
            nextU = origPts[nOrigPts - 1].y;
        } else {
            nextU = origPts[nOrigPts - 1].x;
        }
    }
    /*
     * The canonical source abscissas are increasing, so this test also
     * covers either screen direction.
     */
    if ((firstU > plotMax) || (nextU < plotMin)) {
        return;
    }
    if (span < 1) {
        return;
    }
    if ((Tcl_WideUInt)span > (Tcl_WideUInt)TCL_SIZE_MAX) {
        linePtr->smooth = PEN_SMOOTH_NONE;
        return;
    }
    extra = (Tcl_Size)span;
    if (nOrigPts > TCL_SIZE_MAX - extra - 1) {
        linePtr->smooth = PEN_SMOOTH_NONE;
        return;
    }
    capacity = nOrigPts + extra + 1;
    if (((Tcl_WideUInt)capacity > (Tcl_WideUInt)(SIZE_MAX / sizeof(*intpPts))) ||
        ((Tcl_WideUInt)capacity > (Tcl_WideUInt)(SIZE_MAX / sizeof(*indices)))) {
        linePtr->smooth = PEN_SMOOTH_NONE;
        return;
    }
    if (GetLineArrayByteCount(nOrigPts, sizeof(*splinePts), &splineBytes) != TCL_OK) {
        linePtr->smooth = PEN_SMOOTH_NONE;
        return;
    }
    splinePts = Tcl_AttemptAlloc(splineBytes);
    if (splinePts == NULL) {
        linePtr->smooth = PEN_SMOOTH_NONE;
        return;
    }
    /*
     * Convert screen coordinates into the canonical spline coordinate
     * system:
     *
     *     spline x = mapped data-X direction
     *     spline y = mapped data-Y direction
     *
     * The spline x coordinate is reflected when necessary so that it
     * is always strictly increasing.
     */
    for (i = 0; i < nOrigPts; i++) {
        if (graphPtr->inverted) {
            splinePts[i].x = origPts[i].y;
            splinePts[i].y = origPts[i].x;
        } else {
            splinePts[i] = origPts[i];
        }
        if (reversed) {
            splinePts[i].x = -splinePts[i].x;
        }
    }
    intpPts = Tcl_AttemptAlloc((size_t)capacity * sizeof(*intpPts));
    if (intpPts == NULL) {
        ckfree(splinePts);
        linePtr->smooth = PEN_SMOOTH_NONE;
        return;
    }
    indices = Tcl_AttemptAlloc((size_t)capacity * sizeof(*indices));
    if (indices == NULL) {
        ckfree(intpPts);
        ckfree(splinePts);
        linePtr->smooth = PEN_SMOOTH_NONE;
        return;
    }
    /*
     * Populate the interpolation abscissas with every original knot
     * plus one sample for each intervening visible screen pixel along
     * the mapped data-X direction.
     */
    count = 0;
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {
        intpPts[count].x = splinePts[i].x;
        indices[count] = mapPtr->indices[i];
        count++;
        /*
         * Does any part of this interval intersect the plotting area
         * along the spline coordinate?
         */
        if ((splinePts[j].x >= plotMin) && (splinePts[i].x <= plotMax)) {

            double first;
            double limit;
            int last;

            /*
             * Clip in floating point before converting to int.
             */
            first = splinePts[i].x + 1.0;
            if (first < plotMin) {
                first = plotMin;
            } else if (first > plotMax) {
                first = plotMax;
            }
            limit = splinePts[j].x;
            if (limit < plotMin) {
                limit = plotMin;
            } else if (limit > plotMax) {
                limit = plotMax;
            }
            sample = (int)first;
            last = (int)limit;
            while (sample < last) {
                indices[count] = mapPtr->indices[i];
                intpPts[count].x = (double)sample;
                count++;
                sample++;
            }
        }
    }
    /*
     * The interval loop retains each interval's first knot.  Append the
     * final original knot explicitly.
     */
    if (count >= capacity) {
        linePtr->smooth = PEN_SMOOTH_NONE;
        ckfree(intpPts);
        ckfree(indices);
        ckfree(splinePts);
        return;
    }
    intpPts[count].x = splinePts[nOrigPts - 1].x;
    indices[count] = mapPtr->indices[nOrigPts - 1];
    count++;
    nIntpPts = count;
    result = FALSE;
    if (linePtr->smooth == PEN_SMOOTH_NATURAL) {
        result = Rbc_NaturalSpline(splinePts, nOrigPts, intpPts, nIntpPts);
    } else if (linePtr->smooth == PEN_SMOOTH_QUADRATIC) {
        result = Rbc_QuadraticSpline(splinePts, nOrigPts, intpPts, nIntpPts);
    }
    ckfree(splinePts);
    if (!result) {
        /*
         * The spline interpolation failed.  Fall back to the original
         * coordinates and ordinary line segments.
         */
        linePtr->smooth = PEN_SMOOTH_NONE;
        ckfree(intpPts);
        ckfree(indices);
        return;
    }
    /*
     * Transform the generated canonical spline coordinates back into
     * actual screen coordinates.
     */
    for (i = 0; i < nIntpPts; i++) {
        double u;
        double v;

        u = intpPts[i].x;
        v = intpPts[i].y;
        if (reversed) {
            u = -u;
        }
        if (graphPtr->inverted) {
            intpPts[i].x = v;
            intpPts[i].y = u;
        } else {
            intpPts[i].x = u;
            intpPts[i].y = v;
        }
    }
    ckfree(mapPtr->screenPts);
    ckfree(mapPtr->indices);
    mapPtr->indices = indices;
    mapPtr->screenPts = intpPts;
    mapPtr->nScreenPts = nIntpPts;
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
 *----------------------------------------------------------------------
 */
static void GenerateParametricSpline(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    Extents2D exts;
    Point2D *origPts;
    Point2D *intpPts;
    Tcl_Size *indices;
    Tcl_Size nOrigPts;
    Tcl_Size nIntpPts;
    Tcl_Size capacity;
    Tcl_Size count;
    Tcl_Size i;
    Tcl_Size j;
    int result;

    assert(mapPtr->nScreenPts > 0);
    origPts = mapPtr->screenPts;
    nOrigPts = mapPtr->nScreenPts;
    intpPts = NULL;
    indices = NULL;
    Rbc_GraphExtents(graphPtr, &exts);

    /*
     * Every original point is retained. Additional points are generated
     * at approximately two-pixel intervals along the visible portion of
     * each segment.
     */
    capacity = nOrigPts;
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {
        Segment2D clipped;
        double dist;

        if (Rbc_LineRectClip(&exts, &origPts[i], &origPts[j], &clipped)) {
            double distP;
            double distQ;

            distP = hypot(clipped.p.x - origPts[i].x, clipped.p.y - origPts[i].y);
            distQ = hypot(clipped.q.x - origPts[i].x, clipped.q.y - origPts[i].y);
            /*
             * The first additional sample is two pixels from the clipped
             * starting point.
             */
            distP += 2.0;
            if (distP <= distQ) {
                double extraValue;
                Tcl_Size extra;
                extraValue = floor((distQ - distP) / 2.0) + 1.0;
                if (!FINITE(extraValue) || (extraValue > (double)(TCL_SIZE_MAX - capacity))) {
                    goto fallback;
                }
                extra = (Tcl_Size)extraValue;
                capacity += extra;
            }
        }
    }
    if (((size_t)capacity > (SIZE_MAX / sizeof(*intpPts))) || ((size_t)capacity > (SIZE_MAX / sizeof(*indices)))) {
        goto fallback;
    }
    intpPts = Tcl_AttemptAlloc((size_t)capacity * sizeof(*intpPts));
    if (intpPts == NULL) {
        goto fallback;
    }
    indices = Tcl_AttemptAlloc((size_t)capacity * sizeof(*indices));
    if (indices == NULL) {
        goto fallback;
    }
    /*
     * Store Catmull-Rom evaluation parameters in intpPts:
     *
     *     x = source interval
     *     y = parameter t within that interval
     *
     * Rbc_CatromParametricSpline() replaces these parameters with the
     * corresponding interpolated screen coordinates.
     */
    count = 0;
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {
        Segment2D clipped;
        double dist;

        if (count >= capacity) {
            goto fallback;
        }
        /*
         * Retain the original point at the beginning of this interval.
         */
        intpPts[count].x = (double)i;
        intpPts[count].y = 0.0;
        indices[count] = mapPtr->indices[i];
        count++;
        dist = hypot(origPts[j].x - origPts[i].x, origPts[j].y - origPts[i].y);
        if ((!FINITE(dist)) || (dist <= DBL_EPSILON)) {
            continue;
        }
        if (Rbc_LineRectClip(&exts, &origPts[i], &origPts[j], &clipped)) {
            double distP;
            double distQ;

            distP = hypot(clipped.p.x - origPts[i].x, clipped.p.y - origPts[i].y);
            distQ = hypot(clipped.q.x - origPts[i].x, clipped.q.y - origPts[i].y);
            /*
             * Insert interpolated points every two screen pixels over
             * the visible portion of the source interval.
             */
            distP += 2.0;
            while (distP <= distQ) {
                if (count >= capacity) {
                    goto fallback;
                }
                /*
                 * Identify the point by its source interval and its
                 * normalized position within that interval.
                 */
                intpPts[count].x = (double)i;
                intpPts[count].y = distP / dist;
                /*
                 * Interpolated points map to the source point beginning
                 * the interval.
                 */
                indices[count] = mapPtr->indices[i];
                count++;
                distP += 2.0;
            }
        }
    }
    /*
     * Append the final original point.
     */
    if (count >= capacity) {
        goto fallback;
    }
    intpPts[count].x = (double)(nOrigPts - 1);
    intpPts[count].y = 0.0;
    indices[count] = mapPtr->indices[nOrigPts - 1];
    count++;
    nIntpPts = count;
    result = Rbc_CatromParametricSpline(origPts, nOrigPts, intpPts, nIntpPts);
    if (!result) {
        goto fallback;
    }
    /*
     * Commit only after spline generation succeeds.
     */
    ckfree(mapPtr->screenPts);
    ckfree(mapPtr->indices);
    mapPtr->screenPts = intpPts;
    mapPtr->indices = indices;
    mapPtr->nScreenPts = nIntpPts;
    return;

fallback:
    if (intpPts != NULL) {
        ckfree(intpPts);
    }
    if (indices != NULL) {
        ckfree(indices);
    }
    /*
     * Preserve the original MapInfo arrays and fall back to straight
     * line segments.
     */
    linePtr->smooth = PEN_SMOOTH_NONE;
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
    Tcl_Size *indices;
    Tcl_Size i;
    Tcl_Size count;
    size_t pointBytes;
    size_t indexBytes;

    if (mapPtr->nScreenPts <= 0) {
        return;
    }
    if ((GetLineArrayByteCount(mapPtr->nScreenPts, sizeof(*symbolPts), &pointBytes) != TCL_OK) ||
        (GetLineArrayByteCount(mapPtr->nScreenPts, sizeof(*indices), &indexBytes) != TCL_OK)) {
        return;
    }
    symbolPts = Tcl_AttemptAlloc(pointBytes);
    if (symbolPts == NULL) {
        return;
    }
    indices = Tcl_AttemptAlloc(indexBytes);
    if (indices == NULL) {
        ckfree(symbolPts);
        return;
    }
    Rbc_GraphExtents(graphPtr, &exts);
    count = 0;
    for (i = 0; i < mapPtr->nScreenPts; i++) {
        if (PointInRegion(&exts, mapPtr->screenPts[i].x, mapPtr->screenPts[i].y)) {
            symbolPts[count] = mapPtr->screenPts[i];
            indices[count] = mapPtr->indices[i];
            count++;
        }
    }
    if (count > 0) {
        linePtr->symbolPts = symbolPts;
        linePtr->symbolToData = indices;
        linePtr->nSymbolPts = count;
    } else {
        ckfree(symbolPts);
        ckfree(indices);
    }
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
    Point2D *activePts;
    Tcl_Size *activeToData;
    Tcl_Size nPoints;
    Tcl_Size pointIndex;
    Tcl_Size i;
    Tcl_Size count;

    if (linePtr->activePts != NULL) {
        ckfree(linePtr->activePts);
        linePtr->activePts = NULL;
    }
    if (linePtr->activeToData != NULL) {
        ckfree(linePtr->activeToData);
        linePtr->activeToData = NULL;
    }
    linePtr->nActivePts = 0;
    if (linePtr->core.nActiveIndices <= 0) {
        linePtr->core.flags &= ~ACTIVE_PENDING;
        return;
    }
    {
        size_t pointBytes;
        size_t indexBytes;

        if ((GetLineArrayByteCount(linePtr->core.nActiveIndices, sizeof(*activePts), &pointBytes) != TCL_OK) ||
            (GetLineArrayByteCount(linePtr->core.nActiveIndices, sizeof(*activeToData), &indexBytes) != TCL_OK)) {
            linePtr->core.flags &= ~ACTIVE_PENDING;
            return;
        }
        activePts = Tcl_AttemptAlloc(pointBytes);
        if (activePts == NULL) {
            linePtr->core.flags &= ~ACTIVE_PENDING;
            return;
        }
        activeToData = Tcl_AttemptAlloc(indexBytes);
        if (activeToData == NULL) {
            ckfree(activePts);
            linePtr->core.flags &= ~ACTIVE_PENDING;
            return;
        }
    }
    Rbc_GraphExtents(graphPtr, &exts);
    nPoints = NumberOfPoints(&linePtr->core);
    count = 0;
    for (i = 0; i < linePtr->core.nActiveIndices; i++) {
        Point2D point;
        double x;
        double y;
        pointIndex = linePtr->core.activeIndices[i];
        if ((pointIndex < 0) || (pointIndex >= nPoints)) {
            continue;
        }
        x = linePtr->core.x.valueArr[pointIndex];
        y = linePtr->core.y.valueArr[pointIndex];
        if (!FINITE(x) || !FINITE(y)) {
            continue;
        }
        point = Rbc_Map2D(graphPtr, x, y, &linePtr->core.axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        if (!PointInRegion(&exts, point.x, point.y)) {
            continue;
        }
        activePts[count] = point;
        activeToData[count] = pointIndex;
        count++;
    }
    if (count > 0) {
        linePtr->activePts = activePts;
        linePtr->activeToData = activeToData;
        linePtr->nActivePts = count;
    } else {
        ckfree(activePts);
        ckfree(activeToData);
    }
    linePtr->core.flags &= ~ACTIVE_PENDING;
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
    Tcl_Size *indices;
    Tcl_Size i;
    Tcl_Size count;
    Tcl_Size capacity;

    if (mapPtr->nScreenPts < 2) {
        return;
    }
    capacity = mapPtr->nScreenPts - 1;
    {
        size_t stripBytes;
        size_t indexBytes;

        if ((GetLineArrayByteCount(capacity, sizeof(*strips), &stripBytes) != TCL_OK) ||
            (GetLineArrayByteCount(capacity, sizeof(*indices), &indexBytes) != TCL_OK)) {
            return;
        }
        strips = Tcl_AttemptAlloc(stripBytes);
        if (strips == NULL) {
            return;
        }
        indices = Tcl_AttemptAlloc(indexBytes);
        if (indices == NULL) {
            ckfree(strips);
            return;
        }
    }
    count = 0;
    Rbc_GraphExtents(graphPtr, &exts);
    for (i = 0; i + 1 < mapPtr->nScreenPts; i++) {
        if ((mapPtr->breakBefore != NULL) && mapPtr->breakBefore[i + 1]) {
            continue;
        }
        if (Rbc_LineRectClip(&exts, &mapPtr->screenPts[i], &mapPtr->screenPts[i + 1], &strips[count])) {
            indices[count] = mapPtr->indices[i];
            count++;
        }
    }
    if (count > 0) {
        linePtr->stripToData = indices;
        linePtr->nStrips = count;
        linePtr->strips = strips;
    } else {
        ckfree(indices);
        ckfree(strips);
    }
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
    Tcl_Size i;
    Rbc_ChainLink *linkPtr;
    size_t stripsBytes = 0;
    size_t stripToDataBytes = 0;
    size_t symbolPtsBytes = 0;
    size_t symbolToDataBytes = 0;
    size_t xErrorBarsBytes = 0;
    size_t xErrorToDataBytes = 0;
    size_t yErrorBarsBytes = 0;
    size_t yErrorToDataBytes = 0;

    if (Rbc_ChainGetLength(linePtr->core.palette) < 2) {
        linkPtr = Rbc_ChainFirstLink(linePtr->core.palette);
        stylePtr = Rbc_ChainGetValue(linkPtr);
        stylePtr->nStrips = linePtr->nStrips;
        stylePtr->strips = linePtr->strips;
        stylePtr->nSymbolPts = linePtr->nSymbolPts;
        stylePtr->symbolPts = linePtr->symbolPts;
        stylePtr->xErrorBarCnt = linePtr->core.xErrorBarCnt;
        stylePtr->yErrorBarCnt = linePtr->core.yErrorBarCnt;
        stylePtr->xErrorBars = linePtr->core.xErrorBars;
        stylePtr->yErrorBars = linePtr->core.yErrorBars;
        stylePtr->errorBarCapWidth = linePtr->core.errorBarCapWidth;
        return;
    }
    /*
     * Validate all allocation sizes before modifying any style slices.
     * Ordinary allocation failure remains handled by ckalloc().
     */
    if ((linePtr->nStrips > 0) &&
        ((GetLineArrayByteCount(linePtr->nStrips, sizeof(*linePtr->strips), &stripsBytes) != TCL_OK) ||
         (GetLineArrayByteCount(linePtr->nStrips, sizeof(*linePtr->stripToData), &stripToDataBytes) != TCL_OK))) {
        return;
    }
    if ((linePtr->nSymbolPts > 0) &&
        ((GetLineArrayByteCount(linePtr->nSymbolPts, sizeof(*linePtr->symbolPts), &symbolPtsBytes) != TCL_OK) ||
         (GetLineArrayByteCount(linePtr->nSymbolPts, sizeof(*linePtr->symbolToData), &symbolToDataBytes) != TCL_OK))) {
        return;
    }
    if ((linePtr->core.xErrorBarCnt > 0) &&
        ((GetLineArrayByteCount(linePtr->core.xErrorBarCnt, sizeof(*linePtr->core.xErrorBars), &xErrorBarsBytes) !=
          TCL_OK) ||
         (GetLineArrayByteCount(linePtr->core.xErrorBarCnt, sizeof(*linePtr->core.xErrorToData), &xErrorToDataBytes) !=
          TCL_OK))) {
        return;
    }
    if ((linePtr->core.yErrorBarCnt > 0) &&
        ((GetLineArrayByteCount(linePtr->core.yErrorBarCnt, sizeof(*linePtr->core.yErrorBars), &yErrorBarsBytes) !=
          TCL_OK) ||
         (GetLineArrayByteCount(linePtr->core.yErrorBarCnt, sizeof(*linePtr->core.yErrorToData), &yErrorToDataBytes) !=
          TCL_OK))) {
        return;
    }
    /* We have more than one style. Group line segments and points of
     * like pen styles.  */
    if (linePtr->nStrips > 0) {
        Segment2D *strips;
        Segment2D *segPtr;
        Tcl_Size *stripToData;
        Tcl_Size *indexPtr;
        Tcl_Size dataIndex;

        strips = ckalloc(stripsBytes);
        stripToData = ckalloc(stripToDataBytes);
        segPtr = strips;
        indexPtr = stripToData;
        for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->strips = segPtr;
            for (i = 0; i < linePtr->nStrips; i++) {
                dataIndex = linePtr->stripToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *segPtr++ = linePtr->strips[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->nStrips = (Tcl_Size)(segPtr - stylePtr->strips);
        }
        ckfree(linePtr->strips);
        linePtr->strips = strips;
        ckfree(linePtr->stripToData);
        linePtr->stripToData = stripToData;
    }
    if (linePtr->nSymbolPts > 0) {
        Point2D *symbolPts;
        Point2D *pointPtr;
        Tcl_Size *symbolToData;
        Tcl_Size *indexPtr;
        Tcl_Size dataIndex;

        symbolPts = ckalloc(symbolPtsBytes);
        symbolToData = ckalloc(symbolToDataBytes);
        pointPtr = symbolPts;
        indexPtr = symbolToData;
        for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->symbolPts = pointPtr;
            for (i = 0; i < linePtr->nSymbolPts; i++) {
                dataIndex = linePtr->symbolToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *pointPtr++ = linePtr->symbolPts[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->nSymbolPts = (Tcl_Size)(pointPtr - stylePtr->symbolPts);
        }
        ckfree(linePtr->symbolPts);
        linePtr->symbolPts = symbolPts;
        ckfree(linePtr->symbolToData);
        linePtr->symbolToData = symbolToData;
    }
    if (linePtr->core.xErrorBarCnt > 0) {
        Segment2D *errorBars;
        Segment2D *segPtr;
        Tcl_Size *errorToData;
        Tcl_Size *indexPtr;
        Tcl_Size dataIndex;
        Tcl_Size i;
        errorBars = ckalloc(xErrorBarsBytes);
        errorToData = ckalloc(xErrorToDataBytes);
        segPtr = errorBars;
        indexPtr = errorToData;
        for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->xErrorBars = segPtr;
            for (i = 0; i < linePtr->core.xErrorBarCnt; i++) {
                dataIndex = linePtr->core.xErrorToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *segPtr++ = linePtr->core.xErrorBars[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->xErrorBarCnt = (Tcl_Size)(segPtr - stylePtr->xErrorBars);
        }
        ckfree(linePtr->core.xErrorBars);
        linePtr->core.xErrorBars = errorBars;
        ckfree(linePtr->core.xErrorToData);
        linePtr->core.xErrorToData = errorToData;
    }
    if (linePtr->core.yErrorBarCnt > 0) {
        Segment2D *errorBars;
        Segment2D *segPtr;
        Tcl_Size *errorToData;
        Tcl_Size *indexPtr;
        Tcl_Size dataIndex;
        Tcl_Size i;

        errorBars = ckalloc(yErrorBarsBytes);
        errorToData = ckalloc(yErrorToDataBytes);
        segPtr = errorBars;
        indexPtr = errorToData;
        for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->yErrorBars = segPtr;
            for (i = 0; i < linePtr->core.yErrorBarCnt; i++) {
                dataIndex = linePtr->core.yErrorToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *segPtr++ = linePtr->core.yErrorBars[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->yErrorBarCnt = (Tcl_Size)(segPtr - stylePtr->yErrorBars);
        }
        ckfree(linePtr->core.yErrorBars);
        linePtr->core.yErrorBars = errorBars;
        ckfree(linePtr->core.yErrorToData);
        linePtr->core.yErrorToData = errorToData;
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
 *      Tcl_Size start  - Starting index of the trace in the mapped
 *                        point array.
 *      Tcl_Size length - Number of points forming the trace.
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
static void SaveTrace(Line *linePtr, Tcl_Size start, Tcl_Size length, MapInfo *mapPtr) {
    LineTrace *tracePtr;
    Point2D *screenPts;
    Tcl_Size *indices;
    Tcl_Size i;
    Tcl_Size j;

    tracePtr = ckalloc(sizeof(*tracePtr));
    screenPts = ckalloc((size_t)length * sizeof(*screenPts));
    indices = ckalloc((size_t)length * sizeof(*indices));
    if (mapPtr->indices != NULL) {
        for (i = 0, j = start; i < length; i++, j++) {
            screenPts[i] = mapPtr->screenPts[j];
            indices[i] = mapPtr->indices[j];
        }
    } else {
        for (i = 0, j = start; i < length; i++, j++) {
            screenPts[i] = mapPtr->screenPts[j];
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

static double TraceAbscissa(Graph *graphPtr, Line *linePtr, const Point2D *pointPtr) {
    double value;
    
    /*
     * -trace increasing/decreasing refers to the element's data-X
     * coordinate, not to left-to-right screen motion.
     *
     * Normally data X maps to screen X.  In an inverted graph it maps
     * to screen Y; a normal vertical mapping runs in the opposite
     * screen direction.
     */
    if (graphPtr->inverted) {
        value = -pointPtr->y;
    } else {
        value = pointPtr->x;
    }
    /*
     * Axis -descending reverses whichever screen direction maps data X.
     * Undo that reversal so increasing value always corresponds to
     * increasing data X.
     */
    if (linePtr->core.axes.x->descending) {
        value = -value;
    }
    return value;
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
    Tcl_Size start;
    Tcl_Size count;
    int code1;
    int code2;
    Point2D *p;
    Point2D *q;
    Point2D s;
    Extents2D exts;
    Tcl_Size i;
    int broken;
    int offscreen;
    double lastX;
    double nextX;

    Rbc_GraphExtents(graphPtr, &exts);
    count = 1;
    code1 = OutCode(&exts, mapPtr->screenPts);
    p = mapPtr->screenPts;
    q = p + 1;
    for (i = 1; i < mapPtr->nScreenPts; i++, p++, q++) {
        if ((mapPtr->breakBefore != NULL) && mapPtr->breakBefore[i]) {
            /*
             * q starts a new continuous data run.  Save the previous
             * trace without ever clipping or testing the nonexistent
             * p->q segment.
             */
            if (count > 1) {
                start = i - count;
                SaveTrace(linePtr, start, count, mapPtr);
            }
            count = 1;
            code1 = OutCode(&exts, q);
            continue;
        }
        code2 = OutCode(&exts, q);
        if (code2 != 0) {
            /*
             * Save the coordinates of the last point before clipping.
             */
            s = *q;
        }
        /*
         * Test direction in a coordinate whose increasing direction
         * always corresponds to increasing element data X.
         */
        lastX = TraceAbscissa(graphPtr, linePtr, p);
        nextX = TraceAbscissa(graphPtr, linePtr, q);
        broken = BROKEN_TRACE(linePtr->penDir, lastX, nextX);
        offscreen = ClipSegment(&exts, code1, code2, p, q);
        if (broken || offscreen) {
            /*
             * The last line segment is either totally clipped by the
             * plotting area or has the wrong data-X direction,
             * breaking the trace.
             */
            if (count > 1) {
                start = i - count;
                SaveTrace(linePtr, start, count, mapPtr);
                count = 1;
            }
        } else {
            count++;
            if (code2 != 0) {
                /*
                 * If the last point is clipped, the trace is broken
                 * after this point.  Restore the original coordinate
                 * after saving the trace.
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
    Point2D *origPts;
    Point2D *clipPts;
    Extents2D exts;
    double maxY;
    Tcl_Size inputCount;
    Tcl_Size clipCapacity;
    Tcl_Size n;
    Tcl_Size i;

    /*
     * Always discard the previous mapped polygon first.  The old code
     * performed its INT_MAX check before this cleanup, which could leave
     * a stale fill polygon after remapping failed.
     */
    if (linePtr->fillPts != NULL) {
        ckfree(linePtr->fillPts);
        linePtr->fillPts = NULL;
        linePtr->nFillPts = 0;
    }
    /*
     * Line currently stores one fill polygon only.  Filling a mapped
     * line containing discontinuities would incorrectly bridge separate
     * data runs.
     */
    if (mapPtr->breakBefore != NULL) {
        return;
    }
    if (mapPtr->nScreenPts < 3) {
        return;
    }
    /*
     * Add two bottom vertices to close the fill polygon.
     * Rbc_PolyRectClip() now generates the closing edge internally.
     */
    if (mapPtr->nScreenPts > TCL_SIZE_MAX - 2) {
        return;
    }
    inputCount = mapPtr->nScreenPts + 2;
    /*
     * The clipping routine can emit at most three vertices per input
     * edge, followed by one closing vertex.
     */
    if (inputCount > (TCL_SIZE_MAX - 1) / 3) {
        return;
    }
    clipCapacity = inputCount * 3 + 1;
    if ((inputCount > (Tcl_Size)(SIZE_MAX / sizeof(*origPts))) ||
        (clipCapacity > (Tcl_Size)(SIZE_MAX / sizeof(*clipPts)))) {
        return;
    }
    origPts = (Point2D *)Tcl_AttemptAlloc((size_t)inputCount * sizeof(*origPts));
    if (origPts == NULL) {
        return;
    }
    clipPts = (Point2D *)Tcl_AttemptAlloc((size_t)clipCapacity * sizeof(*clipPts));
    if (clipPts == NULL) {
        ckfree(origPts);
        return;
    }
    Rbc_GraphExtents(graphPtr, &exts);
    maxY = (double)graphPtr->bottom;
    for (i = 0; i < mapPtr->nScreenPts; i++) {
        origPts[i].x = mapPtr->screenPts[i].x + 1;
        origPts[i].y = mapPtr->screenPts[i].y;
        if (origPts[i].y > maxY) {
            maxY = origPts[i].y;
        }
    }
    /*
     * Add edges that extend the polygon to the bottom of the plotting
     * area.
     */
    origPts[i].x = origPts[i - 1].x;
    origPts[i].y = maxY;
    i++;
    origPts[i].x = origPts[0].x;
    origPts[i].y = maxY;
    i++;
    n = Rbc_PolyRectClip(&exts, origPts, inputCount, clipPts, clipCapacity);
    ckfree(origPts);
    if (n < 3) {
        ckfree(clipPts);
        return;
    }
    linePtr->fillPts = clipPts;
    linePtr->nFillPts = n;
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
    ClearPalette(linePtr->core.palette);
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
    if (linePtr->core.xErrorBars != NULL) {
        ckfree((char *)linePtr->core.xErrorBars);
    }
    if (linePtr->core.xErrorToData != NULL) {
        ckfree((char *)linePtr->core.xErrorToData);
    }
    if (linePtr->core.yErrorBars != NULL) {
        ckfree((char *)linePtr->core.yErrorBars);
    }
    if (linePtr->core.yErrorToData != NULL) {
        ckfree((char *)linePtr->core.yErrorToData);
    }
    linePtr->core.xErrorBars = linePtr->core.yErrorBars = linePtr->strips = NULL;
    linePtr->symbolPts = linePtr->activePts = NULL;
    linePtr->stripToData = NULL;
    linePtr->symbolToData = NULL;
    linePtr->activeToData = NULL;
    linePtr->core.xErrorToData = NULL;
    linePtr->core.yErrorToData = NULL;
    linePtr->nActivePts = 0;
    linePtr->nSymbolPts = 0;
    linePtr->nStrips = 0;
    linePtr->core.xErrorBarCnt = 0;
    linePtr->core.yErrorBarCnt = 0;
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
    Line *linePtr = LINE_FROM_CORE(elemPtr);
    MapInfo mapInfo = {0};
    Tcl_Size nPoints;
    int size;
    PenStyle **dataToStyle;
    Rbc_ChainLink *linkPtr;
    LinePenStyle *stylePtr;

    ResetLine(linePtr);
    nPoints = NumberOfPoints(&linePtr->core);
    if (nPoints < 1) {
        return;
    }
    GetScreenPoints(graphPtr, linePtr, &mapInfo);
    if (mapInfo.nScreenPts < 1) {
        return;
    }
    MapSymbols(graphPtr, linePtr, &mapInfo);
    if ((linePtr->core.flags & ACTIVE_PENDING) && (linePtr->core.nActiveIndices > 0)) {
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
            GenerateSteps(graphPtr, linePtr, &mapInfo);
            break;

        case PEN_SMOOTH_NATURAL:
        case PEN_SMOOTH_QUADRATIC:
            if ((mapInfo.nScreenPts < 3) || (mapInfo.breakBefore != NULL)) {
                /*
                 * Never interpolate through missing data.  Proper per-run
                 * spline generation can be added separately.
                 */
                linePtr->smooth = PEN_SMOOTH_NONE;
            } else {
                GenerateSpline(graphPtr, linePtr, &mapInfo);
            }
            break;

        case PEN_SMOOTH_CATROM:
            if ((mapInfo.nScreenPts < 3) || (mapInfo.breakBefore != NULL)) {
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
    ckfree(mapInfo.screenPts);
    ckfree(mapInfo.indices);
    if (mapInfo.breakBefore != NULL) {
        ckfree(mapInfo.breakBefore);
    }

    /* Set the symbol size of all the pen styles. */
    for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        size = ScaleSymbol(elemPtr, stylePtr->penPtr->symbol.size);
        stylePtr->symbolSize = size;
        stylePtr->errorBarCapWidth =
            (stylePtr->penPtr->errorBarCapWidth > 0) ? stylePtr->penPtr->errorBarCapWidth : (int)(size * 0.6666666);
        stylePtr->errorBarCapWidth /= 2;
    }
    dataToStyle = Rbc_StyleMap(&linePtr->core);
    if (((linePtr->core.yHigh.nValues > 0) && (linePtr->core.yLow.nValues > 0)) ||
        ((linePtr->core.xHigh.nValues > 0) && (linePtr->core.xLow.nValues > 0)) || (linePtr->core.xError.nValues > 0) ||
        (linePtr->core.yError.nValues > 0)) {
        Rbc_MapErrorBars(graphPtr, &linePtr->core, dataToStyle);
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
    Point2D sample;

    sample.x = (double)x;
    sample.y = (double)y;
    return Rbc_GetClosestPointOnSegment(&sample, p, q, t);
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
    Tcl_Size dataIndex;

    dataIndex = -1;
    closest.x = closest.y = 0; /* Suppress compiler warning. */
    minDist = searchPtr->dist;
    for (linkPtr = Rbc_ChainFirstLink(linePtr->traces); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        tracePtr = Rbc_ChainGetValue(linkPtr);
        for (pointPtr = tracePtr->screenPts, endPtr = tracePtr->screenPts + (tracePtr->nScreenPts - 1);
             pointPtr < endPtr; pointPtr++) {
            dist = (*distProc)(searchPtr->x, searchPtr->y, pointPtr, pointPtr + 1, &b);
            if (dist < minDist) {
                closest = b;
                dataIndex = tracePtr->symbolToData[pointPtr - tracePtr->screenPts];
                minDist = dist;
            }
        }
    }
    if (minDist < searchPtr->dist) {
        searchPtr->dist = minDist;
        searchPtr->elemPtr = &linePtr->core;
        searchPtr->index = dataIndex;
        searchPtr->point = Rbc_InvMap2D(graphPtr, closest.x, closest.y, &(linePtr->core.axes));
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
    Tcl_Size count;
    register Segment2D *s;
    Tcl_Size dataIndex;

    dataIndex = 0;
    closest.x = closest.y = 0; /* compiler warning */
    minDist = searchPtr->dist;
    s = linePtr->strips;
    for (count = 0; count < linePtr->nStrips; count++, s++) {
        dist = (*distProc)(searchPtr->x, searchPtr->y, &(s->p), &(s->q), &b);
        if (dist < minDist) {
            closest = b;
            dataIndex = linePtr->stripToData[count];
            minDist = dist;
        }
    }
    if (minDist < searchPtr->dist) {
        searchPtr->dist = minDist;
        searchPtr->elemPtr = &linePtr->core;
        searchPtr->index = dataIndex;
        searchPtr->point = Rbc_InvMap2D(graphPtr, closest.x, closest.y, &(linePtr->core.axes));
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
    Point2D *pointPtr;
    double minDist;
    Tcl_Size count;
    Tcl_Size dataIndex;

    minDist = searchPtr->dist;
    dataIndex = -1;
    /*
     * Instead of testing each data point in graph coordinates, look at
     * the array of mapped screen coordinates.  The advantages are:
     *
     * 1) only visible, unclipped points are examined;
     * 2) distances are already measured in screen coordinates.
     */
    pointPtr = linePtr->symbolPts;
    for (count = 0; count < linePtr->nSymbolPts; count++, pointPtr++) {
        double dx;
        double dy;
        double dist;

        dx = fabs((double)searchPtr->x - pointPtr->x);
        dy = fabs((double)searchPtr->y - pointPtr->y);
        switch (searchPtr->along) {
        case SEARCH_BOTH:
            dist = hypot(dx, dy);
            break;
        case SEARCH_X:
            dist = dx;
            break;
        case SEARCH_Y:
            dist = dy;
            break;
        default:
            continue;
        }
        if (dist < minDist) {
            dataIndex = linePtr->symbolToData[count];
            minDist = dist;
        }
    }
    if ((dataIndex >= 0) && (minDist < searchPtr->dist)) {
        searchPtr->elemPtr = &linePtr->core;
        searchPtr->dist = minDist;
        searchPtr->index = dataIndex;
        searchPtr->point.x = linePtr->core.x.valueArr[dataIndex];
        searchPtr->point.y = linePtr->core.y.valueArr[dataIndex];
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
    Tcl_Size nPoints;

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
    Rbc_ExpandErrorBarExtents(elemPtr, extsPtr);
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

    graphPtr = linePtr->core.graphPtr;
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
    Line *linePtr;
    ElemDataTransaction dataTransaction;
    ElemPenTransaction penTransaction;
    ElemAxisTransaction axisTransaction;
    ElemStateTransaction stateTransaction;
    ElemTagsTransaction tagsTransaction;
    ElemStylesTransaction stylesTransaction;
    LineScalarTransaction scalarTransaction;
    LineAreaTransaction areaTransaction;    
    Rbc_ChainLink *linkPtr;
    int dataTransactionPrepared;
    int penTransactionPrepared;
    int axisTransactionPrepared;
    int stateTransactionPrepared;
    int tagsTransactionPrepared;
    int stylesTransactionPrepared;
    int scalarTransactionPrepared;
    int areaTransactionPrepared;    

    linePtr = LINE_FROM_CORE(elemPtr);

    memset(&dataTransaction, 0, sizeof(dataTransaction));
    memset(&penTransaction, 0, sizeof(penTransaction));
    memset(&axisTransaction, 0, sizeof(axisTransaction));
    memset(&stateTransaction, 0, sizeof(stateTransaction));
    memset(&tagsTransaction, 0, sizeof(tagsTransaction));
    memset(&stylesTransaction, 0, sizeof(stylesTransaction));
    memset(&scalarTransaction, 0, sizeof(scalarTransaction));
    memset(&areaTransaction, 0, sizeof(areaTransaction));

    dataTransactionPrepared = FALSE;
    penTransactionPrepared = FALSE;
    axisTransactionPrepared = FALSE;
    stateTransactionPrepared = FALSE;
    tagsTransactionPrepared = FALSE;
    stylesTransactionPrepared = FALSE;
    scalarTransactionPrepared = FALSE;
    areaTransactionPrepared = FALSE;

    /*
     * Parse all data-vector replacements before modifying the live
     * element.
     */
    if (((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_DATA_MASK))) {
        if (Rbc_PrepareElemDataTransaction(graphPtr, elemPtr, &dataTransaction) != TCL_OK) {
            goto error;
        }

        dataTransactionPrepared = TRUE;
    }

    /*
     * Resolve named active and normal pens before modifying the live
     * element. Line and strip elements both require line-type pens.
     */
    if (((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_PEN_MASK))) {
        if (Rbc_PrepareElemPenTransaction(graphPtr, elemPtr, rbcLineElementUid, &penTransaction) != TCL_OK) {
            goto error;
        }

        penTransactionPrepared = TRUE;
    }

    /*
     * Resolve the X and Y axis mappings before modifying the live
     * element.
     */
    if (((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_AXES_MASK))) {
        if (Rbc_PrepareElemAxisTransaction(graphPtr, elemPtr, &axisTransaction) != TCL_OK) {
            goto error;
        }

        axisTransactionPrepared = TRUE;
    }

    /*
     * Parse the element state before modifying the live element.
     *
     * Strip elements have no -state entry in their modern option table,
     * so stateObjPtr remains NULL and this transaction stages nothing for
     * them during initial configuration.
     */
    if (((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_STATE_MASK))) {
        if (Rbc_PrepareElemStateTransaction(graphPtr, elemPtr, &stateTransaction) != TCL_OK) {
            goto error;
        }

        stateTransactionPrepared = TRUE;
    }

    /*
     * Parse the element bind tags before modifying the live element.
     *
     * Both line and strip elements expose -bindtags in their modern
     * option tables.
     */
    if (((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_TAGS_MASK))) {
        if (Rbc_PrepareElemTagsTransaction(graphPtr, elemPtr, &tagsTransaction) != TCL_OK) {
            goto error;
        }

        tagsTransactionPrepared = TRUE;
    }

    /*
     * Parse the element's weighted pen styles before modifying the live
     * palette.
     *
     * Line and strip elements both use line pens and LinePenStyle palette
     * records.
     */
    if (((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_STYLES_MASK))) {
        if (Rbc_PrepareElemStylesTransaction(graphPtr, elemPtr, rbcLineElementUid, sizeof(LinePenStyle),
                                             &stylesTransaction) != TCL_OK) {
            goto error;
        }

        stylesTransactionPrepared = TRUE;
    }

    /*
     * Parse line-specific scalar values before modifying the live element.
     *
     * Both line and strip elements expose -maxsymbols and -smooth. Only
     * graph line elements expose -trace.
     */
    if (((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_SCALAR_MASK))) {
        if (PrepareLineScalarTransaction(graphPtr, elemPtr, linePtr, &scalarTransaction) != TCL_OK) {
            goto error;
        }

        scalarTransactionPrepared = TRUE;
    }

    /*
     * Resolve line-area resources and construct the replacement fill GC
     * before modifying the live element.
     *
     * Strip elements have no area options, but initial modern
     * configuration may still construct their empty fill GC.
     */
    if (((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_AREA_MASK))) {
        if (PrepareLineAreaTransaction(graphPtr, elemPtr, linePtr, &areaTransaction) != TCL_OK) {
            goto error;
        }

        areaTransactionPrepared = TRUE;
    }

    /*
     * Configure the embedded line pen only when its options may have
     * changed.
     */
    if (!elemPtr->optionsConfigured || (elemPtr->optionMask & LINE_ELEM_BUILTIN_PEN_MASK)) {
        if (ConfigurePen(graphPtr, &linePtr->builtinPen.core) != TCL_OK) {
            goto error;
        }
    }

    /*
     * No remaining operation before palette synchronisation can report a
     * Tcl configuration error. Transfer all staged values and references
     * to the live element.
     */
    if (axisTransactionPrepared) {
        Rbc_CommitElemAxisTransaction(graphPtr, elemPtr, &axisTransaction);
    }

    if (penTransactionPrepared) {
        Rbc_CommitElemPenTransaction(graphPtr, elemPtr, &linePtr->builtinPen.core, &penTransaction);
    }

    if (stateTransactionPrepared) {
        Rbc_CommitElemStateTransaction(elemPtr, &stateTransaction);
    }

    if (tagsTransactionPrepared) {
        Rbc_CommitElemTagsTransaction(elemPtr, &tagsTransaction);
    }
    
    if (stylesTransactionPrepared) {
        Rbc_CommitElemStylesTransaction(graphPtr, elemPtr, &stylesTransaction);
    }

    if (scalarTransactionPrepared) {
        CommitLineScalarTransaction(linePtr, &scalarTransaction);
    }

    if (areaTransactionPrepared) {
        CommitLineAreaTransaction(graphPtr, linePtr, &areaTransaction);
    }

    assert(elemPtr->normalPenPtr != NULL);

    linkPtr = Rbc_ChainFirstLink(elemPtr->palette);

    if (linkPtr != NULL) {
        LinePenStyle *stylePtr;

        stylePtr = Rbc_ChainGetValue(linkPtr);
        stylePtr->penPtr = LINE_PEN_FROM_CORE(elemPtr->normalPenPtr);
    }

    /*
     * Nothing below this point can fail. Commit the staged vectors and
     * synchronize their retained Tcl representations.
     */
    if (dataTransactionPrepared) {
        Rbc_CommitElemDataTransaction(elemPtr, &dataTransaction);

        Rbc_SyncElemDataOptionObjects(elemPtr);
    }

    if (!elemPtr->optionsConfigured || (elemPtr->optionMask & LINE_ELEM_SCALE_SYMBOL_MASK)) {
        elemPtr->flags |= MAP_ITEM | SCALE_SYMBOL;
    }

    if (!elemPtr->optionsConfigured || (elemPtr->optionMask & LINE_ELEM_MAP_ITEM_MASK)) {
        elemPtr->flags |= MAP_ITEM;
    }

    return TCL_OK;

error:
    if (areaTransactionPrepared) {
        FreeLineAreaTransaction(graphPtr, &areaTransaction);
    }

    if (stylesTransactionPrepared) {
        Rbc_FreeElemStylesTransaction(graphPtr, &stylesTransaction);
    }

    if (tagsTransactionPrepared) {
        Rbc_FreeElemTagsTransaction(&tagsTransaction);
    }

    if (axisTransactionPrepared) {
        Rbc_FreeElemAxisTransaction(graphPtr, &axisTransaction);
    }

    if (penTransactionPrepared) {
        Rbc_FreeElemPenTransaction(graphPtr, &penTransaction);
    }

    if (dataTransactionPrepared) {
        Rbc_FreeElemDataTransaction(&dataTransaction);
    }

    return TCL_ERROR;
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
    Line *linePtr = LINE_FROM_CORE(elemPtr);
    int mode;

    mode = searchPtr->mode;
    if (mode == SEARCH_AUTO) {
        LinePen *penPtr = LINE_PEN_FROM_CORE(elemPtr->normalPenPtr);

        mode = SEARCH_POINTS;
        if ((NumberOfPoints(elemPtr) > 1) && (penPtr->traceWidth > 0)) {
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
static void DrawCircles(Display *display, Drawable drawable, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts,
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
static void DrawCircles(Display *display, Drawable drawable, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts,
                        Point2D *symbolPts, int radius) {
    Tcl_Size i;
    XArc *arcArr; /* Array of arcs (circle) */
    XArc *arcPtr;
    int reqSize, nArcs;
    int s;
    Tcl_Size count;
    register Point2D *pointPtr, *endPtr;

    s = radius + radius;
    if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / sizeof(*arcArr))) {
        return;
    }
    arcArr = Tcl_AttemptAlloc((size_t)nSymbolPts * sizeof(*arcArr));
    if (arcArr == NULL) {
        return;
    }
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
    if (reqSize < 1) {
        ckfree(arcArr);
        return;
    }
    for (i = 0; i < count; i += (Tcl_Size)reqSize) {
        Tcl_Size remaining;

        remaining = count - i;
        nArcs = (remaining > (Tcl_Size)reqSize) ? reqSize : (int)remaining;
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
static void DrawSquares(Display *display, Drawable drawable, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts,
                        register Point2D *symbolPts, int r) {
    XRectangle *rectArr;
    register Point2D *pointPtr, *endPtr;
    register XRectangle *rectPtr;
    int reqSize;
    int nRects;
    int s;
    Tcl_Size i;
    Tcl_Size count;

    s = r + r;
    if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / sizeof(*rectArr))) {
        return;
    }
    rectArr = Tcl_AttemptAlloc((size_t)nSymbolPts * sizeof(*rectArr));
    if (rectArr == NULL) {
        return;
    }
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
    if (reqSize < 1) {
        ckfree(rectArr);
        return;
    }
    for (i = 0; i < count; i += (Tcl_Size)reqSize) {
        Tcl_Size remaining;

        remaining = count - i;
        nRects = (remaining > (Tcl_Size)reqSize) ? reqSize : (int)remaining;
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
static void DrawSymbols(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, int size,
                        Tcl_Size nSymbolPts, Point2D *symbolPts) {
    XPoint pattern[13]; /* Template for polygon symbols */
    int r1, r2;
    Tcl_Size i, n;
    Tcl_Size count;
    register Point2D *pointPtr, *endPtr;
#define SQRT_PI 1.77245385090552
#define S_RATIO 0.886226925452758

    if (size < 3) {
        if ((penPtr->symbol.fillGC != NULL) && (nSymbolPts > 0)) {
            XPoint *points;
            Tcl_Size offset;
            int maxPoints;

            maxPoints = Rbc_MaxRequestSize(graphPtr->display, sizeof(XPoint));

            if (maxPoints < 1) {
                return;
            }
            if ((size_t)maxPoints > SIZE_MAX / sizeof(*points)) {
                return;
            }
            points = Tcl_AttemptAlloc((size_t)maxPoints * sizeof(*points));
            if (points == NULL) {
                return;
            }
            offset = 0;
            while (offset < nSymbolPts) {
                Tcl_Size remaining;
                int chunk;
                int j;

                remaining = nSymbolPts - offset;
                chunk = (remaining > (Tcl_Size)maxPoints) ? maxPoints : (int)remaining;
                for (j = 0; j < chunk; j++) {
                    points[j].x = (short int)symbolPts[offset + j].x;
                    points[j].y = (short int)symbolPts[offset + j].y;
                }
                XDrawPoints(graphPtr->display, drawable, penPtr->symbol.fillGC, points, chunk, CoordModeOrigin);
                offset += chunk;
            }
            ckfree(points);
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
        Tcl_Size nSegs;
        int reqSize;
        int chunk;

        if (penPtr->symbol.type == SYMBOL_SCROSS) {
            r2 = Round(r2 * M_SQRT1_2);
            pattern[3].y = pattern[2].x = pattern[0].x = pattern[0].y = -r2;
            pattern[3].x = pattern[2].y = pattern[1].y = pattern[1].x = r2;
        } else {
            pattern[0].y = pattern[1].y = pattern[2].x = pattern[3].x = 0;
            pattern[0].x = pattern[2].y = -r2;
            pattern[1].x = pattern[3].y = r2;
        }
        if ((nSymbolPts > TCL_SIZE_MAX / 2) || ((size_t)nSymbolPts > SIZE_MAX / (2 * sizeof(*segArr)))) {
            return;
        }
        segArr = Tcl_AttemptAlloc((size_t)nSymbolPts * 2 * sizeof(*segArr));
        if (segArr == NULL) {
            return;
        }
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
        reqSize = MAX_DRAWSEGMENTS(graphPtr->display);
        if (reqSize < 1) {
            ckfree(segArr);
            return;
        }
        for (i = 0; i < nSegs; i += (Tcl_Size)reqSize) {
            Tcl_Size remaining;

            remaining = nSegs - i;
            chunk = (remaining > (Tcl_Size)reqSize) ? reqSize : (int)remaining;
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
        if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / (13u * sizeof(*polygon)))) {
            return;
        }
        polygon = Tcl_AttemptAlloc((size_t)nSymbolPts * 13u * sizeof(*polygon));
        if (polygon == NULL) {
            return;
        }
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

        if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / (5u * sizeof(*polygon)))) {
            return;
        }
        polygon = Tcl_AttemptAlloc((size_t)nSymbolPts * 5u * sizeof(*polygon));
        if (polygon == NULL) {
            return;
        }
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
        if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / (4u * sizeof(*polygon)))) {
            return;
        }
        polygon = Tcl_AttemptAlloc((size_t)nSymbolPts * 4u * sizeof(*polygon));
        if (polygon == NULL) {
            return;
        }
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
    Line *linePtr = LINE_FROM_CORE(elemPtr);
    LinePen *penPtr = LINE_PEN_FROM_CORE(elemPtr->normalPenPtr);

    if (penPtr->traceWidth > 0) {
        XDrawLine(graphPtr->display, drawable, penPtr->traceGC, x - size, y, x + size, y);
        XDrawLine(graphPtr->display, drawable, penPtr->traceGC, x - size, y + 1, x + size, y + 1);
    }
    if (penPtr->symbol.type != SYMBOL_NONE) {
        Point2D point;
        point.x = x;
        point.y = y;
        DrawSymbols(graphPtr, drawable, linePtr, penPtr, size, 1, &point);
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
    Tcl_Size count;
    Tcl_Size j;
    Tcl_Size remaining;
    int maxPoints;

    /*
     * Depending on whether the line is wide (> 1 pixel), arbitrarily
     * break the line into sections of 100 points.  Long polylines
     * drawn with wide geometric pens can be prohibitively slow.
     */
    if (penPtr->traceGC->line_width > 1) {
        maxPoints = 100;
    } else {
        int requestSize;

        requestSize = Rbc_MaxRequestSize(graphPtr->display, sizeof(POINT));
        /*
         * Reserve one point in each request for the endpoint shared
         * with the preceding chunk.
         */
        if (requestSize <= 1) {
            return;
        }
        maxPoints = requestSize - 1;
    }
    if (maxPoints < 2) {
        return;
    }
    if (((size_t)maxPoints + 1u) > SIZE_MAX / sizeof(*points)) {
        return;
    }
    points = Tcl_AttemptAlloc(((size_t)maxPoints + 1u) * sizeof(*points));
    if (points == NULL) {
        return;
    }
    dc = TkWinGetDrawableDC(graphPtr->display, drawable, &state);
    pen = Rbc_GCToPen(dc, penPtr->traceGC);
    oldPen = SelectPen(dc, pen);
    brush = CreateSolidBrush(penPtr->traceGC->foreground);
    oldBrush = SelectBrush(dc, brush);
    SetROP2(dc, tkpWinRopModes[penPtr->traceGC->function]);
    for (linkPtr = Rbc_ChainFirstLink(linePtr->traces); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        Tcl_Size firstCount;

        tracePtr = Rbc_ChainGetValue(linkPtr);
        /*
         * A polyline requires at least two points.
         */
        if (tracePtr->nScreenPts < 2) {
            continue;
        }
        /*
         * Step 1: draw the first section.  It may contain the entire
         * trace.
         */
        firstCount = MIN((Tcl_Size)maxPoints, tracePtr->nScreenPts);
        for (count = 0; count < firstCount; count++) {
            points[count].x = (int)tracePtr->screenPts[count].x;
            points[count].y = (int)tracePtr->screenPts[count].y;
        }
        Polyline(dc, points, (int)firstCount);
        /*
         * Step 2: draw full continuation chunks.  Each continuation
         * starts with the last point drawn by the preceding request.
         */
        while ((tracePtr->nScreenPts - count) > (Tcl_Size)maxPoints) {
            points[0].x = (int)tracePtr->screenPts[count - 1].x;
            points[0].y = (int)tracePtr->screenPts[count - 1].y;
            for (j = 0; j < (Tcl_Size)maxPoints; j++, count++) {
                points[j + 1].x = (int)tracePtr->screenPts[count].x;
                points[j + 1].y = (int)tracePtr->screenPts[count].y;
            }
            Polyline(dc, points, maxPoints + 1);
        }
        /*
         * Step 3: draw the final partial chunk.
         */
        remaining = tracePtr->nScreenPts - count;
        if (remaining > 0) {
            points[0].x = (int)tracePtr->screenPts[count - 1].x;
            points[0].y = (int)tracePtr->screenPts[count - 1].y;
            for (j = 0; j < remaining; j++, count++) {
                points[j + 1].x = (int)tracePtr->screenPts[count].x;
                points[j + 1].y = (int)tracePtr->screenPts[count].y;
            }
            Polyline(dc, points, (int)remaining + 1);
        }
    }
    ckfree(points);
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
    Tcl_Size count;
    Tcl_Size j;
    Tcl_Size remaining;
    int maxPoints;
    int requestSize;

    requestSize = Rbc_MaxRequestSize(graphPtr->display, sizeof(XPoint));

    /*
     * Reserve one point in each request for the endpoint shared with
     * the preceding chunk.
     */
    if (requestSize <= 1) {
        return;
    }
    maxPoints = requestSize - 1;
    if (maxPoints < 2) {
        return;
    }
    if (((size_t)maxPoints + 1u) > SIZE_MAX / sizeof(*points)) {
        return;
    }
    points = Tcl_AttemptAlloc(((size_t)maxPoints + 1u) * sizeof(*points));
    if (points == NULL) {
        return;
    }
    for (linkPtr = Rbc_ChainFirstLink(linePtr->traces); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        Tcl_Size firstCount;

        tracePtr = Rbc_ChainGetValue(linkPtr);
        /*
         * XDrawLines requires at least two useful points to produce a
         * line.
         */
        if (tracePtr->nScreenPts < 2) {
            continue;
        }
        /*
         * Step 1: draw the first section.  It may contain the entire
         * trace.
         */
        firstCount = MIN((Tcl_Size)maxPoints, tracePtr->nScreenPts);
        for (count = 0; count < firstCount; count++) {
            points[count].x = (short int)tracePtr->screenPts[count].x;
            points[count].y = (short int)tracePtr->screenPts[count].y;
        }
        XDrawLines(graphPtr->display, drawable, penPtr->traceGC, points, (int)firstCount, CoordModeOrigin);
        /*
         * Step 2: draw full continuation chunks.  Each continuation
         * starts with the last point drawn by the preceding request.
         */
        while ((tracePtr->nScreenPts - count) > (Tcl_Size)maxPoints) {
            points[0].x = (short int)tracePtr->screenPts[count - 1].x;
            points[0].y = (short int)tracePtr->screenPts[count - 1].y;
            for (j = 0; j < (Tcl_Size)maxPoints; j++, count++) {
                points[j + 1].x = (short int)tracePtr->screenPts[count].x;
                points[j + 1].y = (short int)tracePtr->screenPts[count].y;
            }
            XDrawLines(graphPtr->display, drawable, penPtr->traceGC, points, maxPoints + 1, CoordModeOrigin);
        }
        /*
         * Step 3: draw the final partial chunk.
         */
        remaining = tracePtr->nScreenPts - count;
        if (remaining > 0) {
            points[0].x = (short int)tracePtr->screenPts[count - 1].x;
            points[0].y = (short int)tracePtr->screenPts[count - 1].y;
            for (j = 0; j < remaining; j++, count++) {
                points[j + 1].x = (short int)tracePtr->screenPts[count].x;
                points[j + 1].y = (short int)tracePtr->screenPts[count].y;
            }
            XDrawLines(graphPtr->display, drawable, penPtr->traceGC, points, (int)remaining + 1, CoordModeOrigin);
        }
    }
    ckfree(points);
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
static void DrawValues(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts,
                       Point2D *symbolPts, const Tcl_Size *pointToData) {
    Point2D *pointPtr;
    Point2D *endPtr;
    Tcl_Size count;
    char string[RBC_VALUE_LABEL_SIZE];

    count = 0;
    for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
        Tcl_Size dataIndex;
        double x;
        double y;
        dataIndex = pointToData[count++];
        x = linePtr->core.x.valueArr[dataIndex];
        y = linePtr->core.y.valueArr[dataIndex];
        Rbc_FormatValueLabel(string, sizeof(string), penPtr->valueFormat, penPtr->valueShow, x, y);
        Rbc_DrawText(graphPtr->tkwin, drawable, string, &penPtr->valueStyle, (int)pointPtr->x, (int)pointPtr->y);
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
    Line *linePtr = LINE_FROM_CORE(elemPtr);
    LinePen *penPtr;

    penPtr = (elemPtr->activePenPtr != NULL) ? LINE_PEN_FROM_CORE(elemPtr->activePenPtr) : NULL;
    int symbolSize;

    if (penPtr == NULL) {
        return;
    }
    symbolSize = ScaleSymbol(elemPtr, penPtr->symbol.size);

    /*
     * nActiveIndices
     *      > 0        Some points are active.  Uses activeArr.
     *      < 0        All points are active.
     *    == 0        No points are active.
     */
    if (elemPtr->nActiveIndices > 0) {
        if (linePtr->core.flags & ACTIVE_PENDING) {
            MapActiveSymbols(graphPtr, linePtr);
        }
        if (penPtr->symbol.type != SYMBOL_NONE) {
            DrawSymbols(graphPtr, drawable, linePtr, penPtr, symbolSize, linePtr->nActivePts, linePtr->activePts);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            DrawValues(graphPtr, drawable, linePtr, penPtr, linePtr->nActivePts, linePtr->activePts,
                       linePtr->activeToData);
        }
    } else if (elemPtr->nActiveIndices < 0) {
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
    LinePen *normalPenPtr;
    Line *linePtr = LINE_FROM_CORE(elemPtr);
    normalPenPtr = LINE_PEN_FROM_CORE(elemPtr->normalPenPtr);
    LinePen *penPtr;
    Rbc_ChainLink *linkPtr;
    register LinePenStyle *stylePtr;
    Tcl_Size count;

    /* Fill area under the curve */
    if (linePtr->fillPts != NULL) {
        XPoint *points;
        int nPoints;

        nPoints = GetDrawablePolygonPointCount(graphPtr->display, linePtr->nFillPts);
        if (nPoints > 0) {
            points = (XPoint *)Tcl_AttemptAlloc((size_t)nPoints * sizeof(*points));
            if (points != NULL) {
                for (count = 0; count < linePtr->nFillPts; count++) {
                    points[count].x = (short int)linePtr->fillPts[count].x;
                    points[count].y = (short int)linePtr->fillPts[count].y;
                }
                if (linePtr->fillTile != NULL) {
                    Rbc_SetTileOrigin(graphPtr->tkwin, linePtr->fillTile, 0, 0);
                    Rbc_TilePolygon(graphPtr->tkwin, drawable, linePtr->fillTile, points, nPoints);
                } else if (linePtr->fillStipple != None) {
                    XFillPolygon(graphPtr->display, drawable, linePtr->fillGC, points, nPoints, Complex,
                                 CoordModeOrigin);
                }
                ckfree(points);
            }
        }
    }
    /* Lines: stripchart segments or graph traces. */
    if (linePtr->nStrips > 0) {
        for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            penPtr = stylePtr->penPtr;
            if ((stylePtr->nStrips > 0) && (penPtr->errorBarLineWidth > 0)) {
                Rbc_Draw2DSegments(graphPtr->display, drawable, penPtr->traceGC, stylePtr->strips, stylePtr->nStrips);
            }
        }
    } else if ((Rbc_ChainGetLength(linePtr->traces) > 0) && (normalPenPtr->traceWidth > 0)) {
        DrawTraces(graphPtr, drawable, linePtr, normalPenPtr);
    }

    if (linePtr->reqMaxSymbols > 0) {
        Tcl_Size total;

        total = 0;
        for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            total += stylePtr->nSymbolPts;
        }
        linePtr->symbolInterval = total / (Tcl_Size)linePtr->reqMaxSymbols;

        linePtr->symbolCounter = 0;
    }

    /* Symbols, error bars, values. */

    count = 0;
    for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
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
static void SymbolsToPostScript(Graph *graphPtr, PsToken psToken, LinePen *penPtr, int size, Tcl_Size nSymbolPts,
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
    LinePen *penPtr = LINE_PEN_FROM_CORE(elemPtr->normalPenPtr);

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
    register Point2D *pointPtr;
    register Point2D *endPtr;
    int count;

#define PS_MAXPATH 1500
    /*
     * Maximum number of components in a PostScript level-1 path.
     */

    SetLineAttributes(psToken, penPtr);
    for (linkPtr = Rbc_ChainFirstLink(linePtr->traces); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        tracePtr = Rbc_ChainGetValue(linkPtr);
        if (tracePtr->nScreenPts <= 0) {
            continue;
        }
        pointPtr = tracePtr->screenPts;
        Rbc_FormatToPostScript(psToken, " newpath %g %g moveto\n", pointPtr->x, pointPtr->y);
        pointPtr++;
        /*
         * Count path components, including the initial moveto.
         */
        count = 1;
        endPtr = tracePtr->screenPts + (tracePtr->nScreenPts - 1);
        for (; pointPtr < endPtr; pointPtr++) {
            Rbc_FormatToPostScript(psToken, " %g %g lineto\n", pointPtr->x, pointPtr->y);
            count++;
            /*
             * Finish the current path only after it has reached the
             * maximum size.  Restart at the current point so that the
             * following segment remains connected.
             */
            if (count >= PS_MAXPATH) {
                Rbc_FormatToPostScript(psToken,
                                       "DashesProc stroke\n"
                                       " newpath %g %g moveto\n",
                                       pointPtr->x, pointPtr->y);
                count = 1;
            }
        }
        Rbc_FormatToPostScript(psToken, " %g %g lineto\n", pointPtr->x, pointPtr->y);
        Rbc_AppendToPostScript(psToken, "DashesProc stroke\n", (char *)NULL);
    }

#undef PS_MAXPATH
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
static void ValuesToPostScript(PsToken psToken, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts, Point2D *symbolPts,
                               const Tcl_Size *pointToData) {
    Point2D *pointPtr;
    Point2D *endPtr;
    Tcl_Size count;
    char string[RBC_VALUE_LABEL_SIZE];

    count = 0;
    for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
        Tcl_Size dataIndex;
        double x;
        double y;

        dataIndex = pointToData[count++];
        x = linePtr->core.x.valueArr[dataIndex];
        y = linePtr->core.y.valueArr[dataIndex];
        Rbc_FormatValueLabel(string, sizeof(string), penPtr->valueFormat, penPtr->valueShow, x, y);
        Rbc_TextToPostScript(psToken, string, &penPtr->valueStyle, pointPtr->x, pointPtr->y);
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
    Line *linePtr;
    LinePen *penPtr;
    int symbolSize;

    linePtr = LINE_FROM_CORE(elemPtr);
    penPtr = (elemPtr->activePenPtr != NULL) ? LINE_PEN_FROM_CORE(elemPtr->activePenPtr) : NULL;

    if (penPtr == NULL) {
        return;
    }

    symbolSize = ScaleSymbol(elemPtr, penPtr->symbol.size);

    if (elemPtr->nActiveIndices > 0) {
        if (elemPtr->flags & ACTIVE_PENDING) {
            MapActiveSymbols(graphPtr, linePtr);
        }
        if (penPtr->symbol.type != SYMBOL_NONE) {
            SymbolsToPostScript(graphPtr, psToken, penPtr, symbolSize, linePtr->nActivePts, linePtr->activePts);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            ValuesToPostScript(psToken, linePtr, penPtr, linePtr->nActivePts, linePtr->activePts,
                               linePtr->activeToData);
        }
    } else if (elemPtr->nActiveIndices < 0) {
        if (penPtr->traceWidth > 0) {
            if (linePtr->nStrips > 0) {
                SetLineAttributes(psToken, penPtr);
                Rbc_2DSegmentsToPostScript(psToken, linePtr->strips, linePtr->nStrips);
            }
            if (Rbc_ChainGetLength(linePtr->traces) > 0) {
                TracesToPostScript(psToken, linePtr, penPtr);
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
    Line *linePtr = LINE_FROM_CORE(elemPtr);
    LinePen *normalPenPtr = LINE_PEN_FROM_CORE(elemPtr->normalPenPtr);
    register LinePenStyle *stylePtr;
    Rbc_ChainLink *linkPtr;
    LinePen *penPtr;
    Tcl_Size count;
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
        for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            penPtr = stylePtr->penPtr;
            if ((stylePtr->nStrips > 0) && (penPtr->traceWidth > 0)) {
                SetLineAttributes(psToken, penPtr);
                Rbc_2DSegmentsToPostScript(psToken, stylePtr->strips, stylePtr->nStrips);
            }
        }
    } else if ((Rbc_ChainGetLength(linePtr->traces) > 0) && (normalPenPtr->traceWidth > 0)) {
        TracesToPostScript(psToken, linePtr, normalPenPtr);
    }

    /* Draw symbols, error bars, values. */

    count = 0;
    for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
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
    Line *linePtr;

    linePtr = LINE_FROM_CORE(elemPtr);
    if (elemPtr->normalPenPtr != &linePtr->builtinPen.core) {
        Rbc_FreePen(graphPtr, elemPtr->normalPenPtr);
    }
    DestroyPen(graphPtr, &linePtr->builtinPen.core);
    if (elemPtr->activePenPtr != NULL) {
        Rbc_FreePen(graphPtr, elemPtr->activePenPtr);
    }
    Rbc_FreeElemVector(&elemPtr->w);
    Rbc_FreeElemVector(&elemPtr->x);
    Rbc_FreeElemVector(&elemPtr->xHigh);
    Rbc_FreeElemVector(&elemPtr->xLow);
    Rbc_FreeElemVector(&elemPtr->xError);
    Rbc_FreeElemVector(&elemPtr->y);
    Rbc_FreeElemVector(&elemPtr->yHigh);
    Rbc_FreeElemVector(&elemPtr->yLow);
    Rbc_FreeElemVector(&elemPtr->yError);
    ResetLine(linePtr);
    if (elemPtr->palette != NULL) {
        Rbc_FreePalette(graphPtr, elemPtr->palette);
        Rbc_ChainDestroy(elemPtr->palette);
        elemPtr->palette = NULL;
    }
    if (elemPtr->tags != NULL) {
        ckfree((char *)elemPtr->tags);
        elemPtr->tags = NULL;
    }
    if (elemPtr->activeIndices != NULL) {
        ckfree((char *)elemPtr->activeIndices);
        elemPtr->activeIndices = NULL;
        elemPtr->nActiveIndices = 0;
    }
    if (linePtr->fillPts != NULL) {
        ckfree((char *)linePtr->fillPts);
        linePtr->fillPts = NULL;
    }
    if (linePtr->fillTile != NULL) {
        Rbc_FreeTile(linePtr->fillTile);
        linePtr->fillTile = NULL;
    }
    if ((linePtr->fillStipple != None) && (linePtr->fillStipple != PATTERN_SOLID)) {
        Tk_FreeBitmap(graphPtr->display, linePtr->fillStipple);
        linePtr->fillStipple = None;
    }
    if (linePtr->fillGC != NULL) {
        Tk_FreeGC(graphPtr->display, linePtr->fillGC);
        linePtr->fillGC = NULL;
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
Element *Rbc_LineElement(Graph *graphPtr, const char *name, Rbc_Uid classUid) {
    Line *linePtr;
    Element *elemPtr;

    linePtr = RbcCalloc(1, sizeof(Line));
    if (linePtr == NULL) {
        return NULL;
    }
    elemPtr = &linePtr->core;
    if (classUid == rbcLineElementUid) {
        elemPtr->optionSpecs = lineElemOptionSpecs;
    } else {
        elemPtr->optionSpecs = stripElemOptionSpecs;
    }

    elemPtr->optionTable = NULL;

    elemPtr->optionMask = 0;
    elemPtr->optionObjc = 0;
    elemPtr->optionObjv = NULL;
    elemPtr->optionsConfigured = FALSE;

    elemPtr->optionsInitialized = FALSE;
    elemPtr->tkResourcesReleased = FALSE;
    elemPtr->procsPtr = &lineProcs;

    /*
     * By default an element's name and label are the same.
     */
    elemPtr->label = RbcStrdup(name);
    elemPtr->name = RbcStrdup(name);
    elemPtr->classUid = classUid;
    elemPtr->flags = SCALE_SYMBOL;
    elemPtr->graphPtr = graphPtr;
    elemPtr->labelRelief = TK_RELIEF_FLAT;

    InitPen(&linePtr->builtinPen, normalLinePenOptionSpecs, NORMAL_PEN);

    elemPtr->normalPenPtr = &linePtr->builtinPen.core;

    elemPtr->palette = Rbc_ChainCreate();

    linePtr->penDir = PEN_BOTH_DIRECTIONS;
    linePtr->reqSmooth = PEN_SMOOTH_NONE;
    return elemPtr;
}
