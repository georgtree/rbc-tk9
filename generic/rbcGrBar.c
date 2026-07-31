/*
 * rbcGrBar.c --
 *
 *      This module implements barchart elements for the rbc graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcGraph.h"
#include <X11/Xutil.h>

#include "rbcGrElem.h"

typedef struct {
    /*
     * Common pen state. This must remain the first member.
     */
    Pen core;

    /*
     * Original Tcl representations for values requiring additional
     * validation or conversion.
     */
    Tcl_Obj *borderWidthObjPtr;
    Tcl_Obj *errorBarCapObjPtr;
    Tcl_Obj *errorBarColorObjPtr;
    Tcl_Obj *errorBarWidthObjPtr;
    Tcl_Obj *showErrorBarsObjPtr;
    Tcl_Obj *showValuesObjPtr;
    Tcl_Obj *valueRotateObjPtr;
    Tcl_Obj *valueShadowObjPtr;

    XColor *fgColor;
    Tk_3DBorder border;
    int borderWidth;
    int relief;
    Pixmap stipple;
    GC gc;

    int errorBarShow;
    int errorBarLineWidth;
    int errorBarCapWidth;
    XColor *errorBarColor;
    GC errorBarGC;

    int valueShow;
    char *valueFormat;
    TextStyle valueStyle;
} BarPen;

#define BAR_PEN_FROM_CORE(penPtr) ((BarPen *)((char *)(penPtr) - offsetof(BarPen, core)))

#define BAR_PEN_CORE_OFFSET(member) (offsetof(BarPen, core) + offsetof(Pen, member))

typedef struct {
    Weight weight; /* Weight range where this pen is valid. */

    BarPen *penPtr; /* Pen to draw */

    Segment2D *xErrorBars; /* Point to start of this pen's X-error bar
                            * segments in the element's array. */

    Segment2D *yErrorBars; /* Point to start of this pen's Y-error bar
                            * segments in the element's array. */
    int xErrorBarCnt;      /* # of error bars for this pen. */

    int yErrorBarCnt; /* # of error bars for this pen. */

    int errorBarCapWidth; /* Length of the cap ends on each
                           * error bar. */

    int symbolSize; /* Size of the pen's symbol scaled to the
                     * current graph size. */

    /* Bar chart specific data. */
    XRectangle *rectangles; /* Indicates starting location in bar
                             * array for this pen. */
    int nRects;             /* Number of bar segments for this pen. */

} BarPenStyle;

typedef struct {
    /*
     * Common element state. This must remain the first member.
     */
    Element core;
    
    /*
     * Bar specific attributes
     */
    BarPen builtinPen;

    int *rectToData;
    XRectangle *rectangles; /* Array of rectangles comprising the bar
                             * segments of the element. */
    int nRects;             /* # of visible bar segments for element */

    int padX; /* Spacing on either side of bar */
    double barWidth;
    int nActive;

    XRectangle *activeRects;
    int *activeToData;
} Bar;

_Static_assert(offsetof(Bar, core) == 0, "Element core must be the first Bar member");

#define BAR_FROM_CORE(elemPtr) ((Bar *)((char *)(elemPtr) - offsetof(Bar, core)))

#define BAR_CORE_OFFSET(member) (offsetof(Bar, core) + offsetof(Element, member))
#define BAR_BUILTIN_PEN_OFFSET(member) (offsetof(Bar, builtinPen) + offsetof(BarPen, member))

#define DEF_BAR_ACTIVE_PEN "activeBar"
#define DEF_BAR_AXIS_X "x"
#define DEF_BAR_AXIS_Y "y"
#define DEF_BAR_BACKGROUND "navyblue"
#define DEF_BAR_BORDERWIDTH "2"
#define DEF_BAR_DATA (char *)NULL
#define DEF_BAR_ERRORBAR_COLOR "defcolor"
#define DEF_BAR_ERRORBAR_LINE_WIDTH "1"
#define DEF_BAR_ERRORBAR_CAP_WIDTH "1"
#define DEF_BAR_FOREGROUND "blue"
#define DEF_BAR_HIDE "no"
#define DEF_BAR_LABEL (char *)NULL
#define DEF_BAR_LABEL_RELIEF "flat"
#define DEF_BAR_NORMAL_STIPPLE ""
#define DEF_BAR_RELIEF "raised"
#define DEF_BAR_SHOW_ERRORBARS "both"
#define DEF_BAR_STATE "normal"
#define DEF_BAR_STYLES ""
#define DEF_BAR_TAGS "all"
#define DEF_BAR_WIDTH "0.0"

#define DEF_PEN_ACTIVE_BACKGROUND "red"
#define DEF_PEN_ACTIVE_FOREGROUND "pink"
#define DEF_PEN_BORDERWIDTH "2"
#define DEF_PEN_NORMAL_BACKGROUND "navyblue"
#define DEF_PEN_NORMAL_FOREGROUND "blue"
#define DEF_PEN_RELIEF "raised"
#define DEF_PEN_STIPPLE ""
#define DEF_PEN_TYPE "bar"
#define DEF_PEN_VALUE_ANCHOR "s"
#define DEF_PEN_VALUE_COLOR RGB_BLACK
#define DEF_PEN_VALUE_FONT STD_FONT_SMALL
#define DEF_PEN_VALUE_FORMAT "%g"
#define DEF_PEN_VALUE_SHADOW (char *)NULL
#define DEF_PEN_SHOW_VALUES "no"

/*
 * Bar-element option conversion masks.
 *
 * These bits describe post-Tk_SetOptions processing required by
 * ConfigureBar. Several bits may be set by one option.
 */
#define BAR_ELEM_PEN_MASK (1 << 0)
#define BAR_ELEM_TAGS_MASK (1 << 1)
#define BAR_ELEM_DATA_MASK (1 << 2)
#define BAR_ELEM_AXES_MASK (1 << 3)
#define BAR_ELEM_STATE_MASK (1 << 4)
#define BAR_ELEM_STYLES_MASK (1 << 5)
#define BAR_ELEM_BUILTIN_PEN_MASK (1 << 6)
#define BAR_ELEM_MAP_ITEM_MASK (1 << 7)

