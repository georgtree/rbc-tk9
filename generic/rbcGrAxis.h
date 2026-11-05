/**
 * @file rbcGrAxis.h
 * @brief Private axis options, tick storage and mapping records.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#ifndef _RBCGRAXIS
#define _RBCGRAXIS

#include "rbcInt.h"
#include "rbcList.h"

#include "rbcChain.h"
#include <tcl.h>

typedef struct GraphStruct Graph;

/** @brief Minimum, maximum and cached normalization of an axis interval. */
typedef struct {
    double min; /**< Lower endpoint in the range coordinate system. */
    double max; /**< Upper endpoint in the range coordinate system. */
    double range; /**< Difference between max and min. */
    double scale; /**< Reciprocal of range, used to normalize coordinates. */
} AxisRange;

/** @brief Variable-size tick label allocation with mapped placement. */
typedef struct {
    Point2D anchorPos; /**< Mapped label anchor in widget pixels. */
    int width; /**< Measured label width in pixels. */
    int height; /**< Measured label height in pixels. */
    char string[1]; /**< Inline NUL-terminated label text; allocation extends beyond this one-byte placeholder. */
} TickLabel;

/** @brief Variable-size allocation containing explicit or generated tick positions. */
typedef struct {
    Tcl_Size nTicks; /**< # of ticks on axis */
    double values[1]; /**< Inline tick values; allocation extends beyond this one-entry placeholder. */
} Ticks;

/** @brief Arithmetic progression used to generate ticks. */
typedef struct {
    double initial; /**< Initial value */
    double step; /**< Size of interval */
    Tcl_Size nSteps; /**< Number of intervals. */
} TickSweep;

/**
 * @brief Graph-owned axis options, retained references and mapped presentation. Deletion may be deferred
 * while components retain references; Tk resources and derived tick storage are released by axis
 * cleanup.
 */
