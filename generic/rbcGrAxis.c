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

extern Tk_CustomOption rbcDistanceOption;
extern Tk_CustomOption rbcPositiveDistanceOption;
extern Tk_CustomOption rbcShadowOption;
extern Tk_CustomOption rbcListOption;

static Tk_OptionParseProc StringToLimit;
static Tk_OptionPrintProc LimitToString;
static Tk_OptionParseProc StringToTicks;
static Tk_OptionPrintProc TicksToString;
static Tk_OptionParseProc StringToAxis;
static Tk_OptionPrintProc AxisToString;
static Tk_OptionParseProc StringToAnyAxis;
static Tk_OptionParseProc StringToFormat;
static Tk_OptionPrintProc FormatToString;
static Tk_OptionParseProc StringToLoose;
static Tk_OptionPrintProc LooseToString;

static Tk_CustomOption limitOption = {StringToLimit, LimitToString, (ClientData)0};

static Tk_CustomOption majorTicksOption = {
    StringToTicks,
    TicksToString,
    (ClientData)AXIS_CONFIG_MAJOR,
};
static Tk_CustomOption minorTicksOption = {
    StringToTicks,
    TicksToString,
    (ClientData)AXIS_CONFIG_MINOR,
};
Tk_CustomOption rbcXAxisOption = {StringToAxis, AxisToString, (ClientData)&rbcXAxisUid};
Tk_CustomOption rbcYAxisOption = {StringToAxis, AxisToString, (ClientData)&rbcYAxisUid};
Tk_CustomOption rbcAnyXAxisOption = {StringToAnyAxis, AxisToString, (ClientData)&rbcXAxisUid};
Tk_CustomOption rbcAnyYAxisOption = {StringToAnyAxis, AxisToString, (ClientData)&rbcYAxisUid};
static Tk_CustomOption formatOption = {
    StringToFormat,
    FormatToString,
    (ClientData)0,
};
static Tk_CustomOption looseOption = {
    StringToLoose,
    LooseToString,
    (ClientData)0,
};

/* Axis flags: */

#define DEF_AXIS_COMMAND (char *)NULL
#define DEF_AXIS_DESCENDING "no"
#define DEF_AXIS_FOREGROUND RGB_BLACK
#define DEF_AXIS_FG_MONO RGB_BLACK
#define DEF_AXIS_HIDE "no"
#define DEF_AXIS_JUSTIFY "center"
#define DEF_AXIS_LIMITS_FORMAT (char *)NULL
#define DEF_AXIS_LINE_WIDTH "1"
#define DEF_AXIS_LOGSCALE "no"
#define DEF_AXIS_LOOSE "no"
#define DEF_AXIS_RANGE "0.0"
#define DEF_AXIS_ROTATE "0.0"
#define DEF_AXIS_SCROLL_INCREMENT "10"
#define DEF_AXIS_SHIFTBY "0.0"
#define DEF_AXIS_SHOWTICKS "yes"
#define DEF_AXIS_STEP "0.0"
#define DEF_AXIS_STEP "0.0"
#define DEF_AXIS_SUBDIVISIONS "2"
#define DEF_AXIS_TAGS "all"
#define DEF_AXIS_TICKS "0"

#ifdef WIN32
#define DEF_AXIS_TICK_FONT "{Arial Narrow} 8"
#else
#define DEF_AXIS_TICK_FONT "TkSmallCaptionFont"
#endif

#define DEF_AXIS_TICK_LENGTH "8"
#define DEF_AXIS_TITLE_ALTERNATE "0"
#define DEF_AXIS_TITLE_FG RGB_BLACK
#define DEF_AXIS_TITLE_FONT STD_FONT
#define DEF_AXIS_X_STEP_BARCHART "1.0"
#define DEF_AXIS_X_SUBDIVISIONS_BARCHART "0"
#define DEF_AXIS_BACKGROUND (char *)NULL
#define DEF_AXIS_BORDERWIDTH "0"
#define DEF_AXIS_RELIEF "flat"

