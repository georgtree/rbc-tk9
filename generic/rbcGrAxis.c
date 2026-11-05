/*
 * rbcGrAxis.c --
 *
 *      This module implements coordinate axes for the rbc graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @defgroup axis_transactions Axis option transactions */
/** @defgroup axis_ranges Axis ranges and tick sweeps */
/** @defgroup axis_presentation Axis labels and presentation */
/** @defgroup axis_layout_control Axis layout and view control */
/** @defgroup axis_registry Axis registry and lifetime */
/** @defgroup axis_configuration Axis configuration entry points */
/** @defgroup axis_commands Axis command dispatch and bindings */

#include "rbcGraph.h"
#include "rbcGrTrack.h"
#include "rbcRender.h"
#include "rbcGrElem.h"
#include <X11/Xutil.h>
#include <stdint.h>
#include <tcl.h>

#define DEF_NUM_TICKS 4 /* Each minor tick is 20% */
#define STATIC_TICK_SPACE 10
#define TICK_LABEL_SIZE 200
#define MAXTICKS 10001
#define CLAMP(val, low, high) (((val) < (low)) ? (low) : ((val) > (high)) ? (high) : (val))

/*
 * Round x in terms of units
 */
#define UCEIL(x, u) (ceil((x) / (u)) * (u))
#define UFLOOR(x, u) (floor((x) / (u)) * (u))

#define LENGTH_MAJOR_TICK 0.030 /* Length of a major tick */
#define LENGTH_MINOR_TICK 0.015 /* Length of a minor (sub)tick */
#define LENGTH_LABEL_TICK                                                                                              \
    0.040 /* Distance from graph to start of the                                                                       \
           * label */
#define NUMDIGITS                                                                                                      \
    15                        /* Specifies the number of                                                               \
                               * digits of accuracy used when                                                          \
                               * outputting axis tick labels. */
#define AVG_TICK_NUM_CHARS 16 /* Assumed average tick label size */

#define TICK_RANGE_TIGHT 0
#define TICK_RANGE_LOOSE 1
#define TICK_RANGE_ALWAYS_LOOSE 2

#define AXIS_TITLE_PAD 2 /* Padding for axis title. */
#define AXIS_LINE_PAD 1  /* Padding for axis line. */

#define HORIZMARGIN(m) (!((m)->site & 0x1)) /* Even sites are horizontal */

typedef enum AxisComponents { MAJOR_TICK, MINOR_TICK, TICK_LABEL, AXIS_LINE } AxisComponent;

typedef struct {
    int axis;  /* Length of the axis.  */
    int t1;    /* Length of a major tick (in pixels). */
    int t2;    /* Length of a minor tick (in pixels). */
    int label; /* Distance from axis to tick label.  */
} AxisInfo;

/* Axis flags: */

#define DEF_AXIS_COMMAND (char *)NULL
#define DEF_AXIS_DESCENDING "no"
#define DEF_AXIS_FOREGROUND RGB_BLACK
#define DEF_AXIS_HIDE "no"
#define DEF_AXIS_JUSTIFY "center"
#define DEF_AXIS_LINE_WIDTH "1"
#define DEF_AXIS_LOGSCALE "no"
#define DEF_AXIS_LOOSE "no"
#define DEF_AXIS_RANGE "0.0"
#define DEF_AXIS_ROTATE "0.0"
#define DEF_AXIS_SCROLL_INCREMENT "10"
#define DEF_AXIS_SHIFTBY "0.0"
#define DEF_AXIS_SHOWTICKS "yes"
#define DEF_AXIS_STEP "0.0"
#define DEF_AXIS_SUBDIVISIONS "2"
#define DEF_AXIS_TAGS "all"
#ifdef WIN32
#define DEF_AXIS_TICK_FONT "{Arial Narrow} 8"
#else
#define DEF_AXIS_TICK_FONT "TkSmallCaptionFont"
#endif
#define DEF_AXIS_TICK_LENGTH "8"
#define DEF_AXIS_TITLE_ALTERNATE "0"
#define DEF_AXIS_TITLE_FONT STD_FONT
#define DEF_AXIS_BACKGROUND (char *)NULL
#define DEF_AXIS_BORDERWIDTH "0"
#define DEF_AXIS_RELIEF "flat"

/*
 * Axis option conversion masks.
 *
 * These typeMask bits describe post-Tk_SetOptions work performed by
 * the modern axis configuration path.
 */
#define AXIS_TAGS_MASK (1u << 0)
#define AXIS_LIMITS_FORMAT_MASK (1u << 1)
#define AXIS_LIMITS_SHADOW_MASK (1u << 2)
#define AXIS_LOOSE_MASK (1u << 3)
#define AXIS_MAJOR_TICKS_MASK (1u << 4)
#define AXIS_MINOR_TICKS_MASK (1u << 5)
#define AXIS_LIMITS_MASK (1u << 6)
#define AXIS_SCROLL_LIMITS_MASK (1u << 7)
#define AXIS_TICK_SHADOW_MASK (1u << 8)
#define AXIS_TITLE_SHADOW_MASK (1u << 9)
#define AXIS_PIXELS_MASK (1u << 10)
#define AXIS_TEXT_STYLE_MASK (1u << 11)
#define AXIS_LAYOUT_MASK (1u << 12)
#define AXIS_MAP_MASK (1u << 13)
#define AXIS_REDRAW_MASK (1u << 14)
#define AXIS_LOG_SCALE_MASK (1u << 15)
#define AXIS_FORMAT_COMMAND_MASK (1u << 16)
#define AXIS_SCROLL_COMMAND_MASK (1u << 17)
/*
 * Options that affect the axis's requested numerical range.
 */
#define AXIS_RANGE_MASK (AXIS_LIMITS_MASK | AXIS_SCROLL_LIMITS_MASK | AXIS_LOOSE_MASK)
/*
 * Options that affect tick generation.
 */
#define AXIS_TICKS_MASK (AXIS_MAJOR_TICKS_MASK | AXIS_MINOR_TICKS_MASK)
/*
 * Options that require recalculating axis geometry.
 */
#define AXIS_GEOMETRY_MASK                                                                                             \
    (AXIS_RANGE_MASK | AXIS_TICKS_MASK | AXIS_PIXELS_MASK | AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK)
/*
 * Options that require remapping graph contents against this axis.
 */
#define AXIS_RESET_MASK (AXIS_GEOMETRY_MASK | AXIS_MAP_MASK)
#define AXIS_SHADOW_MASK (AXIS_LIMITS_SHADOW_MASK | AXIS_TICK_SHADOW_MASK | AXIS_TITLE_SHADOW_MASK)

typedef enum {
    AXIS_LIMIT_OPTION_NONE,
    AXIS_LIMIT_OPTION_MIN,
    AXIS_LIMIT_OPTION_MAX,
    AXIS_LIMIT_OPTION_SCROLL_MIN,
    AXIS_LIMIT_OPTION_SCROLL_MAX
} AxisLimitOption;

#define AXIS_LIMIT_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

/**
 * @ingroup axis_transactions
 * @brief Scalar requested/scroll candidates, initialized from live values; stagedMask selects fields to commit.
 */
typedef struct {
    unsigned int stagedMask;

    double reqMin;
    double reqMax;
    double scrollMin;
    double scrollMax;
} AxisLimitTransaction;

typedef enum { AXIS_TICK_OPTION_NONE, AXIS_TICK_OPTION_MAJOR, AXIS_TICK_OPTION_MINOR } AxisTickOption;

#define AXIS_TICK_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

/**
 * @ingroup axis_transactions
 * @brief Owns major/minor tick allocations until commit. A masked NULL candidate requests automatic ticks.
 */
typedef struct {
    unsigned int stagedMask;
    Ticks *majorTicksPtr;
    Ticks *minorTicksPtr;
} AxisTickTransaction;

/**
 * @ingroup axis_transactions
 * @brief Scalar minimum/maximum policies; staged distinguishes no change from a parsed pair.
 */
typedef struct {
    int staged;
    int looseMin;
    int looseMax;
} AxisLooseTransaction;

typedef enum {
    AXIS_PIXEL_OPTION_NONE,
    AXIS_PIXEL_OPTION_BORDER_WIDTH,
    AXIS_PIXEL_OPTION_LINE_WIDTH,
    AXIS_PIXEL_OPTION_SCROLL_INCREMENT
} AxisPixelOption;

#define AXIS_PIXEL_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

/**
 * @ingroup axis_transactions
 * @brief Scalar pixel candidates; stagedMask identifies parsed fields and no heap cleanup is needed.
 */
typedef struct {
    unsigned int stagedMask;
    int borderWidth;
    int lineWidth;
    int scrollIncrement;
} AxisPixelTransaction;

typedef enum {
    AXIS_SHADOW_OPTION_NONE,
    AXIS_SHADOW_OPTION_LIMITS,
    AXIS_SHADOW_OPTION_TICK,
    AXIS_SHADOW_OPTION_TITLE
} AxisShadowOption;

#define AXIS_SHADOW_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

/**
 * @ingroup axis_transactions
 * @brief Owns staged Tk colour references and offsets until commit; unmasked entries are zero initialized.
 */
typedef struct {
    unsigned int stagedMask;
    Shadow limitsShadow;
    Shadow tickShadow;
    Shadow titleShadow;
} AxisShadowTransaction;

/**
 * @ingroup axis_transactions
 * @brief Owns one staged string-list allocation until commit; staged distinguishes no change from an empty list.
 */
typedef struct {
    int staged;
    char **tags;
} AxisTagsTransaction;

/**
 * @ingroup axis_transactions
 * @brief Owns one split-list allocation with at most two formats until commit; count and staged accompany it.
 */
typedef struct {
    int staged;
    char **formats;
    int nFormats;
} AxisFormatTransaction;

static const Tk_OptionSpec axisOptionSpecs[] = {
    {TK_OPTION_DOUBLE, "-autorange", "autoRange", "AutoRange", DEF_AXIS_RANGE, -1, offsetof(Axis, windowSize), 0, NULL,
     AXIS_RESET_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_BORDER, "-background", "background", "Background", DEF_AXIS_BACKGROUND, -1, offsetof(Axis, border),
     TK_OPTION_NULL_OK, NULL, AXIS_REDRAW_MASK},
    {TK_OPTION_SYNONYM, "-bg", NULL, NULL, NULL, -1, -1, 0, "-background", 0},
    {TK_OPTION_STRING, "-bindtags", "bindTags", "BindTags", DEF_AXIS_TAGS, offsetof(Axis, bindTagsObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_TAGS_MASK},
    {TK_OPTION_SYNONYM, "-bd", NULL, NULL, NULL, -1, -1, 0, "-borderwidth", 0},
    {TK_OPTION_STRING, "-borderwidth", "borderWidth", "BorderWidth", DEF_AXIS_BORDERWIDTH,
     offsetof(Axis, borderWidthObjPtr), -1, 0, NULL, AXIS_PIXELS_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {/*
      * Tk_OptionSpec has no COLOR_ONLY/MONO_ONLY selection.
      * Use the standard colour-display default.
      */
     TK_OPTION_COLOR, "-color", "color", "Color", DEF_AXIS_FOREGROUND, -1, offsetof(Axis, tickTextStyle.color), 0, NULL,
     AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-command", "command", "Command", DEF_AXIS_COMMAND, offsetof(Axis, formatCmdObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_FORMAT_COMMAND_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-descending", "descending", "Descending", DEF_AXIS_DESCENDING, -1, offsetof(Axis, descending),
     0, NULL, AXIS_MAP_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", NULL, -1, offsetof(Axis, hidden), 0, NULL,
     AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_JUSTIFY, "-justify", "justify", "Justify", DEF_AXIS_JUSTIFY, -1, offsetof(Axis, titleTextStyle.justify),
     0, NULL, AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-labeloffset", "labelOffset", "LabelOffset", NULL, -1, offsetof(Axis, labelOffset), 0, NULL,
     AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_COLOR, "-limitscolor", "limitsColor", "Color", DEF_AXIS_FOREGROUND, -1,
     offsetof(Axis, limitsTextStyle.color), 0, NULL, AXIS_REDRAW_MASK},
    {TK_OPTION_FONT, "-limitsfont", "limitsFont", "Font", DEF_AXIS_TICK_FONT, -1, offsetof(Axis, limitsTextStyle.font),
     0, NULL, AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-limitsformat", "limitsFormat", "LimitsFormat", NULL, offsetof(Axis, limitsFormatObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_LIMITS_FORMAT_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-limitsshadow", "limitsShadow", "Shadow", NULL, offsetof(Axis, limitsShadowObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_LIMITS_SHADOW_MASK | AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-linewidth", "lineWidth", "LineWidth", DEF_AXIS_LINE_WIDTH, offsetof(Axis, lineWidthObjPtr), -1,
     0, NULL, AXIS_PIXELS_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-logscale", "logScale", "LogScale", DEF_AXIS_LOGSCALE, -1, offsetof(Axis, logScale), 0, NULL,
     AXIS_LOG_SCALE_MASK | AXIS_MAP_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-loose", "loose", "Loose", DEF_AXIS_LOOSE, offsetof(Axis, looseObjPtr), -1, 0, NULL,
     AXIS_LOOSE_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-majorticks", "majorTicks", "MajorTicks", NULL, offsetof(Axis, majorTicksObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_MAJOR_TICKS_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-max", "max", "Max", NULL, offsetof(Axis, maxObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     AXIS_LIMITS_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-min", "min", "Min", NULL, offsetof(Axis, minObjPtr), -1, TK_OPTION_NULL_OK, NULL,
     AXIS_LIMITS_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-minorticks", "minorTicks", "MinorTicks", NULL, offsetof(Axis, minorTicksObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_MINOR_TICKS_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief", DEF_AXIS_RELIEF, -1, offsetof(Axis, relief), 0, NULL,
     AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_DOUBLE, "-rotate", "rotate", "Rotate", DEF_AXIS_ROTATE, -1, offsetof(Axis, tickTextStyle.theta), 0, NULL,
     AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-scrollcommand", "scrollCommand", "ScrollCommand", NULL, offsetof(Axis, scrollCmdObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_SCROLL_COMMAND_MASK},
    {TK_OPTION_STRING, "-scrollincrement", "scrollIncrement", "ScrollIncrement", DEF_AXIS_SCROLL_INCREMENT,
     offsetof(Axis, scrollIncrementObjPtr), -1, 0, NULL, AXIS_PIXELS_MASK},
    {TK_OPTION_STRING, "-scrollmax", "scrollMax", "ScrollMax", NULL, offsetof(Axis, scrollMaxObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_SCROLL_LIMITS_MASK},
    {TK_OPTION_STRING, "-scrollmin", "scrollMin", "ScrollMin", NULL, offsetof(Axis, scrollMinObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_SCROLL_LIMITS_MASK},
    {TK_OPTION_DOUBLE, "-shiftby", "shiftBy", "ShiftBy", DEF_AXIS_SHIFTBY, -1, offsetof(Axis, shiftBy), 0, NULL,
     AXIS_MAP_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-showticks", "showTicks", "ShowTicks", DEF_AXIS_SHOWTICKS, -1, offsetof(Axis, showTicks), 0,
     NULL, AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_DOUBLE, "-stepsize", "stepSize", "StepSize", NULL, -1, offsetof(Axis, reqStep), 0, NULL,
     AXIS_MAP_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_INT, "-subdivisions", "subdivisions", "Subdivisions", DEF_AXIS_SUBDIVISIONS, -1,
     offsetof(Axis, reqNumMinorTicks), 0, NULL, AXIS_MAP_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_FONT, "-tickfont", "tickFont", "Font", DEF_AXIS_TICK_FONT, -1, offsetof(Axis, tickTextStyle.font), 0,
     NULL, AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_PIXELS, "-ticklength", "tickLength", "TickLength", DEF_AXIS_TICK_LENGTH, -1, offsetof(Axis, tickLength),
     TK_OPTION_NEG_OK, NULL, AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-tickshadow", "tickShadow", "Shadow", NULL, offsetof(Axis, tickShadowObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_TICK_SHADOW_MASK | AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-title", "title", "Title", NULL, -1, offsetof(Axis, title), TK_OPTION_NULL_OK, NULL,
     AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-titlealternate", "titleAlternate", "TitleAlternate", DEF_AXIS_TITLE_ALTERNATE, -1,
     offsetof(Axis, titleAlternate), 0, NULL, AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_COLOR, "-titlecolor", "titleColor", "Color", DEF_AXIS_FOREGROUND, -1,
     offsetof(Axis, titleTextStyle.color), 0, NULL, AXIS_REDRAW_MASK},
    {TK_OPTION_FONT, "-titlefont", "titleFont", "Font", DEF_AXIS_TITLE_FONT, -1, offsetof(Axis, titleTextStyle.font), 0,
     NULL, AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-titleshadow", "titleShadow", "Shadow", NULL, offsetof(Axis, titleShadowObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_TITLE_SHADOW_MASK | AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_END, NULL, NULL, NULL, NULL, 0, 0, 0, NULL, 0}};

/* Rotation for each axis title */
static double titleRotate[4] = {0.0, 90.0, 0.0, 270.0};

/* Forward declarations */
static ClientData MakeAxisTag(Graph *graphPtr, char *tagName);
static void SetAxisRange(AxisRange *rangePtr, double min, double max);
static double NormalizeAxisValue(const AxisRange *rangePtr, double value);
static double InterpolateAxisValue(const AxisRange *rangePtr, double norm);
static int InRange(double x, AxisRange *rangePtr);
static int AxisIsHorizontal(Graph *graphPtr, Axis *axisPtr);
static void FreeLabels(Rbc_Chain *chainPtr);
static void GetDataLimits(Axis *axisPtr, double min, double max);
static void FixAxisRange(Axis *axisPtr);
static double NiceNum(double x, int round);
static Ticks *GenerateTicks(TickSweep *sweepPtr);
static void LogScaleAxis(Axis *axisPtr, double min, double max);
static void LinearScaleAxis(Axis *axisPtr, double min, double max);
static void SweepTicks(Axis *axisPtr);
static void ResetTextStyles(Graph *graphPtr, Axis *axisPtr);
static void DestroyAxis(Graph *graphPtr, Axis *axisPtr);
static void AxisOffsets(Graph *graphPtr, Axis *axisPtr, int margin, int axisOffset, AxisInfo *infoPtr);
static void MakeAxisLine(Graph *graphPtr, Axis *axisPtr, int line, Segment2D *segPtr);
static void MakeTick(Graph *graphPtr, Axis *axisPtr, double value, int tick, int line, Segment2D *segPtr);
static void MapAxis(Graph *graphPtr, Axis *axisPtr, int offset, int margin);
static double LogAxisValue(double exponent);
static double MapHorizontalAxisCoordinate(Graph *graphPtr, Axis *axisPtr, double value);
static double MapVerticalAxisCoordinate(Graph *graphPtr, Axis *axisPtr, double value);
static void MakeLogRange(double value, double *minPtr, double *maxPtr);
static double AdjustViewport(double offset, double windowSize);
static void DrawAxis(Graph *graphPtr, Drawable drawable, Axis *axisPtr);
static void AxisExport(Rbc_ExportContext *exportPtr, Axis *axisPtr);
static void MakeGridLine(Graph *graphPtr, Axis *axisPtr, double value, Segment2D *segPtr);
static void GetAxisGeometry(Graph *graphPtr, Axis *axisPtr);
static int GetMarginGeometry(Graph *graphPtr, Margin *marginPtr);
static void ComputeMargins(Graph *graphPtr);
static Axis *CreateAxis(Graph *graphPtr, char *name, int margin);
static int ConfigureAxis(Graph *graphPtr, Axis *axisPtr);
static int NameToAxis(Graph *graphPtr, const char *name, Axis **axisPtrPtr);
static int GetAxis(Graph *graphPtr, const char *name, Rbc_Uid classUid, Axis **axisPtrPtr);
static void FreeAxis(Graph *graphPtr, Axis *axisPtr);
static int ConfigureNewAxis(Graph *graphPtr, Axis *axisPtr, Tcl_Size objc, Tcl_Obj *const objv[]);
static int InitAxisOptions(Graph *graphPtr, Axis *axisPtr);
static int ConfigureAxisOptions(Graph *graphPtr, Axis *axisPtr, Tcl_Size objc, Tcl_Obj *const objv[], int *maskPtr);
static void ReleaseAxisOptionResources(Graph *graphPtr, Axis *axisPtr);

typedef int RbcGrAxisOp(Graph *graphPtr, Axis *axisPtr, int margin, Tcl_Size objc, Tcl_Obj *const objv[]);
typedef int RbcGrAxisVirtualOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]);

typedef struct {
    Rbc_OpSpecHeader header;
    RbcGrAxisOp *proc;
} AxisOpSpec;

typedef struct {
    Rbc_OpSpecHeader header;
    RbcGrAxisVirtualOp *proc;
} AxisVirtualOpSpec;

static RbcGrAxisOp BindOp;
static RbcGrAxisOp CgetOp;
static RbcGrAxisOp ConfigureOp;
static RbcGrAxisOp InvTransformOp;
static RbcGrAxisOp LimitsOp;
static RbcGrAxisOp TransformOp;
static RbcGrAxisOp UseOp;

static RbcGrAxisVirtualOp BindVirtualOp;
static RbcGrAxisVirtualOp CgetVirtualOp;
static RbcGrAxisVirtualOp ConfigureVirtualOp;
static RbcGrAxisVirtualOp CreateVirtualOp;
static RbcGrAxisVirtualOp DeleteVirtualOp;
static RbcGrAxisVirtualOp GetOp;
static RbcGrAxisVirtualOp InvTransformVirtualOp;
static RbcGrAxisVirtualOp LimitsVirtualOp;
static RbcGrAxisVirtualOp NamesVirtualOp;
static RbcGrAxisVirtualOp TransformVirtualOp;
static RbcGrAxisVirtualOp ViewOp;

/**
 * @ingroup axis_presentation
 * @brief Clamp a wide layout size to a nonnegative int.
 *
 * Returns zero for nonpositive input; otherwise uses Rbc_ClampToInt. Does not alter graph state.
 */
static int LayoutSize(Tcl_WideInt value) {
    if (value <= 0) {
        return 0;
    }
    return Rbc_ClampToInt(value);
}

/**
 * @ingroup axis_presentation
 * @brief Clamp a wide layout range to a positive int.
 *
 * Returns one below the minimum usable scalar range; otherwise uses Rbc_ClampToInt. This arithmetic
 * floor does not make collapsed plot geometry drawable.
 */
static int LayoutRange(Tcl_WideInt value) {
    if (value < 1) {
        return 1;
    }
    return Rbc_ClampToInt(value);
}

/**
 * @ingroup axis_layout_control
 * @brief Validate the list representation of an optional command prefix.
 *
 * NULL and empty lists succeed. Returns a Tcl parsing status for other values; does not evaluate the
 * command, resolve its first word or retain list elements.
 */
static int ValidateAxisCommandPrefix(Tcl_Interp *interp, Tcl_Obj *objPtr) {
    Tcl_Size objc;
    Tcl_Obj **objv;

    if (objPtr == NULL) {
        return TCL_OK;
    }
    if (Tcl_ListObjGetElements(interp, objPtr, &objc, &objv) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @ingroup axis_layout_control
 * @brief Deliver the latest queued scrollbar fractions at idle.
 *
 * Clears the pending flag before delivery and skips destroyed graphs, deleted axes and absent/empty
 * prefixes. Copies the prefix and appends owned fraction objects before evaluation. Preserves
 * graph/interpreter storage and interpreter state, evaluates globally and reports errors in the
 * background. Does not dereference the axis after Tcl evaluation; destruction cancels outstanding idle
 * delivery.
 */
static void NotifyAxisScrollbar(ClientData clientData) {
    Axis *axisPtr = clientData;
    Graph *graphPtr = axisPtr->graphPtr;
    Tcl_Interp *interp = graphPtr->interp;
    Tcl_InterpState state;
    Tcl_Obj *command;
    Tcl_Size length;
    int code;

    axisPtr->scrollNotifyPending = FALSE;
    if (graphPtr->tkwin == NULL || axisPtr->deletePending || axisPtr->scrollCmdObjPtr == NULL) {
        return;
    }
    Tcl_ListObjLength(NULL, axisPtr->scrollCmdObjPtr, &length);
    if (length == 0) {
        return;
    }
    command = Tcl_DuplicateObj(axisPtr->scrollCmdObjPtr);
    Tcl_IncrRefCount(command);
    Tcl_ListObjAppendElement(NULL, command, Tcl_NewDoubleObj(axisPtr->scrollNotifyFirst));
    Tcl_ListObjAppendElement(NULL, command, Tcl_NewDoubleObj(axisPtr->scrollNotifyLast));
    Tcl_Preserve(graphPtr);
    Tcl_Preserve(interp);
    state = Tcl_SaveInterpState(interp, TCL_OK);
    code = Tcl_EvalObjEx(interp, command, TCL_EVAL_GLOBAL);
    if (code != TCL_OK) {
        Tcl_BackgroundException(interp, code);
    }
    Tcl_RestoreInterpState(interp, state);
    Tcl_DecrRefCount(command);
    Tcl_Release(graphPtr);
    Tcl_Release(interp);
}

/**
 * @ingroup axis_layout_control
 * @brief Coalesce a scrollbar notification using the latest fractions.
 *
 * Suppresses updates during GRAPH_EXPORT. Otherwise replaces pending fraction values and schedules at
 * most one idle callback for the axis. Does not evaluate Tcl, compare fractions with the previous
 * delivery or retain a separate axis reference.
 */
static void UpdateAxisScrollbar(Graph *graphPtr, Axis *axisPtr, double firstFract, double lastFract) {
    if (graphPtr->flags & GRAPH_EXPORT) {
        return;
    }
    axisPtr->scrollNotifyFirst = firstFract;
    axisPtr->scrollNotifyLast = lastFract;
    if (!axisPtr->scrollNotifyPending) {
        axisPtr->scrollNotifyPending = TRUE;
        Tcl_DoWhenIdle(NotifyAxisScrollbar, axisPtr);
    }
}

/**
 * @ingroup axis_transactions
 * @brief Compare a validated option with its canonical name.
 *
 * Uses the axis option table to resolve abbreviations and synonyms. Returns false when resolution
 * fails. This helper does not replace Tk option validation or report an interpreter error.
 *
 * @param[in] objPtr Borrowed option-name object resolved through the axis option table, including abbreviations
 *   and synonyms.
 * @param[in] optionName Borrowed NUL-terminated canonical option name, including its leading dash.
 */
static int IsAxisOption(Tcl_Obj *objPtr, const char *optionName) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, axisOptionSpecs);
    return ((name != NULL) && (strcmp(name, optionName) == 0));
}

/**
 * @ingroup axis_ranges
 * @brief Round a finite value to a multiple of a finite nonzero step.
 *
 * Uses halfway-away-from-zero rounding without an integer conversion. Returns the original value if
 * inputs, quotient or rounded product cannot be used safely; does not report a Tcl error.
 */
static double RoundToStep(double value, double step) {
    double scaled;
    double result;

    if ((!FINITE(value)) || (!FINITE(step)) || (step == 0.0)) {
        return value;
    }
    scaled = value / step;
    /*
     * Do not convert a potentially enormous quotient through int.
     * For ordinary values round() has the same halfway-away-from-zero
     * behaviour as the old Round() helper.
     */
    if (!FINITE(scaled)) {
        return value;
    }
    result = round(scaled) * step;
    if (!FINITE(result)) {
        return value;
    }
    return result;
}

/**
 * @ingroup axis_ranges
 * @brief Compute bounded enclosing tick endpoints and count.
 *
 * Requires finite endpoints and a positive finite step. Works in step units to avoid subtracting large
 * raw endpoints. Rejects nonfinite quotients/products and counts outside 1..MAXTICKS before converting
 * to Tcl_Size. Returns TRUE and assigns all outputs only on success; FALSE leaves outputs untouched.
 */
static int GetLinearSweep(double min, double max, double step, double *tickMinPtr, double *tickMaxPtr,
                          Tcl_Size *nTicksPtr) {
    double first;
    double last;
    double count;
    double tickMin;
    double tickMax;

    if ((!FINITE(min)) || (!FINITE(max)) || (!FINITE(step)) || (step <= 0.0)) {
        return FALSE;
    }
    /*
     * Work in units of the tick step.  Do not form
     *
     *     (tickMax - tickMin) / step
     *
     * because the subtraction can overflow even when min and max are
     * individually finite.
     */
    first = floor(min / step);
    last = ceil(max / step);
    if ((!FINITE(first)) || (!FINITE(last))) {
        return FALSE;
    }
    count = last - first + 1.0;
    /*
     * Validate while the count is still double.  Only convert to
     * Tcl_Size after proving that it is small and representable.
     */
    if ((!FINITE(count)) || (count < 1.0) || (count > (double)MAXTICKS)) {
        return FALSE;
    }
    tickMin = first * step;
    tickMax = last * step;
    if ((!FINITE(tickMin)) || (!FINITE(tickMax))) {
        return FALSE;
    }
    *tickMinPtr = tickMin + 0.0;
    *tickMaxPtr = tickMax + 0.0;
    *nTicksPtr = (Tcl_Size)count;
    return TRUE;
}

/**
 * @ingroup axis_ranges
 * @brief Select a practical automatic linear tick interval.
 *
 * First applies NiceNum to the representable positive span, aiming for DEF_NUM_TICKS intervals. If
 * that fails, uses quarter-scaled endpoint subtraction to avoid span overflow. Returns a positive
 * finite nice step when possible, otherwise 1.0; does not allocate or report errors.
 */
static double AutomaticLinearStep(double min, double max) {
    double range;
    double step;

    range = max - min;
    if (FINITE(range) && (range > 0.0)) {
        /*
         * Preserve the old automatic scaling calculation for ordinary
         * representable ranges.
         */
        range = NiceNum(range, 0);
        step = NiceNum(range / DEF_NUM_TICKS, 1);
        if (FINITE(step) && (step > 0.0)) {
            return step;
        }
    }
    /*
     * max - min may overflow even though both limits are finite.
     * Compute approximately one quarter of the range without first
     * forming the full difference.
     */
    range = max * 0.25 - min * 0.25;
    if (FINITE(range) && (range > 0.0)) {
        step = NiceNum(range, 1);
        if (FINITE(step) && (step > 0.0)) {
            return step;
        }
    }
    /*
     * Extremely degenerate/subnormal range.  A unit step is preferable
     * to producing a zero or non-finite tick interval.
     */
    return 1.0;
}

/**
 * @ingroup axis_presentation
 * @brief Round a rotated extent without overflowing int.
 *
 * Nonfinite or nonpositive inputs return zero. Values within half a unit of INT_MAX are saturated
 * before ROUND adds its rounding offset; ordinary positive extents retain legacy rounding.
 */
static int AxisRotatedSize(double value) {
    if ((!FINITE(value)) || (value <= 0.0)) {
        return 0;
    }
    /*
     * ROUND adds 0.5 before converting to int, so values within
     * half a unit of INT_MAX must also be clamped before that cast.
     */
    if (value >= ((double)INT_MAX - 0.5)) {
        return INT_MAX;
    }
    return ROUND(value);
}

/**
 * @ingroup axis_ranges
 * @brief Store endpoints and cached normalization factors.
 * @param[in,out] rangePtr Caller-owned range record whose endpoints, range and scale are replaced.
 * @param[in] min Lower endpoint in axis space; not sorted or validated here.
 * @param[in] max Upper endpoint in axis space; logarithmic ranges use exponents.
 *
 * Stores min/max unchanged, computes their difference and substitutes a unit range when its absolute
 * value is below DBL_EPSILON. Stores the reciprocal as scale. Does not sort or validate endpoints; an
 * overflowed difference remains nonfinite for the normalization helpers to handle.
 */
static void SetAxisRange(AxisRange *rangePtr, double min, double max) {
    rangePtr->min = min;
    rangePtr->max = max;
    rangePtr->range = max - min;
    if (FABS(rangePtr->range) < DBL_EPSILON) {
        rangePtr->range = 1.0;
    }
    rangePtr->scale = 1.0 / rangePtr->range;
}

/**
 * @ingroup axis_ranges
 * @brief Normalize a coordinate against a prepared axis range.
 * @param[in] rangePtr Borrowed prepared range with consistent endpoints and cached range/scale.
 * @param[in] value Axis-space coordinate, already logarithmically converted when needed.
 *
 * Uses cached range/scale for finite spans. For an overflowed span, halves endpoints and the value
 * before division; returns NAN when the half-span is invalid or zero. Does not clip to 0..1 or apply
 * logarithms, descending direction or screen offsets.
 */
static double NormalizeAxisValue(const AxisRange *rangePtr, double value) {
    /*
     * Use the ordinary expression whenever the stored range is
     * representable.  This preserves existing numerical behaviour
     * for normal axes.
     */
    if (FINITE(rangePtr->range)) {
        return (value - rangePtr->min) * rangePtr->scale;
    }
    /*
     * max - min can overflow even when min and max are both finite.
     * Dividing numerator and denominator by two first represents the
     * same normalized coordinate without forming that full range.
     */
    {
        double halfRange;
        halfRange = rangePtr->max * 0.5 - rangePtr->min * 0.5;
        if ((!FINITE(halfRange)) || (halfRange == 0.0)) {
            return NAN;
        }
        return (value * 0.5 - rangePtr->min * 0.5) / halfRange;
    }
}

/**
 * @ingroup axis_ranges
 * @brief Reconstruct an axis-space value from a normalized coordinate.
 * @param[in] rangePtr Borrowed prepared range.
 * @param[in] norm Normalized fraction; values outside [0,1] request extrapolation rather than clipping.
 *
 * Uses cached range and minimum for finite spans, or a half-scaled endpoint interpolation for
 * overflowed spans. Does not clamp the normalized coordinate. Extrapolation may produce nonfinite
 * results; callers must validate values before drawing or integer conversion.
 */
static double InterpolateAxisValue(const AxisRange *rangePtr, double norm) {
    if (FINITE(rangePtr->range)) {
        return norm * rangePtr->range + rangePtr->min;
    }
    /*
     * Reconstruct a value from an overflow-sized range without ever
     * forming max - min.  For norm in [0,1] this is the usual linear
     * interpolation between two finite endpoints.
     */
    return ((1.0 - norm) * (rangePtr->min * 0.5) + norm * (rangePtr->max * 0.5)) * 2.0;
}

/**
 * @ingroup axis_ranges
 * @brief Convert a base-ten exponent within the supported normal-double interval.
 * @param[in] exponent Base-ten exponent to convert; supported bounds are log10(DBL_MIN) through
 *     log10(DBL_MAX).
 *
 * Rejects nonfinite exponents and those outside log10(DBL_MIN)..log10(DBL_MAX) with NAN. Returns exact
 * DBL_MIN/DBL_MAX at the endpoints to avoid power rounding across them. Otherwise requires a finite
 * positive power result. This policy deliberately does not expose subnormal values.
 */
static double LogAxisValue(double exponent) {
    double minExponent;
    double maxExponent;
    double value;

    if (!FINITE(exponent)) {
        return NAN;
    }
    minExponent = log10(DBL_MIN);
    maxExponent = log10(DBL_MAX);
    if ((exponent < minExponent) || (exponent > maxExponent)) {
        return NAN;
    }
    /*
     * Avoid pow() rounding across the representable endpoints.
     */
    if (exponent <= minExponent) {
        return DBL_MIN;
    }
    if (exponent >= maxExponent) {
        return DBL_MAX;
    }
    value = EXP10(exponent);
    if ((!FINITE(value)) || (value <= 0.0)) {
        return NAN;
    }
    return value;
}

/**
 * @ingroup axis_ranges
 * @brief Map an already converted axis-space value horizontally.
 * @param[in] graphPtr Borrowed graph with current layout and mapping scales.
 * @param[in] axisPtr Borrowed axis with a prepared axisRange and live owning track, if any.
 * @param[in] value Already converted axis-space value; logarithmic axes require an exponent, not raw data.
 *
 * Normalizes, rejects nonfinite normalized values and applies descending reversal. Uses the owning
 * track world rectangle when present, otherwise graph horizontal range/offset. Track spans have a one-
 * pixel minimum. Does not perform logarithmic conversion or clipping.
 */
static double MapHorizontalAxisCoordinate(Graph *graphPtr, Axis *axisPtr, double value) {
    value = NormalizeAxisValue(&axisPtr->axisRange, value);
    if (!FINITE(value)) {
        return NAN;
    }
    if (axisPtr->descending) {
        value = 1.0 - value;
    }
    if (axisPtr->track != NULL) {
        return value * MAX(1, axisPtr->track->worldRight - axisPtr->track->worldLeft) + axisPtr->track->worldLeft;
    }
    return value * graphPtr->hRange + graphPtr->hOffset;
}

/**
 * @ingroup axis_ranges
 * @brief Map an already converted axis-space value vertically.
 * @param[in] graphPtr Borrowed graph with current layout and mapping scales.
 * @param[in] axisPtr Borrowed axis with a prepared axisRange and live owning track, if any.
 * @param[in] value Already converted axis-space value; logarithmic axes require an exponent, not raw data.
 *
 * Normalizes, rejects nonfinite normalized values and applies descending reversal and downward screen
 * Y direction. Uses the owning track world rectangle or graph vertical range/offset. Track spans have
 * a one-pixel minimum; no clipping or logarithmic conversion occurs here.
 */
static double MapVerticalAxisCoordinate(Graph *graphPtr, Axis *axisPtr, double value) {
    value = NormalizeAxisValue(&axisPtr->axisRange, value);
    if (!FINITE(value)) {
        return NAN;
    }
    if (axisPtr->descending) {
        value = 1.0 - value;
    }
    if (axisPtr->track != NULL) {
        return (1.0 - value) * MAX(1, axisPtr->track->worldBottom - axisPtr->track->worldTop) +
               axisPtr->track->worldTop;
    }
    return (1.0 - value) * graphPtr->vRange + graphPtr->vOffset;
}

/**
 * @ingroup axis_ranges
 * @brief Construct a positive logarithmic interval around one value.
 * @param[in] value Raw positive data value around which to form a range; invalid inputs select fallback
 *     bounds.
 * @param[out] minPtr Required separate output for the positive raw-data lower limit.
 * @param[out] maxPtr Required separate output for the positive raw-data upper limit.
 *
 * Invalid or nonpositive input selects 0.001..1.0. Otherwise chooses roughly one decade centred on the
 * value, shifting the interval at supported exponent endpoints, and converts it with LogAxisValue.
 * Writes both outputs without changing an Axis record.
 */
static void MakeLogRange(double value, double *minPtr, double *maxPtr) {
    double exponent;
    double low;
    double high;
    double minExponent;
    double maxExponent;

    if ((!FINITE(value)) || (value <= 0.0)) {
        *minPtr = 0.001;
        *maxPtr = 1.0;
        return;
    }
    minExponent = log10(DBL_MIN);
    maxExponent = log10(DBL_MAX);
    exponent = log10(value);
    low = exponent - 0.5;
    high = exponent + 0.5;
    if (low < minExponent) {
        low = minExponent;
        high = MIN(maxExponent, minExponent + 1.0);
    }
    if (high > maxExponent) {
        high = maxExponent;
        low = MAX(minExponent, maxExponent - 1.0);
    }
    *minPtr = LogAxisValue(low);
    *maxPtr = LogAxisValue(high);
}

/**
 * @ingroup axis_ranges
 * @brief Test a finite coordinate against a prepared range.
 * @param[in] x Axis-space coordinate to test, not unconverted logarithmic data.
 * @param[in] rangePtr Borrowed prepared range; not modified despite the non-const pointer type.
 *
 * Normalizes the coordinate, rejects nonfinite results and applies DBL_EPSILON tolerance around the
 * unit interval. Uses axis-space coordinates, which are exponents for logarithmic axes; does not
 * perform raw-data log conversion.
 */
static int InRange(double x, AxisRange *rangePtr) {
    double norm;

    if (!FINITE(x)) {
        return FALSE;
    }
    norm = NormalizeAxisValue(rangePtr, x);
    if (!FINITE(norm)) {
        return FALSE;
    }
    return ((norm >= -DBL_EPSILON) && ((norm - 1.0) < DBL_EPSILON));
}

/**
 * @ingroup axis_ranges
 * @brief Resolve logical axis identity into screen orientation.
 * @param[in] graphPtr Borrowed graph supplying inversion state.
 * @param[in] axisPtr Borrowed axis supplying its logical class identity.
 *
 * Compares the logical Y class with graph inversion. Borrows graph and axis; does not modify their
 * class or layout.
 */
static int AxisIsHorizontal(Graph *graphPtr, Axis *axisPtr) {
    return ((axisPtr->classUid == rbcYAxisUid) == graphPtr->inverted);
}

/* ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */

/**
 * @ingroup axis_transactions
 * @brief Recognize the -bindtags option.
 *
 * Delegates canonical-name matching to IsAxisOption after Tk has rejected unknown or ambiguous
 * options.
 *
 * @param[in] objPtr Borrowed option-name object resolved through the axis option table, including abbreviations
 *   and synonyms.
 */
static int IsAxisBindTagsOption(Tcl_Obj *objPtr) { return IsAxisOption(objPtr, "-bindtags"); }

/**
 * @ingroup axis_transactions
 * @brief Stage a replacement binding-tag list.
 *
 * Parses into a new string-list allocation before releasing the previous candidate. Success owns the
 * new allocation and sets staged; failure preserves the prior candidate. Does not alter live tags.
 *
 * @param[in,out] interp Borrowed interpreter receiving conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed Tcl list of binding tags; its strings are copied into candidate storage.
 * @param[in,out] transactionPtr Initialized or zeroed caller-owned candidate transaction; staged fields are
 *   updated only after successful parsing.
 */
static int StageAxisTags(Tcl_Interp *interp, Tcl_Obj *objPtr, AxisTagsTransaction *transactionPtr) {
    char **newTags;

    newTags = NULL;
    if (Rbc_GetStringListFromObj(interp, objPtr, &newTags) != TCL_OK) {
        return TCL_ERROR;
    }
    if (transactionPtr->tags != NULL) {
        ckfree((char *)transactionPtr->tags);
    }
    transactionPtr->tags = newTags;
    transactionPtr->staged = TRUE;
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Discard an axis tags transaction.
 *
 * Releases owned tag-list allocation and zeroes the complete transaction. Requires initialized or
 * zeroed storage. Safe after commit because transferred ownership has been cleared; does not alter
 * live axis fields.
 *
 * @param[in,out] transactionPtr Initialized or zeroed transaction whose candidate resources are released and
 *   complete contents reset; record is retained.
 */
static void FreeAxisTagsTransaction(AxisTagsTransaction *transactionPtr) {
    if (transactionPtr->tags != NULL) {
        ckfree((char *)transactionPtr->tags);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @ingroup axis_transactions
 * @brief Prepare the axis tags option family.
 *
 * Initializes fresh transaction storage; do not pass an existing owning transaction. Examines the
 * borrowed even-length option vector, identifies explicit options, and on first configuration parses
 * effective defaults only for options not explicitly overridden. Processes every explicit occurrence
 * in caller order, so a bad earlier value cannot be hidden by a later valid one. Returns a Tcl status
 * without committing live derived fields. On error releases all candidates owned by this transaction.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] axisPtr Borrowed axis with candidate Tk fields and the original even-length option vector
 *   installed; derived fields are not committed here.
 * @param[out] transactionPtr Fresh caller-owned transaction storage, initialized here; must not already own
 *   resources. Success requires commit or cleanup.
 */
static int PrepareAxisTagsTransaction(Graph *graphPtr, Axis *axisPtr, AxisTagsTransaction *transactionPtr) {
    int explicitlySpecified;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitlySpecified = FALSE;
    assert((axisPtr->optionObjc & 1) == 0);
    /*
     * Determine whether -bindtags was explicitly supplied.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        if (IsAxisBindTagsOption(axisPtr->optionObjv[i])) {
            explicitlySpecified = TRUE;
        }
    }
    /*
     * During initial configuration, parse the effective
     * default or option-database value unless explicitly overridden.
     */
    if (!axisPtr->optionsConfigured && !explicitlySpecified && (axisPtr->bindTagsObjPtr != NULL)) {
        if (StageAxisTags(graphPtr->interp, axisPtr->bindTagsObjPtr, transactionPtr) != TCL_OK) {
            goto error;
        }
    }
    /*
     * Process every explicit occurrence in caller order.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        if (IsAxisBindTagsOption(axisPtr->optionObjv[i])) {
            if (StageAxisTags(graphPtr->interp, axisPtr->optionObjv[i + 1], transactionPtr) != TCL_OK) {
                goto error;
            }
        }
    }
    return TCL_OK;

error:
    FreeAxisTagsTransaction(transactionPtr);
    return TCL_ERROR;
}

/**
 * @ingroup axis_transactions
 * @brief Commit prepared axis tags candidates.
 *
 * Transfers the staged list, clears its pointer and flag, then frees the old live list. Unstaged
 * options are unchanged. Performs no parsing, callback evaluation or recoverable validation; the
 * caller must have prepared all option families first.
 *
 * @param[in,out] axisPtr Live axis receiving only staged derived fields; all option families must be validated
 *   before any commit.
 * @param[in,out] transactionPtr Successfully prepared candidate transaction; transferred pointers and staging
 *   flags are cleared without freeing the record.
 */
static void CommitAxisTagsTransaction(Axis *axisPtr, AxisTagsTransaction *transactionPtr) {
    char **oldTags;

    if (!transactionPtr->staged) {
        return;
    }
    oldTags = axisPtr->tags;
    axisPtr->tags = transactionPtr->tags;
    transactionPtr->tags = NULL;
    transactionPtr->staged = FALSE;
    if (oldTags != NULL) {
        ckfree((char *)oldTags);
    }
}

/**
 * @ingroup axis_transactions
 * @brief Release a split limit-format allocation.
 *
 * Accepts NULL. Frees the single allocation, not each string separately; cannot clear the caller
 * pointer.
 *
 * @param[in] formats Owned split-list allocation to release, or NULL; individual strings must not be freed
 *   separately.
 */
static void FreeAxisFormats(char **formats) {
    if (formats != NULL) {
        ckfree((char *)formats);
    }
}

/**
 * @ingroup axis_transactions
 * @brief Recognize the -limitsformat option.
 *
 * Delegates canonical-name matching to IsAxisOption after Tk has rejected unknown or ambiguous
 * options.
 *
 * @param[in] objPtr Borrowed option-name object resolved through the axis option table, including abbreviations
 *   and synonyms.
 */
static int IsAxisLimitsFormatOption(Tcl_Obj *objPtr) { return IsAxisOption(objPtr, "-limitsformat"); }

/**
 * @ingroup axis_transactions
 * @brief Validate a format string used with one double argument.
 *
 * Allows literal text, escaped percent signs and at most one a/A/e/E/f/F/g/G conversion, optionally
 * with lowercase l. Rejects dynamic width/precision, other conversion types and excessive format
 * length, width or precision above TICK_LABEL_SIZE. Zero conversions are allowed. Returns a Tcl
 * status; it validates argument compatibility rather than guaranteeing the rendered output fits a
 * label buffer.
 *
 * @param[in,out] interp Borrowed interpreter receiving conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] format Borrowed NUL-terminated printf-style format to validate for a single double argument.
 */
static int ValidateAxisLimitFormat(Tcl_Interp *interp, const char *format) {
    const char *p;
    int nConversions;

    if (strlen(format) > TICK_LABEL_SIZE) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("axis limit format is too long: \"%s\"", format));
        return TCL_ERROR;
    }
    nConversions = 0;
    for (p = format; *p != '\0'; p++) {
        unsigned long value;

        if (*p != '%') {
            continue;
        }
        p++;
        /*
         * A literal percent does not consume an argument.
         */
        if (*p == '%') {
            continue;
        }
        if (*p == '\0') {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("incomplete axis limit format \"%s\"", format));
            return TCL_ERROR;
        }
        /*
         * Only one double is passed to the formatter.
         */
        if (nConversions != 0) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("axis limit format \"%s\" "
                                                   "contains more than one conversion",
                                                   format));
            return TCL_ERROR;
        }
        /*
         * Standard printf flags.
         */
        while ((*p == '-') || (*p == '+') || (*p == ' ') || (*p == '#') || (*p == '0')) {
            p++;
        }
        /*
         * Dynamic width requires another argument and is therefore
         * not valid here.
         */
        if (*p == '*') {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("axis limit format \"%s\" "
                                                   "cannot use a dynamic field width",
                                                   format));
            return TCL_ERROR;
        }
        value = 0;
        while ((*p >= '0') && (*p <= '9')) {
            if (value <= (unsigned long)TICK_LABEL_SIZE) {
                value = value * 10u + (unsigned long)(*p - '0');
            }
            p++;
        }
        if (value > (unsigned long)TICK_LABEL_SIZE) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("axis limit format \"%s\" "
                                                   "has an excessive field width",
                                                   format));
            return TCL_ERROR;
        }
        if (*p == '.') {
            p++;
            if (*p == '*') {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("axis limit format \"%s\" "
                                                       "cannot use a dynamic precision",
                                                       format));
                return TCL_ERROR;
            }
            value = 0;
            while ((*p >= '0') && (*p <= '9')) {
                if (value <= (unsigned long)TICK_LABEL_SIZE) {
                    value = value * 10u + (unsigned long)(*p - '0');
                }
                p++;
            }
            if (value > (unsigned long)TICK_LABEL_SIZE) {
                Tcl_SetObjResult(interp, Tcl_ObjPrintf("axis limit format \"%s\" "
                                                       "has an excessive precision",
                                                       format));
                return TCL_ERROR;
            }
        }
        /*
         * printf accepts 'l' with floating conversions.  'L' would
         * require a long double, which we do not pass.
         */
        if (*p == 'l') {
            p++;
        }
        if ((*p != 'a') && (*p != 'A') && (*p != 'e') && (*p != 'E') && (*p != 'f') && (*p != 'F') && (*p != 'g') &&
            (*p != 'G')) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("invalid floating-point conversion "
                                                   "in axis limit format \"%s\"",
                                                   format));
            return TCL_ERROR;
        }
        nConversions++;
    }
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Allocate zero, one or two validated limit formats.
 *
 * Initializes outputs to NULL and zero. NULL or an empty string succeeds without allocation. Splits
 * the Tcl list, rejects more than two entries and validates each format. Failure frees the split
 * allocation. Success transfers one allocation containing the pointer list and strings to the caller;
 * free it with FreeAxisFormats.
 *
 * @param[in,out] interp Borrowed interpreter receiving conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed list of zero to two format strings; NULL or empty selects no formats.
 * @param[out] formatsPtrPtr Required output initialized to NULL; successful nonempty output transfers one
 *   allocation released by FreeAxisFormats.
 * @param[out] nFormatsPtr Required output initialized to zero; receives the validated format count on success.
 */
static int GetAxisLimitsFormatsFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, char ***formatsPtrPtr, int *nFormatsPtr) {
    const char *string;
    char **formats;
    Tcl_Size nFormats;

    *formatsPtrPtr = NULL;
    *nFormatsPtr = 0;
    if (objPtr == NULL) {
        return TCL_OK;
    }
    string = Tcl_GetString(objPtr);
    if (string[0] == '\0') {
        return TCL_OK;
    }
    formats = NULL;
    nFormats = 0;
    if (Tcl_SplitList(interp, string, &nFormats, (const char ***)&formats) != TCL_OK) {
        return TCL_ERROR;
    }
    if (nFormats > 2) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("too many elements in limits format list \"%s\"", string));
        FreeAxisFormats(formats);
        return TCL_ERROR;
    }
    {
        Tcl_Size i;

        for (i = 0; i < nFormats; i++) {
            if (ValidateAxisLimitFormat(interp, formats[i]) != TCL_OK) {
                FreeAxisFormats(formats);
                return TCL_ERROR;
            }
        }
    }
    *formatsPtrPtr = formats;
    *nFormatsPtr = (int)nFormats;
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Stage replacement axis-limit formats.
 *
 * Parses and validates the replacement before freeing previous staged formats. Success records the new
 * allocation, count and staged flag; failure preserves the previous candidate.
 *
 * @param[in,out] interp Borrowed interpreter receiving conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed list of zero to two format strings; NULL or empty stages removal.
 * @param[in,out] transactionPtr Initialized or zeroed caller-owned candidate transaction; staged fields are
 *   updated only after successful parsing.
 */
