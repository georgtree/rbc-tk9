/**
 * @file rbcGrBar.c
 * @brief Bar elements: configuration, grouping, geometry, drawing and export.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @defgroup bar_configuration Bar configuration and pen lifecycle
 * Internal constructors and staged configuration; no public stubs are added.
 */
/** @defgroup bar_geometry Bar geometry ownership
 * Mapped arrays, style views, active selections and concrete element cleanup.
 */
/** @defgroup bar_output Bar screen and document output
 * Renderer adapters; value-label callbacks require stable drawing records.
 */
/** @defgroup bar_layout Bar layout internals
 * Private data-space placement and mapped geometry services. Source arrays and
 * graph records are borrowed; these helpers do not invoke Tcl callbacks.
 */

#include <stdint.h>
#include <limits.h>
#include "rbcGraph.h"
#include "rbcGrTrack.h"
#include <X11/Xutil.h>
#include <tcl.h>

#include "rbcGrElem.h"
#include "rbcRender.h"

/**
 * @ingroup bar_geometry
 * @brief A concrete bar pen with a first-member Pen header. Tk owns direct option
 * resources; ConfigurePen/DestroyPen own derived colors and drawing GCs.
 */
typedef struct {
    /*
     * Common pen state. This must remain the first member.
     */
    Pen core; /**< Common Pen prefix; must remain first. */
    /*
     * Original Tcl representations for values requiring additional
     * validation or conversion.
     */
    Tcl_Obj *borderWidthObjPtr; /**< Tk option-managed Tcl representation for border width; released with the owning option state. */
    Tcl_Obj *errorBarCapObjPtr; /**< Tk option-managed Tcl representation for error bar cap; released with the owning option state. */
    Tcl_Obj *errorBarColorObjPtr; /**< Tk option-managed Tcl representation for error bar color; released with the owning option state. */
    Tcl_Obj *errorBarWidthObjPtr; /**< Tk option-managed Tcl representation for error bar width; released with the owning option state. */
    Tcl_Obj *showErrorBarsObjPtr; /**< Tk option-managed Tcl representation for show error bars; released with the owning option state. */
    Tcl_Obj *showValuesObjPtr; /**< Tk option-managed Tcl representation for show values; released with the owning option state. */
    Tcl_Obj *valueRotateObjPtr; /**< Tk option-managed Tcl representation for value rotate; released with the owning option state. */
    Tcl_Obj *valueShadowObjPtr; /**< Tk option-managed Tcl representation for value shadow; released with the owning option state. */
    XColor *fgColor; /**< Configured fill color. */
    Tk_3DBorder border; /**< Tk-managed 3D border for the bar fill and relief. */
    int borderWidth; /**< Bevel thickness in pixels. */
    int relief; /**< Tk bar relief selector. */
    Pixmap stipple; /**< Retained fill stipple bitmap, or None. */
    double fillOpacity; /**< Fill alpha; borders and annotations remain opaque. */
    GC gc; /**< Owned native fill GC. */
    int errorBarShow; /**< Bit mask selecting X and Y error bars. */
    int errorBarLineWidth; /**< Error stroke width in pixels. */
    int errorBarCapWidth; /**< Requested error cap width in pixels; mapping resolves automatic widths. */
    XColor *errorBarColor; /**< Configured error color; COLOR_DEFAULT requests the element fallback. */
    GC errorBarGC; /**< Owned GC for native error strokes. */
    int valueShow; /**< Bit mask selecting X and Y coordinates in labels. */
    char *valueFormat; /**< Option-managed numeric format string. */
    Tcl_Obj *valueCommandObjPtr; /**< Tk option-managed Tcl representation for value command; released with the owning option state. */
    Tcl_Obj *valueOffsetObjPtr; /**< Tk option-managed Tcl representation for value offset; released with the owning option state. */
    Point2D valueOffset; /**< Label anchor displacement in pixels. */
    TextStyle valueStyle; /**< Label text style; its GC belongs to the pen. */
} BarPen;

typedef Rbc_RenderRectangle BarRectangle;

#define BAR_PEN_FROM_CORE(penPtr) ((BarPen *)((char *)(penPtr) - offsetof(BarPen, core)))
#define BAR_PEN_CORE_OFFSET(member) (offsetof(BarPen, core) + offsetof(Pen, member))

/**
 * @ingroup bar_geometry
 * @brief A weighted palette entry borrowing a concrete pen and mapped array slices.
 * Counts delimit valid views; the containing element owns the arrays.
 */
typedef struct {
    Weight weight; /**< Weight range where this pen is valid. */
    BarPen *penPtr; /**< Pen to draw */
    Segment2D *xErrorBars; /**< Point to start of this pen's X-error bar segments in the element's array. */
    Segment2D *yErrorBars; /**< Point to start of this pen's Y-error bar segments in the element's array. */
    Tcl_Size xErrorBarCnt; /**< # of error bars for this pen. */
    Tcl_Size yErrorBarCnt; /**< # of error bars for this pen. */
    int errorBarCapWidth; /**< Length of the cap ends on each error bar. */
    int symbolSize; /**< Size of the pen's symbol scaled to the current graph size. */
    /* Bar chart specific data. */
    BarRectangle *rectangles; /**< Indicates starting location in bar array for this pen. */
    Tcl_Size nRects; /**< Number of bar segments for this pen. */
} BarPenStyle;

/**
 * @ingroup bar_geometry
 * @brief A concrete bar element with a first-member Element header and embedded pen.
 * Owns normal/active rectangles and source-index maps; common error arrays
 * reside in the Element header.
 */