static const Tk_OptionSpec barElemOptionSpecs[] = {
    {TK_OPTION_STRING, "-activepen", "activePen", "ActivePen", DEF_BAR_ACTIVE_PEN, BAR_CORE_OFFSET(activePenObjPtr), -1,
     0, NULL, BAR_ELEM_PEN_MASK},
    {TK_OPTION_BORDER, "-background", "background", "Background", DEF_BAR_BACKGROUND, -1,
     BAR_BUILTIN_PEN_OFFSET(border), TK_OPTION_NULL_OK, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_DOUBLE, "-barwidth", "barWidth", "BarWidth", DEF_BAR_WIDTH, -1, offsetof(Bar, barWidth),
     TK_OPTION_DONT_SET_DEFAULT, NULL, BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_SYNONYM, "-bd", NULL, NULL, NULL, -1, -1, 0, "-borderwidth", 0},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL, NULL, -1, -1, 0, "-background", 0},
    {TK_OPTION_STRING, "-bindtags", "bindTags", "BindTags", DEF_BAR_TAGS, BAR_CORE_OFFSET(bindTagsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, BAR_ELEM_TAGS_MASK},
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth", DEF_BAR_BORDERWIDTH,
     BAR_BUILTIN_PEN_OFFSET(borderWidthObjPtr), -1, 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING, "-errorbarcolor", "errorBarColor", "ErrorBarColor", DEF_BAR_ERRORBAR_COLOR,
     BAR_BUILTIN_PEN_OFFSET(errorBarColorObjPtr), -1, 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_PIXELS, "-errorbarwidth", "errorBarWidth", "ErrorBarWidth", DEF_BAR_ERRORBAR_LINE_WIDTH,
     BAR_BUILTIN_PEN_OFFSET(errorBarWidthObjPtr), -1, 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_PIXELS, "-errorbarcap", "errorBarCap", "ErrorBarCap", DEF_BAR_ERRORBAR_CAP_WIDTH,
     BAR_BUILTIN_PEN_OFFSET(errorBarCapObjPtr), -1, 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-data", "data", "Data", NULL, BAR_CORE_OFFSET(dataObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     BAR_ELEM_DATA_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_SYNONYM, "-fg", NULL, NULL, NULL, -1, -1, 0, "-foreground", 0},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground", DEF_BAR_FOREGROUND, -1,
     BAR_BUILTIN_PEN_OFFSET(fgColor), TK_OPTION_NULL_OK, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_BAR_HIDE, -1, BAR_CORE_OFFSET(hidden), TK_OPTION_DONT_SET_DEFAULT,
     NULL, BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-label", "label", "Label", DEF_BAR_LABEL, -1, BAR_CORE_OFFSET(label), TK_OPTION_NULL_OK, NULL,
     BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_RELIEF, "-labelrelief", "labelRelief", "LabelRelief", DEF_BAR_LABEL_RELIEF, -1,
     BAR_CORE_OFFSET(labelRelief), TK_OPTION_DONT_SET_DEFAULT, NULL, BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-mapx", "mapX", "MapX", DEF_BAR_AXIS_X, BAR_CORE_OFFSET(mapXObjPtr), -1, 0, NULL,
     BAR_ELEM_AXES_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-mapy", "mapY", "MapY", DEF_BAR_AXIS_Y, BAR_CORE_OFFSET(mapYObjPtr), -1, 0, NULL,
     BAR_ELEM_AXES_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-pen", "pen", "Pen", NULL, BAR_CORE_OFFSET(normalPenObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     BAR_ELEM_PEN_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief", DEF_BAR_RELIEF, -1, BAR_BUILTIN_PEN_OFFSET(relief), 0, NULL,
     BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING, "-showerrorbars", "showErrorBars", "ShowErrorBars", DEF_BAR_SHOW_ERRORBARS,
     BAR_BUILTIN_PEN_OFFSET(showErrorBarsObjPtr), -1, 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING, "-showvalues", "showValues", "ShowValues", DEF_PEN_SHOW_VALUES,
     BAR_BUILTIN_PEN_OFFSET(showValuesObjPtr), -1, 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING, "-state", "state", "State", DEF_BAR_STATE, BAR_CORE_OFFSET(stateObjPtr), -1, 0, NULL,
     BAR_ELEM_STATE_MASK},
    {TK_OPTION_BITMAP, "-stipple", "stipple", "Stipple", DEF_BAR_NORMAL_STIPPLE, -1, BAR_BUILTIN_PEN_OFFSET(stipple),
     TK_OPTION_NULL_OK, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING, "-styles", "styles", "Styles", DEF_BAR_STYLES, BAR_CORE_OFFSET(stylesObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, BAR_ELEM_STYLES_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_ANCHOR, "-valueanchor", "valueAnchor", "ValueAnchor", DEF_PEN_VALUE_ANCHOR, -1,
     BAR_BUILTIN_PEN_OFFSET(valueStyle.anchor), 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_COLOR, "-valuecolor", "valueColor", "ValueColor", DEF_PEN_VALUE_COLOR, -1,
     BAR_BUILTIN_PEN_OFFSET(valueStyle.color), 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_FONT, "-valuefont", "valueFont", "ValueFont", DEF_PEN_VALUE_FONT, -1,
     BAR_BUILTIN_PEN_OFFSET(valueStyle.font), 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING, "-valueformat", "valueFormat", "ValueFormat", DEF_PEN_VALUE_FORMAT, -1,
     BAR_BUILTIN_PEN_OFFSET(valueFormat), TK_OPTION_NULL_OK, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_DOUBLE, "-valuerotate", "valueRotate", "ValueRotate", "0.0", BAR_BUILTIN_PEN_OFFSET(valueRotateObjPtr),
     BAR_BUILTIN_PEN_OFFSET(valueStyle.theta), 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING, "-valueshadow", "valueShadow", "ValueShadow", DEF_PEN_VALUE_SHADOW,
     BAR_BUILTIN_PEN_OFFSET(valueShadowObjPtr), -1, TK_OPTION_NULL_OK, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING, "-weights", "weights", "Weights", NULL, BAR_CORE_OFFSET(weightsObjPtr), -1, TK_OPTION_NULL_OK,
     NULL, BAR_ELEM_DATA_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-x", "xdata", "Xdata", DEF_BAR_DATA, BAR_CORE_OFFSET(xObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     BAR_ELEM_DATA_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_SYNONYM, "-xdata", NULL, NULL, NULL, -1, -1, 0, "-x", 0},
    {TK_OPTION_STRING, "-y", "ydata", "Ydata", DEF_BAR_DATA, BAR_CORE_OFFSET(yObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     BAR_ELEM_DATA_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_SYNONYM, "-ydata", NULL, NULL, NULL, -1, -1, 0, "-y", 0},
    {TK_OPTION_STRING, "-xerror", "xError", "XError", DEF_BAR_DATA, BAR_CORE_OFFSET(xErrorObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, BAR_ELEM_DATA_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-xhigh", "xHigh", "XHigh", DEF_BAR_DATA, BAR_CORE_OFFSET(xHighObjPtr), -1, TK_OPTION_NULL_OK,
     NULL, BAR_ELEM_DATA_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-xlow", "xLow", "XLow", DEF_BAR_DATA, BAR_CORE_OFFSET(xLowObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     BAR_ELEM_DATA_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-yerror", "yError", "YError", DEF_BAR_DATA, BAR_CORE_OFFSET(yErrorObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, BAR_ELEM_DATA_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-yhigh", "yHigh", "YHigh", DEF_BAR_DATA, BAR_CORE_OFFSET(yHighObjPtr), -1, TK_OPTION_NULL_OK,
     NULL, BAR_ELEM_DATA_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-ylow", "yLow", "YLow", DEF_BAR_DATA, BAR_CORE_OFFSET(yLowObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     BAR_ELEM_DATA_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

#define BAR_PEN_OPTION_ENTRIES(DEFAULT_BG, DEFAULT_FG)                  \
    {                                                                  \
        TK_OPTION_BORDER,                                              \
        "-background", "background", "Background",                     \
        DEFAULT_BG,                                                    \
        -1,                                                            \
        offsetof(BarPen, border),                                      \
        TK_OPTION_NULL_OK,                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_SYNONYM,                                             \
        "-bg", NULL, NULL, NULL,                                       \
        -1, -1, 0,                                                     \
        "-background",                                                 \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_PIXELS,                                              \
        "-borderwidth", "borderWidth", "BorderWidth",                  \
        DEF_PEN_BORDERWIDTH,                                           \
        offsetof(BarPen, borderWidthObjPtr),                           \
        -1,                                                            \
        0,                                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_SYNONYM,                                             \
        "-bd", NULL, NULL, NULL,                                       \
        -1, -1, 0,                                                     \
        "-borderwidth",                                                \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_STRING,                                              \
        "-errorbarcolor", "errorBarColor", "ErrorBarColor",            \
        DEF_BAR_ERRORBAR_COLOR,                                        \
        offsetof(BarPen, errorBarColorObjPtr),                         \
        -1,                                                            \
        0,                                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_PIXELS,                                              \
        "-errorbarwidth", "errorBarWidth", "ErrorBarWidth",            \
        DEF_BAR_ERRORBAR_LINE_WIDTH,                                   \
        offsetof(BarPen, errorBarWidthObjPtr),                         \
        -1,                                                            \
        0,                                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_PIXELS,                                              \
        "-errorbarcap", "errorBarCap", "ErrorBarCap",                  \
        DEF_BAR_ERRORBAR_CAP_WIDTH,                                    \
        offsetof(BarPen, errorBarCapObjPtr),                           \
        -1,                                                            \
        0,                                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_COLOR,                                               \
        "-foreground", "foreground", "Foreground",                     \
        DEFAULT_FG,                                                    \
        -1,                                                            \
        offsetof(BarPen, fgColor),                                     \
        TK_OPTION_NULL_OK,                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_SYNONYM,                                             \
        "-fg", NULL, NULL, NULL,                                       \
        -1, -1, 0,                                                     \
        "-foreground",                                                 \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_RELIEF,                                              \
        "-relief", "relief", "Relief",                                 \
        DEF_PEN_RELIEF,                                                \
        -1,                                                            \
        offsetof(BarPen, relief),                                      \
        0,                                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_STRING,                                              \
        "-showerrorbars", "showErrorBars", "ShowErrorBars",            \
        DEF_BAR_SHOW_ERRORBARS,                                        \
        offsetof(BarPen, showErrorBarsObjPtr),                         \
        -1,                                                            \
        0,                                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_STRING,                                              \
        "-showvalues", "showValues", "ShowValues",                     \
        DEF_PEN_SHOW_VALUES,                                           \
        offsetof(BarPen, showValuesObjPtr),                            \
        -1,                                                            \
        0,                                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_BITMAP,                                              \
        "-stipple", "stipple", "Stipple",                              \
        DEF_PEN_STIPPLE,                                               \
        -1,                                                            \
        offsetof(BarPen, stipple),                                     \
        TK_OPTION_NULL_OK,                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_STRING,                                              \
        "-type", NULL, NULL,                                           \
        DEF_PEN_TYPE,                                                  \
        -1,                                                            \
        BAR_PEN_CORE_OFFSET(typeId),                                   \
        TK_OPTION_NULL_OK,                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_ANCHOR,                                              \
        "-valueanchor", "valueAnchor", "ValueAnchor",                  \
        DEF_PEN_VALUE_ANCHOR,                                          \
        -1,                                                            \
        offsetof(BarPen, valueStyle.anchor),                           \
        0,                                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_COLOR,                                               \
        "-valuecolor", "valueColor", "ValueColor",                     \
        DEF_PEN_VALUE_COLOR,                                           \
        -1,                                                            \
        offsetof(BarPen, valueStyle.color),                            \
        0,                                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_FONT,                                                \
        "-valuefont", "valueFont", "ValueFont",                        \
        DEF_PEN_VALUE_FONT,                                            \
        -1,                                                            \
        offsetof(BarPen, valueStyle.font),                             \
        0,                                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_STRING,                                              \
        "-valueformat", "valueFormat", "ValueFormat",                  \
        DEF_PEN_VALUE_FORMAT,                                          \
        -1,                                                            \
        offsetof(BarPen, valueFormat),                                 \
        TK_OPTION_NULL_OK,                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_DOUBLE,                                              \
        "-valuerotate", "valueRotate", "ValueRotate",                  \
        "0.0",                                                         \
        offsetof(BarPen, valueRotateObjPtr),                           \
        offsetof(BarPen, valueStyle.theta),                            \
        0,                                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_STRING,                                              \
        "-valueshadow", "valueShadow", "ValueShadow",                  \
        DEF_PEN_VALUE_SHADOW,                                          \
        offsetof(BarPen, valueShadowObjPtr),                           \
        -1,                                                            \
        TK_OPTION_NULL_OK,                                             \
        NULL,                                                          \
        0                                                              \
    },                                                                 \
    {                                                                  \
        TK_OPTION_END,                                                 \
        NULL, NULL, NULL, NULL,                                        \
        0, 0, 0, NULL, 0                                              \
    }

static const Tk_OptionSpec normalBarPenOptionSpecs[] = {
    BAR_PEN_OPTION_ENTRIES(DEF_PEN_NORMAL_BACKGROUND, DEF_PEN_NORMAL_FOREGROUND)};

static const Tk_OptionSpec activeBarPenOptionSpecs[] = {
    BAR_PEN_OPTION_ENTRIES(DEF_PEN_ACTIVE_BACKGROUND, DEF_PEN_ACTIVE_FOREGROUND)};

/* Forward declarations */
static PenConfigureProc ConfigurePen;
static PenDestroyProc DestroyPen;
static ElementClosestProc ClosestBar;
static ElementConfigProc ConfigureBar;
static ElementDestroyProc DestroyBar;
static ElementDrawProc DrawActiveBar;
static ElementDrawProc DrawNormalBar;
static ElementDrawSymbolProc DrawSymbol;
static ElementExtentsProc GetBarExtents;
static ElementToPostScriptProc ActiveBarToPostScript;
static ElementToPostScriptProc NormalBarToPostScript;
static ElementSymbolToPostScriptProc SymbolToPostScript;
static ElementMapProc MapBar;

static int Round(register double x);
static void ClearPalette(Rbc_Chain *palette);
static void InitPen(BarPen *penPtr, const Tk_OptionSpec *optionSpecs, unsigned int flags);
static void CheckStacks(Graph *graphPtr, Axis2D *pairPtr, double *minPtr, double *maxPtr);
static void MergePens(Bar *barPtr, PenStyle **dataToStyle);
static void MapActiveBars(Bar *barPtr);
static void ResetBar(Bar *barPtr);

static void DrawBarSegments(Graph *graphPtr, Drawable drawable, BarPen *penPtr, XRectangle *rectangles, int nRects);
static void DrawBarValues(Graph *graphPtr, Drawable drawable, Bar *barPtr, BarPen *penPtr, XRectangle *rectangles,
                          int nRects, int *rectToData);
static void SegmentsToPostScript(Graph *graphPtr, PsToken psToken, BarPen *penPtr, register XRectangle *rectPtr,
                                 int nRects);
static void BarValuesToPostScript(Graph *graphPtr, PsToken psToken, Bar *barPtr, BarPen *penPtr, XRectangle *rectangles,
                                  int nRects, int *rectToData);

static int IsBarPenPrefix(const char *string, Tcl_Size length, const char *fullName) {
    Tcl_Size fullLength;

    fullLength = (Tcl_Size)strlen(fullName);

    return ((length > 0) && (length <= fullLength) && (strncmp(string, fullName, (size_t)length) == 0));
}

static int GetBarPenColorFromObj(Tcl_Interp *interp, Tk_Window tkwin, Tcl_Obj *objPtr, XColor **colorPtrPtr) {
    const char *string;
    Tcl_Size length;
    XColor *colorPtr;

    if ((objPtr == NULL) || (Tcl_GetCharLength(objPtr) == 0)) {
        *colorPtrPtr = NULL;
        return TCL_OK;
    }

    string = Tcl_GetStringFromObj(objPtr, &length);

    if (IsBarPenPrefix(string, length, "defcolor")) {
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

static void FreeBarPenColor(XColor *colorPtr) {
    if ((colorPtr != NULL) && (colorPtr != COLOR_DEFAULT)) {
        Tk_FreeColor(colorPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Round --
 *
 *      TODO: Description
 *
 * Parameters:
 *      x
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int Round(register double x) { return (int)(x + ((x < 0.0) ? -0.5 : 0.5)); }

/*
 * ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * ClearPalette --
 *
 *      Zero out the style's number of rectangles and errorbars.
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
    register BarPenStyle *stylePtr;
    Rbc_ChainLink *linkPtr;

    for (linkPtr = Rbc_ChainFirstLink(palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        stylePtr->xErrorBarCnt = stylePtr->yErrorBarCnt = stylePtr->nRects = 0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigurePen --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Pen *penPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ConfigurePen(Graph *graphPtr, Pen *penPtr) {
    BarPen *bpPtr;
    XColor *newErrorBarColor;

    assert(penPtr->optionSpecs != NULL);

    bpPtr = BAR_PEN_FROM_CORE(penPtr);
    Shadow newShadow;

    int newBorderWidth;
    int newErrorBarCapWidth;
    int newErrorBarLineWidth;
    int newErrorBarShow;
    int newValueShow;

    XGCValues gcValues;
    unsigned long gcMask;
    int fillStyle;
    long defColor;

    GC newGC;
    GC newErrorBarGC;
    GC newValueGC;

    newErrorBarColor = NULL;

    newShadow.color = NULL;
    newShadow.offset = 0;

    /*
     * Parse and validate every fallible derived value first.
     */
    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, bpPtr->borderWidthObjPtr, PIXELS_NONNEGATIVE,
                             &newBorderWidth) != TCL_OK) {
        goto error;
    }

    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, bpPtr->errorBarWidthObjPtr, PIXELS_NONNEGATIVE,
                             &newErrorBarLineWidth) != TCL_OK) {
        goto error;
    }

    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, bpPtr->errorBarCapObjPtr, PIXELS_NONNEGATIVE,
                             &newErrorBarCapWidth) != TCL_OK) {
        goto error;
    }

    if (GetBarPenColorFromObj(graphPtr->interp, graphPtr->tkwin, bpPtr->errorBarColorObjPtr, &newErrorBarColor) !=
        TCL_OK) {
        goto error;
    }

    if (Rbc_GetFillFromObj(graphPtr->interp, bpPtr->showErrorBarsObjPtr, &newErrorBarShow) != TCL_OK) {
        goto error;
    }

    if (Rbc_GetFillFromObj(graphPtr->interp, bpPtr->showValuesObjPtr, &newValueShow) != TCL_OK) {
        goto error;
    }

    if (Rbc_GetShadowFromObj(graphPtr->interp, graphPtr->tkwin, bpPtr->valueShadowObjPtr, &newShadow) != TCL_OK) {
        goto error;
    }

    /*
     * No fallible operation remains. Build all replacement GCs before
     * freeing anything currently in use.
     */
    gcMask = GCFont;
    gcValues.font = Tk_FontId(bpPtr->valueStyle.font);

    if (bpPtr->valueStyle.color != NULL) {
        gcMask |= GCForeground;
        gcValues.foreground = bpPtr->valueStyle.color->pixel;
    }

    newValueGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);

    gcMask = GCForeground;

    if (bpPtr->fgColor != NULL) {
        defColor = bpPtr->fgColor->pixel;
    } else if (bpPtr->border != NULL) {
        defColor = Tk_3DBorderColor(bpPtr->border)->pixel;
    } else {
        defColor = BlackPixel(graphPtr->display, Tk_ScreenNumber(graphPtr->tkwin));
    }

    gcValues.foreground = defColor;

    if ((bpPtr->fgColor != NULL) && (bpPtr->border != NULL)) {
        gcMask |= GCBackground;
        gcValues.background = Tk_3DBorderColor(bpPtr->border)->pixel;

        fillStyle = FillOpaqueStippled;
    } else {
        fillStyle = FillStippled;
    }

    if (bpPtr->stipple != None) {
        gcValues.stipple = bpPtr->stipple;
        gcValues.fill_style = fillStyle;
        gcMask |= GCStipple | GCFillStyle;
    }

    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);

    gcMask = GCForeground | GCLineWidth;

    if ((newErrorBarColor == NULL) || (newErrorBarColor == COLOR_DEFAULT)) {
        gcValues.foreground = defColor;
    } else {
        gcValues.foreground = newErrorBarColor->pixel;
    }

    gcValues.line_width = LineWidth(newErrorBarLineWidth);

    newErrorBarGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);

    /*
     * Commit derived values.
     */
    FreeBarPenColor(bpPtr->errorBarColor);

    if (bpPtr->valueStyle.shadow.color != NULL) {
        Tk_FreeColor(bpPtr->valueStyle.shadow.color);
    }

    bpPtr->borderWidth = newBorderWidth;

    bpPtr->errorBarCapWidth = newErrorBarCapWidth;

    bpPtr->errorBarLineWidth = newErrorBarLineWidth;

    bpPtr->errorBarShow = newErrorBarShow;

    bpPtr->valueShow = newValueShow;

    bpPtr->errorBarColor = newErrorBarColor;

    bpPtr->valueStyle.shadow = newShadow;

    newErrorBarColor = NULL;
    newShadow.color = NULL;

    /*
     * Commit replacement GCs.
     */
    if (bpPtr->valueStyle.gc != NULL) {
        Tk_FreeGC(graphPtr->display, bpPtr->valueStyle.gc);
    }

    if (bpPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, bpPtr->gc);
    }

    if (bpPtr->errorBarGC != NULL) {
        Tk_FreeGC(graphPtr->display, bpPtr->errorBarGC);
    }

    bpPtr->valueStyle.gc = newValueGC;

    bpPtr->gc = newGC;

    bpPtr->errorBarGC = newErrorBarGC;

    return TCL_OK;

error:
    FreeBarPenColor(newErrorBarColor);

    if (newShadow.color != NULL) {
        Tk_FreeColor(newShadow.color);
    }

    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * DestroyPen --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Pen *penPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DestroyPen(Graph *graphPtr, Pen *penPtr) {
    BarPen *bpPtr;

    bpPtr = BAR_PEN_FROM_CORE(penPtr);

    Rbc_FreeTextStyle(graphPtr->display, &bpPtr->valueStyle);

    bpPtr->valueStyle.gc = NULL;

    if (bpPtr->gc != NULL) {
        Tk_FreeGC(graphPtr->display, bpPtr->gc);

        bpPtr->gc = NULL;
    }

    if (bpPtr->errorBarGC != NULL) {
        Tk_FreeGC(graphPtr->display, bpPtr->errorBarGC);

        bpPtr->errorBarGC = NULL;
    }

    /*
     * These resources are derived manually from the retained option
     * objects and therefore are not released by Tk_FreeConfigOptions.
     */
    FreeBarPenColor(bpPtr->errorBarColor);
    bpPtr->errorBarColor = NULL;

    if (bpPtr->valueStyle.shadow.color != NULL) {
        Tk_FreeColor(bpPtr->valueStyle.shadow.color);
        bpPtr->valueStyle.shadow.color = NULL;
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
 *      BarPen *penPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void InitPen(BarPen *penPtr, const Tk_OptionSpec *optionSpecs, unsigned int flags) {
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

    penPtr->relief = TK_RELIEF_RAISED;
    penPtr->errorBarShow = SHOW_BOTH;
    penPtr->valueShow = SHOW_NONE;
    penPtr->borderWidth = 2;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_BarPen --
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
Pen *Rbc_BarPen(const char *penName) {
    const Tk_OptionSpec *optionSpecs;
    unsigned int flags;
    BarPen *penPtr;
    Pen *corePtr;

    if (strcmp(penName, "activeBar") == 0) {
        optionSpecs = activeBarPenOptionSpecs;
        flags = ACTIVE_PEN;
    } else {
        optionSpecs = normalBarPenOptionSpecs;
        flags = NORMAL_PEN;
    }

    penPtr = RbcCalloc(1, sizeof(BarPen));
    assert(penPtr != NULL);

    InitPen(penPtr, optionSpecs, flags);

    corePtr = &penPtr->core;
    corePtr->name = RbcStrdup(penName);

    return corePtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * CheckStacks --
 *
 *      Check that the data limits are not superseded by the heights
 *      of stacked bar segments.  The heights are calculated by
 *      Rbc_ComputeStacks.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis2D *pairPtr
 *      double *minPtr - Current minimum for y-axis
 *      double *maxPtr - Current maximum for y-axis
 *
 * Results:
 *      If the y-axis limits need to be adjusted for stacked segments,
 *      *minPtr* or *maxPtr* are updated.
 *
 * Side effects:
 *      Autoscaling of the y-axis is affected.
 *
 * ----------------------------------------------------------------------
 */
static void CheckStacks(Graph *graphPtr, Axis2D *pairPtr, double *minPtr, double *maxPtr) {
    FreqInfo *infoPtr;
    register int i;

    if ((graphPtr->mode != MODE_STACKED) || (graphPtr->nStacks == 0)) {
        return;
    }
    infoPtr = graphPtr->freqArr;
    for (i = 0; i < graphPtr->nStacks; i++) {
        if ((infoPtr->axes.x == pairPtr->x) && (infoPtr->axes.y == pairPtr->y)) {
            /*

             * Check if any of the y-values (because of stacking) are
             * greater than the current limits of the graph.
             */
            if (infoPtr->sum < 0.0) {
                if (*minPtr > infoPtr->sum) {
                    *minPtr = infoPtr->sum;
                }
            } else {
                if (*maxPtr < infoPtr->sum) {
                    *maxPtr = infoPtr->sum;
                }
            }
        }
        infoPtr++;
    }
}


/*
 * ----------------------------------------------------------------------
 *
 * ConfigureBar --
 *
 * Tk_SetOptions has installed the candidate option values.
 * This function validates retained values and constructs the
 * replacement derived resources transactionally.
 *
 * Parameters:
 *      Graph *graphPtr
 *      register Element *elemPtr
 *
 * Results:
 *      The return value is a standard Tcl result.  If TCL_ERROR is
 *      returned, then the interpreter result contains an error message.
 *
 * Side effects:
 *      Configuration information such as bar foreground/background
 *      color and stipple etc. get set in a new GC.
 *
 * ----------------------------------------------------------------------
 */
static int ConfigureBar(Graph *graphPtr, Element *elemPtr) {
    Bar *barPtr;
    ElemDataTransaction dataTransaction;
    ElemPenTransaction penTransaction;
    ElemAxisTransaction axisTransaction;
    ElemStateTransaction stateTransaction;
    ElemTagsTransaction tagsTransaction;
    ElemStylesTransaction stylesTransaction;
    Rbc_ChainLink *linkPtr;
    int dataTransactionPrepared;
    int penTransactionPrepared;
    int axisTransactionPrepared;
    int stateTransactionPrepared;
    int tagsTransactionPrepared;
    int stylesTransactionPrepared;

    barPtr = BAR_FROM_CORE(elemPtr);

    memset(&dataTransaction, 0, sizeof(dataTransaction));
    memset(&penTransaction, 0, sizeof(penTransaction));
    memset(&axisTransaction, 0, sizeof(axisTransaction));
    memset(&stateTransaction, 0, sizeof(stateTransaction));
    memset(&tagsTransaction, 0, sizeof(tagsTransaction));
    memset(&stylesTransaction, 0, sizeof(stylesTransaction));

    dataTransactionPrepared = FALSE;
    penTransactionPrepared = FALSE;
    axisTransactionPrepared = FALSE;
    stateTransactionPrepared = FALSE;
    tagsTransactionPrepared = FALSE;
    stylesTransactionPrepared = FALSE;

    /*
     * Parse all fallible element-data conversions before modifying any
     * live vectors or derived drawing resources.
     */
    if ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & BAR_ELEM_DATA_MASK)) {
        if (Rbc_PrepareElemDataTransaction(graphPtr, elemPtr, &dataTransaction) != TCL_OK) {
            goto error;
        }

        dataTransactionPrepared = TRUE;
    }

    /*
     * Resolve named pens into temporary references before changing the
     * live active or normal pen.
     */
    if ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & BAR_ELEM_PEN_MASK)) {
        if (Rbc_PrepareElemPenTransaction(graphPtr, elemPtr, rbcBarElementUid, &penTransaction) != TCL_OK) {
            goto error;
        }

        penTransactionPrepared = TRUE;
    }

    /*
     * Resolve X and Y axis mappings before modifying the live element.
     */
    if ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & BAR_ELEM_AXES_MASK)) {
        if (Rbc_PrepareElemAxisTransaction(graphPtr, elemPtr, &axisTransaction) != TCL_OK) {
            goto error;
        }

        axisTransactionPrepared = TRUE;
    }

    /*
     * Validate the element state before modifying any live state or
     * derived drawing resources.
     */
    if ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & BAR_ELEM_STATE_MASK)) {
        if (Rbc_PrepareElemStateTransaction(graphPtr, elemPtr, &stateTransaction) != TCL_OK) {
            goto error;
        }

        stateTransactionPrepared = TRUE;
    }

    /*
     * Parse bind tags before modifying the live element or any derived
     * resources.
     */
    if ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & BAR_ELEM_TAGS_MASK)) {
        if (Rbc_PrepareElemTagsTransaction(graphPtr, elemPtr, &tagsTransaction) != TCL_OK) {
            goto error;
        }

        tagsTransactionPrepared = TRUE;
    }

    /*
     * Parse the complete style palette before changing the live palette
     * or any derived drawing resources.
     */
    if ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & BAR_ELEM_STYLES_MASK)) {
        if (Rbc_PrepareElemStylesTransaction(graphPtr, elemPtr, rbcBarElementUid, sizeof(BarPenStyle),
                                             &stylesTransaction) != TCL_OK) {
            goto error;
        }

        stylesTransactionPrepared = TRUE;
    }

    /*
     * Configure the embedded bar pen initially and whenever one of its
     * element-level options changes.
     */
    if ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & BAR_ELEM_BUILTIN_PEN_MASK)) {
        if (ConfigurePen(graphPtr, &barPtr->builtinPen.core) != TCL_OK) {
            goto error;
        }
    }

    /*
     * No fallible operation remains. Commit all staged values and
     * references to the live element.
     */
    if (stateTransactionPrepared) {
        Rbc_CommitElemStateTransaction(elemPtr, &stateTransaction);
    }

    if (tagsTransactionPrepared) {
        Rbc_CommitElemTagsTransaction(elemPtr, &tagsTransaction);
    }

    if (axisTransactionPrepared) {
        Rbc_CommitElemAxisTransaction(graphPtr, elemPtr, &axisTransaction);
    }

    if (penTransactionPrepared) {
        Rbc_CommitElemPenTransaction(graphPtr, elemPtr, &barPtr->builtinPen.core, &penTransaction);
    }
    
    if (stylesTransactionPrepared) {
        Rbc_CommitElemStylesTransaction(graphPtr, elemPtr, &stylesTransaction);
    }

    assert(elemPtr->normalPenPtr != NULL);
    linkPtr = Rbc_ChainFirstLink(elemPtr->palette);

    if (linkPtr != NULL) {
        BarPenStyle *stylePtr;

        stylePtr = Rbc_ChainGetValue(linkPtr);

        /*
         * BarPenStyle stores a concrete BarPen pointer.
         */
        stylePtr->penPtr = BAR_PEN_FROM_CORE(elemPtr->normalPenPtr);
    }

    /*
     * Staged vectors can now safely replace the live vectors.
     */
    if (dataTransactionPrepared) {
        Rbc_CommitElemDataTransaction(elemPtr, &dataTransaction);

        /*
         * The live vectors and the retained Tcl representations must
         * describe the same configuration.
         */
        Rbc_SyncElemDataOptionObjects(elemPtr);
    }

    if ((!elemPtr->optionsConfigured) || (elemPtr->optionMask & BAR_ELEM_MAP_ITEM_MASK)) {
        elemPtr->flags |= MAP_ITEM;
    }

    return TCL_OK;

