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

#include "rbcGraph.h"
#include "rbcGrElem.h"
#include <X11/Xutil.h>

#define DEF_NUM_TICKS 4 /* Each minor tick is 20% */
#define STATIC_TICK_SPACE 10

#define TICK_LABEL_SIZE 200
#define MAXTICKS 10001

#define CLAMP(val, low, high) (((val) < (low)) ? (low) : ((val) > (high)) ? (high) : (val))

/*
 * Round x in terms of units
 */
#define UROUND(x, u) (Round((x) / (u)) * (u))
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

static Tk_OptionParseProc StringToAxis;
static Tk_OptionPrintProc AxisToString;
static Tk_OptionParseProc StringToAnyAxis;

Tk_CustomOption rbcXAxisOption = {StringToAxis, AxisToString, (ClientData)&rbcXAxisUid};
Tk_CustomOption rbcYAxisOption = {StringToAxis, AxisToString, (ClientData)&rbcYAxisUid};
Tk_CustomOption rbcAnyXAxisOption = {StringToAnyAxis, AxisToString, (ClientData)&rbcXAxisUid};
Tk_CustomOption rbcAnyYAxisOption = {StringToAnyAxis, AxisToString, (ClientData)&rbcYAxisUid};

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

typedef struct {
    unsigned int stagedMask;

    double reqMin;
    double reqMax;
    double scrollMin;
    double scrollMax;
} AxisLimitTransaction;

typedef enum { AXIS_TICK_OPTION_NONE, AXIS_TICK_OPTION_MAJOR, AXIS_TICK_OPTION_MINOR } AxisTickOption;

#define AXIS_TICK_OPTION_MASK(option) (1u << ((unsigned int)(option) - 1u))

typedef struct {
    unsigned int stagedMask;

    Ticks *majorTicksPtr;
    Ticks *minorTicksPtr;
} AxisTickTransaction;

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

typedef struct {
    unsigned int stagedMask;

    Shadow limitsShadow;
    Shadow tickShadow;
    Shadow titleShadow;
} AxisShadowTransaction;

typedef struct {
    int staged;
    char **tags;
} AxisTagsTransaction;

typedef struct {
    int staged;

    char **formats;
    int nFormats;
} AxisFormatTransaction;

static const Tk_OptionSpec axisOptionSpecs[] = {
    {TK_OPTION_DOUBLE, "-autorange", "autoRange", "AutoRange", DEF_AXIS_RANGE, -1, offsetof(Axis, windowSize),
     TK_OPTION_DONT_SET_DEFAULT, NULL, AXIS_RESET_MASK | AXIS_REDRAW_MASK},
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
    {TK_OPTION_STRING, "-command", "command", "Command", DEF_AXIS_COMMAND, -1, offsetof(Axis, formatCmd),
     TK_OPTION_NULL_OK, NULL, AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-descending", "descending", "Descending", DEF_AXIS_DESCENDING, -1, offsetof(Axis, descending),
     TK_OPTION_DONT_SET_DEFAULT, NULL, AXIS_MAP_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-hide", "hide", "Hide", DEF_AXIS_HIDE, -1, offsetof(Axis, hidden), TK_OPTION_DONT_SET_DEFAULT,
     NULL, AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_JUSTIFY, "-justify", "justify", "Justify", DEF_AXIS_JUSTIFY, -1, offsetof(Axis, titleTextStyle.justify),
     TK_OPTION_DONT_SET_DEFAULT, NULL, AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
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
    {TK_OPTION_BOOLEAN, "-logscale", "logScale", "LogScale", DEF_AXIS_LOGSCALE, -1, offsetof(Axis, logScale),
     TK_OPTION_DONT_SET_DEFAULT, NULL, AXIS_LOG_SCALE_MASK | AXIS_MAP_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
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
    {TK_OPTION_RELIEF, "-relief", "relief", "Relief", DEF_AXIS_RELIEF, -1, offsetof(Axis, relief),
     TK_OPTION_DONT_SET_DEFAULT, NULL, AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_DOUBLE, "-rotate", "rotate", "Rotate", DEF_AXIS_ROTATE, -1, offsetof(Axis, tickTextStyle.theta),
     TK_OPTION_DONT_SET_DEFAULT, NULL, AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-scrollcommand", "scrollCommand", "ScrollCommand", NULL, -1, offsetof(Axis, scrollCmdPrefix),
     TK_OPTION_NULL_OK, NULL, 0},
    {TK_OPTION_STRING, "-scrollincrement", "scrollIncrement", "ScrollIncrement", DEF_AXIS_SCROLL_INCREMENT,
     offsetof(Axis, scrollIncrementObjPtr), -1, 0, NULL, AXIS_PIXELS_MASK},
    {TK_OPTION_STRING, "-scrollmax", "scrollMax", "ScrollMax", NULL, offsetof(Axis, scrollMaxObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_SCROLL_LIMITS_MASK},
    {TK_OPTION_STRING, "-scrollmin", "scrollMin", "ScrollMin", NULL, offsetof(Axis, scrollMinObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_SCROLL_LIMITS_MASK},
    {TK_OPTION_DOUBLE, "-shiftby", "shiftBy", "ShiftBy", DEF_AXIS_SHIFTBY, -1, offsetof(Axis, shiftBy),
     TK_OPTION_DONT_SET_DEFAULT, NULL, AXIS_MAP_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-showticks", "showTicks", "ShowTicks", DEF_AXIS_SHOWTICKS, -1, offsetof(Axis, showTicks),
     TK_OPTION_DONT_SET_DEFAULT, NULL, AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_DOUBLE, "-stepsize", "stepSize", "StepSize", DEF_AXIS_STEP, -1, offsetof(Axis, reqStep),
     TK_OPTION_DONT_SET_DEFAULT, NULL, AXIS_MAP_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_DOUBLE, "-tickdivider", "tickDivider", "TickDivider", DEF_AXIS_STEP, -1, offsetof(Axis, tickZoom),
     TK_OPTION_DONT_SET_DEFAULT, NULL, AXIS_MAP_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_INT, "-subdivisions", "subdivisions", "Subdivisions", DEF_AXIS_SUBDIVISIONS, -1,
     offsetof(Axis, reqNumMinorTicks), 0, NULL, AXIS_MAP_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_FONT, "-tickfont", "tickFont", "Font", DEF_AXIS_TICK_FONT, -1, offsetof(Axis, tickTextStyle.font), 0,
     NULL, AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_PIXELS, "-ticklength", "tickLength", "TickLength", DEF_AXIS_TICK_LENGTH, -1, offsetof(Axis, tickLength),
     0, NULL, AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-tickshadow", "tickShadow", "Shadow", NULL, offsetof(Axis, tickShadowObjPtr), -1,
     TK_OPTION_NULL_OK, NULL, AXIS_TICK_SHADOW_MASK | AXIS_TEXT_STYLE_MASK | AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_STRING, "-title", "title", "Title", NULL, -1, offsetof(Axis, title),
     TK_OPTION_DONT_SET_DEFAULT | TK_OPTION_NULL_OK, NULL, AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
    {TK_OPTION_BOOLEAN, "-titlealternate", "titleAlternate", "TitleAlternate", DEF_AXIS_TITLE_ALTERNATE, -1,
     offsetof(Axis, titleAlternate), TK_OPTION_DONT_SET_DEFAULT, NULL, AXIS_LAYOUT_MASK | AXIS_REDRAW_MASK},
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
static int Round(register double x);
static void SetAxisRange(AxisRange *rangePtr, double min, double max);
static int InRange(register double x, AxisRange *rangePtr);
static int AxisIsHorizontal(Graph *graphPtr, Axis *axisPtr);
static void FreeLabels(Rbc_Chain *chainPtr);
static TickLabel *MakeLabel(Graph *graphPtr, Axis *axisPtr, double value);
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
static double AdjustViewport(double offset, double windowSize);
static int GetAxisScrollInfo(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], double *offsetPtr, double windowSize,
                             double scrollUnits);
static void DrawAxis(Graph *graphPtr, Drawable drawable, Axis *axisPtr);
static void AxisToPostScript(PsToken psToken, Axis *axisPtr);
static void MakeGridLine(Graph *graphPtr, Axis *axisPtr, double value, Segment2D *segPtr);
static void GetAxisGeometry(Graph *graphPtr, Axis *axisPtr);
static int GetMarginGeometry(Graph *graphPtr, Margin *marginPtr);
static void ComputeMargins(Graph *graphPtr);
static Axis *CreateAxis(Graph *graphPtr, char *name, int margin);
static int ConfigureAxis(Graph *graphPtr, Axis *axisPtr);
static int NameToAxis(Graph *graphPtr, const char *name, Axis **axisPtrPtr);
static int GetAxis(Graph *graphPtr, const char *name, Rbc_Uid classUid, Axis **axisPtrPtr);
static void FreeAxis(Graph *graphPtr, Axis *axisPtr);
static int ConfigureNewAxis(Graph *graphPtr, Axis *axisPtr, int objc, Tcl_Obj *const objv[]);
static int InitAxisOptions(Graph *graphPtr, Axis *axisPtr);
static int ConfigureAxisOptions(Graph *graphPtr, Axis *axisPtr, int objc, Tcl_Obj *const objv[], int *maskPtr);
static void ReleaseAxisOptionResources(Graph *graphPtr, Axis *axisPtr);

typedef int(RbcGrAxisOp)(Graph *, Axis *, int, int, Tcl_Obj *const[]);
typedef RbcGrAxisOp *RbcGrAxisOpPtr;
static RbcGrAxisOp BindOp;
static RbcGrAxisOp CgetOp;
static RbcGrAxisOp ConfigureOp;
static RbcGrAxisOp InvTransformOp;
static RbcGrAxisOp LimitsOp;
static RbcGrAxisOp TransformOp;
static RbcGrAxisOp UseOp;

typedef int(RbcGrAxisVirtualOp)(Graph *, int, Tcl_Obj *const[]);
typedef RbcGrAxisVirtualOp *RbcGrAxisVirtualOpPtr;
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
static int Round(x)
register double x;
{
    return (int)(x + ((x < 0.0) ? -0.5 : 0.5));
}

/*
 *----------------------------------------------------------------------
 *
 * SetAxisRange --
 *
 *      TODO: Description
 *
 * Parameters:
 *      AxisRange *rangePtr
 *      double min
 *      double max
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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

/*
 * ----------------------------------------------------------------------
 *
 * InRange --
 *
 *      Determines if a value lies within a given range.
 *
 *      The value is normalized and compared against the interval
 *      [0..1], where 0.0 is the minimum and 1.0 is the maximum.
 *      DBL_EPSILON is the smallest number that can be represented
 *      on the host machine, such that (1.0 + epsilon) != 1.0.
 *
 *      Please note, *max* can't equal *min*.
 *
 * Parameters:
 *      register double x
 *      AxisRange *rangePtr
 *
 * Results:
 *      If the value is within the interval [min..max], 1 is
 *      returned; 0 otherwise.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int InRange(register double x, AxisRange *rangePtr) {
    if (rangePtr->range < DBL_EPSILON) {
#ifdef notdef
        return (((rangePtr->max - x) >= (FABS(x) * DBL_EPSILON)) && ((x - rangePtr->min) >= (FABS(x) * DBL_EPSILON)));
#endif
        return (FABS(rangePtr->max - x) >= DBL_EPSILON);
    } else {
        double norm;

        norm = (x - rangePtr->min) * rangePtr->scale;
        return ((norm >= -DBL_EPSILON) && ((norm - 1.0) < DBL_EPSILON));
    }
}

static int AxisIsHorizontal(Graph *graphPtr, Axis *axisPtr) {
    return ((axisPtr->classUid == rbcYAxisUid) == graphPtr->inverted);
}

/* ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * StringToAnyAxis --
 *
 *      Converts the name of an axis to a pointer to its axis structure.
 *
 * Parameters:
 *      ClientData clientData - Class identifier of the type of axis we are looking for.
 *      Tcl_Interp *interp - Interpreter to send results back to.
 *      Tk_Window tkwin - Used to look up pointer to graph.
 *      const char *string - String representing new value.
 *      char *widgRec - Pointer to structure record.
 *      Tcl_Size offset - Offset of field in structure.
 *
 * Results:
 *      The return value is a standard Tcl result.  The axis flags are
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToAnyAxis(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string,
                           char *widgRec, Tcl_Size offset) {
    Axis **axisPtrPtr = (Axis **)(widgRec + offset);
    Rbc_Uid classUid = *(Rbc_Uid *)clientData;
    Graph *graphPtr;
    Axis *axisPtr;

    graphPtr = Rbc_GetGraphFromWindowData(tkwin);
    if (*axisPtrPtr != NULL) {
        FreeAxis(graphPtr, *axisPtrPtr);
    }
    if (string[0] == '\0') {
        axisPtr = NULL;
    } else if (GetAxis(graphPtr, string, classUid, &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    *axisPtrPtr = axisPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToAxis --
 *
 *      Converts the name of an axis to a pointer to its axis structure.
 *
 * Parameters:
 *      ClientData clientData - Class identifier of the type of axis we are looking for.
 *      Tcl_Interp *interp - Interpreter to send results back to.
 *      Tk_Window tkwin - Used to look up pointer to graph.
 *      const char *string - String representing new value.
 *      char *widgRec - Pointer to structure record.
 *      Tcl_Size offset - Offset of field in structure.
 *
 * Results:
 *      The return value is a standard Tcl result.  The axis flags are
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToAxis(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                        Tcl_Size offset) {
    Axis **axisPtrPtr = (Axis **)(widgRec + offset);
    Rbc_Uid classUid = *(Rbc_Uid *)clientData;
    Graph *graphPtr;

    graphPtr = Rbc_GetGraphFromWindowData(tkwin);
    if (*axisPtrPtr != NULL) {
        FreeAxis(graphPtr, *axisPtrPtr);
    }
    if (GetAxis(graphPtr, string, classUid, axisPtrPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * AxisToString --
 *
 *      Convert the window coordinates into a string.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Pointer to structure record.
 *      Tcl_Size offset - Offset of field in structure.
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The string representing the coordinate position is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *----------------------------------------------------------------------
 */
static const char *AxisToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                Tcl_FreeProc **freeProcPtr) {
    Axis *axisPtr = *(Axis **)(widgRec + offset);

    if (axisPtr == NULL) {
        return "";
    }
    return axisPtr->name;
}

/*
 *----------------------------------------------------------------------
 *
 * IsAxisBindTagsOption --
 *
 *      Determines whether an option name represents "-bindtags".
 *
 *      Tk_SetOptions has already rejected unknown and ambiguous
 *      abbreviations.
 *
 *----------------------------------------------------------------------
 */
static int IsAxisBindTagsOption(Tcl_Obj *objPtr) {
    static const char optionName[] = "-bindtags";
    const char *string;
    Tcl_Size length;
    Tcl_Size fullLength;

    string = Tcl_GetStringFromObj(objPtr, &length);
    fullLength = (Tcl_Size)(sizeof(optionName) - 1);
    return ((length > 0) && (length <= fullLength) && (strncmp(string, optionName, (size_t)length) == 0));
}

/*
 *----------------------------------------------------------------------
 *
 * StageAxisTags --
 *
 *      Parses an axis bind-tags value into temporary transaction
 *      storage without modifying the live Axis record.
 *
 *      A previous staged candidate is released only after its
 *      replacement has parsed successfully.
 *
 *----------------------------------------------------------------------
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

static void FreeAxisTagsTransaction(AxisTagsTransaction *transactionPtr) {
    if (transactionPtr->tags != NULL) {
        ckfree((char *)transactionPtr->tags);
    }
    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * PrepareAxisTagsTransaction --
 *
 *      Parses all -bindtags values involved in the current modern
 *      configuration without modifying the live Axis record.
 *
 *      Explicit repeated occurrences are processed in caller order.
 *      Therefore an invalid earlier value is not hidden by a valid
 *      final retained value.
 *
 *----------------------------------------------------------------------
 */
static int PrepareAxisTagsTransaction(Graph *graphPtr, Axis *axisPtr, AxisTagsTransaction *transactionPtr) {
    int explicitlySpecified;
    int i;

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

/*
 *----------------------------------------------------------------------
 *
 * CommitAxisTagsTransaction --
 *
 *      Replaces the live axis bind-tags list with a successfully
 *      staged candidate.
 *
 *----------------------------------------------------------------------
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

static void FreeAxisFormats(char **formats) {
    if (formats != NULL) {
        ckfree((char *)formats);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * IsAxisLimitsFormatOption --
 *
 *      Determines whether an option name represents
 *      "-limitsformat".
 *
 *      Tk_SetOptions has already rejected ambiguous or unknown
 *      abbreviations.
 *
 *----------------------------------------------------------------------
 */
static int IsAxisLimitsFormatOption(Tcl_Obj *objPtr) {
    static const char optionName[] = "-limitsformat";
    const char *string;
    Tcl_Size length;
    Tcl_Size fullLength;

    string = Tcl_GetStringFromObj(objPtr, &length);

    fullLength = (Tcl_Size)(sizeof(optionName) - 1);

    return ((length > 0) && (length <= fullLength) && (strncmp(string, optionName, (size_t)length) == 0));
}

/*
 *----------------------------------------------------------------------
 *
 * GetAxisLimitsFormatsFromObj --
 *
 *      Parses the value of -limitsformat without modifying the live
 *      Axis record.
 *
 *      The value must be a Tcl list containing at most two elements.
 *      A NULL or empty value selects no limit formats.
 *
 *----------------------------------------------------------------------
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

    *formatsPtrPtr = formats;
    *nFormatsPtr = (int)nFormats;

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * StageAxisLimitsFormats --
 *
 *      Parses a -limitsformat value into temporary transaction
 *      storage without modifying the live Axis record.
 *
 *      An earlier staged candidate is retained until the replacement
 *      has parsed successfully.
 *
 *----------------------------------------------------------------------
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

static void FreeAxisFormatTransaction(AxisFormatTransaction *transactionPtr) {
    FreeAxisFormats(transactionPtr->formats);

    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

/*
 *----------------------------------------------------------------------
 *
 * PrepareAxisFormatTransaction --
 *
 *      Parses all -limitsformat values involved in the current modern
 *      configuration without modifying the live Axis record.
 *
 *      Explicit repeated occurrences are processed in caller order.
 *      Therefore an invalid earlier value is not hidden by the final
 *      retained option object.
 *
 *----------------------------------------------------------------------
 */
static int PrepareAxisFormatTransaction(Graph *graphPtr, Axis *axisPtr, AxisFormatTransaction *transactionPtr) {
    int explicitlySpecified;
    int i;

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

/*
 *----------------------------------------------------------------------
 *
 * CommitAxisFormatTransaction --
 *
 *      Replaces the live limit-format list with a successfully staged
 *      candidate.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * InitAxisOptions --
 *
 *      Creates the axis option table and installs its default and
 *      option-database values.
 *
 *      This function does not construct derived axis resources.
 *      ConfigureAxisOptions performs that work through ConfigureAxis.
 *
 * Results:
 *      TCL_OK if the axis options were initialised successfully.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      Creates axisPtr->optionTable and stores Tk-managed option
 *      resources in the Axis record.
 *
 *----------------------------------------------------------------------
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
    componentName = RbcStrdup(axisPtr->name);

    if (componentName[0] != '\0') {
        componentName[0] = (char)tolower((unsigned char)componentName[0]);
    }

    result = Rbc_InitComponentOptions(graphPtr->interp, graphPtr->tkwin, componentName, "Axis", (char *)axisPtr,
                                      axisPtr->optionTable);

    ckfree(componentName);

    if (result != TCL_OK) {
        axisPtr->optionTable = NULL;
        return TCL_ERROR;
    }

    axisPtr->optionsInitialized = TRUE;
    axisPtr->tkResourcesReleased = FALSE;

    return TCL_OK;
}

static void ResetAxisOptionContext(Axis *axisPtr) {
    axisPtr->optionMask = 0;
    axisPtr->optionObjc = 0;
    axisPtr->optionObjv = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * ConfigureAxisOptions --
 *
 *      Applies axis option/value pairs transactionally and
 *      invokes ConfigureAxis to validate and construct derived state.
 *
 *      The original option vector and Tk type mask are temporarily
 *      retained in the Axis record so ConfigureAxis can process
 *      repeated options in caller order.
 *
 * Results:
 *      TCL_OK if all options and derived resources were configured.
 *      TCL_ERROR otherwise.
 *
 * Side Effects:
 *      On success, updates the axis configuration.
 *      On failure, restores all Tk-managed option fields.
 *
 *----------------------------------------------------------------------
 */
static int ConfigureAxisOptions(Graph *graphPtr, Axis *axisPtr, int objc, Tcl_Obj *const objv[], int *maskPtr) {
    Tk_SavedOptions savedOptions;
    Tcl_Obj *errorObjPtr;
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
         * Restoring Tk-managed options may alter the interpreter
         * result. Preserve the error produced by ConfigureAxis.
         */
        errorObjPtr = Tcl_GetObjResult(graphPtr->interp);

        Tcl_IncrRefCount(errorObjPtr);

        /*
         * Never retain pointers to the caller-owned option vector
         * while restoring the configuration.
         */
        ResetAxisOptionContext(axisPtr);

        Tk_RestoreSavedOptions(&savedOptions);

        Tcl_SetObjResult(graphPtr->interp, errorObjPtr);

        Tcl_DecrRefCount(errorObjPtr);

        return TCL_ERROR;
    }

    ResetAxisOptionContext(axisPtr);

    axisPtr->optionsConfigured = TRUE;

    Tk_FreeSavedOptions(&savedOptions);

    if (maskPtr != NULL) {
        *maskPtr = mask;
    }

    return TCL_OK;
}

static int ConfigureNewAxis(Graph *graphPtr, Axis *axisPtr, int objc, Tcl_Obj *const objv[]) {
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

/*
 *----------------------------------------------------------------------
 *
 * ReleaseAxisOptionResources --
 *
 *      Releases resources owned directly by the modern Tk option
 *      table.
 *
 *      Derived axis resources remain the responsibility of the
 *      existing concrete axis destructor.
 *
 *----------------------------------------------------------------------
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

        axisPtr->optionsInitialized = FALSE;
    }

    axisPtr->tkResourcesReleased = TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * GetAxisLimitOption --
 *
 *      Determines whether an option name represents one of the four
 *      manually parsed axis-limit options.
 *
 *      Tk_SetOptions has already rejected unknown and ambiguous
 *      abbreviations. This helper recovers the canonical identity from
 *      the original option/value vector.
 *
 *----------------------------------------------------------------------
 */
static AxisLimitOption GetAxisLimitOption(Tcl_Obj *objPtr) {
    static const struct {
        const char *name;
        AxisLimitOption option;
    } optionMap[] = {{"-min", AXIS_LIMIT_OPTION_MIN},
                     {"-max", AXIS_LIMIT_OPTION_MAX},
                     {"-scrollmin", AXIS_LIMIT_OPTION_SCROLL_MIN},
                     {"-scrollmax", AXIS_LIMIT_OPTION_SCROLL_MAX}};

    const char *string;
    Tcl_Size length;
    AxisLimitOption match;
    size_t i;

    string = Tcl_GetStringFromObj(objPtr, &length);

    /*
     * Prefer exact matches. This is important for -min, which is also
     * a prefix of -minorticks.
     */
    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length == fullLength) && (memcmp(string, optionMap[i].name, (size_t)length) == 0)) {
            return optionMap[i].option;
        }
    }

    /*
     * Recover an accepted unambiguous abbreviation.
     */
    match = AXIS_LIMIT_OPTION_NONE;

    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length > 0) && (length < fullLength) && (strncmp(string, optionMap[i].name, (size_t)length) == 0)) {
            if (match == AXIS_LIMIT_OPTION_NONE) {
                match = optionMap[i].option;
            } else if (match != optionMap[i].option) {
                return AXIS_LIMIT_OPTION_NONE;
            }
        }
    }

    return match;
}