static int StageAxisLimitsFormats(Tcl_Interp *interp, Tcl_Obj *objPtr, AxisFormatTransaction *transactionPtr) {
    char **newFormats;
    int newNFormats;

    newFormats = NULL;
    newNFormats = 0;
    if (GetAxisLimitsFormatsFromObj(interp, objPtr, &newFormats, &newNFormats) != TCL_OK) {
        return TCL_ERROR;
    }
    FreeAxisFormats(transactionPtr->formats);
    transactionPtr->formats = newFormats;
    transactionPtr->nFormats = newNFormats;
    transactionPtr->staged = TRUE;
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Discard an axis format transaction.
 *
 * Releases owned format-list allocation and zeroes the complete transaction. Requires initialized or
 * zeroed storage. Safe after commit because transferred ownership has been cleared; does not alter
 * live axis fields.
 *
 * @param[in,out] transactionPtr Initialized or zeroed transaction whose candidate resources are released and
 *   complete contents reset; record is retained.
 */
static void FreeAxisFormatTransaction(AxisFormatTransaction *transactionPtr) {
    FreeAxisFormats(transactionPtr->formats);
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @ingroup axis_transactions
 * @brief Prepare the axis format option family.
 *
 * Initializes fresh transaction storage; do not pass an existing owning transaction. Examines the
 * borrowed even-length option vector, identifies explicit options, and on first configuration parses
 * effective defaults only for options not explicitly overridden. Processes every explicit occurrence
 * in caller order, so a bad earlier value cannot be hidden by a later valid one. Returns a Tcl status
 * without committing live derived fields. On error releases all candidates owned by this transaction.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] axisPtr Borrowed axis with candidate Tk fields and the original even-length option vector
 *   installed; derived fields are not committed here.
 * @param[out] transactionPtr Fresh caller-owned transaction storage, initialized here; must not already own
 *   resources. Success requires commit or cleanup.
 */
static int PrepareAxisFormatTransaction(Graph *graphPtr, Axis *axisPtr, AxisFormatTransaction *transactionPtr) {
    int explicitlySpecified;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitlySpecified = FALSE;
    assert((axisPtr->optionObjc & 1) == 0);
    /*
     * Determine whether -limitsformat was explicitly supplied.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        if (IsAxisLimitsFormatOption(axisPtr->optionObjv[i])) {
            explicitlySpecified = TRUE;
        }
    }
    /*
     * During initial configuration, parse the effective option
     * database/default value unless explicitly overridden.
     */
    if (!axisPtr->optionsConfigured && !explicitlySpecified && (axisPtr->limitsFormatObjPtr != NULL)) {
        if (StageAxisLimitsFormats(graphPtr->interp, axisPtr->limitsFormatObjPtr, transactionPtr) != TCL_OK) {
            goto error;
        }
    }
    /*
     * Process explicit occurrences in caller order.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        if (IsAxisLimitsFormatOption(axisPtr->optionObjv[i])) {
            if (StageAxisLimitsFormats(graphPtr->interp, axisPtr->optionObjv[i + 1], transactionPtr) != TCL_OK) {
                goto error;
            }
        }
    }
    return TCL_OK;

error:
    FreeAxisFormatTransaction(transactionPtr);
    return TCL_ERROR;
}

/**
 * @ingroup axis_transactions
 * @brief Commit prepared axis format candidates.
 *
 * Transfers the staged format list and count, clears the candidate pointer/count/flag, then frees the
 * old live allocation. Unstaged options are unchanged. Performs no parsing, callback evaluation or
 * recoverable validation; the caller must have prepared all option families first.
 *
 * @param[in,out] axisPtr Live axis receiving only staged derived fields; all option families must be validated
 *   before any commit.
 * @param[in,out] transactionPtr Successfully prepared candidate transaction; transferred pointers and staging
 *   flags are cleared without freeing the record.
 */
static void CommitAxisFormatTransaction(Axis *axisPtr, AxisFormatTransaction *transactionPtr) {
    char **oldFormats;

    if (!transactionPtr->staged) {
        return;
    }
    oldFormats = axisPtr->limitsFormats;
    axisPtr->limitsFormats = transactionPtr->formats;
    axisPtr->nFormats = transactionPtr->nFormats;
    transactionPtr->formats = NULL;
    transactionPtr->nFormats = 0;
    transactionPtr->staged = FALSE;
    FreeAxisFormats(oldFormats);
}

/**
 * @ingroup axis_configuration
 * @brief Initialize Tk options through the named Axis component.
 *
 * Returns TCL_OK if already initialized or successfully initialized. On initialization failure, frees
 * partially installed Tk resources and the option table. The component resource name is temporary; the
 * Axis retains the option table.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in,out] axisPtr Borrowed named axis receiving its option table and defaults; already initialized axes
 *   are unchanged.
 */
static int InitAxisOptions(Graph *graphPtr, Axis *axisPtr) {
    char *componentName;
    int result;

    if (axisPtr->optionsInitialized) {
        return TCL_OK;
    }
    axisPtr->optionTable = Tk_CreateOptionTable(graphPtr->interp, axisOptionSpecs);
    if (axisPtr->optionTable == NULL) {
        return TCL_ERROR;
    }
    /*
     * Preserve the legacy component resource hierarchy:
     *
     *     *graph.x.logScale
     *     *graph.Axis.logScale
     *
     * Temporary Tk child names cannot begin with an uppercase
     * character, so retain the same first-character conversion used
     * by the modern element path.
     */
    componentName = Rbc_GetComponentResourceName(axisPtr->name);
    result = Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, graphPtr->optionProxy, componentName, "Axis",
                                      (char *)axisPtr, axisPtr->optionTable);
    ckfree(componentName);
    if (result != TCL_OK) {
        /* Tk_InitOptions leaves earlier options initialized on failure. */
        Tk_FreeConfigOptions((char *)axisPtr, axisPtr->optionTable, graphPtr->tkwin);
        Tk_DeleteOptionTable(axisPtr->optionTable);
        axisPtr->optionTable = NULL;
        return TCL_ERROR;
    }
    axisPtr->optionsInitialized = TRUE;
    axisPtr->tkResourcesReleased = FALSE;
    return TCL_OK;
}

/**
 * @ingroup axis_configuration
 * @brief Clear borrowed configuration arguments and the option mask.
 *
 * Does not free the caller-owned object vector or alter committed option values.
 *
 * @param[in,out] axisPtr Borrowed axis whose temporary option vector/count/mask are cleared; committed options
 *   remain unchanged.
 */
static void ResetAxisOptionContext(Axis *axisPtr) {
    axisPtr->optionMask = 0;
    axisPtr->optionObjc = 0;
    axisPtr->optionObjv = NULL;
}

/**
 * @ingroup axis_configuration
 * @brief Apply one axis option transaction and construct derived state.
 *
 * Requires initialized options and an even object count. Borrows objv only during ConfigureAxis,
 * clearing that context before rollback or return. On failure restores Tk-managed options; on success
 * releases saved options, marks configuration notification pending, and optionally returns the changed
 * mask. Returns a Tcl status.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in,out] axisPtr Borrowed axis with initialized Tk option table; candidate fields are rolled back on
 *   recoverable failure.
 * @param[in] objc Even number of option/value entries, excluding widget and operation names; zero is allowed.
 * @param[in] objv Borrowed option/value array of objc entries, valid through configuration and any expression
 *   evaluation.
 * @param[out] maskPtr Optional changed-option mask output; assigned only on successful configuration.
 */
static int ConfigureAxisOptions(Graph *graphPtr, Axis *axisPtr, Tcl_Size objc, Tcl_Obj *const objv[], int *maskPtr) {
    Tk_SavedOptions savedOptions;
    int mask;

    assert(axisPtr->optionsInitialized);
    assert(axisPtr->optionTable != NULL);
    assert((objc & 1) == 0);
    /*
     * Clear stale transaction context before invoking Tk.
     */
    ResetAxisOptionContext(axisPtr);
    if (Tk_SetOptions(graphPtr->interp, (char *)axisPtr, axisPtr->optionTable, objc, objv, graphPtr->tkwin,
                      &savedOptions, &mask) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Make the changed-option mask and original argument order
     * available to the axis transactions.
     */
    axisPtr->optionMask = mask;
    axisPtr->optionObjc = objc;
    axisPtr->optionObjv = objv;
    if (ConfigureAxis(graphPtr, axisPtr) != TCL_OK) {
        /*
         * Never retain pointers to the caller-owned option vector
         * while restoring the configuration.
         */
        ResetAxisOptionContext(axisPtr);
        return Rbc_RestoreOptionsOnError(graphPtr->interp, &savedOptions);
    }
    ResetAxisOptionContext(axisPtr);
    axisPtr->optionsConfigured = TRUE;
    axisPtr->notifyConfigure = TRUE;
    Tk_FreeSavedOptions(&savedOptions);
    if (maskPtr != NULL) {
        *maskPtr = mask;
    }
    return TCL_OK;
}

/**
 * @ingroup axis_configuration
 * @brief Initialize options and derive a usable axis configuration.
 *
 * Runs configuration even for an empty option vector so database defaults reach derived fields.
 * Returns a Tcl status; the caller owns failure cleanup. An already initialized revived axis retains
 * its option table.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in,out] axisPtr New or revived axis to initialize/configure; caller remains responsible for failure
 *   cleanup.
 * @param[in] objc Even number of option/value entries, excluding widget and operation names; zero is allowed.
 * @param[in] objv Borrowed option/value array of objc entries, valid through configuration and any expression
 *   evaluation.
 */
static int ConfigureNewAxis(Graph *graphPtr, Axis *axisPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    if (InitAxisOptions(graphPtr, axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Configure even when objc is zero. Tk_InitOptions has installed
     * defaults and option-database values that must be converted into
     * the live axis fields.
     */
    return ConfigureAxisOptions(graphPtr, axisPtr, objc, objv, NULL);
}

/**
 * @ingroup axis_configuration
 * @brief Release Tk-owned option resources once.
 *
 * Clears borrowed transaction context and deletes the initialized option table. Does not release
 * derived labels, geometry, shadow colours or text-style graphics contexts; DestroyAxis owns those
 * resources.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in,out] axisPtr Borrowed axis whose Tk option resources are released once; derived resources and
 *   record remain for separate cleanup.
 */
static void ReleaseAxisOptionResources(Graph *graphPtr, Axis *axisPtr) {
    if (axisPtr->tkResourcesReleased) {
        return;
    }
    /*
     * Do not retain pointers to configuration arguments during
     * destruction.
     */
    ResetAxisOptionContext(axisPtr);
    if (axisPtr->optionsInitialized) {
        Tk_FreeConfigOptions((char *)axisPtr, axisPtr->optionTable, graphPtr->tkwin);
        Tk_DeleteOptionTable(axisPtr->optionTable);
        axisPtr->optionTable = NULL;
        axisPtr->optionsInitialized = FALSE;
    }
    axisPtr->tkResourcesReleased = TRUE;
}

/**
 * @ingroup axis_transactions
 * @brief Classify an axis limit option.
 *
 * Resolves the canonical name using axisOptionSpecs and returns the corresponding enum for display and
 * scroll minima/maxima. Returns the NONE enum for unrelated or unresolved names; performs no
 * allocation.
 *
 * @param[in] objPtr Borrowed option-name object resolved through the axis option table, including abbreviations
 *   and synonyms.
 */
static AxisLimitOption GetAxisLimitOption(Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, axisOptionSpecs);
    if (name == NULL) {
        return AXIS_LIMIT_OPTION_NONE;
    }
    if (strcmp(name, "-min") == 0) {
        return AXIS_LIMIT_OPTION_MIN;
    }
    if (strcmp(name, "-max") == 0) {
        return AXIS_LIMIT_OPTION_MAX;
    }
    if (strcmp(name, "-scrollmin") == 0) {
        return AXIS_LIMIT_OPTION_SCROLL_MIN;
    }
    if (strcmp(name, "-scrollmax") == 0) {
        return AXIS_LIMIT_OPTION_SCROLL_MAX;
    }
    return AXIS_LIMIT_OPTION_NONE;
}

/**
 * @ingroup axis_transactions
 * @brief Evaluate an optional finite axis-limit expression.
 *
 * NULL or an empty object selects VALUE_UNDEFINED. Otherwise calls Tcl_ExprDoubleObj and rejects
 * nonfinite results. Returns a Tcl status. Expression evaluation may execute Tcl substitutions; it is
 * not a literal-only conversion and does not itself preserve graph or axis lifetime. The output is
 * usable only on success.
 *
 * @param[in,out] interp Borrowed interpreter receiving conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed finite Tcl expression, or NULL/empty for VALUE_UNDEFINED; evaluation may execute
 *   Tcl.
 * @param[out] valuePtr Required output receiving a finite limit or VALUE_UNDEFINED on success; unusable on
 *   error.
 */
static int GetAxisLimitFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, double *valuePtr) {
    Tcl_Size length;

    if (objPtr == NULL) {
        *valuePtr = VALUE_UNDEFINED;
        return TCL_OK;
    }
    (void)Tcl_GetStringFromObj(objPtr, &length);
    if (length == 0) {
        *valuePtr = VALUE_UNDEFINED;
        return TCL_OK;
    }
    if (Tcl_ExprDoubleObj(interp, objPtr, valuePtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (!FINITE(*valuePtr)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("axis limit must be a finite value", -1));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Classify an axis pixel option.
 *
 * Resolves the canonical name using axisOptionSpecs and returns the corresponding enum for border
 * width, line width and scroll increment. Returns the NONE enum for unrelated or unresolved names;
 * performs no allocation.
 *
 * @param[in] objPtr Borrowed option-name object resolved through the axis option table, including abbreviations
 *   and synonyms.
 */
static AxisPixelOption GetAxisPixelOption(Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, axisOptionSpecs);
    if (name == NULL) {
        return AXIS_PIXEL_OPTION_NONE;
    }
    if (strcmp(name, "-borderwidth") == 0) {
        return AXIS_PIXEL_OPTION_BORDER_WIDTH;
    }
    if (strcmp(name, "-linewidth") == 0) {
        return AXIS_PIXEL_OPTION_LINE_WIDTH;
    }
    if (strcmp(name, "-scrollincrement") == 0) {
        return AXIS_PIXEL_OPTION_SCROLL_INCREMENT;
    }
    return AXIS_PIXEL_OPTION_NONE;
}

/**
 * @ingroup axis_transactions
 * @brief Stage one validated pixel-distance value.
 *
 * Border and line widths must be nonnegative; scroll increment must be positive. Uses Tk-window
 * scaling through Rbc_GetPixelsFromObj. Success updates the selected scalar and mask only; an invalid
 * internal enum panics.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] objPtr Borrowed screen-distance value interpreted using the graph window scaling.
 * @param[in] option Valid non-NONE selector for this option family; invalid internal selectors panic.
 * @param[in,out] transactionPtr Initialized or zeroed caller-owned candidate transaction; staged fields are
 *   updated only after successful parsing.
 */
static int StageAxisPixelOption(Graph *graphPtr, Tcl_Obj *objPtr, AxisPixelOption option,
                                AxisPixelTransaction *transactionPtr) {
    int check;
    int value;

    switch (option) {
    case AXIS_PIXEL_OPTION_BORDER_WIDTH:
    case AXIS_PIXEL_OPTION_LINE_WIDTH:
        check = PIXELS_NONNEGATIVE;
        break;
    case AXIS_PIXEL_OPTION_SCROLL_INCREMENT:
        check = PIXELS_POSITIVE;
        break;
    case AXIS_PIXEL_OPTION_NONE:
    default:
        Tcl_Panic("StageAxisPixelOption called with invalid option");
        return TCL_ERROR;
    }
    if (Rbc_GetPixelsFromObj(graphPtr->interp, graphPtr->tkwin, objPtr, check, &value) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (option) {
    case AXIS_PIXEL_OPTION_BORDER_WIDTH:
        transactionPtr->borderWidth = value;
        break;
    case AXIS_PIXEL_OPTION_LINE_WIDTH:
        transactionPtr->lineWidth = value;
        break;
    case AXIS_PIXEL_OPTION_SCROLL_INCREMENT:
        transactionPtr->scrollIncrement = value;
        break;
    case AXIS_PIXEL_OPTION_NONE:
    default:
        Tcl_Panic("StageAxisPixelOption called with invalid option");
        return TCL_ERROR;
    }
    transactionPtr->stagedMask |= AXIS_PIXEL_OPTION_MASK(option);
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Prepare the axis pixel option family.
 *
 * Initializes fresh transaction storage; do not pass an existing owning transaction. Examines the
 * borrowed even-length option vector, identifies explicit options, and on first configuration parses
 * effective defaults only for options not explicitly overridden. Processes every explicit occurrence
 * in caller order, so a bad earlier value cannot be hidden by a later valid one. Returns a Tcl status
 * without committing live derived fields. Contains only scalar candidates; no allocation cleanup is
 * required on error.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] axisPtr Borrowed axis with candidate Tk fields and the original even-length option vector
 *   installed; derived fields are not committed here.
 * @param[out] transactionPtr Fresh caller-owned transaction storage, initialized here; must not already own
 *   resources. Success requires commit or cleanup.
 */
static int PrepareAxisPixelTransaction(Graph *graphPtr, Axis *axisPtr, AxisPixelTransaction *transactionPtr) {
    unsigned int explicitMask;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitMask = 0;
    assert((axisPtr->optionObjc & 1) == 0);
    /*
     * Determine which options were explicitly supplied.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        AxisPixelOption option;

        option = GetAxisPixelOption(axisPtr->optionObjv[i]);
        if (option != AXIS_PIXEL_OPTION_NONE) {
            explicitMask |= AXIS_PIXEL_OPTION_MASK(option);
        }
    }
    /*
     * During initial configuration, parse effective defaults
     * and option-database values that were not explicitly overridden.
     */
    if (!axisPtr->optionsConfigured) {
        if (!(explicitMask & AXIS_PIXEL_OPTION_MASK(AXIS_PIXEL_OPTION_BORDER_WIDTH)) &&
            (axisPtr->borderWidthObjPtr != NULL)) {
            if (StageAxisPixelOption(graphPtr, axisPtr->borderWidthObjPtr, AXIS_PIXEL_OPTION_BORDER_WIDTH,
                                     transactionPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        if (!(explicitMask & AXIS_PIXEL_OPTION_MASK(AXIS_PIXEL_OPTION_LINE_WIDTH)) &&
            (axisPtr->lineWidthObjPtr != NULL)) {
            if (StageAxisPixelOption(graphPtr, axisPtr->lineWidthObjPtr, AXIS_PIXEL_OPTION_LINE_WIDTH,
                                     transactionPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        if (!(explicitMask & AXIS_PIXEL_OPTION_MASK(AXIS_PIXEL_OPTION_SCROLL_INCREMENT)) &&
            (axisPtr->scrollIncrementObjPtr != NULL)) {
            if (StageAxisPixelOption(graphPtr, axisPtr->scrollIncrementObjPtr, AXIS_PIXEL_OPTION_SCROLL_INCREMENT,
                                     transactionPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
    }
    /*
     * Process explicit occurrences in their original order.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        AxisPixelOption option;

        option = GetAxisPixelOption(axisPtr->optionObjv[i]);
        if (option == AXIS_PIXEL_OPTION_NONE) {
            continue;
        }
        if (StageAxisPixelOption(graphPtr, axisPtr->optionObjv[i + 1], option, transactionPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Commit prepared axis pixel candidates.
 *
 * Copies only masked values into borderWidth, lineWidth and scrollUnits, then clears the mask.
 * Unstaged options are unchanged. Performs no parsing, callback evaluation or recoverable validation;
 * the caller must have prepared all option families first.
 *
 * @param[in,out] axisPtr Live axis receiving only staged derived fields; all option families must be validated
 *   before any commit.
 * @param[in,out] transactionPtr Successfully prepared candidate transaction; transferred pointers and staging
 *   flags are cleared without freeing the record.
 */
static void CommitAxisPixelTransaction(Axis *axisPtr, AxisPixelTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & AXIS_PIXEL_OPTION_MASK(AXIS_PIXEL_OPTION_BORDER_WIDTH)) {
        axisPtr->borderWidth = transactionPtr->borderWidth;
    }
    if (transactionPtr->stagedMask & AXIS_PIXEL_OPTION_MASK(AXIS_PIXEL_OPTION_LINE_WIDTH)) {
        axisPtr->lineWidth = transactionPtr->lineWidth;
    }
    if (transactionPtr->stagedMask & AXIS_PIXEL_OPTION_MASK(AXIS_PIXEL_OPTION_SCROLL_INCREMENT)) {
        axisPtr->scrollUnits = transactionPtr->scrollIncrement;
    }
    transactionPtr->stagedMask = 0;
}

/**
 * @ingroup axis_transactions
 * @brief Stage one display or scroll limit.
 *
 * Evaluates the candidate before writing the selected scalar and setting its mask bit. Failure leaves
 * the candidate unchanged. An invalid internal enum panics. Cross-field and logarithmic range
 * validation belong to preparation.
 *
 * @param[in,out] interp Borrowed interpreter receiving conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed finite Tcl expression, or NULL/empty for an automatic limit; evaluation may
 *   execute Tcl.
 * @param[in] option Valid non-NONE selector for this option family; invalid internal selectors panic.
 * @param[in,out] transactionPtr Initialized or zeroed caller-owned candidate transaction; staged fields are
 *   updated only after successful parsing.
 */
static int StageAxisLimit(Tcl_Interp *interp, Tcl_Obj *objPtr, AxisLimitOption option,
                          AxisLimitTransaction *transactionPtr) {
    double value;

    if (GetAxisLimitFromObj(interp, objPtr, &value) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (option) {
    case AXIS_LIMIT_OPTION_MIN:
        transactionPtr->reqMin = value;
        break;
    case AXIS_LIMIT_OPTION_MAX:
        transactionPtr->reqMax = value;
        break;
    case AXIS_LIMIT_OPTION_SCROLL_MIN:
        transactionPtr->scrollMin = value;
        break;
    case AXIS_LIMIT_OPTION_SCROLL_MAX:
        transactionPtr->scrollMax = value;
        break;
    case AXIS_LIMIT_OPTION_NONE:
    default:
        Tcl_Panic("StageAxisLimit called with invalid option");
        return TCL_ERROR;
    }
    transactionPtr->stagedMask |= AXIS_LIMIT_OPTION_MASK(option);
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Prepare the axis limit option family.
 *
 * Initializes fresh transaction storage; do not pass an existing owning transaction. Examines the
 * borrowed even-length option vector, identifies explicit options, and on first configuration parses
 * effective defaults only for options not explicitly overridden. Processes every explicit occurrence
 * in caller order, so a bad earlier value cannot be hidden by a later valid one. Returns a Tcl status
 * without committing live derived fields. Contains only scalar candidates; no allocation cleanup is
 * required on error. Starts with current live limits, checks final display and scroll ordering, and
 * requires defined limits to be positive for log scaling even when only logScale changed.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] axisPtr Borrowed axis with candidate Tk fields and the original even-length option vector
 *   installed; derived fields are not committed here.
 * @param[out] transactionPtr Fresh caller-owned transaction storage, initialized here; must not already own
 *   resources. Success requires commit or cleanup.
 */
static int PrepareAxisLimitTransaction(Graph *graphPtr, Axis *axisPtr, AxisLimitTransaction *transactionPtr) {
    unsigned int explicitMask;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    /*
     * Start with the current effective live values. Options not
     * involved in this configuration retain these candidates.
     */
    transactionPtr->reqMin = axisPtr->reqMin;
    transactionPtr->reqMax = axisPtr->reqMax;
    transactionPtr->scrollMin = axisPtr->scrollMin;
    transactionPtr->scrollMax = axisPtr->scrollMax;
    explicitMask = 0;
    assert((axisPtr->optionObjc & 1) == 0);
    /*
     * Determine which limit options were explicitly supplied.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        AxisLimitOption option;

        option = GetAxisLimitOption(axisPtr->optionObjv[i]);
        if (option != AXIS_LIMIT_OPTION_NONE) {
            explicitMask |= AXIS_LIMIT_OPTION_MASK(option);
        }
    }
    /*
     * During initial configuration, parse option-database
     * values that were not explicitly overridden.
     */
    if (!axisPtr->optionsConfigured) {
        if (!(explicitMask & AXIS_LIMIT_OPTION_MASK(AXIS_LIMIT_OPTION_MIN)) && (axisPtr->minObjPtr != NULL)) {
            if (StageAxisLimit(graphPtr->interp, axisPtr->minObjPtr, AXIS_LIMIT_OPTION_MIN, transactionPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        if (!(explicitMask & AXIS_LIMIT_OPTION_MASK(AXIS_LIMIT_OPTION_MAX)) && (axisPtr->maxObjPtr != NULL)) {
            if (StageAxisLimit(graphPtr->interp, axisPtr->maxObjPtr, AXIS_LIMIT_OPTION_MAX, transactionPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        if (!(explicitMask & AXIS_LIMIT_OPTION_MASK(AXIS_LIMIT_OPTION_SCROLL_MIN)) &&
            (axisPtr->scrollMinObjPtr != NULL)) {
            if (StageAxisLimit(graphPtr->interp, axisPtr->scrollMinObjPtr, AXIS_LIMIT_OPTION_SCROLL_MIN,
                               transactionPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
        if (!(explicitMask & AXIS_LIMIT_OPTION_MASK(AXIS_LIMIT_OPTION_SCROLL_MAX)) &&
            (axisPtr->scrollMaxObjPtr != NULL)) {
            if (StageAxisLimit(graphPtr->interp, axisPtr->scrollMaxObjPtr, AXIS_LIMIT_OPTION_SCROLL_MAX,
                               transactionPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
    }
    /*
     * Process explicit occurrences in caller order.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        AxisLimitOption option;

        option = GetAxisLimitOption(axisPtr->optionObjv[i]);
        if (option == AXIS_LIMIT_OPTION_NONE) {
            continue;
        }
        if (StageAxisLimit(graphPtr->interp, axisPtr->optionObjv[i + 1], option, transactionPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /*
     * Validate the effective requested display limits.
     */
    if (DEFINED(transactionPtr->reqMin) && DEFINED(transactionPtr->reqMax) &&
        (transactionPtr->reqMin >= transactionPtr->reqMax)) {
        Tcl_SetObjResult(graphPtr->interp,
                         Tcl_ObjPrintf("impossible limits "
                                       "(min %g >= max %g) for axis \"%s\"",
                                       transactionPtr->reqMin, transactionPtr->reqMax, axisPtr->name));
        return TCL_ERROR;
    }
    if (DEFINED(transactionPtr->scrollMin) && DEFINED(transactionPtr->scrollMax) &&
        (transactionPtr->scrollMin >= transactionPtr->scrollMax)) {
        Tcl_SetObjResult(graphPtr->interp,
                         Tcl_ObjPrintf("impossible scroll limits "
                                       "(scrollmin %g >= scrollmax %g) "
                                       "for axis \"%s\"",
                                       transactionPtr->scrollMin, transactionPtr->scrollMax, axisPtr->name));
        return TCL_ERROR;
    }
    if (axisPtr->logScale) {
        if (DEFINED(transactionPtr->reqMin) && (transactionPtr->reqMin <= 0.0)) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("-min must be positive for "
                                                             "logscale axis \"%s\"",
                                                             axisPtr->name));
            return TCL_ERROR;
        }
        if (DEFINED(transactionPtr->reqMax) && (transactionPtr->reqMax <= 0.0)) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("-max must be positive for "
                                                             "logscale axis \"%s\"",
                                                             axisPtr->name));
            return TCL_ERROR;
        }
        if (DEFINED(transactionPtr->scrollMin) && (transactionPtr->scrollMin <= 0.0)) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("-scrollmin must be positive for "
                                                             "logscale axis \"%s\"",
                                                             axisPtr->name));
            return TCL_ERROR;
        }
        if (DEFINED(transactionPtr->scrollMax) && (transactionPtr->scrollMax <= 0.0)) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("-scrollmax must be positive for "
                                                             "logscale axis \"%s\"",
                                                             axisPtr->name));
            return TCL_ERROR;
        }
    }
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Commit prepared axis limit candidates.
 *
 * Copies only masked requested/scroll limits into the live axis, then clears the mask. Validated but
 * unstaged values remain untouched. Unstaged options are unchanged. Performs no parsing, callback
 * evaluation or recoverable validation; the caller must have prepared all option families first.
 *
 * @param[in,out] axisPtr Live axis receiving only staged derived fields; all option families must be validated
 *   before any commit.
 * @param[in,out] transactionPtr Successfully prepared candidate transaction; transferred pointers and staging
 *   flags are cleared without freeing the record.
 */
static void CommitAxisLimitTransaction(Axis *axisPtr, AxisLimitTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & AXIS_LIMIT_OPTION_MASK(AXIS_LIMIT_OPTION_MIN)) {
        axisPtr->reqMin = transactionPtr->reqMin;
    }
    if (transactionPtr->stagedMask & AXIS_LIMIT_OPTION_MASK(AXIS_LIMIT_OPTION_MAX)) {
        axisPtr->reqMax = transactionPtr->reqMax;
    }
    if (transactionPtr->stagedMask & AXIS_LIMIT_OPTION_MASK(AXIS_LIMIT_OPTION_SCROLL_MIN)) {
        axisPtr->scrollMin = transactionPtr->scrollMin;
    }
    if (transactionPtr->stagedMask & AXIS_LIMIT_OPTION_MASK(AXIS_LIMIT_OPTION_SCROLL_MAX)) {
        axisPtr->scrollMax = transactionPtr->scrollMax;
    }
    transactionPtr->stagedMask = 0;
}

/**
 * @ingroup axis_presentation
 * @brief Release every label in a chain and empty its links.
 *
 * Frees the owned TickLabel allocations, then resets the chain. The chain itself remains owned by the
 * axis and reusable. Requires a valid chain.
 */
static void FreeLabels(Rbc_Chain *chainPtr) {
    Rbc_ChainLink *linkPtr;
    TickLabel *labelPtr;

    for (linkPtr = Rbc_ChainFirstLink(chainPtr); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        labelPtr = Rbc_ChainGetValue(linkPtr);
        ckfree((char *)labelPtr);
    }
    Rbc_ChainReset(chainPtr);
}

/**
 * @ingroup axis_presentation
 * @brief Allocate a label with its copied string inline.
 *
 * Checks allocation-size overflow and uses the fatal allocator. Initializes anchor coordinates to
 * DBL_MAX to mark an unmapped label; dimensions are filled during geometry measurement. The caller
 * owns the allocation and frees it with ckfree.
 */
static TickLabel *NewTickLabel(const char *string) {
    TickLabel *labelPtr;
    size_t length;

    length = strlen(string);
    if (length > (SIZE_MAX - sizeof(TickLabel))) {
        Tcl_Panic("NewTickLabel: label size overflow");
    }
    labelPtr = (TickLabel *)ckalloc(sizeof(TickLabel) + length);
    memcpy(labelPtr->string, string, length + 1);
    labelPtr->anchorPos.x = DBL_MAX;
    labelPtr->anchorPos.y = DBL_MAX;
    return labelPtr;
}

/**
 * @ingroup axis_presentation
 * @brief Format and allocate one major-tick label.
 *
 * Produces the legacy numeric label first: exponent notation for logarithmic axes, NUMDIGITS
 * significant digits otherwise. A nonempty formatter prefix receives widget path and that label.
 * Preserves interpreter state, reports callback errors in the background and falls back to the numeric
 * label. After evaluation checks graph validity without dereferencing the axis; invalidation frees the
 * candidate and returns NULL. Callers must stop traversing cached axis/layout pointers on NULL.
 * Successful labels are caller-owned.
 */
TickLabel *Rbc_AllocAxisTickLabel(Graph *graphPtr, Axis *axisPtr, double value) {
    char defaultLabel[TICK_LABEL_SIZE + 1];

    /*
     * Generate the standard label first.  Keep this exact legacy
     * representation as the second argument passed to -command.
     */
    if (axisPtr->logScale) {
        snprintf(defaultLabel, sizeof(defaultLabel), "1E%d", ROUND(value));
    } else {
        snprintf(defaultLabel, sizeof(defaultLabel), "%.*g", NUMDIGITS, value);
    }
    if (!axisPtr->deletePending && axisPtr->formatCmdObjPtr != NULL) {
        Tcl_Interp *interp;
        Tcl_Obj *cmdObj;
        Tcl_Obj **objv;
        Tcl_Size prefixObjc;
        Tcl_Size objc;
        int result;
        Tcl_InterpState state;
        TickLabel *labelPtr;

        interp = graphPtr->interp;
        state = Tcl_SaveInterpState(interp, TCL_OK);
        /*
         * An empty prefix means standard formatting.
         */
        result = Tcl_ListObjLength(interp, axisPtr->formatCmdObjPtr, &prefixObjc);
        if (result != TCL_OK) {
            /*
             * ConfigureAxis() normally makes this impossible, but
             * preserve the old background-error behaviour if it does
             * occur during drawing.
             */
            Tcl_BackgroundException(interp, result);
            Tcl_RestoreInterpState(interp, state);
            return NewTickLabel(defaultLabel);
        }
        if (prefixObjc == 0) {
            Tcl_RestoreInterpState(interp, state);
            return NewTickLabel(defaultLabel);
        }
        /*
         * Duplicate the prefix so the Tk-managed option object is
         * never modified.
         */
        cmdObj = Tcl_DuplicateObj(axisPtr->formatCmdObjPtr);
        Tcl_IncrRefCount(cmdObj);
        result = Tcl_ListObjAppendElement(interp, cmdObj, Tcl_NewStringObj(Tk_PathName(graphPtr->tkwin), -1));
        if (result == TCL_OK) {
            result = Tcl_ListObjAppendElement(interp, cmdObj, Tcl_NewStringObj(defaultLabel, -1));
        }
        if (result == TCL_OK) {
            result = Tcl_ListObjGetElements(interp, cmdObj, &objc, &objv);
        }
        if (result == TCL_OK) {
            Tcl_ResetResult(interp);
            graphPtr->axisFormatActive = TRUE;
            result = Tcl_EvalObjv(interp, objc, objv, 0);
            graphPtr->axisFormatActive = FALSE;
        }
        Tcl_DecrRefCount(cmdObj);
        if (result != TCL_OK) {
            Tcl_BackgroundException(interp, result);
            labelPtr = NewTickLabel(defaultLabel);
        } else {
            labelPtr = NewTickLabel(Tcl_GetString(Tcl_GetObjResult(interp)));
        }
        Tcl_RestoreInterpState(interp, state);
        /* Do not dereference axisPtr after a callback: it may have been deleted,
         * or its font, tick arrays and owning track may have been replaced. */
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            Rbc_EventuallyRedrawGraph(graphPtr);
            ckfree(labelPtr);
            return NULL;
        }
        return labelPtr;
    }
    return NewTickLabel(defaultLabel);
}

/**
 * @ingroup axis_transactions
 * @brief Release one explicit tick allocation.
 *
 * Accepts NULL. Frees the allocation without modifying the caller pointer.
 *
 * @param[in] ticksPtr Owned tick allocation to release, or NULL; the caller pointer is not cleared.
 */
static void FreeAxisTicks(Ticks *ticksPtr) {
    if (ticksPtr != NULL) {
        ckfree((char *)ticksPtr);
    }
}

/**
 * @ingroup axis_transactions
 * @brief Classify an axis tick option.
 *
 * Resolves the canonical name using axisOptionSpecs and returns the corresponding enum for major and
 * minor tick lists. Returns the NONE enum for unrelated or unresolved names; performs no allocation.
 *
 * @param[in] objPtr Borrowed option-name object resolved through the axis option table, including abbreviations
 *   and synonyms.
 */
static AxisTickOption GetAxisTickOption(Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, axisOptionSpecs);
    if (name == NULL) {
        return AXIS_TICK_OPTION_NONE;
    }
    if (strcmp(name, "-majorticks") == 0) {
        return AXIS_TICK_OPTION_MAJOR;
    }
    if (strcmp(name, "-minorticks") == 0) {
        return AXIS_TICK_OPTION_MINOR;
    }
    return AXIS_TICK_OPTION_NONE;
}

/**
 * @ingroup axis_transactions
 * @brief Recognize the -loose option.
 *
 * Delegates canonical-name matching to IsAxisOption after Tk has rejected unknown or ambiguous
 * options.
 *
 * @param[in] objPtr Borrowed option-name object resolved through the axis option table, including abbreviations
 *   and synonyms.
 */
static int IsAxisLooseOption(Tcl_Obj *objPtr) { return IsAxisOption(objPtr, "-loose"); }

/**
 * @ingroup axis_transactions
 * @brief Parse one or two tick-range policies.
 *
 * Accepts Tcl booleans or the exact string always. One entry applies to both ends; two entries specify
 * minimum then maximum. Returns TCL_ERROR for invalid list length or entries without assigning the
 * output pair. Allocates no retained storage.
 *
 * @param[in,out] interp Borrowed interpreter receiving conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed list of one or two booleans or exact always keywords; NULL is not accepted.
 * @param[out] looseMinPtr Required minimum-end policy output, assigned only after the complete input validates.
 * @param[out] looseMaxPtr Required separate maximum-end policy output, assigned only on success.
 */
static int GetAxisLooseFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, int *looseMinPtr, int *looseMaxPtr) {
    Tcl_Obj **valueObjv;
    Tcl_Size valueObjc;
    int values[2];
    Tcl_Size i;

    if (Tcl_ListObjGetElements(interp, objPtr, &valueObjc, &valueObjv) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((valueObjc < 1) || (valueObjc > 2)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong # elements in loose value \"%s\"", Tcl_GetString(objPtr)));
        return TCL_ERROR;
    }
    for (i = 0; i < valueObjc; i++) {
        const char *string;

        string = Tcl_GetString(valueObjv[i]);
        if (strcmp(string, "always") == 0) {
            values[i] = TICK_RANGE_ALWAYS_LOOSE;
        } else {
            int boolean;

            if (Tcl_GetBooleanFromObj(interp, valueObjv[i], &boolean) != TCL_OK) {
                return TCL_ERROR;
            }
            values[i] = boolean ? TICK_RANGE_LOOSE : TICK_RANGE_TIGHT;
        }
    }
    *looseMinPtr = values[0];
    *looseMaxPtr = values[0];
    if (valueObjc == 2) {
        *looseMaxPtr = values[1];
    }
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Stage minimum and maximum tick-range policies.
 *
 * Parses the complete one/two-entry value before updating the scalar pair and setting staged. Failure
 * preserves earlier candidates; no owned allocation is created.
 *
 * @param[in,out] interp Borrowed interpreter receiving conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed one- or two-entry loose-range policy list; NULL is not accepted.
 * @param[in,out] transactionPtr Initialized or zeroed caller-owned candidate transaction; staged fields are
 *   updated only after successful parsing.
 */
static int StageAxisLoose(Tcl_Interp *interp, Tcl_Obj *objPtr, AxisLooseTransaction *transactionPtr) {
    int looseMin;
    int looseMax;

    if (GetAxisLooseFromObj(interp, objPtr, &looseMin, &looseMax) != TCL_OK) {
        return TCL_ERROR;
    }
    transactionPtr->looseMin = looseMin;
    transactionPtr->looseMax = looseMax;
    transactionPtr->staged = TRUE;
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Prepare the axis loose option family.
 *
 * Initializes fresh transaction storage; do not pass an existing owning transaction. Examines the
 * borrowed even-length option vector, identifies explicit options, and on first configuration parses
 * effective defaults only for options not explicitly overridden. Processes every explicit occurrence
 * in caller order, so a bad earlier value cannot be hidden by a later valid one. Returns a Tcl status
 * without committing live derived fields. Contains only scalar candidates; no allocation cleanup is
 * required on error.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] axisPtr Borrowed axis with candidate Tk fields and the original even-length option vector
 *   installed; derived fields are not committed here.
 * @param[out] transactionPtr Fresh caller-owned transaction storage, initialized here; must not already own
 *   resources. Success requires commit or cleanup.
 */
static int PrepareAxisLooseTransaction(Graph *graphPtr, Axis *axisPtr, AxisLooseTransaction *transactionPtr) {
    int explicitlySpecified;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitlySpecified = FALSE;
    assert((axisPtr->optionObjc & 1) == 0);
    /*
     * Determine whether -loose was supplied explicitly.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        if (IsAxisLooseOption(axisPtr->optionObjv[i])) {
            explicitlySpecified = TRUE;
        }
    }
    /*
     * During initial configuration, parse the effective default
     * or option-database value unless the caller explicitly overrides
     * it.
     */
    if (!axisPtr->optionsConfigured && !explicitlySpecified && (axisPtr->looseObjPtr != NULL)) {
        if (StageAxisLoose(graphPtr->interp, axisPtr->looseObjPtr, transactionPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    /*
     * Process explicit occurrences in caller order.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        if (IsAxisLooseOption(axisPtr->optionObjv[i])) {
            if (StageAxisLoose(graphPtr->interp, axisPtr->optionObjv[i + 1], transactionPtr) != TCL_OK) {
                return TCL_ERROR;
            }
        }
    }
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Commit prepared axis loose candidates.
 *
 * Copies the staged minimum/maximum policies and clears staged. Unstaged options are unchanged.
 * Performs no parsing, callback evaluation or recoverable validation; the caller must have prepared
 * all option families first.
 *
 * @param[in,out] axisPtr Live axis receiving only staged derived fields; all option families must be validated
 *   before any commit.
 * @param[in,out] transactionPtr Successfully prepared candidate transaction; transferred pointers and staging
 *   flags are cleared without freeing the record.
 */
static void CommitAxisLooseTransaction(Axis *axisPtr, AxisLooseTransaction *transactionPtr) {
    if (!transactionPtr->staged) {
        return;
    }
    axisPtr->looseMin = transactionPtr->looseMin;
    axisPtr->looseMax = transactionPtr->looseMax;
    transactionPtr->staged = FALSE;
}

/**
 * @ingroup axis_transactions
 * @brief Compute the allocation size for a variable-length tick array.
 *
 * Rejects negative counts and size_t overflow using offsetof(Ticks, values). Writes the byte count
 * only on TCL_OK. Does not impose MAXTICKS or report an interpreter error; the caller applies those
 * policies.
 *
 * @param[in] nTicks Requested number of double entries; negative or unrepresentable counts fail.
 * @param[out] sizePtr Required byte-size output, including the Ticks header; assigned only on TCL_OK.
 */
static int GetTicksByteCount(Tcl_Size nTicks, size_t *sizePtr) {
    size_t headerSize;

    if (nTicks < 0) {
        return TCL_ERROR;
    }
    headerSize = offsetof(Ticks, values);
    if ((uintmax_t)nTicks > (uintmax_t)((SIZE_MAX - headerSize) / sizeof(double))) {
        return TCL_ERROR;
    }
    *sizePtr = headerSize + (size_t)nTicks * sizeof(double);
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Allocate explicit ticks from a list of finite expressions.
 *
 * NULL or an empty list returns TCL_OK with a NULL output. Rejects more than MAXTICKS entries,
 * overflow, allocation failure and invalid/nonfinite expressions. Evaluates entries in input order
 * with Tcl_ExprDoubleObj; no sorting or deduplication is performed. Frees partial storage on failure
 * and leaves the output NULL. Successful nonempty storage belongs to the caller. Expression evaluation
 * can execute Tcl substitutions; this helper supplies no graph lifetime guard.
 *
 * @param[in,out] interp Borrowed interpreter receiving conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed list of finite Tcl expressions; NULL or empty selects automatic ticks. Evaluation
 *   may execute Tcl.
 * @param[out] ticksPtrPtr Required output initialized to NULL; success transfers any nonempty allocation,
 *   released by FreeAxisTicks.
 */
static int GetAxisTicksFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Ticks **ticksPtrPtr) {
    Tcl_Obj **exprObjv;
    Tcl_Size nExprs;
    Tcl_Size i;
    Ticks *ticksPtr;
    size_t size;

    *ticksPtrPtr = NULL;
    if (objPtr == NULL) {
        return TCL_OK;
    }
    if (Tcl_ListObjGetElements(interp, objPtr, &nExprs, &exprObjv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (nExprs == 0) {
        return TCL_OK;
    }
    /*
     * GetAxisGeometry() already treats more than MAXTICKS as invalid.
     * Reject such a configuration here instead of allocating it and
     * aborting later during layout.
     */
    if (nExprs > (Tcl_Size)MAXTICKS) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("too many axis ticks: maximum is %d", MAXTICKS));
        return TCL_ERROR;
    }
    if (GetTicksByteCount(nExprs, &size) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("axis tick array is too large", -1));
        return TCL_ERROR;
    }
    ticksPtr = Tcl_AttemptAlloc(size);
    if (ticksPtr == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate axis tick array", -1));
        return TCL_ERROR;
    }
    for (i = 0; i < nExprs; i++) {
        if (Tcl_ExprDoubleObj(interp, exprObjv[i], &ticksPtr->values[i]) != TCL_OK) {
            ckfree(ticksPtr);
            return TCL_ERROR;
        }
        if (!FINITE(ticksPtr->values[i])) {
            ckfree(ticksPtr);
            Tcl_SetObjResult(interp, Tcl_NewStringObj("axis tick values must be finite", -1));
            return TCL_ERROR;
        }
    }
    ticksPtr->nTicks = nExprs;
    *ticksPtrPtr = ticksPtr;
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Stage a major or minor explicit tick allocation.
 *
 * Parses and allocates the replacement before releasing an earlier candidate for that option. NULL is
 * a valid staged candidate selecting automatic ticks. Success sets the mask; failure retains previous
 * candidates. Invalid internal enums free the new allocation and panic.
 *
 * @param[in,out] interp Borrowed interpreter receiving conversion diagnostics; see @ref rbc_interp_argument.
 * @param[in] objPtr Borrowed list of finite Tcl expressions; NULL or empty stages automatic ticks. Evaluation
 *   may execute Tcl.
 * @param[in] option Valid non-NONE selector for this option family; invalid internal selectors panic.
 * @param[in,out] transactionPtr Initialized or zeroed caller-owned candidate transaction; staged fields are
 *   updated only after successful parsing.
 */
static int StageAxisTicks(Tcl_Interp *interp, Tcl_Obj *objPtr, AxisTickOption option,
                          AxisTickTransaction *transactionPtr) {
    Ticks *newTicksPtr;
    Ticks **candidatePtrPtr;
    unsigned int mask;

    newTicksPtr = NULL;
    if (GetAxisTicksFromObj(interp, objPtr, &newTicksPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (option) {
    case AXIS_TICK_OPTION_MAJOR:
        candidatePtrPtr = &transactionPtr->majorTicksPtr;
        break;
    case AXIS_TICK_OPTION_MINOR:
        candidatePtrPtr = &transactionPtr->minorTicksPtr;
        break;
    case AXIS_TICK_OPTION_NONE:
    default:
        FreeAxisTicks(newTicksPtr);
        Tcl_Panic("StageAxisTicks called with invalid option");
        return TCL_ERROR;
    }
    mask = AXIS_TICK_OPTION_MASK(option);
    /*
     * Do not discard an earlier candidate until the replacement has
     * parsed successfully.
     */
    if (transactionPtr->stagedMask & mask) {
        FreeAxisTicks(*candidatePtrPtr);
    }
    *candidatePtrPtr = newTicksPtr;
    transactionPtr->stagedMask |= mask;
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Discard an axis tick transaction.
 *
 * Releases owned major and minor tick allocations and zeroes the complete transaction. Requires
 * initialized or zeroed storage. Safe after commit because transferred ownership has been cleared;
 * does not alter live axis fields.
 *
 * @param[in,out] transactionPtr Initialized or zeroed transaction whose candidate resources are released and
 *   complete contents reset; record is retained.
 */
static void FreeAxisTickTransaction(AxisTickTransaction *transactionPtr) {
    FreeAxisTicks(transactionPtr->majorTicksPtr);
    FreeAxisTicks(transactionPtr->minorTicksPtr);
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @ingroup axis_transactions
 * @brief Prepare the axis tick option family.
 *
 * Initializes fresh transaction storage; do not pass an existing owning transaction. Examines the
 * borrowed even-length option vector, identifies explicit options, and on first configuration parses
 * effective defaults only for options not explicitly overridden. Processes every explicit occurrence
 * in caller order, so a bad earlier value cannot be hidden by a later valid one. Returns a Tcl status
 * without committing live derived fields. On error releases all candidates owned by this transaction.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] axisPtr Borrowed axis with candidate Tk fields and the original even-length option vector
 *   installed; derived fields are not committed here.
 * @param[out] transactionPtr Fresh caller-owned transaction storage, initialized here; must not already own
 *   resources. Success requires commit or cleanup.
 */
static int PrepareAxisTickTransaction(Graph *graphPtr, Axis *axisPtr, AxisTickTransaction *transactionPtr) {
    unsigned int explicitMask;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitMask = 0;
    assert((axisPtr->optionObjc & 1) == 0);
    /*
     * Determine which tick options were explicitly supplied.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        AxisTickOption option;

        option = GetAxisTickOption(axisPtr->optionObjv[i]);
        if (option != AXIS_TICK_OPTION_NONE) {
            explicitMask |= AXIS_TICK_OPTION_MASK(option);
        }
    }
    /*
     * On initial configuration, parse effective option-database
     * values that were not explicitly overridden.
     */
    if (!axisPtr->optionsConfigured) {
        if (!(explicitMask & AXIS_TICK_OPTION_MASK(AXIS_TICK_OPTION_MAJOR)) && (axisPtr->majorTicksObjPtr != NULL)) {
            if (StageAxisTicks(graphPtr->interp, axisPtr->majorTicksObjPtr, AXIS_TICK_OPTION_MAJOR, transactionPtr) !=
                TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & AXIS_TICK_OPTION_MASK(AXIS_TICK_OPTION_MINOR)) && (axisPtr->minorTicksObjPtr != NULL)) {
            if (StageAxisTicks(graphPtr->interp, axisPtr->minorTicksObjPtr, AXIS_TICK_OPTION_MINOR, transactionPtr) !=
                TCL_OK) {
                goto error;
            }
        }
    }
    /*
     * Process explicit repeated occurrences in caller order.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        AxisTickOption option;

        option = GetAxisTickOption(axisPtr->optionObjv[i]);
        if (option == AXIS_TICK_OPTION_NONE) {
            continue;
        }
        if (StageAxisTicks(graphPtr->interp, axisPtr->optionObjv[i + 1], option, transactionPtr) != TCL_OK) {
            goto error;
        }
    }
    return TCL_OK;

error:
    FreeAxisTickTransaction(transactionPtr);
    return TCL_ERROR;
}

/**
 * @ingroup axis_transactions
 * @brief Commit prepared axis tick candidates.
 *
 * Transfers masked candidates into t1Ptr/t2Ptr, updates AXIS_CONFIG_MAJOR/MINOR according to non-NULL
 * candidates, releases old arrays and clears transferred pointers and mask. A staged NULL restores
 * automatic tick generation. Unstaged options are unchanged. Performs no parsing, callback evaluation
 * or recoverable validation; the caller must have prepared all option families first.
 *
 * @param[in,out] axisPtr Live axis receiving only staged derived fields; all option families must be validated
 *   before any commit.
 * @param[in,out] transactionPtr Successfully prepared candidate transaction; transferred pointers and staging
 *   flags are cleared without freeing the record.
 */
static void CommitAxisTickTransaction(Axis *axisPtr, AxisTickTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & AXIS_TICK_OPTION_MASK(AXIS_TICK_OPTION_MAJOR)) {
        Ticks *oldTicksPtr;

        oldTicksPtr = axisPtr->t1Ptr;
        axisPtr->t1Ptr = transactionPtr->majorTicksPtr;
        transactionPtr->majorTicksPtr = NULL;
        axisPtr->flags &= ~AXIS_CONFIG_MAJOR;
        if (axisPtr->t1Ptr != NULL) {
            axisPtr->flags |= AXIS_CONFIG_MAJOR;
        }
        FreeAxisTicks(oldTicksPtr);
    }

    if (transactionPtr->stagedMask & AXIS_TICK_OPTION_MASK(AXIS_TICK_OPTION_MINOR)) {
        Ticks *oldTicksPtr;

        oldTicksPtr = axisPtr->t2Ptr;
        axisPtr->t2Ptr = transactionPtr->minorTicksPtr;
        transactionPtr->minorTicksPtr = NULL;
        axisPtr->flags &= ~AXIS_CONFIG_MINOR;
        if (axisPtr->t2Ptr != NULL) {
            axisPtr->flags |= AXIS_CONFIG_MINOR;
        }
        FreeAxisTicks(oldTicksPtr);
    }
    transactionPtr->stagedMask = 0;
}

/**
 * @ingroup axis_transactions
 * @brief Release an owned shadow colour and reset its fields.
 *
 * The Shadow record must be valid, with an owned Tk colour or NULL. Releases the colour reference,
 * then clears colour and offset. Does not free the Shadow record itself.
 *
 * @param[in,out] shadowPtr Caller-owned Shadow record containing an owned Tk color or NULL; color and offset
 *   are cleared.
 */
static void FreeAxisShadow(Shadow *shadowPtr) {
    if (shadowPtr->color != NULL) {
        Tk_FreeColor(shadowPtr->color);
    }
    shadowPtr->color = NULL;
    shadowPtr->offset = 0;
}

/**
 * @ingroup axis_transactions
 * @brief Classify an axis shadow option.
 *
 * Resolves the canonical name using axisOptionSpecs and returns the corresponding enum for limits,
 * tick and title shadows. Returns the NONE enum for unrelated or unresolved names; performs no
 * allocation.
 *
 * @param[in] objPtr Borrowed option-name object resolved through the axis option table, including abbreviations
 *   and synonyms.
 */
static AxisShadowOption GetAxisShadowOption(Tcl_Obj *objPtr) {
    const char *name;

    name = Rbc_GetCanonicalOptionName(objPtr, axisOptionSpecs);
    if (name == NULL) {
        return AXIS_SHADOW_OPTION_NONE;
    }
    if (strcmp(name, "-limitsshadow") == 0) {
        return AXIS_SHADOW_OPTION_LIMITS;
    }
    if (strcmp(name, "-tickshadow") == 0) {
        return AXIS_SHADOW_OPTION_TICK;
    }
    if (strcmp(name, "-titleshadow") == 0) {
        return AXIS_SHADOW_OPTION_TITLE;
    }
    return AXIS_SHADOW_OPTION_NONE;
}

/**
 * @ingroup axis_transactions
 * @brief Stage one owned shadow colour and offset.
 *
 * Acquires and validates a new shadow before releasing an earlier candidate for the same option.
 * Success transfers the new colour reference into the transaction and sets its mask. Invalid internal
 * enums release the new colour and panic.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] objPtr Borrowed shadow color/offset option value; successful parsing acquires a separate color
 *   reference.
 * @param[in] option Valid non-NONE selector for this option family; invalid internal selectors panic.
 * @param[in,out] transactionPtr Initialized or zeroed caller-owned candidate transaction; staged fields are
 *   updated only after successful parsing.
 */
static int StageAxisShadow(Graph *graphPtr, Tcl_Obj *objPtr, AxisShadowOption option,
                           AxisShadowTransaction *transactionPtr) {
    Shadow newShadow;
    Shadow *candidatePtr;
    unsigned int mask;

    newShadow.color = NULL;
    newShadow.offset = 0;
    /*
     * Rbc_GetShadowFromObj acquires the colour and validates the
     * optional non-negative offset without modifying live state.
     */
    if (Rbc_GetShadowFromObj(graphPtr->interp, graphPtr->tkwin, objPtr, &newShadow) != TCL_OK) {
        return TCL_ERROR;
    }
    switch (option) {
    case AXIS_SHADOW_OPTION_LIMITS:
        candidatePtr = &transactionPtr->limitsShadow;
        break;
    case AXIS_SHADOW_OPTION_TICK:
        candidatePtr = &transactionPtr->tickShadow;
        break;
    case AXIS_SHADOW_OPTION_TITLE:
        candidatePtr = &transactionPtr->titleShadow;
        break;
    case AXIS_SHADOW_OPTION_NONE:
    default:
        FreeAxisShadow(&newShadow);
        Tcl_Panic("StageAxisShadow called with invalid option");
        return TCL_ERROR;
    }
    mask = AXIS_SHADOW_OPTION_MASK(option);
    /*
     * Parse and acquire the replacement before releasing an earlier
     * staged candidate for the same option.
     */
    if (transactionPtr->stagedMask & mask) {
        FreeAxisShadow(candidatePtr);
    }
    *candidatePtr = newShadow;
    transactionPtr->stagedMask |= mask;
    return TCL_OK;
}

/**
 * @ingroup axis_transactions
 * @brief Discard an axis shadow transaction.
 *
 * Releases owned three shadow colour references and zeroes the complete transaction. Requires
 * initialized or zeroed storage. Safe after commit because transferred ownership has been cleared;
 * does not alter live axis fields.
 *
 * @param[in,out] transactionPtr Initialized or zeroed transaction whose candidate resources are released and
 *   complete contents reset; record is retained.
 */
static void FreeAxisShadowTransaction(AxisShadowTransaction *transactionPtr) {
    FreeAxisShadow(&transactionPtr->limitsShadow);
    FreeAxisShadow(&transactionPtr->tickShadow);
    FreeAxisShadow(&transactionPtr->titleShadow);
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/**
 * @ingroup axis_transactions
 * @brief Prepare the axis shadow option family.
 *
 * Initializes fresh transaction storage; do not pass an existing owning transaction. Examines the
 * borrowed even-length option vector, identifies explicit options, and on first configuration parses
 * effective defaults only for options not explicitly overridden. Processes every explicit occurrence
 * in caller order, so a bad earlier value cannot be hidden by a later valid one. Returns a Tcl status
 * without committing live derived fields. On error releases all candidates owned by this transaction.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] axisPtr Borrowed axis with candidate Tk fields and the original even-length option vector
 *   installed; derived fields are not committed here.
 * @param[out] transactionPtr Fresh caller-owned transaction storage, initialized here; must not already own
 *   resources. Success requires commit or cleanup.
 */
static int PrepareAxisShadowTransaction(Graph *graphPtr, Axis *axisPtr, AxisShadowTransaction *transactionPtr) {
    unsigned int explicitMask;
    Tcl_Size i;

    memset(transactionPtr, 0, sizeof(*transactionPtr));
    explicitMask = 0;
    assert((axisPtr->optionObjc & 1) == 0);
    /*
     * Determine which shadow options were explicitly supplied.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        AxisShadowOption option;

        option = GetAxisShadowOption(axisPtr->optionObjv[i]);
        if (option != AXIS_SHADOW_OPTION_NONE) {
            explicitMask |= AXIS_SHADOW_OPTION_MASK(option);
        }
    }
    /*
     * During initial configuration, parse effective option
     * database values that were not explicitly overridden.
     */
    if (!axisPtr->optionsConfigured) {
        if (!(explicitMask & AXIS_SHADOW_OPTION_MASK(AXIS_SHADOW_OPTION_LIMITS)) &&
            (axisPtr->limitsShadowObjPtr != NULL)) {
            if (StageAxisShadow(graphPtr, axisPtr->limitsShadowObjPtr, AXIS_SHADOW_OPTION_LIMITS, transactionPtr) !=
                TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & AXIS_SHADOW_OPTION_MASK(AXIS_SHADOW_OPTION_TICK)) && (axisPtr->tickShadowObjPtr != NULL)) {
            if (StageAxisShadow(graphPtr, axisPtr->tickShadowObjPtr, AXIS_SHADOW_OPTION_TICK, transactionPtr) !=
                TCL_OK) {
                goto error;
            }
        }
        if (!(explicitMask & AXIS_SHADOW_OPTION_MASK(AXIS_SHADOW_OPTION_TITLE)) &&
            (axisPtr->titleShadowObjPtr != NULL)) {
            if (StageAxisShadow(graphPtr, axisPtr->titleShadowObjPtr, AXIS_SHADOW_OPTION_TITLE, transactionPtr) !=
                TCL_OK) {
                goto error;
            }
        }
    }
    /*
     * Process explicit repeated occurrences in caller order. An
     * invalid earlier value is therefore not hidden by a later valid
     * occurrence.
     */
    for (i = 0; i < axisPtr->optionObjc; i += 2) {
        AxisShadowOption option;

        option = GetAxisShadowOption(axisPtr->optionObjv[i]);
        if (option == AXIS_SHADOW_OPTION_NONE) {
            continue;
        }
        if (StageAxisShadow(graphPtr, axisPtr->optionObjv[i + 1], option, transactionPtr) != TCL_OK) {
            goto error;
        }
    }
    return TCL_OK;

error:
    FreeAxisShadowTransaction(transactionPtr);
    return TCL_ERROR;
}

/**
 * @ingroup axis_transactions
 * @brief Commit prepared axis shadow candidates.
 *
 * Transfers masked colours and offsets into the three text styles, clears transferred candidate
 * fields, releases old colours and clears the mask. Unstaged options are unchanged. Performs no
 * parsing, callback evaluation or recoverable validation; the caller must have prepared all option
 * families first.
 *
 * @param[in,out] axisPtr Live axis receiving only staged derived fields; all option families must be validated
 *   before any commit.
 * @param[in,out] transactionPtr Successfully prepared candidate transaction; transferred pointers and staging
 *   flags are cleared without freeing the record.
 */
static void CommitAxisShadowTransaction(Axis *axisPtr, AxisShadowTransaction *transactionPtr) {
    if (transactionPtr->stagedMask & AXIS_SHADOW_OPTION_MASK(AXIS_SHADOW_OPTION_LIMITS)) {
        Shadow oldShadow;

        oldShadow = axisPtr->limitsTextStyle.shadow;
        axisPtr->limitsTextStyle.shadow = transactionPtr->limitsShadow;
        transactionPtr->limitsShadow.color = NULL;
        transactionPtr->limitsShadow.offset = 0;
        FreeAxisShadow(&oldShadow);
    }
    if (transactionPtr->stagedMask & AXIS_SHADOW_OPTION_MASK(AXIS_SHADOW_OPTION_TICK)) {
        Shadow oldShadow;

        oldShadow = axisPtr->tickTextStyle.shadow;
        axisPtr->tickTextStyle.shadow = transactionPtr->tickShadow;
        transactionPtr->tickShadow.color = NULL;
        transactionPtr->tickShadow.offset = 0;
        FreeAxisShadow(&oldShadow);
    }
    if (transactionPtr->stagedMask & AXIS_SHADOW_OPTION_MASK(AXIS_SHADOW_OPTION_TITLE)) {
        Shadow oldShadow;

        oldShadow = axisPtr->titleTextStyle.shadow;
        axisPtr->titleTextStyle.shadow = transactionPtr->titleShadow;
        transactionPtr->titleShadow.color = NULL;
        transactionPtr->titleShadow.offset = 0;
        FreeAxisShadow(&oldShadow);
    }
    transactionPtr->stagedMask = 0;
}

/**
 * @ingroup axis_ranges
 * @brief Invert horizontal mapping into a logical data value.
 * @param[in] graphPtr Borrowed graph with current layout and mapping scales.
 * @param[in] axisPtr Borrowed axis with a prepared axisRange and live owning track, if any.
 * @param[in] x Horizontal widget coordinate; off-plot positions are extrapolated.
 *
 * Normalizes against the full owning-track world extent or graph horizontal scale, reverses descending
 * direction and interpolates axis space. Converts logarithmic exponents through LogAxisValue.
 * Extrapolates outside the plot and may return nonfinite values; no layout is performed.
 */
static double InvHMap(Graph *graphPtr, Axis *axisPtr, double x) {
    double value;

    if (axisPtr->track != NULL) {
        x = (x - axisPtr->track->worldLeft) / MAX(1, axisPtr->track->worldRight - axisPtr->track->worldLeft);
    } else {
        x = (double)(x - graphPtr->hOffset) * graphPtr->hScale;
    }
    if (axisPtr->descending) {
        x = 1.0 - x;
    }
    value = InterpolateAxisValue(&axisPtr->axisRange, x);
    if (axisPtr->logScale) {
        value = LogAxisValue(value);
    }
    return value;
}

/**
 * @ingroup axis_ranges
 * @brief Invert vertical mapping into a logical data value.
 * @param[in] graphPtr Borrowed graph with current layout and mapping scales.
 * @param[in] axisPtr Borrowed axis with a prepared axisRange and live owning track, if any.
 * @param[in] y Vertical widget coordinate increasing downward; off-plot positions are extrapolated.
 *
 * Normalizes against the full owning-track world extent or graph vertical scale, reverses descending
 * direction and accounts for downward screen Y. Interpolates axis space and converts logarithmic
 * exponents. Does not clip or refresh layout; callers must handle nonfinite results.
 */
static double InvVMap(Graph *graphPtr, Axis *axisPtr, double y) {
    double value;

    if (axisPtr->track != NULL) {
        y = (y - axisPtr->track->worldTop) / MAX(1, axisPtr->track->worldBottom - axisPtr->track->worldTop);
    } else {
        y = (double)(y - graphPtr->vOffset) * graphPtr->vScale;
    }
    if (axisPtr->descending) {
        y = 1.0 - y;
    }
    value = InterpolateAxisValue(&axisPtr->axisRange, 1.0 - y);
    if (axisPtr->logScale) {
        value = LogAxisValue(value);
    }
    return value;
}

double Rbc_HMap(Graph *graphPtr, Axis *axisPtr, double x) {
    if (!FINITE(x)) {
        return NAN;
    }
    if (axisPtr->logScale) {
        if (x <= 0.0) {
            return NAN;
        }
        x = log10(x);
        if (!FINITE(x)) {
            return NAN;
        }
    }
    return MapHorizontalAxisCoordinate(graphPtr, axisPtr, x);
}

double Rbc_VMap(Graph *graphPtr, Axis *axisPtr, double y) {
    if (!FINITE(y)) {
        return NAN;
    }
    if (axisPtr->logScale) {
        if (y <= 0.0) {
            return NAN;
        }
        y = log10(y);
        if (!FINITE(y)) {
            return NAN;
        }
    }
    return MapVerticalAxisCoordinate(graphPtr, axisPtr, y);
}

Point2D Rbc_Map2D(Graph *graphPtr, double x, double y, Axis2D *axesPtr) {
    Point2D point;

    if (graphPtr->inverted) {
        point.x = Rbc_HMap(graphPtr, axesPtr->y, y);
        point.y = Rbc_VMap(graphPtr, axesPtr->x, x);
    } else {
        point.x = Rbc_HMap(graphPtr, axesPtr->x, x);
        point.y = Rbc_VMap(graphPtr, axesPtr->y, y);
    }
    return point;
}

Point2D Rbc_InvMap2D(Graph *graphPtr, double x, double y, Axis2D *axesPtr) {
    Point2D point;

    if (graphPtr->inverted) {
        point.x = InvVMap(graphPtr, axesPtr->x, y);
        point.y = InvHMap(graphPtr, axesPtr->y, x);
    } else {
        point.x = InvHMap(graphPtr, axesPtr->x, x);
        point.y = InvVMap(graphPtr, axesPtr->y, y);
    }
    return point;
}

/**
 * @ingroup axis_ranges
 * @brief Accumulate an element extent into an axis data range.
 * @param[in,out] axisPtr Live axis whose initialized valueRange endpoints accumulate the extent.
 * @param[in] min Contributing raw-data lower bound.
 * @param[in] max Contributing raw-data upper bound; cached range/scale are not recomputed here.
 *
 * Expands valueRange endpoints by comparison only; does not validate the extent, recompute cached
 * range/scale or apply requested limits. Rbc_ResetAxes initializes the accumulation sentinels before
 * calling this helper.
 */
static void GetDataLimits(Axis *axisPtr, double min, double max) {
    if (axisPtr->valueRange.min > min) {
        axisPtr->valueRange.min = min;
    }
    if (axisPtr->valueRange.max < max) {
        axisPtr->valueRange.max = max;
    }
}

/**
 * @ingroup axis_ranges
 * @brief Resolve accumulated data bounds and requested display limits.
 *
 * Supplies empty-data defaults, expands degenerate ranges and repairs invalid logarithmic inputs
 * before caching valueRange. Copies data limits into min/max, applies requested endpoints and adjusts
 * automatic endpoints when necessary. Applies automatic window shifting only when neither requested
 * endpoint is defined, clamping negative shiftBy to zero. Marks AXIS_DIRTY and updates previous limits
 * when resolved min/max change. Does not generate ticks or map geometry.
 */
static void FixAxisRange(Axis *axisPtr) {
    double min, max;
    /*
     * When auto-scaling, the axis limits are the bounds of the element
     * data.  If no data exists, set arbitrary limits (wrt to log/linear
     * scale).
     */
    min = axisPtr->valueRange.min;
    max = axisPtr->valueRange.max;
    if (min == DBL_MAX) {
        if (DEFINED(axisPtr->reqMin)) {
            min = axisPtr->reqMin;
        } else {
            min = (axisPtr->logScale) ? 0.001 : 0.0;
        }
    }
    if (max == -DBL_MAX) {
        if (DEFINED(axisPtr->reqMax)) {
            max = axisPtr->reqMax;
        } else {
            max = 1.0;
        }
    }
    if (axisPtr->logScale) {
        if ((!FINITE(min)) || (min <= 0.0)) {
            min = 0.001;
        }
        if ((!FINITE(max)) || (max <= 0.0)) {
            max = 1.0;
        }
    }
    if (min >= max) {
        if (axisPtr->logScale) {
            MakeLogRange(min, &min, &max);
        } else {
            double value;

            value = min;
            if (value == 0.0) {
                min = -0.1;
                max = 0.1;
            } else {
                double x;

                x = FABS(value) * 0.1;
                min = value - x;
                max = value + x;
            }
        }
    }
    SetAxisRange(&axisPtr->valueRange, min, max);
    /*
     * The axis limits are either the current data range or overridden
     * by the values selected by the user with the -min or -max
     * options.
     */
    axisPtr->min = min;
    axisPtr->max = max;
    if (DEFINED(axisPtr->reqMin)) {
        axisPtr->min = axisPtr->reqMin;
    }
    if (DEFINED(axisPtr->reqMax)) {
        axisPtr->max = axisPtr->reqMax;
    }
    if (axisPtr->max <= axisPtr->min) {
        if (axisPtr->logScale) {
            double minExponent;
            double maxExponent;

            minExponent = log10(DBL_MIN);
            maxExponent = log10(DBL_MAX);
            if (!DEFINED(axisPtr->reqMin)) {
                double exponent;

                exponent = log10(axisPtr->max);
                axisPtr->min = LogAxisValue(MAX(minExponent, exponent - 1.0));
            }
            if (!DEFINED(axisPtr->reqMax)) {
                double exponent;

                exponent = log10(axisPtr->min);
                axisPtr->max = LogAxisValue(MIN(maxExponent, exponent + 1.0));
            }
        } else {
            if (!DEFINED(axisPtr->reqMin)) {
                axisPtr->min = axisPtr->max - FABS(axisPtr->max) * 0.1;
            }
            if (!DEFINED(axisPtr->reqMax)) {
                axisPtr->max = axisPtr->min + FABS(axisPtr->max) * 0.1;
            }
        }
    }
    /*
     * If a window size is defined, handle auto ranging by shifting
     * the axis limits.
     */
    if ((axisPtr->windowSize > 0.0) && (!DEFINED(axisPtr->reqMin)) && (!DEFINED(axisPtr->reqMax))) {
        if (axisPtr->shiftBy < 0.0) {
            axisPtr->shiftBy = 0.0;
        }
        max = axisPtr->min + axisPtr->windowSize;
        if (axisPtr->max >= max) {
            if (axisPtr->shiftBy > 0.0) {
                max = UCEIL(axisPtr->max, axisPtr->shiftBy);
            }
            axisPtr->min = max - axisPtr->windowSize;
        }
        axisPtr->max = max;
    }
    if ((axisPtr->max != axisPtr->prevMax) || (axisPtr->min != axisPtr->prevMin)) {
        /* Indicate if the axis limits have changed */
        axisPtr->flags |= AXIS_DIRTY;
        /* and save the previous minimum and maximum values */
        axisPtr->prevMin = axisPtr->min;
        axisPtr->prevMax = axisPtr->max;
    }
}

/**
 * @ingroup axis_ranges
 * @brief Choose a decimal 1/2/5/10 multiple near a positive magnitude.
 *
 * Uses Heckbert's nice-number method (Graphics Gems, pages 61-63). The round argument selects nearest-
 * style thresholds rather than upward enclosure. Expects a positive input; does not guarantee a finite
 * positive result for extreme magnitudes, so scaling callers validate it. No graph state is changed.
 */
static double NiceNum(double x, int round) {
    double expt; /* Exponent of x */
    double frac; /* Fractional part of x */
    double nice; /* Nice, rounded fraction */

    expt = floor(log10(x));
    frac = x / EXP10(expt); /* between 1 and 10 */
    if (round) {
        if (frac < 1.5) {
            nice = 1.0;
        } else if (frac < 3.0) {
            nice = 2.0;
        } else if (frac < 7.0) {
            nice = 5.0;
        } else {
            nice = 10.0;
        }
    } else {
        if (frac <= 1.0) {
            nice = 1.0;
        } else if (frac <= 2.0) {
            nice = 2.0;
        } else if (frac <= 5.0) {
            nice = 5.0;
        } else {
            nice = 10.0;
        }
    }
    return nice * EXP10(expt);
}

/**
 * @ingroup axis_ranges
 * @brief Allocate values from an internal tick sweep.
 *
 * Returns an owned allocation, possibly with zero ticks. Invalid counts or nonfinite sweep fields
 * produce an empty array. A zero step selects the fixed log10(2)..log10(9) table and allows at most
 * eight entries. Other steps generate rounded successive values, stopping before a nonfinite value.
 * Uses ckalloc, so allocation failure is not a recoverable NULL result. Free the returned allocation
 * with ckfree.
 */
static Ticks *GenerateTicks(TickSweep *sweepPtr) {
    static const double logTable[] = {0.301029995663981, 0.477121254719662, 0.602059991327962, 0.698970004336019,
                                      0.778151250383644, 0.845098040014257, 0.903089986991944, 0.954242509439325};

    Ticks *ticksPtr;
    Tcl_Size nSteps;
    Tcl_Size i;
    size_t size;

    nSteps = sweepPtr->nSteps;
    /*
     * Generated sweeps are internal layout data.  An invalid sweep
     * should result in no ticks, not terminate the application.
     */
    if ((nSteps < 0) || (nSteps > (Tcl_Size)MAXTICKS) || (!FINITE(sweepPtr->initial)) || (!FINITE(sweepPtr->step))) {
        nSteps = 0;
    }
    if ((sweepPtr->step == 0.0) && (nSteps > (Tcl_Size)(sizeof(logTable) / sizeof(logTable[0])))) {
        nSteps = 0;
    }
    if (GetTicksByteCount(nSteps, &size) != TCL_OK) {
        /*
         * nSteps is already bounded by MAXTICKS, so this is only a
         * final defensive fallback.
         */
        nSteps = 0;
        if (GetTicksByteCount(0, &size) != TCL_OK) {
            Tcl_Panic("can't represent empty axis tick array");
        }
    }
    ticksPtr = ckalloc(size);
    if (sweepPtr->step == 0.0) {
        for (i = 0; i < nSteps; i++) {
            ticksPtr->values[i] = logTable[i];
        }
    } else {
        double value;

        value = sweepPtr->initial;
        for (i = 0; i < nSteps; i++) {
            value = RoundToStep(value, sweepPtr->step);
            if (!FINITE(value)) {
                break;
            }
            ticksPtr->values[i] = value;
            value += sweepPtr->step;
        }
        nSteps = i;
    }
    ticksPtr->nTicks = nSteps;
    return ticksPtr;
}

/**
 * @ingroup axis_ranges
 * @brief Return an independently owned major-tick array.
 *
 * Copies t1Ptr when present, whether explicit or cached generated ticks; size or attempted-allocation
 * failure then returns NULL. Otherwise generates from majorSweep, supporting hidden Polar axes without
 * rectangular tick caches. The axis and prepared sweep are borrowed; callers free the result with
 * ckfree. Generation uses the fatal allocator rather than the copy path's attempted allocator.
 */
Ticks *Rbc_AllocAxisMajorTicks(Axis *axisPtr) {
    Ticks *ticksPtr;
    size_t size;

    /*
     * If the user explicitly supplied -majorticks, duplicate that
     * array so the caller always owns the returned value.
     */
    if (axisPtr->t1Ptr != NULL) {
        if (GetTicksByteCount(axisPtr->t1Ptr->nTicks, &size) != TCL_OK) {
            return NULL;
        }
        ticksPtr = Tcl_AttemptAlloc(size);
        if (ticksPtr == NULL) {
            return NULL;
        }
        ticksPtr->nTicks = axisPtr->t1Ptr->nTicks;
        if (ticksPtr->nTicks > 0) {
            memcpy(ticksPtr->values, axisPtr->t1Ptr->values, (size_t)ticksPtr->nTicks * sizeof(double));
        }
        return ticksPtr;
    }
    /*
     * Hidden Polar axes normally have no cached t1Ptr because their
     * rectangular geometry is never constructed.  Generate the major
     * ticks directly from the already-computed sweep.
     */
    return GenerateTicks(&axisPtr->majorSweep);
}

/**
 * @ingroup axis_ranges
 * @brief Prepare logarithmic mapping bounds and tick sweeps.
 *
 * Repairs invalid positive inputs and collapsed logarithms, then selects major ticks in exponent
 * space. Spans exceeding ten enclosing decades use a nice exponent interval; narrower spans use unit
 * decades. Applies endpoint loose policies and supported exponent bounds. Narrow minor sweeps use the
 * zero-step log table; wide sweeps use four fractions at 0.2 increments. Explicit major ticks disable
 * automatic minors. Writes axisRange and both sweep records, without allocating tick arrays.
 */
static void LogScaleAxis(Axis *axisPtr, double min, double max) {
    double logMin;
    double logMax;
    double tickMin;
    double tickMax;
    double axisMin;
    double axisMax;
    double range;
    double majorStep;
    double minorStep;
    double minExponent;
    double maxExponent;
    Tcl_Size nMajor;
    Tcl_Size nMinor;
    int wideRange;
    int haveSweep;

    minExponent = log10(DBL_MIN);
    maxExponent = log10(DBL_MAX);
    /*
     * FixAxisRange should already guarantee this, but keep the
     * numerical core self-contained.
     */
    if ((!FINITE(min)) || (min <= 0.0)) {
        min = 0.001;
    }
    if ((!FINITE(max)) || (max <= 0.0)) {
        max = 1.0;
    }
    logMin = log10(min);
    logMax = log10(max);
    /*
     * Two distinct raw doubles can occasionally collapse onto the
     * same logarithm through rounding.  Ensure a drawable exponent
     * range.
     */
    if (!(logMin < logMax)) {
        double center;

        center = logMin;
        logMin = MAX(minExponent, center - 0.5);
        logMax = MIN(maxExponent, center + 0.5);
        if (!(logMin < logMax)) {
            if (center >= maxExponent) {
                logMax = maxExponent;
                logMin = MAX(minExponent, maxExponent - 1.0);
            } else {
                logMin = minExponent;
                logMax = MIN(maxExponent, minExponent + 1.0);
            }
        }
    }
    range = ceil(logMax) - floor(logMin);
    wideRange = (range > 10.0);
    if (wideRange) {
        range = NiceNum(range, 0);
        majorStep = NiceNum(range / DEF_NUM_TICKS, 1);
        if ((!FINITE(majorStep)) || (majorStep <= 0.0)) {
            majorStep = 1.0;
        }
    } else {
        majorStep = 1.0;
    }
    haveSweep = GetLinearSweep(logMin, logMax, majorStep, &tickMin, &tickMax, &nMajor);
    if (!haveSweep) {
        majorStep = 1.0;
        haveSweep = GetLinearSweep(logMin, logMax, majorStep, &tickMin, &tickMax, &nMajor);
    }
    if (!haveSweep) {
        tickMin = logMin;
        tickMax = logMax;
        nMajor = 0;
    }
    axisMin = tickMin;
    axisMax = tickMax;
    if ((axisPtr->looseMin == TICK_RANGE_TIGHT) ||
        ((axisPtr->looseMin == TICK_RANGE_LOOSE) && DEFINED(axisPtr->reqMin))) {
        axisMin = logMin;
    }
    if ((axisPtr->looseMax == TICK_RANGE_TIGHT) ||
        ((axisPtr->looseMax == TICK_RANGE_LOOSE) && DEFINED(axisPtr->reqMax))) {
        axisMax = logMax;
    }
    /*
     * axisRange for a log axis is stored in exponent coordinates, but
     * it must still represent values convertible back to finite doubles.
     */
    axisMin = MAX(axisMin, minExponent);
    axisMax = MIN(axisMax, maxExponent);
    if (!(axisMin < axisMax)) {
        axisMin = logMin;
        axisMax = logMax;
    }
    axisPtr->majorSweep.step = majorStep;
    axisPtr->majorSweep.initial = tickMin;
    axisPtr->majorSweep.nSteps = nMajor;
    if (wideRange) {
        /*
         * Wide log axes are treated linearly in exponent space.
         * Generate four equally spaced minor ticks between majors.
         */
        minorStep = 0.2;
        nMinor = 4;
    } else {
        /*
         * step == 0 tells GenerateTicks to use the logarithmic 2..9
         * minor-tick table.
         */
        minorStep = 0.0;
        nMinor = 8;
    }
    /*
     * Explicit major ticks disable automatic minor ticks, as on a
     * linear axis. SweepTicks preserves any explicit -minorticks list.
     */
    if (axisPtr->flags & AXIS_CONFIG_MAJOR) {
        nMinor = 0;
    }
    axisPtr->minorSweep.initial = minorStep;
    axisPtr->minorSweep.step = minorStep;
    axisPtr->minorSweep.nSteps = nMinor;
    SetAxisRange(&axisPtr->axisRange, axisMin, axisMax);
}

/**
 * @ingroup axis_ranges
 * @brief Prepare linear mapping bounds and tick sweeps.
 *
 * Tries a requested positive step, reducing overly large intervals by halves. Falls back to
 * AutomaticLinearStep if the requested sweep is impractical, then to no major ticks if necessary.
 * Applies independent loose policies to mapping endpoints. Automatic minors subdivide the major
 * interval unless explicit major ticks are configured. Writes axisRange and sweep records; actual
 * arrays are generated later.
 */
static void LinearScaleAxis(Axis *axisPtr, double min, double max) {
    double step;
    double tickMin;
    double tickMax;
    double axisMin;
    double axisMax;
    double halfRange;
    Tcl_Size nTicks;
    int haveSweep;

    haveSweep = FALSE;
    /*
     * Honour a requested step when it yields a practical tick count.
     *
     * Preserve the historical behaviour that scales an overly large
     * requested interval down until at least a few intervals fit in
     * the current range.
     */
    if (axisPtr->reqStep > 0.0) {
        step = axisPtr->reqStep;
        /*
         * This is (max - min) / 2 without first forming max - min,
         * which may overflow.
         */
        halfRange = max * 0.5 - min * 0.5;
        if (FINITE(halfRange) && (halfRange > 0.0)) {
            while ((step >= halfRange) && (step > 0.0)) {
                step *= 0.5;
            }
        }
        if (GetLinearSweep(min, max, step, &tickMin, &tickMax, &nTicks)) {
            haveSweep = TRUE;
        }
    }
    /*
     * A requested interval may be finite but far too small for this
     * particular range.  In that case use the normal automatically
     * selected interval rather than generating thousands of ticks.
     */
    if (!haveSweep) {
        step = AutomaticLinearStep(min, max);
        if (GetLinearSweep(min, max, step, &tickMin, &tickMax, &nTicks)) {
            haveSweep = TRUE;
        }
    }
    if (!haveSweep) {
        /*
         * The range is numerically too degenerate to produce a useful
         * sweep.  Keep the axis range but generate no major ticks.
         */
        step = 1.0;
        tickMin = min;
        tickMax = max;
        nTicks = 0;
    }
    axisMin = tickMin;
    axisMax = tickMax;
    axisPtr->majorSweep.step = step;
    axisPtr->majorSweep.initial = tickMin;
    axisPtr->majorSweep.nSteps = nTicks;
    /*
     * The limits of the axis are either the range of the data
     * ("tight") or at the next outer tick interval ("loose").
     */
    if ((axisPtr->looseMin == TICK_RANGE_TIGHT) ||
        ((axisPtr->looseMin == TICK_RANGE_LOOSE) && (DEFINED(axisPtr->reqMin)))) {
        axisMin = min;
    }
    if ((axisPtr->looseMax == TICK_RANGE_TIGHT) ||
        ((axisPtr->looseMax == TICK_RANGE_LOOSE) && (DEFINED(axisPtr->reqMax)))) {
        axisMax = max;
    }
    SetAxisRange(&axisPtr->axisRange, axisMin, axisMax);
    /*
     * Calculate the minor tick step and number.
     */
    if ((axisPtr->reqNumMinorTicks > 0) && ((axisPtr->flags & AXIS_CONFIG_MAJOR) == 0)) {
        nTicks = (Tcl_Size)axisPtr->reqNumMinorTicks - 1;
        step = 1.0 / ((double)nTicks + 1.0);
    } else {
        nTicks = 0;
        /*
         * Don't use zero here.  GenerateTicks uses a zero interval to
         * identify logarithmic minor ticks.
         */
        step = 0.5;
    }
    axisPtr->minorSweep.initial = step;
    axisPtr->minorSweep.step = step;
    axisPtr->minorSweep.nSteps = nTicks;
}

/**
 * @ingroup axis_ranges
 * @brief Refresh generated tick caches while retaining explicit arrays.
 *
 * Replaces t1Ptr/t2Ptr only when their AXIS_CONFIG_MAJOR/MINOR flags are clear. Frees old generated
 * arrays and owns the new GenerateTicks results. Does not change explicit arrays, recompute sweeps or
 * report recoverable allocation errors.
 */
static void SweepTicks(Axis *axisPtr) {
    if ((axisPtr->flags & AXIS_CONFIG_MAJOR) == 0) {
        if (axisPtr->t1Ptr != NULL) {
            ckfree((char *)axisPtr->t1Ptr);
        }
        axisPtr->t1Ptr = GenerateTicks(&axisPtr->majorSweep);
    }
    if ((axisPtr->flags & AXIS_CONFIG_MINOR) == 0) {
        if (axisPtr->t2Ptr != NULL) {
            ckfree((char *)axisPtr->t2Ptr);
        }
        axisPtr->t2Ptr = GenerateTicks(&axisPtr->minorSweep);
    }
}

/**
 * @ingroup axis_ranges
 * @brief Recompute data ranges and scaling sweeps for the graph.
 *
 * Prepares bar frequencies/stacks, resets every registered axis to accumulation sentinels and collects
 * extents from elements that are neither hidden nor plotHidden. Applies Polar empty-grid fallbacks or
 * Smith unit-circle bounds to mapped nonlogarithmic grid axes. Resolves requested ranges, constrains
 * scaling inputs by scroll limits and computes linear/log sweeps. Clears RESET_AXES and requests
 * geometry, layout, mapping and redraw; does not allocate tick caches or draw. Registered delete-
 * pending records are included because consumers may still reference them.
 */
void Rbc_ResetAxes(Graph *graphPtr) {
    Rbc_ChainLink *linkPtr;
    Element *elemPtr;
    Axis *axisPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Extents2D exts;
    double min, max;

    /* FIXME: This should be called whenever the display list of
     * elements change. Maybe yet another flag INIT_STACKS to
     * indicate that the element display list has changed.
     * Needs to be done before the axis limits are set.
     */
    Rbc_InitFreqTable(graphPtr);
    if ((graphPtr->trackGraph || graphPtr->mode == MODE_STACKED) && (graphPtr->nStacks > 0)) {
        Rbc_ComputeStacks(graphPtr);
    }
    Rbc_ResetStacks(graphPtr);
    /*
     * Step 1:  Reset all axes. Initialize the data limits of the axis to
     *        impossible values.
     */
    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        axisPtr->min = axisPtr->valueRange.min = DBL_MAX;
        axisPtr->max = axisPtr->valueRange.max = -DBL_MAX;
    }
    /*
     * Step 2:  For each element that's to be displayed, get the smallest
     *        and largest data values mapped to each X and Y-axis.  This
     *        will be the axis limits if the user doesn't override them
     *        with -min and -max options.
     */
    for (linkPtr = Rbc_ChainFirstLink(graphPtr->elements.displayList); linkPtr != NULL;
         linkPtr = Rbc_ChainNextLink(linkPtr)) {
        elemPtr = Rbc_ChainGetValue(linkPtr);
        if (elemPtr->hidden || elemPtr->plotHidden) {
            continue;
        }
        (*elemPtr->procsPtr->extentsProc)(elemPtr, &exts);
        GetDataLimits(elemPtr->axes.x, exts.left, exts.right);
        GetDataLimits(elemPtr->axes.y, exts.top, exts.bottom);
    }
    /*
     * A Polar representation needs a Cartesian range containing the
     * origin in both dimensions in order to draw its circles and spokes.
     *
     * Normally that range comes from visible element data.  If an axis
     * mapped to the Polar grid has no contributing visible element data,
     * provide a neutral symmetric fallback range.
     *
     * Unlike Smith's intrinsic unit circle below, this is only a fallback:
     * visible Polar element data remain free to determine their normal
     * automatic range.
     */
    if ((graphPtr->classUid == rbcPolarElementUid) && (graphPtr->representation == POLAR_REPRESENTATION_POLAR) &&
        (graphPtr->gridPtr != NULL)) {
        Axis *xAxisPtr;
        Axis *yAxisPtr;

        xAxisPtr = graphPtr->gridPtr->axes.x;
        yAxisPtr = graphPtr->gridPtr->axes.y;
        if ((xAxisPtr != NULL) && (!xAxisPtr->logScale) && (xAxisPtr->valueRange.min == DBL_MAX) &&
            (xAxisPtr->valueRange.max == -DBL_MAX)) {
            GetDataLimits(xAxisPtr, -1.0, 1.0);
        }
        if ((yAxisPtr != NULL) && (!yAxisPtr->logScale) && (yAxisPtr->valueRange.min == DBL_MAX) &&
            (yAxisPtr->valueRange.max == -DBL_MAX)) {
            GetDataLimits(yAxisPtr, -1.0, 1.0);
        }
    }
    /*
     * The Smith representation is defined in the reflection-coefficient
     * plane.  Its unit circle is part of the graph representation itself,
     * so the axes mapped to the Smith grid must always contain [-1,+1],
     * independently of the element data.
     */
    if ((graphPtr->classUid == rbcPolarElementUid) && (graphPtr->representation == POLAR_REPRESENTATION_SMITH) &&
        (graphPtr->gridPtr != NULL)) {
        Axis *xAxisPtr;
        Axis *yAxisPtr;

        xAxisPtr = graphPtr->gridPtr->axes.x;
        yAxisPtr = graphPtr->gridPtr->axes.y;
        if ((xAxisPtr != NULL) && (!xAxisPtr->logScale)) {
            GetDataLimits(xAxisPtr, -1.0, 1.0);
        }
        if ((yAxisPtr != NULL) && (!yAxisPtr->logScale)) {
            GetDataLimits(yAxisPtr, -1.0, 1.0);
        }
    }
    /*
     * Step 3:  Now that we know the range of data values for each axis,
     *        set axis limits and compute a sweep to generate tick values.
     */
    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        FixAxisRange(axisPtr);
        /* Calculate min/max tick (major/minor) layouts */
        min = axisPtr->min;
        max = axisPtr->max;
        if ((DEFINED(axisPtr->scrollMin)) && (min < axisPtr->scrollMin)) {
            min = axisPtr->scrollMin;
        }
        if ((DEFINED(axisPtr->scrollMax)) && (max > axisPtr->scrollMax)) {
            max = axisPtr->scrollMax;
        }
        if (axisPtr->logScale) {
            LogScaleAxis(axisPtr, min, max);
        } else {
            LinearScaleAxis(axisPtr, min, max);
        }

        if ((axisPtr->flags & (AXIS_DIRTY | AXIS_ONSCREEN)) == (AXIS_DIRTY | AXIS_ONSCREEN)) {
            graphPtr->flags |= REDRAW_BACKING_STORE;
        }
    }
    graphPtr->flags &= ~RESET_AXES;
    /*
     * When any axis changes, we need to layout the entire graph.
     */
    graphPtr->flags |= (GET_AXIS_GEOMETRY | LAYOUT_NEEDED | MAP_ALL | REDRAW_WORLD);
}

/**
 * @ingroup axis_configuration
 * @brief Refresh text styles and replace the axis tick graphics context.
 *
 * Borrows configured fonts and colours. Allocates the new tick GC before releasing the old one; uses
 * the existing native line-width conversion and projecting caps.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in,out] axisPtr Borrowed configured axis with valid fonts/colors; text styles and tick GC are
 *   refreshed.
 */
static void ResetTextStyles(Graph *graphPtr, Axis *axisPtr) {
    GC newGC;
    XGCValues gcValues;
    unsigned long gcMask;

    Rbc_ResetTextStyle(graphPtr->tkwin, &axisPtr->titleTextStyle);
    Rbc_ResetTextStyle(graphPtr->tkwin, &axisPtr->tickTextStyle);
    Rbc_ResetTextStyle(graphPtr->tkwin, &axisPtr->limitsTextStyle);
    gcMask = (GCForeground | GCLineWidth | GCCapStyle);
    gcValues.foreground = axisPtr->tickTextStyle.color->pixel;
    gcValues.line_width = LineWidth(axisPtr->lineWidth);
    gcValues.cap_style = CapProjecting;
    newGC = Tk_GetGC(graphPtr->tkwin, gcMask, &gcValues);
    if (axisPtr->tickGC != NULL) {
        Tk_FreeGC(graphPtr->display, axisPtr->tickGC);
    }
    axisPtr->tickGC = newGC;
}

/**
 * @ingroup axis_registry
 * @brief Destroy an axis and all resources it owns.
 *
 * Cancels pending scrollbar notification, releases options and bindings, removes the margin link and
 * registry entry when present, and frees styles, shadows, tick arrays, labels, formats, segments, tags
 * and the record. Does not consult refCount; callers must ensure no consumers retain the record.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in,out] axisPtr Owned axis consumed unconditionally; no consumer may retain it afterward, regardless
 *   of refCount.
 */
static void DestroyAxis(Graph *graphPtr, Axis *axisPtr) {
    if (axisPtr->scrollNotifyPending) {
        Tcl_CancelIdleCall(NotifyAxisScrollbar, axisPtr);
    }
    ReleaseAxisOptionResources(graphPtr, axisPtr);
    if (graphPtr->bindTable != NULL) {
        Rbc_DeleteBindings(graphPtr->bindTable, axisPtr);
    }
    if (axisPtr->linkPtr != NULL) {
        Rbc_ChainDeleteLink(axisPtr->chainPtr, axisPtr->linkPtr);
    }
    if (axisPtr->name != NULL) {
        ckfree((char *)axisPtr->name);
    }
    if (axisPtr->hashPtr != NULL) {
        Tcl_DeleteHashEntry(axisPtr->hashPtr);
    }
    /*
     * These release the graphics contexts maintained by each text
     * style. The option system owns the fonts and foreground colours.
     */
    Rbc_FreeTextStyle(graphPtr->display, &axisPtr->titleTextStyle);
    Rbc_FreeTextStyle(graphPtr->display, &axisPtr->limitsTextStyle);
    Rbc_FreeTextStyle(graphPtr->display, &axisPtr->tickTextStyle);
    /*
     * Shadow colours are allocated independently by the shadow parser
     * and are not released by Rbc_FreeTextStyle or the Tk option table.
     */
    FreeAxisShadow(&axisPtr->titleTextStyle.shadow);
    FreeAxisShadow(&axisPtr->limitsTextStyle.shadow);
    FreeAxisShadow(&axisPtr->tickTextStyle.shadow);
    if (axisPtr->tickGC != NULL) {
        Tk_FreeGC(graphPtr->display, axisPtr->tickGC);
    }
    if (axisPtr->t1Ptr != NULL) {
        ckfree((char *)axisPtr->t1Ptr);
    }
    if (axisPtr->t2Ptr != NULL) {
        ckfree((char *)axisPtr->t2Ptr);
    }
    if (axisPtr->limitsFormats != NULL) {
        ckfree((char *)axisPtr->limitsFormats);
    }
    FreeLabels(axisPtr->tickLabels);
    Rbc_ChainDestroy(axisPtr->tickLabels);
    if (axisPtr->segments != NULL) {
        ckfree((char *)axisPtr->segments);
    }
    if (axisPtr->tags != NULL) {
        ckfree((char *)axisPtr->tags);
    }
    ckfree((char *)axisPtr);
}

/**
 * @ingroup axis_presentation
 * @brief Compute baseline, tick and label offsets for one margin band.
 *
 * Requires a concrete margin and prepared axis thickness. Updates title rotation/anchor/position, tick
 * anchor and picking region. Accounts for plot and axis borders, alternate titles and line width.
 * Returns absolute baseline/major/minor/label positions in AxisInfo. A negative tick length swaps the
 * baseline and major endpoint to retain inward-tick behavior.
 */
static void AxisOffsets(Graph *graphPtr, Axis *axisPtr, int margin, int axisOffset, AxisInfo *infoPtr) {
    int pad; /* Offset of axis from interior region. This
              * includes a possible border and the axis
              * line width. */
    int p;
    int majorOffset, minorOffset, labelOffset;
    int offset;
    int x, y;

    axisPtr->titleTextStyle.theta = titleRotate[margin];
    majorOffset = minorOffset = 0;
    labelOffset = AXIS_TITLE_PAD;
    if (axisPtr->lineWidth > 0) {
        majorOffset = ABS(axisPtr->tickLength);
        minorOffset = 10 * majorOffset / 15;
        labelOffset = majorOffset + AXIS_TITLE_PAD + axisPtr->lineWidth / 2;
    }
    /* Adjust offset for the interior border width and the line width */
    pad = axisPtr->lineWidth + 1;
    if (graphPtr->plotBorderWidth > 0) {
        pad += graphPtr->plotBorderWidth + 1;
    }
    offset = axisOffset + 1 + pad;
    if ((margin == MARGIN_LEFT) || (margin == MARGIN_TOP)) {
        majorOffset = -majorOffset;
        minorOffset = -minorOffset;
        labelOffset = -labelOffset;
    }
    /*
     * Pre-calculate the x-coordinate positions of the axis, tick labels, and
     * the individual major and minor ticks.
     */
    p = 0; /* Suppress compiler warning */
    switch (margin) {
    case MARGIN_TOP:
        p = graphPtr->top - axisOffset - pad;
        if (axisPtr->titleAlternate) {
            x = graphPtr->right + AXIS_TITLE_PAD;
            y = graphPtr->top - axisOffset - (axisPtr->height / 2);
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_W;
        } else {
            x = (graphPtr->right + graphPtr->left) / 2;
            y = graphPtr->top - axisOffset - axisPtr->height - AXIS_TITLE_PAD;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_N;
        }
        axisPtr->tickTextStyle.anchor = TK_ANCHOR_S;
        offset = axisPtr->borderWidth + axisPtr->lineWidth / 2;
        axisPtr->region.left = graphPtr->hOffset - offset - 2;
        axisPtr->region.right = graphPtr->hOffset + graphPtr->hRange + offset - 1;
        axisPtr->region.top = p + labelOffset - 1;
        axisPtr->region.bottom = p;
        axisPtr->titlePos.x = x;
        axisPtr->titlePos.y = y;
        break;
    case MARGIN_BOTTOM:
        p = graphPtr->bottom + axisOffset + pad;
        if (axisPtr->titleAlternate) {
            x = graphPtr->right + AXIS_TITLE_PAD;
            y = graphPtr->bottom + axisOffset + (axisPtr->height / 2);
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_W;
        } else {
            x = (graphPtr->right + graphPtr->left) / 2;
            y = graphPtr->bottom + axisOffset + axisPtr->height + AXIS_TITLE_PAD;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_S;
        }
        axisPtr->tickTextStyle.anchor = TK_ANCHOR_N;
        offset = axisPtr->borderWidth + axisPtr->lineWidth / 2;
        axisPtr->region.left = graphPtr->hOffset - offset - 2;
        axisPtr->region.right = graphPtr->hOffset + graphPtr->hRange + offset - 1;

        axisPtr->region.top = graphPtr->bottom + axisOffset + axisPtr->lineWidth - axisPtr->lineWidth / 2;
        axisPtr->region.bottom = graphPtr->bottom + axisOffset + axisPtr->lineWidth + labelOffset + 1;
        axisPtr->titlePos.x = x;
        axisPtr->titlePos.y = y;
        break;
    case MARGIN_LEFT:
        p = graphPtr->left - axisOffset - pad;
        if (axisPtr->titleAlternate) {
            x = graphPtr->left - axisOffset - (axisPtr->width / 2);
            y = graphPtr->top - AXIS_TITLE_PAD;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_SW;
        } else {
            x = graphPtr->left - axisOffset - axisPtr->width - graphPtr->plotBorderWidth;
            y = (graphPtr->bottom + graphPtr->top) / 2;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_W;
        }
        axisPtr->tickTextStyle.anchor = TK_ANCHOR_E;
        axisPtr->region.left = graphPtr->left - offset + labelOffset - 1;
        axisPtr->region.right = graphPtr->left - offset + 2;
        offset = axisPtr->borderWidth + axisPtr->lineWidth / 2;
        axisPtr->region.top = graphPtr->vOffset - offset - 2;
        axisPtr->region.bottom = graphPtr->vOffset + graphPtr->vRange + offset - 1;
        axisPtr->titlePos.x = x;
        axisPtr->titlePos.y = y;
        break;
    case MARGIN_RIGHT:
        p = graphPtr->right + axisOffset + pad;
        if (axisPtr->titleAlternate) {
            x = graphPtr->right + axisOffset + (axisPtr->width / 2);
            y = graphPtr->top - AXIS_TITLE_PAD;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_SE;
        } else {
            x = graphPtr->right + axisOffset + axisPtr->width + AXIS_TITLE_PAD;
            y = (graphPtr->bottom + graphPtr->top) / 2;
            axisPtr->titleTextStyle.anchor = TK_ANCHOR_E;
        }
        axisPtr->tickTextStyle.anchor = TK_ANCHOR_W;
        axisPtr->region.left = graphPtr->right + axisOffset + axisPtr->lineWidth - axisPtr->lineWidth / 2;
        axisPtr->region.right = graphPtr->right + axisOffset + labelOffset + axisPtr->lineWidth + 1;
        offset = axisPtr->borderWidth + axisPtr->lineWidth / 2;
        axisPtr->region.top = graphPtr->vOffset - offset - 2;
        axisPtr->region.bottom = graphPtr->vOffset + graphPtr->vRange + offset - 1;
        axisPtr->titlePos.x = x;
        axisPtr->titlePos.y = y;
        break;
    case MARGIN_NONE:
        break;
    }
    infoPtr->axis = p - (axisPtr->lineWidth / 2);
    infoPtr->t1 = p + majorOffset;
    infoPtr->t2 = p + minorOffset;
    infoPtr->label = p + labelOffset;
    if (axisPtr->tickLength < 0) {
        int hold;

        hold = infoPtr->t1;
        infoPtr->t1 = infoPtr->axis;
        infoPtr->axis = hold;
    }
}

/**
 * @ingroup axis_presentation
 * @brief Map the baseline endpoints from the prepared axis range.
 *
 * Writes one Segment2D using logical orientation and track-aware coordinate mapping. The line argument
 * is the fixed screen coordinate perpendicular to the axis. Inputs are axis-space bounds, already
 * logarithmic when appropriate.
 */
static void MakeAxisLine(Graph *graphPtr, Axis *axisPtr, int line, Segment2D *segPtr) {
    double min;
    double max;

    min = axisPtr->axisRange.min;
    max = axisPtr->axisRange.max;
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        segPtr->p.x = MapHorizontalAxisCoordinate(graphPtr, axisPtr, min);
        segPtr->q.x = MapHorizontalAxisCoordinate(graphPtr, axisPtr, max);
        segPtr->p.y = segPtr->q.y = line;
    } else {
        segPtr->p.x = segPtr->q.x = line;
        segPtr->p.y = MapVerticalAxisCoordinate(graphPtr, axisPtr, min);
        segPtr->q.y = MapVerticalAxisCoordinate(graphPtr, axisPtr, max);
    }
}

/**
 * @ingroup axis_presentation
 * @brief Map one axis-space value to a tick segment.
 *
 * Writes the segment between the absolute line and tick screen coordinates. Chooses screen orientation
 * from logical class and inversion. Does not check range membership or convert raw logarithmic values;
 * the caller does so before mapping.
 */
static void MakeTick(Graph *graphPtr, Axis *axisPtr, double value, int tick, int line, Segment2D *segPtr) {
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        segPtr->p.x = segPtr->q.x = MapHorizontalAxisCoordinate(graphPtr, axisPtr, value);
        segPtr->p.y = line;
        segPtr->q.y = tick;
    } else {
        segPtr->p.x = line;
        segPtr->p.y = segPtr->q.y = MapVerticalAxisCoordinate(graphPtr, axisPtr, value);
        segPtr->q.x = tick;
    }
}

/**
 * @ingroup axis_ranges
 * @brief Resolve a minor fraction against a major interval.
 *
 * Requires a valid major array index. With explicit majors, requires a following distinct endpoint and
 * a fraction strictly inside 0..1, interpolates between adjacent entries, and rejects rounded results
 * outside that open interval. Otherwise adds majorSweep.step times fraction. Returns FALSE for
 * invalid/nonfinite results without assigning the output; TRUE writes an axis-space coordinate.
 */
static int GetMinorTickPosition(const Axis *axisPtr, const Ticks *majorPtr, Tcl_Size index,
                                double fraction, double *valuePtr) {
    double first;
    double value;

    first = majorPtr->values[index];
    if (axisPtr->flags & AXIS_CONFIG_MAJOR) {
        double last;
        double step;

        if ((index + 1 >= majorPtr->nTicks) || !(fraction > 0.0 && fraction < 1.0)) {
            return FALSE;
        }
        last = majorPtr->values[index + 1];
        if (first == last) {
            return FALSE;
        }
        step = last - first;
        value = FINITE(step) ? first + fraction * step : (1.0 - fraction) * first + fraction * last;
        if (!(value > MIN(first, last) && value < MAX(first, last))) {
            return FALSE;
        }
    } else {
        value = first + axisPtr->majorSweep.step * fraction;
    }
    if (!FINITE(value)) {
        return FALSE;
    }
    *valuePtr = value;
    return TRUE;
}

/**
 * @ingroup axis_presentation
 * @brief Replace cached axis segments and position measured labels.
 *
 * Requires prepared tick arrays and the matching filtered label chain when ticks are shown. Computes
 * offsets, releases old segments, checks count/byte overflow and attempts allocation. Failure leaves
 * zero segments and returns without an interpreter error. Maps in-range major/minor ticks and label
 * anchors, including labelOffset. Saves the owned segment array only when nonempty; does not evaluate
 * label callbacks.
 */
static void MapAxis(Graph *graphPtr, Axis *axisPtr, int offset, int margin) {
    Tcl_Size arraySize;
    Tcl_Size nMajorTicks;
    Tcl_Size nMinorTicks;
    AxisInfo info;
    Segment2D *segments;
    Segment2D *segPtr;

    AxisOffsets(graphPtr, axisPtr, margin, offset, &info);
    /* Save all line coordinates in an array of line segments. */
    if (axisPtr->segments != NULL) {
        ckfree(axisPtr->segments);
        axisPtr->segments = NULL;
    }
    axisPtr->nSegments = 0;
    nMajorTicks = nMinorTicks = 0;
    if (axisPtr->t1Ptr != NULL) {
        nMajorTicks = axisPtr->t1Ptr->nTicks;
    }
    if (axisPtr->t2Ptr != NULL) {
        nMinorTicks = axisPtr->t2Ptr->nTicks;
    }
    if ((nMinorTicks == TCL_SIZE_MAX) || (nMajorTicks > (TCL_SIZE_MAX - 1) / (nMinorTicks + 1))) {
        return;
    }
    arraySize = 1 + (nMajorTicks * (nMinorTicks + 1));
    if ((size_t)arraySize > SIZE_MAX / sizeof(*segments)) {
        return;
    }
    segments = Tcl_AttemptAlloc((size_t)arraySize * sizeof(*segments));
    if (segments == NULL) {
        return;
    }
    segPtr = segments;
    if (axisPtr->lineWidth > 0) {
        /* Axis baseline */
        MakeAxisLine(graphPtr, axisPtr, info.axis, segPtr);
        segPtr++;
    }
    if (axisPtr->showTicks) {
        double t1, t2;
        double labelPos;
        Tcl_Size i;
        Tcl_Size j;
        int isHoriz;
        TickLabel *labelPtr;
        Rbc_ChainLink *linkPtr;
        Segment2D seg;

        isHoriz = AxisIsHorizontal(graphPtr, axisPtr);
        for (i = 0; i < axisPtr->t1Ptr->nTicks; i++) {
            t1 = axisPtr->t1Ptr->values[i];
            /* Minor ticks */
            for (j = 0; j < axisPtr->t2Ptr->nTicks; j++) {
                if (GetMinorTickPosition(axisPtr, axisPtr->t1Ptr, i, axisPtr->t2Ptr->values[j], &t2) &&
                    InRange(t2, &axisPtr->axisRange)) {
                    MakeTick(graphPtr, axisPtr, t2, info.t2, info.axis, segPtr);
                    segPtr++;
                }
            }
            if (!InRange(t1, &axisPtr->axisRange)) {
                continue;
            }
            /* Major tick */
            MakeTick(graphPtr, axisPtr, t1, info.t1, info.axis, segPtr);
            segPtr++;
        }
        linkPtr = Rbc_ChainFirstLink(axisPtr->tickLabels);
        labelPos = (double)info.label;
        for (i = 0; i < axisPtr->t1Ptr->nTicks; i++) {
            t1 = axisPtr->t1Ptr->values[i];
            if (axisPtr->labelOffset) {
                t1 += axisPtr->majorSweep.step * 0.5;
            }
            if (!InRange(t1, &axisPtr->axisRange)) {
                continue;
            }
            labelPtr = Rbc_ChainGetValue(linkPtr);
            linkPtr = Rbc_ChainNextLink(linkPtr);
            MakeTick(graphPtr, axisPtr, t1, info.t1, info.axis, &seg);
            /* Save tick label X-Y position. */
            if (isHoriz) {
                labelPtr->anchorPos.x = seg.p.x;
                labelPtr->anchorPos.y = labelPos;
            } else {
                labelPtr->anchorPos.x = labelPos;
                labelPtr->anchorPos.y = seg.p.y;
            }
        }
    }
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        axisPtr->width = graphPtr->right - graphPtr->left;
    } else {
        axisPtr->height = graphPtr->bottom - graphPtr->top;
    }
    axisPtr->nSegments = (Tcl_Size)(segPtr - segments);
    assert(axisPtr->nSegments <= arraySize);
    if (axisPtr->nSegments > 0) {
        axisPtr->segments = segments;
    } else {
        ckfree(segments);
    }
}

/**
 * @ingroup axis_layout_control
 * @brief Constrain a normalized scrolling offset using canvas-style rules.
 * @param[in] offset Finite normalized start position established by the caller.
 * @param[in] windowSize Finite view/world size ratio; values greater than one allow the world to move within
 *     the view.
 *
 * For views no larger than the world, constrains the offset to 0..1-windowSize. Larger views permit
 * the world to move within the window. Returns the adjusted offset without validating finiteness;
 * callers establish valid scroll state first.
 */
static double AdjustViewport(double offset, double windowSize) {
    /*
     * Canvas-style scrolling allows the world to be scrolled
     * within the window.
     */
    if (windowSize > 1.0) {
        if (windowSize < (1.0 - offset)) {
            offset = 1.0 - windowSize;
        }
        if (offset > 0.0) {
            offset = 0.0;
        }
    } else {
        if ((offset + windowSize) > 1.0) {
            offset = 1.0 - windowSize;
        }
        if (offset < 0.0) {
            offset = 0.0;
        }
    }
    return offset;
}

/**
 * @ingroup axis_layout_control
 * @brief Derive a normalized current view and effective scroll world.
 *
 * Starts from valueRange and min/max, replaces missing world sentinels with view endpoints, then
 * applies requested scroll bounds. Rejects nonfinite values, nonpositive logarithmic inputs and
 * nonincreasing worlds. Intersects the view with the world, collapsing wholly outside views to the
 * nearest boundary. Allows a zero-width view. On TRUE returns normalized endpoints and a world range
 * in linear or exponent coordinates. Outputs must not be consumed after FALSE; no live axis state is
 * modified.
 */
static int GetAxisScrollState(Axis *axisPtr, AxisRange *rangePtr, double *viewMinNormPtr, double *viewMaxNormPtr) {
    double worldMin;
    double worldMax;
    double viewMin;
    double viewMax;
    double viewMinNorm;
    double viewMaxNorm;

    worldMin = axisPtr->valueRange.min;
    worldMax = axisPtr->valueRange.max;
    viewMin = axisPtr->min;
    viewMax = axisPtr->max;
    /*
     * A newly created or currently unused axis may not have had a
     * data range established yet.  valueRange then still contains
     * its reset sentinels.  Use the current axis limits as the
     * fallback for whichever world endpoint is missing.
     *
     * This preserves the historical behaviour of:
     *
     *     axis create Axis1
     *     axis configure Axis1 -scrollmax 10
     *     axis view Axis1
     *
     * which reports an empty view at 0.
     */
    if (worldMin == DBL_MAX) {
        worldMin = viewMin;
    }
    if (worldMax == -DBL_MAX) {
        worldMax = viewMax;
    }
    if (DEFINED(axisPtr->scrollMin)) {
        worldMin = axisPtr->scrollMin;
    }
    if (DEFINED(axisPtr->scrollMax)) {
        worldMax = axisPtr->scrollMax;
    }
    if ((!FINITE(worldMin)) || (!FINITE(worldMax)) || (!FINITE(viewMin)) || (!FINITE(viewMax))) {
        return FALSE;
    }
    if (axisPtr->logScale) {
        if ((worldMin <= 0.0) || (worldMax <= 0.0) || (viewMin <= 0.0) || (viewMax <= 0.0)) {
            return FALSE;
        }
        worldMin = log10(worldMin);
        worldMax = log10(worldMax);
        viewMin = log10(viewMin);
        viewMax = log10(viewMax);
    }
    /*
     * The world must have a non-zero increasing range, but an unused
     * axis is allowed to have a zero-width current view.
     */
    if (worldMin >= worldMax) {
        return FALSE;
    }
    /*
     * Preserve the historical scrolling semantics: the current viewport
     * is first intersected with the effective scroll region.  This is
     * important when an automatically generated axis range extends
     * outside -scrollmin/-scrollmax.
     *
     * If the entire view lies outside the world, collapse it onto the
     * nearest boundary.
     */
    if (viewMax < worldMin) {
        viewMin = worldMin;
        viewMax = worldMin;
    } else if (viewMin > worldMax) {
        viewMin = worldMax;
        viewMax = worldMax;
    } else {
        if (viewMin < worldMin) {
            viewMin = worldMin;
        }
        if (viewMax > worldMax) {
            viewMax = worldMax;
        }
    }
    SetAxisRange(rangePtr, worldMin, worldMax);
    viewMinNorm = NormalizeAxisValue(rangePtr, viewMin);
    viewMaxNorm = NormalizeAxisValue(rangePtr, viewMax);
    if ((!FINITE(viewMinNorm)) || (!FINITE(viewMaxNorm)) || (viewMinNorm > viewMaxNorm)) {
        return FALSE;
    }
    *viewMinNormPtr = viewMinNorm;
    *viewMaxNormPtr = viewMaxNorm;
    return TRUE;
}

/**
 * @ingroup axis_presentation
 * @brief Draw a mapped axis and queue its scrollbar update.
 *
 * Borrows prepared axis geometry. Draws its optional screen border, title, labels and segments through
 * renderer interfaces, with native segment fallback. Screen strokes use projecting caps. When valid
 * scroll geometry exists, updates live min/max and queues scrollbar fractions; the Tcl callback is
 * deferred. Ends all renderer contexts before returning.
 */
static void DrawAxis(Graph *graphPtr, Drawable drawable, Axis *axisPtr) {
    Rbc_RenderContext *text = Rbc_RenderBeginScreenText(graphPtr->tkwin, drawable);
    if (axisPtr->border != NULL) {
        Rbc_RenderScreenBorder(
            graphPtr->tkwin, drawable, axisPtr->border, axisPtr->region.left + graphPtr->plotBorderWidth,
            axisPtr->region.top + graphPtr->plotBorderWidth, axisPtr->region.right - axisPtr->region.left,
            axisPtr->region.bottom - axisPtr->region.top, axisPtr->borderWidth, axisPtr->relief, 1);
    }
    if (axisPtr->title != NULL) {
        Rbc_RenderText(text, axisPtr->title, &axisPtr->titleTextStyle, (int)axisPtr->titlePos.x,
                     (int)axisPtr->titlePos.y);
    }
    if (axisPtr->scrollCmdObjPtr != NULL) {
        AxisRange scrollRange;
        double first;
        double last;
        double windowSize;
        double fract;
        double newMin;
        double newMax;
        int isHoriz;

        if (GetAxisScrollState(axisPtr, &scrollRange, &first, &last)) {
            windowSize = last - first;
            isHoriz = AxisIsHorizontal(graphPtr, axisPtr);
            /*
             * Scrollbar coordinates always increase from left-to-right
             * or top-to-bottom.  Convert the mapped data interval into
             * that orientation.
             */
            if (isHoriz != axisPtr->descending) {
                fract = first;
            } else {
                fract = 1.0 - last;
            }
            fract = AdjustViewport(fract, windowSize);
            if (isHoriz != axisPtr->descending) {
                first = fract;
                last = fract + windowSize;
            } else {
                last = 1.0 - fract;
                first = last - windowSize;
            }
            newMin = InterpolateAxisValue(&scrollRange, first);
            newMax = InterpolateAxisValue(&scrollRange, last);
            if (axisPtr->logScale) {
                newMin = LogAxisValue(newMin);
                newMax = LogAxisValue(newMax);
            }
            /*
             * Never let malformed or numerically unrepresentable scroll
             * geometry poison the live axis with NaN/Inf.
             */
            if (FINITE(newMin) && FINITE(newMax) && (newMin < newMax)) {
                axisPtr->min = newMin;
                axisPtr->max = newMax;
                UpdateAxisScrollbar(graphPtr, axisPtr, fract, fract + windowSize);
            }
        }
    }
    if (axisPtr->showTicks) {
        Rbc_ChainLink *linkPtr;
        TickLabel *labelPtr;

        for (linkPtr = Rbc_ChainFirstLink(axisPtr->tickLabels); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            /* Draw major tick labels */
            labelPtr = Rbc_ChainGetValue(linkPtr);
            Rbc_RenderText(text, labelPtr->string, &axisPtr->tickTextStyle,
                         (int)labelPtr->anchorPos.x, (int)labelPtr->anchorPos.y);
        }
    }
    if ((axisPtr->nSegments > 0) && (axisPtr->lineWidth > 0)) {
        /* Draw the tick marks and axis line. */
        Rbc_RenderContext *ctx = Rbc_RenderBeginDrawable(graphPtr, drawable, graphPtr->width, graphPtr->height,
                                                         axisPtr->tickTextStyle.color, axisPtr->lineWidth, NULL, NULL);
        if (ctx != NULL) {
            Rbc_RenderLineStyle(ctx, CapProjecting, JoinMiter);
            Rbc_RenderSegments(ctx, axisPtr->segments, axisPtr->nSegments);
            Rbc_RenderEnd(ctx);
        } else {
            Rbc_RenderScreenSegments(graphPtr->tkwin, drawable, axisPtr->tickGC, axisPtr->segments, axisPtr->nSegments);
        }
    }
    Rbc_RenderEnd(text);
}

/**
 * @ingroup axis_presentation
 * @brief Export mapped title, labels and strokes through the shared renderer.
 *
 * Borrows the export context and axis geometry; closes contexts it creates. Uses butt caps for
 * exported segments. Unlike DrawAxis, emits no axis border and performs no scrollbar update. Does not
 * reformat labels or refresh layout.
 */
static void AxisExport(Rbc_ExportContext *exportPtr, Axis *axisPtr) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);

    if (axisPtr->title != NULL) {
        Rbc_RenderText(output, axisPtr->title, &axisPtr->titleTextStyle, axisPtr->titlePos.x,
                             axisPtr->titlePos.y);
    }
    if (axisPtr->showTicks) {
        Rbc_ChainLink *linkPtr;
        TickLabel *labelPtr;

        for (linkPtr = Rbc_ChainFirstLink(axisPtr->tickLabels); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            labelPtr = Rbc_ChainGetValue(linkPtr);
            Rbc_RenderText(output, labelPtr->string, &axisPtr->tickTextStyle, labelPtr->anchorPos.x,
                                 labelPtr->anchorPos.y);
        }
    }
    if ((axisPtr->nSegments > 0) && (axisPtr->lineWidth > 0)) {
        Rbc_RenderContext *ctx;

        ctx = Rbc_RenderBeginExport(exportPtr, axisPtr->tickTextStyle.color, axisPtr->lineWidth, NULL,
                                        CapButt, JoinMiter);
        Rbc_RenderSegments(ctx, axisPtr->segments, axisPtr->nSegments);
        Rbc_RenderEnd(ctx);
    }
    Rbc_RenderEnd(output);
}

/**
 * @ingroup axis_presentation
 * @brief Map a tick into a full plot-spanning grid segment.
 *
 * Uses axis-space coordinates and logical orientation. The varying coordinate uses track-aware
 * mapping; the other dimension spans the current graph plot rectangle. Callers entering a track must
 * establish the desired viewport first.
 */
static void MakeGridLine(Graph *graphPtr, Axis *axisPtr, double value, Segment2D *segPtr) {
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        segPtr->p.y = graphPtr->top;
        segPtr->q.y = graphPtr->bottom;
        segPtr->p.x = segPtr->q.x = MapHorizontalAxisCoordinate(graphPtr, axisPtr, value);
    } else {
        segPtr->p.x = graphPtr->left;
        segPtr->q.x = graphPtr->right;
        segPtr->p.y = segPtr->q.y = MapVerticalAxisCoordinate(graphPtr, axisPtr, value);
    }
}

/**
 * @ingroup axis_presentation
 * @brief Allocate in-range grid segments for an optional axis.
 *
 * Initializes outputs to NULL and zero; NULL axes produce no segments. Borrows existing tick arrays or
 * generates temporary ones without installing them on the axis. Includes major ticks and optionally
 * minor ticks, using checked capacity arithmetic and attempted segment allocation. Failure or empty
 * output leaves NULL/zero. Frees temporary ticks on all exits. Successful segments belong to the
 * caller and must be freed with ckfree.
 */
void Rbc_GetAxisSegments(Graph *graphPtr, Axis *axisPtr, int minorGrid, Segment2D **segPtrPtr, Tcl_Size *nSegmentsPtr) {
    Ticks *t1Ptr;
    Ticks *t2Ptr;
    Segment2D *segments;
    Segment2D *segPtr;
    Tcl_Size needed;
    Tcl_Size nSegments;
    Tcl_Size i;
    int freeT1;
    int freeT2;

    *segPtrPtr = NULL;
    *nSegmentsPtr = 0;
    if (axisPtr == NULL) {
        return;
    }
    t1Ptr = axisPtr->t1Ptr;
    freeT1 = FALSE;
    if (t1Ptr == NULL) {
        t1Ptr = GenerateTicks(&axisPtr->majorSweep);
        freeT1 = TRUE;
    }
    t2Ptr = axisPtr->t2Ptr;
    freeT2 = FALSE;
    if (t2Ptr == NULL) {
        t2Ptr = GenerateTicks(&axisPtr->minorSweep);
        freeT2 = TRUE;
    }
    needed = t1Ptr->nTicks;
    if (minorGrid) {
        if ((t2Ptr->nTicks > 0) && (t1Ptr->nTicks > (TCL_SIZE_MAX - needed) / t2Ptr->nTicks)) {
            goto cleanup;
        }
        needed += t1Ptr->nTicks * t2Ptr->nTicks;
    }
    if (needed <= 0) {
        goto cleanup;
    }
    {
        size_t segmentBytes;

        if ((uintmax_t)needed > (uintmax_t)(SIZE_MAX / sizeof(*segments))) {
            goto cleanup;
        }
        segmentBytes = (size_t)needed * sizeof(*segments);
        segments = Tcl_AttemptAlloc(segmentBytes);
    }
    if (segments == NULL) {
        goto cleanup;
    }
    segPtr = segments;
    for (i = 0; i < t1Ptr->nTicks; i++) {
        double value;

        value = t1Ptr->values[i];
        if (minorGrid) {
            Tcl_Size j;

            for (j = 0; j < t2Ptr->nTicks; j++) {
                double subValue;

                if (GetMinorTickPosition(axisPtr, t1Ptr, i, t2Ptr->values[j], &subValue) &&
                    InRange(subValue, &axisPtr->axisRange)) {
                    MakeGridLine(graphPtr, axisPtr, subValue, segPtr);
                    segPtr++;
                }
            }
        }
        if (InRange(value, &axisPtr->axisRange)) {
            MakeGridLine(graphPtr, axisPtr, value, segPtr);
            segPtr++;
        }
    }
    nSegments = (Tcl_Size)(segPtr - segments);
    assert(nSegments <= needed);
    if (nSegments > 0) {
        *segPtrPtr = segments;
        *nSegmentsPtr = nSegments;
    } else {
        ckfree(segments);
    }

cleanup:
    if (freeT1) {
        ckfree(t1Ptr);
    }
    if (freeT2) {
        ckfree(t2Ptr);
    }
}

/**
 * @ingroup axis_presentation
 * @brief Rebuild labels and measure the perpendicular axis thickness.
 *
 * Frees old labels, refreshes generated ticks when shown, filters major-label positions and allocates
 * formatted labels. Stops immediately on formatter invalidation; callers must check graph callback
 * validity before continuing. Measures multiline/rotated labels, baseline, ticks and title using wide
 * size arithmetic, then stores clamped width or height for the orientation. Does not assign final
 * label anchors.
 */
static void GetAxisGeometry(Graph *graphPtr, Axis *axisPtr) {
    Tcl_WideInt height;

    FreeLabels(axisPtr->tickLabels);
    height = 0;
    if (axisPtr->lineWidth > 0) {
        /* Leave room for axis baseline (and pad) */
        height += (Tcl_WideInt)axisPtr->lineWidth + 2;
    }
    if (axisPtr->showTicks) {
        Tcl_WideInt pad;
        Tcl_Size i;
        int lw, lh;
        double x, x2;
        int maxWidth, maxHeight;
        TickLabel *labelPtr;

        SweepTicks(axisPtr);
        maxHeight = maxWidth = 0;
        for (i = 0; i < axisPtr->t1Ptr->nTicks; i++) {
            x2 = x = axisPtr->t1Ptr->values[i];
            if (axisPtr->labelOffset) {
                x2 += axisPtr->majorSweep.step * 0.5;
            }
            if (!InRange(x2, &axisPtr->axisRange)) {
                continue;
            }
            labelPtr = Rbc_AllocAxisTickLabel(graphPtr, axisPtr, x);
            if (labelPtr == NULL) {
                return;
            }
            Rbc_ChainAppend(axisPtr->tickLabels, labelPtr);
            /*
             * Get the dimensions of each tick label.
             * Remember tick labels can be multi-lined and/or rotated.
             */
            Rbc_GetTextExtents(&axisPtr->tickTextStyle, labelPtr->string, &lw, &lh);
            labelPtr->width = lw;
            labelPtr->height = lh;
            if (axisPtr->tickTextStyle.theta > 0.0) {
                double rotWidth, rotHeight;

                Rbc_GetBoundingBox(lw, lh, axisPtr->tickTextStyle.theta, &rotWidth, &rotHeight, (Point2D *)NULL);
                lw = AxisRotatedSize(rotWidth);
                lh = AxisRotatedSize(rotHeight);
            }
            if (maxWidth < lw) {
                maxWidth = lw;
            }
            if (maxHeight < lh) {
                maxHeight = lh;
            }
        }
        /* Because the axis cap style is "CapProjecting", we need to
         * account for an extra 1.5 linewidth at the end of each
         * line.  */
        pad = ((Tcl_WideInt)axisPtr->lineWidth * 15) / 10;
        if (AxisIsHorizontal(graphPtr, axisPtr)) {
            height += (Tcl_WideInt)maxHeight + pad;
        } else {
            height += (Tcl_WideInt)maxWidth + pad;
        }
        if (axisPtr->lineWidth > 0) {
            /* Distance from axis line to tick label. */
            height += AXIS_TITLE_PAD;
            if (axisPtr->tickLength < 0) {
                height -= (Tcl_WideInt)axisPtr->tickLength;
            } else {
                height += (Tcl_WideInt)axisPtr->tickLength;
            }
        }
    }
    if (axisPtr->title != NULL) {
        if (axisPtr->titleAlternate) {
            if (height < axisPtr->titleHeight) {
                height = axisPtr->titleHeight;
            }
        } else {
            height += (Tcl_WideInt)axisPtr->titleHeight + AXIS_TITLE_PAD;
        }
    }
    /* Correct for orientation of the axis. */
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        axisPtr->height = LayoutSize(height);
    } else {
        axisPtr->width = LayoutSize(height);
    }
}

/**
 * @ingroup axis_presentation
 * @brief Measure visible axes and alternate-title requirements in one margin.
 *
 * Optionally refreshes axis geometry when GET_AXIS_GEOMETRY is set, stopping with zero if a formatter
 * invalidates layout. Stores visible count, thickness, title length and axesOffset. Track-owned Y
 * margins reserve the maximum summed thickness of any track rather than summing all tracks. Applies
 * minimum margin dimensions and clamps wide sums; the return value is not by itself a callback-
 * validity indicator.
 */
static int GetMarginGeometry(Graph *graphPtr, Margin *marginPtr) {
    Rbc_ChainLink *linkPtr;
    Axis *axisPtr;
    Tcl_WideInt width, height;
    Tcl_WideInt length;
    int isHoriz;
    Tcl_Size count;

    isHoriz = HORIZMARGIN(marginPtr);
    /* Count the number of visible axes. */
    count = 0;
    length = width = height = 0;
    for (linkPtr = Rbc_ChainFirstLink(marginPtr->axes); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        axisPtr = Rbc_ChainGetValue(linkPtr);
        if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
            if (count == TCL_SIZE_MAX) {
                Tcl_Panic("GetMarginGeometry: visible axis count overflow");
            }
            count++;
            if (graphPtr->flags & GET_AXIS_GEOMETRY) {
                GetAxisGeometry(graphPtr, axisPtr);
                if (!Rbc_GraphCallbackValid(graphPtr)) {
                    return 0;
                }
            }
            if ((axisPtr->titleAlternate) && (length < axisPtr->titleWidth)) {
                length = axisPtr->titleWidth;
            }
            if (isHoriz) {
                height += axisPtr->height;
            } else {
                width += axisPtr->width;
            }
        }
    }
    /* Tracks share the left (or inverted bottom) margin: reserve the maximum
     * per-track thickness. Axes within a track still occupy separate bands. */
    if (graphPtr->trackGraph && marginPtr == &graphPtr->margins[graphPtr->inverted ? MARGIN_BOTTOM : MARGIN_LEFT]) {
        RbcTrack *track;
        Tcl_WideInt maximum = 0;
        for (track = graphPtr->tracks; track != NULL; track = track->next) {
            Tcl_WideInt rowWidth = 0;
            Tcl_Size i;
            for (i = 0; i < track->nAxes; i++) {
                if (!track->axes[i]->hidden) {
                    rowWidth += isHoriz ? track->axes[i]->height : track->axes[i]->width;
                }
            }
            maximum = MAX(maximum, rowWidth);
        }
        if (isHoriz) {
            height = maximum;
        } else {
            width = maximum;
        }
    }
    /* Enforce a minimum size for margins. */
    if (width < 3) {
        width = 3;
    }
    if (height < 3) {
        height = 3;
    }
    marginPtr->nAxes = count;
    marginPtr->axesTitleLength = LayoutSize(length);
    marginPtr->width = LayoutSize(width);
    marginPtr->height = LayoutSize(height);
    marginPtr->axesOffset = HORIZMARGIN(marginPtr) ? marginPtr->height : marginPtr->width;
    return marginPtr->axesOffset;
}

/**
 * @ingroup axis_layout_control
 * @brief Compute a physical plot ratio for equal Polar data-unit scaling.
 *
 * Requires a Polar widget with automatic aspect enabled, a grid and two nonlogarithmic mapped axes
 * with finite positive ranges. Uses the grid-selected axes rather than primary x/y. Returns X/Y range
 * ratio, or its inverse when inverted, only if finite and positive. FALSE leaves the output untouched;
 * performs no layout.
 */
static int GetPolarAutoAspect(Graph *graphPtr, double *aspectPtr) {
    Grid *gridPtr;
    Axis *xAxisPtr;
    Axis *yAxisPtr;
    double xRange;
    double yRange;
    double aspect;

    if ((graphPtr->classUid != rbcPolarElementUid) || (!graphPtr->polarAutoAspect)) {
        return FALSE;
    }
    gridPtr = graphPtr->gridPtr;
    if (gridPtr == NULL) {
        return FALSE;
    }
    /*
     * The Polar/Smith representation follows the axes selected by
     * grid -mapx/-mapy, not necessarily the primary x/y axes.
     */
    xAxisPtr = gridPtr->axes.x;
    yAxisPtr = gridPtr->axes.y;
    if ((xAxisPtr == NULL) || (yAxisPtr == NULL) || xAxisPtr->logScale || yAxisPtr->logScale) {
        return FALSE;
    }
    /*
     * Use the actual mapped ranges.  These are the same ranges used
     * by Rbc_HMap/Rbc_VMap, and therefore include autoscaling,
     * requested limits, loose/tight handling, etc.
     */
    xRange = xAxisPtr->axisRange.range;
    yRange = yAxisPtr->axisRange.range;
    if ((!FINITE(xRange)) || (!FINITE(yRange)) || (xRange <= 0.0) || (yRange <= 0.0)) {
        return FALSE;
    }
    /*
     * Without -invertxy:
     *
     *     hRange / xRange == vRange / yRange
     *
     * therefore:
     *
     *     hRange / vRange == xRange / yRange
     *
     * With -invertxy, the two data dimensions swap their physical
     * screen directions.
     */
    if (graphPtr->inverted) {
        aspect = yRange / xRange;
    } else {
        aspect = xRange / yRange;
    }
    if ((!FINITE(aspect)) || (aspect <= 0.0)) {
        return FALSE;
    }
    *aspectPtr = aspect;
    return TRUE;
}

/**
 * @ingroup axis_layout_control
 * @brief Combine axis, title, legend and aspect requirements into margins.
 *
 * Measures each margin and checks callback validity immediately afterward. Maps the legend against the
 * estimated cavity, adds margin-hosted legend dimensions, applies automatic Polar or fixed aspect,
 * reserves adjoining alternate-title space and finally applies positive requested margin overrides.
 * Uses wide intermediate sizes and clamped storage. Callback invalidation returns early; callers must
 * not treat partial geometry as completed layout.
 */
static void ComputeMargins(Graph *graphPtr) {
    Tcl_WideInt left, right, top, bottom;
    Tcl_WideInt width, height;
    Tcl_WideInt insets;

    /*
     * Step 1:    Compute the amount of space needed to display the
     *        axes (there many be 0 or more) associated with the
     *        margin.
     */
    top = GetMarginGeometry(graphPtr, &graphPtr->topMargin);
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        return;
    }
    bottom = GetMarginGeometry(graphPtr, &graphPtr->bottomMargin);
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        return;
    }
    left = GetMarginGeometry(graphPtr, &graphPtr->leftMargin);
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        return;
    }
    right = GetMarginGeometry(graphPtr, &graphPtr->rightMargin);
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        return;
    }
    /*
     * Step 2:  Add the graph title height to the top margin.
     */
    if (graphPtr->title != NULL) {
        top += graphPtr->titleTextStyle.height;
    }
    insets = 2 * ((Tcl_WideInt)graphPtr->inset + (Tcl_WideInt)graphPtr->plotBorderWidth);
    /*
     * Step 3:  Use the current estimate of the plot area to compute
     *        the legend size.  Add it to the proper margin.
     */
    width = graphPtr->width - (insets + left + right);
    height = graphPtr->height - (insets + top + bottom);
    Rbc_MapLegend(graphPtr->legend, LayoutSize(width), LayoutSize(height));
    if (!Rbc_LegendIsHidden(graphPtr->legend)) {
        switch (Rbc_LegendSite(graphPtr->legend)) {
        case LEGEND_RIGHT:
            right += (Tcl_WideInt)Rbc_LegendWidth(graphPtr->legend) + 2;
            break;
        case LEGEND_LEFT:
            left += (Tcl_WideInt)Rbc_LegendWidth(graphPtr->legend) + 2;
            break;
        case LEGEND_TOP:
            top += (Tcl_WideInt)Rbc_LegendHeight(graphPtr->legend) + 2;
            break;
        case LEGEND_BOTTOM:
            bottom += (Tcl_WideInt)Rbc_LegendHeight(graphPtr->legend) + 2;
            break;
        case LEGEND_XY:
        case LEGEND_PLOT:
        case LEGEND_WINDOW:
            /* Do nothing. */
            break;
        }
    }
    /*
     * Recompute the plotarea, now accounting for the legend.
     */
    width = graphPtr->width - (insets + left + right);
    height = graphPtr->height - (insets + top + bottom);
    /*
     * Step 5: If necessary, correct the plot area aspect.
     *
     * Polar/Smith widgets use an automatically derived aspect by default
     * so that one X data unit and one Y data unit occupy the same physical
     * distance.  An explicitly configured -aspect retains the traditional
     * fixed plot-area W/H semantics.
     */
    {
        double autoAspect;

        if ((width > 0) && (height > 0) && GetPolarAutoAspect(graphPtr, &autoAspect)) {
            int padX;
            int padY;
            int mappedWidth;
            int mappedHeight;

            padX = PADDING(graphPtr->padX);
            padY = PADDING(graphPtr->padY);
            mappedWidth = width - padX;
            mappedHeight = height - padY;
            if ((mappedWidth > 0) && (mappedHeight > 0)) {
                double ratio;

                ratio = (double)mappedWidth / (double)mappedHeight;
                if (ratio > autoAspect) {
                    int scaledWidth;
                    /*
                     * Preserve:
                     *
                     *     hRange / vRange == autoAspect
                     *
                     * and then add the X plot padding back to obtain the
                     * outer plot-area width.
                     */
                    scaledWidth = padX + (int)((double)mappedHeight * autoAspect + 0.5);
                    if (scaledWidth <= padX) {
                        scaledWidth = padX + 1;
                    }
                    if (scaledWidth < width) {
                        right += width - scaledWidth;
                    }
                } else {
                    int scaledHeight;

                    scaledHeight = padY + (int)((double)mappedWidth / autoAspect + 0.5);
                    if (scaledHeight <= padY) {
                        scaledHeight = padY + 1;
                    }
                    if (scaledHeight < height) {
                        top += height - scaledHeight;
                    }
                }
            }
        } else if ((graphPtr->aspect > 0.0) && (width > 0) && (height > 0)) {
            double ratio;

            /*
             * Explicit/fixed aspect semantics.  Leave the historical code
             * unchanged.
             */
            ratio = (double)width / (double)height;
            if (ratio > graphPtr->aspect) {
                int scaledWidth;

                scaledWidth = (int)((double)height * graphPtr->aspect);
                if (scaledWidth < 1) {
                    scaledWidth = 1;
                }
                right += width - scaledWidth;
            } else {
                int scaledHeight;

                scaledHeight = (int)((double)width / graphPtr->aspect);
                if (scaledHeight < 1) {
                    scaledHeight = 1;
                }
                top += height - scaledHeight;
            }
        }
    }
    /*
     * Step 6:    If there's multiple axes in a margin, the axis
     *        titles will be displayed in the adjoining marging.
     *        Make sure there's room for the longest axis titles.
     */
    if (top < graphPtr->leftMargin.axesTitleLength) {
        top = graphPtr->leftMargin.axesTitleLength;
    }
    if (right < graphPtr->bottomMargin.axesTitleLength) {
        right = graphPtr->bottomMargin.axesTitleLength;
    }
    if (top < graphPtr->rightMargin.axesTitleLength) {
        top = graphPtr->rightMargin.axesTitleLength;
    }
    if (right < graphPtr->topMargin.axesTitleLength) {
        right = graphPtr->topMargin.axesTitleLength;
    }
    /*
     * Step 7:  Override calculated values with requested margin
     *        sizes.
     */
    graphPtr->leftMargin.width = LayoutSize(left);
    graphPtr->rightMargin.width = LayoutSize(right);
    graphPtr->topMargin.height = LayoutSize(top);
    graphPtr->bottomMargin.height = LayoutSize(bottom);
    if (graphPtr->leftMargin.reqSize > 0) {
        graphPtr->leftMargin.width = graphPtr->leftMargin.reqSize;
    }
    if (graphPtr->rightMargin.reqSize > 0) {
        graphPtr->rightMargin.width = graphPtr->rightMargin.reqSize;
    }
    if (graphPtr->topMargin.reqSize > 0) {
        graphPtr->topMargin.height = graphPtr->topMargin.reqSize;
    }
    if (graphPtr->bottomMargin.reqSize > 0) {
        graphPtr->bottomMargin.height = graphPtr->bottomMargin.reqSize;
    }
}

/**
 * @ingroup axis_layout_control
 * @brief Compute plot bounds, mapping scales and graph-title position.
 *
 * Calls ComputeMargins and stops on callback invalidation. Adds widget/plot insets, computes the
 * cavity with wide arithmetic and clamps it to at least one pixel. Plot padding defines mapping
 * offsets/ranges; stores reciprocals and title position. A one-pixel arithmetic range is not a
 * drawable-layout guarantee. Does not map axes or clear the caller layout flags.
 */
void Rbc_LayoutMargins(Graph *graphPtr) {
    Tcl_WideInt width, height;
    Tcl_WideInt left, right;
    Tcl_WideInt top, bottom;
    Tcl_WideInt value;

    ComputeMargins(graphPtr);
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        return;
    }
    left =
        (Tcl_WideInt)graphPtr->leftMargin.width + (Tcl_WideInt)graphPtr->inset + (Tcl_WideInt)graphPtr->plotBorderWidth;
    right = (Tcl_WideInt)graphPtr->rightMargin.width + (Tcl_WideInt)graphPtr->inset +
            (Tcl_WideInt)graphPtr->plotBorderWidth;
    top =
        (Tcl_WideInt)graphPtr->topMargin.height + (Tcl_WideInt)graphPtr->inset + (Tcl_WideInt)graphPtr->plotBorderWidth;
    bottom = (Tcl_WideInt)graphPtr->bottomMargin.height + (Tcl_WideInt)graphPtr->inset +
             (Tcl_WideInt)graphPtr->plotBorderWidth;
    /*
     * Calculate the plotting cavity using wide intermediates.  The
     * drawable geometry itself remains int.
     */
    width = (Tcl_WideInt)graphPtr->width - left - right;
    height = (Tcl_WideInt)graphPtr->height - top - bottom;
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    graphPtr->left = Rbc_ClampToInt(left);
    graphPtr->top = Rbc_ClampToInt(top);
    graphPtr->right = Rbc_ClampToInt(left + width);
    graphPtr->bottom = Rbc_ClampToInt(top + height);
    value = top + (Tcl_WideInt)graphPtr->padTop;
    graphPtr->vOffset = Rbc_ClampToInt(value);
    value = height - PADDING(graphPtr->padY);
    graphPtr->vRange = LayoutRange(value);
    value = left + (Tcl_WideInt)graphPtr->padLeft;
    graphPtr->hOffset = Rbc_ClampToInt(value);
    value = width - PADDING(graphPtr->padX);
    graphPtr->hRange = LayoutRange(value);
    graphPtr->hScale = 1.0 / (double)graphPtr->hRange;
    graphPtr->vScale = 1.0 / (double)graphPtr->vRange;
    value = ((Tcl_WideInt)graphPtr->titleTextStyle.height / 2) + (Tcl_WideInt)graphPtr->inset;
    graphPtr->titleY = Rbc_ClampToInt(value);
    value = (Tcl_WideInt)graphPtr->right + (Tcl_WideInt)graphPtr->left;
    graphPtr->titleX = Rbc_ClampToInt(value / 2);
}

/**
 * @ingroup axis_configuration
 * @brief Validate staged options before committing derived axis resources.
 *
 * Prepares limit, tick, loose-range, pixel, shadow, tag and format transactions as needed. Validates
 * command prefixes without evaluating them. Recoverable errors free prepared resources before any
 * commit. Success normalizes rotation, refreshes styles and title extents, marks axis and graph
 * mapping dirty, and schedules redraw. The caller restores Tk-managed fields on error.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in,out] axisPtr Borrowed axis with candidate Tk fields, mask and original option vector installed by
 *   the saved-options wrapper.
 */
static int ConfigureAxis(Graph *graphPtr, Axis *axisPtr) {
    AxisLimitTransaction limitTransaction;
    AxisTickTransaction tickTransaction;
    AxisLooseTransaction looseTransaction;
    AxisPixelTransaction pixelTransaction;
    AxisShadowTransaction shadowTransaction;
    AxisTagsTransaction tagsTransaction;
    AxisFormatTransaction formatTransaction;
    int limitTransactionPrepared;
    int tickTransactionPrepared;
    int looseTransactionPrepared;
    int pixelTransactionPrepared;
    int shadowTransactionPrepared;
    int tagsTransactionPrepared;
    int formatTransactionPrepared;

    memset(&limitTransaction, 0, sizeof(limitTransaction));
    memset(&tickTransaction, 0, sizeof(tickTransaction));
    memset(&looseTransaction, 0, sizeof(looseTransaction));
    memset(&pixelTransaction, 0, sizeof(pixelTransaction));
    memset(&shadowTransaction, 0, sizeof(shadowTransaction));
    memset(&tagsTransaction, 0, sizeof(tagsTransaction));
    memset(&formatTransaction, 0, sizeof(formatTransaction));
    limitTransactionPrepared = FALSE;
    tickTransactionPrepared = FALSE;
    looseTransactionPrepared = FALSE;
    pixelTransactionPrepared = FALSE;
    shadowTransactionPrepared = FALSE;
    tagsTransactionPrepared = FALSE;
    formatTransactionPrepared = FALSE;
    if (!FINITE(axisPtr->windowSize)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-autorange must be a finite value", -1));
        goto error;
    }
    if (!FINITE(axisPtr->tickTextStyle.theta)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-rotate must be a finite value", -1));
        goto error;
    }
    if (!FINITE(axisPtr->shiftBy)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-shiftby must be a finite value", -1));
        goto error;
    }
    if (!FINITE(axisPtr->reqStep)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("-stepsize must be a finite value", -1));
        goto error;
    }
    if (axisPtr->reqNumMinorTicks > (MAXTICKS + 1)) {
        Tcl_SetObjResult(graphPtr->interp, Tcl_ObjPrintf("-subdivisions is too large: "
                                                         "maximum is %d",
                                                         MAXTICKS + 1));
        goto error;
    }
    /*
     * Parse and validate retained axis-limit objects before modifying the
     * live requested range.
     */
    if (((!axisPtr->optionsConfigured) ||
         (axisPtr->optionMask & (AXIS_LIMITS_MASK | AXIS_SCROLL_LIMITS_MASK | AXIS_LOG_SCALE_MASK)))) {
        if (PrepareAxisLimitTransaction(graphPtr, axisPtr, &limitTransaction) != TCL_OK) {
            goto error;
        }
        limitTransactionPrepared = TRUE;
    }
    /*
     * Parse explicit major and minor tick lists before replacing the live
     * tick allocations.
     */
    if (((!axisPtr->optionsConfigured) || (axisPtr->optionMask & AXIS_TICKS_MASK))) {
        if (PrepareAxisTickTransaction(graphPtr, axisPtr, &tickTransaction) != TCL_OK) {
            goto error;
        }
        tickTransactionPrepared = TRUE;
    }
    /*
     * Parse the independently configurable loose minimum and maximum
     * policies before modifying the live axis.
     */
    if (((!axisPtr->optionsConfigured) || (axisPtr->optionMask & AXIS_LOOSE_MASK))) {
        if (PrepareAxisLooseTransaction(graphPtr, axisPtr, &looseTransaction) != TCL_OK) {
            goto error;
        }
        looseTransactionPrepared = TRUE;
    }
    /*
     * Parse non-negative axis widths and the positive scroll increment
     * before modifying the live Axis record.
     */
    if (((!axisPtr->optionsConfigured) || (axisPtr->optionMask & AXIS_PIXELS_MASK))) {
        if (PrepareAxisPixelTransaction(graphPtr, axisPtr, &pixelTransaction) != TCL_OK) {
            goto error;
        }
        pixelTransactionPrepared = TRUE;
    }
    /*
     * Parse all text shadow resources before replacing any live shadow
     * colour references.
     */
    if (((!axisPtr->optionsConfigured) || (axisPtr->optionMask & AXIS_SHADOW_MASK))) {
        if (PrepareAxisShadowTransaction(graphPtr, axisPtr, &shadowTransaction) != TCL_OK) {
            goto error;
        }
        shadowTransactionPrepared = TRUE;
    }
    /*
     * Parse the axis bind-tags list before replacing the live allocation.
     */
    if (((!axisPtr->optionsConfigured) || (axisPtr->optionMask & AXIS_TAGS_MASK))) {
        if (PrepareAxisTagsTransaction(graphPtr, axisPtr, &tagsTransaction) != TCL_OK) {
            goto error;
        }
        tagsTransactionPrepared = TRUE;
    }
    /*
     * Parse the optional one- or two-element limit-format list before
     * replacing the live allocation.
     */
    if (((!axisPtr->optionsConfigured) || (axisPtr->optionMask & AXIS_LIMITS_FORMAT_MASK))) {
        if (PrepareAxisFormatTransaction(graphPtr, axisPtr, &formatTransaction) != TCL_OK) {
            goto error;
        }
        formatTransactionPrepared = TRUE;
    }
    /*
     * -command is a Tcl command prefix.  Validate its list
     * representation while Tk's option transaction can still be rolled
     * back.
     */
    if ((!axisPtr->optionsConfigured) || (axisPtr->optionMask & AXIS_FORMAT_COMMAND_MASK)) {
        if (ValidateAxisCommandPrefix(graphPtr->interp, axisPtr->formatCmdObjPtr) != TCL_OK) {
            goto error;
        }
    }
    /*
     * -scrollcommand is also a Tcl command prefix.
     */
    if ((!axisPtr->optionsConfigured) || (axisPtr->optionMask & AXIS_SCROLL_COMMAND_MASK)) {
        if (ValidateAxisCommandPrefix(graphPtr->interp, axisPtr->scrollCmdObjPtr) != TCL_OK) {
            goto error;
        }
    }
    /*
     * No operation below this point can report a configuration error.
     */
    if (limitTransactionPrepared) {
        CommitAxisLimitTransaction(axisPtr, &limitTransaction);
    }
    if (tickTransactionPrepared) {
        CommitAxisTickTransaction(axisPtr, &tickTransaction);
    }
    if (looseTransactionPrepared) {
        CommitAxisLooseTransaction(axisPtr, &looseTransaction);
    }
    if (pixelTransactionPrepared) {
        CommitAxisPixelTransaction(axisPtr, &pixelTransaction);
    }
    if (shadowTransactionPrepared) {
        CommitAxisShadowTransaction(axisPtr, &shadowTransaction);
    }
    if (tagsTransactionPrepared) {
        CommitAxisTagsTransaction(axisPtr, &tagsTransaction);
    }
    if (formatTransactionPrepared) {
        CommitAxisFormatTransaction(axisPtr, &formatTransaction);
    }
    axisPtr->tickTextStyle.theta = fmod(axisPtr->tickTextStyle.theta, 360.0);
    if (axisPtr->tickTextStyle.theta < 0.0) {
        axisPtr->tickTextStyle.theta += 360.0;
    }
    ResetTextStyles(graphPtr, axisPtr);
    axisPtr->titleWidth = axisPtr->titleHeight = 0;
    if (axisPtr->title != NULL) {
        int w, h;

        Rbc_GetTextExtents(&axisPtr->titleTextStyle, axisPtr->title, &w, &h);
        axisPtr->titleWidth = w;
        axisPtr->titleHeight = h;
    }
    /*
     * Don't bother to check what configuration options have changed.
     * Almost every option changes the size of the plotting area
     * (except for -color and -titlecolor), requiring the graph and
     * its contents to be completely redrawn.
     *
     * Recompute the scale and offset of the axis in case -min, -max
     * options have changed.
     */
    graphPtr->flags |= REDRAW_WORLD;
    graphPtr->flags |= (MAP_WORLD | RESET_AXES);
    axisPtr->flags |= AXIS_DIRTY;
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;

error:
    if (formatTransactionPrepared) {
        FreeAxisFormatTransaction(&formatTransaction);
    }
    if (tagsTransactionPrepared) {
        FreeAxisTagsTransaction(&tagsTransaction);
    }
    if (shadowTransactionPrepared) {
        FreeAxisShadowTransaction(&shadowTransaction);
    }
    if (tickTransactionPrepared) {
        FreeAxisTickTransaction(&tickTransaction);
    }
    return TCL_ERROR;
}

/**
 * @ingroup axis_registry
 * @brief Allocate a registered axis or revive a pending deletion.
 *
 * Rejects names beginning with a dash and existing live names, returning NULL with an interpreter
 * error. A new record owns a copied name and label chain but is not yet configured or linked to a
 * margin. Revival returns the same record, clears deletePending and invalidates notified limits; it
 * preserves references and existing resources.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] name Borrowed NUL-terminated name copied for a new record; an existing pending-deletion record may
 *   be revived.
 * @param[in] margin Initial margin selector used for defaults; does not insert the axis into a margin chain.
 */
static Axis *CreateAxis(Graph *graphPtr, char *name, int margin) {
    Axis *axisPtr;
    Tcl_HashEntry *hPtr;
    int isNew;

    if (name[0] == '-') {
        Rbc_AppendResultStrings(graphPtr->interp, "name of axis \"", name, "\" can't start with a '-'", (char *)NULL);
        return NULL;
    }
    hPtr = Tcl_CreateHashEntry(&graphPtr->axes.table, name, &isNew);
    if (!isNew) {
        axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        if (!axisPtr->deletePending) {
            Rbc_AppendResultStrings(graphPtr->interp, "axis \"", name, "\" already exists in \"",
                                    Tk_PathName(graphPtr->tkwin), "\"", (char *)NULL);
            return NULL;
        }
        axisPtr->deletePending = FALSE;
        axisPtr->notifiedLimitsValid = FALSE;
    } else {
        axisPtr = RbcCalloc(1, sizeof(Axis));
        assert(axisPtr);
        axisPtr->graphPtr = graphPtr;
        axisPtr->optionTable = NULL;
        axisPtr->optionMask = 0;
        axisPtr->optionObjc = 0;
        axisPtr->optionObjv = NULL;
        axisPtr->optionsConfigured = FALSE;
        axisPtr->optionsInitialized = FALSE;
        axisPtr->tkResourcesReleased = FALSE;
        axisPtr->name = RbcStrdup(name);
        axisPtr->hashPtr = hPtr;
        axisPtr->classUid = NULL;
        axisPtr->looseMin = axisPtr->looseMax = TICK_RANGE_TIGHT;
        axisPtr->reqNumMinorTicks = 2;
        axisPtr->scrollUnits = 10;
        axisPtr->showTicks = TRUE;
        axisPtr->reqMin = axisPtr->reqMax = VALUE_UNDEFINED;
        axisPtr->scrollMin = axisPtr->scrollMax = VALUE_UNDEFINED;
        if ((graphPtr->classUid == rbcBarElementUid) && ((margin == MARGIN_TOP) || (margin == MARGIN_BOTTOM))) {
            axisPtr->reqStep = 1.0;
            axisPtr->reqNumMinorTicks = 0;
        }
        if ((margin == MARGIN_RIGHT) || (margin == MARGIN_TOP)) {
            axisPtr->hidden = TRUE;
        }
        Rbc_InitTextStyle(&axisPtr->titleTextStyle);
        Rbc_InitTextStyle(&axisPtr->limitsTextStyle);
        Rbc_InitTextStyle(&axisPtr->tickTextStyle);
        axisPtr->tickLabels = Rbc_ChainCreate();
        axisPtr->lineWidth = 1;
        axisPtr->tickTextStyle.padX.side1 = 2;
        axisPtr->tickTextStyle.padX.side2 = 2;
        Tcl_SetHashValue(hPtr, axisPtr);
    }
    return axisPtr;
}

/**
 * @ingroup axis_registry
 * @brief Look up a live named axis without retaining it.
 *
 * Returns TCL_OK and a borrowed pointer. Missing and delete-pending axes return TCL_ERROR, set the
 * interpreter result, and clear the output pointer.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] name Borrowed NUL-terminated axis name.
 * @param[out] axisPtrPtr Required output receiving a borrowed live axis on success, or NULL on lookup failure;
 *   no reference is acquired.
 */
static int NameToAxis(Graph *graphPtr, const char *name, Axis **axisPtrPtr) {
    Tcl_HashEntry *hPtr;

    hPtr = Tcl_FindHashEntry(&graphPtr->axes.table, name);
    if (hPtr != NULL) {
        Axis *axisPtr;

        axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        if (!axisPtr->deletePending) {
            *axisPtrPtr = axisPtr;
            return TCL_OK;
        }
    }
    Rbc_AppendResultStrings(graphPtr->interp, "can't find axis \"", name, "\" in \"", Tk_PathName(graphPtr->tkwin),
                            "\"", (char *)NULL);
    *axisPtrPtr = NULL;
    return TCL_ERROR;
}

/**
 * @ingroup axis_registry
 * @brief Look up an axis and optionally acquire a typed reference.
 *
 * A NULL classUid performs a borrowed lookup. Otherwise establishes the logical X/Y class when unused
 * or untyped, rejects a conflicting class, and increments refCount on success. Match each acquired
 * reference with FreeAxis. The output pointer is assigned only on success.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] axisName Borrowed NUL-terminated name of a live axis in this graph.
 * @param[in] classUid Interned logical X/Y class to acquire a reference, or NULL for a borrowed lookup.
 * @param[out] axisPtrPtr Required output assigned only on success; borrowed for NULL classUid, otherwise owns
 *   one reference to release with FreeAxis.
 */
static int GetAxis(Graph *graphPtr, const char *axisName, Rbc_Uid classUid, Axis **axisPtrPtr) {
    Axis *axisPtr;

    if (NameToAxis(graphPtr, axisName, &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (classUid != NULL) {
        if ((axisPtr->refCount == 0) || (axisPtr->classUid == NULL)) {
            /* Set the axis type on the first use of it. */
            axisPtr->classUid = classUid;
        } else if (axisPtr->classUid != classUid) {
            Rbc_AppendResultStrings(graphPtr->interp, "axis \"", axisName, "\" is already in use on an opposite ",
                                    axisPtr->classUid, "-axis", (char *)NULL);
            return TCL_ERROR;
        }
        axisPtr->refCount++;
    }
    *axisPtrPtr = axisPtr;
    return TCL_OK;
}

/**
 * @ingroup axis_registry
 * @brief Release one acquired axis reference.
 *
 * Requires a positive reference count. Destroys the record when the count reaches zero and deletion is
 * pending; an unreferenced live axis remains registered.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in,out] axisPtr Axis representing one acquired reference with positive refCount; may be destroyed when
 *   this reference is released.
 */
static void FreeAxis(Graph *graphPtr, Axis *axisPtr) {
    axisPtr->refCount--;
    if ((axisPtr->deletePending) && (axisPtr->refCount == 0)) {
        DestroyAxis(graphPtr, axisPtr);
    }
}

/**
 * @ingroup axis_registry
 * @brief Resolve an optional Tcl axis name with the requested reference policy.
 *
 * A NULL or empty object returns a NULL axis only when allowEmpty is true; otherwise it reports
 * TCL_ERROR. Nonempty names delegate to GetAxis, including its typed-reference ownership and output-
 * on-success rules.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in] objPtr Borrowed axis-name object, or NULL/empty subject to allowEmpty.
 * @param[in] classUid Interned logical X/Y class to acquire a reference, or NULL for a borrowed lookup.
 * @param[in] allowEmpty Nonzero permits a NULL or empty name to resolve successfully to NULL.
 * @param[out] axisPtrPtr Required output; empty names set NULL, successful typed lookup acquires one reference,
 *   and nonempty lookup errors leave it unchanged.
 */
int Rbc_GetAxisFromObj(Graph *graphPtr, Tcl_Obj *objPtr, Rbc_Uid classUid, int allowEmpty, Axis **axisPtrPtr) {
    if ((objPtr == NULL) || (Tcl_GetCharLength(objPtr) == 0)) {
        if (!allowEmpty) {
            Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj("axis name may not be empty", -1));
            *axisPtrPtr = NULL;
            return TCL_ERROR;
        }
        *axisPtrPtr = NULL;
        return TCL_OK;
    }
    return GetAxis(graphPtr, Tcl_GetString(objPtr), classUid, axisPtrPtr);
}

/**
 * @ingroup axis_registry
 * @brief Release an optional retained axis reference.
 *
 * Accepts NULL. A non-NULL argument must represent an acquired reference; release may destroy a
 * delete-pending record.
 *
 * @param[in,out] graphPtr Borrowed owning graph supplying the registry, interpreter and Tk resource context.
 * @param[in,out] axisPtr Axis representing one acquired reference, or NULL; may become invalid during release.
 */
void Rbc_FreeAxisReference(Graph *graphPtr, Axis *axisPtr) {
    if (axisPtr != NULL) {
        FreeAxis(graphPtr, axisPtr);
    }
}

/**
 * @ingroup axis_registry
 * @brief Tear down the complete graph axis registry.
 *
 * Destroys records regardless of reference counts, then deletes registry and tag tables and axis
 * chains. Clears each record hash pointer before destruction to keep hash iteration valid. Consumers
 * must have been detached by graph teardown; this is not ordinary axis deletion.
 *
 * @param[in,out] graphPtr Graph undergoing final teardown with initialized axis containers and all consumers
 *   detached; registry and chains are destroyed.
 */
void Rbc_DestroyAxes(Graph *graphPtr) {
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Axis *axisPtr;
    int i;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        axisPtr->hashPtr = NULL;
        DestroyAxis(graphPtr, axisPtr);
    }
    Tcl_DeleteHashTable(&graphPtr->axes.table);
    for (i = 0; i < 4; i++) {
        Rbc_ChainDestroy(graphPtr->axisChain[i]);
    }
    Tcl_DeleteHashTable(&graphPtr->axes.tagTable);
    Rbc_ChainDestroy(graphPtr->axes.displayList);
}

/**
 * @ingroup axis_registry
 * @brief Create and configure the four initial margin axes.
 *
 * Creates x, y, x2 and y2 with one reference each and the appropriate logical class. Polar axes start
 * hidden before database options are applied. Success links each configured record to its chain.
 * Failure returns TCL_ERROR with partial initialization left for graph teardown.
 *
 * @param[in,out] graphPtr Graph under construction with initialized axis registry; receives four default axes
 *   and margin chains, including partial state on failure.
 */
int Rbc_DefaultAxes(Graph *graphPtr) {
    static char *axisNames[4] = {"x", "y", "x2", "y2"};

    Axis *axisPtr;
    Rbc_Chain *chainPtr;
    int i;

    for (i = 0; i < 4; i++) {
        chainPtr = Rbc_ChainCreate();
        graphPtr->axisChain[i] = chainPtr;
        /*
         * Create a default axis for each margin.
         */
        axisPtr = CreateAxis(graphPtr, axisNames[i], i);
        if (axisPtr == NULL) {
            return TCL_ERROR;
        }
        /*
         * Polar graphs use the ordinary Cartesian axes internally, but their
         * rectangular axis decoration is not part of the default Polar
         * presentation.
         *
         * Set this before ConfigureNewAxis().  The -hide option has no static
         * default value, so an option-database value may still override this
         * widget-class default.
         */
        if (graphPtr->classUid == rbcPolarElementUid) {
            axisPtr->hidden = TRUE;
        }
        /*
         * Default axes are assumed to be in use and visible on their
         * respective margins.
         */
        axisPtr->refCount = 1;
        axisPtr->classUid = (i & 1) ? rbcYAxisUid : rbcXAxisUid;
        axisPtr->flags |= AXIS_ONSCREEN;
        /*
         * Initialise the modern option table, apply option-database
         * defaults, and construct the derived axis resources.
         */
        if (ConfigureNewAxis(graphPtr, axisPtr, 0, NULL) != TCL_OK) {
            return TCL_ERROR;
        }
        axisPtr->linkPtr = Rbc_ChainAppend(chainPtr, axisPtr);
        axisPtr->chainPtr = chainPtr;
    }
    return TCL_OK;
}

/**
 * @ingroup axis_commands
 * @brief Query or configure bindings for a selected margin axis.
 *
 * Interns the axis name as a binding tag and forwards the already sliced argument vector. Returns the
 * binding subsystem Tcl status; does not evaluate an event binding here.
 */
static int BindOp(Graph *graphPtr, Axis *axisPtr, int margin, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    return Rbc_ConfigureBindingsFromObj(interp, graphPtr->bindTable, MakeAxisTag(graphPtr, axisPtr->name), objc,
                                        objv);
}

/**
 * @ingroup axis_commands
 * @brief Return one initialized axis option value.
 *
 * Requires a live axis and initialized Tk option table. Uses the first sliced argument as the option
 * name and returns TCL_ERROR if Tk cannot resolve it.
 */
static int CgetOp(Graph *graphPtr, Axis *axisPtr, int margin, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *valueObjPtr;

    assert(axisPtr->optionTable != NULL);
    assert(axisPtr->optionsInitialized);
    valueObjPtr = Tk_GetOptionValue(graphPtr->interp, (char *)axisPtr, axisPtr->optionTable, objv[0], graphPtr->tkwin);
    if (valueObjPtr == NULL) {
        return TCL_ERROR;
    }
    Tcl_SetObjResult(graphPtr->interp, valueObjPtr);
    return TCL_OK;
}

/**
 * @ingroup axis_commands
 * @brief Query or configure one resolved axis.
 *
 * Zero or one sliced argument returns Tk option metadata. Option/value pairs use ConfigureAxisOptions.
 * Success additionally invalidates backing and margins for an onscreen axis. This transaction covers
 * one axis, not a multi-axis command.
 */
static int ConfigureOp(Graph *graphPtr, Axis *axisPtr, int margin, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Obj *infoObjPtr;

    assert(axisPtr->optionTable != NULL);
    assert(axisPtr->optionsInitialized);
    if (objc == 0) {
        infoObjPtr = Tk_GetOptionInfo(graphPtr->interp, (char *)axisPtr, axisPtr->optionTable, NULL, graphPtr->tkwin);
        if (infoObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(graphPtr->interp, infoObjPtr);
        return TCL_OK;
    }
    if (objc == 1) {
        infoObjPtr =
            Tk_GetOptionInfo(graphPtr->interp, (char *)axisPtr, axisPtr->optionTable, objv[0], graphPtr->tkwin);
        if (infoObjPtr == NULL) {
            return TCL_ERROR;
        }
        Tcl_SetObjResult(graphPtr->interp, infoObjPtr);
        return TCL_OK;
    }
    if (ConfigureAxisOptions(graphPtr, axisPtr, objc, objv, NULL) != TCL_OK) {
        return TCL_ERROR;
    }
    if (axisPtr->flags & AXIS_ONSCREEN) {
        /*
         * Preserve the conservative redraw behaviour used during
         * activation.
         */
        graphPtr->flags |= REDRAW_BACKING_STORE;
        graphPtr->flags |= DRAW_MARGINS;
        Rbc_EventuallyRedrawGraph(graphPtr);
    }
    return TCL_OK;
}

/**
 * @ingroup axis_layout_control
 * @brief Read prepared mapping bounds in data coordinates.
 *
 * Copies linear axisRange endpoints or converts logarithmic exponents with LogAxisValue. Does not sync
 * vectors, reset ranges or validate conversion results. Callers control range freshness.
 */
static void GetAxisLimits(Axis *axisPtr, double *minPtr, double *maxPtr) {
    if (axisPtr->logScale) {
        *minPtr = LogAxisValue(axisPtr->axisRange.min);
        *maxPtr = LogAxisValue(axisPtr->axisRange.max);
    } else {
        *minPtr = axisPtr->axisRange.min;
        *maxPtr = axisPtr->axisRange.max;
    }
}

/**
 * @ingroup axis_layout_control
 * @brief Emit axis configuration and limit-change events after screen display.
 *
 * Collects names of configured live axes in registry order, comparing data-space limits against a
 * separate notification baseline. Configuration or limit changes contribute to RbcAxisChanged; only
 * limit changes contribute to RbcAxisLimitsChanged. Updates baselines and clears configure flags
 * before sending events, balancing temporary list references. The display caller must exclude
 * temporary export mappings; this function has no GRAPH_EXPORT guard of its own.
 */
void Rbc_NotifyAxisChanges(Graph *graphPtr) {
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Tcl_Obj *changedObjPtr = NULL, *limitsObjPtr = NULL;

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        Axis *axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        double min, max;
        int limitsChanged;

        if (axisPtr->deletePending || !axisPtr->optionsConfigured) {
            continue;
        }
        GetAxisLimits(axisPtr, &min, &max);
        limitsChanged = !axisPtr->notifiedLimitsValid || (min != axisPtr->notifiedMin) ||
                        (max != axisPtr->notifiedMax);
        if (limitsChanged || axisPtr->notifyConfigure) {
            if (changedObjPtr == NULL) {
                changedObjPtr = Tcl_NewListObj(0, NULL);
                Tcl_IncrRefCount(changedObjPtr);
            }
            Tcl_ListObjAppendElement(NULL, changedObjPtr, Tcl_NewStringObj(axisPtr->name, -1));
        }
        if (limitsChanged) {
            if (limitsObjPtr == NULL) {
                limitsObjPtr = Tcl_NewListObj(0, NULL);
                Tcl_IncrRefCount(limitsObjPtr);
            }
            Tcl_ListObjAppendElement(NULL, limitsObjPtr, Tcl_NewStringObj(axisPtr->name, -1));
        }
        axisPtr->notifiedMin = min;
        axisPtr->notifiedMax = max;
        axisPtr->notifiedLimitsValid = TRUE;
        axisPtr->notifyConfigure = FALSE;
    }
    if (changedObjPtr != NULL) {
        Tk_SendVirtualEvent(graphPtr->tkwin, "RbcAxisChanged", changedObjPtr);
        Tcl_DecrRefCount(changedObjPtr);
    }
    if (limitsObjPtr != NULL) {
        Tk_SendVirtualEvent(graphPtr->tkwin, "RbcAxisLimitsChanged", limitsObjPtr);
        Tcl_DecrRefCount(limitsObjPtr);
    }
}

/**
 * @ingroup axis_layout_control
 * @brief Return the two prepared axis limits as Tcl doubles.
 *
 * Synchronizes element vectors and resets pending axis ranges before reading data-space bounds.
 * Returns a two-element minimum/maximum list, not a four-coordinate graph rectangle. Requires a valid
 * borrowed axis and does not perform complete margin layout.
 */
static int LimitsOp(Graph *graphPtr, Axis *axisPtr, int margin, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    double min, max;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    Rbc_SyncElementVectors(graphPtr);
    if (graphPtr->flags & RESET_AXES) {
        Rbc_ResetAxes(graphPtr);
    }
    GetAxisLimits(axisPtr, &min, &max);
    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewDoubleObj(min));
    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewDoubleObj(max));
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/**
 * @ingroup axis_layout_control
 * @brief Convert one integer screen coordinate to an axis value.
 *
 * Synchronizes vectors and resets pending ranges, parses the sliced argument as an int and chooses
 * horizontal/vertical inverse mapping from logical class and inversion. Returns TCL_ERROR for parsing
 * failure or a nonfinite result; success returns a Tcl double. Uses existing mapping geometry rather
 * than forcing complete layout.
 */
static int InvTransformOp(Graph *graphPtr, Axis *axisPtr, int margin, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    int x;    /* Integer window coordinate*/
    double y; /* Real graph coordinate */

    Rbc_SyncElementVectors(graphPtr);
    if (graphPtr->flags & RESET_AXES) {
        Rbc_ResetAxes(graphPtr);
    }
    if (Tcl_GetIntFromObj(interp, objv[0], &x) != TCL_OK) {
        return TCL_ERROR;
    }
    /*
     * Is the axis vertical or horizontal?
     *
     * Check the site where the axis was positioned.  If the axis is
     * virtual, all we have to go on is how it was mapped to an
     * element (using either -mapx or -mapy options).
     */
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        y = InvHMap(graphPtr, axisPtr, (double)x);
    } else {
        y = InvVMap(graphPtr, axisPtr, (double)x);
    }
    if (!FINITE(y)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("inverse axis transform is outside the representable range", -1));
        return TCL_ERROR;
    }
    Tcl_SetObjResult(interp, Tcl_NewDoubleObj(y));
    return TCL_OK;
}

/**
 * @ingroup axis_layout_control
 * @brief Evaluate an axis value and return its integer screen coordinate.
 *
 * Synchronizes vectors and resets pending ranges before evaluating the sliced argument with
 * Tcl_ExprDoubleObj. Rejects nonfinite input, nonpositive logarithmic values and mapped coordinates
 * outside int range. Success truncates toward zero, preserving historical conversion. Uses existing
 * mapping geometry. Expression evaluation can execute Tcl; this handler does not revalidate its
 * borrowed axis after that evaluation.
 */
static int TransformOp(Graph *graphPtr, Axis *axisPtr, int margin, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp;
    double value;
    double screen;

    (void)margin;
    (void)objc;
    interp = graphPtr->interp;
    Rbc_SyncElementVectors(graphPtr);
    if (graphPtr->flags & RESET_AXES) {
        Rbc_ResetAxes(graphPtr);
    }
    if (Tcl_ExprDoubleObj(interp, objv[0], &value) != TCL_OK) {
        return TCL_ERROR;
    }
    if (!FINITE(value)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("axis value must be finite", -1));
        return TCL_ERROR;
    }
    if (axisPtr->logScale && (value <= 0.0)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("logscale axis value must be positive", -1));
        return TCL_ERROR;
    }
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        screen = Rbc_HMap(graphPtr, axisPtr, value);
    } else {
        screen = Rbc_VMap(graphPtr, axisPtr, value);
    }
    /*
     * Window coordinates throughout the graph implementation are int.
     * Validate before making the floating-to-integer conversion.
     */
    if ((!FINITE(screen)) || (screen < (double)INT_MIN) || (screen > (double)INT_MAX)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("transformed axis coordinate is outside the integer range", -1));
        return TCL_ERROR;
    }
    /*
     * Preserve the historical truncation-toward-zero behaviour.
     */
    Tcl_SetObjResult(interp, Tcl_NewIntObj((int)screen));
    return TCL_OK;
}

/**
 * @ingroup axis_commands
 * @brief Query or replace the axes displayed on a margin.
 *
 * Queries return chain order. Replacement chooses logical class using inversion and rejects trackgraph
 * Y arrangement, which belongs to tracks. Validates every replacement name and type before changing
 * any chain. Failure leaves all margins unchanged. Success moves existing links as needed and
 * schedules full layout and redraw.
 */
static int UseOp(Graph *graphPtr, Axis *axisPtr, int margin, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    Rbc_Chain *chainPtr;
    Tcl_Size nNames, i;
    Tcl_Obj **names;
    Rbc_ChainLink *linkPtr;
    Rbc_Uid classUid;

    chainPtr = graphPtr->margins[margin].axes;
    if (objc == 0) {
        Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
        for (linkPtr = Rbc_ChainFirstLink(chainPtr); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            axisPtr = Rbc_ChainGetValue(linkPtr);
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(axisPtr->name, -1));
        }
        Tcl_SetObjResult(interp, resultObj);
        return TCL_OK;
    }
    if ((margin == MARGIN_BOTTOM) || (margin == MARGIN_TOP)) {
        classUid = (graphPtr->inverted) ? rbcYAxisUid : rbcXAxisUid;
    } else {
        classUid = (graphPtr->inverted) ? rbcXAxisUid : rbcYAxisUid;
    }
    if (graphPtr->trackGraph && classUid == rbcYAxisUid) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("use track configure -yaxes to arrange trackgraph Y axes", -1));
        return TCL_ERROR;
    }
    if (Tcl_ListObjGetElements(interp, objv[0], &nNames, &names) != TCL_OK) {
        return TCL_ERROR;
    }
    /* Validate the complete replacement before changing any margin or type.
     * An unused member of this chain will have its type cleared below.
     * No callbacks run between validation and commit.
     */
    for (i = 0; i < nNames; i++) {
        if (NameToAxis(graphPtr, Tcl_GetString(names[i]), &axisPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if ((axisPtr->classUid != NULL) && (axisPtr->classUid != classUid) &&
            !((axisPtr->linkPtr != NULL) && (axisPtr->chainPtr == chainPtr) &&
              (axisPtr->refCount == 0))) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong type axis \"%s\": "
                                                   "can't use %s type axis.",
                                                   axisPtr->name, classUid));
            return TCL_ERROR;
        }
    }
    for (linkPtr = Rbc_ChainFirstLink(chainPtr); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        axisPtr = Rbc_ChainGetValue(linkPtr);
        axisPtr->linkPtr = NULL;
        axisPtr->flags &= ~AXIS_ONSCREEN;
        /* Clear the axis type if it's not currently used.*/
        if (axisPtr->refCount == 0) {
            axisPtr->classUid = NULL;
        }
    }
    Rbc_ChainReset(chainPtr);
    for (i = 0; i < nNames; i++) {
        axisPtr = Tcl_GetHashValue(Tcl_FindHashEntry(&graphPtr->axes.table, Tcl_GetString(names[i])));
        axisPtr->classUid = classUid;
        if (axisPtr->linkPtr != NULL) {
            /* Move the axis from the old margin's "use" list to the new. */
            Rbc_ChainUnlinkLink(axisPtr->chainPtr, axisPtr->linkPtr);
            Rbc_ChainAppendLink(chainPtr, axisPtr->linkPtr);
        } else {
            axisPtr->linkPtr = Rbc_ChainAppend(chainPtr, axisPtr);
        }
        axisPtr->chainPtr = chainPtr;
        axisPtr->flags |= AXIS_ONSCREEN;
    }
    graphPtr->flags |= (GET_AXIS_GEOMETRY | LAYOUT_NEEDED | RESET_AXES);
    /* When any axis changes, we need to layout the entire graph.  */
    graphPtr->flags |= (MAP_WORLD | REDRAW_WORLD);
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/**
 * @ingroup axis_commands
 * @brief List interned axis tags or access one tag binding.
 *
 * With no tag argument returns the tag table in hash order. Otherwise interns the supplied tag and
 * forwards remaining arguments; the tag need not name an existing axis.
 */