error:

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
 * GetBarExtents --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Element *elemPtr
 *      Extents2D *extsPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void GetBarExtents(Element *elemPtr, Extents2D *extsPtr) {
    Graph *graphPtr = elemPtr->graphPtr;
    Bar *barPtr = BAR_FROM_CORE(elemPtr);
    double middle, barWidth;
    int nPoints;

    extsPtr->top = extsPtr->left = DBL_MAX;
    extsPtr->bottom = extsPtr->right = -DBL_MAX;

    nPoints = NumberOfPoints(elemPtr);
    if (nPoints < 1) {
        return; /* No data points */
    }
    barWidth = graphPtr->barWidth;
    if (barPtr->barWidth > 0.0) {
        barWidth = barPtr->barWidth;
    }
    middle = barWidth * 0.5;
    extsPtr->left = barPtr->core.x.min - middle;
    extsPtr->right = barPtr->core.x.max + middle;

    extsPtr->top = barPtr->core.y.min;
    extsPtr->bottom = barPtr->core.y.max;
    if (extsPtr->bottom < graphPtr->baseline) {
        extsPtr->bottom = graphPtr->baseline;
    }
    /*
     * Handle "stacked" bar elements specially.
     *
     * If element is stacked, the sum of its ordinates may be outside
     * the minimum/maximum limits of the element's data points.
     */
    if ((graphPtr->mode == MODE_STACKED) && (graphPtr->nStacks > 0)) {
        CheckStacks(graphPtr, &(elemPtr->axes), &(extsPtr->top), &(extsPtr->bottom));
    }
    /* Warning: You get what you deserve if the x-axis is logScale */
    if (elemPtr->axes.x->logScale) {
        extsPtr->left = Rbc_FindElemVectorMinimum(&(barPtr->core.x), DBL_MIN) + middle;
    }
    /* Fix y-min limits for barchart */
    if (elemPtr->axes.y->logScale) {
        if ((extsPtr->top <= 0.0) || (extsPtr->top > 1.0)) {
            extsPtr->top = 1.0;
        }
    } else {
        if (extsPtr->top > 0.0) {
            extsPtr->top = 0.0;
        }
    }
    /* Correct the extents for error bars if they exist. */
    if (elemPtr->xError.nValues > 0) {
        register int i;
        double x;

        /* Correct the data limits for error bars */
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
 * ----------------------------------------------------------------------
 *
 * ClosestBar --
 *
 *      Find the bar segment closest to the window coordinates    point
 *      specified.
 *
 *      Note:  This does not return the height of the stacked segment
 *             (in graph coordinates) properly.
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record
 *      Element *elemPtr - Bar element
 *      ClosestSearch *searchPtr - Info of closest point in element
 *
 * Results:
 *      Returns 1 if the point is width any bar segment, otherwise 0.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void ClosestBar(Graph *graphPtr, Element *elemPtr, ClosestSearch *searchPtr) {
    Bar *barPtr = BAR_FROM_CORE(elemPtr);
    Point2D *pointPtr, *endPtr;
    Point2D t, outline[5];
    XRectangle *rectPtr;
    double left, right, top, bottom;
    double minDist, dist;
    int imin;
    register int i;

    minDist = searchPtr->dist;
    imin = 0;

    rectPtr = barPtr->rectangles;
    for (i = 0; i < barPtr->nRects; i++) {
        if (PointInRectangle(rectPtr, searchPtr->x, searchPtr->y)) {
            imin = barPtr->rectToData[i];
            minDist = 0.0;
            break;
        }
        left = rectPtr->x, top = rectPtr->y;
        right = (double)(rectPtr->x + rectPtr->width);
        bottom = (double)(rectPtr->y + rectPtr->height);
        outline[4].x = outline[3].x = outline[0].x = left;
        outline[4].y = outline[1].y = outline[0].y = top;
        outline[2].x = outline[1].x = right;
        outline[3].y = outline[2].y = bottom;

        for (pointPtr = outline, endPtr = outline + 4; pointPtr < endPtr; pointPtr++) {
            t = Rbc_GetProjection(searchPtr->x, searchPtr->y, pointPtr, pointPtr + 1);
            if (t.x > right) {
                t.x = right;
            } else if (t.x < left) {
                t.x = left;
            }
            if (t.y > bottom) {
                t.y = bottom;
            } else if (t.y < top) {
                t.y = top;
            }
            dist = hypot((t.x - searchPtr->x), (t.y - searchPtr->y));
            if (dist < minDist) {
                minDist = dist;
                imin = barPtr->rectToData[i];
            }
        }
        rectPtr++;
    }
    if (minDist < searchPtr->dist) {
        searchPtr->elemPtr = &barPtr->core;
        searchPtr->dist = minDist;
        searchPtr->index = imin;
        searchPtr->point.x = (double)barPtr->core.x.valueArr[imin];
        searchPtr->point.y = (double)barPtr->core.y.valueArr[imin];
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MergePens --
 *
 *      Reorders the both arrays of points and errorbars to merge pens.
 *
 * Parameters:
 *      Bar *barPtr
 *      PenStyle **dataToStyle
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The old arrays are freed and new ones allocated containing
 *      the reordered points and errorbars.
 *
 *----------------------------------------------------------------------
 */
static void MergePens(Bar *barPtr, PenStyle **dataToStyle) {
    BarPenStyle *stylePtr;
    Rbc_ChainLink *linkPtr;

    if (Rbc_ChainGetLength(barPtr->core.palette) < 2) {
        linkPtr = Rbc_ChainFirstLink(barPtr->core.palette);
        stylePtr = Rbc_ChainGetValue(linkPtr);
        stylePtr->nRects = barPtr->nRects;
        stylePtr->rectangles = barPtr->rectangles;
        stylePtr->symbolSize = barPtr->rectangles->width / 2;
        stylePtr->xErrorBarCnt = barPtr->core.xErrorBarCnt;
        stylePtr->xErrorBars = barPtr->core.xErrorBars;
        stylePtr->yErrorBarCnt = barPtr->core.yErrorBarCnt;
        stylePtr->yErrorBars = barPtr->core.yErrorBars;
        return;
    }
    /* We have more than one style. Group bar segments of like pen
     * styles together.  */

    if (barPtr->nRects > 0) {
        XRectangle *rectangles;
        int *rectToData;
        int dataIndex;
        register XRectangle *rectPtr;
        register int *indexPtr;
        register int i;

        rectangles = (XRectangle *)ckalloc(barPtr->nRects * sizeof(XRectangle));
        rectToData = (int *)ckalloc(barPtr->nRects * sizeof(int));
        assert(rectangles && rectToData);

        rectPtr = rectangles, indexPtr = rectToData;
        for (linkPtr = Rbc_ChainFirstLink(barPtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->symbolSize = rectPtr->width / 2;
            stylePtr->rectangles = rectPtr;
            for (i = 0; i < barPtr->nRects; i++) {
                dataIndex = barPtr->rectToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *rectPtr++ = barPtr->rectangles[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->nRects = rectPtr - stylePtr->rectangles;
        }
        ckfree((char *)barPtr->rectangles);
        barPtr->rectangles = rectangles;
        ckfree((char *)barPtr->rectToData);
        barPtr->rectToData = rectToData;
    }
    if (barPtr->core.xErrorBarCnt > 0) {
        Segment2D *errorBars, *segPtr;
        int *errorToData, *indexPtr;
        int dataIndex;
        register int i;

        errorBars = (Segment2D *)ckalloc(barPtr->core.xErrorBarCnt * sizeof(Segment2D));
        errorToData = (int *)ckalloc(barPtr->core.xErrorBarCnt * sizeof(int));
        assert(errorBars);
        segPtr = errorBars, indexPtr = errorToData;
        for (linkPtr = Rbc_ChainFirstLink(barPtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->xErrorBars = segPtr;
            for (i = 0; i < barPtr->core.xErrorBarCnt; i++) {
                dataIndex = barPtr->core.xErrorToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *segPtr++ = barPtr->core.xErrorBars[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->xErrorBarCnt = segPtr - stylePtr->xErrorBars;
        }
        ckfree((char *)barPtr->core.xErrorBars);
        barPtr->core.xErrorBars = errorBars;
        ckfree((char *)barPtr->core.xErrorToData);
        barPtr->core.xErrorToData = errorToData;
    }
    if (barPtr->core.yErrorBarCnt > 0) {
        Segment2D *errorBars, *segPtr;
        int *errorToData, *indexPtr;
        int dataIndex;
        register int i;

        errorBars = (Segment2D *)ckalloc(barPtr->core.yErrorBarCnt * sizeof(Segment2D));
        errorToData = (int *)ckalloc(barPtr->core.yErrorBarCnt * sizeof(int));
        assert(errorBars);
        segPtr = errorBars, indexPtr = errorToData;
        for (linkPtr = Rbc_ChainFirstLink(barPtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->yErrorBars = segPtr;
            for (i = 0; i < barPtr->core.yErrorBarCnt; i++) {
                dataIndex = barPtr->core.yErrorToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *segPtr++ = barPtr->core.yErrorBars[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->yErrorBarCnt = segPtr - stylePtr->yErrorBars;
        }
        ckfree((char *)barPtr->core.yErrorBars);
        barPtr->core.yErrorBars = errorBars;
        ckfree((char *)barPtr->core.yErrorToData);
        barPtr->core.yErrorToData = errorToData;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MapActiveBars --
 *
 *      Creates an array of points of the active graph coordinates.
 *
 * Parameters:
 *      Bar *barPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is freed and allocated for the active point array.
 *
 *----------------------------------------------------------------------
 */
static void MapActiveBars(Bar *barPtr) {
    if (barPtr->activeRects != NULL) {
        ckfree((char *)barPtr->activeRects);
        barPtr->activeRects = NULL;
    }
    if (barPtr->activeToData != NULL) {
        ckfree((char *)barPtr->activeToData);
        barPtr->activeToData = NULL;
    }
    barPtr->nActive = 0;

    if (barPtr->core.nActiveIndices > 0) {
        XRectangle *activeRects;
        int *activeToData;
        register int i, n;
        register int count;

        activeRects = (XRectangle *)ckalloc(sizeof(XRectangle) * barPtr->core.nActiveIndices);
        assert(activeRects);
        activeToData = (int *)ckalloc(sizeof(int) * barPtr->core.nActiveIndices);
        assert(activeToData);
        count = 0;
        for (i = 0; i < barPtr->nRects; i++) {
            for (n = 0; n < barPtr->core.nActiveIndices; n++) {
                if (barPtr->rectToData[i] == barPtr->core.activeIndices[n]) {
                    activeRects[count] = barPtr->rectangles[i];
                    activeToData[count] = i;
                    count++;
                }
            }
        }
        barPtr->nActive = count;
        barPtr->activeRects = activeRects;
        barPtr->activeToData = activeToData;
    }
    barPtr->core.flags &= ~ACTIVE_PENDING;
}

/*
 *----------------------------------------------------------------------
 *
 * ResetBar --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Bar *barPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void ResetBar(Bar *barPtr) {
    /* Release any storage associated with the display of the bar */
    ClearPalette(barPtr->core.palette);
    if (barPtr->activeRects != NULL) {
        ckfree((char *)barPtr->activeRects);
    }
    if (barPtr->activeToData != NULL) {
        ckfree((char *)barPtr->activeToData);
    }
    if (barPtr->core.xErrorBars != NULL) {
        ckfree((char *)barPtr->core.xErrorBars);
    }
    if (barPtr->core.xErrorToData != NULL) {
        ckfree((char *)barPtr->core.xErrorToData);
    }
    if (barPtr->core.yErrorBars != NULL) {
        ckfree((char *)barPtr->core.yErrorBars);
    }
    if (barPtr->core.yErrorToData != NULL) {
        ckfree((char *)barPtr->core.yErrorToData);
    }
    if (barPtr->rectangles != NULL) {
        ckfree((char *)barPtr->rectangles);
    }
    if (barPtr->rectToData != NULL) {
        ckfree((char *)barPtr->rectToData);
    }
    barPtr->activeToData = barPtr->core.xErrorToData = barPtr->core.yErrorToData = barPtr->rectToData = NULL;
    barPtr->activeRects = barPtr->rectangles = NULL;
    barPtr->core.xErrorBars = barPtr->core.yErrorBars = NULL;
    barPtr->nActive = barPtr->core.xErrorBarCnt = barPtr->core.yErrorBarCnt = barPtr->nRects = 0;
}

/*
 * ----------------------------------------------------------------------
 *
 * MapBar --
 *
 *      Calculates the actual window coordinates of the bar element.
 *      The window coordinates are saved in the bar element structure.
 *
 *      A bar can have multiple segments (more than one x,y pairs).
 *      In this case, the bar can be represented as either a set of
 *      non-contiguous bars or a single multi-segmented (stacked) bar.
 *
 *      The x-axis layout for a barchart may be presented in one of
 *      two ways.  If abscissas are used, the bars are placed at those
 *      coordinates.  Otherwise, the range will represent the number
 *      of values.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Element *elemPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static void MapBar(Graph *graphPtr, Element *elemPtr) {
    Bar *barPtr = BAR_FROM_CORE(elemPtr);
    FreqKey key;
    PenStyle **dataToStyle;
    Point2D c1, c2; /* Two opposite corners of the rectangle
                     * in graph coordinates. */
    double *x, *y;
    double barWidth, barOffset;
    double baseline;
    double dx, dy;
    int *rectToData; /* Maps rectangles to data point indices */
    int height;
    int invertBar;
    int nPoints, count;
    register XRectangle *rectPtr, *rectangles;
    register int i;
    int size;
    Rbc_ChainLink *linkPtr;
    BarPenStyle *stylePtr;

    ResetBar(barPtr);
    nPoints = NumberOfPoints(elemPtr);
    if (nPoints < 1) {
        return; /* No data points */
    }
    barWidth = graphPtr->barWidth;
    if (barPtr->barWidth > 0.0) {
        barWidth = barPtr->barWidth;
    }
    baseline = (barPtr->core.axes.y->logScale) ? 1.0 : graphPtr->baseline;
    barOffset = barWidth * 0.5;

    /*
     * Create an array of rectangles representing the screen coordinates
     * of all the segments in the bar.
     */
    rectPtr = rectangles = (XRectangle *)ckalloc(nPoints * sizeof(XRectangle));
    assert(rectangles);
    rectToData = RbcCalloc(nPoints, sizeof(int));
    assert(rectToData);

    x = barPtr->core.x.valueArr, y = barPtr->core.y.valueArr;
    count = 0;
    for (i = 0; i < nPoints; i++) {
        if (((x[i] - barWidth) > barPtr->core.axes.x->axisRange.max) ||
            ((x[i] + barWidth) < barPtr->core.axes.x->axisRange.min)) {
            continue; /* Abscissa is out of range of the x-axis */
        }
        c1.x = x[i] - barOffset;
        c1.y = y[i];
        c2.x = c1.x + barWidth;
        c2.y = baseline;

        /*
         * If the mode is "aligned" or "stacked" we need to adjust the
         * x or y coordinates of the two corners.
         */

        if ((graphPtr->nStacks > 0) && (graphPtr->mode != MODE_INFRONT)) {
            Tcl_HashEntry *hPtr;

            key.value = x[i];
            key.axes = barPtr->core.axes;
            hPtr = Tcl_FindHashEntry(&(graphPtr->freqTable), (char *)&key);
            if (hPtr != NULL) {
                FreqInfo *infoPtr;
                double slice, width;

                infoPtr = (FreqInfo *)Tcl_GetHashValue(hPtr);
                switch (graphPtr->mode) {
                case MODE_STACKED:
                    c2.y = infoPtr->lastY;
                    c1.y += c2.y;
                    infoPtr->lastY = c1.y;
                    break;

                case MODE_ALIGNED:
                    infoPtr->count++;
                    slice = barWidth / (double)infoPtr->freq;
                    c1.x += slice * (infoPtr->freq - infoPtr->count);
                    c2.x = c1.x + slice;
                    break;

                case MODE_OVERLAP:
                    infoPtr->count++;
                    slice = barWidth / (double)(infoPtr->freq * 2);
                    width = slice * (infoPtr->freq + 1);
                    c1.x += slice * (infoPtr->freq - infoPtr->count);
                    c2.x = c1.x + width;
                    break;
                case MODE_INFRONT:
                    break;
                }
            }
        }
        invertBar = FALSE;
        if (c1.y < c2.y) {
            double temp;

            /* Handle negative bar values by swapping ordinates */
            temp = c1.y, c1.y = c2.y, c2.y = temp;
            invertBar = TRUE;
        }
        /*
         * Get the two corners of the bar segment and compute the rectangle
         */
        c1 = Rbc_Map2D(graphPtr, c1.x, c1.y, &barPtr->core.axes);
        c2 = Rbc_Map2D(graphPtr, c2.x, c2.y, &barPtr->core.axes);

        /* Bound the bars vertically by the size of the graph window */
        if (c1.y < 0.0) {
            c1.y = 0.0;
        } else if (c1.y > (double)graphPtr->height) {
            c1.y = (double)graphPtr->height;
        }
        if (c2.y < 0.0) {
            c2.y = 0.0;
        } else if (c2.y > (double)graphPtr->height) {
            c2.y = (double)graphPtr->height;
        }
        dx = c1.x - c2.x;
        dy = c1.y - c2.y;
        height = (int)Round(FABS(dy));
        if (invertBar) {
            rectPtr->y = (short int)MIN(c1.y, c2.y);
        } else {
            rectPtr->y = (short int)(MAX(c1.y, c2.y)) - height;
        }
        rectPtr->x = (short int)MIN(c1.x, c2.x);
        rectPtr->width = (short int)Round(FABS(dx)) + 1;
        if (rectPtr->width < 1) {
            rectPtr->width = 1;
        }
        rectPtr->height = height + 1;
        if (rectPtr->height < 1) {
            rectPtr->height = 1;
        }
        rectToData[count] = i; /* Save the data index corresponding to the
                                * rectangle */
        rectPtr++;
        count++;
    }
    barPtr->nRects = count;
    barPtr->rectangles = rectangles;
    barPtr->rectToData = rectToData;
    if (barPtr->core.nActiveIndices > 0) {
        MapActiveBars(barPtr);
    }

    size = 20;
    if (count > 0) {
        size = rectangles->width;
    }
    /* Set the symbol size of all the pen styles. */
    for (linkPtr = Rbc_ChainFirstLink(barPtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        stylePtr->symbolSize = size;
        stylePtr->errorBarCapWidth =
            (stylePtr->penPtr->errorBarCapWidth > 0) ? stylePtr->penPtr->errorBarCapWidth : (int)(size * 0.6666666);
        stylePtr->errorBarCapWidth /= 2;
    }
    dataToStyle = Rbc_StyleMap(&barPtr->core);
    if (((barPtr->core.yHigh.nValues > 0) && (barPtr->core.yLow.nValues > 0)) ||
        ((barPtr->core.xHigh.nValues > 0) && (barPtr->core.xLow.nValues > 0)) || (barPtr->core.xError.nValues > 0) ||
        (barPtr->core.yError.nValues > 0)) {
        Rbc_MapErrorBars(graphPtr, &barPtr->core, dataToStyle);
    }
    MergePens(barPtr, dataToStyle);
    ckfree((char *)dataToStyle);
}

/*
 * -----------------------------------------------------------------
 *
 * DrawSymbol --
 *
 *      Draw a symbol centered at the given x,y window coordinate
 *      based upon the element symbol type and size.
 *
 *      Most notable problem is the round-off errors generated when
 *      calculating the centered position of the symbol.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *      Element *elemPtr
 *      int x
 *      int y
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
static void DrawSymbol(Graph *graphPtr, Drawable drawable, Element *elemPtr, int x, int y, int size) {
    BarPen *penPtr;
    int radius;

    penPtr = BAR_PEN_FROM_CORE(elemPtr->normalPenPtr);

    if ((penPtr->border == NULL) && (penPtr->fgColor == NULL)) {
        return;
    }
    radius = (size / 2);
    size--;

    x -= radius;
    y -= radius;
    XSetTSOrigin(graphPtr->display, penPtr->gc, x, y);
    XFillRectangle(graphPtr->display, drawable, penPtr->gc, x, y, size, size);
    XSetTSOrigin(graphPtr->display, penPtr->gc, 0, 0);
}

/*
 * -----------------------------------------------------------------
 *
 * DrawBarSegments --
 *
 *      Draws each of the rectangular segments for the element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *      BarPen *penPtr
 *      XRectangle *rectangles
 *      int nRects
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
static void DrawBarSegments(Graph *graphPtr, Drawable drawable, BarPen *penPtr, XRectangle *rectangles, int nRects) {
    register XRectangle *rectPtr;

    if ((penPtr->border == NULL) && (penPtr->fgColor == NULL)) {
        return;
    }
    XFillRectangles(graphPtr->display, drawable, penPtr->gc, rectangles, nRects);
    if ((penPtr->border != NULL) && (penPtr->borderWidth > 0) && (penPtr->relief != TK_RELIEF_FLAT)) {
        XRectangle *endPtr;

        for (rectPtr = rectangles, endPtr = rectangles + nRects; rectPtr < endPtr; rectPtr++) {
            Rbc_Draw3DRectangle(graphPtr->tkwin, drawable, penPtr->border, rectPtr->x, rectPtr->y, rectPtr->width,
                                rectPtr->height, penPtr->borderWidth, penPtr->relief);
        }
    }
}

/*
 * -----------------------------------------------------------------
 *
 * DrawBarValues --
 *
 *      Draws the numeric value of the bar.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable
 *      Bar *barPtr
 *      BarPen *penPtr
 *      XRectangle *rectangles
 *      int nRects
 *      int *rectToData
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
static void DrawBarValues(Graph *graphPtr, Drawable drawable, Bar *barPtr, BarPen *penPtr, XRectangle *rectangles,
                          int nRects, int *rectToData) {
    XRectangle *rectPtr, *endPtr;
    int count;
    char *fmt;
    char string[TCL_DOUBLE_SPACE * 2 + 2];
    double x, y;
    Point2D anchorPos;

    count = 0;
    fmt = penPtr->valueFormat;
    if (fmt == NULL) {
        fmt = "%g";
    }
    for (rectPtr = rectangles, endPtr = rectangles + nRects; rectPtr < endPtr; rectPtr++) {
        x = barPtr->core.x.valueArr[rectToData[count]];
        y = barPtr->core.y.valueArr[rectToData[count]];
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
        if (graphPtr->inverted) {
            anchorPos.y = rectPtr->y + rectPtr->height * 0.5;
            anchorPos.x = rectPtr->x + rectPtr->width;
            if (y < graphPtr->baseline) {
                anchorPos.x -= rectPtr->width;
            }
        } else {
            anchorPos.x = rectPtr->x + rectPtr->width * 0.5;
            anchorPos.y = rectPtr->y;
            if (y < graphPtr->baseline) {
                anchorPos.y += rectPtr->height;
            }
        }
        Rbc_DrawText(graphPtr->tkwin, drawable, string, &(penPtr->valueStyle), (int)anchorPos.x, (int)anchorPos.y);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * DrawNormalBar --
 *
 *      Draws the rectangle representing the bar element.  If the
 *      relief option is set to "raised" or "sunken" and the bar
 *      borderwidth is set (borderwidth > 0), a 3D border is drawn
 *      around the bar.
 *
 *      Don't draw bars that aren't visible (i.e. within the limits
 *      of the axis).
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable
 *      Element *elemPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      X drawing commands are output.
 *
 * ----------------------------------------------------------------------
 */
static void DrawNormalBar(Graph *graphPtr, Drawable drawable, Element *elemPtr) {
    Bar *barPtr = BAR_FROM_CORE(elemPtr);
    int count;
    Rbc_ChainLink *linkPtr;
    register BarPenStyle *stylePtr;
    BarPen *penPtr;

    count = 0;
    for (linkPtr = Rbc_ChainFirstLink(barPtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        penPtr = stylePtr->penPtr;
        if (stylePtr->nRects > 0) {
            DrawBarSegments(graphPtr, drawable, penPtr, stylePtr->rectangles, stylePtr->nRects);
        }
        if ((stylePtr->xErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_X)) {
            Rbc_Draw2DSegments(graphPtr->display, drawable, penPtr->errorBarGC, stylePtr->xErrorBars,
                               stylePtr->xErrorBarCnt);
        }
        if ((stylePtr->yErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_Y)) {
            Rbc_Draw2DSegments(graphPtr->display, drawable, penPtr->errorBarGC, stylePtr->yErrorBars,
                               stylePtr->yErrorBarCnt);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            DrawBarValues(graphPtr, drawable, barPtr, penPtr, stylePtr->rectangles, stylePtr->nRects,
                          barPtr->rectToData + count);
        }
        count += stylePtr->nRects;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * DrawActiveBar --
 *
 *      Draws rectangles representing the active segments of the
 *      bar element.  If the -relief option is set (other than "flat")
 *      and the borderwidth is greater than 0, a 3D border is drawn
 *      around the each bar segment.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable
 *      Element *elemPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      X drawing commands are output.
 *
 * ----------------------------------------------------------------------
 */
static void DrawActiveBar(Graph *graphPtr, Drawable drawable, Element *elemPtr) {
    Bar *barPtr = BAR_FROM_CORE(elemPtr);

    if (barPtr->core.activePenPtr != NULL) {
        BarPen *penPtr;

        penPtr = BAR_PEN_FROM_CORE(elemPtr->activePenPtr);

        if (barPtr->core.nActiveIndices > 0) {
            if (barPtr->core.flags & ACTIVE_PENDING) {
                MapActiveBars(barPtr);
            }
            DrawBarSegments(graphPtr, drawable, penPtr, barPtr->activeRects, barPtr->nActive);
            if (penPtr->valueShow != SHOW_NONE) {
                DrawBarValues(graphPtr, drawable, barPtr, penPtr, barPtr->activeRects, barPtr->nActive,
                              barPtr->activeToData);
            }
        } else if (barPtr->core.nActiveIndices < 0) {
            DrawBarSegments(graphPtr, drawable, penPtr, barPtr->rectangles, barPtr->nRects);
            if (penPtr->valueShow != SHOW_NONE) {
                DrawBarValues(graphPtr, drawable, barPtr, penPtr, barPtr->rectangles, barPtr->nRects,
                              barPtr->rectToData);
            }
        }
    }
}

/*
 * -----------------------------------------------------------------
 *
 * SymbolToPostScript --
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
 *      Element *elemPtr
 *      double x
 *      double y
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
static void SymbolToPostScript(Graph *graphPtr, PsToken psToken, Element *elemPtr, double x, double y, int size) {
    BarPen *bpPtr = BAR_PEN_FROM_CORE(elemPtr->normalPenPtr);

    if ((bpPtr->border == NULL) && (bpPtr->fgColor == NULL)) {
        return;
    }
    /*
     * Build a PostScript procedure to draw the fill and outline of
     * the symbol after the path of the symbol shape has been formed
     */
    Rbc_AppendToPostScript(psToken, "\n", "/DrawSymbolProc {\n", "  gsave\n    ", (char *)NULL);
    if (bpPtr->stipple != None) {
        if (bpPtr->border != NULL) {
            Rbc_BackgroundToPostScript(psToken, Tk_3DBorderColor(bpPtr->border));
            Rbc_AppendToPostScript(psToken, "    Fill\n    ", (char *)NULL);
        }
        if (bpPtr->fgColor != NULL) {
            Rbc_ForegroundToPostScript(psToken, bpPtr->fgColor);
        } else {
            Rbc_ForegroundToPostScript(psToken, Tk_3DBorderColor(bpPtr->border));
        }
        Rbc_StippleToPostScript(psToken, graphPtr->display, bpPtr->stipple);
    } else if (bpPtr->fgColor != NULL) {
        Rbc_ForegroundToPostScript(psToken, bpPtr->fgColor);
        Rbc_AppendToPostScript(psToken, "    fill\n", (char *)NULL);
    }
    Rbc_AppendToPostScript(psToken, "  grestore\n", (char *)NULL);
    Rbc_AppendToPostScript(psToken, "} def\n\n", (char *)NULL);
    Rbc_FormatToPostScript(psToken, "%g %g %d Sq\n", x, y, size);
}

/*
 *----------------------------------------------------------------------
 *
 * SegmentsToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *      BarPen *penPtr
 *      register XRectangle *rectPtr
 *      int nRects
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void SegmentsToPostScript(Graph *graphPtr, PsToken psToken, BarPen *penPtr, register XRectangle *rectPtr,
                                 int nRects) {
    XRectangle *endPtr;

    if ((penPtr->border == NULL) && (penPtr->fgColor == NULL)) {
        return;
    }
    for (endPtr = rectPtr + nRects; rectPtr < endPtr; rectPtr++) {
        if ((rectPtr->width < 1) || (rectPtr->height < 1)) {
            continue;
        }
        if (penPtr->stipple != None) {
            Rbc_RegionToPostScript(psToken, (double)rectPtr->x, (double)rectPtr->y, (int)rectPtr->width - 1,
                                   (int)rectPtr->height - 1);
            if (penPtr->border != NULL) {
                Rbc_BackgroundToPostScript(psToken, Tk_3DBorderColor(penPtr->border));
                Rbc_AppendToPostScript(psToken, "Fill\n", (char *)NULL);
            }
            if (penPtr->fgColor != NULL) {
                Rbc_ForegroundToPostScript(psToken, penPtr->fgColor);
            } else {
                Rbc_ForegroundToPostScript(psToken, Tk_3DBorderColor(penPtr->border));
            }
            Rbc_StippleToPostScript(psToken, graphPtr->display, penPtr->stipple);
        } else if (penPtr->fgColor != NULL) {
            Rbc_ForegroundToPostScript(psToken, penPtr->fgColor);
            Rbc_RectangleToPostScript(psToken, (double)rectPtr->x, (double)rectPtr->y, (int)rectPtr->width - 1,
                                      (int)rectPtr->height - 1);
        }
        if ((penPtr->border != NULL) && (penPtr->borderWidth > 0) && (penPtr->relief != TK_RELIEF_FLAT)) {
            Rbc_Draw3DRectangleToPostScript(psToken, penPtr->border, (double)rectPtr->x, (double)rectPtr->y,
                                            (int)rectPtr->width, (int)rectPtr->height, penPtr->borderWidth,
                                            penPtr->relief);
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * BarValuesToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *      Bar *barPtr
 *      BarPen *penPtr
 *      XRectangle *rectangles
 *      int nRects
 *      int *rectToData
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void BarValuesToPostScript(Graph *graphPtr, PsToken psToken, Bar *barPtr, BarPen *penPtr, XRectangle *rectangles,
                                  int nRects, int *rectToData) {
    XRectangle *rectPtr, *endPtr;
    int count;
    char *fmt;
    char string[TCL_DOUBLE_SPACE * 2 + 2];
    double x, y;
    Point2D anchorPos;

    count = 0;
    fmt = penPtr->valueFormat;
    if (fmt == NULL) {
        fmt = "%g";
    }
    for (rectPtr = rectangles, endPtr = rectangles + nRects; rectPtr < endPtr; rectPtr++) {
        x = barPtr->core.x.valueArr[rectToData[count]];
        y = barPtr->core.y.valueArr[rectToData[count]];
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
        if (graphPtr->inverted) {
            anchorPos.y = rectPtr->y + rectPtr->height * 0.5;
            anchorPos.x = rectPtr->x + rectPtr->width;
            if (y < graphPtr->baseline) {
                anchorPos.x -= rectPtr->width;
            }
        } else {
            anchorPos.x = rectPtr->x + rectPtr->width * 0.5;
            anchorPos.y = rectPtr->y;
            if (y < graphPtr->baseline) {
                anchorPos.y += rectPtr->height;
            }
        }
        Rbc_TextToPostScript(psToken, string, &(penPtr->valueStyle), anchorPos.x, anchorPos.y);
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * ActiveBarToPostScript --
 *
 *      Similar to the NormalBarToPostScript procedure, generates
 *      PostScript commands to display the rectangles representing the
 *      active bar segments of the element.
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
 * ----------------------------------------------------------------------
 */
static void ActiveBarToPostScript(Graph *graphPtr, PsToken psToken, Element *elemPtr) {
    Bar *barPtr;

    barPtr = BAR_FROM_CORE(elemPtr);
    if (elemPtr->activePenPtr != NULL) {
        BarPen *penPtr;
        penPtr = BAR_PEN_FROM_CORE(elemPtr->activePenPtr);
        if (elemPtr->nActiveIndices > 0) {
            if (elemPtr->flags & ACTIVE_PENDING) {
                MapActiveBars(barPtr);
            }
            SegmentsToPostScript(graphPtr, psToken, penPtr, barPtr->activeRects, barPtr->nActive);
            if (penPtr->valueShow != SHOW_NONE) {
                BarValuesToPostScript(graphPtr, psToken, barPtr, penPtr, barPtr->activeRects, barPtr->nActive,
                                      barPtr->activeToData);
            }
        } else if (elemPtr->nActiveIndices < 0) {
            SegmentsToPostScript(graphPtr, psToken, penPtr, barPtr->rectangles, barPtr->nRects);
            if (penPtr->valueShow != SHOW_NONE) {
                BarValuesToPostScript(graphPtr, psToken, barPtr, penPtr, barPtr->rectangles, barPtr->nRects,
                                      barPtr->rectToData);
            }
        }
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * NormalBarToPostScript --
 *
 *      Generates PostScript commands to form the rectangles
 *      representing the segments of the bar element.
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
 * ----------------------------------------------------------------------
 */
static void NormalBarToPostScript(Graph *graphPtr, PsToken psToken, Element *elemPtr) {
    Bar *barPtr = BAR_FROM_CORE(elemPtr);
    Rbc_ChainLink *linkPtr;
    register BarPenStyle *stylePtr;
    int count;
    BarPen *penPtr;
    XColor *colorPtr;

    count = 0;
    for (linkPtr = Rbc_ChainFirstLink(barPtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        penPtr = stylePtr->penPtr;
        if (stylePtr->nRects > 0) {
            SegmentsToPostScript(graphPtr, psToken, penPtr, stylePtr->rectangles, stylePtr->nRects);
        }
        colorPtr = penPtr->errorBarColor;
        if (colorPtr == COLOR_DEFAULT) {
            colorPtr = penPtr->fgColor;
        }
        if ((stylePtr->xErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_X)) {
            Rbc_LineAttributesToPostScript(psToken, colorPtr, penPtr->errorBarLineWidth, NULL, CapButt, JoinMiter);
            Rbc_2DSegmentsToPostScript(psToken, stylePtr->xErrorBars, stylePtr->xErrorBarCnt);
        }
        if ((stylePtr->yErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_Y)) {
            Rbc_LineAttributesToPostScript(psToken, colorPtr, penPtr->errorBarLineWidth, NULL, CapButt, JoinMiter);
            Rbc_2DSegmentsToPostScript(psToken, stylePtr->yErrorBars, stylePtr->yErrorBarCnt);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            BarValuesToPostScript(graphPtr, psToken, barPtr, penPtr, stylePtr->rectangles, stylePtr->nRects,
                                  barPtr->rectToData + count);
        }
        count += stylePtr->nRects;
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * DestroyBar --
 *
 *      Release memory and resources allocated for the bar element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Element *elemPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Everything associated with the bar element is freed up.
 *
 * ----------------------------------------------------------------------
 */
static void DestroyBar(Graph *graphPtr, Element *elemPtr) {
    Bar *barPtr;

    barPtr = BAR_FROM_CORE(elemPtr);
    Rbc_FreeAxisReference(graphPtr, elemPtr->axes.x);
    elemPtr->axes.x = NULL;
    Rbc_FreeAxisReference(graphPtr, elemPtr->axes.y);
    elemPtr->axes.y = NULL;
    if (elemPtr->normalPenPtr != &barPtr->builtinPen.core) {
        Rbc_FreePen(graphPtr, elemPtr->normalPenPtr);
    }
    DestroyPen(graphPtr, &barPtr->builtinPen.core);
    if (elemPtr->activePenPtr != NULL) {
        Rbc_FreePen(graphPtr, elemPtr->activePenPtr);
    }
    Rbc_FreeElemVector(&elemPtr->x);
    Rbc_FreeElemVector(&elemPtr->y);
    Rbc_FreeElemVector(&elemPtr->w);
    Rbc_FreeElemVector(&elemPtr->xHigh);
    Rbc_FreeElemVector(&elemPtr->xLow);
    Rbc_FreeElemVector(&elemPtr->xError);
    Rbc_FreeElemVector(&elemPtr->yHigh);
    Rbc_FreeElemVector(&elemPtr->yLow);
    Rbc_FreeElemVector(&elemPtr->yError);
    ResetBar(barPtr);
    if (elemPtr->activeIndices != NULL) {
        ckfree((char *)elemPtr->activeIndices);
        elemPtr->activeIndices = NULL;
        elemPtr->nActiveIndices = 0;
    }
    if (elemPtr->palette != NULL) {
        Rbc_FreePalette(graphPtr, elemPtr->palette);
        Rbc_ChainDestroy(elemPtr->palette);
        elemPtr->palette = NULL;
    }
    if (elemPtr->tags != NULL) {
        ckfree((char *)elemPtr->tags);
        elemPtr->tags = NULL;
    }
}

static ElementProcs barProcs = {
    ClosestBar,
    ConfigureBar,
    DestroyBar,
    DrawActiveBar,
    DrawNormalBar,
    DrawSymbol,
    GetBarExtents,
    ActiveBarToPostScript,
    NormalBarToPostScript,
    SymbolToPostScript,
    MapBar,
};

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_BarElement --
 *
 *      Allocate memory and initialize methods for the new bar element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      char *name
 *      Rbc_Uid type
 *
 * Results:
 *      The pointer to the newly allocated element structure is returned.
 *
 * Side effects:
 *      Memory is allocated for the bar element structure.
 *
 * ----------------------------------------------------------------------
 */
Element *Rbc_BarElement(Graph *graphPtr, const char *name, Rbc_Uid classUid) {
    Bar *barPtr;
    Element *elemPtr;

    barPtr = RbcCalloc(1, sizeof(Bar));
    if (barPtr == NULL) {
        return NULL;
    }
    elemPtr = &barPtr->core;
    elemPtr->optionSpecs = barElemOptionSpecs;
    elemPtr->optionTable = NULL;

    elemPtr->optionMask = 0;
    elemPtr->optionObjc = 0;
    elemPtr->optionObjv = NULL;
    elemPtr->optionsConfigured = FALSE;

    elemPtr->optionsInitialized = FALSE;
    elemPtr->tkResourcesReleased = FALSE;
    elemPtr->normalPenPtr = &barPtr->builtinPen.core;
    elemPtr->procsPtr = &barProcs;
    elemPtr->labelRelief = TK_RELIEF_FLAT;
    elemPtr->classUid = classUid;

    /*
     * By default, an element's name and label are identical.
     */
    elemPtr->label = RbcStrdup(name);
    elemPtr->name = RbcStrdup(name);
    elemPtr->graphPtr = graphPtr;
    elemPtr->hidden = FALSE;
    /*
     * The embedded pen's direct option fields are initialized through
     * barElemOptionSpecs, while its derived resources use the normal
     * modern bar-pen configuration.
     */
    InitPen(&barPtr->builtinPen, normalBarPenOptionSpecs, NORMAL_PEN);
    elemPtr->palette = Rbc_ChainCreate();
    return elemPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_InitFreqTable --
 *
 *      Generate a table of abscissa frequencies.  Duplicate
 *      x-coordinates (depending upon the bar drawing mode) indicate
 *      that something special should be done with each bar segment
 *      mapped to the same abscissa (i.e. it should be stacked,
 *      aligned, or overlay-ed with other segments)
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Memory is allocated for the bar element structure.
 *
 * ----------------------------------------------------------------------
 */
void Rbc_InitFreqTable(Graph *graphPtr) {
    register Element *elemPtr;
    Rbc_ChainLink *linkPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Bar *barPtr;
    int isNew;
    int nStacks, nSegs;
    int nPoints;
    FreqKey key;
    Tcl_HashTable freqTable;
    register int i;
    double *xArr;
    Tcl_Size count;
    /*
     * Free resources associated with a previous frequency table. This
     * includes the array of frequency information and the table itself
     */
    if (graphPtr->freqArr != NULL) {
        ckfree((char *)graphPtr->freqArr);
        graphPtr->freqArr = NULL;
    }
    if (graphPtr->nStacks > 0) {
        Tcl_DeleteHashTable(&(graphPtr->freqTable));
        graphPtr->nStacks = 0;
    }
    if (graphPtr->mode == MODE_INFRONT) {
        return; /* No frequency table is needed for
                 * "infront" mode */
    }
    Tcl_InitHashTable(&(graphPtr->freqTable), sizeof(FreqKey) / sizeof(int));

    /*
     * Initialize a hash table and fill it with unique abscissas.
     * Keep track of the frequency of each x-coordinate and how many
     * abscissas have duplicate mappings.
     */
    Tcl_InitHashTable(&freqTable, sizeof(FreqKey) / sizeof(int));
    nSegs = nStacks = 0;
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if ((elemPtr->hidden) || (elemPtr->classUid != rbcBarElementUid)) {
            continue;
        }
        nSegs++;
        barPtr = BAR_FROM_CORE(elemPtr);
        xArr = barPtr->core.x.valueArr;
        nPoints = NumberOfPoints(elemPtr);
        for (i = 0; i < nPoints; i++) {
            key.value = xArr[i];
            key.axes = barPtr->core.axes;
            hPtr = Tcl_CreateHashEntry(&freqTable, (char *)&key, &isNew);
            assert(hPtr != NULL);
            if (isNew) {
                count = 1;
            } else {
                count = (Tcl_Size)Tcl_GetHashValue(hPtr);
                if (count == 1) {
                    nStacks++;
                }
                count++;
            }
            Tcl_SetHashValue(hPtr, (ClientData)count);
        }
    }
    if (nSegs == 0) {
        return; /* No bar elements to be displayed */
    }
    if (nStacks > 0) {
        FreqInfo *infoPtr;
        FreqKey *keyPtr;
        Tcl_HashEntry *h2Ptr;

        graphPtr->freqArr = RbcCalloc(nStacks, sizeof(FreqInfo));
        assert(graphPtr->freqArr);
        infoPtr = graphPtr->freqArr;
        for (hPtr = Tcl_FirstHashEntry(&freqTable, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
            count = (Tcl_Size)Tcl_GetHashValue(hPtr);
            keyPtr = (FreqKey *)Tcl_GetHashKey(&freqTable, hPtr);
            if (count > 1) {
                h2Ptr = Tcl_CreateHashEntry(&(graphPtr->freqTable), (char *)keyPtr, &isNew);
                count = (Tcl_Size)Tcl_GetHashValue(hPtr);
                infoPtr->freq = count;
                infoPtr->axes = keyPtr->axes;
                Tcl_SetHashValue(h2Ptr, infoPtr);
                infoPtr++;
            }
        }
    }
    Tcl_DeleteHashTable(&freqTable);
    graphPtr->nStacks = nStacks;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_ComputeStacks --
 *
 *      Determine the height of each stack of bar segments.  A stack
 *      is created by designating two or more points with the same
 *      abscissa.  Each ordinate defines the height of a segment in
 *      the stack.  This procedure simply looks at all the data points
 *      summing the heights of each stacked segment. The sum is saved
 *      in the frequency information table.  This value will be used
 *      to calculate the y-axis limits (data limits aren't sufficient).
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      The heights of each stack is computed. CheckStacks will
 *      use this information to adjust the y-axis limits if necessary.
 *
 * ----------------------------------------------------------------------
 */
void Rbc_ComputeStacks(Graph *graphPtr) {
    Element *elemPtr;
    Bar *barPtr;
    FreqKey key;
    Rbc_ChainLink *linkPtr;
    Tcl_HashEntry *hPtr;
    int nPoints;
    register int i;
    register FreqInfo *infoPtr;
    double *xArr, *yArr;

    if ((graphPtr->mode != MODE_STACKED) || (graphPtr->nStacks == 0)) {
        return;
    }
    /* Reset the sums for all duplicate values to zero. */

    infoPtr = graphPtr->freqArr;
    for (i = 0; i < graphPtr->nStacks; i++) {
        infoPtr->sum = 0.0;
        infoPtr++;
    }

    /* Look at each bar point, adding the ordinates of duplicate abscissas */

    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if ((elemPtr->hidden) || (elemPtr->classUid != rbcBarElementUid)) {
            continue;
        }
        barPtr = BAR_FROM_CORE(elemPtr);
        xArr = barPtr->core.x.valueArr;
        yArr = barPtr->core.y.valueArr;
        nPoints = NumberOfPoints(elemPtr);
        for (i = 0; i < nPoints; i++) {
            key.value = xArr[i];
            key.axes = barPtr->core.axes;
            hPtr = Tcl_FindHashEntry(&(graphPtr->freqTable), (char *)&key);
            if (hPtr == NULL) {
                continue;
            }
            infoPtr = (FreqInfo *)Tcl_GetHashValue(hPtr);
            infoPtr->sum += yArr[i];
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_ResetStacks --
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
void Rbc_ResetStacks(Graph *graphPtr) {
    register FreqInfo *infoPtr, *endPtr;

    for (infoPtr = graphPtr->freqArr, endPtr = graphPtr->freqArr + graphPtr->nStacks; infoPtr < endPtr; infoPtr++) {
        infoPtr->lastY = 0.0;
        infoPtr->count = 0;
    }
}