/*
 *----------------------------------------------------------------------
 *
 * GetAxisLimitFromObj --
 *
 *      Parses an axis limit without modifying the live Axis record.
 *
 *      A NULL or empty object selects automatic scaling and is
 *      represented internally by VALUE_UNDEFINED.
 *
 *----------------------------------------------------------------------
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

    return Tcl_ExprDoubleObj(interp, objPtr, valuePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * GetAxisPixelOption --
 *
 *      Determines whether an option represents one of the manually
 *      parsed axis pixel-distance options.
 *
 *      Tk_SetOptions has already rejected unknown or ambiguous
 *      abbreviations. The exact "-bd" synonym is mapped to
 *      "-borderwidth".
 *
 *----------------------------------------------------------------------
 */
static AxisPixelOption GetAxisPixelOption(Tcl_Obj *objPtr) {
    static const struct {
        const char *name;
        AxisPixelOption option;
    } optionMap[] = {{"-borderwidth", AXIS_PIXEL_OPTION_BORDER_WIDTH},
                     {"-linewidth", AXIS_PIXEL_OPTION_LINE_WIDTH},
                     {"-scrollincrement", AXIS_PIXEL_OPTION_SCROLL_INCREMENT}};

    const char *string;
    Tcl_Size length;
    AxisPixelOption match;
    size_t i;

    string = Tcl_GetStringFromObj(objPtr, &length);

    /*
     * Handle the exact synonym separately. It is not an abbreviation
     * of "-borderwidth".
     */
    if ((length == 3) && (memcmp(string, "-bd", 3) == 0)) {
        return AXIS_PIXEL_OPTION_BORDER_WIDTH;
    }

    /*
     * Prefer exact canonical names.
     */
    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length == fullLength) && (memcmp(string, optionMap[i].name, (size_t)length) == 0)) {
            return optionMap[i].option;
        }
    }

    /*
     * Recover a canonical option from an accepted abbreviation.
     */
    match = AXIS_PIXEL_OPTION_NONE;

    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length > 0) && (length < fullLength) && (strncmp(string, optionMap[i].name, (size_t)length) == 0)) {
            if (match == AXIS_PIXEL_OPTION_NONE) {
                match = optionMap[i].option;
            } else if (match != optionMap[i].option) {
                return AXIS_PIXEL_OPTION_NONE;
            }
        }
    }

    return match;
}

/*
 *----------------------------------------------------------------------
 *
 * StageAxisPixelOption --
 *
 *      Parses one pixel-distance option into temporary transaction
 *      storage without modifying the live Axis record.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * PrepareAxisPixelTransaction --
 *
 *      Parses all retained axis pixel options involved in the current
 *      modern configuration without modifying the live Axis record.
 *
 *      Explicit repeated occurrences are processed in caller order.
 *      An invalid earlier value therefore causes the whole configure
 *      operation to fail.
 *
 *----------------------------------------------------------------------
 */