static int BindVirtualOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    if (objc == 3) {
        Tcl_HashEntry *hPtr;
        Tcl_HashSearch cursor;
        char *tagName;

        Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
        for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.tagTable, &cursor); hPtr != NULL;
             hPtr = Tcl_NextHashEntry(&cursor)) {
            tagName = Tcl_GetHashKey(&graphPtr->axes.tagTable, hPtr);
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(tagName, -1));
        }
        Tcl_SetObjResult(interp, resultObj);
        return TCL_OK;
    }
    return Rbc_ConfigureBindingsFromObj(interp, graphPtr->bindTable, MakeAxisTag(graphPtr, Tcl_GetString(objv[3])),
                                        objc - 4, objv + 4);
}

/**
 * @ingroup axis_commands
 * @brief Create or revive a named axis and apply its options.
 *
 * Returns the axis name on success. Configuration failure destroys only a newly allocated record.
 * Failed revival restores pending deletion and the previous notification validity while preserving
 * the record and its consumers; ConfigureNewAxis rolls back its option transaction.
 */
static int CreateVirtualOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;
    Tcl_HashEntry *hPtr;
    int revived = FALSE;
    int notifiedLimitsValid = FALSE;

    hPtr = Tcl_FindHashEntry(&graphPtr->axes.table, Tcl_GetString(objv[3]));
    if (hPtr != NULL) {
        axisPtr = Tcl_GetHashValue(hPtr);
        revived = axisPtr->deletePending;
        notifiedLimitsValid = axisPtr->notifiedLimitsValid;
    }

    axisPtr = CreateAxis(graphPtr, Tcl_GetString(objv[3]), MARGIN_NONE);
    if (axisPtr == NULL) {
        return TCL_ERROR;
    }
    if (ConfigureNewAxis(graphPtr, axisPtr, objc - 4, objv + 4) != TCL_OK) {
        goto error;
    }
    Tcl_SetObjResult(graphPtr->interp, Tcl_NewStringObj(axisPtr->name, -1));
    return TCL_OK;