typedef struct {
    char *name; /**< Axis registry name, owned by the axis. */
    Rbc_Uid classUid; /**< Type of axis. */
    Graph *graphPtr; /**< Borrowed owning graph. */
    Tk_OptionTable optionTable; /**< Tk option table for this component configuration lifecycle. */
    int optionMask; /**< Change mask from the current option transaction. */
    Tcl_Size optionObjc; /**< Number of objects in the current borrowed configure argument slice. */
    Tcl_Obj *const *optionObjv; /**< Borrowed configure arguments, valid only during the synchronous configuration transaction. */
    int optionsConfigured; /**< Nonzero after the first successful configuration. */
    int optionsInitialized; /**< Nonzero after Tk option initialization. */
    int tkResourcesReleased; /**< Nonzero after guarded Tk-resource release. */
    Tcl_Obj *bindTagsObjPtr; /**< Tk option-managed Tcl representation for bind tags; released with the owning option state. */
    Tcl_Obj *formatCmdObjPtr; /**< Tk option-managed Tcl representation for format cmd; released with the owning option state. */
    Tcl_Obj *scrollCmdObjPtr; /**< Tk option-managed Tcl representation for scroll cmd; released with the owning option state. */
    int scrollNotifyPending; /**< Nonzero when scrollbar notification awaits delivery. */
    double scrollNotifyFirst; /**< First visible fraction queued for the scrollbar callback. */
    double scrollNotifyLast; /**< Last visible fraction queued for the scrollbar callback. */
    Tcl_Obj *limitsFormatObjPtr; /**< Tk option-managed Tcl representation for limits format; released with the owning option state. */
    Tcl_Obj *limitsShadowObjPtr; /**< Tk option-managed Tcl representation for limits shadow; released with the owning option state. */
    Tcl_Obj *looseObjPtr; /**< Tk option-managed Tcl representation for loose; released with the owning option state. */
    Tcl_Obj *majorTicksObjPtr; /**< Tk option-managed Tcl representation for major ticks; released with the owning option state. */
    Tcl_Obj *minorTicksObjPtr; /**< Tk option-managed Tcl representation for minor ticks; released with the owning option state. */
    Tcl_Obj *minObjPtr; /**< Tk option-managed Tcl representation for min; released with the owning option state. */
    Tcl_Obj *maxObjPtr; /**< Tk option-managed Tcl representation for max; released with the owning option state. */
    Tcl_Obj *scrollMinObjPtr; /**< Tk option-managed Tcl representation for scroll min; released with the owning option state. */
    Tcl_Obj *scrollMaxObjPtr; /**< Tk option-managed Tcl representation for scroll max; released with the owning option state. */
    Tcl_Obj *tickShadowObjPtr; /**< Tk option-managed Tcl representation for tick shadow; released with the owning option state. */
    Tcl_Obj *titleShadowObjPtr; /**< Tk option-managed Tcl representation for title shadow; released with the owning option state. */
    Tcl_Obj *borderWidthObjPtr; /**< Tk option-managed Tcl representation for border width; released with the owning option state. */
    Tcl_Obj *lineWidthObjPtr; /**< Tk option-managed Tcl representation for line width; released with the owning option state. */
    Tcl_Obj *scrollIncrementObjPtr; /**< Tk option-managed Tcl representation for scroll increment; released with the owning option
     * state.
     */
    unsigned int flags; /**< Set bit field definitions below */
    /*
     * AXIS_DRAWN        Axis is designated as a logical axis
     * AXIS_DIRTY
     *
     * AXIS_CONFIG_MAJOR    User specified major ticks.
     * AXIS_CONFIG_MINOR    User specified minor ticks.
     */
    char **tags; /**< Owned NULL-terminated binding-tag array. */
    char *detail; /**< Borrowed pick-detail string identifying the selected axis part. */
    int deletePending; /**< Indicates that the axis was scheduled for deletion. The actual deletion may be deferred until
     * the axis is no longer in use.
     */
    int refCount; /**< Number of retained component references delaying deletion. */
    Tcl_HashEntry *hashPtr; /**< Points to axis entry in hash table. Used to quickly remove axis entries. */
    int logScale; /**< If non-zero, scale the axis values logarithmically. */
    int hidden; /**< If non-zero, don't display the axis title, ticks, or line. */
    int showTicks; /**< If non-zero, display tick marks and labels. */
    int descending; /**< If non-zero, display the range of values on the axis in descending order, from high to low. */
    int looseMin; /**< If non-zero, axis range extends to the outer major ticks, otherwise at the limits of the data
     * values. This is overridden by setting the -min and -max options.
     */
    int looseMax; /**< If non-zero, axis range extends to the outer major ticks, otherwise at the limits of the data
     * values. This is overridden by setting the -min and -max options.
     */
    char *title; /**< Title of the axis. */
    TextStyle titleTextStyle; /**< Text attributes (color, font, rotation, etc.) of the axis title. */
    int titleAlternate; /**< Indicates whether to position the title above/left of the axis. */
    Point2D titlePos; /**< Position of the title */
    int titleWidth; /**< Measured title width in pixels. */
    int titleHeight; /**< Measured title height in pixels. */
    int lineWidth; /**< Width of lines representing axis (including ticks). If zero, then no axis lines or ticks are
     * drawn.
     */
    char **limitsFormats; /**< One or two strings of sprintf-like formats describing how to display virtual axis limits. If
     * NULL, display no limits.
     */
    int nFormats; /**< Number of strings in limitsFormats. */
    TextStyle limitsTextStyle; /**< Text attributes (color, font, rotation, etc.) of the limits. */
    double windowSize; /**< Size of a sliding window of values used to scale the axis automatically as new data values are
     * added. The axis will always display the latest values in this range.
     */
    double shiftBy; /**< Shift maximum by this interval. */
    int tickLength; /**< Length of major ticks in pixels */
    TextStyle tickTextStyle; /**< Text attributes (color, font, rotation, etc.) for labels at each major tick. */
    int scrollUnits; /**< Pixel increment for unit scrolling. */
    double min; /**< The actual axis range. */
    double max; /**< The actual axis range. */
    double reqMin; /**< Requested axis bounds. Consult the axisPtr->flags field for AXIS_CONFIG_MIN and AXIS_CONFIG_MAX
     * to see if the requested bound have been set. They override the computed range of the axis
     * (determined by auto-scaling).
     */
    double reqMax; /**< Requested axis bounds. Consult the axisPtr->flags field for AXIS_CONFIG_MIN and AXIS_CONFIG_MAX
     * to see if the requested bound have been set. They override the computed range of the axis
     * (determined by auto-scaling).
     */
    double scrollMin; /**< Defines the scrolling region of the axis. Normally the region is determined from the data
     * limits. If specified, these values override the data-range.
     */
    double scrollMax; /**< Defines the scrolling region of the axis. Normally the region is determined from the data
     * limits. If specified, these values override the data-range.
     */
    AxisRange valueRange; /**< Range of data values of elements mapped to this axis. This is used to auto-scale the axis in
     * "tight" mode.
     */
    AxisRange axisRange; /**< Smallest and largest major tick values for the axis. The tick values lie outside the range of
     * data values. This is used to auto-scale the axis in "loose" mode.
     */
    double prevMin; /**< Previous mapped lower limit used to detect range changes. */
    double prevMax; /**< Previous mapped upper limit used to detect range changes. */
    double notifiedMin; /**< Last displayed limits, in data units. */
    double notifiedMax; /**< Last displayed limits, in data units. */
    int notifiedLimitsValid; /**< Nonzero when the last displayed-limit snapshot is valid. */
    int notifyConfigure; /**< A successful configuration awaits display. */
    double reqStep; /**< If > 0.0, overrides the computed major tick interval. Otherwise a stepsize is automatically
     * calculated, based upon the range of elements mapped to the axis. The default value is 0.0.
     */
    GC tickGC; /**< Graphics context for axis and tick labels */
    Ticks *t1Ptr; /**< Array of major tick positions. May be set by the user or generated from the major sweep below. */
    Ticks *t2Ptr; /**< Array of minor tick positions. May be set by the user or generated from the minor sweep below. */
    TickSweep minorSweep; /**< Generated minor tick progression. */
    TickSweep majorSweep; /**< Generated major tick progression. */
    int reqNumMinorTicks; /**< If non-zero, represents the requested the number of minor ticks to be uniformly displayed along
     * each major tick.
     */
    int labelOffset; /**< If non-zero, indicates that the tick label should be offset to sit in the middle of the next
     * interval.
     */
    /* The following fields are specific to logical axes */
    Rbc_ChainLink *linkPtr; /**< Axis link in margin list. */
    Rbc_Chain *chainPtr; /**< Borrowed margin axis chain containing linkPtr. */
    struct RbcTrack *track; /**< Owning plot region, or NULL for shared axes. */
    int width; /**< Extents of axis */
    int height; /**< Extents of axis */
    Segment2D *segments; /**< Array of line segments representing the major and minor ticks, but also the axis line itself.
     * The segment coordinates are relative to the axis.
     */
    Tcl_Size nSegments; /**< Number of segments in the above array. */
    Rbc_Chain *tickLabels; /**< Contains major tick label strings and their offsets along the axis. */
    Region2D region; /**< Mapped axis hit-test rectangle in widget pixels. */
    Tk_3DBorder border; /**< Tk option-managed axis background border. */
    int borderWidth; /**< Axis background border thickness in pixels. */
    int relief; /**< Tk relief of the axis background. */
} Axis;

#define AXIS_CONFIG_MAJOR (1 << 4) /* User specified major tick intervals. */
#define AXIS_CONFIG_MINOR (1 << 5) /* User specified minor tick intervals. */
#define AXIS_ONSCREEN                                                                                                  \
    (1 << 6) /* Axis is displayed on the screen via                                                                    \
              * the "use" operation */
#define AXIS_DIRTY (1 << 7)
#define AXIS_ALLOW_NULL (1 << 12)

/** @brief Axis pair borrowed by a mapping or held through its owner references. */
typedef struct {
    Axis *x; /**< X-axis identity; reference management belongs to the containing component. */
    Axis *y; /**< Y-axis identity; reference management belongs to the containing component. */
} Axis2D;

int Rbc_GetAxisFromObj(Graph *graphPtr, Tcl_Obj *objPtr, Rbc_Uid classUid, int allowEmpty, Axis **axisPtrPtr);
void Rbc_FreeAxisReference(Graph *graphPtr, Axis *axisPtr);
Ticks *Rbc_AllocAxisMajorTicks(Axis *axisPtr);
TickLabel *Rbc_AllocAxisTickLabel(Graph *graphPtr, Axis *axisPtr, double value);

#endif /* _RBCGRAXIS */