static int PrepareAxisPixelTransaction(Graph *graphPtr, Axis *axisPtr, AxisPixelTransaction *transactionPtr) {
    unsigned int explicitMask;
    int i;

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

/*
 *----------------------------------------------------------------------
 *
 * CommitAxisPixelTransaction --
 *
 *      Commits successfully parsed pixel-distance values to the live
 *      Axis record.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * StageAxisLimit --
 *
 *      Parses one limit option into temporary transaction storage.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * PrepareAxisLimitTransaction --
 *
 *      Parses all axis-limit values involved in the current modern
 *      configuration and validates the effective requested range
 *      without modifying the live Axis record.
 *
 *      Explicit repeated occurrences are processed in caller order.
 *      Therefore an invalid earlier expression is not hidden by a
 *      valid final occurrence.
 *
 *----------------------------------------------------------------------
 */
static int PrepareAxisLimitTransaction(Graph *graphPtr, Axis *axisPtr, AxisLimitTransaction *transactionPtr) {
    unsigned int explicitMask;
    int i;

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

    /*
     * Preserve the current legacy validation semantics: only a defined
     * requested minimum is checked when logarithmic scaling is active.
     */
    if (axisPtr->logScale && DEFINED(transactionPtr->reqMin) && (transactionPtr->reqMin <= 0.0)) {
        Tcl_SetObjResult(graphPtr->interp,
                         Tcl_ObjPrintf("bad logscale limits "
                                       "(min=%g,max=%g) for axis \"%s\"",
                                       transactionPtr->reqMin, transactionPtr->reqMax, axisPtr->name));

        return TCL_ERROR;
    }

    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * CommitAxisLimitTransaction --
 *
 *      Commits successfully parsed axis limits.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * FreeLabels --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Chain *chainPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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

/*
 * ----------------------------------------------------------------------
 *
 * MakeLabel --
 *
 *      Converts a floating point tick value to a string to be used as its
 *      label.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr - Axis structure
 *      double value - Value to be convert to a decimal string
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Returns a new label in the string character buffer.  The formatted
 *      tick label will be displayed on the graph.
 *
 * ----------------------------------------------------------------------
 */
static TickLabel *MakeLabel(Graph *graphPtr, Axis *axisPtr, double value) {
    char string[TICK_LABEL_SIZE + 1];
    TickLabel *labelPtr;

    /* Generate a default tick label based upon the tick value.  */
    if (axisPtr->logScale) {
        sprintf(string, "1E%d", ROUND(value));
    } else {
        sprintf(string, "%.*g", NUMDIGITS, value);
    }

    if (axisPtr->formatCmd != NULL) {
        Tcl_Interp *interp = graphPtr->interp;
        Tk_Window tkwin = graphPtr->tkwin;

        /*
         * A Tcl proc was designated to format tick labels. Append the path
         * name of the widget and the default tick label as arguments when
         * invoking it. Copy and save the new label from the interpreter
         * result.
         */
        Tcl_ResetResult(interp);
        if (Tcl_VarEval(interp, axisPtr->formatCmd, " ", Tk_PathName(tkwin), " ", string, (char *)NULL) != TCL_OK) {
            Tcl_BackgroundError(interp);
        } else {
            /*
             * The proc could return a string of any length, so arbitrarily
             * limit it to what will fit in the return string.
             */
            strncpy(string, Tcl_GetStringResult(interp), TICK_LABEL_SIZE);
            string[TICK_LABEL_SIZE] = '\0';

            Tcl_ResetResult(interp); /* Clear the interpreter's result. */
        }
    }
    labelPtr = (TickLabel *)ckalloc(sizeof(TickLabel) + strlen(string));
    assert(labelPtr);
    strcpy(labelPtr->string, string);
    labelPtr->anchorPos.x = labelPtr->anchorPos.y = DBL_MAX;
    return labelPtr;
}

static void FreeAxisTicks(Ticks *ticksPtr) {
    if (ticksPtr != NULL) {
        ckfree((char *)ticksPtr);
    }
}


static AxisTickOption GetAxisTickOption(Tcl_Obj *objPtr) {
    static const struct {
        const char *name;
        AxisTickOption option;
    } optionMap[] = {{"-majorticks", AXIS_TICK_OPTION_MAJOR}, {"-minorticks", AXIS_TICK_OPTION_MINOR}};

    const char *string;
    Tcl_Size length;
    AxisTickOption match;
    size_t i;

    string = Tcl_GetStringFromObj(objPtr, &length);

    /*
     * "-min" is a separate exact option, not an abbreviation of
     * "-minorticks".
     */
    if ((length == 4) && (memcmp(string, "-min", 4) == 0)) {
        return AXIS_TICK_OPTION_NONE;
    }

    /*
     * Prefer exact matches.
     */
    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length == fullLength) && (memcmp(string, optionMap[i].name, (size_t)length) == 0)) {
            return optionMap[i].option;
        }
    }

    /*
     * Tk_SetOptions has already rejected ambiguous abbreviations.
     */
    match = AXIS_TICK_OPTION_NONE;

    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length > 0) && (length < fullLength) && (strncmp(string, optionMap[i].name, (size_t)length) == 0)) {
            if (match == AXIS_TICK_OPTION_NONE) {
                match = optionMap[i].option;
            } else if (match != optionMap[i].option) {
                return AXIS_TICK_OPTION_NONE;
            }
        }
    }

    return match;
}

/*
 *----------------------------------------------------------------------
 *
 * IsAxisLooseOption --
 *
 *      Determines whether an option name represents "-loose".
 *
 *      Tk_SetOptions has already rejected unknown and ambiguous
 *      abbreviations. This helper recovers the canonical identity from
 *      the original option/value vector.
 *
 *----------------------------------------------------------------------
 */
static int IsAxisLooseOption(Tcl_Obj *objPtr) {
    static const char optionName[] = "-loose";
    const char *string;
    Tcl_Size length;
    Tcl_Size fullLength;

    string = Tcl_GetStringFromObj(objPtr, &length);

    fullLength = (Tcl_Size)(sizeof(optionName) - 1);

    return ((length > 0) && (length <= fullLength) && (strncmp(string, optionName, (size_t)length) == 0));
}

/*
 *----------------------------------------------------------------------
 *
 * GetAxisLooseFromObj --
 *
 *      Parses the one- or two-element value of the axis -loose option
 *      without modifying the live Axis record.
 *
 *      Boolean values map to TICK_RANGE_TIGHT or TICK_RANGE_LOOSE.
 *      The exact value "always" maps to
 *      TICK_RANGE_ALWAYS_LOOSE.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * StageAxisLoose --
 *
 *      Parses an axis -loose value into temporary transaction storage
 *      without modifying the live Axis record.
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * PrepareAxisLooseTransaction --
 *
 *      Parses all -loose values involved in the current modern
 *      configuration without modifying the live Axis record.
 *
 *      Explicit repeated occurrences are processed in caller order.
 *      An invalid earlier occurrence therefore causes the complete
 *      configuration to fail.
 *
 *----------------------------------------------------------------------
 */
static int PrepareAxisLooseTransaction(Graph *graphPtr, Axis *axisPtr, AxisLooseTransaction *transactionPtr) {
    int explicitlySpecified;
    int i;

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

/*
 *----------------------------------------------------------------------
 *
 * CommitAxisLooseTransaction --
 *
 *      Commits a successfully parsed -loose value to the live Axis
 *      record.
 *
 *----------------------------------------------------------------------
 */
static void CommitAxisLooseTransaction(Axis *axisPtr, AxisLooseTransaction *transactionPtr) {
    if (!transactionPtr->staged) {
        return;
    }

    axisPtr->looseMin = transactionPtr->looseMin;

    axisPtr->looseMax = transactionPtr->looseMax;

    transactionPtr->staged = FALSE;
}

static int GetAxisTicksFromObj(Tcl_Interp *interp, Tcl_Obj *objPtr, Ticks **ticksPtrPtr) {
    Tcl_Obj **exprObjv;
    Tcl_Size nExprs;
    Ticks *ticksPtr;
    Tcl_Size i;

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
     * Preserve the allocation layout used by the former
     * StringToTicks parser and by generated tick arrays.
     */
    ticksPtr = (Ticks *)ckalloc(sizeof(Ticks) + ((size_t)nExprs * sizeof(double)));

    assert(ticksPtr != NULL);

    for (i = 0; i < nExprs; i++) {
        if (Tcl_ExprDoubleObj(interp, exprObjv[i], &ticksPtr->values[i]) != TCL_OK) {
            ckfree((char *)ticksPtr);

            return TCL_ERROR;
        }
    }

    ticksPtr->nTicks = (int)nExprs;
    *ticksPtrPtr = ticksPtr;

    return TCL_OK;
}

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

static void FreeAxisTickTransaction(AxisTickTransaction *transactionPtr) {
    FreeAxisTicks(transactionPtr->majorTicksPtr);

    FreeAxisTicks(transactionPtr->minorTicksPtr);

    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

static int PrepareAxisTickTransaction(Graph *graphPtr, Axis *axisPtr, AxisTickTransaction *transactionPtr) {
    unsigned int explicitMask;
    int i;

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

static void FreeAxisShadow(Shadow *shadowPtr) {
    if (shadowPtr->color != NULL) {
        Tk_FreeColor(shadowPtr->color);
    }

    shadowPtr->color = NULL;
    shadowPtr->offset = 0;
}

static AxisShadowOption GetAxisShadowOption(Tcl_Obj *objPtr) {
    static const struct {
        const char *name;
        AxisShadowOption option;
    } optionMap[] = {{"-limitsshadow", AXIS_SHADOW_OPTION_LIMITS},
                     {"-tickshadow", AXIS_SHADOW_OPTION_TICK},
                     {"-titleshadow", AXIS_SHADOW_OPTION_TITLE}};

    const char *string;
    Tcl_Size length;
    AxisShadowOption match;
    size_t i;

    string = Tcl_GetStringFromObj(objPtr, &length);

    /*
     * Prefer exact option names.
     */
    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length == fullLength) && (memcmp(string, optionMap[i].name, (size_t)length) == 0)) {
            return optionMap[i].option;
        }
    }

    /*
     * Tk_SetOptions has already rejected ambiguous abbreviations.
     */
    match = AXIS_SHADOW_OPTION_NONE;

    for (i = 0; i < sizeof(optionMap) / sizeof(optionMap[0]); i++) {
        Tcl_Size fullLength;

        fullLength = (Tcl_Size)strlen(optionMap[i].name);

        if ((length > 0) && (length < fullLength) && (strncmp(string, optionMap[i].name, (size_t)length) == 0)) {
            if (match == AXIS_SHADOW_OPTION_NONE) {
                match = optionMap[i].option;
            } else if (match != optionMap[i].option) {
                return AXIS_SHADOW_OPTION_NONE;
            }
        }
    }

    return match;
}

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

static void FreeAxisShadowTransaction(AxisShadowTransaction *transactionPtr) {
    FreeAxisShadow(&transactionPtr->limitsShadow);

    FreeAxisShadow(&transactionPtr->tickShadow);

    FreeAxisShadow(&transactionPtr->titleShadow);

    memset(transactionPtr, 0, sizeof(*transactionPtr));
}