error:
    if (revived) {
        /* Configuration rolled back; preserve consumers of the old record. */
        axisPtr->deletePending = TRUE;
        axisPtr->notifiedLimitsValid = notifiedLimitsValid;
    } else {
        DestroyAxis(graphPtr, axisPtr);
    }
    return TCL_ERROR;
}

/**
 * @ingroup axis_commands
 * @brief Resolve a named axis and delegate its option query.
 *
 * Uses a borrowed live lookup and passes the option suffix to CgetOp. Returns the lookup or option-
 * query Tcl status.
 */
static int CgetVirtualOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;

    if (NameToAxis(graphPtr, Tcl_GetString(objv[3]), &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return CgetOp(graphPtr, axisPtr, /*dummy*/ 0, objc - 4, objv + 4);
}

/**
 * @ingroup axis_commands
 * @brief Apply a shared option suffix to one or more named axes.
 *
 * Validates names before the first option beginning with a dash, then configures them sequentially.
 * Earlier successful axes are not rolled back if a later configuration fails. Query results are
 * replaced by each successive axis result.
 */
static int ConfigureVirtualOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;
    Tcl_Size nNames;
    Tcl_Size nOpts;
    Tcl_Size i;
    Tcl_Obj *const *options;

    /* Figure out where the option value pairs begin */
    objc -= 3;
    objv += 3;
    for (i = 0; i < objc; i++) {
        const char *str = Tcl_GetString(objv[i]);
        if (str[0] == '-') {
            break;
        }
        if (NameToAxis(graphPtr, str, &axisPtr) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    nNames = i;         /* Number of pen names specified */
    nOpts = objc - i;   /* Number of options specified */
    options = objv + i; /* Start of options in objv  */
    for (i = 0; i < nNames; i++) {
        if (NameToAxis(graphPtr, Tcl_GetString(objv[i]), &axisPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (ConfigureOp(graphPtr, axisPtr, /*dummy*/ 0, nOpts, options) != TCL_OK) {
            break;
        }
    }
    if (i < nNames) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @ingroup axis_commands
 * @brief Mark named axes for deletion and destroy unreferenced records.
 *
 * Rejects track-owned axes until detached. Marks an active formatter traversal invalid before
 * deletion. Referenced records remain allocated but disappear from live name lookup. Processes names
 * sequentially, so earlier deletions survive a later error.
 */
static int DeleteVirtualOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size i;
    Axis *axisPtr;

    for (i = 3; i < objc; i++) {
        if (NameToAxis(graphPtr, Tcl_GetString(objv[i]), &axisPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (axisPtr->track != NULL) {
            Tcl_SetObjResult(graphPtr->interp,
                             Tcl_NewStringObj("detach the axis from its track before deleting it", -1));
            return TCL_ERROR;
        }
        if (graphPtr->axisFormatActive) {
            graphPtr->axisFormatInvalid = TRUE;
        }
        axisPtr->deletePending = TRUE;
        if (axisPtr->refCount == 0) {
            DestroyAxis(graphPtr, axisPtr);
        }
    }
    return TCL_OK;
}

/**
 * @ingroup axis_commands
 * @brief Report the current axis name or picked detail.
 *
 * Checks the binding context before interpreting the picked object as an axis. Returns a value only
 * for current or detail with an eligible picked axis; otherwise returns TCL_OK without setting a new
 * result.
 */
static int GetOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    Axis *axisPtr;

    if (PTR2INT(Rbc_GetCurrentContext(graphPtr->bindTable)) != GRAPH_BIND_CONTEXT_AXIS) {
        return TCL_OK;
    }
    axisPtr = (Axis *)Rbc_GetCurrentItem(graphPtr->bindTable);
    /* Report only on axes. */
    if ((axisPtr != NULL) &&
        ((axisPtr->classUid == rbcXAxisUid) || (axisPtr->classUid == rbcYAxisUid) || (axisPtr->classUid == NULL))) {
        const char *s = Tcl_GetString(objv[3]);
        const char c = s[0];
        if ((c == 'c') && (strcmp(s, "current") == 0)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(axisPtr->name, -1));
        } else if ((c == 'd') && (strcmp(s, "detail") == 0)) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj(axisPtr->detail, -1));
        }
    }
    return TCL_OK;
}

/**
 * @ingroup axis_layout_control
 * @brief Resolve a named axis for inverse transformation.
 *
 * Uses live borrowed lookup and forwards the argument suffix to InvTransformOp. Returns the lookup or
 * conversion status; acquires no axis reference.
 */
static int InvTransformVirtualOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;

    if (NameToAxis(graphPtr, Tcl_GetString(objv[3]), &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return InvTransformOp(graphPtr, axisPtr, /*dummy*/ 0, objc - 4, objv + 4);
}

/**
 * @ingroup axis_layout_control
 * @brief Resolve a named axis and return its minimum/maximum pair.
 *
 * Uses live borrowed lookup and delegates synchronization and range reset to LimitsOp. Returns
 * TCL_ERROR for a missing axis; this wrapper does not acquire a reference or force full layout.
 */
static int LimitsVirtualOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;

    if (NameToAxis(graphPtr, Tcl_GetString(objv[3]), &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return LimitsOp(graphPtr, axisPtr, /*dummy*/ 0, objc - 4, objv + 4);
}

/**
 * @ingroup axis_commands
 * @brief List live axis names matching any supplied glob pattern.
 *
 * Skips delete-pending records, returns each matching name once and preserves hash traversal order,
 * not display order. No patterns means all live names.
 */
static int NamesVirtualOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Axis *axisPtr;
    Tcl_Size i;

    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);
    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        if (axisPtr->deletePending) {
            continue;
        }
        if (objc == 3) {
            Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(axisPtr->name, -1));
            continue;
        }
        for (i = 3; i < objc; i++) {
            if (Tcl_StringMatch(axisPtr->name, Tcl_GetString(objv[i]))) {
                Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewStringObj(axisPtr->name, -1));
                break;
            }
        }
    }
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/**
 * @ingroup axis_layout_control
 * @brief Resolve a named axis for forward transformation.
 *
 * Uses live borrowed lookup and forwards the suffix to TransformOp, including its expression-
 * evaluation behavior. Returns the lookup or conversion status without acquiring an axis reference.
 */