static Tk_ConfigSpec configSpecs[] = {
    {TK_CONFIG_DOUBLE, "-autorange", "autoRange", "AutoRange", DEF_AXIS_RANGE, offsetof(Axis, windowSize),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BORDER, "-background", "background", "Background", DEF_AXIS_BACKGROUND, offsetof(Axis, border),
     ALL_GRAPHS | TK_CONFIG_NULL_OK},
    {TK_CONFIG_SYNONYM, "-bg", "background", (char *)NULL, (char *)NULL, 0, 0},
    {TK_CONFIG_CUSTOM, "-bindtags", "bindTags", "BindTags", DEF_AXIS_TAGS, offsetof(Axis, tags),
     ALL_GRAPHS | TK_CONFIG_NULL_OK, &rbcListOption},
    {TK_CONFIG_SYNONYM, "-bd", "borderWidth", (char *)NULL, (char *)NULL, 0, ALL_GRAPHS},
    {TK_CONFIG_CUSTOM, "-borderwidth", "borderWidth", "BorderWidth", DEF_AXIS_BORDERWIDTH, offsetof(Axis, borderWidth),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_AXIS_FOREGROUND, offsetof(Axis, tickTextStyle.color),
     TK_CONFIG_COLOR_ONLY | ALL_GRAPHS},
    {TK_CONFIG_COLOR, "-color", "color", "Color", DEF_AXIS_FG_MONO, offsetof(Axis, tickTextStyle.color),
     TK_CONFIG_MONO_ONLY | ALL_GRAPHS},
    {TK_CONFIG_STRING, "-command", "command", "Command", DEF_AXIS_COMMAND, offsetof(Axis, formatCmd),
     TK_CONFIG_NULL_OK | ALL_GRAPHS},
    {TK_CONFIG_BOOLEAN, "-descending", "descending", "Descending", DEF_AXIS_DESCENDING, offsetof(Axis, descending),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-hide", "hide", "Hide", DEF_AXIS_HIDE, offsetof(Axis, hidden),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_JUSTIFY, "-justify", "justify", "Justify", DEF_AXIS_JUSTIFY, offsetof(Axis, titleTextStyle.justify),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-labeloffset", "labelOffset", "LabelOffset", (char *)NULL, offsetof(Axis, labelOffset),
     ALL_GRAPHS},
    {TK_CONFIG_COLOR, "-limitscolor", "limitsColor", "Color", DEF_AXIS_FOREGROUND,
     offsetof(Axis, limitsTextStyle.color), TK_CONFIG_COLOR_ONLY | ALL_GRAPHS},
    {TK_CONFIG_COLOR, "-limitscolor", "limitsColor", "Color", DEF_AXIS_FG_MONO, offsetof(Axis, limitsTextStyle.color),
     TK_CONFIG_MONO_ONLY | ALL_GRAPHS},
    {TK_CONFIG_FONT, "-limitsfont", "limitsFont", "Font", DEF_AXIS_TICK_FONT, offsetof(Axis, limitsTextStyle.font),
     ALL_GRAPHS},
    {TK_CONFIG_CUSTOM, "-limitsformat", "limitsFormat", "LimitsFormat", (char *)NULL, offsetof(Axis, limitsFormats),
     TK_CONFIG_NULL_OK | ALL_GRAPHS, &formatOption},
    {TK_CONFIG_CUSTOM, "-limitsshadow", "limitsShadow", "Shadow", (char *)NULL, offsetof(Axis, limitsTextStyle.shadow),
     TK_CONFIG_COLOR_ONLY | ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-limitsshadow", "limitsShadow", "Shadow", (char *)NULL, offsetof(Axis, limitsTextStyle.shadow),
     TK_CONFIG_MONO_ONLY | ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-linewidth", "lineWidth", "LineWidth", DEF_AXIS_LINE_WIDTH, offsetof(Axis, lineWidth),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT, &rbcDistanceOption},
    {TK_CONFIG_BOOLEAN, "-logscale", "logScale", "LogScale", DEF_AXIS_LOGSCALE, offsetof(Axis, logScale),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_CUSTOM, "-loose", "loose", "Loose", DEF_AXIS_LOOSE, 0, ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT,
     &looseOption},
    {TK_CONFIG_CUSTOM, "-majorticks", "majorTicks", "MajorTicks", (char *)NULL, offsetof(Axis, t1Ptr),
     TK_CONFIG_NULL_OK | ALL_GRAPHS, &majorTicksOption},
    {TK_CONFIG_CUSTOM, "-max", "max", "Max", (char *)NULL, offsetof(Axis, reqMax), TK_CONFIG_NULL_OK | ALL_GRAPHS,
     &limitOption},
    {TK_CONFIG_CUSTOM, "-min", "min", "Min", (char *)NULL, offsetof(Axis, reqMin), TK_CONFIG_NULL_OK | ALL_GRAPHS,
     &limitOption},
    {TK_CONFIG_CUSTOM, "-minorticks", "minorTicks", "MinorTicks", (char *)NULL, offsetof(Axis, t2Ptr),
     TK_CONFIG_NULL_OK | ALL_GRAPHS, &minorTicksOption},
    {TK_CONFIG_RELIEF, "-relief", "relief", "Relief", DEF_AXIS_RELIEF, offsetof(Axis, relief),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_DOUBLE, "-rotate", "rotate", "Rotate", DEF_AXIS_ROTATE, offsetof(Axis, tickTextStyle.theta),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_STRING, "-scrollcommand", "scrollCommand", "ScrollCommand", (char *)NULL,
     offsetof(Axis, scrollCmdPrefix), ALL_GRAPHS | TK_CONFIG_NULL_OK},
    {TK_CONFIG_CUSTOM, "-scrollincrement", "scrollIncrement", "ScrollIncrement", DEF_AXIS_SCROLL_INCREMENT,
     offsetof(Axis, scrollUnits), ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT, &rbcPositiveDistanceOption},
    {TK_CONFIG_CUSTOM, "-scrollmax", "scrollMax", "ScrollMax", (char *)NULL, offsetof(Axis, scrollMax),
     TK_CONFIG_NULL_OK | ALL_GRAPHS, &limitOption},
    {TK_CONFIG_CUSTOM, "-scrollmin", "scrollMin", "ScrollMin", (char *)NULL, offsetof(Axis, scrollMin),
     TK_CONFIG_NULL_OK | ALL_GRAPHS, &limitOption},
    {TK_CONFIG_DOUBLE, "-shiftby", "shiftBy", "ShiftBy", DEF_AXIS_SHIFTBY, offsetof(Axis, shiftBy),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_BOOLEAN, "-showticks", "showTicks", "ShowTicks", DEF_AXIS_SHOWTICKS, offsetof(Axis, showTicks),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_DOUBLE, "-stepsize", "stepSize", "StepSize", DEF_AXIS_STEP, offsetof(Axis, reqStep),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_DOUBLE, "-tickdivider", "tickDivider", "TickDivider", DEF_AXIS_STEP, offsetof(Axis, tickZoom),
     ALL_GRAPHS | TK_CONFIG_DONT_SET_DEFAULT},
    {TK_CONFIG_INT, "-subdivisions", "subdivisions", "Subdivisions", DEF_AXIS_SUBDIVISIONS,
     offsetof(Axis, reqNumMinorTicks), ALL_GRAPHS},
    {TK_CONFIG_FONT, "-tickfont", "tickFont", "Font", DEF_AXIS_TICK_FONT, offsetof(Axis, tickTextStyle.font),
     ALL_GRAPHS},
    {TK_CONFIG_PIXELS, "-ticklength", "tickLength", "TickLength", DEF_AXIS_TICK_LENGTH, offsetof(Axis, tickLength),
     ALL_GRAPHS},
    {TK_CONFIG_CUSTOM, "-tickshadow", "tickShadow", "Shadow", (char *)NULL, offsetof(Axis, tickTextStyle.shadow),
     TK_CONFIG_COLOR_ONLY | ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-tickshadow", "tickShadow", "Shadow", (char *)NULL, offsetof(Axis, tickTextStyle.shadow),
     TK_CONFIG_MONO_ONLY | ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_STRING, "-title", "title", "Title", (char *)NULL, offsetof(Axis, title),
     TK_CONFIG_DONT_SET_DEFAULT | TK_CONFIG_NULL_OK | ALL_GRAPHS},
    {TK_CONFIG_BOOLEAN, "-titlealternate", "titleAlternate", "TitleAlternate", DEF_AXIS_TITLE_ALTERNATE,
     offsetof(Axis, titleAlternate), TK_CONFIG_DONT_SET_DEFAULT | ALL_GRAPHS},
    {TK_CONFIG_COLOR, "-titlecolor", "titleColor", "Color", DEF_AXIS_FOREGROUND, offsetof(Axis, titleTextStyle.color),
     TK_CONFIG_COLOR_ONLY | ALL_GRAPHS},
    {TK_CONFIG_COLOR, "-titlecolor", "titleColor", "TitleColor", DEF_AXIS_FG_MONO, offsetof(Axis, titleTextStyle.color),
     TK_CONFIG_MONO_ONLY | ALL_GRAPHS},
    {TK_CONFIG_FONT, "-titlefont", "titleFont", "Font", DEF_AXIS_TITLE_FONT, offsetof(Axis, titleTextStyle.font),
     ALL_GRAPHS},
    {TK_CONFIG_CUSTOM, "-titleshadow", "titleShadow", "Shadow", (char *)NULL, offsetof(Axis, titleTextStyle.shadow),
     TK_CONFIG_COLOR_ONLY | ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_CUSTOM, "-titleshadow", "titleShadow", "Shadow", (char *)NULL, offsetof(Axis, titleTextStyle.shadow),
     TK_CONFIG_MONO_ONLY | ALL_GRAPHS, &rbcShadowOption},
    {TK_CONFIG_END, NULL, NULL, NULL, NULL, 0, 0}};

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

typedef int(RbcGrAxisOp)(Graph *, Axis *, int, int, Tcl_Obj *const[]);
static RbcGrAxisOp BindOp;
static RbcGrAxisOp CgetOp;
static RbcGrAxisOp ConfigureOp;
static RbcGrAxisOp InvTransformOp;
static RbcGrAxisOp LimitsOp;
static RbcGrAxisOp TransformOp;
static RbcGrAxisOp UseOp;

typedef int(RbcGrAxisVirtualOp)(Graph *, int, Tcl_Obj *const[]);
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
{ return (int)(x + ((x < 0.0) ? -0.5 : 0.5)); }

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
 * StringToFormat --
 *
 *      Convert the name of virtual axis to an pointer.
 *
 * Parameters:
 *      ClientData clientData - Not used.
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
static int StringToFormat(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                          Tcl_Size offset) {
    Axis *axisPtr = (Axis *)(widgRec);
    char **argv;
    Tcl_Size argc;

    if (axisPtr->limitsFormats != NULL) {
        ckfree((char *)axisPtr->limitsFormats);
    }
    axisPtr->limitsFormats = NULL;
    axisPtr->nFormats = 0;

    if ((string == NULL) || (*string == '\0')) {
        return TCL_OK;
    }
    if (Tcl_SplitList(interp, string, &argc, (const char ***)&argv) != TCL_OK) {
        return TCL_ERROR;
    }
    if (argc > 2) {
        Tcl_AppendResult(interp, "too many elements in limits format list \"", string, "\"", (char *)NULL);
        ckfree((char *)argv);
        return TCL_ERROR;
    }
    axisPtr->limitsFormats = argv;
    axisPtr->nFormats = argc;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * FormatToString --
 *
 *      Convert the window coordinates into a string.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget record
 *      Tcl_Size offset - offset of limits field
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The string representing the coordinate position is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *FormatToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                  Tcl_FreeProc **freeProcPtr) {
    Axis *axisPtr = (Axis *)(widgRec);

    if (axisPtr->nFormats == 0) {
        return "";
    }
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return Tcl_Merge(axisPtr->nFormats, (const char *const *)axisPtr->limitsFormats);
}

/*
 * ----------------------------------------------------------------------
 *
 * StringToLimit --
 *
 *      Convert the string representation of an axis limit into its numeric
 *      form.
 *
 * Parameters:
 *      ClientData clientData - Either AXIS_CONFIG_MIN or AXIS_CONFIG_MAX. Indicates which axis limit to set.
 *      Tcl_Interp *interp - Interpreter to send results back to.
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing new value.
 *      char *widgRec - Pointer to structure record.
 *      Tcl_Size offset - Offset of field in structure.
 *
 * Results:
 *      The return value is a standard Tcl result.  The symbol type is
 *      written into the widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int StringToLimit(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                         Tcl_Size offset) {
    double *limitPtr = (double *)(widgRec + offset);

    if ((string == NULL) || (*string == '\0')) {
        *limitPtr = VALUE_UNDEFINED;
    } else if (Tcl_ExprDouble(interp, string, limitPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * LimitToString --
 *
 *      Convert the floating point axis limits into a string.
 *
 * Parameters:
 *      ClientData clientData - Either LMIN or LMAX
 *      Tk_Window tkwin - Not used.
 *      char *widgRec
 *      Tcl_Size offset
 *      Tcl_FreeProc **freeProcPtr
 *
 * Results:
 *      The string representation of the limits is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static const char *LimitToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                 Tcl_FreeProc **freeProcPtr) {
    double limit = *(double *)(widgRec + offset);
    char *result;

    result = "";
    if (DEFINED(limit)) {
        char string[TCL_DOUBLE_SPACE + 1];
        Graph *graphPtr;

        graphPtr = Rbc_GetGraphFromWindowData(tkwin);
        Tcl_PrintDouble(graphPtr->interp, limit, string);
        result = RbcStrdup(string);
        if (result == NULL) {
            return "";
        }
        *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    }
    return result;
}

/*
 * ----------------------------------------------------------------------
 *
 * StringToTicks --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to.
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing new value.
 *      char *widgRec - Pointer to structure record.
 *      Tcl_Size offset - Offset of field in structure.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static int StringToTicks(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                         Tcl_Size offset) {
    unsigned int mask = PTR2UINT(clientData);
    Axis *axisPtr = (Axis *)widgRec;
    Ticks **ticksPtrPtr = (Ticks **)(widgRec + offset);
    int nTicks;
    Ticks *ticksPtr;

    nTicks = 0;
    ticksPtr = NULL;
    if ((string != NULL) && (*string != '\0')) {
        Tcl_Size nExprs;
        const char **exprArr;

        if (Tcl_SplitList(interp, string, &nExprs, &exprArr) != TCL_OK) {
            return TCL_ERROR;
        }
        if (nExprs > 0) {
            register int i;
            int result = TCL_ERROR;
            double value;

            ticksPtr = (Ticks *)ckalloc(sizeof(Ticks) + (nExprs * sizeof(double)));
            assert(ticksPtr);
            for (i = 0; i < nExprs; i++) {
                result = Tcl_ExprDouble(interp, exprArr[i], &value);
                if (result != TCL_OK) {
                    break;
                }
                ticksPtr->values[i] = value;
            }
            ckfree((char *)exprArr);
            if (result != TCL_OK) {
                ckfree((char *)ticksPtr);
                return TCL_ERROR;
            }
            nTicks = nExprs;
        }
    }
    axisPtr->flags &= ~mask;
    if (ticksPtr != NULL) {
        axisPtr->flags |= mask;
        ticksPtr->nTicks = nTicks;
    }
    if (*ticksPtrPtr != NULL) {
        ckfree((char *)*ticksPtrPtr);
    }
    *ticksPtrPtr = ticksPtr;
    return TCL_OK;
}

/*
 * ----------------------------------------------------------------------
 *
 * TicksToString --
 *
 *      Convert array of tick coordinates to a list.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec
 *      Tcl_Size offset
 *      Tcl_FreeProc **freeProcPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * ----------------------------------------------------------------------
 */
static const char *TicksToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                 Tcl_FreeProc **freeProcPtr) {
    Ticks *ticksPtr = *(Ticks **)(widgRec + offset);
    char string[TCL_DOUBLE_SPACE + 1];
    register int i;
    char *result;
    Tcl_DString dString;
    Graph *graphPtr;

    if (ticksPtr == NULL) {
        return "";
    }
    Tcl_DStringInit(&dString);
    graphPtr = Rbc_GetGraphFromWindowData(tkwin);
    for (i = 0; i < ticksPtr->nTicks; i++) {
        Tcl_PrintDouble(graphPtr->interp, ticksPtr->values[i], string);
        Tcl_DStringAppendElement(&dString, string);
    }
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    result = RbcStrdup(Tcl_DStringValue(&dString));
    Tcl_DStringFree(&dString);
    return result;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToLoose --
 *
 *      Convert a string to one of three values.
 *          0 - false, no, off
 *          1 - true, yes, on
 *          2 - always
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to.
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing new value.
 *      char *widgRec - Pointer to structure record.
 *      Tcl_Size offset - Offset of field in structure.
 *
 * Results:
 *      If the string is successfully converted, TCL_OK is returned.
 *      Otherwise, TCL_ERROR is returned and an error message is left in
 *      interpreter's result field.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToLoose(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                         Tcl_Size offset) {
    Axis *axisPtr = (Axis *)(widgRec);
    register int i;
    Tcl_Size argc;
    const char **argv;
    int values[2];

    if (Tcl_SplitList(interp, string, &argc, &argv) != TCL_OK) {
        return TCL_ERROR;
    }
    if ((argc < 1) || (argc > 2)) {
        Tcl_AppendResult(interp, "wrong # elements in loose value \"", string, "\"", (char *)NULL);
        return TCL_ERROR;
    }
    for (i = 0; i < argc; i++) {
        if ((argv[i][0] == 'a') && (strcmp(argv[i], "always") == 0)) {
            values[i] = TICK_RANGE_ALWAYS_LOOSE;
        } else {
            int bool;

            if (Tcl_GetBoolean(interp, argv[i], &bool) != TCL_OK) {
                ckfree((char *)argv);
                return TCL_ERROR;
            }
            values[i] = bool;
        }
    }
    axisPtr->looseMin = axisPtr->looseMax = values[0];
    if (argc > 1) {
        axisPtr->looseMax = values[1];
    }
    ckfree((char *)argv);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * LooseToString --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget record
 *      Tcl_Size offset - offset of flags field in record
 *      Tcl_FreeProc **freeProcPtr - Memory deallocation scheme to use
 *
 * Results:
 *      The string representation of the auto boolean is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *LooseToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                 Tcl_FreeProc **freeProcPtr) {
    Axis *axisPtr = (Axis *)widgRec;
    Tcl_DString dString;
    char *result;

    Tcl_DStringInit(&dString);
    if (axisPtr->looseMin == TICK_RANGE_TIGHT) {
        Tcl_DStringAppendElement(&dString, "0");
    } else if (axisPtr->looseMin == TICK_RANGE_LOOSE) {
        Tcl_DStringAppendElement(&dString, "1");
    } else if (axisPtr->looseMin == TICK_RANGE_ALWAYS_LOOSE) {
        Tcl_DStringAppendElement(&dString, "always");
    }
    if (axisPtr->looseMin != axisPtr->looseMax) {
        if (axisPtr->looseMax == TICK_RANGE_TIGHT) {
            Tcl_DStringAppendElement(&dString, "0");
        } else if (axisPtr->looseMax == TICK_RANGE_LOOSE) {
            Tcl_DStringAppendElement(&dString, "1");
        } else if (axisPtr->looseMax == TICK_RANGE_ALWAYS_LOOSE) {
            Tcl_DStringAppendElement(&dString, "always");
        }
    }
    result = RbcStrdup(Tcl_DStringValue(&dString));
    Tcl_DStringFree(&dString);
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
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
    int flags;

    flags = Rbc_GraphType(graphPtr);
    Tk_FreeOptions(configSpecs, (char *)axisPtr, graphPtr->display, flags);
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
    Rbc_FreeTextStyle(graphPtr->display, &axisPtr->titleTextStyle);
    Rbc_FreeTextStyle(graphPtr->display, &axisPtr->limitsTextStyle);
    Rbc_FreeTextStyle(graphPtr->display, &axisPtr->tickTextStyle);

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
    char errMsg[200];

    /* Check the requested axis limits. Can't allow -min to be greater
     * than -max, or have undefined log scale limits.  */
    if (((DEFINED(axisPtr->reqMin)) && (DEFINED(axisPtr->reqMax))) && (axisPtr->reqMin >= axisPtr->reqMax)) {
        sprintf(errMsg, "impossible limits (min %g >= max %g) for axis \"%s\"", axisPtr->reqMin, axisPtr->reqMax,
                axisPtr->name);
        Tcl_AppendResult(graphPtr->interp, errMsg, (char *)NULL);
        /* Bad values, turn on axis auto-scaling */
        axisPtr->reqMin = axisPtr->reqMax = VALUE_UNDEFINED;
        return TCL_ERROR;
    }
    if ((axisPtr->logScale) && (DEFINED(axisPtr->reqMin)) && (axisPtr->reqMin <= 0.0)) {
        sprintf(errMsg, "bad logscale limits (min=%g,max=%g) for axis \"%s\"", axisPtr->reqMin, axisPtr->reqMax,
                axisPtr->name);
        Tcl_AppendResult(graphPtr->interp, errMsg, (char *)NULL);
        /* Bad minimum value, turn on auto-scaling */
        axisPtr->reqMin = VALUE_UNDEFINED;
        return TCL_ERROR;
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
    register int i;
    Axis *axisPtr;
    Rbc_Chain *chainPtr;
    static char *axisNames[4] = {"x", "y", "x2", "y2"};
    int flags;

    flags = Rbc_GraphType(graphPtr);
    for (i = 0; i < 4; i++) {
        chainPtr = Rbc_ChainCreate();
        graphPtr->axisChain[i] = chainPtr;

        /* Create a default axis for each chain. */
        axisPtr = CreateAxis(graphPtr, axisNames[i], i);
        if (axisPtr == NULL) {
            return TCL_ERROR;
        }
        axisPtr->refCount = 1; /* Default axes are assumed in use. */
        axisPtr->classUid = (i & 1) ? rbcYAxisUid : rbcXAxisUid;
        axisPtr->flags |= AXIS_ONSCREEN;

        /*
         * Rbc_ConfigureWidgetComponent creates a temporary child window
         * by the name of the axis.  It's used so that the Tk routines
         * that access the X resource database can describe a single
         * component and not the entire graph.
         */
        if (Rbc_ConfigureWidgetComponent(graphPtr->interp, graphPtr->tkwin, axisPtr->name, "Axis", configSpecs, 0,
                                         (Tcl_Obj *const *)NULL, (char *)axisPtr, flags) != TCL_OK) {
            return TCL_ERROR;
        }
        if (ConfigureAxis(graphPtr, axisPtr) != TCL_OK) {
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
    return Tk_ConfigureValue(graphPtr->interp, graphPtr->tkwin, configSpecs, (char *)axisPtr, Tcl_GetString(objv[0]),
                             Rbc_GraphType(graphPtr));
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
    int flags;

    flags = TK_CONFIG_ARGV_ONLY | Rbc_GraphType(graphPtr);
    if (objc == 0) {
        return Tk_ConfigureInfo(graphPtr->interp, graphPtr->tkwin, configSpecs, (char *)axisPtr, (char *)NULL, flags);
    } else if (objc == 1) {
        return Tk_ConfigureInfo(graphPtr->interp, graphPtr->tkwin, configSpecs, (char *)axisPtr, Tcl_GetString(objv[0]),
                                flags);
    }
    if (Tk_ConfigureWidget(graphPtr->interp, graphPtr->tkwin, configSpecs, objc, objv, axisPtr, flags) != TCL_OK) {
        return TCL_ERROR;
    }
    if (ConfigureAxis(graphPtr, axisPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (axisPtr->flags & AXIS_ONSCREEN) {
        if (!Rbc_ConfigModified(graphPtr->interp, configSpecs, "-*color", "-background", "-bg", (char *)NULL)) {
            graphPtr->flags |= REDRAW_BACKING_STORE;
        }
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
    int flags;

    axisPtr = CreateAxis(graphPtr, Tcl_GetString(objv[3]), MARGIN_NONE);
    if (axisPtr == NULL) {
        return TCL_ERROR;
    }
    flags = Rbc_GraphType(graphPtr);
    if (Rbc_ConfigureWidgetComponent(graphPtr->interp, graphPtr->tkwin, axisPtr->name, "Axis", configSpecs, objc - 4,
                                     objv + 4, (char *)axisPtr, flags) != TCL_OK) {
        goto error;
    }
    if (ConfigureAxis(graphPtr, axisPtr) != TCL_OK) {
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
    Rbc_Op proc;
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

    proc = Rbc_GetOpFromObj(interp, axisOps, RBC_OP_ARG2, objc, objv);
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
    Rbc_Op proc;
    Axis *axisPtr;
    static Rbc_OpSpec axisOps[] = {{"bind", (Rbc_Op)BindOp, 2, 5, "sequence command"},
                                   {"cget", (Rbc_Op)CgetOp, 4, 4, "option"},
                                   {"configure", (Rbc_Op)ConfigureOp, 3, 0, "?option value?..."},
                                   {"invtransform", (Rbc_Op)InvTransformOp, 4, 4, "value"},
                                   {"limits", (Rbc_Op)LimitsOp, 3, 3, ""},
                                   {"transform", (Rbc_Op)TransformOp, 4, 4, "value"},
                                   {"use", (Rbc_Op)UseOp, 3, 4, "?axisName?"},
                                   RBC_OPSPEC_END};

    proc = Rbc_GetOpFromObj(graphPtr->interp, axisOps, RBC_OP_ARG2, objc, objv);
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
