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
/** @file rbcGrLine.c
 * Line/strip/polar data, geometry, drawing and transaction contracts.
 */
/** @defgroup line_decimation Line decimation internals
 * Private screen-density reduction services. All objects are borrowed from
 * the owning graph; these routines neither mutate source vectors nor invoke Tcl.
 */

/** @defgroup line_fills Line fill internals
 * Private area and uncertainty-band geometry and screen drawing services.
 * Source data is borrowed; mapped polygons belong to the line element.
 */

/** @defgroup polar_data Polar element data internals
 * Private adapters from retained source data to Cartesian graph coordinates.
 */

/** @defgroup line_data_transactions Line and polar data transactions
 * Source ownership, parallel parameters, notification policy and staged commit.
 */

/** @defgroup line_option_transactions Line scalar and fill option transactions
 * Parsing, temporary ownership, resource transfer and tile invalidation.
 */

/** @defgroup line_geometry_lifecycle Line geometry and resource lifecycle
 * Private mapping, cache ownership and line-family pen/element lifecycle.
 */

/** @defgroup line_mapping_pipeline Line mapping pipeline
 * Private continuous-run, clipping, symbol and palette geometry services.
 */

/** @defgroup line_picking_rendering Line picking and stroke dispatch
 * Private distance searches, snapshot metadata and screen stroke adapters.
 */

/** @defgroup line_output_dispatch Line symbol and output dispatch
 * Private screen/export adapters, palette sampling and label lifetimes.
 */

/** @defgroup line_configuration_smoothing Line configuration and smoothing
 * Private configuration orchestration, resampling and scalar adapters.
 */

#include <math.h>
#include <stdint.h>
#include "rbcGraph.h"
#include "rbcRender.h"
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

typedef enum { LINE_DECIMATE_NONE, LINE_DECIMATE_AUTO } LineDecimation;

static const char *const lineDecimateNames[] = {"none", "auto", NULL};

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

#define LINE_DECIMATE_BLOCK_SIZE 64
#define LINE_DECIMATE_NATIVE_POINTS_PER_PIXEL 8
#define LINE_DECIMATE_CAIRO_POINTS_PER_PIXEL 4

typedef struct {
    Tcl_Size minIndex;
    Tcl_Size maxIndex;
    /*
     * Needed so an incremental Y-block rebuild can update the cache's
     * global logarithmic-domain eligibility without rescanning all Y.
     */
    Tcl_Size nonPositiveYCount;
} LineDecimateBlock;

typedef struct {
    LineDecimateBlock *blocks;
    Tcl_Size nBlocks;
    Tcl_Size nPoints;
    /*
     * Retained only for detecting an unexpected data-array replacement.
     * Cache entries themselves contain source indices, not pointers.
     */
    const double *xValues;
    const double *yValues;
    int built;
    int supported;
    /*
     * Source X ordering:
     *
     *   -1  decreasing
     *    0  all X values equal
     *   +1  increasing
     */
    int xDirection;
    /*
     * These allow the same cache to survive axis linear/log changes.
     */
    int allPositiveX;
    int allPositiveY;
    Tcl_Size nonPositiveYCount;
} LineDecimateCache;

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

typedef enum { LINE_DATA_XY, LINE_DATA_COMPLEX } LineDataMode;

typedef enum { LINE_SYMBOL_POINTS_ALL, LINE_SYMBOL_POINTS_REDUCED } LineSymbolPoints;
static const char *lineSymbolPointNames[] = {"all", "reduced", NULL};

typedef enum { LINE_AREA_BASELINE, LINE_AREA_CHORD, LINE_AREA_ORIGIN } LineAreaClose;
static const char *lineAreaCloseNames[] = {"baseline", "chord", "origin", NULL};
typedef enum { LINE_COORDINATES_CARTESIAN, LINE_COORDINATES_POLAR } LineDataCoordinates;

static const char *lineDataCoordinateNames[] = {"cartesian", "polar", NULL};

typedef struct {
    Rbc_Vector *vecPtr;
    Rbc_Complex *valueArr;
    Tcl_Size nValues;
    Tcl_Size arraySize;
    Rbc_VectorId clientId;
    Element *elemPtr;
} ElemComplexVector;

typedef struct {
    int staged;
    ElemComplexVector z;
    LineDataMode mode;
} LineComplexDataTransaction;

typedef struct {
    int staged;
    ElemVector param;
} LineParamTransaction;

typedef enum {
    LINE_CDATA_GAMMA,
    LINE_CDATA_IMPEDANCE,
    LINE_CDATA_ADMITTANCE
} LineComplexDataFormat;

static const char *const lineComplexDataFormatNames[] = {"gamma", "impedance", "admittance", NULL};

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
    Tcl_Obj *valueCommandObjPtr; /* Optional value-label command prefix. */
    Tcl_Obj *valueOffsetObjPtr; /* Pixel offset of the value-label anchor. */
    Point2D valueOffset;
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

/* Separate polygons keep missing-data runs disconnected. */
typedef struct LineBandPolygon {
    Point2D *points;
    Tcl_Size count;
    struct LineBandPolygon *nextPtr;
} LineBandPolygon;

typedef struct {
    int mode;
    Tcl_Obj *patternObjPtr;
    Tcl_Obj *tileObjPtr;
    XColor *foreground;
    XColor *background;
    double opacity;
    Pixmap stipple;
    Rbc_Tile tile;
    GC gc;
    LineBandPolygon *polygons;
} LineErrorBand;

typedef enum { LINE_BAND_NONE, LINE_BAND_Y, LINE_BAND_X } LineBandMode;

static const char *lineErrorBandNames[] = {"none", "y", "x", NULL};

typedef struct {
    Element core;
    /*
     * Line specific configurable attributes
     */
    LinePen builtinPen;
    LineErrorBand errorBand;
    /*
     * Original Tcl representations for line-element options that
     * require validation or conversion after Tk_SetOptions.
     */
    Tcl_Obj *areaPatternObjPtr;
    Tcl_Obj *areaTileObjPtr;
    Tcl_Obj *maxSymbolsObjPtr;
    Tcl_Obj *smoothObjPtr;
    Tcl_Obj *traceObjPtr;
    Tcl_Obj *cDataObjPtr;
    Tcl_Obj *paramObjPtr;
    ElemComplexVector z;
    ElemVector param;
    LineDataMode dataMode;
    LineComplexDataFormat cDataFormat;
    LineDataCoordinates dataCoordinates; /* Interpretation of real input pairs. */
    double z0;
    /* Line smoothing */
    Smoothing reqSmooth; /* Requested smoothing function to use
                          * for connecting the data points */
    Smoothing smooth; /* Smoothing function used. */
    double rTolerance; /* Tolerance to reduce the number of
                        * points displayed. */
    LineSymbolPoints symbolPoints; /* Source samples eligible for normal symbols. */
    LineDecimation decimate;
    LineDecimateCache decimateCache;
    /*
     * True when the current screen mapping intentionally omitted the
     * full symbolPts/symbolToData arrays because the trace was reduced
     * before world-to-screen mapping or -symbolpoints selected a subset.
     */
    int pointMapOmitted;
    /*
     * Drawing related data structures.
     */
    /* Area-under-curve fill attributes. */
    XColor *fillFgColor;
    XColor *fillBgColor;
    GC fillGC;
    LineAreaClose areaClose; /* Closure of the mapped fill polygon. */
    double areaOpacity; /* Cairo solid fill opacity, from zero to one. */
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
#define DEF_LINE_HIDE_PLOT "no"
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
#define DEF_LINE_SCALE_SYMBOLS "no"
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
#define DEF_LINE_CDATA_FORMAT "gamma"
#define DEF_LINE_Z0 "50.0"
#define DEF_LINE_DECIMATE "none"

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
#define LINE_ELEM_CDATA_MASK (1 << 13)
#define LINE_ELEM_CDATA_FORMAT_MASK (1 << 14)
#define LINE_ELEM_Z0_MASK (1 << 15)
#define LINE_ELEM_PARAM_MASK (1 << 16)
#define LINE_ELEM_ERROR_BAND_MASK (1 << 17)

#define LINE_ELEM_COMPLEX_TRANSFORM_MASK (LINE_ELEM_CDATA_FORMAT_MASK | LINE_ELEM_Z0_MASK)
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
        TK_OPTION_STRING_TABLE,                                               \
        "-areaclose", "areaClose", "AreaClose",                               \
        "baseline",                                                           \
        -1,                                                                   \
        offsetof(Line, areaClose),                                            \
        0,                                                                    \
        (ClientData)lineAreaCloseNames,                                       \
        LINE_ELEM_MAP_ITEM_MASK                                               \
    },                                                                        \
    {                                                                         \
        TK_OPTION_DOUBLE,                                                     \
        "-areaopacity", "areaOpacity", "AreaOpacity",                         \
        "1.0",                                                                \
        -1,                                                                   \
        offsetof(Line, areaOpacity),                                         \
        0,                                                                    \
        NULL,                                                                 \
        LINE_ELEM_AREA_MASK                                                   \
    },                                                                        \
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
        0,                                                                    \
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