static int TransformVirtualOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;

    if (NameToAxis(graphPtr, Tcl_GetString(objv[3]), &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return TransformOp(graphPtr, axisPtr, /*dummy*/ 0, objc - 4, objv + 4);
}

/**
 * @ingroup axis_layout_control
 * @brief Query or change the normalized viewport of one named axis.
 *
 * Parses a mutation request before lookup, derives current scroll state and converts its direction to
 * screen orientation. Queries clamp returned fractions to 0..1. Moving an empty view is a no-op. Pages
 * advance by 90 percent of the view; units use scrollUnits and the current scale, or the full owning-
 * track extent. Validates reconstructed data limits before assigning reqMin/reqMax and scheduling
 * geometry/layout/range reset. Does not synchronize vectors or perform layout itself.
 */
static int ViewOp(Graph *graphPtr, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;
    AxisRange scrollRange;
    Tcl_Interp *interp;
    double first;
    double last;
    double windowSize;
    double fract;
    double scrollUnits;
    double newMin;
    double newMax;
    int forward;
    Rbc_ScrollRequest request;

    interp = graphPtr->interp;
    if (objc > 4 && Rbc_ParseScrollRequest(interp, objc, objv, 4, TRUE, &request) != TCL_OK) {
        return TCL_ERROR;
    }
    if (NameToAxis(graphPtr, Tcl_GetString(objv[3]), &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (!GetAxisScrollState(axisPtr, &scrollRange, &first, &last)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("axis \"%s\" has an invalid scroll region", axisPtr->name));
        return TCL_ERROR;
    }
    windowSize = last - first;
    forward = (AxisIsHorizontal(graphPtr, axisPtr) != axisPtr->descending);
    if (forward) {
        fract = first;
        scrollUnits = (double)axisPtr->scrollUnits * graphPtr->hScale;
    } else {
        fract = 1.0 - last;
        scrollUnits = (double)axisPtr->scrollUnits * graphPtr->vScale;
    }
    if (axisPtr->track != NULL) {
        scrollUnits =
            (double)axisPtr->scrollUnits / MAX(1, AxisIsHorizontal(graphPtr, axisPtr)
                                                      ? axisPtr->track->worldRight - axisPtr->track->worldLeft
                                                      : axisPtr->track->worldBottom - axisPtr->track->worldTop);
    }
    if (objc == 4) {
        Tcl_Obj *resultObj;

        resultObj = Tcl_NewListObj(0, NULL);
        Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewDoubleObj(CLAMP(fract, 0.0, 1.0)));
        Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewDoubleObj(CLAMP(fract + windowSize, 0.0, 1.0)));
        Tcl_SetObjResult(interp, resultObj);
        return TCL_OK;
    }
    if (windowSize <= 0.0) {
        /*
         * There is no established viewport to move yet.  Preserve the
         * historical no-op behaviour for an unused axis.
         */
        return TCL_OK;
    }
    if (request.kind == RBC_SCROLL_MOVETO) {
        fract = request.value;
    } else if (request.kind == RBC_SCROLL_PAGES) {
        fract += request.value * windowSize * 0.9;
    } else {
        fract += request.value * scrollUnits;
    }
    fract = AdjustViewport(fract, windowSize);
    if (forward) {
        first = fract;
        last = fract + windowSize;
    } else {
        last = 1.0 - fract;
        first = last - windowSize;
    }
    newMin = InterpolateAxisValue(&scrollRange, first);
    newMax = InterpolateAxisValue(&scrollRange, last);
    if (axisPtr->logScale) {
        newMin = LogAxisValue(newMin);
        newMax = LogAxisValue(newMax);
    }
    if ((!FINITE(newMin)) || (!FINITE(newMax)) || (newMin >= newMax)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("scroll request produced an invalid range "
                                               "for axis \"%s\"",
                                               axisPtr->name));
        return TCL_ERROR;
    }
    axisPtr->reqMin = newMin;
    axisPtr->reqMax = newMax;
    graphPtr->flags |= (GET_AXIS_GEOMETRY | LAYOUT_NEEDED | RESET_AXES);
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/**
 * @ingroup axis_layout_control
 * @brief Apply local scrolling to selected track axes with limit rollback.
 *
 * An optional -mapy must name a member of the track. Queries use the selected or first axis; mutations
 * apply to all members unless selected. Parses requests and performs graph layout before invoking
 * ViewOp. Saves requested limits and graph flags, restoring them on a later axis error while retaining
 * REDRAW_PENDING because an idle callback may already be scheduled. Frees temporary limit storage and
 * name objects. Borrows the track and axis array; performs no independent reference acquisition.
 */