static int PrepareAxisShadowTransaction(Graph *graphPtr, Axis *axisPtr, AxisShadowTransaction *transactionPtr) {
    unsigned int explicitMask;
    int i;

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

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_InvHMap --
 *
 *      Maps the given screen coordinate back to a graph coordinate.
 *      Called by the graph locater routine.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      double x
 *
 * Results:
 *      Returns the graph coordinate value at the given window
 *      y-coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
double Rbc_InvHMap(Graph *graphPtr, Axis *axisPtr, double x) {
    double value;

    x = (double)(x - graphPtr->hOffset) * graphPtr->hScale;
    if (axisPtr->descending) {
        x = 1.0 - x;
    }
    value = (x * axisPtr->axisRange.range) + axisPtr->axisRange.min;
    if (axisPtr->logScale) {
        value = EXP10(value);
    }
    return value;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_InvVMap --
 *
 *      Maps the given window y-coordinate back to a graph coordinate
 *      value. Called by the graph locater routine.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      double y
 *
 * Results:
 *      Returns the graph coordinate value at the given window
 *      y-coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
double Rbc_InvVMap(Graph *graphPtr, Axis *axisPtr, double y) {
    double value;

    y = (double)(y - graphPtr->vOffset) * graphPtr->vScale;
    if (axisPtr->descending) {
        y = 1.0 - y;
    }
    value = ((1.0 - y) * axisPtr->axisRange.range) + axisPtr->axisRange.min;
    if (axisPtr->logScale) {
        value = EXP10(value);
    }
    return value;
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_HMap --
 *
 *      Map the given graph coordinate value to its axis, returning a window
 *      position.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      double x
 *
 * Results:
 *      Returns a double precision number representing the window coordinate
 *      position on the given axis.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
double Rbc_HMap(Graph *graphPtr, Axis *axisPtr, double x) {
    if ((axisPtr->logScale) && (x != 0.0)) {
        x = log10(FABS(x));
    }
    /* Map graph coordinate to normalized coordinates [0..1] */
    x = (x - axisPtr->axisRange.min) * axisPtr->axisRange.scale;
    if (axisPtr->descending) {
        x = 1.0 - x;
    }
    return (x * graphPtr->hRange + graphPtr->hOffset);
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_VMap --
 *
 *      Map the given graph coordinate value to its axis, returning a window
 *      position.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      double y
 *
 * Results:
 *      Returns a double precision number representing the window coordinate
 *      position on the given axis.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
double Rbc_VMap(Graph *graphPtr, Axis *axisPtr, double y) {
    if ((axisPtr->logScale) && (y != 0.0)) {
        y = log10(FABS(y));
    }
    /* Map graph coordinate to normalized coordinates [0..1] */
    y = (y - axisPtr->axisRange.min) * axisPtr->axisRange.scale;
    if (axisPtr->descending) {
        y = 1.0 - y;
    }
    return (((1.0 - y) * graphPtr->vRange) + graphPtr->vOffset);
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_Map2D --
 *
 *      Maps the given graph x,y coordinate values to a window position.
 *
 * Parameters:
 *      Graph *graphPtr
 *      double x - Graph x coordinate
 *      double y - Graph y coordinate
 *      Axis2D *axesPtr - Specifies which axes to use
 *
 * Results:
 *      Returns a XPoint structure containing the window coordinates of
 *      the given graph x,y coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
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

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_InvMap2D --
 *
 *      Maps the given window x,y coordinates to graph values.
 *
 * Parameters:
 *      Graph *graphPtr
 *      double x - Window x coordinate
 *      double y - Window y coordinate
 *      Axis2D *axesPtr - Specifies which axes to use
 *
 * Results:
 *      Returns a structure containing the graph coordinates of
 *      the given window x,y coordinate.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
Point2D Rbc_InvMap2D(Graph *graphPtr, double x, double y, Axis2D *axesPtr) {
    Point2D point;

    if (graphPtr->inverted) {
        point.x = Rbc_InvVMap(graphPtr, axesPtr->x, y);
        point.y = Rbc_InvHMap(graphPtr, axesPtr->y, x);
    } else {
        point.x = Rbc_InvHMap(graphPtr, axesPtr->x, x);
        point.y = Rbc_InvVMap(graphPtr, axesPtr->y, y);
    }
    return point;
}

/*
 *----------------------------------------------------------------------
 *
 * GetDataLimits --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Axis *axisPtr
 *      double min
 *      double max
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void GetDataLimits(Axis *axisPtr, double min, double max) {
    if (axisPtr->valueRange.min > min) {
        axisPtr->valueRange.min = min;
    }
    if (axisPtr->valueRange.max < max) {
        axisPtr->valueRange.max = max;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FixAxisRange --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Axis *axisPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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
    if (min >= max) {
        double value;

        /*
         * There is no range of data (i.e. min is not less than max),
         * so manufacture one.
         */
        value = min;
        if (value == 0.0) {
            min = -0.1, max = 0.1;
        } else {
            double x;

            x = FABS(value) * 0.1;
            min = value - x, max = value + x;
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

    if (axisPtr->max < axisPtr->min) {

        /*
         * If the limits still don't make sense, it's because one
         * limit configuration option (-min or -max) was set and the
         * other default (based upon the data) is too small or large.
         * Remedy this by making up a new min or max from the
         * user-defined limit.
         */

        if (!DEFINED(axisPtr->reqMin)) {
            axisPtr->min = axisPtr->max - (FABS(axisPtr->max) * 0.1);
        }
        if (!DEFINED(axisPtr->reqMax)) {
            axisPtr->max = axisPtr->min + (FABS(axisPtr->max) * 0.1);
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

/*
 * ----------------------------------------------------------------------
 *
 * NiceNum --
 *
 *      Reference: Paul Heckbert, "Nice Numbers for Graph Labels",
 *             Graphics Gems, pp 61-63.
 *
 *      Finds a "nice" number approximately equal to x.
 *
 * Parameters:
 *      double x
 *      int round - If non-zero, round. Otherwise take ceiling of value.
 *      ClientData clientData
 *      Rbc_BindPickProc *pickProc
 *      Rbc_BindTagProc *tagProc
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * GenerateTicks --
 *
 *      TODO: Description
 *
 * Parameters:
 *      TickSweep *sweepPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static Ticks *GenerateTicks(TickSweep *sweepPtr) {
    Ticks *ticksPtr;
    register int i;

    ticksPtr = (Ticks *)ckalloc(sizeof(Ticks) + (sweepPtr->nSteps * sizeof(double)));
    assert(ticksPtr);

    if (sweepPtr->step == 0.0) {
        static double logTable[] = {/* Precomputed log10 values [1..10] */
                                    0.0,
                                    0.301029995663981,
                                    0.477121254719662,
                                    0.602059991327962,
                                    0.698970004336019,
                                    0.778151250383644,
                                    0.845098040014257,
                                    0.903089986991944,
                                    0.954242509439325,
                                    1.0};
        /* Hack: A zero step indicates to use log values. */
        for (i = 0; i < sweepPtr->nSteps; i++) {
            ticksPtr->values[i] = logTable[i];
        }
    } else {
        double value;

        value = sweepPtr->initial; /* Start from smallest axis tick */
        for (i = 0; i < sweepPtr->nSteps; i++) {
            value = UROUND(value, sweepPtr->step);
            ticksPtr->values[i] = value;
            value += sweepPtr->step;
        }
    }
    ticksPtr->nTicks = sweepPtr->nSteps;
    return ticksPtr;
}

/*
 * ----------------------------------------------------------------------
 *
 * LogScaleAxis --
 *
 *      Determine the range and units of a log scaled axis.
 *
 *      Unless the axis limits are specified, the axis is scaled
 *      automatically, where the smallest and largest major ticks encompass
 *      the range of actual data values.  When an axis limit is specified,
 *      that value represents the smallest(min)/largest(max) value in the
 *      displayed range of values.
 *
 *      Both manual and automatic scaling are affected by the step used.  By
 *      default, the step is the largest power of ten to divide the range in
 *      more than one piece.
 *
 *      Automatic scaling:
 *      Find the smallest number of units which contain the range of values.
 *      The minimum and maximum major tick values will be represent the
 *      range of values for the axis. This greatest number of major ticks
 *      possible is 10.
 *
 *      Manual scaling:
 *          Make the minimum and maximum data values the represent the range of
 *          the values for the axis.  The minimum and maximum major ticks will be
 *          inclusive of this range.  This provides the largest area for plotting
 *          and the expected results when the axis min and max values have be set
 *          by the user (.e.g zooming).  The maximum number of major ticks is 20.
 *
 *          For log scale, there's the possibility that the minimum and
 *          maximum data values are the same magnitude.  To represent the
 *          points properly, at least one full decade should be shown.
 *          However, if you zoom a log scale plot, the results should be
 *          predictable. Therefore, in that case, show only minor ticks.
 *          Lastly, there should be an appropriate way to handle numbers
 *          <=0.
 *
 *              maxY
 *                |    units = magnitude (of least significant digit)
 *                |    high  = largest unit tick < max axis value
 *          high _|    low   = smallest unit tick > min axis value
 *                |
 *                |    range = high - low
 *                |    # ticks = greatest factor of range/units
 *               _|
 *            U   |
 *            n   |
 *            i   |
 *            t  _|
 *                |
 *                |
 *                |
 *           low _|
 *                |
 *                |_minX________________maxX__
 *                |   |       |      |       |
 *         minY  low                        high
 *               minY
 *
 *
 *      numTicks = Number of ticks
 *      min = Minimum value of axis
 *      max = Maximum value of axis
 *      range    = Range of values (max - min)
 *
 *      If the number of decades is greater than ten, it is assumed
 *      that the full set of log-style ticks can't be drawn properly.
 *
 * Parameters:
 *      Axis *axisPtr
 *      double min
 *      double max
 *
 * Results:
 *      None
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ---------------------------------------------------------------------- */
static void LogScaleAxis(Axis *axisPtr, double min, double max) {
    double range;
    double tickMin, tickMax;
    double majorStep, minorStep;
    int nMajor, nMinor;

    min = (min != 0.0) ? log10(FABS(min)) : 0.0;
    max = (max != 0.0) ? log10(FABS(max)) : 1.0;

    tickMin = floor(min);
    tickMax = ceil(max);
    range = tickMax - tickMin;

    if (range > 10) {
        /* There are too many decades to display a major tick at every
         * decade.  Instead, treat the axis as a linear scale.  */
        range = NiceNum(range, 0);
        majorStep = NiceNum(range / DEF_NUM_TICKS, 1);
        tickMin = UFLOOR(tickMin, majorStep);
        tickMax = UCEIL(tickMax, majorStep);
        nMajor = (int)((tickMax - tickMin) / majorStep) + 1;
        minorStep = EXP10(floor(log10(majorStep)));
        if (minorStep == majorStep) {
            nMinor = 4, minorStep = 0.2;
        } else {
            nMinor = Round(majorStep / minorStep) - 1;
        }
    } else {
        if (tickMin == tickMax) {
            tickMax++;
        }
        majorStep = 1.0;
        nMajor = (int)(tickMax - tickMin + 1); /* FIXME: Check this. */

        minorStep = 0.0; /* This is a special hack to pass
                          * information to the GenerateTicks
                          * routine. An interval of 0.0 tells
                          *    1) this is a minor sweep and
                          *    2) the axis is log scale.
                          */
        nMinor = 10;
    }
    if ((axisPtr->looseMin == TICK_RANGE_TIGHT) ||
        ((axisPtr->looseMin == TICK_RANGE_LOOSE) && (DEFINED(axisPtr->reqMin)))) {
        tickMin = min;
        nMajor++;
    }
    if ((axisPtr->looseMax == TICK_RANGE_TIGHT) ||
        ((axisPtr->looseMax == TICK_RANGE_LOOSE) && (DEFINED(axisPtr->reqMax)))) {
        tickMax = max;
    }
    axisPtr->majorSweep.step = majorStep;
    axisPtr->majorSweep.initial = floor(tickMin);
    axisPtr->majorSweep.nSteps = nMajor;
    axisPtr->minorSweep.initial = axisPtr->minorSweep.step = minorStep;
    axisPtr->minorSweep.nSteps = nMinor;

    SetAxisRange(&axisPtr->axisRange, tickMin, tickMax);
}

/*
 * ----------------------------------------------------------------------
 *
 * LinearScaleAxis --
 *
 *      Determine the units of a linear scaled axis.
 *
 *      The axis limits are either the range of the data values mapped
 *      to the axis (autoscaled), or the values specified by the -min
 *      and -max options (manual).
 *
 *      If autoscaled, the smallest and largest major ticks will
 *      encompass the range of data values.  If the -loose option is
 *      selected, the next outer ticks are choosen.  If tight, the
 *      ticks are at or inside of the data limits are used.
 *
 *      If manually set, the ticks are at or inside the data limits
 *      are used.  This makes sense for zooming.  You want the
 *      selected range to represent the next limit, not something a
 *      bit bigger.
 *
 *      Note: I added an "always" value to the -loose option to force
 *            the manually selected axes to be loose. It's probably
 *            not a good idea.
 *
 *              maxY
 *                |    units = magnitude (of least significant digit)
 *                |    high  = largest unit tick < max axis value
 *          high _|    low   = smallest unit tick > min axis value
 *                |
 *                |    range = high - low
 *                |    # ticks = greatest factor of range/units
 *               _|
 *            U   |
 *            n   |
 *            i   |
 *            t  _|
 *                |
 *                |
 *                |
 *           low _|
 *                |
 *                |_minX________________maxX__
 *                |   |       |      |       |
 *         minY  low                        high
 *               minY
 *
 *      numTicks = Number of ticks
 *      min = Minimum value of axis
 *      max = Maximum value of axis
 *      range    = Range of values (max - min)
 *
 * Parameters:
 *      Axis *axisPtr
 *      double min
 *      double max
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The axis tick information is set.  The actual tick values will
 *      be generated later.
 *
 * ----------------------------------------------------------------------
 */
static void LinearScaleAxis(Axis *axisPtr, double min, double max) {
    double range, step;
    double tickMin, tickMax;
    double axisMin, axisMax;
    int nTicks;

    range = max - min;

    /* Calculate the major tick stepping. */
    if (axisPtr->reqStep > 0.0) {
        /* An interval was designated by the user.  Keep scaling it
         * until it fits comfortably within the current range of the
         * axis.  */
        step = axisPtr->reqStep;
        while ((2 * step) >= range) {
            step *= 0.5;
        }
    } else {
        range = NiceNum(range, 0);
        step = NiceNum(range / DEF_NUM_TICKS, 1);
    }

    /* Find the outer tick values. Add 0.0 to prevent getting -0.0. */
    axisMin = tickMin = floor(min / step) * step + 0.0;
    axisMax = tickMax = ceil(max / step) * step + 0.0;

    nTicks = Round((tickMax - tickMin) / step) + 1;
    axisPtr->majorSweep.step = step;
    axisPtr->majorSweep.initial = tickMin;
    axisPtr->majorSweep.nSteps = nTicks;

    /*
     * The limits of the axis are either the range of the data
     * ("tight") or at the next outer tick interval ("loose").  The
     * looseness or tightness has to do with how the axis fits the
     * range of data values.  This option is overridden when
     * the user sets an axis limit (by either -min or -max option).
     * The axis limit is always at the selected limit (otherwise we
     * assume that user would have picked a different number).
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

    /* Now calculate the minor tick step and number. */

    if ((axisPtr->reqNumMinorTicks > 0) && ((axisPtr->flags & AXIS_CONFIG_MAJOR) == 0)) {
        nTicks = axisPtr->reqNumMinorTicks - 1;
        step = 1.0 / (nTicks + 1);
    } else {
        nTicks = 0; /* No minor ticks. */
        step = 0.5; /* Don't set the minor tick interval
                     * to 0.0. It makes the GenerateTicks
                     * routine create minor log-scale tick
                     * marks.  */
    }
    axisPtr->minorSweep.initial = axisPtr->minorSweep.step = step;
    axisPtr->minorSweep.nSteps = nTicks;
}

/*
 *----------------------------------------------------------------------
 *
 * SweepTicks --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Axis *axisPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_ResetAxes --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
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
    if ((graphPtr->mode == MODE_STACKED) && (graphPtr->nStacks > 0)) {
        Rbc_ComputeStacks(graphPtr);
    }
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
        (*elemPtr->procsPtr->extentsProc)(elemPtr, &exts);
        GetDataLimits(elemPtr->axes.x, exts.left, exts.right);
        GetDataLimits(elemPtr->axes.y, exts.top, exts.bottom);
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

/*
 * ----------------------------------------------------------------------
 *
 * ResetTextStyles --
 *
 *      Configures axis attributes (font, line width, label, etc) and
 *      allocates a new (possibly shared) graphics context.  Line cap
 *      style is projecting.  This is for the problem of when a tick
 *      sits directly at the end point of the axis.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      Axis resources are allocated (GC, font). Axis layout is
 *      deferred until the height and width of the window are known.
 *
 * ----------------------------------------------------------------------
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

/*
 * ----------------------------------------------------------------------
 *
 * DestroyAxis --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      Resources (font, color, gc, labels, etc.) associated with the
 *      axis are deallocated.
 *
 * ----------------------------------------------------------------------
 */
static void DestroyAxis(Graph *graphPtr, Axis *axisPtr) {
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

/*
 * ----------------------------------------------------------------------
 *
 * AxisOffsets --
 *
 *      Determines the sites of the axis, major and minor ticks,
 *      and title of the axis.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      int margin
 *      int axisOffset
 *      AxisInfo *infoPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * MakeAxisLine --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr - Axis information
 *      int line
 *      Segment2D *segPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void MakeAxisLine(Graph *graphPtr, Axis *axisPtr, int line, Segment2D *segPtr) {
    double min, max;

    min = axisPtr->axisRange.min;
    max = axisPtr->axisRange.max;
    if (axisPtr->logScale) {
        min = EXP10(min);
        max = EXP10(max);
    }
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        segPtr->p.x = Rbc_HMap(graphPtr, axisPtr, min);
        segPtr->q.x = Rbc_HMap(graphPtr, axisPtr, max);
        segPtr->p.y = segPtr->q.y = line;
    } else {
        segPtr->q.x = segPtr->p.x = line;
        segPtr->p.y = Rbc_VMap(graphPtr, axisPtr, min);
        segPtr->q.y = Rbc_VMap(graphPtr, axisPtr, max);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MakeTick --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr - Axis information
 *      double value
 *      int tick - Length of tick
 *      int line - Length axis line
 *      Segment2D *segPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void MakeTick(Graph *graphPtr, Axis *axisPtr, double value, int tick, int line, Segment2D *segPtr) {
    if (axisPtr->logScale) {
        value = EXP10(value);
    }
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        segPtr->p.x = segPtr->q.x = Rbc_HMap(graphPtr, axisPtr, value);
        segPtr->p.y = line;
        segPtr->q.y = tick;
    } else {
        segPtr->p.x = line;
        segPtr->p.y = segPtr->q.y = Rbc_VMap(graphPtr, axisPtr, value);
        segPtr->q.x = tick;
    }
}

/*
 * -----------------------------------------------------------------
 *
 * MapAxis --
 *
 *      Pre-calculates positions of the axis, ticks, and labels (to be
 *      used later when displaying the axis).  Calculates the values
 *      for each major and minor tick and checks to see if they are in
 *      range (the outer ticks may be outside of the range of plotted
 *      values).
 *
 *      Line segments for the minor and major ticks are saved into one
 *      XSegment array so that they can be drawn by a single
 *      XDrawSegments call. The positions of the tick labels are also
 *      computed and saved.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      int offset
 *      int margin
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Line segments and tick labels are saved and used later to draw
 *      the axis.
 *
 * -----------------------------------------------------------------
 */
static void MapAxis(Graph *graphPtr, Axis *axisPtr, int offset, int margin) {
    int arraySize;
    int nMajorTicks, nMinorTicks;
    AxisInfo info;
    Segment2D *segments;
    Segment2D *segPtr;

    AxisOffsets(graphPtr, axisPtr, margin, offset, &info);

    /* Save all line coordinates in an array of line segments. */

    if (axisPtr->segments != NULL) {
        ckfree((char *)axisPtr->segments);
    }
    nMajorTicks = nMinorTicks = 0;
    if (axisPtr->t1Ptr != NULL) {
        nMajorTicks = axisPtr->t1Ptr->nTicks;
    }
    if (axisPtr->t2Ptr != NULL) {
        nMinorTicks = axisPtr->t2Ptr->nTicks;
    }
    arraySize = 1 + (nMajorTicks * (nMinorTicks + 1));
    segments = (Segment2D *)ckalloc(arraySize * sizeof(Segment2D));
    assert(segments);

    segPtr = segments;
    if (axisPtr->lineWidth > 0) {
        /* Axis baseline */
        MakeAxisLine(graphPtr, axisPtr, info.axis, segPtr);
        segPtr++;
    }
    if (axisPtr->showTicks) {
        double t1, t2;
        double labelPos;
        register int i, j;
        int isHoriz;
        TickLabel *labelPtr;
        Rbc_ChainLink *linkPtr;
        Segment2D seg;

        isHoriz = AxisIsHorizontal(graphPtr, axisPtr);
        for (i = 0; i < axisPtr->t1Ptr->nTicks; i++) {
            t1 = axisPtr->t1Ptr->values[i];
            /* Minor ticks */
            for (j = 0; j < axisPtr->t2Ptr->nTicks; j++) {
                t2 = t1 + (axisPtr->majorSweep.step * axisPtr->t2Ptr->values[j]);
                if (InRange(t2, &axisPtr->axisRange)) {
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
    axisPtr->segments = segments;
    axisPtr->nSegments = segPtr - segments;
    assert(axisPtr->nSegments <= arraySize);
}

/*
 *----------------------------------------------------------------------
 *
 * AdjustViewport --
 *
 *      Adjusts the offsets of the viewport according to the scroll mode.
 *      This is to accommodate both "listbox" and "canvas" style scrolling.
 *
 *      "canvas"    The viewport scrolls within the range of world
 *              coordinates.  This way the viewport always displays
 *              a full page of the world.  If the world is smaller
 *              than the viewport, then (bizarrely) the world and
 *              viewport are inverted so that the world moves up
 *              and down within the viewport.
 *
 *      "listbox"    The viewport can scroll beyond the range of world
 *              coordinates.  Every entry can be displayed at the
 *              top of the viewport.  This also means that the
 *              scrollbar thumb weirdly shrinks as the last entry
 *              is scrolled upward.
 *
 * Parameters:
 *      double offset
 *      double windowSize
 *
 * Results:
 *      The corrected offset is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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

/*
 *----------------------------------------------------------------------
 *
 * GetAxisScrollInfo --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *      double *offsetPtr
 *      double windowSize
 *      double scrollUnits
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int GetAxisScrollInfo(Tcl_Interp *interp, int objc, Tcl_Obj *const objv[], double *offsetPtr, double windowSize,
                             double scrollUnits) {
    char c;
    const char *s;
    unsigned int length;
    double offset;
    int count;
    double fract;

    offset = *offsetPtr;
    s = Tcl_GetString(objv[0]);
    c = s[0];
    length = strlen(s);
    if ((c == 's') && (strncmp(s, "scroll", length) == 0)) {
        assert(objc == 3);
        /* scroll number unit/page */
        if (Tcl_GetIntFromObj(interp, objv[1], &count) != TCL_OK) {
            return TCL_ERROR;
        }
        s = Tcl_GetString(objv[2]);
        c = s[0];
        length = strlen(s);
        if ((c == 'u') && (strncmp(s, "units", length) == 0)) {
            fract = (double)count * scrollUnits;
        } else if ((c == 'p') && (strncmp(s, "pages", length) == 0)) {
            /* A page is 90% of the view-able window. */
            fract = (double)count * windowSize * 0.9;
        } else {
            Tcl_AppendResult(interp, "unknown \"scroll\" units \"", s, "\"", (char *)NULL);
            return TCL_ERROR;
        }
        offset += fract;
    } else if ((c == 'm') && (strncmp(s, "moveto", length) == 0)) {
        assert(objc == 2);
        /* moveto fraction */
        if (Tcl_GetDoubleFromObj(interp, objv[1], &fract) != TCL_OK) {
            return TCL_ERROR;
        }
        offset = fract;
    } else {
        /* Treat like "scroll units" */
        if (Tcl_GetInt(interp, s, &count) != TCL_OK) {
            return TCL_ERROR;
        }
        fract = (double)count * scrollUnits;
        offset += fract;
        /* CHECK THIS: return TCL_OK; */
    }
    *offsetPtr = AdjustViewport(offset, windowSize);
    return TCL_OK;
}

/*
 * -----------------------------------------------------------------
 *
 * DrawAxis --
 *
 *      Draws the axis, ticks, and labels onto the canvas.
 *
 *      Initializes and passes text attribute information through
 *      TextStyle structure.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable
 *      Axis *axisPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Axis gets drawn on window.
 *
 * -----------------------------------------------------------------
 */
static void DrawAxis(Graph *graphPtr, Drawable drawable, Axis *axisPtr) {
    if (axisPtr->border != NULL) {
        Rbc_Fill3DRectangle(
            graphPtr->tkwin, drawable, axisPtr->border, axisPtr->region.left + graphPtr->plotBorderWidth,
            axisPtr->region.top + graphPtr->plotBorderWidth, axisPtr->region.right - axisPtr->region.left,
            axisPtr->region.bottom - axisPtr->region.top, axisPtr->borderWidth, axisPtr->relief);
    }
    if (axisPtr->title != NULL) {
        Rbc_DrawText(graphPtr->tkwin, drawable, axisPtr->title, &axisPtr->titleTextStyle, (int)axisPtr->titlePos.x,
                     (int)axisPtr->titlePos.y);
    }
    if (axisPtr->scrollCmdPrefix != NULL) {
        double viewWidth, viewMin, viewMax;
        double worldWidth, worldMin, worldMax;
        double fract;
        int isHoriz;

        worldMin = axisPtr->valueRange.min;
        worldMax = axisPtr->valueRange.max;
        if (DEFINED(axisPtr->scrollMin)) {
            worldMin = axisPtr->scrollMin;
        }
        if (DEFINED(axisPtr->scrollMax)) {
            worldMax = axisPtr->scrollMax;
        }
        viewMin = axisPtr->min;
        viewMax = axisPtr->max;
        if (viewMin < worldMin) {
            viewMin = worldMin;
        }
        if (viewMax > worldMax) {
            viewMax = worldMax;
        }
        if (axisPtr->logScale) {
            worldMin = log10(worldMin);
            worldMax = log10(worldMax);
            viewMin = log10(viewMin);
            viewMax = log10(viewMax);
        }
        worldWidth = worldMax - worldMin;
        viewWidth = viewMax - viewMin;
        isHoriz = AxisIsHorizontal(graphPtr, axisPtr);

        if (isHoriz != axisPtr->descending) {
            fract = (viewMin - worldMin) / worldWidth;
        } else {
            fract = (worldMax - viewMax) / worldWidth;
        }
        fract = AdjustViewport(fract, viewWidth / worldWidth);

        if (isHoriz != axisPtr->descending) {
            viewMin = (fract * worldWidth);
            axisPtr->min = viewMin + worldMin;
            axisPtr->max = axisPtr->min + viewWidth;
            viewMax = viewMin + viewWidth;
            if (axisPtr->logScale) {
                axisPtr->min = EXP10(axisPtr->min);
                axisPtr->max = EXP10(axisPtr->max);
            }
            Rbc_UpdateScrollbar(graphPtr->interp, axisPtr->scrollCmdPrefix, (viewMin / worldWidth),
                                (viewMax / worldWidth));
        } else {
            viewMax = (fract * worldWidth);
            axisPtr->max = worldMax - viewMax;
            axisPtr->min = axisPtr->max - viewWidth;
            viewMin = viewMax + viewWidth;
            if (axisPtr->logScale) {
                axisPtr->min = EXP10(axisPtr->min);
                axisPtr->max = EXP10(axisPtr->max);
            }
            Rbc_UpdateScrollbar(graphPtr->interp, axisPtr->scrollCmdPrefix, (viewMax / worldWidth),
                                (viewMin / worldWidth));
        }
    }
    if (axisPtr->showTicks) {
        register Rbc_ChainLink *linkPtr;
        TickLabel *labelPtr;

        for (linkPtr = Rbc_ChainFirstLink(axisPtr->tickLabels); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            /* Draw major tick labels */
            labelPtr = Rbc_ChainGetValue(linkPtr);
            Rbc_DrawText(graphPtr->tkwin, drawable, labelPtr->string, &axisPtr->tickTextStyle,
                         (int)labelPtr->anchorPos.x, (int)labelPtr->anchorPos.y);
        }
    }
    if ((axisPtr->nSegments > 0) && (axisPtr->lineWidth > 0)) {
        /* Draw the tick marks and axis line. */
        Rbc_Draw2DSegments(graphPtr->display, drawable, axisPtr->tickGC, axisPtr->segments, axisPtr->nSegments);
    }
}

/*
 * -----------------------------------------------------------------
 *
 * AxisToPostScript --
 *
 *      Generates PostScript output to draw the axis, ticks, and
 *      labels.
 *
 *      Initializes and passes text attribute information through
 *      TextStyle structure.
 *
 * Parameters:
 *      PsToken psToken
 *      Axis *axisPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      PostScript output is left in the graphPtr->interp result;
 *
 * -----------------------------------------------------------------
 */
static void AxisToPostScript(PsToken psToken, Axis *axisPtr) {
    if (axisPtr->title != NULL) {
        Rbc_TextToPostScript(psToken, axisPtr->title, &axisPtr->titleTextStyle, axisPtr->titlePos.x,
                             axisPtr->titlePos.y);
    }
    if (axisPtr->showTicks) {
        register Rbc_ChainLink *linkPtr;
        TickLabel *labelPtr;

        for (linkPtr = Rbc_ChainFirstLink(axisPtr->tickLabels); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            labelPtr = Rbc_ChainGetValue(linkPtr);
            Rbc_TextToPostScript(psToken, labelPtr->string, &axisPtr->tickTextStyle, labelPtr->anchorPos.x,
                                 labelPtr->anchorPos.y);
        }
    }
    if ((axisPtr->nSegments > 0) && (axisPtr->lineWidth > 0)) {
        Rbc_LineAttributesToPostScript(psToken, axisPtr->tickTextStyle.color, axisPtr->lineWidth, (Rbc_Dashes *)NULL,
                                       CapButt, JoinMiter);
        Rbc_2DSegmentsToPostScript(psToken, axisPtr->segments, axisPtr->nSegments);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * MakeGridLine --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      double value
 *      Segment2D *segPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void MakeGridLine(Graph *graphPtr, Axis *axisPtr, double value, Segment2D *segPtr) {
    if (axisPtr->logScale) {
        value = EXP10(value);
    }
    /* Grid lines run orthogonally to the axis */
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        segPtr->p.y = graphPtr->top;
        segPtr->q.y = graphPtr->bottom;
        segPtr->p.x = segPtr->q.x = Rbc_HMap(graphPtr, axisPtr, value);
    } else {
        segPtr->p.x = graphPtr->left;
        segPtr->q.x = graphPtr->right;
        segPtr->p.y = segPtr->q.y = Rbc_VMap(graphPtr, axisPtr, value);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetAxisSegments --
 *
 *      Assembles the grid lines associated with an axis. Generates
 *      tick positions if necessary (this happens when the axis is
 *      not a logical axis too).
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      Segment2D **segPtrPtr
 *      int *nSegmentsPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_GetAxisSegments(Graph *graphPtr, Axis *axisPtr, Segment2D **segPtrPtr, int *nSegmentsPtr) {
    int needed;
    Ticks *t1Ptr, *t2Ptr;
    register int i;
    double value;
    Segment2D *segments, *segPtr;

    *nSegmentsPtr = 0;
    *segPtrPtr = NULL;
    if (axisPtr == NULL) {
        return;
    }
    t1Ptr = axisPtr->t1Ptr;
    if (t1Ptr == NULL) {
        t1Ptr = GenerateTicks(&axisPtr->majorSweep);
    }
    t2Ptr = axisPtr->t2Ptr;
    if (t2Ptr == NULL) {
        t2Ptr = GenerateTicks(&axisPtr->minorSweep);
    }

    needed = t1Ptr->nTicks;
    if (graphPtr->gridPtr->minorGrid) {
        needed += (t1Ptr->nTicks * t2Ptr->nTicks);
    }
    if (needed == 0) {
        return;
    }
    segments = (Segment2D *)ckalloc(sizeof(Segment2D) * needed);
    if (segments == NULL) {
        return; /* Can't allocate memory for grid. */
    }

    segPtr = segments;
    for (i = 0; i < t1Ptr->nTicks; i++) {
        value = t1Ptr->values[i];
        if (graphPtr->gridPtr->minorGrid) {
            register int j;
            double subValue;

            for (j = 0; j < t2Ptr->nTicks; j++) {
                subValue = value + (axisPtr->majorSweep.step * t2Ptr->values[j]);
                if (InRange(subValue, &axisPtr->axisRange)) {
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

    if (t1Ptr != axisPtr->t1Ptr) {
        ckfree((char *)t1Ptr); /* Free generated ticks. */
    }
    if (t2Ptr != axisPtr->t2Ptr) {
        ckfree((char *)t2Ptr); /* Free generated ticks. */
    }
    *nSegmentsPtr = segPtr - segments;
    assert(*nSegmentsPtr <= needed);
    *segPtrPtr = segments;
}

/*
 *----------------------------------------------------------------------
 *
 * GetAxisGeometry --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void GetAxisGeometry(Graph *graphPtr, Axis *axisPtr) {
    int height;

    FreeLabels(axisPtr->tickLabels);
    height = 0;
    if (axisPtr->lineWidth > 0) {
        /* Leave room for axis baseline (and pad) */
        height += axisPtr->lineWidth + 2;
    }
    if (axisPtr->showTicks) {
        int pad;
        register int i, nLabels;
        int lw, lh;
        double x, x2;
        int maxWidth, maxHeight;
        TickLabel *labelPtr;

        SweepTicks(axisPtr);

        if (axisPtr->t1Ptr->nTicks < 0) {
            fprintf(stderr, "%s major ticks can't be %d\n", axisPtr->name, axisPtr->t1Ptr->nTicks);
            abort();
        }
        if (axisPtr->t1Ptr->nTicks > MAXTICKS) {
            fprintf(stderr, "too big, %s major ticks can't be %d\n", axisPtr->name, axisPtr->t1Ptr->nTicks);
            abort();
        }

        maxHeight = maxWidth = 0;
        nLabels = 0;
        for (i = 0; i < axisPtr->t1Ptr->nTicks; i++) {
            x2 = x = axisPtr->t1Ptr->values[i];
            if (axisPtr->labelOffset) {
                x2 += axisPtr->majorSweep.step * 0.5;
            }
            if (!InRange(x2, &axisPtr->axisRange)) {
                continue;
            }
            labelPtr = MakeLabel(graphPtr, axisPtr, x);
            Rbc_ChainAppend(axisPtr->tickLabels, labelPtr);
            nLabels++;
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
                lw = ROUND(rotWidth);
                lh = ROUND(rotHeight);
            }
            if (maxWidth < lw) {
                maxWidth = lw;
            }
            if (maxHeight < lh) {
                maxHeight = lh;
            }
        }
        assert(nLabels <= axisPtr->t1Ptr->nTicks);

        /* Because the axis cap style is "CapProjecting", we need to
         * account for an extra 1.5 linewidth at the end of each
         * line.  */

        pad = ((axisPtr->lineWidth * 15) / 10);

        if (AxisIsHorizontal(graphPtr, axisPtr)) {
            height += maxHeight + pad;
        } else {
            height += maxWidth + pad;
        }
        if (axisPtr->lineWidth > 0) {
            /* Distance from axis line to tick label. */
            height += AXIS_TITLE_PAD;
            height += ABS(axisPtr->tickLength);
        }
    }

    if (axisPtr->title != NULL) {
        if (axisPtr->titleAlternate) {
            if (height < axisPtr->titleHeight) {
                height = axisPtr->titleHeight;
            }
        } else {
            height += axisPtr->titleHeight + AXIS_TITLE_PAD;
        }
    }

    /* Correct for orientation of the axis. */
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        axisPtr->height = height;
    } else {
        axisPtr->width = height;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * GetMarginGeometry --
 *
 *      Examines all the axes in the given margin and determines the
 *      area required to display them.
 *
 *      Note: For multiple axes, the titles are displayed in another
 *            margin. So we must keep track of the widest title.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Margin *marginPtr
 *
 * Results:
 *      Returns the width or height of the margin, depending if it
 *      runs horizontally along the graph or vertically.
 *
 * Side Effects:
 *      The area width and height set in the margin.  Note again that
 *      this may be corrected later (mulitple axes) to adjust for
 *      the longest title in another margin.
 *
 *----------------------------------------------------------------------
 */
static int GetMarginGeometry(Graph *graphPtr, Margin *marginPtr) {
    Rbc_ChainLink *linkPtr;
    Axis *axisPtr;
    int width, height;
    int isHoriz;
    int length, count;

    isHoriz = HORIZMARGIN(marginPtr);
    /* Count the number of visible axes. */
    count = 0;
    length = width = height = 0;
    for (linkPtr = Rbc_ChainFirstLink(marginPtr->axes); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
        axisPtr = Rbc_ChainGetValue(linkPtr);
        if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
            count++;
            if (graphPtr->flags & GET_AXIS_GEOMETRY) {
                GetAxisGeometry(graphPtr, axisPtr);
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
    /* Enforce a minimum size for margins. */
    if (width < 3) {
        width = 3;
    }
    if (height < 3) {
        height = 3;
    }
    marginPtr->nAxes = count;
    marginPtr->axesTitleLength = length;
    marginPtr->width = width;
    marginPtr->height = height;
    marginPtr->axesOffset = (HORIZMARGIN(marginPtr)) ? height : width;
    return marginPtr->axesOffset;
}

/*
 *----------------------------------------------------------------------
 *
 * ComputeMargins --
 *
 *      Computes the size of the margins and the plotting area.  We
 *      first compute the space needed for the axes in each margin.
 *      Then how much space the legend will occupy.  Finally, if the
 *      user has requested a margin size, we override the computed
 *      value.
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
 *---------------------------------------------------------------------- */
static void ComputeMargins(Graph *graphPtr) {
    int left, right, top, bottom;
    int width, height;
    int insets;

    /*
     * Step 1:    Compute the amount of space needed to display the
     *        axes (there many be 0 or more) associated with the
     *        margin.
     */
    top = GetMarginGeometry(graphPtr, &graphPtr->topMargin);
    bottom = GetMarginGeometry(graphPtr, &graphPtr->bottomMargin);
    left = GetMarginGeometry(graphPtr, &graphPtr->leftMargin);
    right = GetMarginGeometry(graphPtr, &graphPtr->rightMargin);

    /*
     * Step 2:  Add the graph title height to the top margin.
     */
    if (graphPtr->title != NULL) {
        top += graphPtr->titleTextStyle.height;
    }
    insets = 2 * (graphPtr->inset + graphPtr->plotBorderWidth);

    /*
     * Step 3:  Use the current estimate of the plot area to compute
     *        the legend size.  Add it to the proper margin.
     */
    width = graphPtr->width - (insets + left + right);
    height = graphPtr->height - (insets + top + bottom);
    Rbc_MapLegend(graphPtr->legend, width, height);
    if (!Rbc_LegendIsHidden(graphPtr->legend)) {
        switch (Rbc_LegendSite(graphPtr->legend)) {
        case LEGEND_RIGHT:
            right += Rbc_LegendWidth(graphPtr->legend) + 2;
            break;
        case LEGEND_LEFT:
            left += Rbc_LegendWidth(graphPtr->legend) + 2;
            break;
        case LEGEND_TOP:
            top += Rbc_LegendHeight(graphPtr->legend) + 2;
            break;
        case LEGEND_BOTTOM:
            bottom += Rbc_LegendHeight(graphPtr->legend) + 2;
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
     * Step 5:    If necessary, correct for the requested plot area
     *        aspect ratio.
     */
    if (graphPtr->aspect > 0.0) {
        double ratio;

        /*
         * Shrink one dimension of the plotarea to fit the requested
         * width/height aspect ratio.
         */
        ratio = (double)width / (double)height;
        if (ratio > graphPtr->aspect) {
            int scaledWidth;

            /* Shrink the width. */
            scaledWidth = (int)(height * graphPtr->aspect);
            if (scaledWidth < 1) {
                scaledWidth = 1;
            }
            right += (width - scaledWidth); /* Add the difference to
                                             * the right margin. */
            /* CHECK THIS: width = scaledWidth; */
        } else {
            int scaledHeight;

            /* Shrink the height. */
            scaledHeight = (int)(width / graphPtr->aspect);
            if (scaledHeight < 1) {
                scaledHeight = 1;
            }
            top += (height - scaledHeight); /* Add the difference to
                                             * the top margin. */
            /* CHECK THIS: height = scaledHeight; */
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

    graphPtr->leftMargin.width = left;
    graphPtr->rightMargin.width = right;
    graphPtr->topMargin.height = top;
    graphPtr->bottomMargin.height = bottom;

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

/*
 * -----------------------------------------------------------------
 *
 * Rbc_LayoutMargins --
 *
 *      Calculate the layout of the graph.  Based upon the data,
 *      axis limits, X and Y titles, and title height, determine
 *      the cavity left which is the plotting surface.  The first
 *      step get the data and axis limits for calculating the space
 *      needed for the top, bottom, left, and right margins.
 *
 *      1) The LEFT margin is the area from the left border to the
 *         Y axis (not including ticks). It composes the border
 *         width, the width an optional Y axis label and its padding,
 *         and the tick numeric labels. The Y axis label is rotated
 *         90 degrees so that the width is the font height.
 *
 *      2) The RIGHT margin is the area from the end of the graph
 *         to the right window border. It composes the border width,
 *         some padding, the font height (this may be dubious. It
 *         appears to provide a more even border), the max of the
 *         legend width and 1/2 max X tick number. This last part is
 *         so that the last tick label is not clipped.
 *
 *               Window Width
 *          ___________________________________________________________
 *          |          |                               |               |
 *          |          |   TOP  height of title        |               |
 *          |          |                               |               |
 *          |          |           x2 title            |               |
 *          |          |                               |               |
 *          |          |        height of x2-axis      |               |
 *          |__________|_______________________________|_______________|  W
 *          |          | -plotpady                     |               |  i
 *          |__________|_______________________________|_______________|  n
 *          |          | top                   right   |               |  d
 *          |          |                               |               |  o
 *          |   LEFT   |                               |     RIGHT     |  w
 *          |          |                               |               |
 *          | y        |     Free area = 104%          |      y2       |  H
 *          |          |     Plotting surface = 100%   |               |  e
 *          | t        |     Tick length = 2 + 2%      |      t        |  i
 *          | i        |                               |      i        |  g
 *          | t        |                               |      t  legend|  h
 *          | l        |                               |      l   width|  t
 *          | e        |                               |      e        |
 *          |    height|                               |height         |
 *          |       of |                               | of            |
 *          |    y-axis|                               |y2-axis        |
 *          |          |                               |               |
 *          |          |origin 0,0                     |               |
 *          |__________|_left_________________bottom___|_______________|
 *          |          |-plotpady                      |               |
 *          |__________|_______________________________|_______________|
 *          |          | (xoffset, yoffset)            |               |
 *          |          |                               |               |
 *          |          |       height of x-axis        |               |
 *          |          |                               |               |
 *          |          |   BOTTOM   x title            |               |
 *          |__________|_______________________________|_______________|
 *
 *      3) The TOP margin is the area from the top window border to the top
 *         of the graph. It composes the border width, twice the height of
 *         the title font (if one is given) and some padding between the
 *         title.
 *
 *      4) The BOTTOM margin is area from the bottom window border to the
 *         X axis (not including ticks). It composes the border width, the height
 *         an optional X axis label and its padding, the height of the font
 *         of the tick labels.
 *
 *      The plotting area is between the margins which includes the X and Y axes
 *      including the ticks but not the tick numeric labels. The length of
 *      the ticks and its padding is 5% of the entire plotting area.  Hence the
 *      entire plotting area is scaled as 105% of the width and height of the
 *      area.
 *
 *      The axis labels, ticks labels, title, and legend may or may not be
 *      displayed which must be taken into account.
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
 * -----------------------------------------------------------------
 */
void Rbc_LayoutMargins(Graph *graphPtr) {
    int width, height;
    int titleY;
    int left, right, top, bottom;

    ComputeMargins(graphPtr);
    left = graphPtr->leftMargin.width + graphPtr->inset + graphPtr->plotBorderWidth;
    right = graphPtr->rightMargin.width + graphPtr->inset + graphPtr->plotBorderWidth;
    top = graphPtr->topMargin.height + graphPtr->inset + graphPtr->plotBorderWidth;
    bottom = graphPtr->bottomMargin.height + graphPtr->inset + graphPtr->plotBorderWidth;

    /* Based upon the margins, calculate the space left for the graph. */
    width = graphPtr->width - (left + right);
    height = graphPtr->height - (top + bottom);
    if (width < 1) {
        width = 1;
    }
    if (height < 1) {
        height = 1;
    }
    graphPtr->left = left;
    graphPtr->right = left + width;
    graphPtr->bottom = top + height;
    graphPtr->top = top;

    graphPtr->vOffset = top + graphPtr->padTop;
    graphPtr->vRange = height - PADDING(graphPtr->padY);
    graphPtr->hOffset = left + graphPtr->padLeft;
    graphPtr->hRange = width - PADDING(graphPtr->padX);

    if (graphPtr->vRange < 1) {
        graphPtr->vRange = 1;
    }
    if (graphPtr->hRange < 1) {
        graphPtr->hRange = 1;
    }
    graphPtr->hScale = 1.0 / (double)graphPtr->hRange;
    graphPtr->vScale = 1.0 / (double)graphPtr->vRange;

    /*
     * Calculate the placement of the graph title so it is centered within the
     * space provided for it in the top margin
     */
    titleY = graphPtr->titleTextStyle.height;
    graphPtr->titleY = (titleY / 2) + graphPtr->inset;
    graphPtr->titleX = (graphPtr->right + graphPtr->left) / 2;
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureAxis --
 *
 *      Configures axis attributes (font, line width, label, etc).
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      Axis layout is deferred until the height and width of the
 *      window are known.
 *
 * ----------------------------------------------------------------------
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

    axisPtr->tickTextStyle.theta = FMOD(axisPtr->tickTextStyle.theta, 360.0);
    if (axisPtr->tickTextStyle.theta < 0.0) {
        axisPtr->tickTextStyle.theta += 360.0;
    }
    ResetTextStyles(graphPtr, axisPtr);

    axisPtr->titleWidth = axisPtr->titleHeight = 0;
    if (axisPtr->title != NULL) {
        int w, h;

        Rbc_GetTextExtents(&axisPtr->titleTextStyle, axisPtr->title, &w, &h);
        axisPtr->titleWidth = (short int)w;
        axisPtr->titleHeight = (short int)h;
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

/*
 * ----------------------------------------------------------------------
 *
 * CreateAxis --
 *
 *      Create and initialize a structure containing information to
 *      display a graph axis.
 *
 * Parameters:
 *      Graph *graphPtr
 *      char *name - Identifier for axis.
 *      int margin
 *
 * Results:
 *      The return value is a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static Axis *CreateAxis(Graph *graphPtr, char *name, int margin) {
    Axis *axisPtr;
    Tcl_HashEntry *hPtr;
    int isNew;

    if (name[0] == '-') {
        Tcl_AppendResult(graphPtr->interp, "name of axis \"", name, "\" can't start with a '-'", (char *)NULL);
        return NULL;
    }
    hPtr = Tcl_CreateHashEntry(&graphPtr->axes.table, name, &isNew);
    if (!isNew) {
        axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        if (!axisPtr->deletePending) {
            Tcl_AppendResult(graphPtr->interp, "axis \"", name, "\" already exists in \"", Tk_PathName(graphPtr->tkwin),
                             "\"", (char *)NULL);
            return NULL;
        }
        axisPtr->deletePending = FALSE;
    } else {
        axisPtr = RbcCalloc(1, sizeof(Axis));
        assert(axisPtr);

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

/*
 *----------------------------------------------------------------------
 *
 * NameToAxis --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr - Graph widget record.
 *      const char *name - Name of the axis to be searched for.
 *      Axis **axisPtrPtr - (out) Pointer to found axis structure.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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
    Tcl_AppendResult(graphPtr->interp, "can't find axis \"", name, "\" in \"", Tk_PathName(graphPtr->tkwin), "\"",
                     (char *)NULL);
    *axisPtrPtr = NULL;
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * GetAxis --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      const char *axisName
 *      Rbc_Uid classUid
 *      Axis **axisPtrPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
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
            Tcl_AppendResult(graphPtr->interp, "axis \"", axisName, "\" is already in use on an opposite ",
                             axisPtr->classUid, "-axis", (char *)NULL);
            return TCL_ERROR;
        }
        axisPtr->refCount++;
    }
    *axisPtrPtr = axisPtr;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * FreeAxis --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void FreeAxis(Graph *graphPtr, Axis *axisPtr) {
    axisPtr->refCount--;
    if ((axisPtr->deletePending) && (axisPtr->refCount == 0)) {
        DestroyAxis(graphPtr, axisPtr);
    }
}

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

void Rbc_FreeAxisReference(Graph *graphPtr, Axis *axisPtr) {
    if (axisPtr != NULL) {
        FreeAxis(graphPtr, axisPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DestroyAxes --
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DefaultAxes --
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

/*----------------------------------------------------------------------
 *
 * BindOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      int margin
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
static int BindOp(Graph *graphPtr, Axis *axisPtr, int margin, int objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;

    return Rbc_ConfigureBindingsFromObj(interp, graphPtr->bindTable, Rbc_MakeAxisTag(graphPtr, axisPtr->name), objc,
                                        objv);
}

/*
 * ----------------------------------------------------------------------
 *
 * CgetOp --
 *
 *      Queries axis attributes (font, line width, label, etc).
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      int margin
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Return value is a standard Tcl result.  If querying configuration
 *      values, the interpreter result will contain the results.
 *
 * Side Effects:
 *      TODO: SIde Effects
 *
 * ----------------------------------------------------------------------
 */
static int CgetOp(Graph *graphPtr, Axis *axisPtr, int margin, int objc, Tcl_Obj *const objv[]) {
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

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureOp --
 *
 *      Queries or resets axis attributes (font, line width, label, etc).
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      int margin
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Return value is a standard Tcl result.  If querying configuration
 *      values, the interppreter result will contain the results.
 *
 * Side Effects:
 *      Axis resources are possibly allocated (GC, font). Axis layout is
 *      deferred until the height and width of the window are known.
 *
 * ----------------------------------------------------------------------
 */
static int ConfigureOp(Graph *graphPtr, Axis *axisPtr, int margin, int objc, Tcl_Obj *const objv[]) {
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

/*
 *--------------------------------------------------------------
 *
 * LimitsOp --
 *
 *      This procedure returns a string representing the axis limits
 *      of the graph.  The format of the string is {left top right bottom}.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      int margin
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Always returns TCL_OK. The interpreter result is set to
 *      a list of the graph axis limits.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int LimitsOp(Graph *graphPtr, Axis *axisPtr, int margin, int objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    double min, max;
    Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

    if (graphPtr->flags & RESET_AXES) {
        Rbc_ResetAxes(graphPtr);
    }
    if (axisPtr->logScale) {
        min = EXP10(axisPtr->axisRange.min);
        max = EXP10(axisPtr->axisRange.max);
    } else {
        min = axisPtr->axisRange.min;
        max = axisPtr->axisRange.max;
    }
    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewDoubleObj(min));
    Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewDoubleObj(max));
    Tcl_SetObjResult(interp, resultObj);
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * InvTransformOp --
 *
 *      Maps the given window coordinate into an axis-value.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      int margin
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result. The interpreter result contains
 *      the axis value. If an error occurred, TCL_ERROR is returned
 *      and the interpreter result will contain an error message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int InvTransformOp(Graph *graphPtr, Axis *axisPtr, int margin, int objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    int x;    /* Integer window coordinate*/
    double y; /* Real graph coordinate */

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
        y = Rbc_InvHMap(graphPtr, axisPtr, (double)x);
    } else {
        y = Rbc_InvVMap(graphPtr, axisPtr, (double)x);
    }
    Tcl_SetObjResult(interp, Tcl_NewDoubleObj(y));
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * TransformOp --
 *
 *      Maps the given axis-value to a window coordinate.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      int margin
 *      int objc - Not used.
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result. The interpreter result contains
 *      the window coordinate. If an error occurred, TCL_ERROR
 *      is returned and the interpreter result will contain an error
 *      message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int TransformOp(Graph *graphPtr, Axis *axisPtr, int margin, int objc, /* Not used. */
                       Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    double x;

    if (graphPtr->flags & RESET_AXES) {
        Rbc_ResetAxes(graphPtr);
    }
    if (Tcl_ExprDoubleObj(interp, objv[0], &x) != TCL_OK) {
        return TCL_ERROR;
    }
    if (AxisIsHorizontal(graphPtr, axisPtr)) {
        x = Rbc_HMap(graphPtr, axisPtr, x);
    } else {
        x = Rbc_VMap(graphPtr, axisPtr, x);
    }
    Tcl_SetObjResult(interp, Tcl_NewIntObj((int)x));
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * UseOp --
 *
 *      Changes the virtual axis used by the logical axis.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Axis *axisPtr
 *      int margin
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.  If the named axis doesn't exist
 *      an error message is set in the interpreter result.
 *
 *      .g xaxis use "abc def gah"
 *      .g xaxis use [lappend abc [.g axis use]]
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int UseOp(Graph *graphPtr, Axis *axisPtr, int margin, int objc, Tcl_Obj *const objv[]) {
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
    if (Tcl_ListObjGetElements(interp, objv[0], &nNames, &names) != TCL_OK) {
        return TCL_ERROR;
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
        if (NameToAxis(graphPtr, Tcl_GetString(names[i]), &axisPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (axisPtr->classUid == NULL) {
            axisPtr->classUid = classUid;
        } else if (axisPtr->classUid != classUid) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("wrong type axis \"%s\": "
                                                   "can't use %s type axis.",
                                                   axisPtr->name, classUid));
            return TCL_ERROR;
        }
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

/*
 *----------------------------------------------------------------------
 *
 * BindVirtualOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
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
static int BindVirtualOp(Graph *graphPtr, int objc, Tcl_Obj *const objv[]) {
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
    return Rbc_ConfigureBindingsFromObj(interp, graphPtr->bindTable, Rbc_MakeAxisTag(graphPtr, Tcl_GetString(objv[3])),
                                        objc - 4, objv + 4);
}

/*
 * ----------------------------------------------------------------------
 *
 * CreateVirtualOp --
 *
 *      Creates a new axis.
 *
 * Parameters:
 *      Graph *graphPtr
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int CreateVirtualOp(Graph *graphPtr, int objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;

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
    DestroyAxis(graphPtr, axisPtr);

    return TCL_ERROR;
}

/*
 * ----------------------------------------------------------------------
 *
 * CgetVirtualOp --
 *
 *      Queries axis attributes (font, line width, label, etc).
 *
 * Parameters:
 *      Graph *graphPtr
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Return value is a standard Tcl result.  If querying configuration
 *      values, the interpreter result will contain the results.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int CgetVirtualOp(Graph *graphPtr, int objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;

    if (NameToAxis(graphPtr, Tcl_GetString(objv[3]), &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return CgetOp(graphPtr, axisPtr, /*dummy*/ 0, objc - 4, objv + 4);
}

/*
 * ----------------------------------------------------------------------
 *
 * ConfigureVirtualOp --
 *
 *      Queries or resets axis attributes (font, line width, label, etc).
 *
 * Parameters:
 *      Graph *graphPtr
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Return value is a standard Tcl result.  If querying configuration
 *      values, the interpreter result will contain the results.
 *
 * Side Effects:
 *      Axis resources are possibly allocated (GC, font). Axis layout is
 *      deferred until the height and width of the window are known.
 *
 * ----------------------------------------------------------------------
 */
static int ConfigureVirtualOp(Graph *graphPtr, int objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;
    int nNames, nOpts;
    Tcl_Obj *const *options;
    register int i;

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

/*
 * ----------------------------------------------------------------------
 *
 * DeleteVirtualOp --
 *
 *      Deletes one or more axes.  The actual removal may be deferred
 *      until the axis is no longer used by any element. The axis
 *      can't be referenced by its name any longer and it may be
 *      recreated.
 *
 * Parameters:
 *      Graph *graphPtr
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int DeleteVirtualOp(Graph *graphPtr, int objc, Tcl_Obj *const objv[]) {
    register int i;
    Axis *axisPtr;

    for (i = 3; i < objc; i++) {
        if (NameToAxis(graphPtr, Tcl_GetString(objv[i]), &axisPtr) != TCL_OK) {
            return TCL_ERROR;
        }
        axisPtr->deletePending = TRUE;
        if (axisPtr->refCount == 0) {
            DestroyAxis(graphPtr, axisPtr);
        }
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * GetOp --
 *
 *      Returns the name of the picked axis (using the axis
 *      bind operation).  Right now, the only name accepted is
 *      "current".
 *
 * Parameters:
 *      Graph *graphPtr
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      A standard Tcl result.  The interpreter result will contain
 *      the name of the axis.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int GetOp(Graph *graphPtr, int objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    register Axis *axisPtr;

    axisPtr = (Axis *)Rbc_GetCurrentItem(graphPtr->bindTable);
    /* Report only on axes. */
    if ((axisPtr != NULL) &&
        ((axisPtr->classUid == rbcXAxisUid) || (axisPtr->classUid == rbcYAxisUid) || (axisPtr->classUid == NULL))) {
        const char *s = Tcl_GetString(objv[3]);
        const char c = s[0];

        if ((c == 'c') && (strcmp(s, "current") == 0)) {
            //            Tcl_SetResult(interp, axisPtr->name, TCL_VOLATILE);
            Tcl_SetObjResult(interp, Tcl_NewStringObj(axisPtr->name, -1));
        } else if ((c == 'd') && (strcmp(s, "detail") == 0)) {
            //            Tcl_SetResult(interp, axisPtr->detail, TCL_VOLATILE);
            Tcl_SetObjResult(interp, Tcl_NewStringObj(axisPtr->detail, -1));
        }
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * InvTransformVirtualOp --
 *
 *      Maps the given window coordinate into an axis-value.
 *
 * Parameters:
 *      Graph *graphPtr
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result. The interpreter result contains
 *      the axis value. If an error occurred, TCL_ERROR is returned
 *      and the interpreter result will contain an error message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int InvTransformVirtualOp(Graph *graphPtr, int objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;

    if (NameToAxis(graphPtr, Tcl_GetString(objv[3]), &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return InvTransformOp(graphPtr, axisPtr, /*dummy*/ 0, objc - 4, objv + 4);
}

/*
 *--------------------------------------------------------------
 *
 * LimitsVirtualOp --
 *
 *      This procedure returns a string representing the axis limits
 *      of the graph.  The format of the string is { left top right bottom}.
 *
 * Parameters:
 *      Graph *graphPtr
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Always returns TCL_OK.  The interpreter result is set to
 *      a list of the graph axis limits.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int LimitsVirtualOp(Graph *graphPtr, int objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;

    if (NameToAxis(graphPtr, Tcl_GetString(objv[3]), &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return LimitsOp(graphPtr, axisPtr, /*dummy*/ 0, objc - 4, objv + 4);
}

/*
 * ----------------------------------------------------------------------
 *
 * NamesVirtualOp --
 *
 *      Return a list of the names of all the axes.
 *
 * Parameters:
 *      Graph *graphPtr
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int NamesVirtualOp(Graph *graphPtr, int objc, Tcl_Obj *const objv[]) {
    Tcl_Interp *interp = graphPtr->interp;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Axis *axisPtr;
    register int i;
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

/*
 * ----------------------------------------------------------------------
 *
 * TransformVirtualOp --
 *
 *    Maps the given axis-value to a window coordinate.
 *
 * Parameters:
 *      Graph *graphPtr
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      Returns a standard Tcl result. The interpreter result contains
 *      the window coordinate. If an error occurred, TCL_ERROR
 *      is returned and the interpreter result will contain an error
 *      message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int TransformVirtualOp(Graph *graphPtr, int objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;

    if (NameToAxis(graphPtr, Tcl_GetString(objv[3]), &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return TransformOp(graphPtr, axisPtr, /*dummy*/ 0, objc - 4, objv + 4);
}

/*
 *----------------------------------------------------------------------
 *
 * ViewOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
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
static int ViewOp(Graph *graphPtr, int objc, Tcl_Obj *const objv[]) {
    Axis *axisPtr;
    Tcl_Interp *interp = graphPtr->interp;
    double axisOffset, scrollUnits;
    double fract;
    double viewMin, viewMax, worldMin, worldMax;
    double viewWidth, worldWidth;

    if (NameToAxis(graphPtr, Tcl_GetString(objv[3]), &axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    worldMin = axisPtr->valueRange.min;
    worldMax = axisPtr->valueRange.max;
    /* Override data dimensions with user-selected limits. */
    if (DEFINED(axisPtr->scrollMin)) {
        worldMin = axisPtr->scrollMin;
    }
    if (DEFINED(axisPtr->scrollMax)) {
        worldMax = axisPtr->scrollMax;
    }
    viewMin = axisPtr->min;
    viewMax = axisPtr->max;
    /* Bound the view within scroll region. */
    if (viewMin < worldMin) {
        viewMin = worldMin;
    }
    if (viewMax > worldMax) {
        viewMax = worldMax;
    }
    if (axisPtr->logScale) {
        worldMin = log10(worldMin);
        worldMax = log10(worldMax);
        viewMin = log10(viewMin);
        viewMax = log10(viewMax);
    }
    worldWidth = worldMax - worldMin;
    viewWidth = viewMax - viewMin;

    /* Unlike horizontal axes, vertical axis values run opposite of
     * the scrollbar first/last values.  So instead of pushing the
     * axis minimum around, we move the maximum instead. */

    if (AxisIsHorizontal(graphPtr, axisPtr) != axisPtr->descending) {
        axisOffset = viewMin - worldMin;
        scrollUnits = (double)axisPtr->scrollUnits * graphPtr->hScale;
    } else {
        axisOffset = worldMax - viewMax;
        scrollUnits = (double)axisPtr->scrollUnits * graphPtr->vScale;
    }
    if (objc == 4) {
        Tcl_Obj *resultObj = Tcl_NewListObj(0, NULL);

        /* Note: Bound the fractions between 0.0 and 1.0 to support
         * "canvas"-style scrolling. */
        fract = axisOffset / worldWidth;
        Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewDoubleObj(CLAMP(fract, 0.0, 1.0)));
        fract = (axisOffset + viewWidth) / worldWidth;
        Tcl_ListObjAppendElement(NULL, resultObj, Tcl_NewDoubleObj(CLAMP(fract, 0.0, 1.0)));
        Tcl_SetObjResult(interp, resultObj);
        return TCL_OK;
    }
    fract = axisOffset / worldWidth;
    if (GetAxisScrollInfo(interp, objc - 4, objv + 4, &fract, viewWidth / worldWidth, scrollUnits) != TCL_OK) {
        return TCL_ERROR;
    }
    if (AxisIsHorizontal(graphPtr, axisPtr) != axisPtr->descending) {
        axisPtr->reqMin = (fract * worldWidth) + worldMin;
        axisPtr->reqMax = axisPtr->reqMin + viewWidth;
    } else {
        axisPtr->reqMax = worldMax - (fract * worldWidth);
        axisPtr->reqMin = axisPtr->reqMax - viewWidth;
    }
    if (axisPtr->logScale) {
        axisPtr->reqMin = EXP10(axisPtr->reqMin);
        axisPtr->reqMax = EXP10(axisPtr->reqMax);
    }
    graphPtr->flags |= (GET_AXIS_GEOMETRY | LAYOUT_NEEDED | RESET_AXES);
    Rbc_EventuallyRedrawGraph(graphPtr);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_VirtualAxisOp --
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
int Rbc_VirtualAxisOp(Graph *graphPtr, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]) {
    RbcGrAxisVirtualOpPtr proc;
    int result;
    static Rbc_OpSpec axisOps[] = {
        {"bind", (Rbc_Op)BindVirtualOp, 3, 6, "axisName sequence command"},
        {"cget", (Rbc_Op)CgetVirtualOp, 5, 5, "axisName option"},
        {"configure", (Rbc_Op)ConfigureVirtualOp, 4, 0, "axisName ?axisName?... ?option value?..."},
        {"create", (Rbc_Op)CreateVirtualOp, 4, 0, "axisName ?option value?..."},
        {"delete", (Rbc_Op)DeleteVirtualOp, 3, 0, "?axisName?..."},
        {"get", (Rbc_Op)GetOp, 4, 4, "name"},
        {"invtransform", (Rbc_Op)InvTransformVirtualOp, 5, 5, "axisName value"},
        {"limits", (Rbc_Op)LimitsVirtualOp, 4, 4, "axisName"},
        {"names", (Rbc_Op)NamesVirtualOp, 3, 0, "?pattern?..."},
        {"transform", (Rbc_Op)TransformVirtualOp, 5, 5, "axisName value"},
        {"view", (Rbc_Op)ViewOp, 4, 7, "axisName ?moveto fract? ?scroll number what?"},
        RBC_OPSPEC_END};

    proc = (RbcGrAxisVirtualOpPtr)Rbc_GetOpFromObj(interp, axisOps, RBC_OP_ARG2, objc, objv);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    result = (*proc)(graphPtr, objc, objv);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_AxisOp --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      int margin
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
int Rbc_AxisOp(Graph *graphPtr, int margin, int objc, Tcl_Obj *const objv[]) {
    int result;
    RbcGrAxisOpPtr proc;
    Axis *axisPtr;
    static Rbc_OpSpec axisOps[] = {{"bind", (Rbc_Op)BindOp, 2, 5, "sequence command"},
                                   {"cget", (Rbc_Op)CgetOp, 4, 4, "option"},
                                   {"configure", (Rbc_Op)ConfigureOp, 3, 0, "?option value?..."},
                                   {"invtransform", (Rbc_Op)InvTransformOp, 4, 4, "value"},
                                   {"limits", (Rbc_Op)LimitsOp, 3, 3, ""},
                                   {"transform", (Rbc_Op)TransformOp, 4, 4, "value"},
                                   {"use", (Rbc_Op)UseOp, 3, 4, "?axisName?"},
                                   RBC_OPSPEC_END};

    proc = (RbcGrAxisOpPtr)Rbc_GetOpFromObj(graphPtr->interp, axisOps, RBC_OP_ARG2, objc, objv);
    if (proc == NULL) {
        return TCL_ERROR;
    }
    axisPtr = Rbc_GetFirstAxis(graphPtr->margins[margin].axes);
    result = (*proc)(graphPtr, axisPtr, margin, objc - 3, objv + 3);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MapAxes --
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
void Rbc_MapAxes(Graph *graphPtr) {
    Axis *axisPtr;
    Rbc_Chain *chainPtr;
    Rbc_ChainLink *linkPtr;
    register int margin;
    int offset;

    for (margin = 0; margin < 4; margin++) {
        chainPtr = graphPtr->margins[margin].axes;
        offset = 0;
        for (linkPtr = Rbc_ChainFirstLink(chainPtr); linkPtr != NULL; linkPtr = Rbc_ChainNextLink(linkPtr)) {
            axisPtr = Rbc_ChainGetValue(linkPtr);
            if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DrawAxes --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_DrawAxes(Graph *graphPtr, Drawable drawable) {
    Axis *axisPtr;
    Rbc_ChainLink *linkPtr;
    register int i;

    for (i = 0; i < 4; i++) {
        for (linkPtr = Rbc_ChainFirstLink(graphPtr->margins[i].axes); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            axisPtr = Rbc_ChainGetValue(linkPtr);
            if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
                DrawAxis(graphPtr, drawable, axisPtr);
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_AxesToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_AxesToPostScript(Graph *graphPtr, PsToken psToken) {
    Axis *axisPtr;
    Rbc_ChainLink *linkPtr;
    register int i;

    for (i = 0; i < 4; i++) {
        for (linkPtr = Rbc_ChainFirstLink(graphPtr->margins[i].axes); linkPtr != NULL;
             linkPtr = Rbc_ChainNextLink(linkPtr)) {
            axisPtr = Rbc_ChainGetValue(linkPtr);
            if ((!axisPtr->hidden) && (axisPtr->flags & AXIS_ONSCREEN)) {
                AxisToPostScript(psToken, axisPtr);
            }
        }
    }
}

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_DrawAxisLimits --
 *
 *      Draws the min/max values of the axis in the plotting area.
 *      The text strings are formatted according to the "sprintf"
 *      format descriptors in the limitsFormats array.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Draws the numeric values of the axis limits into the outer
 *      regions of the plotting area.
 *
 * ----------------------------------------------------------------------
 */
void Rbc_DrawAxisLimits(Graph *graphPtr, Drawable drawable) {
    Axis *axisPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Dim2D textDim;
    int isHoriz;
    char *minPtr, *maxPtr;
    char *minFormat, *maxFormat;
    char minString[200], maxString[200];
    int vMin, hMin, vMax, hMax;

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
            sprintf(minString, minFormat, axisPtr->axisRange.min);
        }
        if (maxFormat[0] != '\0') {
            maxPtr = maxString;
            sprintf(maxString, maxFormat, axisPtr->axisRange.max);
        }
        if (axisPtr->descending) {
            char *tmp;

            tmp = minPtr, minPtr = maxPtr, maxPtr = tmp;
        }
        if (maxPtr != NULL) {
            if (isHoriz) {
                axisPtr->limitsTextStyle.theta = 90.0;
                axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SE;
                Rbc_DrawText2(graphPtr->tkwin, drawable, maxPtr, &axisPtr->limitsTextStyle, graphPtr->right, hMax,
                              &textDim);
                hMax -= (textDim.height + SPACING);
            } else {
                axisPtr->limitsTextStyle.theta = 0.0;
                axisPtr->limitsTextStyle.anchor = TK_ANCHOR_NW;
                Rbc_DrawText2(graphPtr->tkwin, drawable, maxPtr, &axisPtr->limitsTextStyle, vMax, graphPtr->top,
                              &textDim);
                vMax += (textDim.width + SPACING);
            }
        }
        if (minPtr != NULL) {
            axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SW;
            if (isHoriz) {
                axisPtr->limitsTextStyle.theta = 90.0;
                Rbc_DrawText2(graphPtr->tkwin, drawable, minPtr, &axisPtr->limitsTextStyle, graphPtr->left, hMin,
                              &textDim);
                hMin -= (textDim.height + SPACING);
            } else {
                axisPtr->limitsTextStyle.theta = 0.0;
                Rbc_DrawText2(graphPtr->tkwin, drawable, minPtr, &axisPtr->limitsTextStyle, vMin, graphPtr->bottom,
                              &textDim);
                vMin += (textDim.width + SPACING);
            }
        }
    } /* Loop on axes */
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_AxisLimitsToPostScript --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      PsToken psToken
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_AxisLimitsToPostScript(Graph *graphPtr, PsToken psToken) {
    Axis *axisPtr;
    Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    double vMin, hMin, vMax, hMax;
    char string[200];
    int textWidth, textHeight;
    char *minFmt, *maxFmt;

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
        if (*maxFmt != '\0') {
            sprintf(string, maxFmt, axisPtr->axisRange.max);
            Rbc_GetTextExtents(&axisPtr->tickTextStyle, string, &textWidth, &textHeight);
            if ((textWidth > 0) && (textHeight > 0)) {
                if (axisPtr->classUid == rbcXAxisUid) {
                    axisPtr->limitsTextStyle.theta = 90.0;
                    axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SE;
                    Rbc_TextToPostScript(psToken, string, &axisPtr->limitsTextStyle, (double)graphPtr->right, hMax);
                    hMax -= (textWidth + SPACING);
                } else {
                    axisPtr->limitsTextStyle.theta = 0.0;
                    axisPtr->limitsTextStyle.anchor = TK_ANCHOR_NW;
                    Rbc_TextToPostScript(psToken, string, &axisPtr->limitsTextStyle, vMax, (double)graphPtr->top);
                    vMax += (textWidth + SPACING);
                }
            }
        }
        if (*minFmt != '\0') {
            sprintf(string, minFmt, axisPtr->axisRange.min);
            Rbc_GetTextExtents(&axisPtr->tickTextStyle, string, &textWidth, &textHeight);
            if ((textWidth > 0) && (textHeight > 0)) {
                axisPtr->limitsTextStyle.anchor = TK_ANCHOR_SW;
                if (axisPtr->classUid == rbcXAxisUid) {
                    axisPtr->limitsTextStyle.theta = 90.0;
                    Rbc_TextToPostScript(psToken, string, &axisPtr->limitsTextStyle, (double)graphPtr->left, hMin);
                    hMin -= (textWidth + SPACING);
                } else {
                    axisPtr->limitsTextStyle.theta = 0.0;
                    Rbc_TextToPostScript(psToken, string, &axisPtr->limitsTextStyle, vMin, (double)graphPtr->bottom);
                    vMin += (textWidth + SPACING);
                }
            }
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetFirstAxis --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Rbc_Chain *chainPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Axis *Rbc_GetFirstAxis(Rbc_Chain *chainPtr) {
    Rbc_ChainLink *linkPtr;

    linkPtr = Rbc_ChainFirstLink(chainPtr);
    if (linkPtr == NULL) {
        return NULL;
    }
    return Rbc_ChainGetValue(linkPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_NearestAxis --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      int x - Point to be tested
 *      int y - Point to be tested
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Axis *Rbc_NearestAxis(Graph *graphPtr, int x, int y) {
    register Tcl_HashEntry *hPtr;
    Tcl_HashSearch cursor;
    Axis *axisPtr;
    int width, height;
    double rotWidth, rotHeight;
    Point2D bbox[5];

    for (hPtr = Tcl_FirstHashEntry(&graphPtr->axes.table, &cursor); hPtr != NULL; hPtr = Tcl_NextHashEntry(&cursor)) {
        axisPtr = (Axis *)Tcl_GetHashValue(hPtr);
        if ((axisPtr->hidden) || (!(axisPtr->flags & AXIS_ONSCREEN))) {
            continue; /* Don't check hidden axes or axes
                       * that are virtual. */
        }
        if (axisPtr->showTicks) {
            register Rbc_ChainLink *linkPtr;
            TickLabel *labelPtr;
            Point2D t;

            for (linkPtr = Rbc_ChainFirstLink(axisPtr->tickLabels); linkPtr != NULL;
                 linkPtr = Rbc_ChainNextLink(linkPtr)) {
                labelPtr = Rbc_ChainGetValue(linkPtr);
                Rbc_GetBoundingBox(labelPtr->width, labelPtr->height, axisPtr->tickTextStyle.theta, &rotWidth,
                                   &rotHeight, bbox);
                width = ROUND(rotWidth);
                height = ROUND(rotHeight);
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
            width = ROUND(rotWidth);
            height = ROUND(rotHeight);
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MakeAxisTag --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Graph *graphPtr
 *      char *tagName
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
ClientData Rbc_MakeAxisTag(Graph *graphPtr, char *tagName) {
    Tcl_HashEntry *hPtr;
    int isNew;

    hPtr = Tcl_CreateHashEntry(&graphPtr->axes.tagTable, tagName, &isNew);
    assert(hPtr);
    return Tcl_GetHashKey(&graphPtr->axes.tagTable, hPtr);
}