typedef struct {
    /*
     * Common element state. This must remain the first member.
     */
    Element core; /**< Common Element prefix; must remain first. */
    /*
     * Bar specific attributes
     */
    BarPen builtinPen; /**< Embedded default bar pen owned by this element. */
    Tcl_Size *rectToData; /**< Owned map from each displayed rectangle to its source sample index. */
    BarRectangle *rectangles; /**< Array of rectangles comprising the bar segments of the element. */
    Tcl_Size nRects; /**< # of visible bar segments for element */
    int padX; /**< Spacing on either side of bar */
    double barWidth; /**< Requested bar width in X data units; zero inherits graph width. */
    int barAlign; /**< Anchor of the full group interval. */
    int errorBarPosition; /**< Data coordinates or final bar placement. */
    double barOffset; /**< Translation in data X coordinates. */
    Tcl_Size nActive; /**< Number of mapped active rectangles. */
    BarRectangle *activeRects; /**< Owned mapped active rectangle array. */
    Tcl_Size *activeToData; /**< Owned map from active rectangles to source indices. */
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
#define DEF_BAR_HIDE_PLOT "no"
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

enum { BAR_ALIGN_CENTER, BAR_ALIGN_LEFT, BAR_ALIGN_RIGHT };

static const char *const errorBarPositionNames[] = {"data", "bar", NULL};

static const char *const barAlignNames[] = {"center", "left", "right", NULL};

/** @ingroup component_option_tables
 * @brief Bar element data bindings, placement, error presentation and pen selection.
 * @hideinitializer
 */
static const Tk_OptionSpec barElemOptionSpecs[] = {
    {TK_OPTION_STRING, "-activepen", "activePen", "ActivePen", DEF_BAR_ACTIVE_PEN, BAR_CORE_OFFSET(activePenObjPtr), -1,
     0, NULL, BAR_ELEM_PEN_MASK},
    {TK_OPTION_BORDER, "-background", "background", "Background", DEF_BAR_BACKGROUND, -1,
     BAR_BUILTIN_PEN_OFFSET(border), TK_OPTION_NULL_OK, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING_TABLE, "-baralign", "barAlign", "BarAlign", "center", -1,
     offsetof(Bar, barAlign), 0, barAlignNames, BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_DOUBLE, "-baroffset", "barOffset", "BarOffset", "0.0", -1,
     offsetof(Bar, barOffset), 0, NULL, BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_DOUBLE, "-barwidth", "barWidth", "BarWidth", DEF_BAR_WIDTH, -1, offsetof(Bar, barWidth), 0, NULL,
     BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_SYNONYM, "-bd", NULL, NULL, NULL, -1, -1, 0, "-borderwidth", 0},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL, NULL, -1, -1, 0, "-background", 0},
    {TK_OPTION_STRING, "-bindtags", "bindTags", "BindTags", DEF_BAR_TAGS, BAR_CORE_OFFSET(bindTagsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, BAR_ELEM_TAGS_MASK},
    {TK_OPTION_PIXELS, "-borderwidth", "borderWidth", "BorderWidth", DEF_BAR_BORDERWIDTH,
     BAR_BUILTIN_PEN_OFFSET(borderWidthObjPtr), -1, 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING, "-errorbarcolor", "errorBarColor", "ErrorBarColor", DEF_BAR_ERRORBAR_COLOR,
     BAR_BUILTIN_PEN_OFFSET(errorBarColorObjPtr), -1, 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING_TABLE, "-errorbarposition", "errorBarPosition", "ErrorBarPosition", "data", -1,
     offsetof(Bar, errorBarPosition), 0, errorBarPositionNames, BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_PIXELS, "-errorbarwidth", "errorBarWidth", "ErrorBarWidth", DEF_BAR_ERRORBAR_LINE_WIDTH,
     BAR_BUILTIN_PEN_OFFSET(errorBarWidthObjPtr), -1, 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_PIXELS, "-errorbarcap", "errorBarCap", "ErrorBarCap", DEF_BAR_ERRORBAR_CAP_WIDTH,
     BAR_BUILTIN_PEN_OFFSET(errorBarCapObjPtr), -1, 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-data", "data", "Data", NULL, BAR_CORE_OFFSET(dataObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     BAR_ELEM_DATA_MASK | BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_DOUBLE, "-fillopacity", "fillOpacity", "FillOpacity", "1.0", -1,
     BAR_BUILTIN_PEN_OFFSET(fillOpacity), 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_SYNONYM, "-fg", NULL, NULL, NULL, -1, -1, 0, "-foreground", 0},
    {TK_OPTION_COLOR, "-foreground", "foreground", "Foreground", DEF_BAR_FOREGROUND, -1,
     BAR_BUILTIN_PEN_OFFSET(fgColor), TK_OPTION_NULL_OK, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_BAR_HIDE, -1, BAR_CORE_OFFSET(hidden), 0, NULL,
     BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_BOOLEAN, "-hideplot", "hidePlot", "HidePlot", DEF_BAR_HIDE_PLOT, -1, BAR_CORE_OFFSET(plotHidden), 0,
     NULL, BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-label", "label", "Label", DEF_BAR_LABEL, -1, BAR_CORE_OFFSET(label), TK_OPTION_NULL_OK, NULL,
     BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_RELIEF, "-labelrelief", "labelRelief", "LabelRelief", DEF_BAR_LABEL_RELIEF, -1,
     BAR_CORE_OFFSET(labelRelief), 0, NULL, BAR_ELEM_MAP_ITEM_MASK},
    {TK_OPTION_STRING, "-track", "track", "Track", "", BAR_CORE_OFFSET(trackObjPtr), -1, 0, NULL,
     BAR_ELEM_AXES_MASK | BAR_ELEM_MAP_ITEM_MASK},
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
    {TK_OPTION_STRING, "-valueoffset", "valueOffset", "ValueOffset", "0 0",
     BAR_BUILTIN_PEN_OFFSET(valueOffsetObjPtr), -1, 0, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
    {TK_OPTION_STRING, "-valuecommand", "valueCommand", "ValueCommand", NULL,
     BAR_BUILTIN_PEN_OFFSET(valueCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL, BAR_ELEM_BUILTIN_PEN_MASK},
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

#define BAR_PEN_OPTION_ENTRIES(DEFAULT_BG, DEFAULT_FG)                                                                 \
    {TK_OPTION_BORDER,                                                                                                 \
     "-background",                                                                                                    \
     "background",                                                                                                     \
     "Background",                                                                                                     \
     DEFAULT_BG,                                                                                                       \
     -1,                                                                                                               \
     offsetof(BarPen, border),                                                                                         \
     TK_OPTION_NULL_OK,                                                                                                \
     NULL,                                                                                                             \
     0},                                                                                                               \
        {TK_OPTION_SYNONYM, "-bg", NULL, NULL, NULL, -1, -1, 0, "-background", 0},                                     \
        {TK_OPTION_PIXELS,                                                                                             \
         "-borderwidth",                                                                                               \
         "borderWidth",                                                                                                \
         "BorderWidth",                                                                                                \
         DEF_PEN_BORDERWIDTH,                                                                                          \
         offsetof(BarPen, borderWidthObjPtr),                                                                          \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_SYNONYM, "-bd", NULL, NULL, NULL, -1, -1, 0, "-borderwidth", 0},                                    \
        {TK_OPTION_STRING,                                                                                             \
         "-errorbarcolor",                                                                                             \
         "errorBarColor",                                                                                              \
         "ErrorBarColor",                                                                                              \
         DEF_BAR_ERRORBAR_COLOR,                                                                                       \
         offsetof(BarPen, errorBarColorObjPtr),                                                                        \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_PIXELS,                                                                                             \
         "-errorbarwidth",                                                                                             \
         "errorBarWidth",                                                                                              \
         "ErrorBarWidth",                                                                                              \
         DEF_BAR_ERRORBAR_LINE_WIDTH,                                                                                  \
         offsetof(BarPen, errorBarWidthObjPtr),                                                                        \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_PIXELS,                                                                                             \
         "-errorbarcap",                                                                                               \
         "errorBarCap",                                                                                                \
         "ErrorBarCap",                                                                                                \
         DEF_BAR_ERRORBAR_CAP_WIDTH,                                                                                   \
         offsetof(BarPen, errorBarCapObjPtr),                                                                          \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_COLOR,                                                                                              \
         "-foreground",                                                                                                \
         "foreground",                                                                                                 \
         "Foreground",                                                                                                 \
         DEFAULT_FG,                                                                                                   \
         -1,                                                                                                           \
         offsetof(BarPen, fgColor),                                                                                    \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_DOUBLE, "-fillopacity", "fillOpacity", "FillOpacity", "1.0", -1,                                  \
         offsetof(BarPen, fillOpacity), 0, NULL, 0},                                                                   \
        {TK_OPTION_SYNONYM, "-fg", NULL, NULL, NULL, -1, -1, 0, "-foreground", 0},                                     \
        {TK_OPTION_RELIEF, "-relief", "relief", "Relief", DEF_PEN_RELIEF, -1, offsetof(BarPen, relief), 0, NULL, 0},   \
        {TK_OPTION_STRING,                                                                                             \
         "-showerrorbars",                                                                                             \
         "showErrorBars",                                                                                              \
         "ShowErrorBars",                                                                                              \
         DEF_BAR_SHOW_ERRORBARS,                                                                                       \
         offsetof(BarPen, showErrorBarsObjPtr),                                                                        \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-showvalues",                                                                                                \
         "showValues",                                                                                                 \
         "ShowValues",                                                                                                 \
         DEF_PEN_SHOW_VALUES,                                                                                          \
         offsetof(BarPen, showValuesObjPtr),                                                                           \
         -1,                                                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_BITMAP,          "-stipple",        "stipple", "Stipple", DEF_PEN_STIPPLE, -1,                      \
         offsetof(BarPen, stipple), TK_OPTION_NULL_OK, NULL,      0},                                                  \
        {TK_OPTION_STRING,  "-type", NULL, NULL, DEF_PEN_TYPE, -1, BAR_PEN_CORE_OFFSET(typeId),                        \
         TK_OPTION_NULL_OK, NULL,    0},                                                                               \
        {TK_OPTION_ANCHOR,                                                                                             \
         "-valueanchor",                                                                                               \
         "valueAnchor",                                                                                                \
         "ValueAnchor",                                                                                                \
         DEF_PEN_VALUE_ANCHOR,                                                                                         \
         -1,                                                                                                           \
         offsetof(BarPen, valueStyle.anchor),                                                                          \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_COLOR,                                                                                              \
         "-valuecolor",                                                                                                \
         "valueColor",                                                                                                 \
         "ValueColor",                                                                                                 \
         DEF_PEN_VALUE_COLOR,                                                                                          \
         -1,                                                                                                           \
         offsetof(BarPen, valueStyle.color),                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_FONT,                                                                                               \
         "-valuefont",                                                                                                 \
         "valueFont",                                                                                                  \
         "ValueFont",                                                                                                  \
         DEF_PEN_VALUE_FONT,                                                                                           \
         -1,                                                                                                           \
         offsetof(BarPen, valueStyle.font),                                                                            \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING, "-valueoffset", "valueOffset", "ValueOffset", "0 0", \
         offsetof(BarPen, valueOffsetObjPtr), -1, 0, NULL, 0}, \
        {TK_OPTION_STRING, "-valuecommand", "valueCommand", "ValueCommand", NULL, \
         offsetof(BarPen, valueCommandObjPtr), -1, TK_OPTION_NULL_OK, NULL, 0}, \
        {TK_OPTION_STRING,                                                                                             \
         "-valueformat",                                                                                               \
         "valueFormat",                                                                                                \
         "ValueFormat",                                                                                                \
         DEF_PEN_VALUE_FORMAT,                                                                                         \
         -1,                                                                                                           \
         offsetof(BarPen, valueFormat),                                                                                \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_DOUBLE,                                                                                             \
         "-valuerotate",                                                                                               \
         "valueRotate",                                                                                                \
         "ValueRotate",                                                                                                \
         "0.0",                                                                                                        \
         offsetof(BarPen, valueRotateObjPtr),                                                                          \
         offsetof(BarPen, valueStyle.theta),                                                                           \
         0,                                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_STRING,                                                                                             \
         "-valueshadow",                                                                                               \
         "valueShadow",                                                                                                \
         "ValueShadow",                                                                                                \
         DEF_PEN_VALUE_SHADOW,                                                                                         \
         offsetof(BarPen, valueShadowObjPtr),                                                                          \
         -1,                                                                                                           \
         TK_OPTION_NULL_OK,                                                                                            \
         NULL,                                                                                                         \
         0},                                                                                                           \
        {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}

/** @ingroup component_option_tables
 * @brief Shared bar pen entries instantiated with normal foreground and background defaults.
 * @hideinitializer
 */
static const Tk_OptionSpec normalBarPenOptionSpecs[] = {
    BAR_PEN_OPTION_ENTRIES(DEF_PEN_NORMAL_BACKGROUND, DEF_PEN_NORMAL_FOREGROUND)};
/** @ingroup component_option_tables
 * @brief The same bar pen entries instantiated with active foreground and background defaults.
 * @hideinitializer
 */
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
static ElementExportProc ActiveBarExport;
static ElementExportProc NormalBarExport;
static ElementSymbolExportProc SymbolExport;
static ElementMapProc MapBar;

static int Round(register double x);
static void ClearPalette(Rbc_Chain *palette);
static void InitPen(BarPen *penPtr, const Tk_OptionSpec *optionSpecs, unsigned int flags);

/**
 * @ingroup bar_layout
 * @brief Select the owning track override or the graph bar mode.
 *
 * The borrowed Y axis must be live. A nonnegative track barMode overrides
 * the graph mode; axis identity, rather than this helper, isolates grouping.
 * No state is changed.
 *
 * @param[in] graphPtr Borrowed graph supplying the fallback bar mode.
 * @param[in] yAxis Live owning Y axis; its track may override the graph mode.
 */
static int GetTrackBarMode(Graph *graphPtr, Axis *yAxis) {
    return yAxis->track != NULL && yAxis->track->barMode >= 0 ? yAxis->track->barMode : graphPtr->mode;
}

static void CheckStacks(Graph *graphPtr, Axis2D *pairPtr, double *minPtr, double *maxPtr);
static void MergePens(Bar *barPtr, PenStyle **dataToStyle);
static void MapActiveBars(Bar *barPtr);
static void ResetBar(Bar *barPtr);
static void DrawBarSegments(Graph *graphPtr, Drawable drawable, BarPen *penPtr, BarRectangle *rectangles,
                            Tcl_Size nRects);
static void DrawBarValues(Graph *graphPtr, Drawable drawable, Bar *barPtr, BarPen *penPtr, BarRectangle *rectangles,
                          Tcl_Size nRects, const Tcl_Size *rectToData);
static void SegmentsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, BarPen *penPtr, BarRectangle *rectPtr,
                                 Tcl_Size nRects);
static void BarValuesExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Bar *barPtr, BarPen *penPtr,
                                  BarRectangle *rectangles, Tcl_Size nRects, const Tcl_Size *rectToData);

/**
 * @ingroup bar_configuration
 * @brief Recognize a nonempty, case-sensitive prefix of a color keyword.
 *
 * The borrowed strings must contain at least their stated lengths. Returns
 * a boolean; an empty spelling and a spelling longer than fullName fail.
 *
 * @param[in] string Borrowed candidate spelling, readable for length bytes.
 * @param[in] length Candidate byte count; nonpositive or overlong spellings do not match.
 * @param[in] fullName Borrowed NUL-terminated keyword spelling.
 */
static int IsBarPenPrefix(const char *string, Tcl_Size length, const char *fullName) {
    Tcl_Size fullLength;

    fullLength = (Tcl_Size)strlen(fullName);
    return ((length > 0) && (length <= fullLength) && (strncmp(string, fullName, (size_t)length) == 0));
}

/**
 * @ingroup bar_configuration
 * @brief Resolve an optional error color into a staged resource.
 *
 * NULL or an empty object yields NULL; a nonempty prefix of defcolor yields
 * COLOR_DEFAULT. Other spellings acquire a Tk color reference. Returns TCL_OK
 * and writes colorPtrPtr on success, or TCL_ERROR with an interpreter diagnostic
 * and the output unchanged. Release acquired colors with FreeBarPenColor.
 *
 * @param[in] interp Borrowed interpreter receiving color errors; see @ref rbc_interp_argument.
 * @param[in] tkwin Borrowed live Tk window used to acquire a color reference.
 * @param[in] objPtr Borrowed option object; NULL and empty mean no explicit color.
 * @param[out] colorPtrPtr Required output slot; unchanged on failure. Successful values require
 *    sentinel-aware release.
 */
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

/**
 * @ingroup bar_configuration
 * @brief Release an acquired color while accepting the default sentinel.
 *
 * NULL and COLOR_DEFAULT are no-ops. Does not clear the caller's pointer or
 * release a retained option object.
 *
 * @param[in] colorPtr Owned Tk color reference to release, or NULL/COLOR_DEFAULT for no release.
 */
static void FreeBarPenColor(XColor *colorPtr) {
    if ((colorPtr != NULL) && (colorPtr != COLOR_DEFAULT)) {
        Tk_FreeColor(colorPtr);
    }
}

/**
 * @ingroup bar_configuration
 * @brief Resolve a bar color consistently for native GCs and renderer output.
 *
 * Borrows the pen and candidate color. NULL and COLOR_DEFAULT inherit the
 * foreground, then the border color, then screen black. Returns a borrowed
 * color or initializes and returns caller-owned fallback storage. The caller
 * must keep fallback alive while the renderer uses the returned pointer.
 * Accepting a candidate color also supports ConfigurePen before commit.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in] penPtr Borrowed configured concrete bar pen; drawing resources must remain valid throughout
 *    the call.
 * @param[in] colorPtr Borrowed candidate color, NULL or COLOR_DEFAULT; no reference is acquired.
 * @param[out] fallbackPtr Required caller storage if all inherited colors are absent; retain through
 *    renderer use.
 */
static const XColor *GetBarColor(Graph *graphPtr, const BarPen *penPtr,
                                 const XColor *colorPtr, XColor *fallbackPtr) {
    if ((colorPtr != NULL) && (colorPtr != COLOR_DEFAULT)) {
        return colorPtr;
    }
    if (penPtr->fgColor != NULL) {
        return penPtr->fgColor;
    }
    if (penPtr->border != NULL) {
        return Tk_3DBorderColor(penPtr->border);
    }
    memset(fallbackPtr, 0, sizeof(*fallbackPtr));
    fallbackPtr->pixel = BlackPixel(graphPtr->display, Tk_ScreenNumber(graphPtr->tkwin));
    fallbackPtr->flags = DoRed | DoGreen | DoBlue;
    return fallbackPtr;
}

/**
 * @ingroup bar_layout
 * @brief Round a bounded pixel distance with half values away from zero.
 *
 * The caller must supply a finite value whose adjusted value fits in int.
 * This helper does not clamp or report conversion errors.
 *
 * @param[in] x Finite pixel distance whose half-adjusted value is representable as int.
 */
static int Round(register double x) { return (int)(x + ((x < 0.0) ? -0.5 : 0.5)); }

/**
 * @ingroup bar_layout
 * @brief Validate a Tcl_Size array count before size_t multiplication.
 *
 * Rejects negative counts, zero element size and multiplication overflow.
 * Returns TCL_OK and writes bytesPtr on success (including a zero count),
 * otherwise TCL_ERROR with the output unchanged. No interpreter diagnostic
 * or allocation is performed.
 *
 * @param[in] count Nonnegative number of array entries; zero is accepted.
 * @param[in] elementSize Positive byte size of one entry.
 * @param[out] bytesPtr Required output slot for the product; unchanged on error.
 */
static int GetBarArrayByteCount(Tcl_Size count, size_t elementSize, size_t *bytesPtr) {
    if ((count < 0) || (elementSize == 0) || ((uintmax_t)count > (uintmax_t)(SIZE_MAX / elementSize))) {
        return TCL_ERROR;
    }
    *bytesPtr = (size_t)count * elementSize;
    return TCL_OK;
}
/**
 * @ingroup bar_geometry
 * @brief Invalidate rectangle and error-segment counts in every style.
 *
 * The palette and styles are borrowed. Slice pointers are not cleared or
 * freed; their zero counts prevent use until remapping replaces the views.
 * Pen references and weight ranges are retained.
 *
 * @param[in,out] palette Initialized chain of bar styles; counts are cleared while pointers and pen
 *    references remain.
 */
static void ClearPalette(Rbc_Chain *palette) {
    register BarPenStyle *stylePtr;
    Rbc_ChainLink *linkPtr;

    for (linkPtr = Rbc_ChainFirstLink(palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        stylePtr->xErrorBarCnt = stylePtr->yErrorBarCnt = stylePtr->nRects = 0;
    }
}

/**
 * @ingroup bar_configuration
 * @brief Validate and replace a bar pen's derived drawing resources.
 *
 * Tk has already installed candidate option fields. Validate opacity,
 * label offset, command prefix, format, rotation, pixel widths, show masks
 * and shadow before replacing any derived resource. Acquire all replacement
 * GCs before freeing the old ones. Returns TCL_OK on commit; TCL_ERROR frees
 * staged colors and leaves derived resources unchanged. The caller owns
 * rollback of the candidate Tk option fields. Validation does not invoke the
 * value-label command. Works for named pens and the element's embedded pen.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in,out] penPtr Common header of a concrete bar pen with candidate Tk option fields already
 *    installed.
 */
static int ConfigurePen(Graph *graphPtr, Pen *penPtr) {
    BarPen *bpPtr;
    XColor *newErrorBarColor;
    int newBorderWidth;
    int newErrorBarCapWidth;
    int newErrorBarLineWidth;
    int newErrorBarShow;
    int newValueShow;
    Point2D newValueOffset;
    Shadow newShadow;
    XGCValues gcValues;
    unsigned long gcMask;
    int fillStyle;
    XColor fallbackColor;
    unsigned long defColor;
    GC newGC;
    GC newErrorBarGC;
    GC newValueGC;

    assert(penPtr->optionSpecs != NULL);
    bpPtr = BAR_PEN_FROM_CORE(penPtr);
    newErrorBarColor = NULL;
    newShadow.color = NULL;
    newShadow.offset = 0;
    if ((!FINITE(bpPtr->fillOpacity)) || (bpPtr->fillOpacity < 0.0) || (bpPtr->fillOpacity > 1.0)) {
        Tcl_SetObjResult(graphPtr->interp,
                         Tcl_NewStringObj("-fillopacity must be a finite number between 0 and 1", -1));
        goto error;
    }
    if (Rbc_GetValueOffset(graphPtr->interp, bpPtr->valueOffsetObjPtr, &newValueOffset) != TCL_OK) {
        goto error;
    }
    if (Rbc_ValidateValueCommand(graphPtr->interp, bpPtr->valueCommandObjPtr) != TCL_OK) {
        goto error;
    }
    if (Rbc_ValidateValueFormat(graphPtr->interp, bpPtr->valueFormat) != TCL_OK) {
        goto error;
    }
    /*
     * Rotation eventually reaches trigonometric and screen-coordinate
     * calculations, so a non-finite angle is not meaningful.
     */
    if (!FINITE(bpPtr->valueStyle.theta)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-valuerotate must be a finite value", -1));
        goto error;
    }
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
    defColor = GetBarColor(graphPtr, bpPtr, NULL, &fallbackColor)->pixel;
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
    gcValues.foreground = GetBarColor(graphPtr, bpPtr, newErrorBarColor, &fallbackColor)->pixel;
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
    bpPtr->valueOffset = newValueOffset;
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

/**
 * @ingroup bar_configuration
 * @brief Release bar-pen resources derived outside the Tk option table.
 *
 * Frees text-style drawing resources, fill/error GCs, the derived error
 * color and shadow color, clearing released pointers. Does not free the pen
 * record, name, or Tk-managed option resources; shared pen/element teardown
 * owns those. The embedded pen must not be freed as a standalone allocation.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in,out] penPtr Concrete bar pen header whose derived resources are released; allocation and Tk
 *    option fields remain.
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

/**
 * @ingroup bar_configuration
 * @brief Initialize methods and fallback fields in a zeroed bar pen.
 *
 * The caller supplies storage and a non-NULL option specification. Installs
 * configuration/destruction callbacks and initializes text style and flags.
 * Does not apply Tk defaults, allocate drawing GCs or register a named pen.
 *
 * @param[in,out] penPtr Caller-owned zeroed concrete storage; initialized in place, not registered.
 * @param[in] optionSpecs Non-NULL option template retained by pointer; must outlive the pen.
 * @param[in] flags Initial common pen flags, such as NORMAL_PEN or ACTIVE_PEN.
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

/**
 * @ingroup bar_configuration
 * @brief Allocate an unregistered named bar pen for shared pen creation.
 *
 * Returns the common Pen header of owned, zeroed concrete storage with a
 * copied name. The exact name activeBar selects active option defaults;
 * other names select normal defaults. Allocation is asserted. The shared
 * pen manager must initialize options, register/configure the pen and own
 * its eventual destruction. This is an internal constructor, not a stubs API.
 *
 * @param[in] penName Borrowed NUL-terminated name copied into the new pen; exact activeBar selects
 *    active defaults.
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
    penPtr = Rbc_Calloc(1, sizeof(BarPen));
    assert(penPtr != NULL);
    InitPen(penPtr, optionSpecs, flags);
    corePtr = &penPtr->core;
    corePtr->name = Rbc_Strdup(penName);
    return corePtr;
}

/**
 * @ingroup bar_layout
 * @brief Expand Y limits using precomputed totals for one axis pair.
 *
 * Only operates in the effective stacked mode with duplicate metadata.
 * Reads matching records in graph-owned freqArr; negative totals may lower
 * minPtr, nonnegative totals may raise maxPtr. Requires Rbc_ComputeStacks
 * first. Does not reset counters or inspect intermediate cumulative heights.
 *
 * @param[in] graphPtr Borrowed graph with current frequency metadata and computed stack sums.
 * @param[in] pairPtr Borrowed axis pair selecting the matching duplicate records.
 * @param[in,out] minPtr Required current Y minimum; may be lowered by negative totals.
 * @param[in,out] maxPtr Required current Y maximum; may be raised by nonnegative totals.
 */
static void CheckStacks(Graph *graphPtr, Axis2D *pairPtr, double *minPtr, double *maxPtr) {
    FreqInfo *infoPtr;
    Tcl_Size i;

    if ((GetTrackBarMode(graphPtr, pairPtr->y) != MODE_STACKED) || (graphPtr->nStacks == 0)) {
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

/**
 * @ingroup bar_configuration
 * @brief Stage element dependencies before committing bar configuration.
 *
 * Called after Tk installs candidate options. Rejects nonfinite width and
 * offset; a finite nonpositive element width retains graph-width fallback.
 * Prepares changed data, pens, axes, state, tags and styles, then configures
 * the embedded pen if needed. Commits dependencies and synchronizes retained
 * data objects only after fallible preparation succeeds. Updates the palette
 * default pen and sets MAP_ITEM for mapping-related changes. Returns TCL_OK
 * on success; TCL_ERROR frees prepared candidates and leaves Tk option
 * rollback to the caller. Data preparation can evaluate expressions: ordinary
 * rollback is not a guarantee against arbitrary callback side effects.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in,out] elemPtr Common header of a live bar element with candidate Tk fields and option-mask
 *    state.
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
     * Non-positive element bar widths retain their historical meaning:
     * use the graph-wide bar width.  Only non-finite values are invalid.
     */
    if (!FINITE(barPtr->barOffset)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-baroffset must be a finite value", -1));
        goto error;
    }
    if (!FINITE(barPtr->barWidth)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-barwidth must be a finite value", -1));
        goto error;
    }
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

/**
 * @ingroup bar_layout
 * @brief Return the configured baseline with the historical log fallback.
 *
 * Uses one when the Y axis is logarithmic and the configured baseline is
 * nonpositive. Borrows the live element, graph and Y axis; no state is changed.
 *
 * @param[in] elemPtr Borrowed bar element with live graph and selected Y axis.
 */
static double GetBarBaseline(Element *elemPtr) {
    double baseline = elemPtr->graphPtr->baseline;

    if (elemPtr->axes.y->logScale && (baseline <= 0.0)) {
        baseline = 1.0;
    }
    return baseline;
}

/**
 * @ingroup bar_layout
 * @brief Return the anchor displacement for the full data-X interval.
 *
 * Left gives zero, right gives width and center gives half width. Apply
 * before grouped subdivision. Width is the caller's effective bar width;
 * this helper neither validates it nor advances grouping counters.
 *
 * @param[in] barPtr Borrowed bar supplying alignment.
 * @param[in] width Effective data-X width of the full bar/group interval, before subdivision.
 */
static double BarAnchorOffset(const Bar *barPtr, double width) {
    switch (barPtr->barAlign) {
    case BAR_ALIGN_LEFT:
        return 0.0;
    case BAR_ALIGN_RIGHT:
        return width;
    default:
        return width * 0.5;
    }
}

/**
 * @ingroup bar_layout
 * @brief Compute one bar's data-space corners and consume its group position.
 * @param graphPtr Borrowed graph with initialized grouping state.
 * @param barPtr Borrowed bar element and axis pair.
 * @param index Valid index in both source arrays.
 * @param clip Reject full X intervals outside linear-axis viewport limits.
 * @param firstPtr Receives the lower X edge and placed data-value Y.
 * @param secondPtr Receives the upper X edge and baseline or previous stack Y.
 * @return TRUE for a placement; FALSE leaves the output corners unchanged.
 * @details Apply each element's width, alignment and offset before subdivision.
 * Duplicate keys use the original X value and both axis identities. Successful
 * grouped placement advances count or lastY; this is not a pure query. Reset
 * grouping state before a new ordered pass. Log-invalid and nonfinite input,
 * invalid full X intervals, viewport rejection and stack overflow are skipped.
 * Corners are not screen mapped or reordered for negative-height bars.
 */
static int PlaceBar(Graph *graphPtr, Bar *barPtr, Tcl_Size index, int clip,
                    Point2D *firstPtr, Point2D *secondPtr) {
    Point2D c1, c2;
    FreqKey key;
    double x = barPtr->core.x.valueArr[index];
    double y = barPtr->core.y.valueArr[index];
    double barWidth = (barPtr->barWidth > 0.0) ? barPtr->barWidth : graphPtr->barWidth;
    double barOffset = BarAnchorOffset(barPtr, barWidth);
    double baseline = GetBarBaseline(&barPtr->core);

    if ((!FINITE(x)) || (!FINITE(y))) {
        return FALSE;
    }
    c1.x = (x + barPtr->barOffset) - barOffset;
    c2.x = c1.x + barWidth;
    if ((!FINITE(c1.x)) || (!FINITE(c2.x)) ||
        (barPtr->core.axes.x->logScale && (c1.x <= 0.0))) {
        return FALSE;
    }
    if (barPtr->core.axes.y->logScale && (y <= 0.0)) {
        return FALSE;
    }
    if (clip && (!barPtr->core.axes.x->logScale) &&
        ((c1.x > barPtr->core.axes.x->axisRange.max) ||
         (c2.x < barPtr->core.axes.x->axisRange.min))) {
        return FALSE;
    }
    c1.y = y;
    c2.y = baseline;
    /*
     * If the mode is "aligned" or "stacked" we need to adjust the
     * x or y coordinates of the two corners.
     */
    if ((graphPtr->nStacks > 0) && (GetTrackBarMode(graphPtr, barPtr->core.axes.y) != MODE_INFRONT)) {
        Tcl_HashEntry *hPtr;

        key.value = (x == 0.0) ? 0.0 : x;
        key.axes = barPtr->core.axes;
        hPtr = Tcl_FindHashEntry(&(graphPtr->freqTable), (char *)&key);
        if (hPtr != NULL) {
            FreqInfo *infoPtr;
            double slice, width;

            infoPtr = (FreqInfo *)Tcl_GetHashValue(hPtr);
            switch (GetTrackBarMode(graphPtr, barPtr->core.axes.y)) {
            case MODE_STACKED: {
                double previousY;
                double stackedY;

                previousY = infoPtr->lastY;
                stackedY = c1.y + previousY;
                /*
                 * Do not let one overflowing stack value poison every subsequent
                 * bar at this abscissa.
                 */
                if (!FINITE(stackedY)) {
                    return FALSE;
                }
                /*
                 * The stack accumulator starts at zero, because stacked values are
                 * additive data quantities.  Zero itself cannot be drawn on a
                 * logarithmic axis, so use the effective log-bar baseline
                 * only as the lower edge of the first visible stacked segment.
                 */
                if (barPtr->core.axes.y->logScale && (previousY <= 0.0)) {
                    c2.y = baseline;

                } else {
                    c2.y = previousY;
                }
                c1.y = stackedY;
                infoPtr->lastY = stackedY;
            } break;
            case MODE_ALIGNED:
                infoPtr->count++;
                slice = barWidth / (double)infoPtr->freq;
                c1.x += slice * (double)(infoPtr->freq - infoPtr->count);
                c2.x = c1.x + slice;
                break;
            case MODE_OVERLAP:
                infoPtr->count++;
                slice = barWidth / ((double)infoPtr->freq * 2.0);
                width = slice * ((double)infoPtr->freq + 1.0);
                c1.x += slice * (double)(infoPtr->freq - infoPtr->count);
                c2.x = c1.x + width;
                break;
            case MODE_INFRONT:
                break;
            }
        }
    }
    *firstPtr = c1;
    *secondPtr = c2;
    return TRUE;
}

/**
 * @ingroup bar_geometry
 * @brief Allocate per-source error translations initially marked invalid.
 *
 * Returns an owned Point2D array with both coordinates set to NaN in every
 * slot, or NULL if the byte count is invalid. Allocation uses ckalloc; this
 * is not a recoverable allocator-failure interface. The caller frees the array
 * after mapping or expanding error limits.
 *
 * @param[in] nPoints Source-slot count; each allocated slot starts with NaN X/Y offsets. Invalid size
 *    returns NULL.
 */
static Point2D *NewBarErrorOffsets(Tcl_Size nPoints) {
    Point2D *offsets;
    size_t bytes;
    Tcl_Size i;

    if (GetBarArrayByteCount(nPoints, sizeof(*offsets), &bytes) != TCL_OK) {
        return NULL;
    }
    offsets = ckalloc(bytes);
    for (i = 0; i < nPoints; i++) {
        offsets[i].x = offsets[i].y = NAN;
    }
    return offsets;
}

/**
 * @ingroup bar_geometry
 * @brief Store the placed-center and placed-value displacement of one sample.
 *
 * All pointers are borrowed and index must be valid in both source arrays
 * and offsets. Corners must come from PlaceBar before swapping Y edges or
 * screen mapping. Averages X endpoints as two halves and subtracts source X;
 * subtracts source Y from the first corner's Y. Does not allocate or validate.
 *
 * @param[in] barPtr Borrowed bar with original X/Y arrays.
 * @param[in] index Original source index valid in both source arrays and offsets.
 * @param[in] firstPtr Borrowed placed lower-X/value-Y corner before screen mapping or Y-edge swaps.
 * @param[in] secondPtr Borrowed placed upper-X/baseline-Y corner from the same placement.
 * @param[in,out] offsets Caller-owned per-source array; only the entry at index is overwritten.
 */
static void SetBarErrorOffset(Bar *barPtr, Tcl_Size index, const Point2D *firstPtr,
                              const Point2D *secondPtr, Point2D *offsets) {
    offsets[index].x = firstPtr->x * 0.5 + secondPtr->x * 0.5 - barPtr->core.x.valueArr[index];
    offsets[index].y = firstPtr->y - barPtr->core.y.valueArr[index];
}

/**
 * @ingroup bar_layout
 * @brief Compute bar data limits and translated error limits.
 * @param elemPtr Borrowed bar element.
 * @param extsPtr Receives bounds, or sentinel empty bounds without paired data.
 * @details Includes full anchored X intervals, baseline rules and duplicate
 * stack totals. Calls PlaceBar without viewport rejection to advance group
 * state even when error bars retain data positioning. In bar positioning mode,
 * owns a temporary per-source translation array and frees it after expanding
 * error limits. The caller must reset group state before the ordered pass.
 */
static void GetBarExtents(Element *elemPtr, Extents2D *extsPtr) {
    Graph *graphPtr = elemPtr->graphPtr;
    Bar *barPtr = BAR_FROM_CORE(elemPtr);
    double anchorOffset, barWidth, baseline;
    Tcl_Size nPoints, i;
    Point2D *errorOffsets = NULL;

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
    anchorOffset = BarAnchorOffset(barPtr, barWidth);
    extsPtr->left = (barPtr->core.x.min + barPtr->barOffset) - anchorOffset;
    extsPtr->right = (barPtr->core.x.max + barPtr->barOffset) - anchorOffset + barWidth;
    extsPtr->top = barPtr->core.y.min;
    extsPtr->bottom = barPtr->core.y.max;
    baseline = GetBarBaseline(elemPtr);
    if (extsPtr->bottom < baseline) {
        extsPtr->bottom = baseline;
    }
    /*
     * Handle "stacked" bar elements specially.
     *
     * If element is stacked, the sum of its ordinates may be outside
     * the minimum/maximum limits of the element's data points.
     */
    if ((GetTrackBarMode(graphPtr, elemPtr->axes.y) == MODE_STACKED) && (graphPtr->nStacks > 0)) {
        CheckStacks(graphPtr, &(elemPtr->axes), &(extsPtr->top), &(extsPtr->bottom));
    }
    if (elemPtr->axes.x->logScale) {
        Tcl_Size i;

        extsPtr->left = DBL_MAX;
        extsPtr->right = -DBL_MAX;
        for (i = 0; i < nPoints; i++) {
            double left, right;

            left = (elemPtr->x.valueArr[i] + barPtr->barOffset) - anchorOffset;
            right = left + barWidth;
            if (FINITE(left) && FINITE(right) && (left > 0.0)) {
                extsPtr->left = MIN(extsPtr->left, left);
                extsPtr->right = MAX(extsPtr->right, right);
            }
        }
    }
    /* Fix y-min limits for barchart */
    if (elemPtr->axes.y->logScale) {
        if (extsPtr->top <= 0.0) {
            Tcl_Size i;

            extsPtr->top = baseline;
            for (i = 0; i < nPoints; i++) {
                double value = barPtr->core.y.valueArr[i];

                if (FINITE(value) && (value > 0.0) && (value < extsPtr->top)) {
                    extsPtr->top = value;
                }
            }
        }
        if (extsPtr->top > baseline) {
            extsPtr->top = baseline;
        }
    } else {
        if (extsPtr->top > 0.0) {
            extsPtr->top = 0.0;
        }
    }
    if (barPtr->errorBarPosition) {
        errorOffsets = NewBarErrorOffsets(nPoints);
    }
    for (i = 0; i < nPoints; i++) {
        Point2D first, second;

        if (PlaceBar(graphPtr, barPtr, i, FALSE, &first, &second) && (errorOffsets != NULL)) {
            SetBarErrorOffset(barPtr, i, &first, &second, errorOffsets);
        }
    }
    if (errorOffsets != NULL) {
        Rbc_ExpandErrorBarExtentsTranslated(elemPtr, extsPtr, 0.0, errorOffsets);
        ckfree(errorOffsets);
    } else if (!barPtr->errorBarPosition) {
        Rbc_ExpandErrorBarExtentsOffset(elemPtr, extsPtr, barPtr->barOffset);
    }
}

/**
 * @ingroup bar_layout
 * @brief Search mapped bar rectangles while retaining source identity.
 * @param graphPtr Borrowed graph (unused by this search).
 * @param elemPtr Borrowed bar element with current mapped rectangles.
 * @param searchPtr In/out search; replaced only for a strictly closer hit.
 * @details Interior hits have zero distance; other candidates use rectangle
 * edges. rectToData translates mapped order back to source order. Reported X
 * and Y are original data values, not shifted centers or cumulative stack
 * values. No arrays are allocated and no graph state is changed.
 */
static void ClosestBar(Graph *graphPtr, Element *elemPtr, ClosestSearch *searchPtr) {
    Bar *barPtr = BAR_FROM_CORE(elemPtr);
    Point2D *pointPtr, *endPtr;
    Point2D sample, t, outline[5];
    BarRectangle *rectPtr;
    double left, right, top, bottom;
    double minDist, dist;
    Tcl_Size imin;
    Tcl_Size i;

    minDist = searchPtr->dist;
    imin = 0;
    sample.x = searchPtr->x;
    sample.y = searchPtr->y;
    rectPtr = barPtr->rectangles;
    for (i = 0; i < barPtr->nRects; i++, rectPtr++) {
        left = (double)rectPtr->x;
        top = (double)rectPtr->y;
        right = left + (double)rectPtr->width;
        bottom = top + (double)rectPtr->height;
        if ((searchPtr->x >= left) && (searchPtr->x <= right) && (searchPtr->y >= top) && (searchPtr->y <= bottom)) {
            imin = barPtr->rectToData[i];
            minDist = 0.0;
            break;
        }
        outline[4].x = outline[3].x = outline[0].x = left;
        outline[4].y = outline[1].y = outline[0].y = top;
        outline[2].x = outline[1].x = right;
        outline[3].y = outline[2].y = bottom;
        for (pointPtr = outline, endPtr = outline + 4; pointPtr < endPtr; pointPtr++) {
            dist = Rbc_GetClosestPointOnSegment(&sample, pointPtr, pointPtr + 1, &t);
            if (dist < minDist) {
                minDist = dist;
                imin = barPtr->rectToData[i];
            }
        }
    }
    if (minDist < searchPtr->dist) {
        searchPtr->elemPtr = &barPtr->core;
        searchPtr->dist = minDist;
        searchPtr->index = imin;
        searchPtr->point.x = (double)barPtr->core.x.valueArr[imin];
        searchPtr->point.y = (double)barPtr->core.y.valueArr[imin];
    }
}

/**
 * @ingroup bar_geometry
 * @brief Look up mapped bounds by original source index.
 *
 * The non-NULL element must be a bar; no class check or remapping occurs.
 * Returns TRUE and writes physical window bounds on a match. Returns FALSE
 * for missing geometry, invalid arguments or an unmapped index, leaving
 * output unchanged. Bounds include placement, inversion and mapping's
 * window clamp, but are not intersected with the final renderer plot clip.
 * Borrows all storage and does not invoke callbacks.
 *
 * @param[in] elemPtr Borrowed bar element with current mapped storage; NULL returns FALSE, other classes
 *    are invalid.
 * @param[in] dataIndex Original source index, not a mapped rectangle index; negative values return
 *    FALSE.
 * @param[out] extsPtr Required output for success; NULL returns FALSE. Bounds remain unchanged on a
 *    miss.
 */
int Rbc_GetBarRectangle(Element *elemPtr, Tcl_Size dataIndex, Extents2D *extsPtr) {
    Bar *barPtr;
    Tcl_Size i;

    if ((elemPtr == NULL) || (extsPtr == NULL) || (dataIndex < 0)) {
        return FALSE;
    }
    barPtr = BAR_FROM_CORE(elemPtr);
    if ((barPtr->rectangles == NULL) || (barPtr->rectToData == NULL)) {
        return FALSE;
    }
    for (i = 0; i < barPtr->nRects; i++) {
        BarRectangle *rectPtr;

        if (barPtr->rectToData[i] != dataIndex) {
            continue;
        }
        rectPtr = barPtr->rectangles + i;
        extsPtr->left = (double)rectPtr->x;
        extsPtr->top = (double)rectPtr->y;
        extsPtr->right = extsPtr->left + (double)rectPtr->width;
        extsPtr->bottom = extsPtr->top + (double)rectPtr->height;
        return TRUE;
    }
    return FALSE;
}

/**
 * @ingroup bar_geometry
 * @brief Group rectangles and errors into palette slices with source maps.
 *
 * Requires a nonempty palette and a dataToStyle entry for each referenced
 * source index. With one style, installs borrowed views without copying.
 * Otherwise allocates replacement arrays, visits styles in palette order,
 * preserves order within each style and replaces the owned geometry and
 * index maps together. Styles borrow slices, never own them. Invalid byte
 * counts cause an early return; there is no Tcl error result. Does not change
 * source vectors or the separately copied active geometry.
 *
 * @param[in,out] barPtr Mapped bar with nonempty palette; owned geometry/index arrays may be replaced
 *    together.
 * @param[in] dataToStyle Borrowed source-indexed style lookup covering every referenced source sample.
 */
static void MergePens(Bar *barPtr, PenStyle **dataToStyle) {
    BarPenStyle *stylePtr;
    Rbc_ChainLink *linkPtr;

    size_t rectanglesBytes = 0;
    size_t rectToDataBytes = 0;
    size_t xErrorBarsBytes = 0;
    size_t xErrorToDataBytes = 0;
    size_t yErrorBarsBytes = 0;
    size_t yErrorToDataBytes = 0;
    if (Rbc_ChainGetLength(barPtr->core.palette) < 2) {
        linkPtr = Rbc_ChainFirstLink(barPtr->core.palette);
        stylePtr = Rbc_ChainGetValue(linkPtr);
        stylePtr->nRects = barPtr->nRects;
        stylePtr->rectangles = barPtr->rectangles;
        stylePtr->xErrorBarCnt = barPtr->core.xErrorBarCnt;
        stylePtr->xErrorBars = barPtr->core.xErrorBars;
        stylePtr->yErrorBarCnt = barPtr->core.yErrorBarCnt;
        stylePtr->yErrorBars = barPtr->core.yErrorBars;
        return;
    }
    if ((barPtr->nRects > 0) &&
        ((GetBarArrayByteCount(barPtr->nRects, sizeof(*barPtr->rectangles), &rectanglesBytes) != TCL_OK) ||
         (GetBarArrayByteCount(barPtr->nRects, sizeof(*barPtr->rectToData), &rectToDataBytes) != TCL_OK))) {
        return;
    }
    if ((barPtr->core.xErrorBarCnt > 0) &&
        ((GetBarArrayByteCount(barPtr->core.xErrorBarCnt, sizeof(*barPtr->core.xErrorBars), &xErrorBarsBytes) !=
          TCL_OK) ||
         (GetBarArrayByteCount(barPtr->core.xErrorBarCnt, sizeof(*barPtr->core.xErrorToData), &xErrorToDataBytes) !=
          TCL_OK))) {
        return;
    }
    if ((barPtr->core.yErrorBarCnt > 0) &&
        ((GetBarArrayByteCount(barPtr->core.yErrorBarCnt, sizeof(*barPtr->core.yErrorBars), &yErrorBarsBytes) !=
          TCL_OK) ||
         (GetBarArrayByteCount(barPtr->core.yErrorBarCnt, sizeof(*barPtr->core.yErrorToData), &yErrorToDataBytes) !=
          TCL_OK))) {
        return;
    }
    /* We have more than one style. Group bar segments of like pen
     * styles together.  */
    if (barPtr->nRects > 0) {
        BarRectangle *rectangles;
        BarRectangle *rectPtr;
        Tcl_Size *rectToData;
        Tcl_Size *indexPtr;
        Tcl_Size dataIndex;
        Tcl_Size i;

        rectangles = ckalloc(rectanglesBytes);
        rectToData = ckalloc(rectToDataBytes);
        rectPtr = rectangles;
        indexPtr = rectToData;
        for (linkPtr = Rbc_ChainFirstLink(barPtr->core.palette); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->rectangles = rectPtr;
            for (i = 0; i < barPtr->nRects; i++) {
                dataIndex = barPtr->rectToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *rectPtr++ = barPtr->rectangles[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->nRects = (Tcl_Size)(rectPtr - stylePtr->rectangles);
        }
        ckfree(barPtr->rectangles);
        barPtr->rectangles = rectangles;
        ckfree(barPtr->rectToData);
        barPtr->rectToData = rectToData;
    }
    if (barPtr->core.xErrorBarCnt > 0) {
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
        for (linkPtr = Rbc_ChainFirstLink(barPtr->core.palette); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->xErrorBars = segPtr;
            for (i = 0; i < barPtr->core.xErrorBarCnt; i++) {
                dataIndex = barPtr->core.xErrorToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *segPtr++ = barPtr->core.xErrorBars[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->xErrorBarCnt = (Tcl_Size)(segPtr - stylePtr->xErrorBars);
        }
        ckfree(barPtr->core.xErrorBars);
        barPtr->core.xErrorBars = errorBars;
        ckfree(barPtr->core.xErrorToData);
        barPtr->core.xErrorToData = errorToData;
    }
    if (barPtr->core.yErrorBarCnt > 0) {
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
        for (linkPtr = Rbc_ChainFirstLink(barPtr->core.palette); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            stylePtr = Rbc_ChainGetValue(linkPtr);
            stylePtr->yErrorBars = segPtr;
            for (i = 0; i < barPtr->core.yErrorBarCnt; i++) {
                dataIndex = barPtr->core.yErrorToData[i];
                if (dataToStyle[dataIndex] == (PenStyle *)stylePtr) {
                    *segPtr++ = barPtr->core.yErrorBars[i];
                    *indexPtr++ = dataIndex;
                }
            }
            stylePtr->yErrorBarCnt = (Tcl_Size)(segPtr - stylePtr->yErrorBars);
        }
        ckfree(barPtr->core.yErrorBars);
        barPtr->core.yErrorBars = errorBars;
        ckfree(barPtr->core.yErrorToData);
        barPtr->core.yErrorToData = errorToData;
    }
}

/**
 * @ingroup bar_geometry
 * @brief Rebuild the explicit active subset from mapped source indices.
 *
 * Frees previous active arrays first. For positive nActiveIndices, copies
 * matching rectangles in mapped order and retains original source indices;
 * repeated requests do not duplicate a rectangle. Unmapped requests yield
 * no rectangle. Clears ACTIVE_PENDING even for empty or size-invalid output.
 * Negative all-active mode is handled by drawing/export, not copied here.
 *
 * @param[in,out] barPtr Mapped bar with active selection; old active arrays are freed and ACTIVE_PENDING
 *    cleared.
 */
static void MapActiveBars(Bar *barPtr) {
    if (barPtr->activeRects != NULL) {
        ckfree(barPtr->activeRects);
        barPtr->activeRects = NULL;
    }
    if (barPtr->activeToData != NULL) {
        ckfree(barPtr->activeToData);
        barPtr->activeToData = NULL;
    }
    barPtr->nActive = 0;
    if (barPtr->core.nActiveIndices > 0) {
        BarRectangle *activeRects;
        Tcl_Size *activeToData;
        Tcl_Size activeIndex;
        Tcl_Size n;
        Tcl_Size i;
        Tcl_Size count;
        size_t activeRectsBytes;
        size_t activeToDataBytes;

        if ((GetBarArrayByteCount(barPtr->core.nActiveIndices, sizeof(*activeRects), &activeRectsBytes) != TCL_OK) ||
            (GetBarArrayByteCount(barPtr->core.nActiveIndices, sizeof(*activeToData), &activeToDataBytes) != TCL_OK)) {
            barPtr->core.flags &= ~ACTIVE_PENDING;
            return;
        }
        activeRects = ckalloc(activeRectsBytes);
        activeToData = ckalloc(activeToDataBytes);
        count = 0;
        for (i = 0; i < barPtr->nRects; i++) {
            for (n = 0; n < barPtr->core.nActiveIndices; n++) {
                activeIndex = barPtr->core.activeIndices[n];
                if (barPtr->rectToData[i] == activeIndex) {
                    activeRects[count] = barPtr->rectangles[i];
                    /*
                     * Store the source data index, not the
                     * rectangle-array index.
                     */
                    activeToData[count] = barPtr->rectToData[i];

                    count++;
                    break;
                }
            }
        }
        if (count > 0) {
            barPtr->nActive = count;
            barPtr->activeRects = activeRects;
            barPtr->activeToData = activeToData;
        } else {
            ckfree(activeRects);
            ckfree(activeToData);
        }
    }
    barPtr->core.flags &= ~ACTIVE_PENDING;
}

/**
 * @ingroup bar_geometry
 * @brief Discard owned mapped geometry and invalidate palette views.
 *
 * Clears palette counts, frees normal/active rectangles, error segments and
 * their source-index maps, then zeros the owning pointers and counts. Does
 * not clear borrowed style pointers, free source vectors, release pens or
 * discard the active-index selection. Used before remapping and at teardown.
 *
 * @param[in,out] barPtr Live bar owning mapped arrays; source data, pens and active-index selection are
 *    retained.
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
    barPtr->activeToData = NULL;
    barPtr->rectToData = NULL;
    barPtr->core.xErrorToData = NULL;
    barPtr->core.yErrorToData = NULL;
    barPtr->activeRects = barPtr->rectangles = NULL;
    barPtr->core.xErrorBars = barPtr->core.yErrorBars = NULL;
    barPtr->nActive = 0;
    barPtr->nRects = 0;
    barPtr->core.xErrorBarCnt = 0;
    barPtr->core.yErrorBarCnt = 0;
}

/**
 * @ingroup bar_layout
 * @brief Replace bar display geometry from source data and group state.
 * @param graphPtr Borrowed graph in the element's mapping viewport.
 * @param elemPtr Borrowed bar element whose old geometry is discarded.
 * @details Allocates owned rectangles and original-source indices, places bars
 * in order, maps corners and bounds coordinates before integer conversion.
 * Owns temporary error translations and style lookup storage; frees both after
 * mapping errors and grouping geometry by pen. Active rectangles are rebuilt
 * when explicit active indices exist. This consumes group state and must run
 * in the ordered mapping pass, after its reset. Renderer clipping is separate
 * from the window bounds used here to keep integer conversion safe.
 */
static void MapBar(Graph *graphPtr, Element *elemPtr) {
    Bar *barPtr = BAR_FROM_CORE(elemPtr);
    PenStyle **dataToStyle;
    Point2D c1, c2; /* Two opposite corners of the rectangle
                     * in graph coordinates. */
    Point2D *errorOffsets = NULL;
    double dx, dy;
    Tcl_Size *rectToData; /* Maps rectangles to data point indices */
    int height;
    int invertBar;
    Tcl_Size nPoints;
    Tcl_Size count;
    BarRectangle *rectPtr;
    BarRectangle *rectangles;
    Tcl_Size i;
    int size;
    Rbc_ChainLink *linkPtr;
    BarPenStyle *stylePtr;
    size_t rectanglesBytes;
    size_t rectToDataBytes;

    ResetBar(barPtr);
    nPoints = NumberOfPoints(elemPtr);
    if (nPoints < 1) {
        return; /* No data points */
    }
    if ((GetBarArrayByteCount(nPoints, sizeof(*rectangles), &rectanglesBytes) != TCL_OK) ||
        (GetBarArrayByteCount(nPoints, sizeof(*rectToData), &rectToDataBytes) != TCL_OK)) {
        return;
    }
    if (barPtr->errorBarPosition) {
        errorOffsets = NewBarErrorOffsets(nPoints);
    }
    /*
     * Create an array of rectangles representing the screen coordinates
     * of all the segments in the bar.
     */
    rectangles = ckalloc(rectanglesBytes);
    rectToData = ckalloc(rectToDataBytes);
    rectPtr = rectangles;
    count = 0;
    for (i = 0; i < nPoints; i++) {
        if (!PlaceBar(graphPtr, barPtr, i, TRUE, &c1, &c2)) {
            continue;
        }
        if (errorOffsets != NULL) {
            SetBarErrorOffset(barPtr, i, &c1, &c2, errorOffsets);
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
        /*
         * Mapping should normally produce finite screen coordinates.
         * Ignore a bar if arithmetic overflow or an invalid transform did
         * not.
         */
        if ((!FINITE(c1.x)) || (!FINITE(c1.y)) || (!FINITE(c2.x)) || (!FINITE(c2.y))) {
            continue;
        }
        /*
         * Bound horizontal coordinates by the graph window before converting
         * them to int, just as the vertical coordinates are bounded below.
         */
        if (c1.x < 0.0) {
            c1.x = 0.0;
        } else if (c1.x > (double)graphPtr->width) {
            c1.x = (double)graphPtr->width;
        }
        if (c2.x < 0.0) {
            c2.x = 0.0;
        } else if (c2.x > (double)graphPtr->width) {
            c2.x = (double)graphPtr->width;
        }
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
        height = Round(FABS(dy));
        if (invertBar) {
            rectPtr->y = (int)MIN(c1.y, c2.y);
        } else {
            rectPtr->y = (int)MAX(c1.y, c2.y) - height;
        }
        rectPtr->x = (int)MIN(c1.x, c2.x);
        rectPtr->width = Round(FABS(dx)) + 1;
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
        if (errorOffsets != NULL) {
            Rbc_MapErrorBarsTranslated(graphPtr, &barPtr->core, dataToStyle, 0.0, errorOffsets);
        } else if (!barPtr->errorBarPosition) {
            Rbc_MapErrorBarsOffset(graphPtr, &barPtr->core, dataToStyle, barPtr->barOffset);
        }
    }
    if (errorOffsets != NULL) {
        ckfree(errorOffsets);
    }
    MergePens(barPtr, dataToStyle);
    ckfree((char *)dataToStyle);
}

/**
 * @ingroup bar_output
 * @brief Draw a square legend sample using the normal pen.
 *
 * Borrows graph, drawable and element. Skips zero opacity or absent colors.
 * Preserves integer centering and size-minus-one geometry. Tries the renderer
 * legend adapter, then the screen fallback when unhandled. The sample is
 * independent of data rectangles, active/style pens and plot placement.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in] drawable Borrowed compatible legend destination.
 * @param[in] elemPtr Borrowed bar element with configured normal pen.
 * @param[in] x Legend-sample center X in destination pixels.
 * @param[in] y Legend-sample center Y in destination pixels.
 * @param[in] size Nominal square sample size; integer centering and size-minus-one geometry are
 *    preserved.
 * @param[in] width Destination drawable width passed to the legend renderer adapter.
 * @param[in] height Destination drawable height passed to the legend renderer adapter.
 */
static void DrawSymbol(Graph *graphPtr, Drawable drawable, Element *elemPtr, int x, int y, int size, int width,
                       int height) {
    BarPen *penPtr;
    int radius;

    penPtr = BAR_PEN_FROM_CORE(elemPtr->normalPenPtr);
    if ((penPtr->fillOpacity == 0.0) || ((penPtr->border == NULL) && (penPtr->fgColor == NULL))) {
        return;
    }
    radius = (size / 2);
    size--;
    x -= radius;
    y -= radius;
    {
        Rbc_RenderRectangle r = {x, y, size, size};
        const XColor *background = (penPtr->border != NULL) ? Tk_3DBorderColor(penPtr->border) : NULL;
        const XColor *foreground = (penPtr->fgColor != NULL) ? penPtr->fgColor : background;
        if (Rbc_RenderLegendBar(graphPtr, drawable, width, height, &r, foreground,
                                (penPtr->fgColor != NULL) ? background : NULL, penPtr->stipple, penPtr->fillOpacity))
            return;
    }
    Rbc_RenderScreenLegendBar(graphPtr, drawable, penPtr->gc, x, y, size);
}

/**
 * @ingroup bar_output
 * @brief Submit one bar rectangle batch with its pen style.
 *
 * Borrows rectangles and pen resources; opens and closes a screen bar
 * context around the complete batch. The outer element drawing scope supplies
 * track/plot clipping, including borders. Does not map or copy geometry.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in] drawable Borrowed compatible screen destination; outer caller supplies plot/track clipping.
 * @param[in] penPtr Borrowed configured concrete bar pen; drawing resources must remain valid throughout
 *    the call.
 * @param[in] rectangles Borrowed mapped rectangle array with nRects entries.
 * @param[in] nRects Nonnegative rectangle count; not a source-sample or segment count.
 */
static void DrawBarSegments(Graph *graphPtr, Drawable drawable, BarPen *penPtr, BarRectangle *rectangles,
                            Tcl_Size nRects) {
    Rbc_RenderBarStyle style = {penPtr->border, penPtr->fgColor, penPtr->gc, penPtr->stipple,
        penPtr->borderWidth, penPtr->relief, penPtr->fillOpacity};
    Rbc_RenderContext *ctx = Rbc_RenderBeginBars(graphPtr, drawable, &style);
    Rbc_RenderFillRectangles(ctx, rectangles, nRects);
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup bar_output
 * @brief Draw source-value labels anchored to mapped rectangles.
 *
 * The borrowed rectangle and source-index arrays must correspond. Obtains
 * one owned label reference per rectangle and releases it after use; empty
 * text is skipped. Inversion and the source value relative to the effective
 * baseline choose the anchor edge. Adds the pen offset, checks int bounds
 * and truncates screen coordinates. Label commands may execute Tcl: callers
 * must keep graph, pen, element and geometry live and unchanged throughout
 * drawing; this loop adds no preservation against callback mutation.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in] drawable Borrowed compatible screen destination; outer caller supplies plot/track clipping.
 * @param[in] barPtr Borrowed bar with source arrays stable across label callbacks.
 * @param[in] penPtr Borrowed configured concrete bar pen; drawing resources must remain valid throughout
 *    the call.
 * @param[in] rectangles Borrowed mapped rectangle batch, stable throughout label evaluation.
 * @param[in] nRects Nonnegative number of rectangles and matching source-index entries.
 * @param[in] rectToData Borrowed parallel map to valid original X/Y indices; not palette-relative
 *    indices.
 */
static void DrawBarValues(Graph *graphPtr, Drawable drawable, Bar *barPtr, BarPen *penPtr, BarRectangle *rectangles,
                          Tcl_Size nRects, const Tcl_Size *rectToData) {
    BarRectangle *rectPtr;
    BarRectangle *endPtr;
    Tcl_Size count;
    Tcl_Obj *labelObjPtr;
    double x;
    double y;
    Point2D anchorPos;

    Rbc_RenderContext *text = Rbc_RenderBeginScreenText(graphPtr->tkwin, drawable);
    count = 0;
    for (rectPtr = rectangles, endPtr = rectangles + nRects; rectPtr < endPtr; rectPtr++) {
        Tcl_Size dataIndex;
        dataIndex = rectToData[count++];
        x = barPtr->core.x.valueArr[dataIndex];
        y = barPtr->core.y.valueArr[dataIndex];
        labelObjPtr = Rbc_GetElementValueLabel(&barPtr->core, penPtr->valueCommandObjPtr,
                                               penPtr->valueFormat, penPtr->valueShow, dataIndex, x, y);
        if (Tcl_GetCharLength(labelObjPtr) == 0) {
            Tcl_DecrRefCount(labelObjPtr);
            continue;
        }
        if (graphPtr->inverted) {
            anchorPos.y = rectPtr->y + rectPtr->height * 0.5;
            anchorPos.x = rectPtr->x + rectPtr->width;
            if (y < GetBarBaseline(&barPtr->core)) {
                anchorPos.x -= rectPtr->width;
            }
        } else {
            anchorPos.x = rectPtr->x + rectPtr->width * 0.5;
            anchorPos.y = rectPtr->y;
            if (y < GetBarBaseline(&barPtr->core)) {
                anchorPos.y += rectPtr->height;
            }
        }
        {
            double labelX = anchorPos.x + penPtr->valueOffset.x;
            double labelY = anchorPos.y + penPtr->valueOffset.y;
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
 * @ingroup bar_output
 * @brief Draw one error-stroke batch with screen color and width defaults.
 *
 * Borrows pen and segments. NULL/default color falls back to foreground,
 * then border color, then screen black. Supplies at least one pixel to the stroke
 * renderer and the configured GC for native drawing. Closes any returned
 * context; an unavailable context produces no strokes. Does not map errors.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in] drawable Borrowed compatible screen destination; outer caller supplies plot/track clipping.
 * @param[in] penPtr Borrowed configured concrete bar pen; drawing resources must remain valid throughout
 *    the call.
 * @param[in] segments Borrowed mapped independent error strokes, including any cap segments.
 * @param[in] count Nonnegative number of segment records, not the number of source errors.
 */
static void DrawBarErrorSegments(Graph *graphPtr, Drawable drawable, BarPen *penPtr, const Segment2D *segments,
                                 Tcl_Size count) {
    Rbc_RenderContext *ctx;

    XColor fallbackColor;
    const XColor *color = GetBarColor(graphPtr, penPtr, penPtr->errorBarColor, &fallbackColor);
    ctx = Rbc_RenderBeginStroke(graphPtr, drawable, penPtr->errorBarGC, RBC_RENDER_STROKE_SEGMENTS,
        color, MAX(1, penPtr->errorBarLineWidth), NULL, NULL);
    if (ctx != NULL) {
        Rbc_RenderSegments(ctx, segments, count);
        Rbc_RenderEnd(ctx);
    }
}

/**
 * @ingroup bar_output
 * @brief Draw palette-grouped bars, errors and labels in that order.
 *
 * Requires current geometry and style slices. Each style draws rectangles,
 * enabled X errors, enabled Y errors, then labels. Advances the parallel
 * rectToData slice by each style's rectangle count. The caller supplies the
 * clipping/lifetime scope; label commands can execute Tcl and must not mutate
 * the borrowed drawing records.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in] drawable Borrowed compatible screen destination; outer caller supplies plot/track clipping.
 * @param[in,out] elemPtr Mapped bar with current palette slices and parallel source maps; callbacks must
 *    not mutate them.
 */
static void DrawNormalBar(Graph *graphPtr, Drawable drawable, Element *elemPtr) {
    Bar *barPtr = BAR_FROM_CORE(elemPtr);
    Tcl_Size count;
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
            DrawBarErrorSegments(graphPtr, drawable, penPtr, stylePtr->xErrorBars, stylePtr->xErrorBarCnt);
        }
        if ((stylePtr->yErrorBarCnt > 0) && (penPtr->errorBarShow & SHOW_Y)) {
            DrawBarErrorSegments(graphPtr, drawable, penPtr, stylePtr->yErrorBars, stylePtr->yErrorBarCnt);
        }
        if (penPtr->valueShow != SHOW_NONE) {
            DrawBarValues(graphPtr, drawable, barPtr, penPtr, stylePtr->rectangles, stylePtr->nRects,
                          barPtr->rectToData + count);
        }
        count += stylePtr->nRects;
    }
}

/**
 * @ingroup bar_output
 * @brief Draw active rectangles and labels with the active pen.
 *
 * A missing active pen or zero selection draws nothing. Positive selection
 * rebuilds ACTIVE_PENDING geometry on demand; a negative selection draws all
 * mapped rectangles. Does not overlay active error bars. Borrows records and
 * inherits DrawBarValues callback restrictions.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in] drawable Borrowed compatible screen destination; outer caller supplies plot/track clipping.
 * @param[in,out] elemPtr Mapped bar with stable drawing dependencies; pending active geometry may be
 *    rebuilt.
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

/**
 * @ingroup bar_output
 * @brief Export a normal-pen legend sample through the selected backend.
 *
 * Skips zero opacity or absent colors. With stipple and no foreground,
 * promotes background to stipple foreground and leaves gaps transparent.
 * Borrows style resources and closes the symbol context after one point;
 * does not emit data rectangles or relief borders.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in,out] exportPtr Borrowed initialized export context; outer caller owns clipping and document
 *    finalization.
 * @param[in] elemPtr Borrowed bar element with configured normal pen.
 * @param[in] x Mapped sample center X in export pixel coordinates.
 * @param[in] y Mapped sample center Y in export pixel coordinates.
 * @param[in] size Nominal square legend sample size in pixels.
 */
static void SymbolExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Element *elemPtr, double x, double y, int size) {
    BarPen *bpPtr = BAR_PEN_FROM_CORE(elemPtr->normalPenPtr);
    Rbc_RenderFillStyle style;
    Rbc_RenderContext *ctx;
    Point2D center = {x, y};

    if ((bpPtr->fillOpacity == 0.0) || ((bpPtr->border == NULL) && (bpPtr->fgColor == NULL))) {
        return;
    }
    style.foreground = bpPtr->fgColor;
    style.background = (bpPtr->border != NULL) ? Tk_3DBorderColor(bpPtr->border) : NULL;
    style.stipple = bpPtr->stipple;
    /* Match the screen GC: without -foreground, the background color
     * paints the stipple bits and the gaps remain transparent. */
    if ((style.stipple != None) && (style.foreground == NULL)) {
        style.foreground = style.background;
        style.background = NULL;
    }
    style.opacity = bpPtr->fillOpacity;
    style.backgroundOnly = FALSE;
    ctx = Rbc_RenderBeginExportBarSymbol(graphPtr, exportPtr, &style, size);
    Rbc_RenderSymbolPoints(ctx, &center, 1);
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup bar_output
 * @brief Export filled rectangles and optional relief borders for one pen.
 *
 * Borrows the mapped rectangle batch. Normalizes stipple colors, opens one
 * fill context and skips rectangles with dimensions below one. Preserves the
 * historical one-pixel fill inset. Positive opacity enables fills; nonflat
 * borders use the original dimensions and can remain visible at zero fill
 * opacity. No colors means no output. Closes its context before returning;
 * plot/track clipping belongs to the outer export scope.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in,out] exportPtr Borrowed initialized export context; outer caller owns clipping and document
 *    finalization.
 * @param[in] penPtr Borrowed configured concrete bar pen; drawing resources must remain valid throughout
 *    the call.
 * @param[in] rectPtr Borrowed mapped rectangle batch; array extent must permit nRects-entry traversal.
 * @param[in] nRects Nonnegative rectangle count; each rectangle with a dimension below one is skipped.
 */
static void SegmentsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, BarPen *penPtr, BarRectangle *rectPtr,
                                 Tcl_Size nRects) {
    BarRectangle *endPtr;
    Rbc_RenderFillStyle style;
    Rbc_RenderContext *ctx;

    if ((penPtr->border == NULL) && (penPtr->fgColor == NULL)) {
        return;
    }
    style.foreground = penPtr->fgColor;
    style.background = (penPtr->border != NULL) ? Tk_3DBorderColor(penPtr->border) : NULL;
    style.stipple = penPtr->stipple;
    /* Match the screen GC: without -foreground, the background color
     * paints the stipple bits and the gaps remain transparent. */
    if ((style.stipple != None) && (style.foreground == NULL)) {
        style.foreground = style.background;
        style.background = NULL;
    }
    style.opacity = penPtr->fillOpacity;
    style.backgroundOnly = FALSE;
    ctx = Rbc_RenderBeginExportFill(graphPtr, exportPtr, &style);
    for (endPtr = rectPtr + nRects; rectPtr < endPtr; rectPtr++) {
        if ((rectPtr->width < 1) || (rectPtr->height < 1)) {
            continue;
        }
        /* Preserve the historical PostScript one-pixel inset. */
        Rbc_RenderRectangle fill = {rectPtr->x, rectPtr->y, rectPtr->width - 1, rectPtr->height - 1};

        if (penPtr->fillOpacity > 0.0) {
            Rbc_RenderFillRectangles(ctx, &fill, 1);
        }
        if ((penPtr->border != NULL) && (penPtr->borderWidth > 0) && (penPtr->relief != TK_RELIEF_FLAT)) {
            Rbc_RenderBorder(ctx, penPtr->border, (double)rectPtr->x, (double)rectPtr->y,
                                            rectPtr->width, rectPtr->height, penPtr->borderWidth, penPtr->relief, FALSE);
        }
    }
    Rbc_RenderEnd(ctx);
}

/**
 * @ingroup bar_output
 * @brief Export source-value labels at mapped bar anchors.
 *
 * Uses the same source-index lookup and baseline/inversion anchor rule as
 * DrawBarValues, retaining floating-point output coordinates. Borrows drawing
 * records, releases each acquired label object and closes the output context.
 * Label commands may execute Tcl and must not mutate owners or geometry or
 * enter nested event loops. Empty labels are skipped.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in,out] exportPtr Borrowed initialized export context; outer caller owns clipping and document
 *    finalization.
 * @param[in] barPtr Borrowed bar with source arrays stable across label callbacks.
 * @param[in] penPtr Borrowed configured concrete bar pen; drawing resources must remain valid throughout
 *    the call.
 * @param[in] rectangles Borrowed mapped rectangle batch, stable throughout label evaluation.
 * @param[in] nRects Nonnegative number of rectangles and matching source-index entries.
 * @param[in] rectToData Borrowed parallel map to valid original X/Y indices; not palette-relative
 *    indices.
 */
static void BarValuesExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Bar *barPtr, BarPen *penPtr,
                                  BarRectangle *rectangles, Tcl_Size nRects, const Tcl_Size *rectToData) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    BarRectangle *rectPtr, *endPtr;
    Tcl_Size count;
    Tcl_Obj *labelObjPtr;
    double x, y;
    Point2D anchorPos;

    count = 0;
    for (rectPtr = rectangles, endPtr = rectangles + nRects; rectPtr < endPtr; rectPtr++) {
        Tcl_Size dataIndex;
        dataIndex = rectToData[count++];
        x = barPtr->core.x.valueArr[dataIndex];
        y = barPtr->core.y.valueArr[dataIndex];
        labelObjPtr = Rbc_GetElementValueLabel(&barPtr->core, penPtr->valueCommandObjPtr,
                                               penPtr->valueFormat, penPtr->valueShow, dataIndex, x, y);
        if (Tcl_GetCharLength(labelObjPtr) == 0) {
            Tcl_DecrRefCount(labelObjPtr);
            continue;
        }
        if (graphPtr->inverted) {
            anchorPos.y = rectPtr->y + rectPtr->height * 0.5;
            anchorPos.x = rectPtr->x + rectPtr->width;
            if (y < GetBarBaseline(&barPtr->core)) {
                anchorPos.x -= rectPtr->width;
            }
        } else {
            anchorPos.x = rectPtr->x + rectPtr->width * 0.5;
            anchorPos.y = rectPtr->y;
            if (y < GetBarBaseline(&barPtr->core)) {
                anchorPos.y += rectPtr->height;
            }
        }
        Rbc_RenderText(output, Tcl_GetString(labelObjPtr), &penPtr->valueStyle,
                             anchorPos.x + penPtr->valueOffset.x, anchorPos.y + penPtr->valueOffset.y);
        Tcl_DecrRefCount(labelObjPtr);
    }
    Rbc_RenderEnd(output);
}

/**
 * @ingroup bar_output
 * @brief Export selected or all active rectangles and their labels.
 *
 * Uses the active pen if present. Positive selection rebuilds pending
 * active geometry; negative selection uses the full mapped arrays. Zero
 * selection emits nothing. Does not export active errors. Requires the outer
 * export clip and the label-callback lifetime contract of BarValuesExport.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in,out] exportPtr Borrowed initialized export context; outer caller owns clipping and document
 *    finalization.
 * @param[in,out] elemPtr Mapped bar with stable drawing dependencies; pending active geometry may be
 *    rebuilt.
 */
static void ActiveBarExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Element *elemPtr) {
    Bar *barPtr;

    barPtr = BAR_FROM_CORE(elemPtr);
    if (elemPtr->activePenPtr != NULL) {
        BarPen *penPtr;
        penPtr = BAR_PEN_FROM_CORE(elemPtr->activePenPtr);
        if (elemPtr->nActiveIndices > 0) {
            if (elemPtr->flags & ACTIVE_PENDING) {
                MapActiveBars(barPtr);
            }
            SegmentsExport(graphPtr, exportPtr, penPtr, barPtr->activeRects, barPtr->nActive);
            if (penPtr->valueShow != SHOW_NONE) {
                BarValuesExport(graphPtr, exportPtr, barPtr, penPtr, barPtr->activeRects, barPtr->nActive,
                                      barPtr->activeToData);
            }
        } else if (elemPtr->nActiveIndices < 0) {
            SegmentsExport(graphPtr, exportPtr, penPtr, barPtr->rectangles, barPtr->nRects);
            if (penPtr->valueShow != SHOW_NONE) {
                BarValuesExport(graphPtr, exportPtr, barPtr, penPtr, barPtr->rectangles, barPtr->nRects,
                                      barPtr->rectToData);
            }
        }
    }
}

/**
 * @ingroup bar_output
 * @brief Export palette-grouped bars, error strokes and value labels.
 *
 * Borrows current style slices and corresponding source-index ranges.
 * Uses the shared bar color fallback for errors and passes the configured
 * width to export stroke contexts. Labels may execute Tcl; the outer
 * caller must maintain the drawing records and export clip throughout.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in,out] exportPtr Borrowed initialized export context; outer caller owns clipping and document
 *    finalization.
 * @param[in,out] elemPtr Mapped bar with current palette slices and parallel source maps; callbacks must
 *    not mutate them.
 */
static void NormalBarExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Element *elemPtr) {
    Bar *barPtr = BAR_FROM_CORE(elemPtr);
    Rbc_ChainLink *linkPtr;
    register BarPenStyle *stylePtr;
    Tcl_Size count;
    BarPen *penPtr;
    const XColor *colorPtr;
    XColor fallbackColor;

    count = 0;
    for (linkPtr = Rbc_ChainFirstLink(barPtr->core.palette); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        stylePtr = Rbc_ChainGetValue(linkPtr);
        penPtr = stylePtr->penPtr;
        if (stylePtr->nRects > 0) {
            SegmentsExport(graphPtr, exportPtr, penPtr, stylePtr->rectangles, stylePtr->nRects);
        }
        colorPtr = GetBarColor(graphPtr, penPtr, penPtr->errorBarColor, &fallbackColor);
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
        if (penPtr->valueShow != SHOW_NONE) {
            BarValuesExport(graphPtr, exportPtr, barPtr, penPtr, stylePtr->rectangles, stylePtr->nRects,
                                  barPtr->rectToData + count);
        }
        count += stylePtr->nRects;
    }
}

/**
 * @ingroup bar_geometry
 * @brief Release concrete bar dependencies without freeing the element record.
 *
 * Drops axis and named-pen references, destroys the embedded pen's derived
 * resources, detaches all source slots and frees mapped geometry, active
 * selection, palette and tags. Source-vector creators retain ownership of
 * their vectors. Shared element teardown handles Tk-managed options, names
 * and the concrete record allocation; this callback must not free them.
 *
 * @param[in] graphPtr Borrowed live owning graph providing Tk, display and renderer context.
 * @param[in,out] elemPtr Common bar header; dependencies and mapped storage are released, concrete
 *    allocation remains.
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
    ActiveBarExport,
    NormalBarExport,
    SymbolExport,
    MapBar,
    NULL /* Uses ordinary X/Y point count. */
};

/**
 * @ingroup bar_configuration
 * @brief Allocate an unconfigured bar element for shared element creation.
 *
 * Returns the common Element header of owned zeroed storage, or NULL on
 * allocation failure. Copies name and initial label, borrows graph/class
 * identity, installs bar methods, initializes the embedded normal pen and
 * creates an empty palette. The caller must apply options, configure and
 * register the element, or release it through shared element teardown.
 * Ordinary paired X/Y counting is used; no complex point-count hook is set.
 *
 * @param[in] graphPtr Borrowed owning graph retained by the unconfigured element.
 * @param[in] name Borrowed NUL-terminated name copied into both initial name and label.
 * @param[in] classUid Borrowed stable element-class identity installed in the common header.
 */
Element *Rbc_BarElement(Graph *graphPtr, const char *name, Rbc_Uid classUid) {
    Bar *barPtr;
    Element *elemPtr;

    barPtr = Rbc_Calloc(1, sizeof(Bar));
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
    elemPtr->label = Rbc_Strdup(name);
    elemPtr->name = Rbc_Strdup(name);
    elemPtr->graphPtr = graphPtr;
    elemPtr->hidden = FALSE;
    elemPtr->plotHidden = FALSE;
    /*
     * The embedded pen's direct option fields are initialized through
     * barElemOptionSpecs, while its derived resources use the normal
     * modern bar-pen configuration.
     */
    InitPen(&barPtr->builtinPen, normalBarPenOptionSpecs, NORMAL_PEN);
    elemPtr->palette = Rbc_ChainCreate();
    return elemPtr;
}

/**
 * @ingroup bar_layout
 * @brief Rebuild duplicate-X grouping metadata for displayed bar elements.
 *
 * Releases previous graph-owned metadata first. Ordinary normal mode needs
 * no table; trackgraphs still need one for local overrides. Counts finite
 * paired samples with valid log-Y values, using source X and both axis
 * identities; normalizes signed zero. Only duplicate keys receive persistent
 * records, owned by freqArr and borrowed by hash entries. nStacks indicates
 * persistent table initialization. Does not apply width/offset/alignment or
 * compute stack sums. The graph must retain axes throughout metadata use.
 *
 * @param[in,out] graphPtr Live graph with displayed bars and axes; previous grouping metadata is
 *    replaced.
 */
void Rbc_InitFreqTable(Graph *graphPtr) {
    register Element *elemPtr;
    Rbc_ChainLink *linkPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Bar *barPtr;
    int isNew;
    Tcl_Size nStacks;
    Tcl_Size nSegs;
    Tcl_Size nPoints;
    Tcl_Size i;
    Tcl_Size count;
    FreqKey key;
    Tcl_HashTable freqTable;
    const double *xArr;
    const double *yArr;
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
    if (!graphPtr->trackGraph && graphPtr->mode == MODE_INFRONT) {
        return; /* No frequency table is needed for
                 * "infront" mode */
    }
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
        if ((elemPtr->hidden) || (elemPtr->plotHidden) || (elemPtr->classUid != rbcBarElementUid)) {
            continue;
        }
        nSegs++;
        barPtr = BAR_FROM_CORE(elemPtr);
        xArr = barPtr->core.x.valueArr;
        yArr = barPtr->core.y.valueArr;
        nPoints = NumberOfPoints(elemPtr);
        for (i = 0; i < nPoints; i++) {
            if ((!FINITE(xArr[i])) || (!FINITE(yArr[i]))) {
                continue;
            }
            if (barPtr->core.axes.y->logScale && (yArr[i] <= 0.0)) {
                continue;
            }
            /*
             * FreqKey is a binary hash key.  Normalize signed zero so that
             * -0.0 and +0.0 identify the same numeric abscissa.
             */
            key.value = (xArr[i] == 0.0) ? 0.0 : xArr[i];
            key.axes = barPtr->core.axes;
            hPtr = Tcl_CreateHashEntry(&freqTable, (char *)&key, &isNew);
            if (isNew) {
                count = 1;
            } else {
                count = (Tcl_Size)(uintptr_t)Tcl_GetHashValue(hPtr);
                count++;
                if (count == 2) {
                    nStacks++;
                }
            }
            Tcl_SetHashValue(hPtr, (ClientData)(uintptr_t)count);
        }
    }
    if (nSegs == 0) {
        Tcl_DeleteHashTable(&freqTable);
        return;
    }
    if (nStacks > 0) {
        FreqInfo *infoPtr;
        FreqKey *keyPtr;
        Tcl_HashEntry *h2Ptr;
        size_t freqBytes;

        if (GetBarArrayByteCount(nStacks, sizeof(*graphPtr->freqArr), &freqBytes) != TCL_OK) {
            Tcl_DeleteHashTable(&freqTable);
            return;
        }
        /*
         * The persistent table exists only when there really are
         * duplicate abscissas.  graphPtr->nStacks therefore remains a
         * valid indication that this table has been initialized.
         */
        Tcl_InitHashTable(&(graphPtr->freqTable), sizeof(FreqKey) / sizeof(int));
        /*
         * FreqInfo.count, lastY and sum rely on initial zero values, so
         * preserve calloc semantics here.
         */
        graphPtr->freqArr = Rbc_Calloc(1, freqBytes);
        assert(graphPtr->freqArr);
        infoPtr = graphPtr->freqArr;
        for (hPtr = Tcl_FirstHashEntry(&freqTable, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
            count = (Tcl_Size)(uintptr_t)Tcl_GetHashValue(hPtr);
            keyPtr = (FreqKey *)Tcl_GetHashKey(&freqTable, hPtr);
            if (count > 1) {
                h2Ptr = Tcl_CreateHashEntry(&(graphPtr->freqTable), (char *)keyPtr, &isNew);
                count = (Tcl_Size)(uintptr_t)Tcl_GetHashValue(hPtr);
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

/**
 * @ingroup bar_layout
 * @brief Accumulate signed source-Y totals for duplicate grouping keys.
 *
 * Requires current frequency metadata. Resets sums, then visits displayed
 * bars and valid pairs in display-list order. Ordinary nonstacked graphs
 * return early; trackgraphs compute all duplicate totals and CheckStacks
 * chooses where to use them. Sums are ordinary double additions, without
 * PlaceBar's overflow rejection or separate positive/negative accumulation.
 * Does not alter placement counters or source data.
 *
 * @param[in,out] graphPtr Graph with current duplicate metadata and source arrays; sums are replaced,
 *    placement cursors retained.
 */
void Rbc_ComputeStacks(Graph *graphPtr) {
    Element *elemPtr;
    Bar *barPtr;
    FreqKey key;
    Rbc_ChainLink *linkPtr;
    Tcl_HashEntry *hPtr;
    Tcl_Size nPoints;
    Tcl_Size i;
    register FreqInfo *infoPtr;
    const double *xArr;
    const double *yArr;

    if ((!graphPtr->trackGraph && graphPtr->mode != MODE_STACKED) || (graphPtr->nStacks == 0)) {
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
        if ((elemPtr->hidden) || (elemPtr->plotHidden) || (elemPtr->classUid != rbcBarElementUid)) {
            continue;
        }
        barPtr = BAR_FROM_CORE(elemPtr);
        xArr = barPtr->core.x.valueArr;
        yArr = barPtr->core.y.valueArr;
        nPoints = NumberOfPoints(elemPtr);
        for (i = 0; i < nPoints; i++) {
            if ((!FINITE(xArr[i])) || (!FINITE(yArr[i]))) {
                continue;
            }
            if (barPtr->core.axes.y->logScale && (yArr[i] <= 0.0)) {
                continue;
            }
            key.value = (xArr[i] == 0.0) ? 0.0 : xArr[i];
            key.axes = barPtr->core.axes;
            hPtr = Tcl_FindHashEntry(&(graphPtr->freqTable), (char *)&key);
            if (hPtr != NULL) {
                infoPtr = Tcl_GetHashValue(hPtr);
                infoPtr->sum += yArr[i];
            }
        }
    }
}

/**
 * @ingroup bar_layout
 * @brief Reset mutable group placement cursors before an ordered pass.
 *
 * Sets every duplicate record's lastY and count to zero, retaining sums,
 * frequencies and axis identities. A missing/empty array is a no-op. Call
 * before an extents or mapping traversal, not between members of a group.
 *
 * @param[in,out] graphPtr Graph with initialized duplicate metadata, or an empty array; only placement
 *    cursors are reset.
 */
void Rbc_ResetStacks(Graph *graphPtr) {
    FreqInfo *infoPtr;
    Tcl_Size i;

    if ((graphPtr->nStacks <= 0) || (graphPtr->freqArr == NULL)) {
        return;
    }
    infoPtr = graphPtr->freqArr;
    for (i = 0; i < graphPtr->nStacks; i++, infoPtr++) {
        infoPtr->lastY = 0.0;
        infoPtr->count = 0;
    }
}

/**
 * @ingroup bar_configuration
 * @brief Refresh label text styles after a graph font change.
 *
 * Visits bar pens in the named-pen table and embedded pens in the element
 * table, including elements outside the display list. Resets each text style
 * with a non-NULL font. Borrows graph-owned records; does not rebuild bar
 * geometry, select active pens or itself schedule a redraw.
 *
 * @param[in,out] graphPtr Live graph with valid pen/element tables and Tk window; label styles are
 *    refreshed.
 */
void Rbc_BarFontsChanged(Graph *graphPtr) {
    Tcl_HashSearch iter;
    Tcl_HashEntry *entryPtr;

    for (entryPtr = Tcl_FirstHashEntry(&graphPtr->penTable, &iter); entryPtr != NULL;
         entryPtr = Tcl_NextHashEntry(&iter)) {
        Pen *penPtr = Tcl_GetHashValue(entryPtr);

        if (penPtr->classUid == rbcBarElementUid) {
            BarPen *typedPtr = BAR_PEN_FROM_CORE(penPtr);

            if (typedPtr->valueStyle.font != NULL) {
                Rbc_ResetTextStyle(graphPtr->tkwin, &typedPtr->valueStyle);
            }
        }
    }
    for (entryPtr = Tcl_FirstHashEntry(&graphPtr->elements.table, &iter); entryPtr != NULL;
         entryPtr = Tcl_NextHashEntry(&iter)) {
        Element *elemPtr = Tcl_GetHashValue(entryPtr);

        if (elemPtr->classUid == rbcBarElementUid) {
            Bar *typedPtr = BAR_FROM_CORE(elemPtr);

            if (typedPtr->builtinPen.valueStyle.font != NULL) {
                Rbc_ResetTextStyle(graphPtr->tkwin, &typedPtr->builtinPen.valueStyle);
            }
        }
    }
}