int Rbc_TrackYViewOp(Graph *graphPtr, RbcTrack *track, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Tcl_Size start = 4, n, i;
    Axis **axes = track->axes;
    Axis *selected = NULL;
    double *oldLimits;
    Tcl_Obj *args[7];
    unsigned int oldFlags;
    int code = TCL_OK;
    if (objc > start && strcmp(Tcl_GetString(objv[start]), "-mapy") == 0) {
        if (objc <= start + 1) {
            goto usage;
        }
        for (i = 0; i < track->nAxes; i++) {
            if (strcmp(track->axes[i]->name, Tcl_GetString(objv[start + 1])) == 0) {
                selected = track->axes[i];
            }
        }
        if (selected == NULL) {
            Tcl_SetObjResult(interp, Tcl_NewStringObj("Y axis must belong to the selected track", -1));
            return TCL_ERROR;
        }
        axes = &selected;
        start += 2;
    }
    n = selected != NULL ? 1 : track->nAxes;
    if (n == 0) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("track has no Y axes", -1));
        return TCL_ERROR;
    }
    if (objc != start) {
        Rbc_ScrollRequest request;
        if (Rbc_ParseScrollRequest(interp, objc, objv, start, FALSE, &request) != TCL_OK) {
            return TCL_ERROR;
        }
    }
    if (Rbc_LayoutGraph(graphPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (objc == start) {
        n = 1;
    }
    oldLimits = ckalloc(sizeof(double) * (size_t)n * 2);
    oldFlags = graphPtr->flags;
    for (i = 0; i < n; i++) {
        oldLimits[2 * i] = axes[i]->reqMin;
        oldLimits[2 * i + 1] = axes[i]->reqMax;
    }
    args[0] = objv[0];
    args[1] = objv[1];
    args[2] = objv[2];
    for (i = start; i < objc; i++) {
        args[4 + i - start] = objv[i];
    }
    for (i = 0; i < n; i++) {
        args[3] = Tcl_NewStringObj(axes[i]->name, -1);
        Tcl_IncrRefCount(args[3]);
        code = ViewOp(graphPtr, 4 + objc - start, args);
        Tcl_DecrRefCount(args[3]);
        if (code != TCL_OK) {
            break;
        }
    }
    if (code != TCL_OK) {
        for (i = 0; i < n; i++) {
            axes[i]->reqMin = oldLimits[2 * i];
            axes[i]->reqMax = oldLimits[2 * i + 1];
        }
        /* Retain the scheduled redraw flag; it may already own an idle callback. */
        graphPtr->flags = oldFlags | (graphPtr->flags & REDRAW_PENDING);
    }
    ckfree(oldLimits);
    return code;
usage:
    Tcl_WrongNumArgs(interp, 4, objv, "?-mapy axis? ?moveto fraction|scroll number units|pages?");
    return TCL_ERROR;
}

/**
 * @ingroup axis_commands
 * @brief Dispatch the named-axis command family.
 *
 * Validates operation and argument counts with the shared operation table, then forwards the complete
 * widget command vector. Handlers resolve names and apply their own lifetime and transaction rules.
 */
int Rbc_VirtualAxisOp(Graph *graphPtr, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const AxisVirtualOpSpec axisOps[] = {
        {{"bind", 3, 6, "axisName sequence command"}, BindVirtualOp},
        {{"cget", 5, 5, "axisName option"}, CgetVirtualOp},
        {{"configure", 4, 0, "axisName ?axisName?... ?option value?..."}, ConfigureVirtualOp},
        {{"create", 4, 0, "axisName ?option value?..."}, CreateVirtualOp},
        {{"delete", 3, 0, "?axisName?..."}, DeleteVirtualOp},
        {{"get", 4, 4, "name"}, GetOp},
        {{"invtransform", 5, 5, "axisName value"}, InvTransformVirtualOp},
        {{"limits", 4, 4, "axisName"}, LimitsVirtualOp},
        {{"names", 3, 0, "?pattern?..."}, NamesVirtualOp},
        {{"transform", 5, 5, "axisName value"}, TransformVirtualOp},
        {{"view", 4, 7,
          "axisName ?moveto fract? "
          "?scroll number what?"},
         ViewOp},
        {{NULL, 0, 0, NULL}, NULL}};
    int index;

    if (Rbc_GetOpIndexFromObj(interp, axisOps, (Tcl_Size)sizeof(axisOps[0]), RBC_OP_ARG2, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    return axisOps[index].proc(graphPtr, objc, objv);
}

/**
 * @ingroup axis_commands
 * @brief Dispatch a margin-axis operation.
 *
 * Validates operation and argument counts, selects the first eligible axis from the margin chain and
 * passes the suffix after the operation. UseOp can query or replace the chain; other handlers require
 * an appropriate selected axis.
 */
int Rbc_AxisOp(Graph *graphPtr, int margin, Tcl_Size objc, Tcl_Obj *const objv[]) {
    static const AxisOpSpec axisOps[] = {{{"bind", 2, 5, "sequence command"}, BindOp},
                                         {{"cget", 4, 4, "option"}, CgetOp},
                                         {{"configure", 3, 0, "?option value?..."}, ConfigureOp},
                                         {{"invtransform", 4, 4, "value"}, InvTransformOp},
                                         {{"limits", 3, 3, ""}, LimitsOp},
                                         {{"transform", 4, 4, "value"}, TransformOp},
                                         {{"use", 3, 4, "?axisName?"}, UseOp},
                                         {{NULL, 0, 0, NULL}, NULL}};

    Axis *axisPtr;
    int index;

    if (Rbc_GetOpIndexFromObj(graphPtr->interp, axisOps, (Tcl_Size)sizeof(axisOps[0]), RBC_OP_ARG2, objc, objv,
                              &index) != TCL_OK) {
        return TCL_ERROR;
    }
    axisPtr = Rbc_GetFirstAxis(graphPtr->margins[margin].axes);
    return axisOps[index].proc(graphPtr, axisPtr, margin, objc - 3, objv + 3);
}

/**
 * @ingroup axis_presentation
 * @brief Map visible margin axes after arranging track viewports.
 *
 * Calls Rbc_LayoutTracks, then maps nonhidden onscreen axes in margin order. Ordinary axes accumulate
 * margin offsets; track axes use per-track offsets and the full world rectangle inside an entered
 * viewport. Skips empty visible tracks and restores the graph viewport after each local axis. Requires
 * current ranges and measured labels.
 */
void Rbc_MapAxes(Graph *graphPtr) {
    Axis *axisPtr;
    Rbc_Chain *chainPtr;
    Rbc_ChainLink *linkPtr;
    int margin;
    int offset;

    Rbc_LayoutTracks(graphPtr);
    for (margin = 0; margin < 4; margin++) {
        chainPtr = graphPtr->margins[margin].axes;
        offset = 0;
        for (linkPtr = Rbc_ChainFirstLink(chainPtr); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            axisPtr = Rbc_ChainGetValue(linkPtr);
            if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
                if (axisPtr->track != NULL) {
                    RbcTrackViewport saved;
                    if (axisPtr->track->right <= axisPtr->track->left ||
                        axisPtr->track->bottom <= axisPtr->track->top) {
                        continue;
                    }
                    Rbc_EnterTrack(graphPtr, axisPtr->track, &saved);
                    graphPtr->left = axisPtr->track->worldLeft;
                    graphPtr->right = axisPtr->track->worldRight;
                    graphPtr->top = axisPtr->track->worldTop;
                    graphPtr->bottom = axisPtr->track->worldBottom;
                    MapAxis(graphPtr, axisPtr, axisPtr->track->axisOffset, margin);
                    axisPtr->track->axisOffset +=
                        AxisIsHorizontal(graphPtr, axisPtr) ? axisPtr->height : axisPtr->width;
                    Rbc_LeaveTrack(graphPtr, &saved);
                    continue;
                }
                MapAxis(graphPtr, axisPtr, offset, margin);
                if (AxisIsHorizontal(graphPtr, axisPtr)) {
                    offset += axisPtr->height;
                } else {
                    offset += axisPtr->width;
                }
            }
        }
    }
}

/**
 * @ingroup axis_presentation
 * @brief Check whether both plot ranges permit mapped presentation.
 *
 * Returns true only when horizontal and vertical ranges exceed one pixel. Prevents drawing, exporting
 * or picking stale geometry and newly allocated labels with unmapped DBL_MAX anchors. Does not perform
 * layout.
 */
static int AxisLayoutUsable(Graph *graphPtr) {
    return (graphPtr->hRange > 1) && (graphPtr->vRange > 1);
}

/**
 * @ingroup axis_presentation
 * @brief Confine local axis output to its visible scrolling band.
 *
 * Allocates a graph-sized temporary pixmap and GC, copies the visible band into it, draws the complete
 * axis there and copies only that band back. The band includes the axis margin; inversion switches the
 * clipped direction. Releases temporary resources. Empty bands return immediately.
 */
static void DrawTrackAxis(Graph *graphPtr, Drawable drawable, Axis *axisPtr) {
    RbcTrack *track = axisPtr->track;
    int left = graphPtr->inverted ? track->left : 0;
    int top = graphPtr->inverted ? 0 : track->top;
    int width = graphPtr->inverted ? track->right - track->left + 1 : graphPtr->width;
    int height = graphPtr->inverted ? graphPtr->height : track->bottom - track->top + 1;
    XGCValues values;
    GC gc;
    Pixmap pixmap;
    if (width <= 0 || height <= 0) {
        return;
    }
    values.graphics_exposures = False;
    gc = Tk_GetGC(graphPtr->tkwin, GCGraphicsExposures, &values);
    pixmap = Tk_GetPixmap(graphPtr->display, drawable, graphPtr->width, graphPtr->height, Tk_Depth(graphPtr->tkwin));
    Rbc_RenderScreenCopyArea(graphPtr->tkwin, drawable, pixmap, gc, left, top, width, height, left, top);
    DrawAxis(graphPtr, pixmap, axisPtr);
    Rbc_RenderScreenCopyArea(graphPtr->tkwin, pixmap, drawable, gc, left, top, width, height, left, top);
    Tk_FreePixmap(graphPtr->display, pixmap);
    Tk_FreeGC(graphPtr->display, gc);
}

/**
 * @ingroup axis_presentation
 * @brief Draw eligible axes with scrolling-track clipping.
 *
 * Returns for collapsed plot geometry. Traverses margin chains, skipping hidden/offscreen axes and
 * tracks with collapsed visible rectangles. Uses DrawTrackAxis only for track-owned axes when track
 * scrolling is enabled; otherwise draws directly. Borrows all axis records and does not map them.
 */
void Rbc_DrawAxes(Graph *graphPtr, Drawable drawable) {
    Axis *axisPtr;
    Rbc_ChainLink *linkPtr;
    int i;

    if (!AxisLayoutUsable(graphPtr)) {
        return;
    }

    for (i = 0; i < 4; i++) {
        for (linkPtr = Rbc_ChainFirstLink(graphPtr->margins[i].axes); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            axisPtr = Rbc_ChainGetValue(linkPtr);
            if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
                if (axisPtr->track != NULL && (axisPtr->track->right - axisPtr->track->left <= 1 ||
                                               axisPtr->track->bottom - axisPtr->track->top <= 1)) {
                    continue;
                }
                if (graphPtr->trackScroll && axisPtr->track != NULL) {
                    DrawTrackAxis(graphPtr, drawable, axisPtr);
                } else {
                    DrawAxis(graphPtr, drawable, axisPtr);
                }
            }
        }
    }
}

/**
 * @ingroup axis_presentation
 * @brief Export eligible axes with scrolling-track clip scopes.
 *
 * Uses the same collapsed-layout and visibility checks as screen drawing. For scrolling local axes,
 * opens a renderer clip around the visible band including margins, exports the axis and closes the
 * scope. Requires geometry already mapped for the export dimensions.
 */
void Rbc_AxesExport(Graph *graphPtr, Rbc_ExportContext *exportPtr) {
    Axis *axisPtr;
    Rbc_ChainLink *linkPtr;
    int i;

    if (!AxisLayoutUsable(graphPtr)) {
        return;
    }
    for (i = 0; i < 4; i++) {
        for (linkPtr = Rbc_ChainFirstLink(graphPtr->margins[i].axes); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            axisPtr = Rbc_ChainGetValue(linkPtr);
            if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
                if (axisPtr->track != NULL && (axisPtr->track->right - axisPtr->track->left <= 1 ||
                                               axisPtr->track->bottom - axisPtr->track->top <= 1)) {
                    continue;
                }
                if (graphPtr->trackScroll && axisPtr->track != NULL) {
                    RbcTrack *track = axisPtr->track;
                    Rbc_RenderContext *ctx = Rbc_RenderBeginExportOutput(exportPtr);
                    Rbc_RenderClipBegin(ctx, graphPtr->inverted ? track->left : 0, graphPtr->inverted ? 0 : track->top,
                                        graphPtr->inverted ? track->right - track->left + 1 : graphPtr->width,
                                        graphPtr->inverted ? graphPtr->height : track->bottom - track->top + 1);
                    AxisExport(exportPtr, axisPtr);
                    Rbc_RenderClipEnd(ctx);
                    Rbc_RenderEnd(ctx);
                } else {
                    AxisExport(exportPtr, axisPtr);
                }
            }
        }
    }
}