#define LINE_ELEMENT_OPTION_ENTRIES(AREA_ENTRIES, REDUCE_ENTRY, STATE_ENTRY, TRACE_ENTRY)                              \
    {TK_OPTION_STRING,                                                                                                 \
     "-activepen",                                                                                                     \
     "activePen",                                                                                                      \
     "ActivePen",                                                                                                      \
     DEF_LINE_ACTIVE_PEN,                                                                                              \
     LINE_CORE_OFFSET(activePenObjPtr),                                                                                \
     -1,                                                                                                               \
     TK_OPTION_NULL_OK,                                                                                                \
     NULL,                                                                                                             \
     LINE_ELEM_PEN_MASK},                                                                                              \
        AREA_ENTRIES{TK_OPTION_STRING,                                                                                 \
                     "-bindtags",                                                                                      \
                     "bindTags",                                                                                       \
                     "BindTags",                                                                                       \
                     DEF_LINE_TAGS,                                                                                    \
                     LINE_CORE_OFFSET(bindTagsObjPtr),                                                                 \
                     -1,                                                                                               \
                     TK_OPTION_NULL_OK,                                                                                \
                     NULL,                                                                                             \
                     LINE_ELEM_TAGS_MASK},                                                                             \
        {TK_OPTION_COLOR,                                                                                              \
         "-color",                                                                                                     \
         "color",                                                                                                      \
         "Color",                                                                                                      \
         DEF_LINE_PEN_COLOR,                                                                                           \
         -1,                                                                                                           \
         LINE_BUILTIN_PEN_OFFSET(traceColor),                                                                          \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-dashes",                                                                                                    \
         "dashes",                                                                                                     \
         "Dashes",                                                                                                     \
         DEF_LINE_DASHES,                                                                                              \
         LINE_BUILTIN_PEN_OFFSET(dashesObjPtr),                                                                        \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-data",                                                                                                      \
         "data",                                                                                                       \
         "Data",                                                                                                       \
         DEF_LINE_DATA,                                                                                                \
         LINE_CORE_OFFSET(dataObjPtr),                                                                                 \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                               \
        {TK_OPTION_STRING,                                                                                             \
         "-errorbarcolor",                                                                                             \
         "errorBarColor",                                                                                              \
         "ErrorBarColor",                                                                                              \
         DEF_LINE_ERRORBAR_COLOR,                                                                                      \
         LINE_BUILTIN_PEN_OFFSET(errorBarColorObjPtr),                                                                 \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-errorbarwidth",                                                                                             \
         "errorBarWidth",                                                                                              \
         "ErrorBarWidth",                                                                                              \
         DEF_LINE_ERRORBAR_LINE_WIDTH,                                                                                 \
         LINE_BUILTIN_PEN_OFFSET(errorBarWidthObjPtr),                                                                 \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-errorbarcap",                                                                                               \
         "errorBarCap",                                                                                                \
         "ErrorBarCap",                                                                                                \
         DEF_LINE_ERRORBAR_CAP_WIDTH,                                                                                  \
         LINE_BUILTIN_PEN_OFFSET(errorBarCapObjPtr),                                                                   \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                        \
        {TK_OPTION_STRING,                                                                                             \
         "-fill",                                                                                                      \
         "fill",                                                                                                       \
         "Fill",                                                                                                       \
         DEF_LINE_FILL_COLOR,                                                                                          \
         LINE_BUILTIN_PEN_OFFSET(fillObjPtr),                                                                          \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_BOOLEAN,      "-hide", "hide", "Hide", DEF_LINE_HIDE, -1, LINE_CORE_OFFSET(hidden), 0, NULL,        \
         LINE_ELEM_MAP_ITEM_MASK},                                                                                     \
        {TK_OPTION_BOOLEAN,                                                                                            \
         "-hideplot",                                                                                                  \
         "hidePlot",                                                                                                   \
         "HidePlot",                                                                                                   \
         DEF_LINE_HIDE_PLOT,                                                                                           \
         -1,                                                                                                           \
         LINE_CORE_OFFSET(plotHidden),                                                                                 \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_MAP_ITEM_MASK},                                                                                     \
        {TK_OPTION_STRING,                                                                                             \
         "-label",                                                                                                     \
         "label",                                                                                                      \
         "Label",                                                                                                      \
         DEF_LINE_LABEL,                                                                                               \
         -1,                                                                                                           \
         LINE_CORE_OFFSET(label),                                                                                      \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_MAP_ITEM_MASK},                                                                                     \
        {TK_OPTION_RELIEF,                                                                                             \
         "-labelrelief",                                                                                               \
         "labelRelief",                                                                                                \
         "LabelRelief",                                                                                                \
         DEF_LINE_LABEL_RELIEF,                                                                                        \
         -1,                                                                                                           \
         LINE_CORE_OFFSET(labelRelief),                                                                                \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_MAP_ITEM_MASK},                                                                                     \
        {TK_OPTION_STRING,                                                                                             \
         "-linewidth",                                                                                                 \
         "lineWidth",                                                                                                  \
         "LineWidth",                                                                                                  \
         DEF_LINE_PEN_WIDTH,                                                                                           \
         LINE_BUILTIN_PEN_OFFSET(lineWidthObjPtr),                                                                     \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-mapx",                                                                                                      \
         "mapX",                                                                                                       \
         "MapX",                                                                                                       \
         DEF_LINE_AXIS_X,                                                                                              \
         LINE_CORE_OFFSET(mapXObjPtr),                                                                                 \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_AXES_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                               \
        {TK_OPTION_STRING,                                                                                             \
         "-mapy",                                                                                                      \
         "mapY",                                                                                                       \
         "MapY",                                                                                                       \
         DEF_LINE_AXIS_Y,                                                                                              \
         LINE_CORE_OFFSET(mapYObjPtr),                                                                                 \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_AXES_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                               \
        {TK_OPTION_STRING,                                                                                             \
         "-track",                                                                                                     \
         "track",                                                                                                      \
         "Track",                                                                                                      \
         "",                                                                                                           \
         LINE_CORE_OFFSET(trackObjPtr),                                                                                \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_MAP_ITEM_MASK},                                                                                     \
        {TK_OPTION_STRING_TABLE,                                                                                       \
         "-symbolpoints",                                                                                              \
         "symbolPoints",                                                                                               \
         "SymbolPoints",                                                                                               \
         "all",                                                                                                        \
         -1,                                                                                                           \
         offsetof(Line, symbolPoints),                                                                                 \
         0,                                                                                                            \
         (ClientData)lineSymbolPointNames,                                                                             \
         LINE_ELEM_MAP_ITEM_MASK},                                                                                     \
        {TK_OPTION_STRING,                                                                                             \
         "-maxsymbols",                                                                                                \
         "maxSymbols",                                                                                                 \
         "MaxSymbols",                                                                                                 \
         DEF_LINE_MAX_SYMBOLS,                                                                                         \
         offsetof(Line, maxSymbolsObjPtr),                                                                             \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_MAX_SYMBOLS_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                        \
        {TK_OPTION_STRING,                                                                                             \
         "-offdash",                                                                                                   \
         "offDash",                                                                                                    \
         "OffDash",                                                                                                    \
         DEF_LINE_OFFDASH_COLOR,                                                                                       \
         LINE_BUILTIN_PEN_OFFSET(offDashObjPtr),                                                                       \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-outline",                                                                                                   \
         "outline",                                                                                                    \
         "Outline",                                                                                                    \
         DEF_LINE_OUTLINE_COLOR,                                                                                       \
         LINE_BUILTIN_PEN_OFFSET(outlineObjPtr),                                                                       \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-outlinewidth",                                                                                              \
         "outlineWidth",                                                                                               \
         "OutlineWidth",                                                                                               \
         DEF_LINE_OUTLINE_WIDTH,                                                                                       \
         LINE_BUILTIN_PEN_OFFSET(outlineWidthObjPtr),                                                                  \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-pen",                                                                                                       \
         "pen",                                                                                                        \
         "Pen",                                                                                                        \
         NULL,                                                                                                         \
         LINE_CORE_OFFSET(normalPenObjPtr),                                                                            \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_PEN_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                                \
        {TK_OPTION_STRING,                                                                                             \
         "-pixels",                                                                                                    \
         "pixels",                                                                                                     \
         "Pixels",                                                                                                     \
         DEF_LINE_PIXELS,                                                                                              \
         LINE_BUILTIN_PEN_OFFSET(pixelsObjPtr),                                                                        \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                        \
        REDUCE_ENTRY{TK_OPTION_BOOLEAN,                                                                                \
                     "-scalesymbols",                                                                                  \
                     "scaleSymbols",                                                                                   \
                     "ScaleSymbols",                                                                                   \
                     DEF_LINE_SCALE_SYMBOLS,                                                                           \
                     -1,                                                                                               \
                     LINE_CORE_OFFSET(scaleSymbols),                                                                   \
                     0,                                                                                                \
                     NULL,                                                                                             \
                     LINE_ELEM_SCALE_SYMBOL_MASK},                                                                     \
        {TK_OPTION_STRING,                                                                                             \
         "-showerrorbars",                                                                                             \
         "showErrorBars",                                                                                              \
         "ShowErrorBars",                                                                                              \
         DEF_LINE_SHOW_ERRORBARS,                                                                                      \
         LINE_BUILTIN_PEN_OFFSET(showErrorBarsObjPtr),                                                                 \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-showvalues",                                                                                                \
         "showValues",                                                                                                 \
         "ShowValues",                                                                                                 \
         DEF_PEN_SHOW_VALUES,                                                                                          \
         LINE_BUILTIN_PEN_OFFSET(showValuesObjPtr),                                                                    \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-smooth",                                                                                                    \
         "smooth",                                                                                                     \
         "Smooth",                                                                                                     \
         DEF_LINE_SMOOTH,                                                                                              \
         offsetof(Line, smoothObjPtr),                                                                                 \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_SMOOTH_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                             \
        STATE_ENTRY{TK_OPTION_STRING,                                                                                  \
                    "-styles",                                                                                         \
                    "styles",                                                                                          \
                    "Styles",                                                                                          \
                    DEF_LINE_STYLES,                                                                                   \
                    LINE_CORE_OFFSET(stylesObjPtr),                                                                    \
                    -1,                                                                                                \
                    TK_OPTION_NULL_OK,                                                                                 \
                    NULL,                                                                                              \
                    LINE_ELEM_STYLES_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-symbol",                                                                                                    \
         "symbol",                                                                                                     \
         "Symbol",                                                                                                     \
         DEF_LINE_SYMBOL,                                                                                              \
         LINE_BUILTIN_PEN_OFFSET(symbolObjPtr),                                                                        \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        TRACE_ENTRY{TK_OPTION_ANCHOR,                                                                                  \
                    "-valueanchor",                                                                                    \
                    "valueAnchor",                                                                                     \
                    "ValueAnchor",                                                                                     \
                    DEF_PEN_VALUE_ANCHOR,                                                                              \
                    -1,                                                                                                \
                    LINE_BUILTIN_PEN_OFFSET(valueStyle.anchor),                                                        \
                    0,                                                                                                 \
                    NULL,                                                                                              \
                    LINE_ELEM_BUILTIN_PEN_MASK},                                                                       \
        {TK_OPTION_COLOR,                                                                                              \
         "-valuecolor",                                                                                                \
         "valueColor",                                                                                                 \
         "ValueColor",                                                                                                 \
         DEF_PEN_VALUE_COLOR,                                                                                          \
         -1,                                                                                                           \
         LINE_BUILTIN_PEN_OFFSET(valueStyle.color),                                                                    \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_FONT,                                                                                               \
         "-valuefont",                                                                                                 \
         "valueFont",                                                                                                  \
         "ValueFont",                                                                                                  \
         DEF_PEN_VALUE_FONT,                                                                                           \
         -1,                                                                                                           \
         LINE_BUILTIN_PEN_OFFSET(valueStyle.font),                                                                     \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-valueoffset",                                                                                               \
         "valueOffset",                                                                                                \
         "ValueOffset",                                                                                                \
         "0 0",                                                                                                        \
         LINE_BUILTIN_PEN_OFFSET(valueOffsetObjPtr),                                                                   \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-valuecommand",                                                                                              \
         "valueCommand",                                                                                               \
         "ValueCommand",                                                                                               \
         NULL,                                                                                                         \
         LINE_BUILTIN_PEN_OFFSET(valueCommandObjPtr),                                                                  \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-valueformat",                                                                                               \
         "valueFormat",                                                                                                \
         "ValueFormat",                                                                                                \
         DEF_PEN_VALUE_FORMAT,                                                                                         \
         -1,                                                                                                           \
         LINE_BUILTIN_PEN_OFFSET(valueFormat),                                                                         \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_DOUBLE,                                                                                             \
         "-valuerotate",                                                                                               \
         "valueRotate",                                                                                                \
         "ValueRotate",                                                                                                \
         "0.0",                                                                                                        \
         LINE_BUILTIN_PEN_OFFSET(valueRotateObjPtr),                                                                   \
         LINE_BUILTIN_PEN_OFFSET(valueStyle.theta),                                                                    \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-valueshadow",                                                                                               \
         "valueShadow",                                                                                                \
         "ValueShadow",                                                                                                \
         DEF_PEN_VALUE_SHADOW,                                                                                         \
         LINE_BUILTIN_PEN_OFFSET(valueShadowObjPtr),                                                                   \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_BUILTIN_PEN_MASK},                                                                                  \
        {TK_OPTION_STRING,                                                                                             \
         "-weights",                                                                                                   \
         "weights",                                                                                                    \
         "Weights",                                                                                                    \
         NULL,                                                                                                         \
         LINE_CORE_OFFSET(weightsObjPtr),                                                                              \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                               \
        {TK_OPTION_STRING,          "-x", "xData",           "XData", DEF_LINE_X_DATA,                                 \
         LINE_CORE_OFFSET(xObjPtr), -1,   TK_OPTION_NULL_OK, NULL,    LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK},  \
        {TK_OPTION_SYNONYM, "-xdata", NULL, NULL, NULL, -1, -1, 0, "-x", 0},                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-xerror",                                                                                                    \
         "xError",                                                                                                     \
         "XError",                                                                                                     \
         NULL,                                                                                                         \
         LINE_CORE_OFFSET(xErrorObjPtr),                                                                               \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                               \
        {TK_OPTION_STRING,                                                                                             \
         "-xhigh",                                                                                                     \
         "xHigh",                                                                                                      \
         "XHigh",                                                                                                      \
         NULL,                                                                                                         \
         LINE_CORE_OFFSET(xHighObjPtr),                                                                                \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                               \
        {TK_OPTION_STRING,                                                                                             \
         "-xlow",                                                                                                      \
         "xLow",                                                                                                       \
         "XLow",                                                                                                       \
         NULL,                                                                                                         \
         LINE_CORE_OFFSET(xLowObjPtr),                                                                                 \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                               \
        {TK_OPTION_STRING,          "-y", "yData",           "YData", DEF_LINE_Y_DATA,                                 \
         LINE_CORE_OFFSET(yObjPtr), -1,   TK_OPTION_NULL_OK, NULL,    LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK},  \
        {TK_OPTION_SYNONYM, "-ydata", NULL, NULL, NULL, -1, -1, 0, "-y", 0},                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-yerror",                                                                                                    \
         "yError",                                                                                                     \
         "YError",                                                                                                     \
         NULL,                                                                                                         \
         LINE_CORE_OFFSET(yErrorObjPtr),                                                                               \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                               \
        {TK_OPTION_STRING,                                                                                             \
         "-yhigh",                                                                                                     \
         "yHigh",                                                                                                      \
         "YHigh",                                                                                                      \
         NULL,                                                                                                         \
         LINE_CORE_OFFSET(yHighObjPtr),                                                                                \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                               \
    {                                                                                                                  \
        TK_OPTION_STRING, "-ylow", "yLow", "YLow", NULL, LINE_CORE_OFFSET(yLowObjPtr), -1, TK_OPTION_NULL_OK, NULL,    \
            LINE_ELEM_DATA_MASK | LINE_ELEM_MAP_ITEM_MASK                                                              \
    }

#define LINE_BAND_OFFSET(member) (offsetof(Line, errorBand) + offsetof(LineErrorBand, member))
#define LINE_ELEMENT_ERROR_BAND_OPTION_ENTRIES                                                                         \
    {TK_OPTION_STRING_TABLE,                                                                                           \
     "-errorband",                                                                                                     \
     "errorBand",                                                                                                      \
     "ErrorBand",                                                                                                      \
     "none",                                                                                                           \
     -1,                                                                                                               \
     LINE_BAND_OFFSET(mode),                                                                                           \
     0,                                                                                                                \
     (ClientData)lineErrorBandNames,                                                                                   \
     LINE_ELEM_MAP_ITEM_MASK},                                                                                         \
        {TK_OPTION_STRING,                                                                                             \
         "-errorbandpattern",                                                                                          \
         "errorBandPattern",                                                                                           \
         "ErrorBandPattern",                                                                                           \
         "solid",                                                                                                      \
         LINE_BAND_OFFSET(patternObjPtr),                                                                              \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_ERROR_BAND_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                         \
        {TK_OPTION_STRING,                                                                                             \
         "-errorbandtile",                                                                                             \
         "errorBandTile",                                                                                              \
         "ErrorBandTile",                                                                                              \
         NULL,                                                                                                         \
         LINE_BAND_OFFSET(tileObjPtr),                                                                                 \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_ERROR_BAND_MASK | LINE_ELEM_MAP_ITEM_MASK},                                                         \
        {TK_OPTION_COLOR,                                                                                              \
         "-errorbandforeground",                                                                                       \
         "errorBandForeground",                                                                                        \
         "ErrorBandForeground",                                                                                        \
         DEF_LINE_PATTERN_FG,                                                                                          \
         -1,                                                                                                           \
         LINE_BAND_OFFSET(foreground),                                                                                 \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_ERROR_BAND_MASK},                                                                                   \
        {TK_OPTION_COLOR,                                                                                              \
         "-errorbandbackground",                                                                                       \
         "errorBandBackground",                                                                                        \
         "ErrorBandBackground",                                                                                        \
         DEF_LINE_PATTERN_BG,                                                                                          \
         -1,                                                                                                           \
         LINE_BAND_OFFSET(background),                                                                                 \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_ERROR_BAND_MASK},                                                                                   \
        {TK_OPTION_DOUBLE,                                                                                             \
         "-errorbandopacity",                                                                                          \
         "errorBandOpacity",                                                                                           \
         "ErrorBandOpacity",                                                                                           \
         "1.0",                                                                                                        \
         -1,                                                                                                           \
         LINE_BAND_OFFSET(opacity),                                                                                    \
         0,                                                                                                            \
         NULL,                                                                                                         \
         LINE_ELEM_ERROR_BAND_MASK},

static const Tk_OptionSpec lineElemOptionSpecs[] = {
    LINE_ELEMENT_ERROR_BAND_OPTION_ENTRIES LINE_ELEMENT_OPTION_ENTRIES(
        LINE_ELEMENT_AREA_OPTION_ENTRIES, LINE_ELEMENT_REDUCE_OPTION_ENTRY, LINE_ELEMENT_STATE_OPTION_ENTRY,
        LINE_ELEMENT_TRACE_OPTION_ENTRY),
    {TK_OPTION_STRING_TABLE, "-decimate", "decimate", "Decimate", DEF_LINE_DECIMATE, -1, offsetof(Line, decimate), 0,
     (ClientData)lineDecimateNames, LINE_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-param", "param", "Param", NULL, offsetof(Line, paramObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     LINE_ELEM_PARAM_MASK},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

static const Tk_OptionSpec stripElemOptionSpecs[] = {
    LINE_ELEMENT_ERROR_BAND_OPTION_ENTRIES LINE_ELEMENT_OPTION_ENTRIES(
        LINE_ELEMENT_NO_OPTION_ENTRIES, LINE_ELEMENT_REDUCE_OPTION_ENTRY, LINE_ELEMENT_NO_OPTION_ENTRIES,
        LINE_ELEMENT_NO_OPTION_ENTRIES),
    {TK_OPTION_STRING_TABLE, "-decimate", "decimate", "Decimate", DEF_LINE_DECIMATE, -1, offsetof(Line, decimate), 0,
     (ClientData)lineDecimateNames, LINE_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

#define LINE_PEN_OPTION_ENTRIES(DEFAULT_COLOR)                                                                         \
    {TK_OPTION_COLOR, "-color", "color", "Color", DEFAULT_COLOR, -1, offsetof(LinePen, traceColor), 0, NULL, 0},       \
        {TK_OPTION_STRING,  "-dashes", "dashes", "Dashes", DEF_PEN_DASHES, offsetof(LinePen, dashesObjPtr), -1,        \
         TK_OPTION_NULL_OK, NULL,      0},                                                                             \
        {TK_OPTION_STRING,                                                                                             \
         "-errorbarcolor",                                                                                             \
         "errorBarColor",                                                                                              \
         "ErrorBarColor",                                                                                              \
         DEF_LINE_ERRORBAR_COLOR,                                                                                      \
         offsetof(LinePen, errorBarColorObjPtr),                                                                       \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-errorbarwidth",                                                                                             \
         "errorBarWidth",                                                                                              \
         "ErrorBarWidth",                                                                                              \
         DEF_LINE_ERRORBAR_LINE_WIDTH,                                                                                 \
         offsetof(LinePen, errorBarWidthObjPtr),                                                                       \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-errorbarcap",                                                                                               \
         "errorBarCap",                                                                                                \
         "ErrorBarCap",                                                                                                \
         DEF_LINE_ERRORBAR_CAP_WIDTH,                                                                                  \
         offsetof(LinePen, errorBarCapObjPtr),                                                                         \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,  "-fill", "fill", "Fill", DEF_PEN_FILL_COLOR, offsetof(LinePen, fillObjPtr), -1,            \
         TK_OPTION_NULL_OK, NULL,    0},                                                                               \
        {TK_OPTION_STRING,                                                                                             \
         "-linewidth",                                                                                                 \
         "lineWidth",                                                                                                  \
         "LineWidth",                                                                                                  \
         DEF_PEN_LINE_WIDTH,                                                                                           \
         offsetof(LinePen, lineWidthObjPtr),                                                                           \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-offdash",                                                                                                   \
         "offDash",                                                                                                    \
         "OffDash",                                                                                                    \
         DEF_PEN_OFFDASH_COLOR,                                                                                        \
         offsetof(LinePen, offDashObjPtr),                                                                             \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-outline",                                                                                                   \
         "outline",                                                                                                    \
         "Outline",                                                                                                    \
         DEF_PEN_OUTLINE_COLOR,                                                                                        \
         offsetof(LinePen, outlineObjPtr),                                                                             \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-outlinewidth",                                                                                              \
         "outlineWidth",                                                                                               \
         "OutlineWidth",                                                                                               \
         DEF_PEN_OUTLINE_WIDTH,                                                                                        \
         offsetof(LinePen, outlineWidthObjPtr),                                                                        \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-pixels",                                                                                                    \
         "pixels",                                                                                                     \
         "Pixels",                                                                                                     \
         DEF_PEN_PIXELS,                                                                                               \
         offsetof(LinePen, pixelsObjPtr),                                                                              \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-showerrorbars",                                                                                             \
         "showErrorBars",                                                                                              \
         "ShowErrorBars",                                                                                              \
         DEF_LINE_SHOW_ERRORBARS,                                                                                      \
         offsetof(LinePen, showErrorBarsObjPtr),                                                                       \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-showvalues",                                                                                                \
         "showValues",                                                                                                 \
         "ShowValues",                                                                                                 \
         DEF_PEN_SHOW_VALUES,                                                                                          \
         offsetof(LinePen, showValuesObjPtr),                                                                          \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-symbol",                                                                                                    \
         "symbol",                                                                                                     \
         "Symbol",                                                                                                     \
         DEF_PEN_SYMBOL,                                                                                               \
         offsetof(LinePen, symbolObjPtr),                                                                              \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,  "-type", NULL, NULL, DEF_PEN_TYPE, -1, LINE_PEN_CORE_OFFSET(typeId),                       \
         TK_OPTION_NULL_OK, NULL,    0},                                                                               \
        {TK_OPTION_ANCHOR,                                                                                             \
         "-valueanchor",                                                                                               \
         "valueAnchor",                                                                                                \
         "ValueAnchor",                                                                                                \
         DEF_PEN_VALUE_ANCHOR,                                                                                         \
         -1,                                                                                                           \
         offsetof(LinePen, valueStyle.anchor),                                                                         \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_COLOR,                                                                                              \
         "-valuecolor",                                                                                                \
         "valueColor",                                                                                                 \
         "ValueColor",                                                                                                 \
         DEF_PEN_VALUE_COLOR,                                                                                          \
         -1,                                                                                                           \
         offsetof(LinePen, valueStyle.color),                                                                          \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_FONT,                                                                                               \
         "-valuefont",                                                                                                 \
         "valueFont",                                                                                                  \
         "ValueFont",                                                                                                  \
         DEF_PEN_VALUE_FONT,                                                                                           \
         -1,                                                                                                           \
         offsetof(LinePen, valueStyle.font),                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-valueoffset",                                                                                               \
         "valueOffset",                                                                                                \
         "ValueOffset",                                                                                                \
         "0 0",                                                                                                        \
         offsetof(LinePen, valueOffsetObjPtr),                                                                         \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-valuecommand",                                                                                              \
         "valueCommand",                                                                                               \
         "ValueCommand",                                                                                               \
         NULL,                                                                                                         \
         offsetof(LinePen, valueCommandObjPtr),                                                                        \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-valueformat",                                                                                               \
         "valueFormat",                                                                                                \
         "ValueFormat",                                                                                                \
         DEF_PEN_VALUE_FORMAT,                                                                                         \
         -1,                                                                                                           \
         offsetof(LinePen, valueFormat),                                                                               \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_DOUBLE,                                                                                             \
         "-valuerotate",                                                                                               \
         "valueRotate",                                                                                                \
         "ValueRotate",                                                                                                \
         "0.0",                                                                                                        \
         offsetof(LinePen, valueRotateObjPtr),                                                                         \
         offsetof(LinePen, valueStyle.theta),                                                                          \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-valueshadow",                                                                                               \
         "valueShadow",                                                                                                \
         "ValueShadow",                                                                                                \
         DEF_PEN_VALUE_SHADOW,                                                                                         \
         offsetof(LinePen, valueShadowObjPtr),                                                                         \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}

static const Tk_OptionSpec normalLinePenOptionSpecs[] = {LINE_PEN_OPTION_ENTRIES(DEF_PEN_NORMAL_COLOR)};

static const Tk_OptionSpec activeLinePenOptionSpecs[] = {LINE_PEN_OPTION_ENTRIES(DEF_PEN_ACTIVE_COLOR)};

static const Tk_OptionSpec polarElemOptionSpecs[] = {
    {TK_OPTION_STRING_TABLE, "-datacoordinates", "dataCoordinates", "DataCoordinates", "cartesian", -1,
     offsetof(Line, dataCoordinates), 0, (ClientData)lineDataCoordinateNames, LINE_ELEM_MAP_ITEM_MASK},
    LINE_ELEMENT_OPTION_ENTRIES(LINE_ELEMENT_AREA_OPTION_ENTRIES, LINE_ELEMENT_REDUCE_OPTION_ENTRY,
                                LINE_ELEMENT_STATE_OPTION_ENTRY, LINE_ELEMENT_TRACE_OPTION_ENTRY),
    {TK_OPTION_STRING, "-param", "param", "Param", NULL, offsetof(Line, paramObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     LINE_ELEM_PARAM_MASK},
    {TK_OPTION_STRING, "-cdata", "cData", "CData", NULL, offsetof(Line, cDataObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     LINE_ELEM_CDATA_MASK | LINE_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING_TABLE, "-cdataformat", "cDataFormat", "CDataFormat", DEF_LINE_CDATA_FORMAT, -1,
     offsetof(Line, cDataFormat), 0, (ClientData)lineComplexDataFormatNames,
     LINE_ELEM_CDATA_FORMAT_MASK | LINE_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_DOUBLE, "-z0", "z0", "Z0", DEF_LINE_Z0, -1, offsetof(Line, z0), 0, NULL,
     LINE_ELEM_Z0_MASK | LINE_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

typedef double(DistanceProc)(int x, int y, Point2D *p, Point2D *q, Point2D *t);

/* Forward declarations */
static PenConfigureProc ConfigurePen;
static PenDestroyProc DestroyPen;
static ElementClosestProc ClosestLine;
static ElementClosestInfoProc LineClosestInfo;
static ElementClosestInfoProc PolarClosestInfo;
static ElementConfigProc ConfigureLine;
static ElementDestroyProc DestroyLine;
static ElementDrawProc DrawActiveLine;
static ElementDrawProc DrawNormalLine;
static ElementDrawSymbolProc DrawSymbol;
static ElementExtentsProc GetLineExtents;
static ElementExportProc ActiveLineExport;
static ElementExportProc NormalLineExport;
static ElementSymbolExportProc SymbolExport;
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
static int CanPixelDecimateLine(Line *linePtr);
static int LineNeedsPixelDecimation(Graph *graphPtr, Line *linePtr);
static void InvalidateLineDecimateCache(Line *linePtr);
static int CanPreMapDecimateLine(Line *linePtr);
static double MapLineDataAbscissa(Graph *graphPtr, Line *linePtr, double x);
static int BuildPixelDecimatedScreenPoints(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static int IsMonotonicMappedAbscissa(Graph *graphPtr, MapInfo *mapPtr);
static int CanRetainLineDecimatePrefix(const ElemVector *vPtr, const double *cachedValues, Tcl_Size nPoints);
static int GrowLineDecimateCache(Line *linePtr, Tcl_Size nPoints);
static void DecimatePointsByPixels(Graph *graphPtr, MapInfo *mapPtr);
static void GenerateSteps(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void GenerateSpline(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr);
static void GenerateSplineRuns(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr, int parametric);
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
static void ClosestPoint(Graph *graphPtr, Line *linePtr, ClosestSearch *searchPtr);
static void DrawSymbols(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, int size,
                        Tcl_Size nSymbolPts, Point2D *symbolPts);
static void DrawTraces(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr);
static void DrawValues(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts,
                       Point2D *symbolPts, const Tcl_Size *pointToData, Tcl_Size interval, Tcl_Size offset);
static void SymbolsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, LinePen *penPtr, int size, Tcl_Size nSymbolPts,
                                Point2D *symbolPts, Tcl_Size interval, Tcl_Size *counterPtr);
static Rbc_RenderContext *BeginLineExport(Rbc_ExportContext *exportPtr, LinePen *penPtr);
static void TracesExport(Rbc_ExportContext *exportPtr, Line *linePtr, LinePen *penPtr);
static void ValuesExport(Rbc_ExportContext *exportPtr, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts,
                         Point2D *symbolPts, const Tcl_Size *pointToData, Tcl_Size interval, Tcl_Size offset);

/**
 * @ingroup line_configuration_smoothing
 * @brief Recognize a nonempty case-sensitive prefix of a pen keyword.
 *
 * Compares the supplied byte length with the full keyword and then strncmp.
 * Borrows both strings; returns FALSE for empty or overlong prefixes. Does not
 * perform abbreviation disambiguation against other keywords.
 */
static int IsLinePenPrefix(const char *string, Tcl_Size length, const char *fullName) {
    Tcl_Size fullLength;

    fullLength = (Tcl_Size)strlen(fullName);
    return ((length > 0) && (length <= fullLength) && (strncmp(string, fullName, (size_t)length) == 0));
}

/**
 * @ingroup line_configuration_smoothing
 * @brief Resolve a nullable pen color or the default-color sentinel.
 *
 * NULL/empty input succeeds with NULL only when allowNull is true. A nonempty
 * prefix of defcolor produces COLOR_DEFAULT without acquiring a Tk color.
 * Other strings acquire a Tk color reference for the caller to release through
 * FreeLinePenColor. On error returns TCL_ERROR with a diagnostic and leaves
 * the output pointer unchanged. Does not retain the input Tcl object.
 */
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

/**
 * @ingroup line_configuration_smoothing
 * @brief Consume recorded X/Y source-change metadata.
 *
 * Clears pending/all flags and changed bounds in the element's X and Y slots.
 * Does not deliver or cancel vector notifications, replace values, or clear
 * metadata in other slots. Mapping calls this after handling cache refresh.
 */
static void ClearLineVectorChanges(Line *linePtr) {
    ElemVector *xPtr;
    ElemVector *yPtr;

    xPtr = &linePtr->core.x;
    yPtr = &linePtr->core.y;
    xPtr->changePending = FALSE;
    xPtr->changeAll = FALSE;
    xPtr->changedFirst = 0;
    xPtr->changedLast = 0;
    yPtr->changePending = FALSE;
    yPtr->changeAll = FALSE;
    yPtr->changedFirst = 0;
    yPtr->changedLast = 0;
}

/**
 * @ingroup line_configuration_smoothing
 * @brief Divide finite complex inputs using a denominator component ratio.
 *
 * Chooses the larger denominator component to form the ratio. Returns FALSE
 * for nonfinite inputs, zero denominator or nonfinite computed output.
 * Early rejection leaves outputs unchanged; final finiteness rejection can
 * leave computed nonfinite outputs, so callers must honor the return value.
 * This formulation does not guarantee overflow-free arithmetic for all finite
 * extreme inputs. Borrows scalar inputs and writes only the output doubles.
 */
static int ComplexDivide(double nr, double ni, double dr, double di, double *realPtr, double *imagPtr) {
    double ratio;
    double denominator;

    if ((!FINITE(nr)) || (!FINITE(ni)) || (!FINITE(dr)) || (!FINITE(di))) {
        return FALSE;
    }
    if (fabs(dr) >= fabs(di)) {
        if (dr == 0.0) {
            return FALSE;
        }
        ratio = di / dr;
        denominator = dr + di * ratio;
        if (denominator == 0.0) {
            return FALSE;
        }
        *realPtr = (nr + ni * ratio) / denominator;
        *imagPtr = (ni - nr * ratio) / denominator;
    } else {
        if (di == 0.0) {
            return FALSE;
        }
        ratio = dr / di;
        denominator = di + dr * ratio;
        if (denominator == 0.0) {
            return FALSE;
        }
        *realPtr = (nr * ratio + ni) / denominator;
        *imagPtr = (ni * ratio - nr) / denominator;
    }
    return FINITE(*realPtr) && FINITE(*imagPtr);
}

#define LINE_PI 3.14159265358979323846264338327950288

/**
 * @ingroup line_picking_rendering
 * @brief Insert a named value into a closest-result dictionary.
 *
 * Borrows the writable result dictionary and value; Tcl_DictObjPut retains
 * the key/value on success. Allocates the key object and returns the Tcl
 * status. Does not write interpreter variables or invoke their traces.
 * The caller owns result lifetime and must supply an unshared dictionary.
 */
static int SetClosestInfoObj(Tcl_Interp *interp, Tcl_Obj *resultObjPtr, const char *name, Tcl_Obj *valueObjPtr) {
    return Tcl_DictObjPut(interp, resultObjPtr, Tcl_NewStringObj(name, -1), valueObjPtr);
}

/**
 * @ingroup line_picking_rendering
 * @brief Append a double-valued closest-result field.
 *
 * Constructs a Tcl double and delegates dictionary insertion. Returns the
 * insertion status; the result dictionary owns the inserted object on success.
 * Does not format the number through a string or publish an array variable.
 */
static int SetClosestInfoDouble(Tcl_Interp *interp, Tcl_Obj *resultObjPtr, const char *name, double value) {
    return SetClosestInfoObj(interp, resultObjPtr, name, Tcl_NewDoubleObj(value));
}

/**
 * @ingroup line_picking_rendering
 * @brief Append a string-valued closest-result field.
 *
 * Copies the NUL-terminated value into a Tcl string and returns dictionary
 * insertion status. Borrows the result dictionary and does not publish variables.
 */
static int SetClosestInfoString(Tcl_Interp *interp, Tcl_Obj *resultObjPtr, const char *name, const char *value) {
    return SetClosestInfoObj(interp, resultObjPtr, name, Tcl_NewStringObj(value, -1));
}

/**
 * @ingroup line_picking_rendering
 * @brief Append a real/imaginary pair as a two-element Tcl list.
 *
 * Creates two Tcl doubles and a list in real, imaginary order, then inserts
 * that list into the borrowed result dictionary. Returns the insertion status.
 * No RBC vector is created and no variable traces are run.
 */
static int SetClosestInfoPair(Tcl_Interp *interp, Tcl_Obj *resultObjPtr, const char *name, double real, double imag) {
    Tcl_Obj *objv[2];

    objv[0] = Tcl_NewDoubleObj(real);
    objv[1] = Tcl_NewDoubleObj(imag);
    return SetClosestInfoObj(interp, resultObjPtr, name, Tcl_NewListObj(2, objv));
}

/**
 * @ingroup line_configuration_smoothing
 * @brief Convert reflection coefficient to normalized impedance.
 *
 * Computes (1 + gamma)/(1 - gamma) through ComplexDivide, except exact
 * (+1, 0), which succeeds with (HUGE_VAL, 0) for an open circuit. TRUE therefore
 * does not imply finite output. Failure follows ComplexDivide output rules.
 * No physical reference-impedance scaling is performed here.
 */
static int GammaToNormalizedImpedance(double gammaReal, double gammaImag, double *resistancePtr, double *reactancePtr) {
    /*
     * z = (1 + Gamma) / (1 - Gamma)
     *
     * Gamma = +1 is the open-circuit point.
     */
    if ((gammaReal == 1.0) && (gammaImag == 0.0)) {
        *resistancePtr = HUGE_VAL;
        *reactancePtr = 0.0;
        return TRUE;
    }
    return ComplexDivide(1.0 + gammaReal, gammaImag, 1.0 - gammaReal, -gammaImag, resistancePtr, reactancePtr);
}

/**
 * @ingroup line_configuration_smoothing
 * @brief Convert reflection coefficient to normalized admittance.
 *
 * Computes (1 - gamma)/(1 + gamma) through ComplexDivide, except exact
 * (-1, 0), which succeeds with (HUGE_VAL, 0) for a short circuit. TRUE therefore
 * does not imply finite output. Physical admittance scaling belongs to the
 * caller; failed division outputs must not be consumed.
 */
static int GammaToNormalizedAdmittance(double gammaReal, double gammaImag, double *conductancePtr,
                                       double *susceptancePtr) {
    /*
     * y = (1 - Gamma) / (1 + Gamma)
     *
     * Gamma = -1 is the short-circuit point.
     */
    if ((gammaReal == -1.0) && (gammaImag == 0.0)) {
        *conductancePtr = HUGE_VAL;
        *susceptancePtr = 0.0;
        return TRUE;
    }
    return ComplexDivide(1.0 - gammaReal, -gammaImag, 1.0 + gammaReal, gammaImag, conductancePtr, susceptancePtr);
}

/**
 * @ingroup polar_data
 * @brief Convert one borrowed complex sample to graph-plane coordinates.
 * @param linePtr Borrowed line with validated data format and reference impedance.
 * @param index Source index in the retained complex vector.
 * @param xPtr Receives the real graph coordinate.
 * @param yPtr Receives the imaginary graph coordinate.
 * @return FALSE for an unavailable sample or failed impedance/admittance division.
 * @details Gamma input is copied directly, including nonfinite values; callers
 * must validate coordinates before mapping. Impedance and admittance are
 * normalized using z0, then converted to Gamma. A FALSE return does not promise
 * unchanged output values. No source storage is copied, retained or modified.
 */
static int GetComplexLineDataPoint(Line *linePtr, Tcl_Size index, double *xPtr, double *yPtr) {
    const Rbc_Complex *valuePtr;
    double real;
    double imag;

    if ((index < 0) || (index >= linePtr->z.nValues) || (linePtr->z.valueArr == NULL)) {
        return FALSE;
    }
    valuePtr = linePtr->z.valueArr + index;
    real = valuePtr->real;
    imag = valuePtr->imag;
    switch (linePtr->cDataFormat) {
    case LINE_CDATA_GAMMA:
        *xPtr = real;
        *yPtr = imag;
        return TRUE;
    case LINE_CDATA_IMPEDANCE: {
        double r;
        double x;
        
        /*
         * z = Z / Z0
         * Gamma = (z - 1) / (z + 1)
         */
        r = real / linePtr->z0;
        x = imag / linePtr->z0;
        return ComplexDivide(r - 1.0, x, r + 1.0, x, xPtr, yPtr);
    }
    case LINE_CDATA_ADMITTANCE: {
        double g;
        double b;

        /*
         * y = Y * Z0
         * Gamma = (1 - y) / (1 + y)
         */
        g = real * linePtr->z0;
        b = imag * linePtr->z0;
        return ComplexDivide(1.0 - g, -b, 1.0 + g, b, xPtr, yPtr);
    }
    }
    Tcl_Panic("bad complex data format %d", (int)linePtr->cDataFormat);
    return FALSE;
}

/**
 * @ingroup line_data_transactions
 * @brief Release an owned line-pen color unless it is NULL or COLOR_DEFAULT.
 *
 * The sentinel represents inheritance rather than a Tk color allocation.
 * Does not clear the caller's pointer; callers must not release it twice.
 */
static void FreeLinePenColor(XColor *colorPtr) {
    if ((colorPtr != NULL) && (colorPtr != COLOR_DEFAULT)) {
        Tk_FreeColor(colorPtr);
    }
}


/**
 * @ingroup line_data_transactions
 * @brief Validate a count-to-byte multiplication without allocating storage.
 *
 * Rejects negative counts, zero element size and SIZE_MAX overflow. Assigns
 * bytesPtr only on success. Zero count is valid with a positive element size.
 * Returns TCL_OK or TCL_ERROR without setting an interpreter diagnostic.
 */
static int GetLineArrayByteCount(Tcl_Size count, size_t elementSize, size_t *bytesPtr) {
    if ((count < 0) || (elementSize == 0) || ((uintmax_t)count > (uintmax_t)(SIZE_MAX / elementSize))) {
        return TCL_ERROR;
    }
    *bytesPtr = (size_t)count * elementSize;
    return TCL_OK;
}

/**
 * @ingroup line_data_transactions
 * @brief Return the current source point count for a line representation.
 *
 * Complex mode uses Z length; X/Y mode uses the smaller length. Named sources
 * are queried directly so pending notifications do not hide a resize. Literal
 * slots use cached counts. Does not refresh cached pointers or deliver callbacks;
 * a current count alone does not make cached sample storage current.
 */
static Tcl_Size LinePointCount(Element *elemPtr) {
    Line *linePtr;

    linePtr = LINE_FROM_CORE(elemPtr);
    if (linePtr->dataMode == LINE_DATA_COMPLEX) {
        return linePtr->z.clientId != NULL ? Rbc_VectorLength(linePtr->z.vecPtr) : linePtr->z.nValues;
    }
    return MIN(elemPtr->x.clientId != NULL ? Rbc_VectorLength(elemPtr->x.vecPtr) : elemPtr->x.nValues,
               elemPtr->y.clientId != NULL ? Rbc_VectorLength(elemPtr->y.vecPtr) : elemPtr->y.nValues);
}

/**
 * @ingroup line_data_transactions
 * @brief Read a cached source sample and convert it to graph-plane coordinates.
 *
 * Complex input delegates to GetComplexLineDataPoint. X/Y input checks cached
 * paired length and non-NULL arrays. Polar X/Y requires finite theta/radius and
 * converts radians to Cartesian coordinates; Cartesian X/Y copies directly,
 * without rejecting nonfinite values. Returns a boolean; callers must reject
 * unusable outputs and ensure borrowed arrays are synchronized before reading.
 */
static int GetLineDataPoint(Line *linePtr, Tcl_Size index, double *xPtr, double *yPtr) {
    if (linePtr->dataMode == LINE_DATA_COMPLEX) {
        return GetComplexLineDataPoint(linePtr, index, xPtr, yPtr);
    } else {
        Tcl_Size nPoints;

        nPoints = NumberOfXYPoints(&linePtr->core);
        if ((index < 0) || (index >= nPoints) || (linePtr->core.x.valueArr == NULL) ||
            (linePtr->core.y.valueArr == NULL)) {
            return FALSE;
        }
        if (linePtr->dataCoordinates == LINE_COORDINATES_POLAR) {
            double theta;
            double radius;

            theta = linePtr->core.x.valueArr[index];
            radius = linePtr->core.y.valueArr[index];
            if ((!FINITE(theta)) || (!FINITE(radius))) {
                return FALSE;
            }
            *xPtr = radius * cos(theta);
            *yPtr = radius * sin(theta);
        } else {
            *xPtr = linePtr->core.x.valueArr[index];
            *yPtr = linePtr->core.y.valueArr[index];
        }
        return TRUE;
    }
}

/**
 * @ingroup line_data_transactions
 * @brief Refresh a complex slot's borrowed length, capacity and array pointer.
 *
 * Requires a live vecPtr of complex type. Does not own the array, install a
 * callback, compute extrema, invalidate mapping or schedule redraw.
 */
static void SyncComplexVector(ElemComplexVector *vPtr) {
    vPtr->nValues = Rbc_VectorLength(vPtr->vecPtr);
    vPtr->arraySize = Rbc_VectorSize(vPtr->vecPtr);
    vPtr->valueArr = Rbc_VectorComplexData(vPtr->vecPtr);
}

/**
 * @ingroup line_data_transactions
 * @brief Detach a complex-vector client and zero the whole slot.
 *
 * Accepts an empty slot. Does not destroy the source vector or free its borrowed
 * numeric storage. The previous elemPtr owner association is also cleared.
 */
static void FreeComplexVector(ElemComplexVector *vPtr) {
    if (vPtr->clientId != NULL) {
        Rbc_FreeVectorId(vPtr->clientId);
    }
    memset(vPtr, 0, sizeof(*vPtr));
}

/**
 * @ingroup line_data_transactions
 * @brief Refresh or clear a stable complex-data slot on vector notification.
 *
 * Client data must identify the committed line member. Destruction clears the
 * client ID, vector, array, length and capacity. Update reacquires the vector and
 * refreshes cached storage on success. Always marks axes and element mapping
 * dirty, scheduling backing-store redraw only when not hidden or plotHidden.
 * No partial changed-range accumulation is performed by this callback.
 */
static void ComplexVectorChangedProc(Tcl_Interp *interp, ClientData clientData, Rbc_VectorNotify notify) {
    ElemComplexVector *vPtr;
    Element *elemPtr;
    Graph *graphPtr;

    vPtr = clientData;
    elemPtr = vPtr->elemPtr;
    graphPtr = elemPtr->graphPtr;
    switch (notify) {
    case RBC_VECTOR_NOTIFY_DESTROY:
        vPtr->clientId = NULL;
        vPtr->vecPtr = NULL;
        vPtr->valueArr = NULL;
        vPtr->nValues = 0;
        vPtr->arraySize = 0;
        break;
    case RBC_VECTOR_NOTIFY_UPDATE:
    default:
        if (Rbc_GetVectorById(interp, vPtr->clientId, &vPtr->vecPtr) == TCL_OK) {
            SyncComplexVector(vPtr);
        }
        break;
    }
    graphPtr->flags |= RESET_AXES;
    elemPtr->flags |= MAP_ITEM;
    if ((!elemPtr->hidden) && (!elemPtr->plotHidden)) {
        graphPtr->flags |= REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
}

/**
 * @ingroup polar_data
 * @brief Acquire a staged client of a named complex vector.
 * @param interp Interpreter used for lookup and error reporting.
 * @param elemPtr Borrowed owner recorded in the candidate.
 * @param objPtr Borrowed vector name; NULL or empty means no vector.
 * @param candidatePtr Fresh output slot, initialized here rather than released.
 * @return TCL_OK for an empty slot or a retained complex-vector client;
 * TCL_ERROR after releasing a failed lookup/type-check client.
 * @details The candidate owns its client ID and borrows the vector record and
 * numeric storage. No update callback is installed until commit moves it into
 * the stable line record. Release an abandoned candidate with FreeComplexVector.
 * This parser does not accept a list of complex values as a fallback.
 */
static int ParseComplexVectorObj(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr,
                                 ElemComplexVector *candidatePtr) {
    const char *name;
    Rbc_VectorId clientId;

    memset(candidatePtr, 0, sizeof(*candidatePtr));
    candidatePtr->elemPtr = elemPtr;
    if ((objPtr == NULL) || (Tcl_GetCharLength(objPtr) == 0)) {
        return TCL_OK;
    }
    name = Tcl_GetString(objPtr);
    clientId = Rbc_AllocVectorId(interp, name);
    if (clientId == NULL) {
        return TCL_ERROR;
    }
    if (Rbc_GetVectorById(interp, clientId, &candidatePtr->vecPtr) != TCL_OK) {
        Rbc_FreeVectorId(clientId);
        memset(candidatePtr, 0, sizeof(*candidatePtr));
        return TCL_ERROR;
    }
    if (Rbc_VectorGetType(candidatePtr->vecPtr) != RBC_VECTOR_COMPLEX) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector \"%s\" is real; -cdata requires a complex vector", name));
        Rbc_FreeVectorId(clientId);
        memset(candidatePtr, 0, sizeof(*candidatePtr));
        return TCL_ERROR;
    }
    candidatePtr->clientId = clientId;
    SyncComplexVector(candidatePtr);
    return TCL_OK;
}

/**
 * @ingroup line_data_transactions
 * @brief Replace one retained Tcl option object with balanced references.
 *
 * An identical pointer is a no-op. Retains the replacement before releasing the
 * old object; NULL clears the slot. Does not parse, configure or evaluate it.
 */
static void ReplaceLineOptionObject(Tcl_Obj **objPtrPtr, Tcl_Obj *newObjPtr) {
    Tcl_Obj *oldObjPtr;

    oldObjPtr = *objPtrPtr;
    if (oldObjPtr == newObjPtr) {
        return;
    }
    if (newObjPtr != NULL) {
        Tcl_IncrRefCount(newObjPtr);
    }
    *objPtrPtr = newObjPtr;
    if (oldObjPtr != NULL) {
        Tcl_DecrRefCount(oldObjPtr);
    }
}

/**
 * @ingroup line_data_transactions
 * @brief Replace a staged complex source only after candidate parsing succeeds.
 *
 * Uses ParseComplexVectorObj, then releases an earlier staged client and transfers
 * the new candidate. Failure leaves the earlier candidate untouched. The caller
 * owns transaction cleanup; no callback is registered in temporary storage.
 */
static int StageComplexData(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr,
                            LineComplexDataTransaction *transactionPtr) {
    ElemComplexVector candidate;

    if (ParseComplexVectorObj(interp, elemPtr, objPtr, &candidate) != TCL_OK) {
        return TCL_ERROR;
    }
    if (transactionPtr->staged) {
        FreeComplexVector(&transactionPtr->z);
    }
    transactionPtr->z = candidate;
    transactionPtr->staged = TRUE;
    return TCL_OK;
}

/**
 * @ingroup line_data_transactions
 * @brief Prepare complex-source replacement and data-mode selection in option order.
 *
 * Initializes a fresh transaction from the current mode. First configuration
 * considers the retained default unless explicit -cdata was supplied. Processes
 * canonical explicit options in caller order: nonempty -cdata selects complex,
 * empty -cdata or ordinary data options select X/Y. Later mode selection does not
 * excuse an invalid earlier complex source. Failure frees staged clients and
 * zeros the transaction, leaving live line storage unchanged.
 */
static int PrepareLineComplexDataTransaction(Graph *graphPtr, Element *elemPtr, Line *linePtr,
                                             LineComplexDataTransaction *transactionPtr) {
    int explicitCData;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    transactionPtr->mode = linePtr->dataMode;
    explicitCData = FALSE;
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        const char *name;

        name = Rbc_GetCanonicalOptionName(elemPtr->optionObjv[i], elemPtr->optionSpecs);
        if ((name != NULL) && (strcmp(name, "-cdata") == 0)) {
            explicitCData = TRUE;
        }
    }
    /*
     * As with the ordinary data transaction, option-database values
     * precede explicit caller options.
     */
    if (!elemPtr->optionsConfigured) {
        transactionPtr->mode = LINE_DATA_XY;
        if ((!explicitCData) && (linePtr->cDataObjPtr != NULL)) {
            if (StageComplexData(graphPtr->interp, elemPtr, linePtr->cDataObjPtr, transactionPtr) != TCL_OK) {
                goto error;
            }
            transactionPtr->mode = LINE_DATA_COMPLEX;
        }
    }
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        Tcl_Obj *valueObjPtr;
        const char *name;

        name = Rbc_GetCanonicalOptionName(elemPtr->optionObjv[i], elemPtr->optionSpecs);
        if (name == NULL) {
            continue;
        }
        valueObjPtr = elemPtr->optionObjv[i + 1];
        if (strcmp(name, "-cdata") == 0) {
            if (StageComplexData(graphPtr->interp, elemPtr, valueObjPtr, transactionPtr) != TCL_OK) {
                goto error;
            }
            transactionPtr->mode =
                ((valueObjPtr != NULL) && (Tcl_GetCharLength(valueObjPtr) > 0)) ? LINE_DATA_COMPLEX : LINE_DATA_XY;
        } else if ((strcmp(name, "-data") == 0) || (strcmp(name, "-x") == 0) || (strcmp(name, "-y") == 0)) {
            /*
             * Ordinary X/Y input switches the element back to
             * Cartesian X/Y storage.  Because canonical option names
             * are used, -xdata and -ydata arrive here as -x/-y.
             */
            transactionPtr->mode = LINE_DATA_XY;
        }
    }
    return TCL_OK;

error:
    if (transactionPtr->staged) {
        FreeComplexVector(&transactionPtr->z);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
    return TCL_ERROR;
}

/**
 * @ingroup line_data_transactions
 * @brief Commit a prepared complex-source transaction into stable line storage.
 *
 * In complex mode, transfers a staged slot and then registers its notification
 * callback against linePtr->z; an unstaged complex slot remains unchanged.
 * In X/Y mode, frees live and staged complex clients and clears retained -cdata.
 * Stores the selected mode and clears staged ownership. Does not free external
 * vectors or itself schedule graph redraw; preparation must already have passed.
 */
static void CommitLineComplexDataTransaction(Line *linePtr, LineComplexDataTransaction *transactionPtr) {
    if (transactionPtr->mode == LINE_DATA_COMPLEX) {
        if (transactionPtr->staged) {
            FreeComplexVector(&linePtr->z);
            linePtr->z = transactionPtr->z;
            memset(&transactionPtr->z, 0, sizeof(transactionPtr->z));
            if (linePtr->z.clientId != NULL) {
                Rbc_SetVectorChangedProc(linePtr->z.clientId, ComplexVectorChangedProc, &linePtr->z);
            }
        }
    } else {
        FreeComplexVector(&linePtr->z);
        if (transactionPtr->staged) {
            FreeComplexVector(&transactionPtr->z);
        }
        /*
         * An explicit -data/-x/-y switches away from -cdata.
         */
        ReplaceLineOptionObject(&linePtr->cDataObjPtr, NULL);
    }
    linePtr->dataMode = transactionPtr->mode;
    transactionPtr->staged = FALSE;
}

/**
 * @ingroup line_data_transactions
 * @brief Release an abandoned staged complex source and zero its transaction.
 *
 * Safe after commit because staged ownership has been cleared. Does not release
 * the live line source or change data mode.
 */
static void FreeLineComplexDataTransaction(LineComplexDataTransaction *transactionPtr) {
    if (transactionPtr->staged) {
        FreeComplexVector(&transactionPtr->z);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @ingroup line_data_transactions
 * @brief Refresh a live real parameter slot's borrowed storage and extrema.
 *
 * Requires a valid vecPtr. Copies length, capacity, array and vector range without
 * geometry invalidation, notification delivery or ownership transfer.
 */
static void SyncLineParamVector(ElemVector *vPtr) {
    vPtr->nValues = Rbc_VectorLength(vPtr->vecPtr);
    vPtr->arraySize = Rbc_VectorSize(vPtr->vecPtr);
    vPtr->valueArr = Rbc_VectorData(vPtr->vecPtr);
    Rbc_VectorGetRange(vPtr->vecPtr, &vPtr->min, &vPtr->max);
}

/**
 * @ingroup line_data_transactions
 * @brief Refresh parallel parameter data without invalidating plotted geometry.
 *
 * Destruction clears client identity and borrowed storage. Update reacquires the
 * source and synchronizes it; failed lookup clears vector/array/count/capacity
 * while retaining the client ID for later cleanup. Cached extrema are not reset
 * on those clear paths. Does not reset axes, map geometry or request redraw.
 */
static void LineParamVectorChangedProc(Tcl_Interp *interp, ClientData clientData, Rbc_VectorNotify notify) {
    ElemVector *vPtr;

    vPtr = clientData;
    switch (notify) {
    case RBC_VECTOR_NOTIFY_DESTROY:
        vPtr->clientId = NULL;
        vPtr->vecPtr = NULL;
        vPtr->valueArr = NULL;
        vPtr->nValues = 0;
        vPtr->arraySize = 0;
        break;
    case RBC_VECTOR_NOTIFY_UPDATE:
    default:
        if (Rbc_GetVectorById(interp, vPtr->clientId, &vPtr->vecPtr) == TCL_OK) {
            SyncLineParamVector(vPtr);
        } else {
            /*
             * Never leave a potentially stale valueArr accessible if
             * the vector can no longer be resolved.
             */
            vPtr->vecPtr = NULL;
            vPtr->valueArr = NULL;
            vPtr->nValues = 0;
            vPtr->arraySize = 0;
        }
        break;
    }
}

/**
 * @ingroup line_data_transactions
 * @brief Parse and retain a replacement parameter candidate transactionally.
 *
 * Accepts the common real-vector or numeric-expression-list input. On failure
 * releases the local candidate and retains any earlier staged value. On success
 * frees the earlier staged slot and transfers the candidate. Does not install
 * parameter notifications until commit moves it to stable line storage.
 */
static int StageLineParam(Tcl_Interp *interp, Element *elemPtr, Tcl_Obj *objPtr, LineParamTransaction *transactionPtr) {
    ElemVector candidate;

    memset(&candidate, 0, sizeof(candidate));
    if (Rbc_ParseElemVectorObj(interp, elemPtr, objPtr, &candidate) != TCL_OK) {
        Rbc_FreeElemVector(&candidate);
        return TCL_ERROR;
    }
    if (transactionPtr->staged) {
        Rbc_FreeElemVector(&transactionPtr->param);
    }
    transactionPtr->param = candidate;
    transactionPtr->staged = TRUE;
    return TCL_OK;
}

/**
 * @ingroup line_data_transactions
 * @brief Prepare parameter input from defaults and ordered explicit options.
 *
 * Initializes a fresh transaction. On first configuration uses the retained
 * default only when no explicit canonical -param occurs. Validates every explicit
 * occurrence in order, including values superseded later. Failure frees staged
 * storage and zeros the transaction. Length compatibility is a separate check.
 */
static int PrepareLineParamTransaction(Graph *graphPtr, Element *elemPtr, Line *linePtr,
                                       LineParamTransaction *transactionPtr) {
    int explicitParam;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitParam = FALSE;
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        const char *name;

        name = Rbc_GetCanonicalOptionName(elemPtr->optionObjv[i], elemPtr->optionSpecs);
        if ((name != NULL) && (strcmp(name, "-param") == 0)) {
            explicitParam = TRUE;
        }
    }
    /*
     * Process an option-database value on the first configuration
     * unless the caller explicitly supplied -param.
     */
    if (!elemPtr->optionsConfigured && !explicitParam && (linePtr->paramObjPtr != NULL)) {
        if (StageLineParam(graphPtr->interp, elemPtr, linePtr->paramObjPtr, transactionPtr) != TCL_OK) {
            goto error;
        }
    }
    /*
     * Process explicit occurrences in caller order.  Thus an invalid
     * earlier repeated -param still makes the whole configure fail.
     */
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        const char *name;

        name = Rbc_GetCanonicalOptionName(elemPtr->optionObjv[i], elemPtr->optionSpecs);
        if ((name != NULL) && (strcmp(name, "-param") == 0)) {
            if (StageLineParam(graphPtr->interp, elemPtr, elemPtr->optionObjv[i + 1], transactionPtr) != TCL_OK) {
                goto error;
            }
        }
    }
    return TCL_OK;

error:
    if (transactionPtr->staged) {
        Rbc_FreeElemVector(&transactionPtr->param);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
    return TCL_ERROR;
}

/**
 * @ingroup line_data_transactions
 * @brief Transfer staged parameter ownership to the line and install its callback.
 *
 * No-op without a staged candidate. Releases the old live slot, moves the new
 * one, restores its owner pointer and registers LineParamVectorChangedProc for
 * a named source. Clears candidate ownership for subsequent transaction cleanup.
 */
static void CommitLineParamTransaction(Line *linePtr, LineParamTransaction *transactionPtr) {
    if (!transactionPtr->staged) {
        return;
    }
    Rbc_FreeElemVector(&linePtr->param);
    linePtr->param = transactionPtr->param;
    memset(&transactionPtr->param, 0, sizeof(transactionPtr->param));
    linePtr->param.elemPtr = &linePtr->core;
    if (linePtr->param.clientId != NULL) {
        Rbc_SetVectorChangedProc(linePtr->param.clientId, LineParamVectorChangedProc, &linePtr->param);
    }
    transactionPtr->staged = FALSE;
}

/**
 * @ingroup line_data_transactions
 * @brief Release a staged parameter candidate and zero its transaction.
 *
 * Safe on an empty or committed transaction. Uses common slot cleanup for owned
 * list arrays and retained vector clients, without destroying source vectors.
 */
static void FreeLineParamTransaction(LineParamTransaction *transactionPtr) {
    if (transactionPtr->staged) {
        Rbc_FreeElemVector(&transactionPtr->param);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @ingroup line_data_transactions
 * @brief Test whether the retained parameter option is nonempty.
 *
 * Examines paramObjPtr text only, not current numeric length or client validity.
 * A nonempty option whose external source became empty is still configured.
 */
static int LineParamConfigured(Line *linePtr) {
    return ((linePtr->paramObjPtr != NULL) && (Tcl_GetCharLength(linePtr->paramObjPtr) > 0));
}

/**
 * @ingroup line_data_transactions
 * @brief Decide whether configuration must validate parallel parameter length.
 *
 * Returns true on initial configuration or an explicit canonical -param, -data,
 * -x, -y or -cdata option. Other changes may proceed during a temporary mismatch
 * caused by independently resized external vectors. Does not compare lengths.
 */
static int LineParamLengthValidationNeeded(Element *elemPtr) {
    Tcl_Size i;

    if (!elemPtr->optionsConfigured) {
        return TRUE;
    }
    for (i = 0; i < elemPtr->optionObjc; i += 2) {
        const char *name;

        name = Rbc_GetCanonicalOptionName(elemPtr->optionObjv[i], elemPtr->optionSpecs);
        if (name == NULL) {
            continue;
        }
        if ((strcmp(name, "-param") == 0) || (strcmp(name, "-data") == 0) || (strcmp(name, "-x") == 0) ||
            (strcmp(name, "-y") == 0) || (strcmp(name, "-cdata") == 0)) {
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * @ingroup line_data_transactions
 * @brief Count prospective data points using current named-vector lengths.
 *
 * Uses the staged mode when a complex transaction is supplied. Complex mode
 * selects staged or live Z storage; X/Y delegates to the common transaction
 * count. Named sources are queried directly, while literals use their stored
 * counts. Does not refresh arrays, deliver notifications or commit candidates.
 */
static Tcl_Size ProspectiveLinePointCount(Line *linePtr, const ElemDataTransaction *dataTransactionPtr,
                                          const LineComplexDataTransaction *complexDataTransactionPtr) {
    LineDataMode mode;

    mode = linePtr->dataMode;
    if (complexDataTransactionPtr != NULL) {
        mode = complexDataTransactionPtr->mode;
    }
    if (mode == LINE_DATA_COMPLEX) {
        const ElemComplexVector *zPtr = &linePtr->z;

        if ((complexDataTransactionPtr != NULL) && complexDataTransactionPtr->staged) {
            zPtr = &complexDataTransactionPtr->z;
        }
        return zPtr->clientId != NULL ? Rbc_VectorLength(zPtr->vecPtr) : zPtr->nValues;
    }
    return Rbc_ElemDataTransactionPointCount(&linePtr->core, dataTransactionPtr);
}

/**
 * @ingroup line_data_transactions
 * @brief Require configured parameter length to match prospective data length.
 *
 * Empty retained -param disables the check. Selects staged parameter storage
 * when available, otherwise the live slot, querying current named-vector length.
 * Obtains data length through ProspectiveLinePointCount. Returns TCL_ERROR with
 * both counts on mismatch, TCL_OK otherwise. Does not refresh arrays, deliver
 * notifications, commit candidates or free them.
 */
static int ValidateLineParamLength(Graph *graphPtr, Line *linePtr, const ElemDataTransaction *dataTransactionPtr,
                                   const LineComplexDataTransaction *complexDataTransactionPtr,
                                   const LineParamTransaction *paramTransactionPtr) {
    const ElemVector *paramPtr = &linePtr->param;
    Tcl_Size nParam;
    Tcl_Size nPoints;

    /*
     * Empty -param disables the parallel parameter mapping.
     */
    if (!LineParamConfigured(linePtr)) {
        return TCL_OK;
    }
    if ((paramTransactionPtr != NULL) && paramTransactionPtr->staged) {
        paramPtr = &paramTransactionPtr->param;
    }
    nParam = paramPtr->clientId != NULL ? Rbc_VectorLength(paramPtr->vecPtr) : paramPtr->nValues;
    nPoints = ProspectiveLinePointCount(linePtr, dataTransactionPtr, complexDataTransactionPtr);
    if (nParam != nPoints) {
        Tcl_SetObjResult(graphPtr->interp,
                         Tcl_ObjPrintf("parameter vector has %" TCL_SIZE_MODIFIER
                                       "d values, but element has %" TCL_SIZE_MODIFIER "d data points",
                                       nParam, nPoints));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @ingroup line_option_transactions
 * @brief Identify a manually parsed scalar option by its canonical Tk name.
 *
 * Recognizes maxsymbols, smooth and trace; returns NONE for other options or
 * unresolved names. The enclosing Tk option pass has already validated names
 * and abbreviations. Does not parse the option value or change live fields.
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

/**
 * @ingroup line_option_transactions
 * @brief Identify pattern or tile options for an area or error-band transaction.
 *
 * band selects errorbandpattern/errorbandtile instead of areapattern/areatile.
 * Resolves canonical names through the element option table; returns NONE for
 * other options. Tk has already checked ambiguous or unknown option names.
 */
static LineAreaOption GetLineAreaOption(Element *elemPtr, Tcl_Obj *objPtr, int band) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, elemPtr->optionSpecs);
    if (name == NULL) {
        return LINE_AREA_OPTION_NONE;
    }
    if (strcmp(name, band ? "-errorbandpattern" : "-areapattern") == 0) {
        return LINE_AREA_OPTION_PATTERN;
    }
    if (strcmp(name, band ? "-errorbandtile" : "-areatile") == 0) {
        return LINE_AREA_OPTION_TILE;
    }
    return LINE_AREA_OPTION_NONE;
}

/**
 * @ingroup line_configuration_smoothing
 * @brief Round a bounded double to an integer with halfway cases away from zero.
 *
 * Adds or subtracts one half and casts to int. Caller must provide a finite
 * value whose adjusted result is representable; this helper neither clamps
 * nor reports overflow. Used for native-compatible screen geometry.
 */
INLINE static int Round(register double x) { return (int)(x + ((x < 0.0) ? -0.5 : 0.5)); }

/*
 * ----------------------------------------------------------------------
 *     Custom configuration option (parse and print) routines
 * ----------------------------------------------------------------------
 */

/**
 * @ingroup line_option_transactions
 * @brief Acquire a fill pattern without modifying live element resources.
 *
 * NULL or empty text yields None; exact solid yields PATTERN_SOLID. Other text
 * is a Tk bitmap name and acquires a bitmap reference. Assigns stipplePtr only
 * on success. Returns TCL_ERROR with the bitmap diagnostic on lookup failure.
 * Only a real bitmap result requires Tk_FreeBitmap; sentinels do not.
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

/**
 * @ingroup line_option_transactions
 * @brief Parse a borrowed Tcl pattern object through GetPatternFromString.
 *
 * NULL is treated as empty. Does not retain the object; successful bitmap
 * ownership belongs to the caller and must be released or transferred.
 */
static int GetPatternFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, Pixmap *stipplePtr) {
    const char *string;

    string = (objPtr == NULL) ? NULL : Tcl_GetString(objPtr);
    return GetPatternFromString(interp, tkwin, string, stipplePtr);
}

/**
 * @ingroup line_option_transactions
 * @brief Release an owned bitmap pattern, excluding None and PATTERN_SOLID.
 *
 * Does not modify the caller's field. Requires the bitmap's display context;
 * callers must clear or replace ownership to avoid a second release.
 */
static void FreeLinePattern(Display *display, Pixmap stipple) {
    if ((stipple != None) && (stipple != PATTERN_SOLID)) {
        Tk_FreeBitmap(display, stipple);
    }
}

/**
 * @ingroup line_option_transactions
 * @brief Acquire a replacement stipple before releasing an earlier candidate.
 *
 * Failure retains prior staged state. Success stores the pattern or sentinel
 * and marks the pattern bit. The transaction owns real bitmap references until
 * commit or FreeLineAreaTransaction; live line resources are unchanged.
 */
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

/**
 * @ingroup line_option_transactions
 * @brief Acquire a replacement tile client in temporary transaction storage.
 *
 * NULL or empty input stages a cleared tile. Nonempty input resolves an RBC
 * tile before releasing any previous staged client. Failure leaves that earlier
 * candidate intact. Does not install the line notification callback yet.
 */
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

/**
 * @ingroup line_option_transactions
 * @brief Acquire a Tk GC for effective fill colors and a pattern.
 *
 * Colors and stipple are borrowed. Non-NULL colors supply foreground/background
 * pixels; a real bitmap selects FillStippled without a background and
 * FillOpaqueStippled with one. None and PATTERN_SOLID do not install stippling.
 * Returns a Tk-managed GC reference for later Tk_FreeGC; this helper does not
 * decide whether geometry should be filled or implement opacity or tile drawing.
 */
static GC CreateLineFillGC(Graph *graphPtr, XColor *foreground, XColor *background, Pixmap stipple) {
    unsigned long gcMask;
    XGCValues gcValues;

    gcMask = 0;
    memset(&gcValues, 0, sizeof(gcValues));
    if (foreground != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = foreground->pixel;
    }
    if (background != NULL) {
        gcMask |= GCBackground;
        gcValues.background = background->pixel;
    }
    if ((stipple != None) && (stipple != PATTERN_SOLID)) {
        gcMask |= GCStipple | GCFillStyle;
        gcValues.stipple = stipple;
        gcValues.fill_style = (background == NULL) ? FillStippled : FillOpaqueStippled;
    }
    return Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
}

/**
 * @ingroup line_option_transactions
 * @brief Release all remaining owned area candidates and zero the transaction.
 *
 * Releases a staged real pattern, any tile client and any candidate GC. Does
 * not free the live line resources or Tk-managed color options. Safe after
 * commit because moved ownership is cleared. Requires the owning display.
 */
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

/**
 * @ingroup line_option_transactions
 * @brief Prepare pattern, tile and replacement GC for area or band configuration.
 *
 * Initializes fresh transaction storage. Initial defaults are parsed only for
 * options not explicitly supplied. Every explicit occurrence is processed in
 * caller order; an invalid earlier value cannot be hidden by a later one.
 * Builds a GC using the staged or retained stipple and colors already installed
 * by Tk_SetOptions. Failure frees staged resources and leaves live derived
 * resources unchanged; the caller remains responsible for Tk option rollback.
 */
static int PrepareLineAreaTransaction(Graph *graphPtr, Element *elemPtr, Line *linePtr,
                                      LineAreaTransaction *transactionPtr, int band) {
    Tcl_Obj *patternObjPtr = band ? linePtr->errorBand.patternObjPtr : linePtr->areaPatternObjPtr;
    Tcl_Obj *tileObjPtr = band ? linePtr->errorBand.tileObjPtr : linePtr->areaTileObjPtr;
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

        option = GetLineAreaOption(elemPtr, elemPtr->optionObjv[i], band);
        if (option != LINE_AREA_OPTION_NONE) {
            explicitMask |= LINE_AREA_OPTION_MASK(option);
        }
    }
    /*
     * During initial modern configuration, process effective defaults
     * and option-database values not explicitly overridden.
     */
    if (!elemPtr->optionsConfigured) {
        if (!(explicitMask & LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_PATTERN)) && (patternObjPtr != NULL)) {
            if (StageLineAreaPattern(graphPtr, patternObjPtr, transactionPtr) != TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_TILE)) && (tileObjPtr != NULL)) {
            if (StageLineAreaTile(graphPtr, tileObjPtr, transactionPtr) != TCL_OK) {
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

        option = GetLineAreaOption(elemPtr, elemPtr->optionObjv[i], band);
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
        effectiveStipple = band ? linePtr->errorBand.stipple : linePtr->fillStipple;
    }
    transactionPtr->gc = CreateLineFillGC(graphPtr,
        band ? linePtr->errorBand.foreground : linePtr->fillFgColor,
        band ? linePtr->errorBand.background : linePtr->fillBgColor, effectiveStipple);
    return TCL_OK;

error:
    FreeLineAreaTransaction(graphPtr, transactionPtr);
    return TCL_ERROR;
}

/**
 * @ingroup line_option_transactions
 * @brief Transfer prepared area or band resources to their live line fields.
 *
 * Replaces only staged pattern/tile fields and transfers the candidate GC when
 * present, releasing displaced resources. Installs TileChangedProc on a new tile
 * or ensures it is installed on the retained tile. Clears moved candidates and
 * stagedMask for cleanup. No parsing, geometry mapping or redraw is performed.
 */
static void CommitLineAreaTransaction(Graph *graphPtr, Line *linePtr, LineAreaTransaction *transactionPtr, int band) {
    Pixmap *stipplePtr = band ? &linePtr->errorBand.stipple : &linePtr->fillStipple;
    Rbc_Tile *tilePtr = band ? &linePtr->errorBand.tile : &linePtr->fillTile;
    GC *gcPtr = band ? &linePtr->errorBand.gc : &linePtr->fillGC;

    if (transactionPtr->stagedMask & LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_PATTERN)) {
        Pixmap oldStipple;

        oldStipple = *stipplePtr;
        *stipplePtr = transactionPtr->stipple;
        transactionPtr->stipple = None;
        FreeLinePattern(graphPtr->display, oldStipple);
    }
    if (transactionPtr->stagedMask & LINE_AREA_OPTION_MASK(LINE_AREA_OPTION_TILE)) {
        Rbc_Tile oldTile;

        oldTile = *tilePtr;
        *tilePtr = transactionPtr->tile;
        transactionPtr->tile = NULL;
        if (*tilePtr != NULL) {
            Rbc_SetTileChangedProc(*tilePtr, TileChangedProc, linePtr);
        }
        if (oldTile != NULL) {
            Rbc_FreeTile(oldTile);
        }
    } else if (*tilePtr != NULL) {
        /*
         * Ensure that an existing tile also has its callback after the
         * initial modern configuration.
         */
        Rbc_SetTileChangedProc(*tilePtr, TileChangedProc, linePtr);
    }
    if (transactionPtr->gc != NULL) {
        GC oldGC;

        oldGC = *gcPtr;
        *gcPtr = transactionPtr->gc;
        transactionPtr->gc = NULL;
        if (oldGC != NULL) {
            Tk_FreeGC(graphPtr->display, oldGC);
        }
    }
    transactionPtr->stagedMask = 0;
}

/**
 * @ingroup line_option_transactions
 * @brief Release bitmap and mask references owned by a parsed symbol.
 *
 * Clears each released handle to None; repeated cleanup is safe. Leaves the
 * symbol type untouched. The caller supplies the bitmap display context.
 */
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

/**
 * @ingroup line_option_transactions
 * @brief Parse a geometric symbol name or acquire a bitmap and optional mask.
 *
 * Initializes fresh output storage to SYMBOL_NONE with empty handles; do not
 * pass an already owning result. Empty input disables symbols. Built-in names
 * use legacy prefix rules, with two-character minima for c/s names.
 * Otherwise accepts a one- or two-item bitmap list. A missing mask releases the
 * already acquired bitmap before returning TCL_ERROR. Success transfers owned
 * bitmap/mask references to the caller; mask dimensions are not validated here.
 */
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

/**
 * @ingroup line_option_transactions
 * @brief Parse an exact entry in the smoothing table without modifying the line.
 *
 * Accepts linear, step, natural, cubic, quadratic and catrom; cubic aliases
 * natural. No prefix matching is performed. Writes valuePtr only on success.
 * Failure returns TCL_ERROR with a diagnostic listing all accepted names.
 */
static int GetSmoothFromString(Tcl_Interp *interp, const char *string, Smoothing *valuePtr) {
    SmoothingInfo *siPtr;

    for (siPtr = smoothingInfo; siPtr->name != NULL; siPtr++) {
        if (strcmp(string, siPtr->name) == 0) {
            *valuePtr = siPtr->value;
            return TCL_OK;
        }
    }
    Rbc_AppendResultStrings(interp, "bad smooth value \"", string, "\": should be linear, step, natural, cubic, quadratic, or catrom",
                     (char *)NULL);
    return TCL_ERROR;
}

/**
 * @ingroup line_option_transactions
 * @brief Parse the string of a borrowed Tcl object as a smoothing value.
 *
 * Delegates to GetSmoothFromString without retaining or evaluating the object.
 * Leaves the output unchanged on parse failure.
 */
static int GetSmoothFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Smoothing *valuePtr) {
    return GetSmoothFromString(interp, Tcl_GetString(objPtr), valuePtr);
}

/**
 * @ingroup line_option_transactions
 * @brief Parse a nonempty prefix of increasing, decreasing or both.
 *
 * The leading i/d/b selects the candidate; longer-than-name strings are rejected.
 * Writes penDirPtr only on success, otherwise returns TCL_ERROR with a diagnostic.
 * Does not alter traces or mapped geometry.
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

/**
 * @ingroup line_option_transactions
 * @brief Parse a borrowed Tcl object's string as a trace-direction prefix.
 *
 * Delegates to GetPenDirFromString without retaining the object or evaluating
 * an expression. Returns the delegated Tcl status.
 */
static int GetPenDirFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *penDirPtr) {
    return GetPenDirFromString(interp, Tcl_GetString(objPtr), penDirPtr);
}

/**
 * @ingroup line_option_transactions
 * @brief Convert one recognized scalar into candidate storage and mark its bit.
 *
 * maxsymbols uses the existing nonnegative pixel parser; smooth and trace use
 * their dedicated name parsers. Conversion failure leaves that candidate field
 * and mask unchanged. An invalid option enum is an internal programming error
 * and calls Tcl_Panic. Does not modify the live line or schedule redraw.
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

/**
 * @ingroup line_option_transactions
 * @brief Parse ordered scalar defaults and explicit values into a fresh transaction.
 *
 * Processes initial defaults only when not explicitly overridden, then each
 * explicit occurrence in caller order. Strip elements have no trace option and
 * their traceObjPtr remains NULL. Failure may leave earlier staged scalar values
 * in the transaction, but owns no resources to release and changes no live field.
 * The caller must not commit a failed transaction.
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

/**
 * @ingroup line_option_transactions
 * @brief Copy successfully staged scalar values into live line configuration.
 *
 * Copies maxsymbols to reqMaxSymbols, smoothing to reqSmooth, and trace direction
 * to penDir only when their bits are present. Clears stagedMask afterward.
 * Values are already validated; mapping, redraw and effective smoothing selection
 * belong to later stages, not this commit helper.
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

/**
 * @ingroup line_geometry_lifecycle
 * @brief Clear palette counts without releasing storage.
 *
 * Zeros each style's symbol, strip and X/Y error-bar counts. Array pointers
 * and pen references remain unchanged; the owning line releases shared geometry.
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

/**
 * @ingroup line_geometry_lifecycle
 * @brief Stage and replace a line pen's derived drawing resources.
 *
 * Borrows the graph and pen. Validates option representations and stages
 * colors, shadow and symbol resources before constructing replacement GCs.
 * On conversion failure, frees staged resources and returns TCL_ERROR with
 * an interpreter diagnostic, leaving existing derived resources intact.
 * On success, releases old derived resources and transfers the candidates.
 * The enclosing configuration transaction owns Tk option rollback.
 *
 * Uses Tk-managed symbol, text and error-bar GCs but a private trace GC,
 * including its dash state. Bitmap symbols retain their mask/background
 * semantics. Does not map geometry or schedule drawing.
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
    Point2D newValueOffset;
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
    if (Rbc_GetValueOffset(graphPtr->interp, lpPtr->valueOffsetObjPtr, &newValueOffset) != TCL_OK) {
        goto error;
    }
    if (Rbc_ValidateValueCommand(graphPtr->interp, lpPtr->valueCommandObjPtr) != TCL_OK) {
        goto error;
    }
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
    lpPtr->valueOffset = newValueOffset;
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

/**
 * @ingroup line_geometry_lifecycle
 * @brief Release derived line-pen resources.
 *
 * Frees text-style resources, Tk-managed GCs, the private trace GC, bitmap
 * and mask, and separately retained colors. Clears released fields.
 * Does not free the pen record or its Tk option values; outer pen teardown
 * owns those. Requires the graph display to remain valid.
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

/**
 * @ingroup line_geometry_lifecycle
 * @brief Initialize a zero-allocated line pen before option setup.
 *
 * Installs configuration/destruction callbacks, option specifications and
 * baseline symbol, error-bar and value-label settings. Does not zero the
 * whole record, initialize Tk options, or allocate GCs. The initial empty
 * name is borrowed; the factory replaces it with owned storage.
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

/**
 * @ingroup line_geometry_lifecycle
 * @brief Allocate an unregistered line pen.
 *
 * The exact name activeLine selects active specifications; all other names
 * select normal specifications. Duplicates the name and returns the embedded
 * Pen pointer. Allocation is asserted. Registry insertion, Tk option
 * initialization and configuration belong to the caller.
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

/**
 * @ingroup line_geometry_lifecycle
 * @brief Compute a bounded odd screen size for symbols.
 *
 * When symbol scaling is enabled, SCALE_SYMBOL captures current axis ranges
 * as the baseline. Later calls use the smaller finite positive range ratio;
 * invalid ranges or ratios retain unit scaling. May update the baseline and
 * clear SCALE_SYMBOL. Clamps before integer conversion to at least one and
 * at most the smaller graph screen range, then chooses an odd size without
 * exceeding that cap. Does not allocate or invoke Tcl.
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

/**
 * @ingroup line_geometry_lifecycle
 * @brief Build owned mapped points with original source indices.
 *
 * Initializes the output map to empty; callers must first release any old
 * map storage. Reads finite source points and retains only finite mapped
 * coordinates. Does not clip them to the viewport or synchronize sources.
 * Allocates a break-before array only when retained indices contain gaps.
 *
 * Transfers the point, index and optional break arrays to the map on success.
 * Checked-size or allocation failure frees candidates and leaves an empty
 * map without an interpreter diagnostic.
 */
static void GetScreenPoints(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
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
    /*
     * This is only an allocation upper bound.  Some finite raw data
     * may still be outside the domain of a logarithmic axis.
     */
    nScreenPoints = 0;
    for (i = 0; i < nDataPoints; i++) {
        double x;
        double y;

        if (!GetLineDataPoint(linePtr, i, &x, &y)) {
            continue;
        }
        if (FINITE(x) && FINITE(y)) {
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
        double x;
        double y;

        if (!GetLineDataPoint(linePtr, i, &x, &y)) {
            continue;
        }
        if ((!FINITE(x)) || (!FINITE(y))) {
            continue;
        }
        point = Rbc_Map2D(graphPtr, x, y, &linePtr->core.axes);
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

/**
 * @ingroup line_configuration_smoothing
 * @brief Select the physical screen coordinate along mapped data X.
 *
 * Returns screen X normally and screen Y when inverted. Does not undo axis
 * descending direction or perform an inverse transform. Borrows both inputs.
 */
INLINE static double PixelAbscissa(Graph *graphPtr, const Point2D *pointPtr) {
    return graphPtr->inverted ? pointPtr->y : pointPtr->x;
}

/**
 * @ingroup line_configuration_smoothing
 * @brief Select the screen coordinate perpendicular to mapped data X.
 *
 * Returns screen Y normally and screen X when inverted. Pixel buckets use
 * its extrema to preserve the visible envelope. Does not change sign or
 * recover data coordinates.
 */
INLINE static double PixelOrdinate(Graph *graphPtr, const Point2D *pointPtr) {
    return graphPtr->inverted ? pointPtr->x : pointPtr->y;
}

/** @ingroup line_decimation
 * Test semantic eligibility, independently of source density.
 * @param linePtr Borrowed live line/strip element.
 * @return TRUE for automatic, ordinary XY, unsmoothed, single-pen traces without
 * area fill, outside export. Does not guarantee that reduction will be attempted.
 */
static int CanPixelDecimateLine(Line *linePtr) {
    /*
     * Screen-density decimation is a display optimization.  Export
     * output remaps the graph for its own layout and must retain the
     * full line geometry.
     */
    if (linePtr->core.graphPtr->flags & GRAPH_EXPORT) {
        return FALSE;
    }
    if (linePtr->decimate != LINE_DECIMATE_AUTO) {
        return FALSE;
    }
    /*
     * Support ordinary XY line and strip elements.  Polar elements retain
     * the full mapping path.
     */
    if ((linePtr->core.classUid != rbcLineElementUid) && (linePtr->core.classUid != rbcStripElementUid)) {
        return FALSE;
    }
    if (linePtr->dataMode != LINE_DATA_XY) {
        return FALSE;
    }
    /*
     * "linear" is RBC's ordinary unsmoothed polyline mode.
     */
    if (linePtr->reqSmooth != PEN_SMOOTH_NONE) {
        return FALSE;
    }
    /*
     * Weighted/multiple pen styles can introduce a style transition at
     * any source point.  Do not discard such points yet.
     */
    if (Rbc_ChainGetLength(linePtr->core.palette) >= 2) {
        return FALSE;
    }
    /*
     * Area filling depends on the precise mapped polygon.  Leave it on
     * the original path until fill semantics have been considered
     * separately.
     */
    if ((linePtr->fillTile != NULL) || (linePtr->fillStipple != None)) {
        return FALSE;
    }
    return TRUE;
}

/** @ingroup line_decimation
 * Test source density against the physical data-X plot span.
 * @param graphPtr Borrowed graph with current mapping/extents, including track scope.
 * @param linePtr Borrowed element supplying the complete paired source count.
 * @return TRUE at the renderer's density threshold, FALSE for empty data or
 * invalid/subpixel spans. Uses vertical extent when axes are inverted.
 */
static int LineNeedsPixelDecimation(Graph *graphPtr, Line *linePtr) {
    Extents2D exts;
    Tcl_Size nPoints;
    double span;
    double density;

    nPoints = NumberOfPoints(&linePtr->core);
    if (nPoints < 1) {
        return FALSE;
    }
    Rbc_GraphExtents(graphPtr, &exts);
    /*
     * Data X maps horizontally normally and vertically with
     * -invertxy.
     */
    if (graphPtr->inverted) {
        span = fabs(exts.bottom - exts.top);
    } else {
        span = fabs(exts.right - exts.left);
    }
    if ((!FINITE(span)) || (span < 1.0)) {
        return FALSE;
    }
    density = (double)nPoints / span;
    return density >= ((graphPtr->renderer == RBC_RENDERER_CAIRO) ?
        LINE_DECIMATE_CAIRO_POINTS_PER_PIXEL : LINE_DECIMATE_NATIVE_POINTS_PER_PIXEL);
}

static void InvalidateLineDecimateCache(Line *linePtr) {
    LineDecimateCache *cachePtr;

    cachePtr = &linePtr->decimateCache;
    if (cachePtr->blocks != NULL) {
        ckfree(cachePtr->blocks);
    }
    memset(cachePtr, 0, sizeof(*cachePtr));
}

static int BuildLineDecimateCache(Line *linePtr) {
    LineDecimateCache *cachePtr;
    Element *elemPtr;
    const double *xValues;
    const double *yValues;
    Tcl_Size nPoints;
    Tcl_Size nBlocks;
    Tcl_Size blockIndex;
    Tcl_Size previousIndex;
    int direction;
    size_t blockBytes;

    cachePtr = &linePtr->decimateCache;
    elemPtr = &linePtr->core;
    InvalidateLineDecimateCache(linePtr);
    nPoints = NumberOfPoints(elemPtr);
    xValues = elemPtr->x.valueArr;
    yValues = elemPtr->y.valueArr;
    cachePtr->built = TRUE;
    cachePtr->nPoints = nPoints;
    cachePtr->xValues = xValues;
    cachePtr->yValues = yValues;
    cachePtr->allPositiveX = TRUE;
    cachePtr->allPositiveY = TRUE;
    cachePtr->nonPositiveYCount = 0;    
    if ((nPoints < 1) || (xValues == NULL) || (yValues == NULL)) {
        return FALSE;
    }
    nBlocks = (nPoints / LINE_DECIMATE_BLOCK_SIZE) + ((nPoints % LINE_DECIMATE_BLOCK_SIZE) != 0);
    if (GetLineArrayByteCount(nBlocks, sizeof(*cachePtr->blocks), &blockBytes) != TCL_OK) {
        return FALSE;
    }
    cachePtr->blocks = Tcl_AttemptAlloc(blockBytes);
    if (cachePtr->blocks == NULL) {
        return FALSE;
    }
    cachePtr->nBlocks = nBlocks;
    direction = 0;
    previousIndex = -1;
    for (blockIndex = 0; blockIndex < nBlocks; blockIndex++) {
        LineDecimateBlock *blockPtr;
        Tcl_Size first;
        Tcl_Size last;
        Tcl_Size i;
        Tcl_Size minIndex;
        Tcl_Size maxIndex;

        blockPtr = cachePtr->blocks + blockIndex;
        blockPtr->nonPositiveYCount = 0;
        first = blockIndex * LINE_DECIMATE_BLOCK_SIZE;
        last = first + LINE_DECIMATE_BLOCK_SIZE;
        if (last > nPoints) {
            last = nPoints;
        }
        minIndex = first;
        maxIndex = first;
        for (i = first; i < last; i++) {
            double x;
            double y;

            x = xValues[i];
            y = yValues[i];
            /*
             * Initial cache implementation deliberately excludes
             * discontinuities. Stage 2 remains the fallback.
             */
            if ((!FINITE(x)) || (!FINITE(y))) {
                goto unsupported;
            }
            if (x <= 0.0) {
                cachePtr->allPositiveX = FALSE;
            }
            if (y <= 0.0) {
                blockPtr->nonPositiveYCount++;
                cachePtr->nonPositiveYCount++;
            }
            if (previousIndex >= 0) {
                double previousX;

                previousX = xValues[previousIndex];
                if (x > previousX) {
                    if (direction < 0) {
                        goto unsupported;
                    }
                    direction = 1;
                } else if (x < previousX) {
                    if (direction > 0) {
                        goto unsupported;
                    }
                    direction = -1;
                }
            }
            previousIndex = i;
            /*
             * Strict comparisons intentionally preserve the earliest
             * source index when extrema are equal, matching Stage 2.
             */
            if (y < yValues[minIndex]) {
                minIndex = i;
            }
            if (y > yValues[maxIndex]) {
                maxIndex = i;
            }
        }
        blockPtr->minIndex = minIndex;
        blockPtr->maxIndex = maxIndex;
    }
    cachePtr->allPositiveY = (cachePtr->nonPositiveYCount == 0);
    cachePtr->xDirection = direction;
    cachePtr->supported = TRUE;
    return TRUE;

unsupported:
    ckfree(cachePtr->blocks);
    cachePtr->blocks = NULL;
    cachePtr->nBlocks = 0;
    cachePtr->supported = FALSE;
    cachePtr->xDirection = 0;
    return FALSE;
}

/*
 *----------------------------------------------------------------------
 *
 * RebuildLineDecimateYBlock --
 *
 *      Rebuilds the Y-dependent summary for one existing Stage-3
 *      decimation block.
 *
 *      Source X is deliberately not inspected here: this helper is
 *      used only when the graph has received a precisely ranged Y-only
 *      update.
 *
 * Results:
 *      TRUE if the block was rebuilt successfully.
 *
 *      FALSE if the new Y data cannot be represented by the cached
 *      continuous-data path.
 *
 *----------------------------------------------------------------------
 */
static int RebuildLineDecimateYBlock(Line *linePtr, Tcl_Size blockIndex) {
    LineDecimateCache *cachePtr;
    LineDecimateBlock *blockPtr;
    const double *yValues;
    Tcl_Size first;
    Tcl_Size last;
    Tcl_Size minIndex;
    Tcl_Size maxIndex;
    Tcl_Size nonPositiveYCount;
    Tcl_Size i;

    cachePtr = &linePtr->decimateCache;
    if ((blockIndex < 0) || (blockIndex >= cachePtr->nBlocks)) {
        return FALSE;
    }
    yValues = cachePtr->yValues;
    if (yValues == NULL) {
        return FALSE;
    }
    blockPtr = cachePtr->blocks + blockIndex;
    first = blockIndex * LINE_DECIMATE_BLOCK_SIZE;
    last = first + LINE_DECIMATE_BLOCK_SIZE;
    if (last > cachePtr->nPoints) {
        last = cachePtr->nPoints;
    }
    if (first >= last) {
        return FALSE;
    }
    minIndex = first;
    maxIndex = first;
    nonPositiveYCount = 0;
    for (i = first; i < last; i++) {
        double y;

        y = yValues[i];
        /*
         * The simple cached path represents one continuous run.
         */
        if (!FINITE(y)) {
            return FALSE;
        }
        if (y <= 0.0) {
            nonPositiveYCount++;
        }
        /*
         * Preserve the earliest source index on equal extrema,
         * matching the full cache builder.
         */
        if (y < yValues[minIndex]) {
            minIndex = i;
        }
        if (y > yValues[maxIndex]) {
            maxIndex = i;
        }
    }
    if (cachePtr->nonPositiveYCount < blockPtr->nonPositiveYCount) {
        return FALSE;
    }
    cachePtr->nonPositiveYCount -= blockPtr->nonPositiveYCount;
    cachePtr->nonPositiveYCount += nonPositiveYCount;
    blockPtr->minIndex = minIndex;
    blockPtr->maxIndex = maxIndex;
    blockPtr->nonPositiveYCount = nonPositiveYCount;
    cachePtr->allPositiveY = (cachePtr->nonPositiveYCount == 0);
    return TRUE;
}

static int EnsureLineDecimateCache(Line *linePtr) {
    LineDecimateCache *cachePtr;
    Tcl_Size nPoints;

    cachePtr = &linePtr->decimateCache;
    nPoints = NumberOfPoints(&linePtr->core);
    /*
     * MAP_ITEM invalidation should normally catch data replacement.
     * These checks are an additional safeguard.
     */
    if (cachePtr->built && ((cachePtr->nPoints != nPoints) || (cachePtr->xValues != linePtr->core.x.valueArr) ||
                            (cachePtr->yValues != linePtr->core.y.valueArr))) {
        InvalidateLineDecimateCache(linePtr);
    }
    if (!cachePtr->built) {
        BuildLineDecimateCache(linePtr);
    }
    if (!cachePtr->supported) {
        return FALSE;
    }
    /*
     * A non-positive value is perfectly valid on a linear axis. It
     * prevents the simple cached path only while that axis is logarithmic,
     * because it would introduce discontinuities.
     */
    if (linePtr->core.axes.x->logScale && !cachePtr->allPositiveX) {
        return FALSE;
    }
    if (linePtr->core.axes.y->logScale && !cachePtr->allPositiveY) {
        return FALSE;
    }
    return TRUE;
}

/** @ingroup line_decimation
 * Maintain a persistent source-domain cache after element invalidation.
 * @param linePtr Borrowed element with current source pointers and change metadata.
 * @return TRUE when no cache exists or the existing cache can be retained or
 * updated; FALSE asks the caller to invalidate it. Not a geometry-generation result.
 *
 * Exact Y changes rebuild affected summaries; X changes invalidate ordering.
 * Growth delegates to checked prefix retention. Does not clear source change flags.
 */
static int RefreshLineDecimateCache(Line *linePtr) {
    LineDecimateCache *cachePtr;
    Element *elemPtr;
    Tcl_Size nPoints;
    Tcl_Size first;
    Tcl_Size last;
    Tcl_Size firstBlock;
    Tcl_Size lastBlock;
    Tcl_Size blockIndex;

    cachePtr = &linePtr->decimateCache;
    elemPtr = &linePtr->core;
    if (!cachePtr->built) {
        return TRUE;
    }
    nPoints = NumberOfPoints(elemPtr);
    /*
     * A pure tail append can preserve and extend the Stage-3 cache even
     * when vector growth relocates the backing arrays.
     *
     * Shrinkage, replacement, changes inside the cached prefix, or
     * unsupported new X ordering still fall back to full invalidation.
     */
    if ((cachePtr->nPoints != nPoints) || (cachePtr->xValues != elemPtr->x.valueArr) ||
        (cachePtr->yValues != elemPtr->y.valueArr)) {
        return GrowLineDecimateCache(linePtr, nPoints);
    }
    /*
     * Source X controls monotonicity and pixel-range binary searches.
     * Do not attempt incremental X maintenance yet.
     */
    if (elemPtr->x.changePending) {
        return FALSE;
    }
    /*
     * MAP_ITEM may have been caused by a pen/style/state option rather
     * than source data.  The data-domain cache itself survives those
     * changes.
     */
    if (!elemPtr->y.changePending) {
        return TRUE;
    }
    /*
     * Incremental maintenance requires a cache that was previously
     * known to support the Stage-3 path and an exact changed Y range.
     */
    if (!cachePtr->supported || elemPtr->y.changeAll) {
        return FALSE;
    }
    first = elemPtr->y.changedFirst;
    last = elemPtr->y.changedLast;
    if ((first < 0) || (last < first)) {
        return FALSE;
    }
    /*
     * Y may be longer than X.  Changes wholly beyond NumberOfPoints()
     * do not affect this line's mapped XY data.
     */
    if (first >= nPoints) {
        return TRUE;
    }
    if (last >= nPoints) {
        last = nPoints - 1;
    }
    firstBlock = first / LINE_DECIMATE_BLOCK_SIZE;
    lastBlock = last / LINE_DECIMATE_BLOCK_SIZE;
    for (blockIndex = firstBlock; blockIndex <= lastBlock; blockIndex++) {
        if (!RebuildLineDecimateYBlock(linePtr, blockIndex)) {
            return FALSE;
        }
    }
    return TRUE;
}

static void GetCachedRangeExtrema(Line *linePtr, Tcl_Size first, Tcl_Size last, Tcl_Size *minIndexPtr,
                                  Tcl_Size *maxIndexPtr) {
    LineDecimateCache *cachePtr;
    const double *yValues;
    Tcl_Size minIndex;
    Tcl_Size maxIndex;
    Tcl_Size i;

    cachePtr = &linePtr->decimateCache;
    yValues = cachePtr->yValues;
    minIndex = first;
    maxIndex = first;
    i = first;
    while (i <= last) {
        if (((i % LINE_DECIMATE_BLOCK_SIZE) == 0) && ((last - i) >= (LINE_DECIMATE_BLOCK_SIZE - 1))) {
            LineDecimateBlock *blockPtr;

            blockPtr = cachePtr->blocks + (i / LINE_DECIMATE_BLOCK_SIZE);
            if (yValues[blockPtr->minIndex] < yValues[minIndex]) {
                minIndex = blockPtr->minIndex;
            }
            if (yValues[blockPtr->maxIndex] > yValues[maxIndex]) {
                maxIndex = blockPtr->maxIndex;
            }
            i += LINE_DECIMATE_BLOCK_SIZE;
        } else {
            if (yValues[i] < yValues[minIndex]) {
                minIndex = i;
            }
            if (yValues[i] > yValues[maxIndex]) {
                maxIndex = i;
            }
            i++;
        }
    }
    *minIndexPtr = minIndex;
    *maxIndexPtr = maxIndex;
}

static int GetCachedPixelBucket(Graph *graphPtr, Line *linePtr, Tcl_Size index, double pixelMin, double pixelMax,
                                double *bucketPtr) {
    double x;
    double abscissa;

    x = linePtr->decimateCache.xValues[index];
    abscissa = MapLineDataAbscissa(graphPtr, linePtr, x);
    if (!FINITE(abscissa)) {
        return FALSE;
    }
    if (abscissa < pixelMin) {
        *bucketPtr = pixelMin - 1.0;
    } else if (abscissa > pixelMax) {
        *bucketPtr = pixelMax + 1.0;
    } else {
        *bucketPtr = trunc(abscissa);
    }
    return TRUE;
}

static Tcl_Size FindNextCachedPixelBucket(Graph *graphPtr, Line *linePtr, Tcl_Size first, double currentBucket,
                                          int direction, double pixelMin, double pixelMax, int *okPtr) {
    Tcl_Size low;
    Tcl_Size high;

    low = first + 1;
    high = linePtr->decimateCache.nPoints;
    while (low < high) {
        Tcl_Size middle;
        double bucket;

        middle = low + ((high - low) / 2);
        if (!GetCachedPixelBucket(graphPtr, linePtr, middle, pixelMin, pixelMax, &bucket)) {
            *okPtr = FALSE;
            return high;
        }
        if (((direction > 0) && (bucket <= currentBucket)) || ((direction < 0) && (bucket >= currentBucket))) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    *okPtr = TRUE;
    return low;
}

static int CanPreMapDecimateLine(Line *linePtr) {
    Element *elemPtr;
    LinePen *penPtr;

    if (!CanPixelDecimateLine(linePtr)) {
        return FALSE;
    }
    elemPtr = &linePtr->core;
    penPtr = LINE_PEN_FROM_CORE(elemPtr->normalPenPtr);
    /*
     * The first pre-map implementation is trace-only.  If the normal
     * pen needs mapped data points for symbols or value labels, retain
     * the existing full mapping path.
     */
    if (penPtr->symbol.type != SYMBOL_NONE) {
        return FALSE;
    }
    if (penPtr->valueShow != SHOW_NONE) {
        return FALSE;
    }
    /*
     * Error bars currently use the per-source-point style map and have
     * their own world-to-screen mapping.  Leave them on the existing
     * path for now.
     */
    if ((elemPtr->xError.nValues > 0) || (elemPtr->yError.nValues > 0) || (elemPtr->xHigh.nValues > 0) ||
        (elemPtr->xLow.nValues > 0) || (elemPtr->yHigh.nValues > 0) || (elemPtr->yLow.nValues > 0)) {
        return FALSE;
    }
    /*
     * If "all points active" uses symbols or value labels, the active
     * renderer also needs the complete symbolPts array.
     *
     * Explicit active indices (> 0) are fine: MapActiveSymbols()
     * maps those indices independently.
     */
    if ((elemPtr->nActiveIndices < 0) && (elemPtr->activePenPtr != NULL)) {
        LinePen *activePenPtr;

        activePenPtr = LINE_PEN_FROM_CORE(elemPtr->activePenPtr);
        if ((activePenPtr->symbol.type != SYMBOL_NONE) || (activePenPtr->valueShow != SHOW_NONE)) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * CanRetainLineDecimatePrefix --
 *
 *      Determines whether the first nPoints source values represented
 *      by an existing Stage-3 cache are known to be unchanged.
 *
 *      A vector append may relocate its backing array.  Pointer
 *      equality therefore is not required when the vector supplied an
 *      exact changed range beginning at or beyond the cached prefix.
 *
 *----------------------------------------------------------------------
 */
static int CanRetainLineDecimatePrefix(const ElemVector *vPtr, const double *cachedValues, Tcl_Size nPoints) {
    if (!vPtr->changePending) {
        /*
         * With no reported source change, an unexpected backing-array
         * replacement remains unsafe.
         */
        return vPtr->valueArr == cachedValues;
    }
    if (vPtr->changeAll) {
        return FALSE;
    }
    /*
     * Every reported change is outside the already-cached prefix.
     */
    if (vPtr->changedFirst >= nPoints) {
        return TRUE;
    }
    return FALSE;
}

/*
 *----------------------------------------------------------------------
 *
 * IsMonotonicMappedAbscissa --
 *
 *      Tests whether each continuous mapped run is monotonic along the
 *      physical screen dimension corresponding to data X.
 *
 *      Axis transforms are monotonic, so monotonic data X remains
 *      monotonic here even with logarithmic axes, -descending, and
 *      -invertxy.  The direction itself does not matter.
 *
 *      Discontinuous runs are tested independently because no segment
 *      will ever connect two such runs.
 *
 *----------------------------------------------------------------------
 */
static int IsMonotonicMappedAbscissa(Graph *graphPtr, MapInfo *mapPtr) {
    Tcl_Size i;
    int direction;

    if (mapPtr->nScreenPts < 2) {
        return TRUE;
    }
    direction = 0;
    for (i = 1; i < mapPtr->nScreenPts; i++) {
        double previous;
        double current;

        if ((mapPtr->breakBefore != NULL) && mapPtr->breakBefore[i]) {
            direction = 0;
            continue;
        }
        previous = PixelAbscissa(graphPtr, mapPtr->screenPts + i - 1);
        current = PixelAbscissa(graphPtr, mapPtr->screenPts + i);
        if (current > previous) {
            if (direction < 0) {
                return FALSE;
            }
            direction = 1;
        } else if (current < previous) {
            if (direction > 0) {
                return FALSE;
            }
            direction = -1;
        }
    }
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * AppendPixelBucket --
 *
 *      Adds the first, minimum-ordinate, maximum-ordinate, and last
 *      point of one screen-pixel bucket.
 *
 *      The selected source positions are sorted into original data order
 *      before being appended.  Duplicate representatives are removed.
 *
 *----------------------------------------------------------------------
 */
static void AppendPixelBucket(Tcl_Size first, Tcl_Size minIndex, Tcl_Size maxIndex, Tcl_Size last, Tcl_Size *selected,
                              Tcl_Size *countPtr) {
    Tcl_Size candidates[4];
    int i;
    int j;

    candidates[0] = first;
    candidates[1] = minIndex;
    candidates[2] = maxIndex;
    candidates[3] = last;
    /*
     * There are only four entries, so a tiny insertion sort is simpler
     * than involving qsort().
     */
    for (i = 1; i < 4; i++) {
        Tcl_Size value;

        value = candidates[i];
        j = i;
        while ((j > 0) && (candidates[j - 1] > value)) {
            candidates[j] = candidates[j - 1];
            j--;
        }
        candidates[j] = value;
    }
    for (i = 0; i < 4; i++) {
        if ((i == 0) || (candidates[i] != candidates[i - 1])) {
            selected[*countPtr] = candidates[i];
            (*countPtr)++;
        }
    }
}

/**
 * @ingroup line_configuration_smoothing
 * @brief Map a source X value to its physical screen coordinate.
 *
 * Uses horizontal axis mapping normally and vertical mapping when inverted,
 * with the element's X axis and current graph viewport. Borrows graph/line
 * state; does not validate the source or establish the viewport.
 */
INLINE static double MapLineDataAbscissa(Graph *graphPtr, Line *linePtr, double x) {
    if (graphPtr->inverted) {
        return Rbc_VMap(graphPtr, linePtr->core.axes.x, x);
    }
    return Rbc_HMap(graphPtr, linePtr->core.axes.x, x);
}

static int AppendSourcePixelBucket(Tcl_Size first, Tcl_Size minIndex, Tcl_Size maxIndex, Tcl_Size last, int startsRun,
                                   Tcl_Size capacity, Tcl_Size *selected, unsigned char *selectedBreakBefore,
                                   Tcl_Size *countPtr) {
    Tcl_Size candidate[4];
    int i;
    int j;
    int firstOutput;
    
    candidate[0] = first;
    candidate[1] = minIndex;
    candidate[2] = maxIndex;
    candidate[3] = last;
    /*
     * Sort the four source indices into original data order.
     */
    for (i = 1; i < 4; i++) {
        Tcl_Size value;

        value = candidate[i];
        j = i;

        while ((j > 0) && (candidate[j - 1] > value)) {
            candidate[j] = candidate[j - 1];
            j--;
        }
        candidate[j] = value;
    }
    firstOutput = TRUE;
    for (i = 0; i < 4; i++) {
        if ((i > 0) && (candidate[i] == candidate[i - 1])) {
            continue;
        }
        if (*countPtr >= capacity) {
            return FALSE;
        }
        selected[*countPtr] = candidate[i];
        selectedBreakBefore[*countPtr] = (unsigned char)(startsRun && firstOutput && (*countPtr > 0));
        (*countPtr)++;
        firstOutput = FALSE;
    }
    return TRUE;
}

static int BuildPixelDecimatedScreenPoints(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    Extents2D exts;
    Tcl_Size nPoints;
    Tcl_Size capacity;
    Tcl_Size *selected;
    unsigned char *selectedBreakBefore;
    Point2D *screenPts;
    Tcl_Size *indices;
    unsigned char *breakBefore;
    Tcl_Size count;
    Tcl_Size mappedCount;
    Tcl_Size i;
    size_t selectedBytes;
    size_t breakBytes;
    size_t pointBytes;
    size_t indexBytes;
    double pixelMin;
    double pixelMax;
    double pixelSpan;
    int haveBucket;
    double currentBucket;
    Tcl_Size first;
    Tcl_Size last;
    Tcl_Size minIndex;
    Tcl_Size maxIndex;
    double minY;
    double maxY;
    int startsRun;
    int nextStartsRun;
    int havePreviousX;
    double previousX;
    int direction;

    mapPtr->screenPts = NULL;
    mapPtr->indices = NULL;
    mapPtr->breakBefore = NULL;
    mapPtr->nScreenPts = 0;
    nPoints = NumberOfPoints(&linePtr->core);
    if (nPoints < 1) {
        return TRUE;
    }
    Rbc_GraphExtents(graphPtr, &exts);
    /*
     * Data X maps horizontally in a normal graph and vertically under
     * -invertxy.
     */
    if (graphPtr->inverted) {
        pixelMin = exts.top;
        pixelMax = exts.bottom;
    } else {
        pixelMin = exts.left;
        pixelMax = exts.right;
    }
    if (pixelMax < pixelMin) {
        double tmp;

        tmp = pixelMin;
        pixelMin = pixelMax;
        pixelMax = tmp;
    }
    /*
     * We retain at most four representatives for each visible pixel
     * bucket, plus two collapsed off-screen buckets.
     *
     * Multiple discontinuous runs could exceed this bound.  If that
     * happens, abandon the fast path and let the ordinary mapper handle
     * the element.
     */
    pixelSpan = ceil(pixelMax) - floor(pixelMin) + 3.0;
    if ((!FINITE(pixelSpan)) || (pixelSpan < 1.0) || (pixelSpan > (double)TCL_SIZE_MAX / 4.0)) {
        return FALSE;
    }
    capacity = (Tcl_Size)pixelSpan * 4;
    if (capacity > nPoints) {
        capacity = nPoints;
    }
    if (capacity < 1) {
        capacity = 1;
    }
    if ((GetLineArrayByteCount(capacity, sizeof(*selected), &selectedBytes) != TCL_OK) ||
        (GetLineArrayByteCount(capacity, sizeof(*selectedBreakBefore), &breakBytes) != TCL_OK)) {
        return FALSE;
    }
    selected = Tcl_AttemptAlloc(selectedBytes);
    if (selected == NULL) {
        return FALSE;
    }
    selectedBreakBefore = Tcl_AttemptAlloc(breakBytes);
    if (selectedBreakBefore == NULL) {
        ckfree(selected);
        return FALSE;
    }
    count = 0;
    haveBucket = FALSE;
    currentBucket = 0.0;
    first = 0;
    last = 0;
    minIndex = 0;
    maxIndex = 0;
    minY = 0.0;
    maxY = 0.0;
    startsRun = FALSE;
    nextStartsRun = FALSE;
    havePreviousX = FALSE;
    previousX = 0.0;
    direction = 0;
    for (i = 0; i < nPoints; i++) {
        double x;
        double y;
        double abscissa;
        double bucket;
        int valid;

        valid = GetLineDataPoint(linePtr, i, &x, &y);
        if (valid) {
            valid = FINITE(x) && FINITE(y);
        }
        /*
         * Match Rbc_HMap/Rbc_VMap logarithmic-domain behavior without
         * performing the Y transformation for every source sample.
         */
        if (valid && linePtr->core.axes.x->logScale && (x <= 0.0)) {
            valid = FALSE;
        }
        if (valid && linePtr->core.axes.y->logScale && (y <= 0.0)) {
            valid = FALSE;
        }
        if (valid) {
            abscissa = MapLineDataAbscissa(graphPtr, linePtr, x);
            if (!FINITE(abscissa)) {
                valid = FALSE;
            }
        }
        if (!valid) {
            /*
             * Finish the current continuous run.
             */
            if (haveBucket) {
                if (!AppendSourcePixelBucket(first, minIndex, maxIndex, last, startsRun, capacity, selected,
                                             selectedBreakBefore, &count)) {
                    ckfree(selected);
                    ckfree(selectedBreakBefore);
                    return FALSE;
                }
                haveBucket = FALSE;
            }
            nextStartsRun = (count > 0);
            havePreviousX = FALSE;
            direction = 0;
            continue;
        }
        /*
         * Check source-X monotonicity within each continuous run.
         *
         * Axis transformations are monotonic, so we do not need to
         * transform every X merely to establish ordering.
         */
        if (havePreviousX) {
            if (x > previousX) {
                if (direction < 0) {
                    ckfree(selected);
                    ckfree(selectedBreakBefore);
                    return FALSE;
                }
                direction = 1;
            } else if (x < previousX) {
                if (direction > 0) {
                    ckfree(selected);
                    ckfree(selectedBreakBefore);
                    return FALSE;
                }
                direction = -1;
            }
        }
        previousX = x;
        havePreviousX = TRUE;
        /*
         * Collapse everything left/before and right/after the visible
         * data-X extent into one bucket each.  For a monotonic run only
         * the boundary-neighbour representatives can participate in a
         * visible clipped segment.
         */
        if (abscissa < pixelMin) {
            bucket = pixelMin - 1.0;
        } else if (abscissa > pixelMax) {
            bucket = pixelMax + 1.0;
        } else {
            /*
             * Match the integer conversion used by the native
             * rendering paths.
             */
            bucket = trunc(abscissa);
        }
        if (!haveBucket) {
            currentBucket = bucket;
            first = i;
            last = i;
            minIndex = i;
            maxIndex = i;
            minY = y;
            maxY = y;
            startsRun = nextStartsRun;
            nextStartsRun = FALSE;
            haveBucket = TRUE;
            continue;
        }
        if (bucket == currentBucket) {
            last = i;
            if (y < minY) {
                minY = y;
                minIndex = i;
            }
            if (y > maxY) {
                maxY = y;
                maxIndex = i;
            }
            continue;
        }
        /*
         * The data-X pixel changed.  Finish the previous bucket.
         */
        if (!AppendSourcePixelBucket(first, minIndex, maxIndex, last, startsRun, capacity, selected,
                                     selectedBreakBefore, &count)) {
            ckfree(selected);
            ckfree(selectedBreakBefore);
            return FALSE;
        }
        currentBucket = bucket;
        first = i;
        last = i;
        minIndex = i;
        maxIndex = i;
        minY = y;
        maxY = y;
        startsRun = FALSE;
    }
    if (haveBucket) {
        if (!AppendSourcePixelBucket(first, minIndex, maxIndex, last, startsRun, capacity, selected,
                                     selectedBreakBefore, &count)) {
            ckfree(selected);
            ckfree(selectedBreakBefore);
            return FALSE;
        }
    }
    if (count < 1) {
        ckfree(selected);
        ckfree(selectedBreakBefore);
        return TRUE;
    }
    if ((GetLineArrayByteCount(count, sizeof(*screenPts), &pointBytes) != TCL_OK) ||
        (GetLineArrayByteCount(count, sizeof(*indices), &indexBytes) != TCL_OK) ||
        (GetLineArrayByteCount(count, sizeof(*breakBefore), &breakBytes) != TCL_OK)) {
        ckfree(selected);
        ckfree(selectedBreakBefore);
        return FALSE;
    }
    screenPts = Tcl_AttemptAlloc(pointBytes);
    if (screenPts == NULL) {
        ckfree(selected);
        ckfree(selectedBreakBefore);
        return FALSE;
    }
    indices = Tcl_AttemptAlloc(indexBytes);
    if (indices == NULL) {
        ckfree(screenPts);
        ckfree(selected);
        ckfree(selectedBreakBefore);
        return FALSE;
    }
    breakBefore = Tcl_AttemptAlloc(breakBytes);
    if (breakBefore == NULL) {
        ckfree(indices);
        ckfree(screenPts);
        ckfree(selected);
        ckfree(selectedBreakBefore);
        return FALSE;
    }
    mappedCount = 0;
    nextStartsRun = FALSE;
    for (i = 0; i < count; i++) {
        Point2D point;
        Tcl_Size sourceIndex;
        double x;
        double y;

        sourceIndex = selected[i];
        if (selectedBreakBefore[i]) {
            nextStartsRun = TRUE;
        }
        if (!GetLineDataPoint(linePtr, sourceIndex, &x, &y)) {
            nextStartsRun = TRUE;
            continue;
        }
        point = Rbc_Map2D(graphPtr, x, y, &linePtr->core.axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            nextStartsRun = TRUE;
            continue;
        }
        screenPts[mappedCount] = point;
        indices[mappedCount] = sourceIndex;
        breakBefore[mappedCount] = (unsigned char)(nextStartsRun && (mappedCount > 0));
        nextStartsRun = FALSE;
        mappedCount++;
    }
    ckfree(selected);
    ckfree(selectedBreakBefore);
    if (mappedCount < 1) {
        ckfree(screenPts);
        ckfree(indices);
        ckfree(breakBefore);
        return TRUE;
    }
    /*
     * Avoid carrying a break array when none exists, matching
     * GetScreenPoints().
     */
    {
        int hasBreaks;

        hasBreaks = FALSE;
        for (i = 1; i < mappedCount; i++) {
            if (breakBefore[i]) {
                hasBreaks = TRUE;
                break;
            }
        }
        if (!hasBreaks) {
            ckfree(breakBefore);
            breakBefore = NULL;
        }
    }
    mapPtr->screenPts = screenPts;
    mapPtr->indices = indices;
    mapPtr->breakBefore = breakBefore;
    mapPtr->nScreenPts = mappedCount;
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * GetCachedSourceRangeForAbscissa --
 *
 *      Finds the half-open source-index range [first,last) whose
 *      physical mapped data-X coordinate lies between low and high.
 *
 *      The Stage-3 cache guarantees that source X is globally
 *      monotonic.  The mapped direction may nevertheless be reversed
 *      by axis -descending or graph -invertxy, so determine ordering
 *      from the mapped endpoint abscissas.
 *
 * Results:
 *      TRUE on success.  firstPtr and lastPtr receive a half-open
 *      source range.  An empty range is represented by first == last.
 *
 *      FALSE if the mapped abscissa cannot safely be evaluated.
 *
 *----------------------------------------------------------------------
 */
static int GetCachedSourceRangeForAbscissa(Graph *graphPtr, Line *linePtr, double low, double high, Tcl_Size *firstPtr,
                                           Tcl_Size *lastPtr) {
    LineDecimateCache *cachePtr;
    Tcl_Size nPoints;
    Tcl_Size lo;
    Tcl_Size hi;
    Tcl_Size first;
    double firstAbscissa;
    double lastAbscissa;

    cachePtr = &linePtr->decimateCache;
    nPoints = cachePtr->nPoints;
    *firstPtr = 0;
    *lastPtr = 0;
    if (nPoints < 1) {
        return TRUE;
    }
    if (high < low) {
        double tmp;

        tmp = low;
        low = high;
        high = tmp;
    }
    firstAbscissa = MapLineDataAbscissa(graphPtr, linePtr, cachePtr->xValues[0]);
    lastAbscissa = MapLineDataAbscissa(graphPtr, linePtr, cachePtr->xValues[nPoints - 1]);
    if ((!FINITE(firstAbscissa)) || (!FINITE(lastAbscissa))) {
        return FALSE;
    }
    if (lastAbscissa > firstAbscissa) {
        /*
         * Increasing mapped abscissa.
         *
         * Find the first source point with:
         *
         *     abscissa >= low
         */
        lo = 0;
        hi = nPoints;
        while (lo < hi) {
            Tcl_Size middle;
            double abscissa;

            middle = lo + ((hi - lo) / 2);
            abscissa = MapLineDataAbscissa(graphPtr, linePtr, cachePtr->xValues[middle]);
            if (!FINITE(abscissa)) {
                return FALSE;
            }
            if (abscissa < low) {
                lo = middle + 1;
            } else {
                hi = middle;
            }
        }
        first = lo;
        /*
         * Find the first source point with:
         *
         *     abscissa > high
         */
        lo = first;
        hi = nPoints;
        while (lo < hi) {
            Tcl_Size middle;
            double abscissa;

            middle = lo + ((hi - lo) / 2);
            abscissa = MapLineDataAbscissa(graphPtr, linePtr, cachePtr->xValues[middle]);
            if (!FINITE(abscissa)) {
                return FALSE;
            }
            if (abscissa <= high) {
                lo = middle + 1;
            } else {
                hi = middle;
            }
        }
        *firstPtr = first;
        *lastPtr = lo;
        return TRUE;
    }
    if (lastAbscissa < firstAbscissa) {
        /*
         * Decreasing mapped abscissa.
         *
         * Find the first source point with:
         *
         *     abscissa <= high
         */
        lo = 0;
        hi = nPoints;
        while (lo < hi) {
            Tcl_Size middle;
            double abscissa;

            middle = lo + ((hi - lo) / 2);
            abscissa = MapLineDataAbscissa(graphPtr, linePtr, cachePtr->xValues[middle]);
            if (!FINITE(abscissa)) {
                return FALSE;
            }
            if (abscissa > high) {
                lo = middle + 1;
            } else {
                hi = middle;
            }
        }
        first = lo;
        /*
         * Find the first source point with:
         *
         *     abscissa < low
         */
        lo = first;
        hi = nPoints;
        while (lo < hi) {
            Tcl_Size middle;
            double abscissa;

            middle = lo + ((hi - lo) / 2);
            abscissa = MapLineDataAbscissa(graphPtr, linePtr, cachePtr->xValues[middle]);
            if (!FINITE(abscissa)) {
                return FALSE;
            }
            if (abscissa >= low) {
                lo = middle + 1;
            } else {
                hi = middle;
            }
        }
        *firstPtr = first;
        *lastPtr = lo;
        return TRUE;
    }
    /*
     * All source X values map to the same physical abscissa.
     */
    if ((firstAbscissa >= low) && (firstAbscissa <= high)) {
        *firstPtr = 0;
        *lastPtr = nPoints;
    }
    return TRUE;
}

/** @ingroup line_decimation
 * Build reduced mapped geometry from source-domain summaries.
 * @param graphPtr Borrowed graph with current axes and plot scope.
 * @param linePtr Borrowed eligible trace-only element; may build its persistent cache.
 * @param mapPtr Caller-owned mapping record receiving allocated points and source indices.
 * @return TRUE if handled, FALSE asks the caller to use the source-scanning fallback.
 *
 * Supports a finite continuous monotonic-X run. Binary searches locate pixel
 * buckets; block extrema supply first/min/max/last representatives in source
 * order. Successful output belongs to the mapping caller. breakBefore is NULL.
 */
static int BuildCachedPixelDecimatedScreenPoints(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr) {
    LineDecimateCache *cachePtr;
    Extents2D exts;
    Tcl_Size nPoints;
    Tcl_Size capacity;
    Tcl_Size *selected;
    unsigned char *selectedBreakBefore;
    Tcl_Size count;
    Tcl_Size first;
    Tcl_Size i;
    Point2D *screenPts;
    Tcl_Size *indices;
    size_t selectedBytes;
    size_t selectedBreakBytes;
    size_t pointBytes;
    size_t indexBytes;
    double pixelMin;
    double pixelMax;
    double pixelSpan;
    double firstBucket;
    double lastBucket;
    int direction;

    mapPtr->screenPts = NULL;
    mapPtr->indices = NULL;
    mapPtr->breakBefore = NULL;
    mapPtr->nScreenPts = 0;
    if (!EnsureLineDecimateCache(linePtr)) {
        return FALSE;
    }
    cachePtr = &linePtr->decimateCache;
    nPoints = cachePtr->nPoints;
    if (nPoints < 1) {
        return TRUE;
    }
    /*
     * Data X is represented by screen X normally and by screen Y when
     * the graph is inverted.
     */
    Rbc_GraphExtents(graphPtr, &exts);
    if (graphPtr->inverted) {
        pixelMin = exts.top;
        pixelMax = exts.bottom;
    } else {
        pixelMin = exts.left;
        pixelMax = exts.right;
    }
    if (pixelMax < pixelMin) {
        double tmp;

        tmp = pixelMin;
        pixelMin = pixelMax;
        pixelMax = tmp;
    }
    /*
     * Match the BuildPixelDecimatedScreenPoints() allocation bound:
     *
     *     visible integer pixel buckets
     *     + one collapsed bucket before the plot
     *     + one collapsed bucket after the plot
     *
     * Each bucket can produce at most four representatives.
     */
    pixelSpan = ceil(pixelMax) - floor(pixelMin) + 3.0;
    if ((!FINITE(pixelSpan)) || (pixelSpan < 1.0) || (pixelSpan > (double)TCL_SIZE_MAX / 4.0)) {
        return FALSE;
    }
    capacity = (Tcl_Size)pixelSpan * 4;
    if (capacity > nPoints) {
        capacity = nPoints;
    }
    if (capacity < 1) {
        capacity = 1;
    }
    if ((GetLineArrayByteCount(capacity, sizeof(*selected), &selectedBytes) != TCL_OK) ||
        (GetLineArrayByteCount(capacity, sizeof(*selectedBreakBefore), &selectedBreakBytes) != TCL_OK)) {
        return FALSE;
    }
    selected = Tcl_AttemptAlloc(selectedBytes);
    if (selected == NULL) {
        return FALSE;
    }
    /*
     * AppendSourcePixelBucket() already accepts a break array.  The
     * cached path currently has no discontinuities, so every entry
     * remains zero, but using the existing helper keeps the
     * first/min/max/last emission logic shared with BuildPixelDecimatedScreenPoints().
     */
    selectedBreakBefore = Tcl_AttemptAlloc(selectedBreakBytes);
    if (selectedBreakBefore == NULL) {
        ckfree(selected);
        return FALSE;
    }
    /*
     * Determine the direction in physical screen-pixel space, not from
     * source-X ordering.
     *
     * Axis -descending and graph -invertxy can reverse the mapped
     * direction even though source X itself is monotonic.
     */
    if (!GetCachedPixelBucket(graphPtr, linePtr, 0, pixelMin, pixelMax, &firstBucket)) {
        ckfree(selectedBreakBefore);
        ckfree(selected);
        return FALSE;
    }
    if (!GetCachedPixelBucket(graphPtr, linePtr, nPoints - 1, pixelMin, pixelMax, &lastBucket)) {
        ckfree(selectedBreakBefore);
        ckfree(selected);
        return FALSE;
    }
    if (lastBucket > firstBucket) {
        direction = 1;
    } else if (lastBucket < firstBucket) {
        direction = -1;
    } else {
        direction = 0;
    }
    count = 0;
    first = 0;
    while (first < nPoints) {
        Tcl_Size next;
        Tcl_Size last;
        Tcl_Size minIndex;
        Tcl_Size maxIndex;
        double bucket;
        int ok;

        if (!GetCachedPixelBucket(graphPtr, linePtr, first, pixelMin, pixelMax, &bucket)) {
            ckfree(selectedBreakBefore);
            ckfree(selected);
            return FALSE;
        }
        /*
         * If the first and final source points map to the same bucket,
         * monotonicity guarantees that the complete source sequence is
         * in that bucket.
         *
         * This also handles the all-X-equal case.
         */
        if (direction == 0) {
            next = nPoints;
        } else {
            next = FindNextCachedPixelBucket(graphPtr, linePtr, first, bucket, direction, pixelMin, pixelMax, &ok);
            if (!ok) {
                ckfree(selectedBreakBefore);
                ckfree(selected);
                return FALSE;
            }
            /*
             * Defensive check against a malformed binary-search result.
             */
            if ((next <= first) || (next > nPoints)) {
                ckfree(selectedBreakBefore);
                ckfree(selected);
                return FALSE;
            }
        }
        last = next - 1;
        GetCachedRangeExtrema(linePtr, first, last, &minIndex, &maxIndex);
        /*
         * The cached implementation currently represents one continuous
         * run, therefore startsRun is always FALSE.
         */
        if (!AppendSourcePixelBucket(first, minIndex, maxIndex, last, FALSE, capacity, selected, selectedBreakBefore,
                                     &count)) {
            ckfree(selectedBreakBefore);
            ckfree(selected);
            return FALSE;
        }
        first = next;
    }
    if (count < 1) {
        ckfree(selectedBreakBefore);
        ckfree(selected);
        return TRUE;
    }
    if ((GetLineArrayByteCount(count, sizeof(*screenPts), &pointBytes) != TCL_OK) ||
        (GetLineArrayByteCount(count, sizeof(*indices), &indexBytes) != TCL_OK)) {
        ckfree(selectedBreakBefore);
        ckfree(selected);
        return FALSE;
    }
    screenPts = Tcl_AttemptAlloc(pointBytes);
    if (screenPts == NULL) {
        ckfree(selectedBreakBefore);
        ckfree(selected);
        return FALSE;
    }
    indices = Tcl_AttemptAlloc(indexBytes);
    if (indices == NULL) {
        ckfree(screenPts);
        ckfree(selectedBreakBefore);
        ckfree(selected);
        return FALSE;
    }
    /*
     * Only the retained representatives now require the full X/Y
     * world-to-screen transformation.
     */
    for (i = 0; i < count; i++) {
        Tcl_Size sourceIndex;
        Point2D point;
        double x;
        double y;

        sourceIndex = selected[i];
        if (!GetLineDataPoint(linePtr, sourceIndex, &x, &y)) {
            ckfree(indices);
            ckfree(screenPts);
            ckfree(selectedBreakBefore);
            ckfree(selected);
            return FALSE;
        }
        point = Rbc_Map2D(graphPtr, x, y, &linePtr->core.axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            ckfree(indices);
            ckfree(screenPts);
            ckfree(selectedBreakBefore);
            ckfree(selected);
            return FALSE;
        }
        screenPts[i] = point;
        indices[i] = sourceIndex;
    }
    ckfree(selectedBreakBefore);
    ckfree(selected);
    mapPtr->screenPts = screenPts;
    mapPtr->indices = indices;
    mapPtr->breakBefore = NULL;
    mapPtr->nScreenPts = count;
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * GrowLineDecimateCache --
 *
 *      Extends an existing supported Stage-3 cache when the source XY
 *      data have only grown at the end.
 *
 *      The cached prefix is preserved.  Only newly exposed source
 *      samples are checked for finite values, X monotonicity, and
 *      logarithmic-domain eligibility.
 *
 *      If the old final block was partial, that block is rebuilt
 *      because newly appended samples now belong to it.  Additional
 *      64-sample blocks are constructed as necessary.
 *
 *      Backing-array relocation is allowed when exact vector change
 *      information proves that the cached prefix was not modified.
 *
 * Results:
 *      TRUE if the cache was successfully retained/grown.
 *
 *      FALSE if the caller must invalidate and rebuild/fall back.
 *
 *----------------------------------------------------------------------
 */
static int GrowLineDecimateCache(Line *linePtr, Tcl_Size nPoints) {
    LineDecimateCache *cachePtr;
    Element *elemPtr;
    const double *xValues;
    const double *yValues;
    LineDecimateBlock *blocks;
    Tcl_Size oldPoints;
    Tcl_Size newBlocks;
    Tcl_Size firstBlock;
    Tcl_Size blockIndex;
    Tcl_Size i;
    Tcl_Size addedNonPositiveY;
    int direction;
    int allPositiveX;
    size_t blockBytes;

    cachePtr = &linePtr->decimateCache;
    elemPtr = &linePtr->core;
    if ((!cachePtr->built) || (!cachePtr->supported)) {
        return FALSE;
    }
    oldPoints = cachePtr->nPoints;
    if (nPoints < oldPoints) {
        return FALSE;
    }
    xValues = elemPtr->x.valueArr;
    yValues = elemPtr->y.valueArr;
    if ((xValues == NULL) || (yValues == NULL)) {
        return FALSE;
    }
    /*
     * Appending may move either vector to a new allocation.  Exact
     * ranged notifications are what make that relocation safe.
     */
    if (!CanRetainLineDecimatePrefix(&elemPtr->x, cachePtr->xValues, oldPoints) ||
        !CanRetainLineDecimatePrefix(&elemPtr->y, cachePtr->yValues, oldPoints)) {
        return FALSE;
    }
    /*
     * One source vector may have grown while the other is still the
     * shorter vector. NumberOfPoints() therefore may not have changed
     * yet.  Rebind the retained source pointers so a later append of
     * the other vector can still extend this cache.
     */
    if (nPoints == oldPoints) {
        cachePtr->xValues = xValues;
        cachePtr->yValues = yValues;
        return TRUE;
    }
    if (oldPoints < 1) {
        return FALSE;
    }
    /*
     * First validate the new source interval without modifying the
     * existing cache.
     */
    direction = cachePtr->xDirection;
    allPositiveX = cachePtr->allPositiveX;
    addedNonPositiveY = 0;
    for (i = oldPoints; i < nPoints; i++) {
        double previousX;
        double x;
        double y;

        x = xValues[i];
        y = yValues[i];
        if ((!FINITE(x)) || (!FINITE(y))) {
            return FALSE;
        }
        if (x <= 0.0) {
            allPositiveX = FALSE;
        }
        if (y <= 0.0) {
            addedNonPositiveY++;
        }
        previousX = xValues[i - 1];
        if (x > previousX) {
            if (direction < 0) {
                return FALSE;
            }
            direction = 1;
        } else if (x < previousX) {
            if (direction > 0) {
                return FALSE;
            }
            direction = -1;
        }
    }
    newBlocks = (nPoints / LINE_DECIMATE_BLOCK_SIZE) + ((nPoints % LINE_DECIMATE_BLOCK_SIZE) != 0);
    blocks = cachePtr->blocks;
    if (newBlocks > cachePtr->nBlocks) {
        LineDecimateBlock *newBlockArr;

        if (GetLineArrayByteCount(newBlocks, sizeof(*newBlockArr), &blockBytes) != TCL_OK) {
            return FALSE;
        }
        newBlockArr = Tcl_AttemptAlloc(blockBytes);
        if (newBlockArr == NULL) {
            return FALSE;
        }
        if (cachePtr->nBlocks > 0) {
            size_t oldBytes;

            if (GetLineArrayByteCount(cachePtr->nBlocks, sizeof(*newBlockArr), &oldBytes) != TCL_OK) {
                ckfree(newBlockArr);
                return FALSE;
            }
            memcpy(newBlockArr, cachePtr->blocks, oldBytes);
        }
        blocks = newBlockArr;
    }
    /*
     * If oldPoints was not block-aligned, its old final block has
     * acquired new members and must be rebuilt.  If it was aligned,
     * oldPoints / block-size is simply the first new block.
     */
    firstBlock = oldPoints / LINE_DECIMATE_BLOCK_SIZE;
    for (blockIndex = firstBlock; blockIndex < newBlocks; blockIndex++) {
        LineDecimateBlock *blockPtr;
        Tcl_Size first;
        Tcl_Size last;
        Tcl_Size minIndex;
        Tcl_Size maxIndex;
        Tcl_Size nonPositiveYCount;

        blockPtr = blocks + blockIndex;
        first = blockIndex * LINE_DECIMATE_BLOCK_SIZE;
        last = first + LINE_DECIMATE_BLOCK_SIZE;
        if (last > nPoints) {
            last = nPoints;
        }
        minIndex = first;
        maxIndex = first;
        nonPositiveYCount = 0;
        for (i = first; i < last; i++) {
            double y;

            y = yValues[i];
            /*
             * New samples were validated above and cached-prefix
             * samples were validated when the cache was built.
             */
            if (y <= 0.0) {
                nonPositiveYCount++;
            }
            /*
             * Strict comparisons retain the earliest equal extremum,
             * matching BuildLineDecimateCache().
             */
            if (y < yValues[minIndex]) {
                minIndex = i;
            }
            if (y > yValues[maxIndex]) {
                maxIndex = i;
            }
        }
        blockPtr->minIndex = minIndex;
        blockPtr->maxIndex = maxIndex;
        blockPtr->nonPositiveYCount = nonPositiveYCount;
    }
    /*
     * Commit only after all validation/allocation has succeeded.
     */
    if (blocks != cachePtr->blocks) {
        ckfree(cachePtr->blocks);
        cachePtr->blocks = blocks;
    }
    cachePtr->nBlocks = newBlocks;
    cachePtr->nPoints = nPoints;
    cachePtr->xValues = xValues;
    cachePtr->yValues = yValues;
    cachePtr->xDirection = direction;
    cachePtr->allPositiveX = allPositiveX;
    cachePtr->nonPositiveYCount += addedNonPositiveY;
    cachePtr->allPositiveY = (cachePtr->nonPositiveYCount == 0);
    return TRUE;
}

/** @ingroup line_decimation
 * Reduce eligible mapped geometry without changing source values.
 * @param graphPtr Borrowed graph defining the physical data-X direction.
 * @param mapPtr Owned mapped arrays; replacements, when made, remain owned by caller.
 *
 * Retains first/min/max/last representatives per pixel bucket and continuous
 * run. Leaves geometry unchanged for short/nonmonotonic inputs, allocation
 * failure or no useful reduction. Preserves source indices and run boundaries.
 */
static void DecimatePointsByPixels(Graph *graphPtr, MapInfo *mapPtr) {
    Point2D *screenPts;
    Tcl_Size *indices;
    unsigned char *breakBefore;
    Tcl_Size *selected;
    Tcl_Size nPoints;
    Tcl_Size count;
    Tcl_Size runStart;
    size_t selectedBytes;
    size_t pointBytes;
    size_t indexBytes;

    if ((mapPtr == NULL) || (mapPtr->screenPts == NULL) || (mapPtr->indices == NULL) || (mapPtr->nScreenPts < 5)) {
        return;
    }
    /*
     * Never apply pixel-column aggregation to an arbitrary XY path.
     * A non-monotonic path can leave and later revisit the same pixel
     * column, and combining those visits would change its topology.
     */
    if (!IsMonotonicMappedAbscissa(graphPtr, mapPtr)) {
        return;
    }
    nPoints = mapPtr->nScreenPts;
    if (GetLineArrayByteCount(nPoints, sizeof(*selected), &selectedBytes) != TCL_OK) {
        return;
    }
    selected = Tcl_AttemptAlloc(selectedBytes);
    if (selected == NULL) {
        return;
    }
    count = 0;
    runStart = 0;
    while (runStart < nPoints) {
        Tcl_Size runEnd;
        Tcl_Size i;

        /*
         * Find the end of this continuous data run.
         */
        runEnd = runStart + 1;
        while ((runEnd < nPoints) && ((mapPtr->breakBefore == NULL) || !mapPtr->breakBefore[runEnd])) {
            runEnd++;
        }
        /*
         * Reduce consecutive equal pixel buckets within this run.
         */
        i = runStart;
        while (i < runEnd) {
            Tcl_Size first;
            Tcl_Size last;
            Tcl_Size minIndex;
            Tcl_Size maxIndex;
            Tcl_Size j;
            double bucket;
            double minOrdinate;
            double maxOrdinate;

            first = i;
            last = i;
            minIndex = i;
            maxIndex = i;
            /*
             * The native GDI/X11 paths truncate Point2D coordinates when
             * converting them to integer drawable coordinates.
             *
             * trunc() gives us the same pixel grouping without risking
             * overflow by casting a potentially far-offscreen coordinate
             * to int here.
             */
            bucket = trunc(PixelAbscissa(graphPtr, mapPtr->screenPts + i));
            minOrdinate = PixelOrdinate(graphPtr, mapPtr->screenPts + i);
            maxOrdinate = minOrdinate;
            for (j = i + 1; j < runEnd; j++) {
                double nextBucket;
                double ordinate;

                nextBucket = trunc(PixelAbscissa(graphPtr, mapPtr->screenPts + j));
                if (nextBucket != bucket) {
                    break;
                }
                last = j;
                ordinate = PixelOrdinate(graphPtr, mapPtr->screenPts + j);
                if (ordinate < minOrdinate) {
                    minOrdinate = ordinate;
                    minIndex = j;
                }
                if (ordinate > maxOrdinate) {
                    maxOrdinate = ordinate;
                    maxIndex = j;
                }
            }
            AppendPixelBucket(first, minIndex, maxIndex, last, selected, &count);
            i = j;
        }
        runStart = runEnd;
    }

    /*
     * Sparse data naturally produces one representative per original
     * point.  In that case -decimate auto costs only the scan above and
     * leaves the existing arrays untouched.
     */
    if (count == nPoints) {
        ckfree(selected);
        return;
    }
    if ((count <= 0) || (GetLineArrayByteCount(count, sizeof(*screenPts), &pointBytes) != TCL_OK) ||
        (GetLineArrayByteCount(count, sizeof(*indices), &indexBytes) != TCL_OK)) {
        ckfree(selected);
        return;
    }
    screenPts = Tcl_AttemptAlloc(pointBytes);
    if (screenPts == NULL) {
        ckfree(selected);
        return;
    }
    indices = Tcl_AttemptAlloc(indexBytes);
    if (indices == NULL) {
        ckfree(screenPts);
        ckfree(selected);
        return;
    }
    breakBefore = NULL;
    if (mapPtr->breakBefore != NULL) {
        size_t breakBytes;

        if (GetLineArrayByteCount(count, sizeof(*breakBefore), &breakBytes) != TCL_OK) {
            ckfree(screenPts);
            ckfree(indices);
            ckfree(selected);
            return;
        }
        breakBefore = Tcl_AttemptAlloc(breakBytes);
        if (breakBefore == NULL) {
            ckfree(screenPts);
            ckfree(indices);
            ckfree(selected);
            return;
        }
    }
    /*
     * Construct the reduced arrays atomically.  selected[] contains
     * positions in the old MapInfo arrays, not source-data indices.
     */
    {
        Tcl_Size i;

        for (i = 0; i < count; i++) {
            Tcl_Size k;

            k = selected[i];
            screenPts[i] = mapPtr->screenPts[k];
            indices[i] = mapPtr->indices[k];
            if (breakBefore != NULL) {
                breakBefore[i] = mapPtr->breakBefore[k];
            }
        }
    }
    ckfree(selected);
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

/**
 * @ingroup line_geometry_lifecycle
 * @brief Simplify each continuous mapped run without joining gaps.
 *
 * Borrows the line point map and forwards the tolerance to Rbc_SimplifyLine.
 * Selected points retain original source indices and break flags. Replaces
 * and frees old arrays only after all replacement arrays are available.
 * Invalid counts, size/allocation failures, or unchanged selection leave the
 * original map intact. Does not modify source vectors or report Tcl errors.
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

/**
 * @ingroup line_geometry_lifecycle
 * @brief Replace a mapped polyline with stepped geometry.
 *
 * Processes each continuous run independently. Each intermediate point
 * changes data X first while retaining the preceding data Y, accounting for
 * inverted axes. Intermediate and following points use the following source
 * index. Breaks copy the next point without inserting a connecting step.
 *
 * Transfers newly allocated arrays and frees the old map on success. On
 * size/allocation failure, retains old arrays and changes effective smooth
 * to PEN_SMOOTH_NONE; the requested smoothing option is unchanged.
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

/**
 * @ingroup line_configuration_smoothing
 * @brief Resample a continuous run as a function of mapped data X.
 *
 * Requires a nonempty map with valid source indices and no internal breaks.
 * Natural/quadratic smoothing uses screen X normally or screen Y when inverted.
 * Reflects decreasing abscissas into increasing canonical coordinates without
 * reordering source indices. Retains every knot and adds visible pixel samples;
 * inserted samples inherit the starting knot's source index.
 *
 * Commits replacement arrays only after interpolation succeeds. Size/allocation
 * or evaluator failures preserve the input and set effective smoothing to none.
 * Early exits for fewer than three points, nonmonotonic/equal abscissas, an
 * invisible interval or empty screen span leave both input and effective mode
 * unchanged. Thus the effective flag alone does not prove smoothing occurred.
 * Does not alter reqSmooth or report Tcl errors; break handling belongs to
 * GenerateSplineRuns.
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

/**
 * @ingroup line_configuration_smoothing
 * @brief Resample a continuous run by source interval and local parameter.
 *
 * Requires nonempty finite mapped geometry with parallel source indices and
 * no internal breaks. Retains knots and adds samples approximately every two
 * pixels along visible portions of original straight segments. Evaluation
 * parameters identify the source interval and its normalized position; inserted
 * samples report the interval's starting source index.
 *
 * Selects natural, quadratic or Catmull-Rom evaluation from effective smooth.
 * Replaces point/index arrays only after successful evaluation. Size, allocation,
 * capacity or evaluator failure frees candidates, retains original arrays and
 * sets effective smoothing to none, leaving reqSmooth unchanged. Sample density
 * uses the original segments, not an adaptive bound on the curved result.
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
    switch (linePtr->smooth) {
    case PEN_SMOOTH_NATURAL:
        result = Rbc_NaturalParametricSplineEval(origPts, nOrigPts, intpPts, nIntpPts);
        break;
    case PEN_SMOOTH_QUADRATIC:
        result = Rbc_QuadraticParametricSplineEval(origPts, nOrigPts, intpPts, nIntpPts);
        break;
    case PEN_SMOOTH_CATROM:
        result = Rbc_CatromParametricSpline(origPts, nOrigPts, intpPts, nIntpPts);
        break;
    default:
        result = FALSE;
        break;
    }
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

/**
 * @ingroup line_mapping_pipeline
 * @brief Release a temporary map run and clear its record.
 *
 * Frees owned screen points, source indices and optional break flags, then
 * zeros the complete MapInfo. The record itself remains caller-owned.
 */
static void FreeMapRun(MapInfo *mapPtr) {
    if (mapPtr->screenPts != NULL) {
        ckfree(mapPtr->screenPts);
    }
    if (mapPtr->indices != NULL) {
        ckfree(mapPtr->indices);
    }
    if (mapPtr->breakBefore != NULL) {
        ckfree(mapPtr->breakBefore);
    }
    memset(mapPtr, 0, sizeof(*mapPtr));
}

/**
 * @ingroup line_mapping_pipeline
 * @brief Copy one continuous source interval into an empty temporary map.
 *
 * The caller supplies a valid first/length range and an unused destination.
 * Zeros the destination before allocating; does not release previous contents.
 * Copies points and original indices, deliberately omitting break flags because
 * the interval represents one continuous run. Returns FALSE for nonpositive
 * length, byte-count overflow or attempted-allocation failure, cleaning partial
 * storage. Borrows and does not modify the source.
 */
static int CopyMapRun(const MapInfo *srcPtr, Tcl_Size first, Tcl_Size length, MapInfo *dstPtr) {
    size_t pointBytes;
    size_t indexBytes;

    memset(dstPtr, 0, sizeof(*dstPtr));
    if (length < 1) {
        return FALSE;
    }
    if (((size_t)length > (SIZE_MAX / sizeof(*dstPtr->screenPts))) ||
        ((size_t)length > (SIZE_MAX / sizeof(*dstPtr->indices)))) {
        return FALSE;
    }
    pointBytes = (size_t)length * sizeof(*dstPtr->screenPts);
    indexBytes = (size_t)length * sizeof(*dstPtr->indices);
    dstPtr->screenPts = Tcl_AttemptAlloc(pointBytes);
    if (dstPtr->screenPts == NULL) {
        return FALSE;
    }
    dstPtr->indices = Tcl_AttemptAlloc(indexBytes);
    if (dstPtr->indices == NULL) {
        ckfree(dstPtr->screenPts);
        dstPtr->screenPts = NULL;
        return FALSE;
    }
    memcpy(dstPtr->screenPts, srcPtr->screenPts + first, pointBytes);
    memcpy(dstPtr->indices, srcPtr->indices + first, indexBytes);
    dstPtr->nScreenPts = length;
    dstPtr->breakBefore = NULL;
    return TRUE;
}

/**
 * @ingroup line_mapping_pipeline
 * @brief Smooth discontinuous geometry one continuous run at a time.
 *
 * Requires a nonempty map with break flags. Copies each run, applies the
 * selected ordinary or parametric generator to runs of at least three points,
 * then concatenates results with new boundary flags. Individual generator
 * fallbacks retain that run's copied coordinates. Effective smoothing remains
 * the requested mode if any run succeeds, otherwise PEN_SMOOTH_NONE.
 *
 * Replaces original map arrays only after all combined storage is ready.
 * Run-copy, size or combined-allocation failure frees temporary storage,
 * retains the original map and sets effective smoothing to PEN_SMOOTH_NONE.
 * Does not change reqSmooth or report an interpreter error.
 */
static void GenerateSplineRuns(Graph *graphPtr, Line *linePtr, MapInfo *mapPtr, int parametric) {
    MapInfo *runs;
    Point2D *screenPts;
    Tcl_Size *indices;
    unsigned char *breakBefore;
    Smoothing requestedSmooth;
    Tcl_Size nRuns;
    Tcl_Size runIndex;
    Tcl_Size start;
    Tcl_Size total;
    Tcl_Size offset;
    Tcl_Size i;
    int smoothedAny;

    assert(mapPtr->breakBefore != NULL);
    assert(mapPtr->nScreenPts > 0);
    runs = NULL;
    screenPts = NULL;
    indices = NULL;
    breakBefore = NULL;
    requestedSmooth = linePtr->smooth;
    smoothedAny = FALSE;
    /*
     * Count continuous runs.
     */
    nRuns = 1;
    for (i = 1; i < mapPtr->nScreenPts; i++) {
        if (mapPtr->breakBefore[i]) {
            nRuns++;
        }
    }
    if ((size_t)nRuns > (SIZE_MAX / sizeof(*runs))) {
        goto fallback;
    }
    runs = Tcl_AttemptAlloc((size_t)nRuns * sizeof(*runs));
    if (runs == NULL) {
        goto fallback;
    }
    memset(runs, 0, (size_t)nRuns * sizeof(*runs));
    /*
     * Copy and smooth each continuous run independently.
     */
    runIndex = 0;
    start = 0;
    for (i = 1; i <= mapPtr->nScreenPts; i++) {
        int atEnd;
        int atBreak;

        atEnd = (i == mapPtr->nScreenPts);
        atBreak = (!atEnd && mapPtr->breakBefore[i]);
        if (atEnd || atBreak) {
            Tcl_Size length;

            length = i - start;
            if (!CopyMapRun(mapPtr, start, length, runs + runIndex)) {
                goto fallback;
            }
            if (length >= 3) {
                /*
                 * The individual spline generators use
                 * linePtr->smooth to select/evaluate the algorithm.
                 */
                linePtr->smooth = requestedSmooth;
                if (parametric) {
                    GenerateParametricSpline(graphPtr, linePtr, runs + runIndex);
                } else {
                    GenerateSpline(graphPtr, linePtr, runs + runIndex);
                }
                if (linePtr->smooth == requestedSmooth) {
                    smoothedAny = TRUE;
                }
                /*
                 * A failed run falls back to its original copied
                 * coordinates.  Restore the requested mode before
                 * processing the next run.
                 */
                linePtr->smooth = requestedSmooth;
            }
            runIndex++;
            start = i;
        }
    }
    /*
     * Determine the size of the combined result.
     */
    total = 0;
    for (i = 0; i < nRuns; i++) {
        if (runs[i].nScreenPts > (TCL_SIZE_MAX - total)) {
            goto fallback;
        }
        total += runs[i].nScreenPts;
    }
    if ((total < 1) || ((size_t)total > (SIZE_MAX / sizeof(*screenPts))) ||
        ((size_t)total > (SIZE_MAX / sizeof(*indices))) || ((size_t)total > (SIZE_MAX / sizeof(*breakBefore)))) {
        goto fallback;
    }
    screenPts = Tcl_AttemptAlloc((size_t)total * sizeof(*screenPts));
    if (screenPts == NULL) {
        goto fallback;
    }
    indices = Tcl_AttemptAlloc((size_t)total * sizeof(*indices));
    if (indices == NULL) {
        goto fallback;
    }
    breakBefore = Tcl_AttemptAlloc((size_t)total * sizeof(*breakBefore));
    if (breakBefore == NULL) {
        goto fallback;
    }
    memset(breakBefore, 0, (size_t)total * sizeof(*breakBefore));
    /*
     * Concatenate the runs and recreate their boundaries.
     */
    offset = 0;
    for (i = 0; i < nRuns; i++) {
        Tcl_Size count;

        count = runs[i].nScreenPts;
        memcpy(screenPts + offset, runs[i].screenPts, (size_t)count * sizeof(*screenPts));
        memcpy(indices + offset, runs[i].indices, (size_t)count * sizeof(*indices));
        if (offset > 0) {
            breakBefore[offset] = TRUE;
        }
        offset += count;
    }
    /*
     * Commit atomically.
     */
    ckfree(mapPtr->screenPts);
    ckfree(mapPtr->indices);
    ckfree(mapPtr->breakBefore);
    mapPtr->screenPts = screenPts;
    mapPtr->indices = indices;
    mapPtr->breakBefore = breakBefore;
    mapPtr->nScreenPts = total;
    screenPts = NULL;
    indices = NULL;
    breakBefore = NULL;
    linePtr->smooth = smoothedAny ? requestedSmooth : PEN_SMOOTH_NONE;
    for (i = 0; i < nRuns; i++) {
        FreeMapRun(runs + i);
    }
    ckfree(runs);
    return;

fallback:
    if (screenPts != NULL) {
        ckfree(screenPts);
    }
    if (indices != NULL) {
        ckfree(indices);
    }
    if (breakBefore != NULL) {
        ckfree(breakBefore);
    }
    if (runs != NULL) {
        for (i = 0; i < nRuns; i++) {
            FreeMapRun(runs + i);
        }
        ckfree(runs);
    }
    /*
     * The original mapPtr has not been modified.  Use its unsmoothed
     * runs and retain their breakBefore boundaries.
     */
    linePtr->smooth = PEN_SMOOTH_NONE;
}

/**
 * @ingroup line_mapping_pipeline
 * @brief Return conservative pixel reach for a symbol.
 *
 * Returns zero for a NULL pen or SYMBOL_NONE, otherwise size plus twice
 * the outline width and two pixels for rounding. Borrows the pen and does
 * not compute exact symbol bounds or allocate storage.
 */
static double SymbolPadding(LinePen *penPtr, int size) {
    if ((penPtr == NULL) || (penPtr->symbol.type == SYMBOL_NONE)) {
        return 0.0;
    }
    return (double)size + 2.0 * (double)penPtr->symbol.outlineWidth + 2.0;
}

/**
 * @ingroup line_mapping_pipeline
 * @brief Expand mapping extents to retain partially visible symbols.
 *
 * Starts from Rbc_GraphExtents and expands all sides by the largest padding
 * among palette pens and the active pen. Whole-element activation can reuse
 * normal symbol arrays, so the active pen must contribute. Calls ScaleSymbol,
 * which may capture axis-range baselines. Returns conservative center bounds;
 * actual drawing still requires clipping to the plot viewport.
 */
static void GetSymbolMapExtents(Graph *graphPtr, Line *linePtr, Extents2D *extsPtr) {
    Rbc_ChainLink *linkPtr;
    double padding = 0.0;

    Rbc_GraphExtents(graphPtr, extsPtr);
    for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        LinePenStyle *stylePtr = Rbc_ChainGetValue(linkPtr);
        int size = ScaleSymbol(&linePtr->core, stylePtr->penPtr->symbol.size);
        double reach = SymbolPadding(stylePtr->penPtr, size);

        if (reach > padding) {
            padding = reach;
        }
    }
    if (linePtr->core.activePenPtr != NULL) {
        LinePen *penPtr = LINE_PEN_FROM_CORE(linePtr->core.activePenPtr);
        int size = ScaleSymbol(&linePtr->core, penPtr->symbol.size);
        double reach = SymbolPadding(penPtr, size);

        if (reach > padding) {
            padding = reach;
        }
    }
    extsPtr->left -= padding;
    extsPtr->right += padding;
    extsPtr->top -= padding;
    extsPtr->bottom += padding;
}

/**
 * @ingroup line_mapping_pipeline
 * @brief Copy visible candidate centers and original indices into the line.
 *
 * Borrows a source-ordered map and filters centers through expanded symbol
 * extents. The caller must have cleared old symbol storage first. Transfers
 * new arrays and count when any centers survive; otherwise frees candidates.
 * Empty input, checked-size failure or attempted-allocation failure leaves
 * existing line fields unchanged. Does not clip the symbol shapes themselves.
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
    GetSymbolMapExtents(graphPtr, linePtr, &exts);
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

/**
 * @ingroup line_mapping_pipeline
 * @brief Retain original symbol centers that survived trace reduction.
 *
 * Requires both mappings in source order, before MergePens. Matches both
 * source index and exact screen coordinates, since inserted step/spline
 * points can share an original index. Compacts symbol points and indices in
 * place without changing capacity. Sets pointMapOmitted if any centers are
 * removed so exact picking can search source samples instead.
 */
static void FilterReducedSymbols(Line *linePtr, const MapInfo *mapPtr) {
    Tcl_Size i;
    Tcl_Size j = 0;
    Tcl_Size count = 0;

    for (i = 0; i < linePtr->nSymbolPts; i++) {
        Tcl_Size index = linePtr->symbolToData[i];
        Point2D point = linePtr->symbolPts[i];
        int retained = FALSE;

        while ((j < mapPtr->nScreenPts) && (mapPtr->indices[j] < index)) {
            j++;
        }
        while ((j < mapPtr->nScreenPts) && (mapPtr->indices[j] == index)) {
            if ((mapPtr->screenPts[j].x == point.x) && (mapPtr->screenPts[j].y == point.y)) {
                retained = TRUE;
            }
            j++;
        }
        if (retained) {
            linePtr->symbolPts[count] = point;
            linePtr->symbolToData[count++] = index;
        }
    }
    if (count < linePtr->nSymbolPts) {
        /* Exact point searches must continue to consider every sample. */
        linePtr->pointMapOmitted = TRUE;
    }
    linePtr->nSymbolPts = count;
}

/**
 * @ingroup line_geometry_lifecycle
 * @brief Rebuild visible symbols for explicit active indices.
 *
 * Releases the previous active point/index arrays first. Filters explicit
 * indices against the current point count, finite source/mapped coordinates
 * and symbol-map extents, preserving selection order and source indices.
 * Whole-element activation is handled elsewhere, not expanded here.
 *
 * Transfers candidate arrays only if points survive. Checked allocation
 * failure or an empty result leaves no active geometry. Clears ACTIVE_PENDING
 * on every exit, without changing the retained activation selection.
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
    GetSymbolMapExtents(graphPtr, linePtr, &exts);
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
        if (!GetLineDataPoint(linePtr, pointIndex, &x, &y)) {
            continue;
        }
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

/**
 * @ingroup line_mapping_pipeline
 * @brief Build independent clipped segments for a strip element.
 *
 * Borrows mapped points and skips pairs crossing a break flag. Clips each
 * remaining adjacent pair against graph extents and records the starting
 * point's source index. Requires old strip storage to be cleared by the caller.
 * Transfers arrays only when segments survive; frees empty candidates.
 * Insufficient points, size failure or attempted-allocation failure leaves
 * line fields unchanged. Does not apply the line trace-direction filter.
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

/**
 * @ingroup line_mapping_pipeline
 * @brief Group mapped geometry into palette-owned views of line storage.
 *
 * Requires a nonempty palette. For one style, attaches existing arrays
 * without allocation and permits a NULL dataToStyle. For multiple styles,
 * requires every source index to map to a valid palette style. Validates all
 * byte counts before changing slices, then reorders strips, symbols and
 * X/Y error segments with their parallel original-index arrays.
 *
 * Preserves order within each style and frees replaced backing arrays.
 * Style pointers borrow slices; the line owns the complete allocations.
 * Size failure returns without mutation; ordinary allocation uses ckalloc.
 * Run source-order filtering before this operation. Trace chains are not
 * reordered here.
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

/**
 * @ingroup line_mapping_pipeline
 * @brief Classify a finite point relative to a clipping rectangle.
 *
 * Returns a bit mask for sides strictly outside the rectangle. Points on
 * its edges are inside. Borrows both arguments without mutation; callers
 * supply valid ordered extents and finite coordinates.
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

/**
 * @ingroup line_mapping_pipeline
 * @brief Clip segment endpoints in place using supplied outcodes.
 *
 * Borrows a valid rectangle and writable finite endpoints with matching
 * outcodes. Iteratively intersects outside endpoints with rectangle edges.
 * Returns zero for an accepted segment and nonzero for rejection, opposite
 * to a conventional success predicate. May modify endpoints even when the
 * segment is ultimately rejected. Does not allocate or preserve originals.
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

/**
 * @ingroup line_mapping_pipeline
 * @brief Append an owned copy of a mapped interval to the trace chain.
 *
 * Caller supplies a valid interval and allocation-safe length. Copies points
 * and source indices; if no index array exists, uses positions in the map.
 * Stores the starting map offset, creates the chain lazily and appends a new
 * trace. Uses ckalloc; FreeTraces owns the resulting arrays and record.
 * Does not transfer or modify the source map.
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

/**
 * @ingroup line_geometry_lifecycle
 * @brief Release all owned line traces and their chain.
 *
 * Frees each trace's point and source-index arrays and the trace record,
 * then destroys the chain and clears its pointer. Does not release source
 * data or other mapped arrays.
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

/**
 * @ingroup line_mapping_pipeline
 * @brief Return a screen-derived coordinate ordered by increasing data X.
 *
 * Uses screen X normally and negated screen Y for inverted graphs, then
 * undoes X-axis descending direction. This coordinate is for trace-direction
 * comparisons, not an inverse transform or recovered data value. Borrows all
 * arguments and performs no allocation.
 */
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

/**
 * @ingroup line_mapping_pipeline
 * @brief Split and clip a mapped polyline into owned drawable traces.
 *
 * Requires a nonempty writable map. Break flags prevent testing nonexistent
 * connecting segments. Trace direction is measured in data-X order, including
 * inverted and descending axes. Rejected segments and clipped exits split
 * traces; only runs with at least two points are saved.
 *
 * Clipping mutates temporary map coordinates. An outside following point is
 * restored after saving an accepted clipped exit, but the map is not generally
 * preserved. SaveTrace copies surviving geometry into line-owned storage.
 * Call fill and source-coordinate filtering before this destructive stage.
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

/**
 * @ingroup line_fills
 * @brief Build one clipped polygon from the mapped center line.
 * @param graphPtr Borrowed graph in the element's mapping viewport.
 * @param linePtr Borrowed line with old fill storage already cleared.
 * @param mapPtr Borrowed smoothed/reduced screen points, never modified here.
 * @details Discontinuous input is not filled. Baseline closure adds two screen
 * vertices, origin closure adds mapped (0,0), and chord closure adds none.
 * On success transfers the clipped array to fillPts/nFillPts. Insufficient
 * points, invalid origin, size overflow, allocation failure or an empty clip
 * leave those fields unchanged. Temporary unclipped storage is always freed.
 * This routine does not free a previous fill: ResetLine must run first.
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
    Tcl_Size extraPoints;
    Point2D origin;

    /*
     * Line currently stores one fill polygon only.  Filling a mapped
     * line containing discontinuities would incorrectly bridge separate
     * data runs.
     */
    if (mapPtr->breakBefore != NULL) {
        return;
    }
    /* Baseline and origin closure add vertices; chord closure does not. */
    if (mapPtr->nScreenPts < ((linePtr->areaClose == LINE_AREA_CHORD) ? 3 : 2)) {
        return;
    }
    /*
     * Add the vertices required by the selected closure.
     * Rbc_PolyRectClip() now generates the closing edge internally.
     */
    extraPoints = (linePtr->areaClose == LINE_AREA_BASELINE) ? 2 :
                  (linePtr->areaClose == LINE_AREA_ORIGIN) ? 1 : 0;
    if (linePtr->areaClose == LINE_AREA_ORIGIN) {
        if (linePtr->core.axes.x->logScale || linePtr->core.axes.y->logScale) {
            return;
        }
        origin = Rbc_Map2D(graphPtr, 0.0, 0.0, &linePtr->core.axes);
        if ((!FINITE(origin.x)) || (!FINITE(origin.y))) {
            return;
        }
    }
    if (mapPtr->nScreenPts > TCL_SIZE_MAX - extraPoints) {
        return;
    }
    inputCount = mapPtr->nScreenPts + extraPoints;
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
        /* Preserve the historical offset only for baseline filling. */
        origPts[i].x = mapPtr->screenPts[i].x + ((linePtr->areaClose == LINE_AREA_BASELINE) ? 1.0 : 0.0);
        origPts[i].y = mapPtr->screenPts[i].y;
        if (origPts[i].y > maxY) {
            maxY = origPts[i].y;
        }
    }
    if (linePtr->areaClose == LINE_AREA_BASELINE) {
        origPts[i].x = origPts[i - 1].x;
        origPts[i].y = maxY;
        i++;
        origPts[i].x = origPts[0].x;
        origPts[i].y = maxY;
    } else if (linePtr->areaClose == LINE_AREA_ORIGIN) {
        origPts[i] = origin;
    }
    n = Rbc_PolyRectClip(&exts, origPts, inputCount, clipPts, clipCapacity);
    ckfree(origPts);
    if (n < 3) {
        ckfree(clipPts);
        return;
    }
    linePtr->fillPts = clipPts;
    linePtr->nFillPts = n;
}

/**
 * @ingroup line_fills
 * @brief Append a clipped uncertainty polygon to an owned list.
 * @param graphPtr Borrowed graph providing current plot extents.
 * @param upper Borrowed upper boundary in source traversal order.
 * @param lower Borrowed lower boundary in source traversal order.
 * @param count Number of samples present in both arrays.
 * @param tailPtr In/out pointer to the list's next insertion link.
 * @details Copies upper forward and lower backward, clips the resulting
 * polygon, and appends an owned node and point array. Updates the insertion
 * link only on success. Fewer than two samples, size overflow, allocation
 * failure or fewer than three clipped vertices leave the list unchanged.
 * Neither input array is retained or modified.
 */
static void AppendErrorBandPolygon(Graph *graphPtr, Point2D *upper, Point2D *lower, Tcl_Size count,
                                   LineBandPolygon ***tailPtr) {
    Point2D *points;
    Point2D *clipped;
    LineBandPolygon *polygonPtr;
    Extents2D exts;
    Tcl_Size n, capacity, i;
    size_t bytes, clipBytes;

    if ((count < 2) || (count > (TCL_SIZE_MAX - 1) / 6)) {
        return;
    }
    n = 2 * count;
    capacity = 3 * n + 1;
    if ((GetLineArrayByteCount(n, sizeof(*points), &bytes) != TCL_OK) ||
        (GetLineArrayByteCount(capacity, sizeof(*clipped), &clipBytes) != TCL_OK)) {
        return;
    }
    points = Tcl_AttemptAlloc(bytes);
    clipped = Tcl_AttemptAlloc(clipBytes);
    polygonPtr = Tcl_AttemptAlloc(sizeof(*polygonPtr));
    if ((points == NULL) || (clipped == NULL) || (polygonPtr == NULL)) {
        if (points != NULL) {
            ckfree(points);
        }
        if (clipped != NULL) {
            ckfree(clipped);
        }
        if (polygonPtr != NULL) {
            ckfree(polygonPtr);
        }
        return;
    }
    for (i = 0; i < count; i++) {
        points[i] = upper[i];
        points[n - i - 1] = lower[i];
    }
    Rbc_GraphExtents(graphPtr, &exts);
    n = Rbc_PolyRectClip(&exts, points, n, clipped, capacity);
    ckfree(points);
    if (n < 3) {
        ckfree(clipped);
        ckfree(polygonPtr);
        return;
    }
    polygonPtr->points = clipped;
    polygonPtr->count = n;
    polygonPtr->nextPtr = NULL;
    **tailPtr = polygonPtr;
    *tailPtr = &polygonPtr->nextPtr;
}

/**
 * @ingroup line_fills
 * @brief Map source error bounds into independently clipped run polygons.
 * @param graphPtr Borrowed graph in the element's mapping viewport.
 * @param linePtr Borrowed line whose old band list has been cleared.
 * @details Uses original paired samples and the selected X or Y error inputs,
 * not reduced center-line points. Symmetric errors take precedence over paired
 * low/high bounds. Invalid values, log-domain violations and trace-direction
 * breaks terminate runs. Step mode inserts X-first boundary corners. Renderer
 * point limits split long runs at shared samples. Temporary boundary arrays
 * are freed; successful polygons remain owned by errorBand.polygons.
 * Disabled/unpainted bands, short input and workspace failure produce no list.
 * Individual polygon failures omit that piece rather than rolling back earlier
 * pieces. Configuration must already have restricted bands to Cartesian axes
 * and linear or step smoothing. No Tcl callbacks are invoked.
 */
static void MapErrorBand(Graph *graphPtr, Line *linePtr) {
    Element *elemPtr = &linePtr->core;
    LineErrorBand *bandPtr = &linePtr->errorBand;
    LineBandPolygon **tailPtr = &bandPtr->polygons;
    int xBand = (bandPtr->mode == LINE_BAND_X);
    ElemVector *errorPtr = xBand ? &elemPtr->xError : &elemPtr->yError;
    ElemVector *lowPtr = xBand ? &elemPtr->xLow : &elemPtr->yLow;
    ElemVector *highPtr = xBand ? &elemPtr->xHigh : &elemPtr->yHigh;
    Point2D *upper, *lower;
    Tcl_Size n, i, count, capacity, pointLimit;
    size_t bytes;
    double previousX = 0.0;
    int step = (linePtr->reqSmooth == PEN_SMOOTH_STEP);

    if (!bandPtr->mode || ((bandPtr->tile == NULL) && (bandPtr->stipple == None))) {
        return;
    }
    n = NumberOfPoints(elemPtr);
    if (errorPtr->nValues > 0) {
        n = MIN(n, errorPtr->nValues);
    } else {
        n = MIN3(n, lowPtr->nValues, highPtr->nValues);
    }
    if ((n < 2) || (n > TCL_SIZE_MAX / 2)) {
        return;
    }
    capacity = step ? n * 2 : n;
    pointLimit = Rbc_RenderPolygonPointLimit(graphPtr);
    if (pointLimit != TCL_SIZE_MAX) {
        /* Clipping may emit six vertices per boundary sample. Split at shared
         * samples, reserving one point for closure, within the output limit. */
        Tcl_Size limit = (pointLimit - 1) / 6;
        if (limit < 3) {
            return;
        }
        capacity = MIN(capacity, limit);
    }
    if (GetLineArrayByteCount(capacity, sizeof(*upper), &bytes) != TCL_OK) {
        return;
    }
    upper = Tcl_AttemptAlloc(bytes);
    lower = Tcl_AttemptAlloc(bytes);
    if ((upper == NULL) || (lower == NULL)) {
        if (upper != NULL) {
            ckfree(upper);
        }
        if (lower != NULL) {
            ckfree(lower);
        }
        return;
    }
    count = 0;
    for (i = 0; i < n; i++) {
        double x = elemPtr->x.valueArr[i];
        double y = elemPtr->y.valueArr[i];
        double high, low;
        Point2D u, l;
        int valid;

        if (errorPtr->nValues > 0) {
            double center = xBand ? x : y;
            high = center + errorPtr->valueArr[i];
            low = center - errorPtr->valueArr[i];
        } else {
            high = highPtr->valueArr[i];
            low = lowPtr->valueArr[i];
        }
        valid = FINITE(x) && FINITE(y) && FINITE(high) && FINITE(low);
        if (valid && (low > high)) {
            double swap = low;
            low = high;
            high = swap;
        }
        if ((elemPtr->axes.x->logScale && ((x <= 0.0) || (xBand && (low <= 0.0)))) ||
            (elemPtr->axes.y->logScale && ((y <= 0.0) || (!xBand && (low <= 0.0))))) {
            valid = FALSE;
        }
        if (valid) {
            if (xBand) {
                u = Rbc_Map2D(graphPtr, high, y, &elemPtr->axes);
                l = Rbc_Map2D(graphPtr, low, y, &elemPtr->axes);
            } else {
                u = Rbc_Map2D(graphPtr, x, high, &elemPtr->axes);
                l = Rbc_Map2D(graphPtr, x, low, &elemPtr->axes);
            }
            valid = FINITE(u.x) && FINITE(u.y) && FINITE(l.x) && FINITE(l.y);
        }
        if (!valid || ((count > 0) && BROKEN_TRACE(linePtr->penDir, previousX, x))) {
            AppendErrorBandPolygon(graphPtr, upper, lower, count, &tailPtr);
            count = 0;
        }
        if (!valid) {
            continue;
        }
        if (count + (step ? 2 : 1) > capacity) {
            AppendErrorBandPolygon(graphPtr, upper, lower, count, &tailPtr);
            upper[0] = upper[count - 1];
            lower[0] = lower[count - 1];
            count = 1;
        }
        if (step && (count > 0)) {
            /* Both boundary paths follow the trace's X-first, then Y
             * convention, even when the uncertainty is along X. */
            upper[count] = upper[count - 1];
            lower[count] = lower[count - 1];
            if (graphPtr->inverted) {
                upper[count].y = u.y;
                lower[count].y = l.y;
            } else {
                upper[count].x = u.x;
                lower[count].x = l.x;
            }
            count++;
        }
        upper[count] = u;
        lower[count] = l;
        count++;
        previousX = x;
    }
    AppendErrorBandPolygon(graphPtr, upper, lower, count, &tailPtr);
    ckfree(upper);
    ckfree(lower);
}

/**
 * @ingroup line_geometry_lifecycle
 * @brief Discard mapped geometry before rebuilding a line.
 *
 * Releases area/error-band polygons, traces, symbols, strips, explicit
 * active geometry and X/Y error-bar arrays with their source-index maps.
 * Clears palette counts, array pointers, geometry counts and pointMapOmitted.
 * Explicit active indices request remapping through ACTIVE_PENDING.
 *
 * Retains source data, pens, activation indices and the source decimation
 * cache. This is a geometry reset, not complete element teardown.
 */
static void ResetLine(Line *linePtr) {
    LineBandPolygon *polygonPtr;

    while ((polygonPtr = linePtr->errorBand.polygons) != NULL) {
        linePtr->errorBand.polygons = polygonPtr->nextPtr;
        ckfree(polygonPtr->points);
        ckfree(polygonPtr);
    }
    /*
     * Discard the old fill even when MapLine returns early or skips
     * MapFillArea because fewer than two source points remain.
     */
    if (linePtr->fillPts != NULL) {
        ckfree(linePtr->fillPts);
        linePtr->fillPts = NULL;
    }
    linePtr->nFillPts = 0;
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
    if (linePtr->core.nActiveIndices > 0) {
        linePtr->core.flags |= ACTIVE_PENDING;
    }
    linePtr->nSymbolPts = 0;
    linePtr->nStrips = 0;
    linePtr->pointMapOmitted = FALSE;    
    linePtr->core.xErrorBarCnt = 0;
    linePtr->core.yErrorBarCnt = 0;
}

/**
 * @ingroup line_geometry_lifecycle
 * @brief Compact error segments and source indices in place.
 *
 * Keeps segments whose source index is selected, preserving order and
 * parallel indices, and returns the retained count. All parts of a selected
 * bar, including caps, survive. The caller supplies valid source indices
 * and selection storage; this helper does not check bounds or allocate.
 */
static Tcl_Size FilterErrorBarSegments(Segment2D *segments, Tcl_Size *indices, Tcl_Size count,
                                       const unsigned char *selected) {
    Tcl_Size i, kept = 0;

    for (i = 0; i < count; i++) {
        if (selected[indices[i]]) {
            segments[kept] = segments[i];
            indices[kept++] = indices[i];
        }
    }
    return kept;
}

/**
 * @ingroup line_geometry_lifecycle
 * @brief Match retained error bars to the symbol selection.
 *
 * Runs before palette geometry is reordered by MergePens. When symbol
 * limits or reduced-symbol mode require filtering, builds a source-index
 * selection using palette traversal and the shared symbol interval counter.
 * Compacts X/Y error-bar segments with their parallel index arrays.
 *
 * Borrows a valid data-to-style map. Allocates temporary selection storage
 * with checked byte count and ckalloc, then frees it. Does not resize the
 * segment allocations, change source data, or regenerate geometry.
 */
static void FilterLineErrorBars(Line *linePtr, PenStyle **dataToStyle) {
    Element *elemPtr = &linePtr->core;
    unsigned char *selected;
    size_t bytes;
    Tcl_Size i, counter = 0, interval = 0;
    Rbc_ChainLink *linkPtr;

    if (((elemPtr->xErrorBarCnt == 0) && (elemPtr->yErrorBarCnt == 0)) ||
        ((linePtr->reqMaxSymbols <= 0) &&
         ((linePtr->symbolPoints != LINE_SYMBOL_POINTS_REDUCED) || (linePtr->rTolerance <= 0.0)))) {
        return;
    }
    if (GetLineArrayByteCount(NumberOfPoints(elemPtr), sizeof(*selected), &bytes) != TCL_OK) {
        return;
    }
    selected = ckalloc(bytes);
    memset(selected, 0, bytes);
    if (linePtr->reqMaxSymbols > 0) {
        interval = linePtr->nSymbolPts / (Tcl_Size)linePtr->reqMaxSymbols;
    }
    for (linkPtr = Rbc_ChainFirstLink(elemPtr->palette); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        PenStyle *stylePtr = Rbc_ChainGetValue(linkPtr);

        for (i = 0; i < linePtr->nSymbolPts; i++) {
            Tcl_Size index = linePtr->symbolToData[i];

            if (dataToStyle[index] == stylePtr) {
                if ((interval <= 1) || ((counter % interval) == 0)) {
                    selected[index] = 1;
                }
                counter++;
            }
        }
    }
    elemPtr->xErrorBarCnt = FilterErrorBarSegments(elemPtr->xErrorBars, elemPtr->xErrorToData,
                                                  elemPtr->xErrorBarCnt, selected);
    elemPtr->yErrorBarCnt = FilterErrorBarSegments(elemPtr->yErrorBars, elemPtr->yErrorToData,
                                                  elemPtr->yErrorBarCnt, selected);
    ckfree(selected);
}

/**
 * @ingroup line_mapping_pipeline
 * @brief Rebuild line-family drawing geometry in the current graph viewport.
 *
 * Borrows the element and graph with axes/viewports prepared by the caller.
 * Refreshes or invalidates pending source-decimation summaries, consumes
 * change metadata and resets old mapped geometry before early exits.
 * Builds the error band, chooses cached/pre-map/full mapping, retains symbol
 * centers, then applies smoothing, density/tolerance reduction and area fill.
 * Dispatches strip segments versus connected traces by element class.
 *
 * Frees temporary map storage after geometry copies are made. The restricted
 * pre-map path marks pointMapOmitted and attaches the single palette style
 * without a source-sized style map. The general path computes style sizes,
 * maps eligible XY error bars, filters them and finally merges palette slices.
 * Individual helpers define allocation fallbacks; this void operation is not
 * an all-or-nothing transaction and does not emit Tcl diagnostics.
 */
static void MapLine(Graph *graphPtr, Element *elemPtr) {
    Line *linePtr = LINE_FROM_CORE(elemPtr);
    MapInfo mapInfo = {0};
    Tcl_Size nPoints;
    int size;
    int preMapDecimated;
    int pixelDecimation;    
    PenStyle **dataToStyle;
    Rbc_ChainLink *linkPtr;
    LinePenStyle *stylePtr;

    if (elemPtr->flags & MAP_ITEM) {
        /*
         * Preserve the persistent Stage-3 cache across changes that do
         * not affect its source-domain summaries.
         *
         * A precisely ranged Y-only vector update can refresh just the
         * affected 64-sample blocks.  Anything less certain falls back
         * to complete invalidation.
         */
        if (!RefreshLineDecimateCache(linePtr)) {
            InvalidateLineDecimateCache(linePtr);
        }
    }
    /*
     * The pending vector-change metadata has now been consumed,
     * regardless of whether the cache survived or was invalidated.
     */
    ClearLineVectorChanges(linePtr);
    ResetLine(linePtr);
    nPoints = NumberOfPoints(&linePtr->core);
    if (nPoints < 1) {
        return;
    }
    MapErrorBand(graphPtr, linePtr);
    preMapDecimated = FALSE;
    /*
     * Separate semantic eligibility from the runtime density decision.
     *
     * In particular, do not build the persistent decimation cache for a
     * low-density line that is cheaper to map and draw normally.
     */
    pixelDecimation = CanPixelDecimateLine(linePtr) && LineNeedsPixelDecimation(graphPtr, linePtr);
    if (pixelDecimation && CanPreMapDecimateLine(linePtr)) {
        preMapDecimated = BuildCachedPixelDecimatedScreenPoints(graphPtr, linePtr, &mapInfo);
        if (!preMapDecimated) {
            /*
             * Unsupported cached cases—non-monotonic X,
             * discontinuities, log-domain breaks, etc.—retain
             * the Stage-2 implementation.
             */
            preMapDecimated = BuildPixelDecimatedScreenPoints(graphPtr, linePtr, &mapInfo);
        }
    }
    if (!preMapDecimated) {
        GetScreenPoints(graphPtr, linePtr, &mapInfo);
    }
    if (mapInfo.nScreenPts < 1) {
        return;
    }
    if (preMapDecimated) {
        /*
         * Full symbolPts/symbolToData arrays were intentionally
         * omitted.  Exact point searches will map source points on
         * demand.
         */
        linePtr->pointMapOmitted = TRUE;
    } else {
        MapSymbols(graphPtr, linePtr, &mapInfo);
    }
    if ((linePtr->core.flags & ACTIVE_PENDING) && (linePtr->core.nActiveIndices > 0)) {
        MapActiveSymbols(graphPtr, linePtr);
    }
    /*
     * Map connecting line segments if they are to be displayed.
     */
    if ((nPoints > 1) && ((linePtr->core.classUid == rbcStripElementUid) || (linePtr->builtinPen.traceWidth > 0) ||
                          (linePtr->fillTile != NULL) || (linePtr->fillStipple != None) ||
                          ((linePtr->symbolPoints == LINE_SYMBOL_POINTS_REDUCED) && (linePtr->rTolerance > 0.0)))) {
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
            if (mapInfo.nScreenPts < 3) {
                linePtr->smooth = PEN_SMOOTH_NONE;
            } else if (mapInfo.breakBefore != NULL) {
                GenerateSplineRuns(graphPtr, linePtr, &mapInfo, linePtr->core.classUid == rbcPolarElementUid);
            } else if (linePtr->core.classUid == rbcPolarElementUid) {
                GenerateParametricSpline(graphPtr, linePtr, &mapInfo);
            } else {
                GenerateSpline(graphPtr, linePtr, &mapInfo);
            }
            break;
        case PEN_SMOOTH_CATROM:
            if (mapInfo.nScreenPts < 3) {
                linePtr->smooth = PEN_SMOOTH_NONE;
            } else if (mapInfo.breakBefore != NULL) {
                GenerateSplineRuns(graphPtr, linePtr, &mapInfo, TRUE);
            } else {
                GenerateParametricSpline(graphPtr, linePtr, &mapInfo);
            }
            break;
        default:
            break;
        }
        /*
         * The pre-map path already produced pixel-density geometry.
         */
        if (!preMapDecimated && pixelDecimation) {
            DecimatePointsByPixels(graphPtr, &mapInfo);
        }
        if (linePtr->rTolerance > 0.0) {
            ReducePoints(&mapInfo, linePtr->rTolerance);
            if (linePtr->symbolPoints == LINE_SYMBOL_POINTS_REDUCED) {
                FilterReducedSymbols(linePtr, &mapInfo);
            }
        }
        if ((linePtr->fillTile != NULL) || (linePtr->fillStipple != None)) {
            MapFillArea(graphPtr, linePtr, &mapInfo);
        }
        /*
         * Trackgraphs can mix line and strip elements.  Mapping and closest
         * searches must select the same representation for each element.
         */
        if (linePtr->core.classUid == rbcStripElementUid) {
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
    /*
     * The pre-map path is deliberately restricted to a single trace pen
     * with no symbols, values, styles, or error bars.
     *
     * Avoid Rbc_StyleMap(): for a multi-million-point waveform that array
     * alone would otherwise contain one pointer per source point.
     */
    if (preMapDecimated) {
        /* The fast path permits only one pen. Attach its mapped strip segments
         * without constructing the source-sized style map. Trace drawing reads
         * the trace chain directly, but strip drawing reads this pen slice. */
        MergePens(linePtr, NULL);
        return;
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
    if ((linePtr->dataMode == LINE_DATA_XY) &&
        (((linePtr->core.yHigh.nValues > 0) && (linePtr->core.yLow.nValues > 0)) ||
         ((linePtr->core.xHigh.nValues > 0) && (linePtr->core.xLow.nValues > 0)) ||
         (linePtr->core.xError.nValues > 0) || (linePtr->core.yError.nValues > 0))) {
        Rbc_MapErrorBars(graphPtr, &linePtr->core, dataToStyle);
    }
    FilterLineErrorBars(linePtr, dataToStyle);
    MergePens(linePtr, dataToStyle);
    ckfree((char *)dataToStyle);
}

/**
 * @ingroup line_picking_rendering
 * @brief Measure Euclidean distance from a pixel query to a segment.
 *
 * Delegates to Rbc_GetClosestPointOnSegment using a double-precision query.
 * Borrows endpoints and writes the nearest segment point to t. Returns the
 * pixel distance, including endpoint clamping, without modifying endpoints.
 */
static double DistanceToLine(int x, int y, Point2D *p, Point2D *q, Point2D *t) {
    Point2D sample;

    sample.x = (double)x;
    sample.y = (double)y;
    return Rbc_GetClosestPointOnSegment(&sample, p, q, t);
}

/**
 * @ingroup line_picking_rendering
 * @brief Intersect a segment with the query screen X coordinate.
 *
 * Returns DBL_MAX without writing t when X is outside the endpoint span.
 * Otherwise writes the intersection and returns its absolute screen Y
 * distance from the query. Near-vertical segments choose the nearer endpoint
 * in Y, with ties choosing q; near-horizontal segments use their constant Y.
 * This is a trace intersection metric, not point-search X distance.
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

/**
 * @ingroup line_picking_rendering
 * @brief Intersect a segment with the query screen Y coordinate.
 *
 * Returns DBL_MAX without writing t when Y is outside the endpoint span.
 * Otherwise writes the intersection and returns its absolute screen X
 * distance from the query. Near-horizontal segments choose the nearer endpoint
 * in X, with ties choosing q; near-vertical segments use their constant X.
 * Borrows endpoints and does not allocate.
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

/**
 * @ingroup line_picking_rendering
 * @brief Search connected mapped traces for a strict distance improvement.
 *
 * Calls the supplied script-free distance function on each consecutive pair.
 * On improvement, stores element, distance, the segment start's source index
 * and the inverse-mapped closest coordinate in the borrowed search record.
 * Returns TRUE only if it improves the incoming distance; ties retain the
 * previous candidate. Requires current trace geometry and prepared axes.
 */
static int ClosestTrace(Graph *graphPtr, Line *linePtr, ClosestSearch *searchPtr, DistanceProc *distProc) {
    Rbc_ChainLink *linkPtr;
    Point2D closest, b;
    LineTrace *tracePtr;
    double dist, minDist;
    Point2D *pointPtr, *endPtr;
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

/**
 * @ingroup line_picking_rendering
 * @brief Search independent mapped segments for a closer interpolated hit.
 *
 * Uses the supplied distance callback and the parallel stripToData array.
 * Commits a strict improvement to the search record, inverse-mapping the hit
 * coordinate and reporting the segment's retained source index. Returns FALSE
 * without changing the record if no segment improves it. Borrows geometry.
 */
static int ClosestStrip(Graph *graphPtr, Line *linePtr, ClosestSearch *searchPtr, DistanceProc *distProc) {
    Point2D closest, b;
    double dist, minDist;
    Tcl_Size count;
    Segment2D *s;
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

/**
 * @ingroup line_picking_rendering
 * @brief Search retained original centers inside the plot rectangle.
 *
 * Ignores centers outside unpadded graph extents even if part of their symbol
 * could be visible. SEARCH_BOTH uses Euclidean distance; SEARCH_X and SEARCH_Y
 * use absolute screen X and Y distances respectively. Strict improvements
 * retain source indices and recover data coordinates from the source adapter.
 * Requires current mapped arrays and source data; does not synchronize them.
 */
static void ClosestMappedPoint(Line *linePtr, ClosestSearch *searchPtr) {
    Point2D *pointPtr;
    double minDist;
    Extents2D exts;    
    Tcl_Size count;
    Tcl_Size dataIndex;

    Rbc_GraphExtents(linePtr->core.graphPtr, &exts);    
    minDist = searchPtr->dist;
    dataIndex = -1;
    pointPtr = linePtr->symbolPts;
    for (count = 0; count < linePtr->nSymbolPts; count++, pointPtr++) {
        double dx;
        double dy;
        double dist;

        if (!PointInRegion(&exts, pointPtr->x, pointPtr->y)) {
            continue;
        }        
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
        GetLineDataPoint(linePtr, dataIndex, &searchPtr->point.x, &searchPtr->point.y);
    }
}

/**
 * @ingroup line_picking_rendering
 * @brief Search original samples when mapped centers were omitted.
 *
 * Maps finite source samples on demand and applies the same viewport and
 * point-distance rules as ClosestMappedPoint. May build/use the decimation
 * cache to restrict the source interval, then commits only a strict improvement.
 * Reads source data without invoking Tcl callbacks.
 *
 * Candidate pruning uses the screen coordinate onto which data X maps:
 * SEARCH_X normally, SEARCH_Y when inverted, or SEARCH_BOTH in either
 * orientation. Other directional queries must search the full source range.
 */
static void ClosestSourcePoint(Graph *graphPtr, Line *linePtr, ClosestSearch *searchPtr) {
    Extents2D exts;
    Tcl_Size nPoints;
    Tcl_Size first;
    Tcl_Size last;
    Tcl_Size dataIndex;
    Tcl_Size i;
    double minDist;

    Rbc_GraphExtents(graphPtr, &exts);
    nPoints = NumberOfPoints(&linePtr->core);
    minDist = searchPtr->dist;
    dataIndex = -1;
    first = 0;
    last = nPoints;
    /*
     * Prune by source X only when its screen coordinate contributes to
     * the distance metric. Data X maps to screen X normally and screen Y
     * when inverted. The other directional metric ignores that coordinate
     * and must retain the full source range.
     *
     * When the cache is valid, binary search finds the candidate interval
     * that can improve the current distance.
     */
    if (((searchPtr->along == SEARCH_BOTH) ||
         (searchPtr->along == (graphPtr->inverted ? SEARCH_Y : SEARCH_X))) &&
        EnsureLineDecimateCache(linePtr)) {
        double queryAbscissa;
        Tcl_Size candidateFirst;
        Tcl_Size candidateLast;

        if (graphPtr->inverted) {
            queryAbscissa = (double)searchPtr->y;
        } else {
            queryAbscissa = (double)searchPtr->x;
        }
        if (GetCachedSourceRangeForAbscissa(graphPtr, linePtr, queryAbscissa - minDist, queryAbscissa + minDist,
                                            &candidateFirst, &candidateLast)) {
            first = candidateFirst;
            last = candidateLast;
        }
    }
    for (i = first; i < last; i++) {
        Point2D point;
        double x;
        double y;
        double dx;
        double dy;
        double dist;

        if (!GetLineDataPoint(linePtr, i, &x, &y)) {
            continue;
        }
        if ((!FINITE(x)) || (!FINITE(y))) {
            continue;
        }
        point = Rbc_Map2D(graphPtr, x, y, &linePtr->core.axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        /*
         * Match ClosestMappedPoint exactly: point searches considering only centers inside the original graph extents
         */
        if (!PointInRegion(&exts, point.x, point.y)) {
            continue;
        }
        dx = fabs((double)searchPtr->x - point.x);
        dy = fabs((double)searchPtr->y - point.y);
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
            minDist = dist;
            dataIndex = i;
        }
    }
    if ((dataIndex >= 0) && (minDist < searchPtr->dist)) {
        searchPtr->elemPtr = &linePtr->core;
        searchPtr->dist = minDist;
        searchPtr->index = dataIndex;
        GetLineDataPoint(linePtr, dataIndex, &searchPtr->point.x, &searchPtr->point.y);
    }
}

/**
 * @ingroup line_picking_rendering
 * @brief Dispatch exact point picking according to map completeness.
 *
 * Uses retained mapped centers unless pointMapOmitted requests an original
 * source search. Borrows all inputs and updates the search record only through
 * the selected helper. The source path may allocate a decimation cache.
 */
static void ClosestPoint(Graph *graphPtr, Line *linePtr, ClosestSearch *searchPtr) {
    if (!linePtr->pointMapOmitted) {
        ClosestMappedPoint(linePtr, searchPtr);
        return;
    }
    ClosestSourcePoint(graphPtr, linePtr, searchPtr);
}

/**
 * @ingroup line_mapping_pipeline
 * @brief Compute source-domain bounds for axis ranging.
 *
 * Initializes empty sentinel extents. Complex data or polar-coordinate input
 * is converted pointwise to Cartesian coordinates and enclosed in a symmetric
 * square using the largest finite-point radius; radius overflow clamps to
 * DBL_MAX. No valid points retain the empty sentinel bounds.
 *
 * Ordinary XY input uses vector extrema, finding a positive minimum when a
 * log axis requires it, and expands the result for error bars. Does not use
 * smoothed or decimated screen geometry, allocate, or synchronize source data.
 */
static void GetLineExtents(Element *elemPtr, Extents2D *extsPtr) {
    Line *linePtr;
    Tcl_Size nPoints;

    linePtr = LINE_FROM_CORE(elemPtr);
    extsPtr->top = extsPtr->left = DBL_MAX;
    extsPtr->bottom = extsPtr->right = -DBL_MAX;
    nPoints = NumberOfPoints(elemPtr);
    if (nPoints < 1) {
        return;
    }
    if ((linePtr->dataMode == LINE_DATA_COMPLEX) || (linePtr->dataCoordinates == LINE_COORDINATES_POLAR)) {
        double radius;
        Tcl_Size i;
        int found;

        radius = 0.0;
        found = FALSE;
        for (i = 0; i < nPoints; i++) {
            double x;
            double y;
            double r;

            if (!GetLineDataPoint(linePtr, i, &x, &y)) {
                continue;
            }
            if ((!FINITE(x)) || (!FINITE(y))) {
                continue;
            }
            r = hypot(x, y);
            if (!FINITE(r)) {
                r = DBL_MAX;
            }
            if ((!found) || (r > radius)) {
                radius = r;
                found = TRUE;
            }
        }
        if (!found) {
            return;
        }
        extsPtr->left = -radius;
        extsPtr->right = radius;
        extsPtr->top = -radius;
        extsPtr->bottom = radius;
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

/**
 * @ingroup line_option_transactions
 * @brief Schedule line redisplay when a retained area or band tile changes.
 *
 * clientData is the stable owning Line; tile is unused. If the graph window
 * exists, sets REDRAW_WORLD and REDRAW_BACKING_STORE and queues redraw. Does not
 * filter hidden elements, remap source data or synchronously draw. Resource
 * teardown must detach tile clients before their owner storage is released.
 */
static void TileChangedProc(ClientData clientData, Rbc_Tile tile) {
    Line *linePtr = clientData;
    Graph *graphPtr;

    graphPtr = linePtr->core.graphPtr;
    if (graphPtr->tkwin != NULL) {
        graphPtr->flags |= REDRAW_WORLD | REDRAW_BACKING_STORE;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
}

/**
 * @ingroup line_configuration_smoothing
 * @brief Prepare and commit line-family derived configuration state.
 *
 * Borrows a live element with Tk option candidates already installed and the
 * changed-option mask/argument order available. Validates opacity and data-mode
 * restrictions; stages common data, complex data, parameters, pens, axes, state,
 * tags, styles, scalars and fill resources as required. Validates prospective
 * parameter lengths and error-band smoothing, then configures the built-in pen.
 *
 * Validates polar reference impedance before configuring the embedded pen
 * or transferring staged state. Commits non-data transactions, synchronizes
 * the first palette pen, then commits staged data. Successful completion
 * sets mapping flags. The enclosing caller restores Tk options on error.
 *
 * Error cleanup releases still-owned staged resources and returns TCL_ERROR.
 * It does not undo already committed derived state, restore Tk options itself,
 * or make application callbacks during parsing generally reentrant.
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
    LineAreaTransaction bandTransaction;
    LineComplexDataTransaction complexDataTransaction;
    LineParamTransaction paramTransaction;
    Rbc_ChainLink *linkPtr;
    int dataTransactionPrepared;
    int penTransactionPrepared;
    int axisTransactionPrepared;
    int stateTransactionPrepared;
    int tagsTransactionPrepared;
    int stylesTransactionPrepared;
    int scalarTransactionPrepared;
    int areaTransactionPrepared;
    int bandTransactionPrepared;
    int complexDataTransactionPrepared;
    int paramTransactionPrepared;    

    linePtr = LINE_FROM_CORE(elemPtr);
    if ((!FINITE(linePtr->areaOpacity)) || (linePtr->areaOpacity < 0.0) || (linePtr->areaOpacity > 1.0)) {
        Tcl_SetObjResult(graphPtr->interp,
                         Tcl_NewStringObj("-areaopacity must be a finite number between 0 and 1", -1));
        return TCL_ERROR;
    }
    if ((!FINITE(linePtr->errorBand.opacity)) || (linePtr->errorBand.opacity < 0.0) ||
        (linePtr->errorBand.opacity > 1.0)) {
        Tcl_SetObjResult(graphPtr->interp,
                         Tcl_NewStringObj("-errorbandopacity must be a finite number between 0 and 1", -1));
        return TCL_ERROR;
    }
    if (linePtr->errorBand.mode && (graphPtr->classUid == rbcPolarElementUid)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("error bands require Cartesian axes", -1));
        return TCL_ERROR;
    }
    memset(&dataTransaction, 0, sizeof(dataTransaction));
    memset(&penTransaction, 0, sizeof(penTransaction));
    memset(&axisTransaction, 0, sizeof(axisTransaction));
    memset(&stateTransaction, 0, sizeof(stateTransaction));
    memset(&tagsTransaction, 0, sizeof(tagsTransaction));
    memset(&stylesTransaction, 0, sizeof(stylesTransaction));
    memset(&scalarTransaction, 0, sizeof(scalarTransaction));
    memset(&areaTransaction, 0, sizeof(areaTransaction));
    memset(&bandTransaction, 0, sizeof(bandTransaction));
    memset(&complexDataTransaction, 0, sizeof(complexDataTransaction));
    memset(&paramTransaction, 0, sizeof(paramTransaction));
    dataTransactionPrepared = FALSE;
    penTransactionPrepared = FALSE;
    axisTransactionPrepared = FALSE;
    stateTransactionPrepared = FALSE;
    tagsTransactionPrepared = FALSE;
    stylesTransactionPrepared = FALSE;
    scalarTransactionPrepared = FALSE;
    areaTransactionPrepared = FALSE;
    bandTransactionPrepared = FALSE;
    complexDataTransactionPrepared = FALSE;
    paramTransactionPrepared = FALSE;
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
    if ((elemPtr->classUid == rbcPolarElementUid) &&
        ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & (LINE_ELEM_DATA_MASK | LINE_ELEM_CDATA_MASK)))) {
        if (PrepareLineComplexDataTransaction(graphPtr, elemPtr, linePtr, &complexDataTransaction) != TCL_OK) {
            goto error;
        }
        complexDataTransactionPrepared = TRUE;
    }
    if (linePtr->dataCoordinates == LINE_COORDINATES_POLAR) {
        LineDataMode mode;
        Tcl_Obj *errorObjects[] = {elemPtr->xErrorObjPtr, elemPtr->yErrorObjPtr, elemPtr->xHighObjPtr,
                                  elemPtr->xLowObjPtr, elemPtr->yHighObjPtr, elemPtr->yLowObjPtr};
        size_t i;

        mode = complexDataTransactionPrepared ? complexDataTransaction.mode : linePtr->dataMode;
        if (mode == LINE_DATA_COMPLEX) {
            Tcl_SetObjResult(graphPtr->interp,
                             Tcl_NewStringObj("-datacoordinates polar cannot be used with -cdata", -1));
            goto error;
        }
        for (i = 0; i < sizeof(errorObjects) / sizeof(errorObjects[0]); i++) {
            if ((errorObjects[i] != NULL) && (Tcl_GetCharLength(errorObjects[i]) > 0)) {
                Tcl_SetObjResult(graphPtr->interp,
                                 Tcl_NewStringObj("-datacoordinates polar requires empty error-bar data options", -1));
                goto error;
            }
        }
    }
    /*
     * -param is available for graph line and Polar elements, but not
     * strip elements.
     */
    if (((elemPtr->classUid == rbcLineElementUid) || (elemPtr->classUid == rbcPolarElementUid)) &&
        ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_PARAM_MASK))) {
        if (PrepareLineParamTransaction(graphPtr, elemPtr, linePtr, &paramTransaction) != TCL_OK) {
            goto error;
        }
        paramTransactionPrepared = TRUE;
    }
    /*
     * Validate the prospective complete data/parameter state before any
     * transaction is committed.
     */
    if (((elemPtr->classUid == rbcLineElementUid) || (elemPtr->classUid == rbcPolarElementUid)) &&
        LineParamLengthValidationNeeded(elemPtr)) {
        if (ValidateLineParamLength(graphPtr, linePtr, dataTransactionPrepared ? &dataTransaction : NULL,
                                    complexDataTransactionPrepared ? &complexDataTransaction : NULL,
                                    paramTransactionPrepared ? &paramTransaction : NULL) != TCL_OK) {
            goto error;
        }
    }
    /*
     * Resolve named active and normal pens before modifying the live
     * element. Line, strip, and polar elements all require line-type pens.
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
     * Line, strip, and polar elements all use line pens and LinePenStyle palette
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
    if (linePtr->errorBand.mode) {
        Smoothing smooth = (scalarTransaction.stagedMask & LINE_SCALAR_OPTION_MASK(LINE_SCALAR_OPTION_SMOOTH))
            ? scalarTransaction.smooth : linePtr->reqSmooth;

        if ((smooth != PEN_SMOOTH_NONE) && (smooth != PEN_SMOOTH_STEP)) {
            Tcl_SetObjResult(graphPtr->interp,
                             Tcl_NewStringObj("error bands support only linear or step smoothing", -1));
            goto error;
        }
    }
    if ((elemPtr->classUid != rbcPolarElementUid) &&
        ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_ERROR_BAND_MASK))) {
        if (PrepareLineAreaTransaction(graphPtr, elemPtr, linePtr, &bandTransaction, TRUE) != TCL_OK) {
            goto error;
        }
        bandTransactionPrepared = TRUE;
    }
    /*
     * Resolve line-area resources and construct the replacement fill GC
     * before modifying the live element.
     *
     * Strip elements have no area options, but initial modern
     * configuration may still construct their empty fill GC.
     */
    if (((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_AREA_MASK))) {
        if (PrepareLineAreaTransaction(graphPtr, elemPtr, linePtr, &areaTransaction, FALSE) != TCL_OK) {
            goto error;
        }
        areaTransactionPrepared = TRUE;
    }
    /*
     * Validate reference impedance before replacing the embedded pen or
     * committing any staged derived resources. Tk rollback cannot undo
     * those transfers.
     */
    if ((elemPtr->classUid == rbcPolarElementUid) &&
        ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & LINE_ELEM_Z0_MASK))) {
        if ((!FINITE(linePtr->z0)) || (linePtr->z0 <= 0.0)) {
            Tcl_SetObjResult(
                graphPtr->interp,
                Tcl_ObjPrintf("bad reference impedance \"%g\": must be a positive finite value", linePtr->z0));
            goto error;
        }
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
        CommitLineAreaTransaction(graphPtr, linePtr, &areaTransaction, FALSE);
    }
    if (bandTransactionPrepared) {
        CommitLineAreaTransaction(graphPtr, linePtr, &bandTransaction, TRUE);
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
    if (complexDataTransactionPrepared) {
        CommitLineComplexDataTransaction(linePtr, &complexDataTransaction);
    }
    if (paramTransactionPrepared) {
        CommitLineParamTransaction(linePtr, &paramTransaction);
    }
    if (!elemPtr->optionsConfigured || (elemPtr->optionMask & LINE_ELEM_SCALE_SYMBOL_MASK)) {
        elemPtr->flags |= MAP_ITEM | SCALE_SYMBOL;
    }
    if (!elemPtr->optionsConfigured || (elemPtr->optionMask & LINE_ELEM_MAP_ITEM_MASK)) {
        elemPtr->flags |= MAP_ITEM;
    }
    return TCL_OK;

error:
    if (bandTransactionPrepared) {
        FreeLineAreaTransaction(graphPtr, &bandTransaction);
    }
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
    if (complexDataTransactionPrepared) {
        FreeLineComplexDataTransaction(&complexDataTransaction);
    }
    if (paramTransactionPrepared) {
        FreeLineParamTransaction(&paramTransaction);
    }
    return TCL_ERROR;
}

/**
 * @ingroup line_picking_rendering
 * @brief Select point or interpolated picking for a line-family element.
 *
 * SEARCH_AUTO selects traces only with more than one point and positive
 * normal-pen trace width. Point mode uses ClosestPoint; trace mode chooses
 * an intersection or Euclidean metric and dispatches strip versus connected
 * geometry by element class. Directional trace searches fall back to points
 * when no segment improves the incoming result; SEARCH_BOTH does not.
 * Requires current geometry and the owning graph/track viewport.
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
        ClosestPoint(graphPtr, linePtr, searchPtr);
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
            ClosestPoint(graphPtr, linePtr, searchPtr);
        }
    }
}

/**
 * @ingroup line_picking_rendering
 * @brief Append a safe source-index parameter value when available.
 *
 * Omits param without error while a named parameter vector has pending
 * notification, lengths differ, storage is absent, or the hit index is invalid.
 * Otherwise inserts the value at the reported source index, even for an
 * interpolated geometric hit. Does not interpolate the parameter, synchronize
 * the vector, or publish interpreter variables. Returns dictionary status.
 */
static int AppendLineParamClosestInfo(Graph *graphPtr, Line *linePtr, const ClosestSearch *searchPtr,
                                      Tcl_Obj *resultObjPtr) {
    ElemVector *paramPtr;
    Tcl_Size nPoints;

    paramPtr = &linePtr->param;
    /*
     * A named parameter vector may have changed but not yet delivered
     * its notification.  Never inspect a potentially stale valueArr.
     */
    if ((paramPtr->clientId != NULL) && Rbc_VectorNotifyPending(paramPtr->clientId)) {
        return TCL_OK;
    }
    nPoints = NumberOfPoints(&linePtr->core);
    /*
     * Independent external-vector resizing is permitted.  While the
     * lengths disagree, simply omit parameter information.
     */
    if ((nPoints <= 0) || (paramPtr->nValues != nPoints) || (paramPtr->valueArr == NULL)) {
        return TCL_OK;
    }
    if ((searchPtr->index < 0) || (searchPtr->index >= paramPtr->nValues)) {
        return TCL_OK;
    }
    /*
     * For interpolated closest searches, param follows the same source
     * index reported by info(index).  It is intentionally not a
     * geometrically interpolated parameter value.
     */
    return SetClosestInfoDouble(graphPtr->interp, resultObjPtr, "param", paramPtr->valueArr[searchPtr->index]);
}

/**
 * @ingroup line_picking_rendering
 * @brief Collect optional line/strip parameter metadata.
 *
 * Delegates to AppendLineParamClosestInfo using the borrowed hit record and
 * writable result dictionary. No array traces run during this collection phase.
 * The common closest handler owns later publication and dictionary lifetime.
 */
static int LineClosestInfo(Graph *graphPtr, Element *elemPtr, const ClosestSearch *searchPtr, Tcl_Obj *resultObjPtr) {
    return AppendLineParamClosestInfo(graphPtr, LINE_FROM_CORE(elemPtr), searchPtr, resultObjPtr);
}

/**
 * @ingroup line_picking_rendering
 * @brief Collect polar or Smith metadata from the plotted hit coordinate.
 *
 * Appends optional source-index param first. Finite Cartesian hit coordinates
 * produce radius, a nonnegative radian angle normalized below a full turn,
 * and source dataFormat. A zero radius produces zero angle. Nonfinite hit
 * coordinates omit these fields without error.
 *
 * Smith representation additionally inserts gamma and z0, then normalized
 * and physical impedance/admittance when their conversion succeeds. Impedance
 * scales by z0 and admittance divides by z0. Metadata describes the geometric
 * hit, except param which follows the source index. Returns the first Tcl
 * dictionary error without rolling back preceding fields. Does not publish
 * variables or invoke application callbacks.
 */
static int PolarClosestInfo(Graph *graphPtr, Element *elemPtr, const ClosestSearch *searchPtr, Tcl_Obj *resultObjPtr) {
    Line *linePtr;
    Tcl_Interp *interp;
    double real;
    double imag;
    double radius;
    double angle;

    linePtr = LINE_FROM_CORE(elemPtr);
    interp = graphPtr->interp;
    if (AppendLineParamClosestInfo(graphPtr, linePtr, searchPtr, resultObjPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * search.point is always the final plotted Cartesian coordinate.
     *
     * For Polar:
     *
     *     real = x
     *     imag = y
     *
     * For Smith:
     *
     *     real = Re(Gamma)
     *     imag = Im(Gamma)
     */
    real = searchPtr->point.x;
    imag = searchPtr->point.y;
    if ((!FINITE(real)) || (!FINITE(imag))) {
        return TCL_OK;
    }
    radius = hypot(real, imag);
    if (radius == 0.0) {
        angle = 0.0;
    } else {
        angle = atan2(imag, real);
        if (angle < 0.0) {
            angle += 2.0 * LINE_PI;
        }
        /*
         * Normalize negative zero and rounded full turns.
         */
        if ((angle == 0.0) || (angle >= 2.0 * LINE_PI)) {
            angle = 0.0;
        }
    }
    if (SetClosestInfoDouble(interp, resultObjPtr, "radius", radius) != TCL_OK) {
        return TCL_ERROR;
    }
    if (SetClosestInfoDouble(interp, resultObjPtr, "angle", angle) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Report the representation of the element's source.  This does
     * not alter the meaning of x/y above.
     */
    if (linePtr->dataMode == LINE_DATA_COMPLEX) {
        if (SetClosestInfoString(interp, resultObjPtr, "dataFormat",
                                 lineComplexDataFormatNames[linePtr->cDataFormat]) != TCL_OK) {
            return TCL_ERROR;
        }
    } else {
        if (SetClosestInfoString(interp, resultObjPtr, "dataFormat",
                                 (linePtr->dataCoordinates == LINE_COORDINATES_POLAR) ? "polar" : "xy") != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /*
     * Polar representation stops here.  In Smith representation the
     * plotted Cartesian point is Gamma, from which all impedance and
     * admittance quantities can be recovered.
     */
    if (graphPtr->representation != POLAR_REPRESENTATION_SMITH) {
        return TCL_OK;
    }
    if (SetClosestInfoPair(interp, resultObjPtr, "gamma", real, imag) != TCL_OK) {
        return TCL_ERROR;
    }
    if (SetClosestInfoDouble(interp, resultObjPtr, "z0", linePtr->z0) != TCL_OK) {
        return TCL_ERROR;
    }
    {
        double resistance;
        double reactance;

        if (GammaToNormalizedImpedance(real, imag, &resistance, &reactance)) {
            double physicalResistance;
            double physicalReactance;

            physicalResistance = resistance * linePtr->z0;
            physicalReactance = reactance * linePtr->z0;
            if (SetClosestInfoDouble(interp, resultObjPtr, "normalizedResistance", resistance) != TCL_OK) {
                return TCL_ERROR;
            }
            if (SetClosestInfoDouble(interp, resultObjPtr, "normalizedReactance", reactance) != TCL_OK) {
                return TCL_ERROR;
            }
            if (SetClosestInfoPair(interp, resultObjPtr, "normalizedImpedance", resistance, reactance) != TCL_OK) {
                return TCL_ERROR;
            }
            if (SetClosestInfoDouble(interp, resultObjPtr, "resistance", physicalResistance) != TCL_OK) {
                return TCL_ERROR;
            }
            if (SetClosestInfoDouble(interp, resultObjPtr, "reactance", physicalReactance) != TCL_OK) {
                return TCL_ERROR;
            }
            if (SetClosestInfoPair(interp, resultObjPtr, "impedance", physicalResistance, physicalReactance) !=
                TCL_OK) {
                return TCL_ERROR;
            }
        }
    }
    {
        double conductance;
        double susceptance;

        if (GammaToNormalizedAdmittance(real, imag, &conductance, &susceptance)) {
            double physicalConductance;
            double physicalSusceptance;

            /*
             * y = Y * Z0, therefore Y = y / Z0.
             */
            physicalConductance = conductance / linePtr->z0;
            physicalSusceptance = susceptance / linePtr->z0;
            if (SetClosestInfoDouble(interp, resultObjPtr, "normalizedConductance", conductance) != TCL_OK) {
                return TCL_ERROR;
            }
            if (SetClosestInfoDouble(interp, resultObjPtr, "normalizedSusceptance", susceptance) != TCL_OK) {
                return TCL_ERROR;
            }
            if (SetClosestInfoPair(interp, resultObjPtr, "normalizedAdmittance", conductance, susceptance) != TCL_OK) {
                return TCL_ERROR;
            }
            if (SetClosestInfoDouble(interp, resultObjPtr, "conductance", physicalConductance) != TCL_OK) {
                return TCL_ERROR;
            }
            if (SetClosestInfoDouble(interp, resultObjPtr, "susceptance", physicalSusceptance) != TCL_OK) {
                return TCL_ERROR;
            }
            if (SetClosestInfoPair(interp, resultObjPtr, "admittance", physicalConductance, physicalSusceptance) !=
                TCL_OK) {
                return TCL_ERROR;
            }
        }
    }
    return TCL_OK;
}

/**
 * @ingroup line_picking_rendering
 * @brief Draw connected traces through one screen stroke context.
 *
 * Borrows the pen and line geometry. Begins a trace stroke using GC, color,
 * width and dashes, resolving default off-dash color to the trace color.
 * Draws every stored polyline and ends the context. A NULL context silently
 * skips drawing. Geometry and pen ownership remain with the element.
 */
static void DrawTraces(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr) {
    Rbc_RenderContext *ctx;
    Rbc_ChainLink *linkPtr;

    ctx = Rbc_RenderBeginStroke(graphPtr, drawable, penPtr->traceGC, RBC_RENDER_STROKE_TRACES,
                          penPtr->traceColor, penPtr->traceWidth, &penPtr->traceDashes,
                          (penPtr->traceOffColor == COLOR_DEFAULT) ? penPtr->traceColor : penPtr->traceOffColor);
    if (ctx == NULL) {
        return;
    }
    for (linkPtr = Rbc_ChainFirstLink(linePtr->traces); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        LineTrace *tracePtr = Rbc_ChainGetValue(linkPtr);
        Rbc_RenderPolyline(ctx, tracePtr->screenPts, tracePtr->nScreenPts);
    }
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup line_picking_rendering
 * @brief Draw independent segments through one screen stroke context.
 *
 * Borrows the pen and segment array for the duration of the call. Resolves
 * default off-dash color, begins a strip stroke, submits the segment batch and
 * ends the context. A NULL context skips the batch. Does not remap geometry
 * or retain the array beyond the call.
 */
static void DrawRenderedStrips(Graph *graphPtr, Drawable drawable, LinePen *penPtr, const Segment2D *segments,
                               Tcl_Size count) {
    Rbc_RenderContext *ctx = Rbc_RenderBeginStroke(graphPtr, drawable, penPtr->traceGC, RBC_RENDER_STROKE_STRIPS,
        penPtr->traceColor, penPtr->traceWidth, &penPtr->traceDashes,
        (penPtr->traceOffColor == COLOR_DEFAULT) ? penPtr->traceColor : penPtr->traceOffColor);
    if (ctx != NULL) {
        Rbc_RenderSegments(ctx, segments, count);
        Rbc_RenderEnd(ctx);
    }
}

/**
 * @ingroup line_output_dispatch
 * @brief Build an origin-centered geometric symbol template.
 *
 * Zeros the caller-owned shape and fills circle, polygon or independent
 * segment geometry using native proportions and integer rounding. Returns
 * FALSE for unsupported types, including bitmap and none; such output is not
 * a usable template. Borrows no persistent storage and performs no allocation.
 */
static int GetRenderedSymbolShape(SymbolType type, int size, Rbc_RenderShape *shape) {
    int i;

    int r1 = (int)ceil(size * 0.5);
    int r2 = (int)ceil(size * 0.886226925452758 * 0.5);
    memset(shape, 0, sizeof(*shape));
    shape->type = RBC_RENDER_POLYGON;
    switch (type) {
    case SYMBOL_CIRCLE:
        shape->type = RBC_RENDER_CIRCLE;
        shape->radius = r1;
        break;
    case SYMBOL_SQUARE:
        shape->nPoints = 4;
        shape->points[0] = (Point2D){-r2, -r2};
        shape->points[1] = (Point2D){ r2, -r2};
        shape->points[2] = (Point2D){ r2,  r2};
        shape->points[3] = (Point2D){-r2,  r2};
        break;
    case SYMBOL_DIAMOND:
        shape->nPoints = 4;
        shape->points[0] = (Point2D){-r1, 0};
        shape->points[1] = (Point2D){0, -r1};
        shape->points[2] = (Point2D){r1, 0};
        shape->points[3] = (Point2D){0, r1};
        break;
    case SYMBOL_SPLUS:
    case SYMBOL_SCROSS:
        shape->type = RBC_RENDER_SEGMENTS;
        shape->nPoints = 4;
        if (type == SYMBOL_SCROSS) {
            r2 = Round(r2 * M_SQRT1_2);
            shape->points[0] = (Point2D){-r2, -r2};
            shape->points[1] = (Point2D){ r2,  r2};
            shape->points[2] = (Point2D){-r2,  r2};
            shape->points[3] = (Point2D){ r2, -r2};
        } else {
            shape->points[0] = (Point2D){-r2, 0};
            shape->points[1] = (Point2D){ r2, 0};
            shape->points[2] = (Point2D){0, -r2};
            shape->points[3] = (Point2D){0,  r2};
        }
        break;
    case SYMBOL_PLUS:
    case SYMBOL_CROSS: {
        int d = r2 / 3;
        Point2D points[] = {
            {-r2,-d}, {-d,-d}, {-d,-r2}, {d,-r2}, {d,-d}, {r2,-d},
            {r2,d}, {d,d}, {d,r2}, {-d,r2}, {-d,d}, {-r2,d}
        };
        shape->nPoints = 12;
        memcpy(shape->points, points, sizeof(points));
        if (type == SYMBOL_CROSS) {
            for (i = 0; i < 12; i++) {
                double dx = shape->points[i].x * M_SQRT1_2;
                double dy = shape->points[i].y * M_SQRT1_2;
                shape->points[i].x = Round(dx - dy);
                shape->points[i].y = Round(dx + dy);
            }
        }
        break;
    }
    case SYMBOL_TRIANGLE:
    case SYMBOL_ARROW: {
        int b = Round(size * 1.3467736870885982 * 0.7);
        int b2 = Round(b * 0.5);
        int h2 = Round(0.57735026918962573 * b2);
        int h1 = Round(b2 / 0.86602540378443871);
        int direction = (type == SYMBOL_ARROW) ? -1 : 1;
        shape->nPoints = 3;
        shape->points[0] = (Point2D){0, -direction * h1};
        shape->points[1] = (Point2D){b2, direction * h2};
        shape->points[2] = (Point2D){-b2, direction * h2};
        break;
    }
    default:
        return FALSE;
    }
    return TRUE;
}

/**
 * @ingroup line_output_dispatch
 * @brief Handle tiny Cairo symbols as filled points.
 *
 * Returns FALSE unless Cairo is selected and size is below three, or when
 * context creation fails, allowing fallback. A missing effective fill color or
 * nonpositive count is successfully handled without drawing. Resolves default
 * fill to trace color and uses explicit drawable dimensions when supplied.
 * Draws all centers without symbol-interval filtering, then ends the context.
 */
static int DrawRenderedPoints(Graph *graphPtr, Drawable drawable, LinePen *penPtr, int size, Tcl_Size count,
                              const Point2D *points, int targetWidth, int targetHeight) {
    Rbc_RenderContext *ctx;
    XColor *color;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (size >= 3)) {
        return FALSE;
    }
    color = (penPtr->symbol.fillColor == COLOR_DEFAULT) ? penPtr->traceColor : penPtr->symbol.fillColor;
    if ((color == NULL) || (count <= 0)) {
        return TRUE;
    }
    ctx = (targetWidth > 0)
              ? Rbc_RenderBeginDrawable(graphPtr, drawable, targetWidth, targetHeight, color, 1, NULL, NULL)
              : Rbc_RenderBegin(graphPtr, drawable, color, 1, NULL, NULL);
    if (ctx == NULL) {
        return FALSE;
    }
    Rbc_RenderPoints(ctx, points, count);
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/**
 * @ingroup line_output_dispatch
 * @brief Draw a Cairo bitmap-symbol pass with shared scaled resources.
 *
 * Requires Cairo, a bitmap symbol, size at least three, positive count and
 * an effective foreground. Scales bitmap and optional mask once. Transparent
 * background uses the bitmap itself as mask. Releases temporary pixmaps after
 * context creation; renderer setup captures the required image data.
 *
 * Returns FALSE on ineligibility or setup failure for caller fallback. Submits
 * selected centers in batches of 256, advancing the shared symbol counter only
 * for plot drawing with a positive interval. Explicit drawable targets bypass
 * that interval. Ends the context and returns TRUE after handling the pass.
 */
static int DrawRenderedBitmapSymbols(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, int size,
                                     Tcl_Size nPoints, const Point2D *points, int targetWidth, int targetHeight) {
    Pixmap bitmap, mask = None;
    Rbc_RenderContext *ctx;
    XColor *foreground, *background;
    int width, height, scaledWidth, scaledHeight;
    double scale;
    Point2D centers[256];
    Tcl_Size i, count = 0, counter = linePtr->symbolCounter;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (penPtr->symbol.type != SYMBOL_BITMAP) || (size < 3) ||
        (nPoints <= 0)) {
        return FALSE;
    }
    foreground = (penPtr->symbol.outlineColor == COLOR_DEFAULT) ? penPtr->traceColor : penPtr->symbol.outlineColor;
    background = (penPtr->symbol.fillColor == COLOR_DEFAULT) ? penPtr->traceColor : penPtr->symbol.fillColor;
    if (foreground == NULL) {
        return FALSE;
    }
    Tk_SizeOfBitmap(graphPtr->display, penPtr->symbol.bitmap, &width, &height);
    if ((width <= 0) || (height <= 0)) {
        return FALSE;
    }
    scale = MIN((double)size / width, (double)size / height);
    scaledWidth = (int)(width * scale);
    scaledHeight = (int)(height * scale);
    if ((scaledWidth <= 0) || (scaledHeight <= 0)) {
        return FALSE;
    }
    bitmap = Rbc_ScaleBitmap(graphPtr->tkwin, penPtr->symbol.bitmap, width, height, scaledWidth, scaledHeight);
    if (bitmap == None) {
        return FALSE;
    }
    if (background == NULL) {
        mask = bitmap; /* Native transparent symbols use their own bitmap as the mask. */
    } else if (penPtr->symbol.mask != None) {
        mask = Rbc_ScaleBitmap(graphPtr->tkwin, penPtr->symbol.mask, width, height, scaledWidth, scaledHeight);
        if (mask == None) {
            Tk_FreePixmap(graphPtr->display, bitmap);
            return FALSE;
        }
    }
    ctx = Rbc_RenderBeginBitmapSymbols(graphPtr, drawable, bitmap, mask, scaledWidth, scaledHeight, foreground,
                                       background, targetWidth, targetHeight);
    if ((mask != None) && (mask != bitmap)) {
        Tk_FreePixmap(graphPtr->display, mask);
    }
    Tk_FreePixmap(graphPtr->display, bitmap);
    if (ctx == NULL) {
        return FALSE;
    }
    for (i = 0; i < nPoints; i++) {
        int draw = TRUE;
        if ((targetWidth == 0) && (linePtr->symbolInterval > 0)) {
            draw = ((counter % linePtr->symbolInterval) == 0);
            counter++;
        }
        if (draw) {
            centers[count++] = points[i];
            if (count == 256) {
                Rbc_RenderBitmapSymbols(ctx, centers, count);
                count = 0;
            }
        }
    }
    Rbc_RenderBitmapSymbols(ctx, centers, count);
    Rbc_RenderEnd(ctx);
    if ((targetWidth == 0) && (linePtr->symbolInterval > 0)) {
        linePtr->symbolCounter = counter;
    }
    return TRUE;
}

/**
 * @ingroup line_output_dispatch
 * @brief Draw geometric Cairo symbols in fill and outline passes.
 *
 * Returns FALSE for non-Cairo, tiny/unsupported shapes or failed context
 * creation. Resolves default colors from the trace pen. Segment symbols omit
 * fill and retain a hairline outline even at configured width zero.
 *
 * Each pass starts from the same counter so fill and outline select identical
 * centers. Uses batches of 256 and advances the line counter once for the
 * whole input when an interval is active. Ends the context and returns TRUE;
 * geometry and pen resources remain borrowed.
 */
static int DrawRenderedSymbols(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, int size,
                               Tcl_Size nSymbolPts, const Point2D *symbolPts) {
    Rbc_RenderContext *ctx;
    Rbc_RenderShape shape;
    XColor *fillColor, *outlineColor;
    Point2D centers[256];
    Tcl_Size i, count, counter;
    int pass, outline;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (size < 3) ||
        !GetRenderedSymbolShape(penPtr->symbol.type, size, &shape)) {
        return FALSE;
    }
    fillColor = (penPtr->symbol.fillColor == COLOR_DEFAULT) ? penPtr->traceColor : penPtr->symbol.fillColor;
    outlineColor = (penPtr->symbol.outlineColor == COLOR_DEFAULT) ? penPtr->traceColor : penPtr->symbol.outlineColor;
    outline = (penPtr->symbol.outlineWidth > 0);
    if (shape.type == RBC_RENDER_SEGMENTS) {
        fillColor = NULL;
        outline = TRUE; /* Native splus/scross retain a hairline at width zero. */
    }
    ctx = Rbc_RenderBegin(graphPtr, drawable, outlineColor, MAX(1, penPtr->symbol.outlineWidth), NULL, NULL);
    if (ctx == NULL) {
        return FALSE;
    }
    for (pass = 0; pass < 2; pass++) {
        if (((pass == 0) && (fillColor == NULL)) || ((pass == 1) && !outline)) {
            continue;
        }
        count = 0;
        counter = linePtr->symbolCounter;
        for (i = 0; i < nSymbolPts; i++) {
            int draw = TRUE;

            if (linePtr->symbolInterval > 0) {
                draw = ((counter % linePtr->symbolInterval) == 0);
                counter++;
            }
            if (draw) {
                centers[count] = symbolPts[i];
                count++;
                if (count == 256) {
                    Rbc_RenderSymbols(ctx, &shape, centers, count, (pass == 0) ? fillColor : NULL, pass == 1);
                    count = 0;
                }
            }
        }
        Rbc_RenderSymbols(ctx, &shape, centers, count, (pass == 0) ? fillColor : NULL, pass == 1);
    }
    if (linePtr->symbolInterval > 0) {
        linePtr->symbolCounter += nSymbolPts;
    }
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/**
 * @ingroup line_output_dispatch
 * @brief Submit solid screen error-bar segments through a stroke context.
 *
 * Resolves default error-bar color to trace color and uses at least unit
 * width for renderer stroke setup while retaining the native GC. Begins and
 * ends one segment context; NULL context skips drawing. Borrows all arrays
 * and pen resources and does not apply symbol sampling here.
 */
static void DrawRenderedErrorBars(Graph *graphPtr, Drawable drawable, LinePen *penPtr, const Segment2D *segments,
                                  Tcl_Size count) {
    XColor *color = (penPtr->errorBarColor == COLOR_DEFAULT) ? penPtr->traceColor : penPtr->errorBarColor;
    Rbc_RenderContext *ctx = Rbc_RenderBeginStroke(graphPtr, drawable, penPtr->errorBarGC,
        RBC_RENDER_STROKE_SEGMENTS, color, MAX(1, penPtr->errorBarLineWidth), NULL, NULL);
    if (ctx != NULL) {
        Rbc_RenderSegments(ctx, segments, count);
        Rbc_RenderEnd(ctx);
    }
}

/**
 * @ingroup line_output_dispatch
 * @brief Dispatch native symbol drawing and retain its sampling counter.
 *
 * Builds a temporary native style from the pen's GCs, bitmap, mask, interval
 * and counter. Submits borrowed centers through a native symbol context,
 * ends it and copies the updated style counter back to the line. Does not
 * establish a plot clip; callers must supply an appropriate target/clip.
 */
static void DrawSymbolsUnclipped(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, int size,
                                 Tcl_Size nSymbolPts, Point2D *symbolPts) {
    static const Rbc_RenderSymbolType types[] = {
        RBC_RENDER_SYMBOL_NONE, RBC_RENDER_SYMBOL_SQUARE, RBC_RENDER_SYMBOL_CIRCLE,
        RBC_RENDER_SYMBOL_DIAMOND, RBC_RENDER_SYMBOL_PLUS, RBC_RENDER_SYMBOL_CROSS,
        RBC_RENDER_SYMBOL_SPLUS, RBC_RENDER_SYMBOL_SCROSS, RBC_RENDER_SYMBOL_TRIANGLE,
        RBC_RENDER_SYMBOL_ARROW, RBC_RENDER_SYMBOL_BITMAP
    };
    Rbc_RenderNativeSymbolStyle style = {types[penPtr->symbol.type], penPtr->symbol.outlineWidth,
        penPtr->symbol.outlineGC, penPtr->symbol.fillGC, penPtr->symbol.bitmap, penPtr->symbol.mask,
        linePtr->symbolInterval, linePtr->symbolCounter};
    Rbc_RenderContext *ctx = Rbc_RenderBeginNativeSymbols(graphPtr, drawable, &style, size);
    Rbc_RenderSymbolPoints(ctx, symbolPts, nSymbolPts);
    Rbc_RenderEnd(ctx);
    linePtr->symbolCounter = style.counter;
}

/**
 * @ingroup line_output_dispatch
 * @brief Draw plot symbols with renderer fallbacks and boundary clipping.
 *
 * Skips empty arrays and SYMBOL_NONE. Tries tiny Cairo points, Cairo bitmaps
 * and Cairo geometric symbols in order. If none handles the pass, bounds the
 * plot rectangle by the drawable and checks conservative symbol padding.
 *
 * Wholly interior native batches draw directly. Boundary batches use a screen
 * clip context and its target drawable, then end clip and context. Empty clips
 * skip drawing. Borrows centers and pen resources; helpers may advance the
 * line's shared sampling counter.
 */
static void DrawSymbols(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, int size,
                        Tcl_Size nSymbolPts, Point2D *symbolPts) {
    double padding;
    Tcl_Size i;
    int needsClip = FALSE;
    int left, top, right, bottom;

    if ((nSymbolPts <= 0) || (penPtr->symbol.type == SYMBOL_NONE)) {
        return;
    }
    if (DrawRenderedPoints(graphPtr, drawable, penPtr, size, nSymbolPts, symbolPts, 0, 0) ||
        DrawRenderedBitmapSymbols(graphPtr, drawable, linePtr, penPtr, size, nSymbolPts, symbolPts, 0, 0) ||
        DrawRenderedSymbols(graphPtr, drawable, linePtr, penPtr, size, nSymbolPts, symbolPts)) {
        return;
    }
    left = MAX(0, graphPtr->left);
    top = MAX(0, graphPtr->top);
    right = MIN(graphPtr->width - 1, graphPtr->right);
    bottom = MIN(graphPtr->height - 1, graphPtr->bottom);
    if ((right < left) || (bottom < top)) {
        return;
    }
    padding = SymbolPadding(penPtr, size);
    for (i = 0; i < nSymbolPts; i++) {
        if ((symbolPts[i].x - padding < left) || (symbolPts[i].x + padding > right) ||
            (symbolPts[i].y - padding < top) || (symbolPts[i].y + padding > bottom)) {
            needsClip = TRUE;
            break;
        }
    }
    if (!needsClip) {
        DrawSymbolsUnclipped(graphPtr, drawable, linePtr, penPtr, size, nSymbolPts, symbolPts);
        return;
    }
    {
        Rbc_RenderContext *clip = Rbc_RenderBeginScreenClip(graphPtr, drawable, graphPtr->drawGC, 1);
        Drawable target;
        Rbc_RenderClipBegin(clip, left, top, right-left+1, bottom-top+1);
        target = Rbc_RenderClipDrawable(clip);
        if (target != None) {
            DrawSymbolsUnclipped(graphPtr, target, linePtr, penPtr, size, nSymbolPts, symbolPts);
        }
        Rbc_RenderClipEnd(clip);
        Rbc_RenderEnd(clip);
    }
}

/**
 * @ingroup line_output_dispatch
 * @brief Draw one legend sample into an explicitly sized drawable.
 *
 * Uses the normal pen to draw a trace sample and optional centered symbol.
 * The trace uses two adjacent segments, with a native legend-line fallback
 * if context creation fails. Symbols try tiny points, bitmap rendering and
 * geometric templates before native fallback. Does not establish a plot clip
 * or remap source data; width/height describe the legend target.
 */
static void DrawSymbol(Graph *graphPtr, Drawable drawable, Element *elemPtr, int x, int y, int size, int width,
                       int height) {
    Line *linePtr = LINE_FROM_CORE(elemPtr);
    LinePen *penPtr = LINE_PEN_FROM_CORE(elemPtr->normalPenPtr);
    Rbc_RenderContext *ctx;

    if (penPtr->traceWidth > 0) {
        ctx = Rbc_RenderBeginDrawable(graphPtr, drawable, width, height, penPtr->traceColor, penPtr->traceWidth,
                                      &penPtr->traceDashes, penPtr->traceOffColor);
        if (ctx != NULL) {
            Segment2D segments[2] = {{{x - size, y}, {x + size, y}}, {{x - size, y + 1}, {x + size, y + 1}}};
            Rbc_RenderSegments(ctx, segments, 2);
            Rbc_RenderEnd(ctx);
        } else {
            Rbc_RenderScreenLegendLine(graphPtr, drawable, penPtr->traceGC, x, y, size);
        }
    }
    if (penPtr->symbol.type != SYMBOL_NONE) {
        Point2D point = {x, y};
        Rbc_RenderShape shape;

        if (DrawRenderedPoints(graphPtr, drawable, penPtr, size, 1, &point, width, height) ||
            DrawRenderedBitmapSymbols(graphPtr, drawable, linePtr, penPtr, size, 1, &point, width, height))
            return;
        if ((size >= 3) && GetRenderedSymbolShape(penPtr->symbol.type, size, &shape)) {
            XColor *fill = (penPtr->symbol.fillColor == COLOR_DEFAULT) ? penPtr->traceColor : penPtr->symbol.fillColor;
            XColor *outline =
                (penPtr->symbol.outlineColor == COLOR_DEFAULT) ? penPtr->traceColor : penPtr->symbol.outlineColor;
            int drawOutline = (penPtr->symbol.outlineWidth > 0) && (outline != NULL);
            if (shape.type == RBC_RENDER_SEGMENTS) {
                fill = NULL;
                drawOutline = (outline != NULL);
            }
            ctx = Rbc_RenderBeginDrawable(graphPtr, drawable, width, height, (outline != NULL) ? outline : fill,
                                          MAX(1, penPtr->symbol.outlineWidth), NULL, NULL);
            if (ctx != NULL) {
                Rbc_RenderSymbols(ctx, &shape, &point, 1, fill, drawOutline);
                Rbc_RenderEnd(ctx);
                return;
            }
        }
        DrawSymbolsUnclipped(graphPtr, drawable, linePtr, penPtr, size, 1, &point);
    }
}

/**
 * @ingroup line_output_dispatch
 * @brief Render sampled value labels at mapped centers on screen.
 *
 * Borrows point and parallel source-index arrays. Applies interval/offset
 * selection and unpadded viewport filtering before obtaining source values.
 * Rbc_GetElementValueLabel returns one retained Tcl object per label; this
 * routine releases it even when empty. Offset positions outside integer range
 * are skipped; accepted coordinates are converted to integers for screen text.
 *
 * Owns and ends one text context. Label commands can run Tcl: callers must
 * keep drawing records, arrays and pens stable, and callbacks must not mutate
 * drawing state or enter nested event processing. This loop adds no lifetime
 * or reentrancy guard.
 */
static void DrawValues(Graph *graphPtr, Drawable drawable, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts,
                       Point2D *symbolPts, const Tcl_Size *pointToData, Tcl_Size interval, Tcl_Size offset) {
    Point2D *pointPtr;
    Point2D *endPtr;
    Tcl_Size count;
    Extents2D exts;
    Tcl_Obj *labelObjPtr;

    Rbc_RenderContext *text = Rbc_RenderBeginScreenText(graphPtr->tkwin, drawable);
    Rbc_GraphExtents(linePtr->core.graphPtr, &exts);    
    count = 0;
    for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
        Tcl_Size dataIndex;
        double x;
        double y;
        dataIndex = pointToData[count++];
        if ((interval > 1) && (((offset + count - 1) % interval) != 0)) {
            continue;
        }
        if (!PointInRegion(&exts, pointPtr->x, pointPtr->y)) {
            continue;
        }
        if (!GetLineDataPoint(linePtr, dataIndex, &x, &y)) {
            continue;
        }
        labelObjPtr = Rbc_GetElementValueLabel(&linePtr->core, penPtr->valueCommandObjPtr,
                                               penPtr->valueFormat, penPtr->valueShow, dataIndex, x, y);
        if (Tcl_GetCharLength(labelObjPtr) == 0) {
            Tcl_DecrRefCount(labelObjPtr);
            continue;
        }
        {
            double labelX = pointPtr->x + penPtr->valueOffset.x;
            double labelY = pointPtr->y + penPtr->valueOffset.y;
            if ((labelX >= INT_MIN) && (labelX <= INT_MAX) &&
                (labelY >= INT_MIN) && (labelY <= INT_MAX)) {
                Rbc_RenderText(text, Tcl_GetString(labelObjPtr), &penPtr->valueStyle,
                             (int)labelX, (int)labelY);
            }
        }
        Tcl_DecrRefCount(labelObjPtr);
    }
    Rbc_RenderEnd(text);
}

/**
 * @ingroup line_output_dispatch
 * @brief Draw explicit active points or the whole active element.
 *
 * Returns when there is no active pen. Positive active count refreshes pending
 * active geometry and draws its symbols/labels only. Negative count draws the
 * whole mapped trace or strip plus symbols/labels; zero draws nothing.
 * Uses the scaled active symbol size. Does not draw fills or error bars.
 * Value-label callbacks have the same stability requirements as DrawValues.
 */
static void DrawActiveLine(Graph *graphPtr, Drawable drawable, Element *elemPtr) {
    Line *linePtr = LINE_FROM_CORE(elemPtr);
    LinePen *penPtr;
    int symbolSize;

    penPtr = (elemPtr->activePenPtr != NULL) ? LINE_PEN_FROM_CORE(elemPtr->activePenPtr) : NULL;
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
                       linePtr->activeToData, 0, 0);
        }
    } else if (elemPtr->nActiveIndices < 0) {
        if (penPtr->traceWidth > 0) {
            if (linePtr->nStrips > 0) {
                DrawRenderedStrips(graphPtr, drawable, penPtr, linePtr->strips, linePtr->nStrips);
            } else if (Rbc_ChainGetLength(linePtr->traces) > 0) {
                DrawTraces(graphPtr, drawable, linePtr, penPtr);
            }
        }
        if (penPtr->symbol.type != SYMBOL_NONE) {
            DrawSymbols(graphPtr, drawable, linePtr, penPtr, symbolSize, linePtr->nSymbolPts, linePtr->symbolPts);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            DrawValues(graphPtr, drawable, linePtr, penPtr, linePtr->nSymbolPts, linePtr->symbolPts,
                       linePtr->symbolToData, 0, 0);
        }
    }
}

/**
 * @ingroup line_output_dispatch
 * @brief Attempt a Cairo tile or pattern fill before native fallback.
 *
 * Returns FALSE outside Cairo. A supplied tile takes precedence: obtains its
 * photo block and delegates tile drawing, without trying the pattern if that
 * fails. With no tile, None stipple declines the operation; missing foreground
 * is recovered from the native GC. PATTERN_SOLID becomes no renderer stipple.
 * Forwards pattern opacity; tile transparency comes from the image itself.
 * Borrows geometry/resources and returns whether the renderer handled the fill.
 */
static int DrawRenderedFill(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                             XColor *foreground, XColor *background, GC gc, Rbc_Tile tile,
                             Pixmap stipple, double opacity) {
    XColor gcColor;

    if (graphPtr->renderer != RBC_RENDERER_CAIRO) {
        return FALSE;
    }
    if (tile != NULL) {
        Tk_PhotoImageBlock block;

        if (!Rbc_GetTilePhoto(tile, &block)) {
            return FALSE;
        }
        return Rbc_RenderTileArea(graphPtr, drawable, points, count, &block);
    }
    if (stipple == None) {
        return FALSE;
    }
    if (foreground == NULL) {
        if (!Rbc_RenderGCForeground(graphPtr, gc, &gcColor))
            return FALSE;
        foreground = &gcColor;
    }
    return Rbc_RenderAreaOpacity(graphPtr, drawable, points, count, foreground,
                                 background,
                                 (stipple == PATTERN_SOLID) ? None : stipple,
                                 opacity);
}

/**
 * @ingroup line_fills
 * @brief Draw borrowed fill geometry with Cairo-first/native fallback policy.
 * @param graphPtr Borrowed graph with the desired renderer and active clip.
 * @param drawable Target window or pixmap.
 * @param points Borrowed clipped polygon, or NULL to skip drawing.
 * @param count Number of polygon vertices.
 * @param foreground Borrowed foreground color; Cairo can recover it from gc.
 * @param background Borrowed optional stipple background color.
 * @param gc Borrowed native fill GC.
 * @param tile Borrowed optional tile, which takes precedence over stipple.
 * @param stipple Bitmap, PATTERN_SOLID sentinel, or None.
 * @param opacity Validated pattern opacity; native fallback does not blend it.
 * @details A tile without a pixmap suppresses drawing rather than exposing the
 * pattern beneath it. A successful Cairo fill ends dispatch; otherwise a native
 * fill context is begun and ended here. Input geometry and resources remain
 * owned by the element. Tile drawing uses the tile's own image transparency.
 */
static void DrawLineFill(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                          XColor *foreground, XColor *background, GC gc, Rbc_Tile tile,
                          Pixmap stipple, double opacity) {
    Rbc_RenderContext *ctx;
    if ((points == NULL) || ((tile != NULL) && (Rbc_PixmapOfTile(tile) == None)) ||
        DrawRenderedFill(graphPtr, drawable, points, count, foreground, background,
                         gc, tile, stipple, opacity)) {
        return;
    }
    ctx = Rbc_RenderBeginNativeFill(graphPtr, drawable, gc, tile, stipple);
    Rbc_RenderFillPolygon(ctx, points, count);
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup line_output_dispatch
 * @brief Compute the shared screen/export symbol sampling interval.
 *
 * Returns zero when no positive requested maximum exists. Otherwise sums
 * palette symbol counts and returns integer division by the requested maximum.
 * Zero and one both select all centers in normal use. This is a density
 * interval, not a strict maximum-count guarantee. Does not mutate the line.
 */
static Tcl_Size SymbolInterval(Line *linePtr) {
    Tcl_Size total = 0;
    Rbc_ChainLink *linkPtr;

    if (linePtr->reqMaxSymbols <= 0) {
        return 0;
    }
    for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        LinePenStyle *stylePtr = Rbc_ChainGetValue(linkPtr);

        total += stylePtr->nSymbolPts;
    }
    return total / (Tcl_Size)linePtr->reqMaxSymbols;
}

/**
 * @ingroup line_output_dispatch
 * @brief Draw mapped fills, strokes and palette annotations on screen.
 *
 * Draws area then error-band polygons, followed by palette strip strokes or
 * the normal-pen connected traces. Initializes shared symbol interval/counter,
 * then draws each style's enabled error bars, symbols and value labels.
 * Styles without symbols still advance the interval counter; label offsets
 * track the parallel source-index slices. Resets symbolInterval on completion.
 *
 * Borrows mapped geometry and configured resources. Label callbacks require
 * stable drawing state; this function does not protect iteration from mutation.
 */
static void DrawNormalLine(Graph *graphPtr, Drawable drawable, Element *elemPtr) {
    LinePen *normalPenPtr;
    Line *linePtr = LINE_FROM_CORE(elemPtr);
    normalPenPtr = LINE_PEN_FROM_CORE(elemPtr->normalPenPtr);
    LinePen *penPtr;
    Rbc_ChainLink *linkPtr;
    register LinePenStyle *stylePtr;
    Tcl_Size count;

    LineBandPolygon *polygonPtr;
    LineErrorBand *bandPtr = &linePtr->errorBand;

    /* Ordinary area first, then the uncertainty band, then annotations. */
    DrawLineFill(graphPtr, drawable, linePtr->fillPts, linePtr->nFillPts, linePtr->fillFgColor,
                  linePtr->fillBgColor, linePtr->fillGC, linePtr->fillTile, linePtr->fillStipple,
                  linePtr->areaOpacity);
    for (polygonPtr = bandPtr->polygons; polygonPtr != NULL; polygonPtr = polygonPtr->nextPtr) {
        DrawLineFill(graphPtr, drawable, polygonPtr->points, polygonPtr->count, bandPtr->foreground,
                      bandPtr->background, bandPtr->gc, bandPtr->tile, bandPtr->stipple, bandPtr->opacity);
    }
    /* Lines: stripchart segments or graph traces. */
    if (linePtr->nStrips > 0) {
        for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            penPtr = stylePtr->penPtr;
            if ((stylePtr->nStrips > 0) && (penPtr->traceWidth > 0)) {
                DrawRenderedStrips(graphPtr, drawable, penPtr, stylePtr->strips, stylePtr->nStrips);
            }
        }
    } else if ((Rbc_ChainGetLength(linePtr->traces) > 0) && (normalPenPtr->traceWidth > 0)) {
        DrawTraces(graphPtr, drawable, linePtr, normalPenPtr);
    }
    linePtr->symbolInterval = SymbolInterval(linePtr);
    linePtr->symbolCounter = 0;
    /* Symbols, error bars, values. */
    count = 0;
    for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        penPtr = stylePtr->penPtr;
        if ((stylePtr->xErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_X)) {
            DrawRenderedErrorBars(graphPtr, drawable, penPtr, stylePtr->xErrorBars, stylePtr->xErrorBarCnt);
        }
        if ((stylePtr->yErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_Y)) {
            DrawRenderedErrorBars(graphPtr, drawable, penPtr, stylePtr->yErrorBars, stylePtr->yErrorBarCnt);
        }
        if ((stylePtr->nSymbolPts > 0) && (penPtr->symbol.type != SYMBOL_NONE)) {
            DrawSymbols(graphPtr, drawable, linePtr, penPtr, stylePtr->symbolSize, stylePtr->nSymbolPts,
                        stylePtr->symbolPts);
        } else if (linePtr->symbolInterval > 0) {
            linePtr->symbolCounter += stylePtr->nSymbolPts;
        }
        if (penPtr->valueShow != SHOW_NONE) {
            DrawValues(graphPtr, drawable, linePtr, penPtr, stylePtr->nSymbolPts, stylePtr->symbolPts,
                       linePtr->symbolToData + count, linePtr->symbolInterval, count);
        }
        count += stylePtr->nSymbolPts;
    }
    linePtr->symbolInterval = 0;
}

/**
 * @ingroup line_output_dispatch
 * @brief Export sampled symbols using a resolved renderer symbol style.
 *
 * Borrows pen and center arrays, skipping none/empty symbols. Resolves default
 * colors and passes bitmap/mask and size to an export-symbol context. For
 * interval greater than one, requires a writable counter, advances it for
 * every input and submits selected centers. Otherwise submits the full batch
 * without touching the counter, which may be NULL. Ends the context.
 */
static void SymbolsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, LinePen *penPtr, int size, Tcl_Size nSymbolPts,
                                Point2D *symbolPts, Tcl_Size interval, Tcl_Size *counterPtr) {
    static const Rbc_RenderSymbolType types[] = {
        RBC_RENDER_SYMBOL_CIRCLE, /* SYMBOL_NONE is not drawn here. */
        RBC_RENDER_SYMBOL_SQUARE, RBC_RENDER_SYMBOL_CIRCLE, RBC_RENDER_SYMBOL_DIAMOND,
        RBC_RENDER_SYMBOL_PLUS, RBC_RENDER_SYMBOL_CROSS, RBC_RENDER_SYMBOL_SPLUS,
        RBC_RENDER_SYMBOL_SCROSS, RBC_RENDER_SYMBOL_TRIANGLE, RBC_RENDER_SYMBOL_ARROW,
        RBC_RENDER_SYMBOL_BITMAP
    };
    Rbc_RenderSymbolStyle style;
    Rbc_RenderContext *ctx;

    if ((penPtr->symbol.type == SYMBOL_NONE) || (nSymbolPts <= 0)) {
        return;
    }
    style.type = types[penPtr->symbol.type];
    style.size = size;
    style.outlineWidth = penPtr->symbol.outlineWidth;
    style.outlineColor = (penPtr->symbol.outlineColor == COLOR_DEFAULT) ?
        penPtr->traceColor : penPtr->symbol.outlineColor;
    style.fillColor = (penPtr->symbol.fillColor == COLOR_DEFAULT) ?
        penPtr->traceColor : penPtr->symbol.fillColor;
    style.bitmap = penPtr->symbol.bitmap;
    style.mask = penPtr->symbol.mask;
    ctx = Rbc_RenderBeginExportSymbol(graphPtr, exportPtr, &style);
    if (interval > 1) {
        Tcl_Size i;

        for (i = 0; i < nSymbolPts; i++) {
            if ((*counterPtr % interval) == 0) {
                Rbc_RenderSymbolPoints(ctx, symbolPts + i, 1);
            }
            (*counterPtr)++;
        }
    } else {
        Rbc_RenderSymbolPoints(ctx, symbolPts, nSymbolPts);
    }
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup line_output_dispatch
 * @brief Export a normal-pen legend sample.
 *
 * Emits an optional trace segment spanning twice the supplied size, with
 * trace width increased by two and butt/miter stroke settings, then exports
 * one centered symbol without sampling. Borrows the pen and export context;
 * does not use the screen legend's two-segment construction.
 */
static void SymbolExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Element *elemPtr, double x, double y, int size) {
    LinePen *penPtr = LINE_PEN_FROM_CORE(elemPtr->normalPenPtr);
    if (penPtr->traceWidth > 0) {
        /* The legend sample is twice the symbol width and uses a thicker stroke. */
        Segment2D segment = {{x - size, y}, {x + size, y}};
        Rbc_RenderContext *ctx = Rbc_RenderBeginExport(exportPtr, penPtr->traceColor, penPtr->traceWidth + 2,
                                                          &penPtr->traceDashes, CapButt, JoinMiter);

        Rbc_RenderSegments(ctx, &segment, 1);
        Rbc_RenderEnd(ctx);
    }
    if (penPtr->symbol.type != SYMBOL_NONE) {
        Point2D point;
        point.x = x, point.y = y;
        SymbolsExport(graphPtr, exportPtr, penPtr, size, 1, &point, 0, NULL);
    }
}

/**
 * @ingroup line_output_dispatch
 * @brief Create an export stroke context for a line pen.
 *
 * Uses trace color, width and dashes with butt caps and miter joins, then
 * sets the optional off-dash background. Returns the renderer context for the
 * caller to end. Does not own the pen or alter mapped geometry.
 */
static Rbc_RenderContext *BeginLineExport(Rbc_ExportContext *exportPtr, LinePen *penPtr) {
    Rbc_RenderContext *ctx;

    ctx = Rbc_RenderBeginExport(exportPtr, penPtr->traceColor, penPtr->traceWidth, &penPtr->traceDashes,
                                    CapButt, JoinMiter);
    Rbc_RenderDashBackground(ctx, penPtr->traceOffColor);
    return ctx;
}

/**
 * @ingroup line_output_dispatch
 * @brief Export all connected traces through one pen stroke context.
 *
 * Begins a line export context, submits each borrowed trace polyline in chain
 * order and ends the context. Does not resample, remap or take ownership of
 * trace arrays; export preparation must supply the desired geometry.
 */
static void TracesExport(Rbc_ExportContext *exportPtr, Line *linePtr, LinePen *penPtr) {
    Rbc_ChainLink *linkPtr;
    Rbc_RenderContext *ctx = BeginLineExport(exportPtr, penPtr);

    for (linkPtr = Rbc_ChainFirstLink(linePtr->traces); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        LineTrace *tracePtr = Rbc_ChainGetValue(linkPtr);

        Rbc_RenderPolyline(ctx, tracePtr->screenPts, tracePtr->nScreenPts);
    }
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup line_output_dispatch
 * @brief Export sampled labels using double-precision mapped positions.
 *
 * Applies the same interval/offset, viewport and source-value filtering as
 * screen labels. Creates one output context and releases each retained label
 * object after use. Exports center plus offset without screen integer conversion.
 *
 * Label callbacks may execute Tcl and require stable element, pen and mapping
 * storage. Does not guard against callback mutation or nested event processing.
 * Ends the output context; owns neither the export destination nor input arrays.
 */
static void ValuesExport(Rbc_ExportContext *exportPtr, Line *linePtr, LinePen *penPtr, Tcl_Size nSymbolPts, Point2D *symbolPts,
                               const Tcl_Size *pointToData, Tcl_Size interval, Tcl_Size offset) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    Point2D *pointPtr;
    Point2D *endPtr;
    Tcl_Size count;
    Extents2D exts;    
    Tcl_Obj *labelObjPtr;

    Rbc_GraphExtents(linePtr->core.graphPtr, &exts);    
    count = 0;
    for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
        Tcl_Size dataIndex;
        double x;
        double y;

        dataIndex = pointToData[count++];
        if ((interval > 1) && (((offset + count - 1) % interval) != 0)) {
            continue;
        }
        if (!PointInRegion(&exts, pointPtr->x, pointPtr->y)) {
            continue;
        }
        if (!GetLineDataPoint(linePtr, dataIndex, &x, &y)) {
            continue;
        }
        labelObjPtr = Rbc_GetElementValueLabel(&linePtr->core, penPtr->valueCommandObjPtr,
                                               penPtr->valueFormat, penPtr->valueShow, dataIndex, x, y);
        if (Tcl_GetCharLength(labelObjPtr) == 0) {
            Tcl_DecrRefCount(labelObjPtr);
            continue;
        }
        Rbc_RenderText(output, Tcl_GetString(labelObjPtr), &penPtr->valueStyle,
                             pointPtr->x + penPtr->valueOffset.x, pointPtr->y + penPtr->valueOffset.y);
        Tcl_DecrRefCount(labelObjPtr);
    }
    Rbc_RenderEnd(output);
}

/**
 * @ingroup line_output_dispatch
 * @brief Export explicit active symbols or whole-element highlighting.
 *
 * With an active pen, positive active count refreshes pending active geometry
 * and exports symbols/labels. Negative count exports available strips/traces,
 * symbols and labels for the whole element. Zero count or absent pen emits
 * nothing. Uses scaled active size and no symbol sampling. Does not export
 * active fills/error bars. Label callbacks require stable drawing resources.
 */
static void ActiveLineExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Element *elemPtr) {
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
            SymbolsExport(graphPtr, exportPtr, penPtr, symbolSize, linePtr->nActivePts, linePtr->activePts, 0, NULL);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            ValuesExport(exportPtr, linePtr, penPtr, linePtr->nActivePts, linePtr->activePts,
                               linePtr->activeToData, 0, 0);
        }
    } else if (elemPtr->nActiveIndices < 0) {
        if (penPtr->traceWidth > 0) {
            if (linePtr->nStrips > 0) {
                Rbc_RenderContext *ctx = BeginLineExport(exportPtr, penPtr);

                Rbc_RenderSegments(ctx, linePtr->strips, linePtr->nStrips);
                Rbc_RenderEnd(ctx);
            }
            if (Rbc_ChainGetLength(linePtr->traces) > 0) {
                TracesExport(exportPtr, linePtr, penPtr);
            }
        }
        if (penPtr->symbol.type != SYMBOL_NONE) {
            SymbolsExport(graphPtr, exportPtr, penPtr, symbolSize, linePtr->nSymbolPts, linePtr->symbolPts, 0, NULL);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            ValuesExport(exportPtr, linePtr, penPtr, linePtr->nSymbolPts, linePtr->symbolPts,
                               linePtr->symbolToData, 0, 0);
        }
    }
}

/**
 * @ingroup line_output_dispatch
 * @brief Export mapped fills, strokes and palette annotations in order.
 *
 * Exports area then error-band polygons through fill contexts, including tile,
 * pattern and opacity policy handled by the backend. Exports palette strip
 * strokes or normal-pen connected traces, then enabled error bars, symbols
 * and labels per style. Shares a local symbol interval/counter across styles
 * and keeps label source-index offsets aligned with palette slices.
 *
 * Borrows geometry already prepared for export; does not disable decimation
 * or remap here. Backend-specific fill behavior remains in the renderer.
 * Value-label callbacks require stable drawing state throughout traversal.
 */
static void NormalLineExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Element *elemPtr) {
    Line *linePtr = LINE_FROM_CORE(elemPtr);
    LinePen *normalPenPtr = LINE_PEN_FROM_CORE(elemPtr->normalPenPtr);
    register LinePenStyle *stylePtr;
    Rbc_ChainLink *linkPtr;
    LinePen *penPtr;
    Tcl_Size count;
    Tcl_Size interval = SymbolInterval(linePtr);
    Tcl_Size counter = 0;
    XColor *colorPtr;

    /* Draw fill area; PostScript retains its opaque/tile-background policy. */
    if (linePtr->fillPts != NULL) {
        Rbc_RenderFillStyle style = {linePtr->fillFgColor, linePtr->fillBgColor,
            (linePtr->fillStipple == PATTERN_SOLID) ? None : linePtr->fillStipple,
            linePtr->areaOpacity, linePtr->fillTile != NULL};
        Rbc_RenderContext *ctx = Rbc_RenderBeginExportFill(graphPtr, exportPtr, &style);

        if (linePtr->fillTile != NULL) {
            Rbc_RenderSetFillTile(ctx, linePtr->fillTile);
        }
        Rbc_RenderFillPolygon(ctx, linePtr->fillPts, linePtr->nFillPts);
        Rbc_RenderEnd(ctx);
    }
    if (linePtr->errorBand.polygons != NULL) {
        LineErrorBand *bandPtr = &linePtr->errorBand;
        LineBandPolygon *polygonPtr;
        Rbc_RenderFillStyle style = {bandPtr->foreground, bandPtr->background,
            (bandPtr->stipple == PATTERN_SOLID) ? None : bandPtr->stipple,
            bandPtr->opacity, bandPtr->tile != NULL};
        Rbc_RenderContext *ctx = Rbc_RenderBeginExportFill(graphPtr, exportPtr, &style);

        if (bandPtr->tile != NULL) {
            Rbc_RenderSetFillTile(ctx, bandPtr->tile);
        }
        for (polygonPtr = bandPtr->polygons; polygonPtr != NULL; polygonPtr = polygonPtr->nextPtr) {
            Rbc_RenderFillPolygon(ctx, polygonPtr->points, polygonPtr->count);
        }
        Rbc_RenderEnd(ctx);
    }
    /* Draw lines */
    if (linePtr->nStrips > 0) {
        for (linkPtr = Rbc_ChainFirstLink(linePtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            penPtr = stylePtr->penPtr;
            if ((stylePtr->nStrips > 0) && (penPtr->traceWidth > 0)) {
                Rbc_RenderContext *ctx = BeginLineExport(exportPtr, penPtr);

                Rbc_RenderSegments(ctx, stylePtr->strips, stylePtr->nStrips);
                Rbc_RenderEnd(ctx);
            }
        }
    } else if ((Rbc_ChainGetLength(linePtr->traces) > 0) && (normalPenPtr->traceWidth > 0)) {
        TracesExport(exportPtr, linePtr, normalPenPtr);
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
            Rbc_RenderContext *ctx;

            ctx = Rbc_RenderBeginExport(exportPtr, colorPtr, penPtr->errorBarLineWidth, NULL, CapButt, JoinMiter);
            Rbc_RenderSegments(ctx, stylePtr->xErrorBars, stylePtr->xErrorBarCnt);
            Rbc_RenderEnd(ctx);
        }
        if ((stylePtr->yErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_Y)) {
            Rbc_RenderContext *ctx;

            ctx = Rbc_RenderBeginExport(exportPtr, colorPtr, penPtr->errorBarLineWidth, NULL, CapButt, JoinMiter);
            Rbc_RenderSegments(ctx, stylePtr->yErrorBars, stylePtr->yErrorBarCnt);
            Rbc_RenderEnd(ctx);
        }
        if ((stylePtr->nSymbolPts > 0) && (stylePtr->penPtr->symbol.type != SYMBOL_NONE)) {
            SymbolsExport(graphPtr, exportPtr, penPtr, stylePtr->symbolSize, stylePtr->nSymbolPts,
                                stylePtr->symbolPts, interval, &counter);
        } else {
            counter += stylePtr->nSymbolPts;
        }
        if (penPtr->valueShow != SHOW_NONE) {
            ValuesExport(exportPtr, linePtr, penPtr, stylePtr->nSymbolPts, stylePtr->symbolPts,
                               linePtr->symbolToData + count, interval, count);
        }
        count += stylePtr->nSymbolPts;
    }
}

/**
 * @ingroup line_geometry_lifecycle
 * @brief Release resources owned by a line, strip or polar element.
 *
 * Releases named pen references, built-in pen resources, data subscriptions,
 * decimation cache, mapped geometry, palette, tags, activation indices and
 * fill/error-band drawing resources. External source vectors and images
 * are not destroyed.
 *
 * The common element lifecycle guards this callback against repeated teardown;
 * not every field is cleared here. It owns record/name and Tk option cleanup
 * outside this callback. Requires a live display for drawing-resource release.
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
    Rbc_FreeElemVector(&linePtr->param);
    FreeComplexVector(&linePtr->z);
    InvalidateLineDecimateCache(linePtr);
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
    if (linePtr->errorBand.tile != NULL) {
        Rbc_FreeTile(linePtr->errorBand.tile);
    }
    FreeLinePattern(graphPtr->display, linePtr->errorBand.stipple);
    if (linePtr->errorBand.gc != NULL) {
        Tk_FreeGC(graphPtr->display, linePtr->errorBand.gc);
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
    ActiveLineExport, /* Prints active element. */
    NormalLineExport, /* Prints normal element. */
    SymbolExport,     /* Prints the line's symbol. */
    MapLine,                /* Compute element's screen coordinates. */
    NULL,                   /* Ordinary X/Y point count. */
    LineClosestInfo         /* Optional parameter information. */
};

static ElementProcs polarLineProcs = {
    ClosestLine,
    ConfigureLine,
    DestroyLine,
    DrawActiveLine,
    DrawNormalLine,
    DrawSymbol,
    GetLineExtents,
    ActiveLineExport,
    NormalLineExport,
    SymbolExport,
    MapLine,
    LinePointCount,  /* X/Y or complex data. */
    PolarClosestInfo /* Polar/Smith closest information. */
};

/**
 * @ingroup line_geometry_lifecycle
 * @brief Allocate an unregistered line-family element.
 *
 * Returns NULL if record allocation fails. Selects line, polar or strip
 * specifications and the corresponding dispatch table, duplicates name and
 * label, initializes the built-in normal pen and creates the palette.
 * Starts with XY data, no requested smoothing and pending symbol-scale
 * baseline capture. Caller owns registration, Tk option initialization and
 * configuration of the returned embedded Element pointer.
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
    } else if (classUid == rbcPolarElementUid) {
        elemPtr->optionSpecs = polarElemOptionSpecs;
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
    if (classUid == rbcPolarElementUid) {
        elemPtr->procsPtr = &polarLineProcs;
    } else {
        elemPtr->procsPtr = &lineProcs;
    }
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
    linePtr->dataMode = LINE_DATA_XY;
    return elemPtr;
}

/**
 * @ingroup line_geometry_lifecycle
 * @brief Invalidate cached value-label text styles after font changes.
 *
 * Visits registered line pens and built-in pens of all line, strip and
 * polar elements, including hidden elements. Resets styles with a non-NULL
 * font. Shared named pens are visited through the registry rather than once
 * per element. Borrows a live graph/window; does not itself schedule redraw
 * or layout.
 */
void Rbc_LineFontsChanged(Graph *graphPtr) {
    Tcl_HashSearch iter;
    Tcl_HashEntry *entryPtr;

    for (entryPtr = Tcl_FirstHashEntry(&graphPtr->penTable, &iter); entryPtr != NULL;
         entryPtr = Tcl_NextHashEntry(&iter)) {
        Pen *penPtr = Tcl_GetHashValue(entryPtr);

        if (penPtr->classUid == rbcLineElementUid) {
            LinePen *typedPtr = LINE_PEN_FROM_CORE(penPtr);

            if (typedPtr->valueStyle.font != NULL) {
                Rbc_ResetTextStyle(graphPtr->tkwin, &typedPtr->valueStyle);
            }
        }
    }
    for (entryPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &iter); entryPtr != NULL;
         entryPtr = Tcl_NextHashEntry(&iter)) {
        Element *elemPtr = Tcl_GetHashValue(entryPtr);

        if ((elemPtr->classUid == rbcLineElementUid) || (elemPtr->classUid == rbcStripElementUid) ||
            (elemPtr->classUid == rbcPolarElementUid)) {
            Line *typedPtr = LINE_FROM_CORE(elemPtr);

            if (typedPtr->builtinPen.valueStyle.font != NULL) {
                Rbc_ResetTextStyle(graphPtr->tkwin, &typedPtr->builtinPen.valueStyle);
            }
        }
    }
}