/**
 * @ingroup axis_presentation
 * @brief Draw formatted range annotations around the plot edges.
 *
 * Iterates the axis registry in hash order, selecting nFormats rather than margin visibility. Formats
 * axisRange endpoints with bounded snprintf, uses logical orientation plus inversion and swaps labels
 * for descending axes. Measures/draws with limitsTextStyle and updates its rotation/anchor.
 * Logarithmic annotations currently receive exponent values; these are not the data-space limits
 * command results.
 */
void Rbc_DrawAxisLimits(Graph *graphPtr, Drawable drawable) {
    Axis *axisPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Dim2D textDim;
    int isHoriz;
    char *minPtr, *maxPtr;
    char *minFormat, *maxFormat;
    char minString[TICK_LABEL_SIZE + 1];
    char maxString[TICK_LABEL_SIZE + 1];
    int vMin, hMin, vMax, hMax;

    if (!AxisLayoutUsable(graphPtr)) {
        return;
    }

    Rbc_RenderContext *text = Rbc_RenderBeginScreenText(graphPtr->tkwin, drawable);

#define SPACING 8
    vMin = vMax = graphPtr->left + graphPtr->padLeft + 2;
    hMin = hMax = graphPtr->bottom - graphPtr->padBottom - 2; /* Offsets */
    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        if (axisPtr->nFormats == 0) {
            continue;
        }
        isHoriz = AxisIsHorizontal(graphPtr, axisPtr);
        minPtr = maxPtr = NULL;
        minFormat = maxFormat = axisPtr->limitsFormats[0];
        if (axisPtr->nFormats > 1) {
            maxFormat = axisPtr->limitsFormats[1];
        }
        if (minFormat[0] != '\0') {
            minPtr = minString;
            snprintf(minString, sizeof(minString), minFormat, axisPtr->axisRange.min);
        }
        if (maxFormat[0] != '\0') {
            maxPtr = maxString;
            snprintf(maxString, sizeof(maxString), maxFormat, axisPtr->axisRange.max);
        }
        if (axisPtr->descending) {
            char *tmp;

            tmp = minPtr, minPtr = maxPtr, maxPtr = tmp;
        }
        if (maxPtr != NULL) {
            if (isHoriz) {
                axisPtr->limitsTextStyle.theta = 90.0;
                axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SE;
                Rbc_RenderTextSize(text, maxPtr, &axisPtr->limitsTextStyle, graphPtr->right, hMax,
                              &textDim);
                hMax -= (textDim.height + SPACING);
            } else {
                axisPtr->limitsTextStyle.theta = 0.0;
                axisPtr->limitsTextStyle.anchor = TK_ANCHOR_NW;
                Rbc_RenderTextSize(text, maxPtr, &axisPtr->limitsTextStyle, vMax, graphPtr->top,
                              &textDim);
                vMax += (textDim.width + SPACING);
            }
        }
        if (minPtr != NULL) {
            axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SW;
            if (isHoriz) {
                axisPtr->limitsTextStyle.theta = 90.0;
                Rbc_RenderTextSize(text, minPtr, &axisPtr->limitsTextStyle, graphPtr->left, hMin,
                              &textDim);
                hMin -= (textDim.height + SPACING);
            } else {
                axisPtr->limitsTextStyle.theta = 0.0;
                Rbc_RenderTextSize(text, minPtr, &axisPtr->limitsTextStyle, vMin, graphPtr->bottom,
                              &textDim);
                vMin += (textDim.width + SPACING);
            }
        }
    } /* Loop on axes */
    Rbc_RenderEnd(text);
}

/**
 * @ingroup axis_presentation
 * @brief Export formatted range annotations with screen-equivalent endpoint placement.
 *
 * Iterates formatted registry axes and formats axisRange endpoints with bounded snprintf. Uses
 * inversion-aware orientation, swaps descending endpoint strings and measures with limitsTextStyle,
 * matching screen placement. Ends its output context even when layout is collapsed.
 */
void Rbc_AxisLimitsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    Axis *axisPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    double vMin, hMin, vMax, hMax;
    char minString[TICK_LABEL_SIZE + 1];
    char maxString[TICK_LABEL_SIZE + 1];
    char *minPtr, *maxPtr;
    int isHoriz;
    int textWidth, textHeight;
    char *minFmt, *maxFmt;

    if (!AxisLayoutUsable(graphPtr)) {
        Rbc_RenderEnd(output);
        return;
    }

#define SPACING 8
    vMin = vMax = graphPtr->left + graphPtr->padLeft + 2;
    hMin = hMax = graphPtr->bottom - graphPtr->padBottom - 2; /* Offsets */
    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        if (axisPtr->nFormats == 0) {
            continue;
        }
        minFmt = maxFmt = axisPtr->limitsFormats[0];
        if (axisPtr->nFormats > 1) {
            maxFmt = axisPtr->limitsFormats[1];
        }
        isHoriz = AxisIsHorizontal(graphPtr, axisPtr);
        minPtr = maxPtr = NULL;
        if (*minFmt != '\0') {
            snprintf(minString, sizeof(minString), minFmt, axisPtr->axisRange.min);
            minPtr = minString;
        }
        if (*maxFmt != '\0') {
            snprintf(maxString, sizeof(maxString), maxFmt, axisPtr->axisRange.max);
            maxPtr = maxString;
        }
        if (axisPtr->descending) {
            char *tmp = minPtr;
            minPtr = maxPtr;
            maxPtr = tmp;
        }
        if (maxPtr != NULL) {
            Rbc_GetTextExtents(&axisPtr->limitsTextStyle, maxPtr, &textWidth, &textHeight);
            if ((textWidth > 0) && (textHeight > 0)) {
                if (isHoriz) {
                    axisPtr->limitsTextStyle.theta = 90.0;
                    axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SE;
                    Rbc_RenderText(output, maxPtr, &axisPtr->limitsTextStyle, (double)graphPtr->right, hMax);
                    hMax -= (textWidth + SPACING);
                } else {
                    axisPtr->limitsTextStyle.theta = 0.0;
                    axisPtr->limitsTextStyle.anchor = TK_ANCHOR_NW;
                    Rbc_RenderText(output, maxPtr, &axisPtr->limitsTextStyle, vMax, (double)graphPtr->top);
                    vMax += (textWidth + SPACING);
                }
            }
        }
        if (minPtr != NULL) {
            Rbc_GetTextExtents(&axisPtr->limitsTextStyle, minPtr, &textWidth, &textHeight);
            if ((textWidth > 0) && (textHeight > 0)) {
                axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SW;
                if (isHoriz) {
                    axisPtr->limitsTextStyle.theta = 90.0;
                    Rbc_RenderText(output, minPtr, &axisPtr->limitsTextStyle, (double)graphPtr->left, hMin);
                    hMin -= (textWidth + SPACING);
                } else {
                    axisPtr->limitsTextStyle.theta = 0.0;
                    Rbc_RenderText(output, minPtr, &axisPtr->limitsTextStyle, vMin, (double)graphPtr->bottom);
                    vMin += (textWidth + SPACING);
                }
            }
        }
    }
    Rbc_RenderEnd(output);
}

/**
 * @ingroup axis_presentation
 * @brief Borrow the first record in an axis chain.
 *
 * Returns NULL for an empty chain. Performs no hidden/deletion filtering or reference acquisition;
 * callers must apply any additional eligibility requirements.
 */
Axis *Rbc_GetFirstAxis(Rbc_Chain *chainPtr) {
    Rbc_ChainLink *linkPtr;

    linkPtr = Rbc_ChainFirstLink(chainPtr);
    if (linkPtr == NULL) {
        return NULL;
    }
    return Rbc_ChainGetValue(linkPtr);
}

/**
 * @ingroup axis_presentation
 * @brief Pick the first axis whose label, title or region contains a point.
 *
 * Returns NULL for unusable layout or no hit. Iterates registry order, not nearest-distance order;
 * skips hidden/offscreen axes and collapsed or out-of-band scrolling tracks. Tests rotated label
 * polygons, then title polygon, then line region. Sets detail to label, title or line on the borrowed
 * returned record. Requires prepared geometry and does not acquire a reference.
 */
Axis *Rbc_NearestAxis(Graph *graphPtr, int x, int y) {
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Axis *axisPtr;
    int width, height;
    double rotWidth, rotHeight;
    Point2D bbox[5];

    if (!AxisLayoutUsable(graphPtr)) {
        return NULL;
    }

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        if ((axisPtr->hidden) || (!(axisPtr->flags & AXIS_ONSCREEN))) {
            continue; /* Don't check hidden axes or axes
                       * that are virtual. */
        }
        if (axisPtr->track != NULL &&
            (axisPtr->track->right - axisPtr->track->left <= 1 || axisPtr->track->bottom - axisPtr->track->top <= 1)) {
            continue;
        }
        if (graphPtr->trackScroll && axisPtr->track != NULL &&
            (graphPtr->inverted ? (x < axisPtr->track->left || x > axisPtr->track->right)
                                : (y < axisPtr->track->top || y > axisPtr->track->bottom))) {
            continue;
        }
        if (axisPtr->showTicks) {
            Rbc_ChainLink *linkPtr;
            TickLabel *labelPtr;
            Point2D t;

            for (linkPtr = Rbc_ChainFirstLink(axisPtr->tickLabels); linkPtr != NULL;
                 linkPtr = Rbc_ChainNextLink(linkPtr)) {
                labelPtr = Rbc_ChainGetValue(linkPtr);
                Rbc_GetBoundingBox(labelPtr->width, labelPtr->height, axisPtr->tickTextStyle.theta, &rotWidth,
                                   &rotHeight, bbox);
                width = AxisRotatedSize(rotWidth);
                height = AxisRotatedSize(rotHeight);
                t = Rbc_TranslatePoint(&labelPtr->anchorPos, width, height, axisPtr->tickTextStyle.anchor);
                t.x = x - t.x - (width * 0.5);
                t.y = y - t.y - (height * 0.5);
                bbox[4] = bbox[0];
                if (Rbc_PointInPolygon(&t, bbox, 5)) {
                    axisPtr->detail = "label";
                    return axisPtr;
                }
            }
        }
        if (axisPtr->title != NULL) { /* and then the title string. */
            Point2D t;

            Rbc_GetTextExtents(&axisPtr->titleTextStyle, axisPtr->title, &width, &height);
            Rbc_GetBoundingBox(width, height, axisPtr->titleTextStyle.theta, &rotWidth, &rotHeight, bbox);
            width = AxisRotatedSize(rotWidth);
            height = AxisRotatedSize(rotHeight);
            t = Rbc_TranslatePoint(&axisPtr->titlePos, width, height, axisPtr->titleTextStyle.anchor);
            /* Translate the point so that the 0,0 is the upper left
             * corner of the bounding box.  */
            t.x = x - t.x - (width / 2);
            t.y = y - t.y - (height / 2);
            bbox[4] = bbox[0];
            if (Rbc_PointInPolygon(&t, bbox, 5)) {
                axisPtr->detail = "title";
                return axisPtr;
            }
        }
        if (axisPtr->lineWidth > 0) { /* Check for the axis region */
            if (PointInRegion(&axisPtr->region, x, y)) {
                axisPtr->detail = "line";
                return axisPtr;
            }
        }
    }
    return NULL;
}

/**
 * @ingroup axis_commands
 * @brief Intern a binding tag in the graph axis tag table.
 *
 * Returns a borrowed stable hash key. Its storage belongs to the graph and is released with the tag
 * table, not by individual binding operations.
 */
static ClientData MakeAxisTag(Graph *graphPtr, char *tagName) {
    Tcl_HashEntry *hPtr;
    int isNew;

    hPtr = Tcl_CreateHashEntry(&graphPtr->axes.tagTable, tagName, &isNew);
    assert(hPtr);
    return Tcl_GetHashKey(&graphPtr->axes.tagTable, hPtr);
}

/**
 * @ingroup axis_commands
 * @brief Append axis name, class and configured tags in binding order.
 *
 * Borrows the graph, axis and destination list. Interned keys remain owned by the graph tag table. The
 * list is appended to rather than cleared; configured tags follow the name and class.
 */
void Rbc_AppendAxisBindingTags(Graph *graphPtr, Axis *axisPtr, Rbc_List list) {
    char **p;

    Rbc_ListAppend(list, MakeAxisTag(graphPtr, axisPtr->name), 0);
    Rbc_ListAppend(list, MakeAxisTag(graphPtr, axisPtr->classUid), 0);
    if (axisPtr->tags == NULL) {
        return;
    }
    for (p = axisPtr->tags; *p != NULL; p++) {
        Rbc_ListAppend(list, MakeAxisTag(graphPtr, *p), 0);
    }
}

/**
 * @ingroup axis_presentation
 * @brief Refresh configured axis styles after named-font changes.
 *
 * Visits all registered configured axes, refreshes text GCs and title dimensions, and marks each
 * AXIS_DIRTY. Does not regenerate tick labels or directly schedule full graph layout; the font-change
 * caller owns subsequent invalidation.
 */
void Rbc_AxisFontsChanged(Graph *graphPtr) {
    Tcl_HashSearch iter;
    Tcl_HashEntry *entryPtr;

    for (entryPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &iter); entryPtr != NULL;
         entryPtr = Tcl_NextHashEntry(&iter)) {
        Axis *axisPtr = Tcl_GetHashValue(entryPtr);

        if (!axisPtr->optionsConfigured) {
            continue;
        }
        ResetTextStyles(graphPtr, axisPtr);
        if (axisPtr->title != NULL) {
            Rbc_GetTextExtents(&axisPtr->titleTextStyle, axisPtr->title,
                               &axisPtr->titleWidth, &axisPtr->titleHeight);
        }
        axisPtr->flags |= AXIS_DIRTY;
    }
}
