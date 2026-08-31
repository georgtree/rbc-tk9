/*
 * rbcGrPolar.c --
 *
 *      Polar-specific graph mapping.
 *
 *      Polar graph data remain Cartesian:
 *
 *          x = real
 *          y = imaginary
 *
 *      This module changes only the representation of the grid.
 */

#include "rbcGraph.h"

#define POLAR_PI 3.14159265358979323846264338327950288
#define POLAR_DEG_TO_RAD (POLAR_PI / 180.0)

#define POLAR_CIRCLE_SEGMENTS 180

#define POLAR_ANGLE_LABEL_RADIUS 0.90
#define POLAR_RADIAL_LABEL_OFFSET 2

#define SMITH_CIRCLE_SEGMENTS 180
#define SMITH_ARC_SEGMENTS 90

#define SMITH_RESISTANCE_LABEL_OFFSET 2
#define SMITH_REACTANCE_LABEL_RADIUS 0.94

typedef struct {
    double min;
    double max;
} PolarRadiusRange;

static int GetPolarRadiusRange(Grid *gridPtr, PolarRadiusRange *rangePtr) {
    Axis *xAxisPtr;
    Axis *yAxisPtr;
    double dx;
    double dy;
    double r1, r2, r3, r4;

    xAxisPtr = gridPtr->axes.x;
    yAxisPtr = gridPtr->axes.y;
    if ((xAxisPtr == NULL) || (yAxisPtr == NULL)) {
        return FALSE;
    }
    /*
     * Polar geometry is Cartesian and therefore requires linear
     * mapped axes.
     */
    if (xAxisPtr->logScale || yAxisPtr->logScale) {
        return FALSE;
    }
    if ((!FINITE(xAxisPtr->min)) || (!FINITE(xAxisPtr->max)) || (!FINITE(yAxisPtr->min)) || (!FINITE(yAxisPtr->max)) ||
        (xAxisPtr->min >= xAxisPtr->max) || (yAxisPtr->min >= yAxisPtr->max)) {
        return FALSE;
    }
    /*
     * Minimum distance from the origin to the viewport rectangle.
     *
     * If zero lies inside a coordinate interval, the nearest
     * distance in that dimension is zero.
     */
    if (xAxisPtr->min > 0.0) {
        dx = xAxisPtr->min;
    } else if (xAxisPtr->max < 0.0) {
        dx = -xAxisPtr->max;
    } else {
        dx = 0.0;
    }
    if (yAxisPtr->min > 0.0) {
        dy = yAxisPtr->min;
    } else if (yAxisPtr->max < 0.0) {
        dy = -yAxisPtr->max;
    } else {
        dy = 0.0;
    }
    rangePtr->min = hypot(dx, dy);
    /*
     * The maximum visible radius is the distance to the farthest
     * viewport corner.
     */
    r1 = hypot(xAxisPtr->min, yAxisPtr->min);
    r2 = hypot(xAxisPtr->min, yAxisPtr->max);
    r3 = hypot(xAxisPtr->max, yAxisPtr->min);
    r4 = hypot(xAxisPtr->max, yAxisPtr->max);
    rangePtr->max = r1;
    if (r2 > rangePtr->max) {
        rangePtr->max = r2;
    }
    if (r3 > rangePtr->max) {
        rangePtr->max = r3;
    }
    if (r4 > rangePtr->max) {
        rangePtr->max = r4;
    }
    return FINITE(rangePtr->min) && FINITE(rangePtr->max) && (rangePtr->max > 0.0);
}

static double PolarCompleteCircleRadius(Grid *gridPtr) {
    Axis *xAxisPtr;
    Axis *yAxisPtr;
    double radius;

    xAxisPtr = gridPtr->axes.x;
    yAxisPtr = gridPtr->axes.y;
    if ((xAxisPtr == NULL) || (yAxisPtr == NULL)) {
        return 0.0;
    }
    if (xAxisPtr->logScale || yAxisPtr->logScale) {
        return 0.0;
    }
    /*
     * A complete circle centered at the origin can exist only when
     * the origin lies strictly inside both mapped axis ranges.
     */
    if ((xAxisPtr->min >= 0.0) || (xAxisPtr->max <= 0.0) || (yAxisPtr->min >= 0.0) || (yAxisPtr->max <= 0.0)) {
        return 0.0;
    }
    radius = xAxisPtr->max;
    if (-xAxisPtr->min < radius) {
        radius = -xAxisPtr->min;
    }
    if (yAxisPtr->max < radius) {
        radius = yAxisPtr->max;
    }
    if (-yAxisPtr->min < radius) {
        radius = -yAxisPtr->min;
    }
    return radius;
}

static int ClipPolarRay(Grid *gridPtr, double degrees, Point2D *pPtr, Point2D *qPtr) {
    Axis *xAxisPtr;
    Axis *yAxisPtr;
    double dx;
    double dy;
    double theta;
    double tEnter;
    double tExit;
    double t1;
    double t2;
    double tmp;

    xAxisPtr = gridPtr->axes.x;
    yAxisPtr = gridPtr->axes.y;
    if ((xAxisPtr == NULL) || (yAxisPtr == NULL)) {
        return FALSE;
    }
    theta = degrees * POLAR_DEG_TO_RAD;
    dx = cos(theta);
    dy = sin(theta);
    tEnter = 0.0;
    tExit = DBL_MAX;
    /*
     * Clip against the X slab.
     */
    if (fabs(dx) <= DBL_EPSILON) {
        if ((0.0 < xAxisPtr->min) || (0.0 > xAxisPtr->max)) {
            return FALSE;
        }
    } else {
        t1 = xAxisPtr->min / dx;
        t2 = xAxisPtr->max / dx;
        if (t1 > t2) {
            tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        if (t1 > tEnter) {
            tEnter = t1;
        }
        if (t2 < tExit) {
            tExit = t2;
        }
    }
    /*
     * Clip against the Y slab.
     */
    if (fabs(dy) <= DBL_EPSILON) {
        if ((0.0 < yAxisPtr->min) || (0.0 > yAxisPtr->max)) {
            return FALSE;
        }
    } else {
        t1 = yAxisPtr->min / dy;
        t2 = yAxisPtr->max / dy;
        if (t1 > t2) {
            tmp = t1;
            t1 = t2;
            t2 = tmp;
        }
        if (t1 > tEnter) {
            tEnter = t1;
        }
        if (t2 < tExit) {
            tExit = t2;
        }
    }
    /*
     * No positive part of this ray intersects the viewport.
     */
    if ((tExit < tEnter) || (tExit < 0.0)) {
        return FALSE;
    }
    if (tEnter < 0.0) {
        tEnter = 0.0;
    }
    if (tEnter > tExit) {
        return FALSE;
    }
    pPtr->x = tEnter * dx;
    pPtr->y = tEnter * dy;
    qPtr->x = tExit * dx;
    qPtr->y = tExit * dy;
    return TRUE;
}

static void MapCircle(Graph *graphPtr, Grid *gridPtr, double radius, Segment2D *segments) {
    Point2D first;
    Point2D previous;
    Tcl_Size i;

    first = Rbc_Map2D(graphPtr, radius, 0.0, &gridPtr->axes);
    previous = first;
    for (i = 1; i <= POLAR_CIRCLE_SEGMENTS; i++) {
        double theta;
        Point2D next;

        theta = (2.0 * POLAR_PI * (double)i) / (double)POLAR_CIRCLE_SEGMENTS;
        next = Rbc_Map2D(graphPtr, radius * cos(theta), radius * sin(theta), &gridPtr->axes);
        segments[i - 1].p = previous;
        segments[i - 1].q = next;
        previous = next;
    }
}

static int MapPolarSpoke(Graph *graphPtr, Grid *gridPtr, double degrees, Segment2D *segmentPtr) {
    Point2D p;
    Point2D q;

    if (!ClipPolarRay(gridPtr, degrees, &p, &q)) {
        return FALSE;
    }
    segmentPtr->p = Rbc_Map2D(graphPtr, p.x, p.y, &gridPtr->axes);
    segmentPtr->q = Rbc_Map2D(graphPtr, q.x, q.y, &gridPtr->axes);
    return TRUE;
}

static double PolarGridSegmentValue(Graph *graphPtr, Grid *gridPtr, const Segment2D *segmentPtr) {
    Point2D origin;
    Point2D point;

    origin = Rbc_Map2D(graphPtr, 0.0, 0.0, &gridPtr->axes);
    if (graphPtr->inverted) {
        /*
         * Data X is mapped in the screen-Y direction.
         */
        point = Rbc_InvMap2D(graphPtr, origin.x, segmentPtr->p.y, &gridPtr->axes);
    } else {
        /*
         * Data X is mapped in the screen-X direction.
         */
        point = Rbc_InvMap2D(graphPtr, segmentPtr->p.x, origin.y, &gridPtr->axes);
    }
    return point.x;
}

static void MapPolarCircles(Graph *graphPtr, Grid *gridPtr, const PolarRadiusRange *rangePtr) {
    Segment2D *axisSegments;
    Segment2D *circleSegments;
    Tcl_Size nAxisSegments;
    Tcl_Size nCircles;
    Tcl_Size needed;
    Tcl_Size i;

    axisSegments = NULL;
    nAxisSegments = 0;
    Rbc_GetAxisSegments(graphPtr, gridPtr->axes.x, &axisSegments, &nAxisSegments);
    if ((axisSegments == NULL) || (nAxisSegments <= 0)) {
        return;
    }
    /*
     * First count usable positive radial ticks.
     *
     * Negative X ticks would produce duplicate circles because
     * radius is unsigned.
     */
    nCircles = 0;
    for (i = 0; i < nAxisSegments; i++) {
        double radius;

        radius = PolarGridSegmentValue(graphPtr, gridPtr, axisSegments + i);
        if ((!FINITE(radius)) || (radius <= 0.0) || (radius < rangePtr->min) || (radius > rangePtr->max)) {
            continue;
        }
        nCircles++;
    }
    if (nCircles == 0) {
        ckfree(axisSegments);
        return;
    }
    if (nCircles > TCL_SIZE_MAX / POLAR_CIRCLE_SEGMENTS) {
        ckfree(axisSegments);
        return;
    }
    needed = nCircles * POLAR_CIRCLE_SEGMENTS;
    if ((size_t)needed > SIZE_MAX / sizeof(Segment2D)) {
        ckfree(axisSegments);
        return;
    }
    circleSegments = Tcl_AttemptAlloc((size_t)needed * sizeof(Segment2D));
    if (circleSegments == NULL) {
        ckfree(axisSegments);
        return;
    }
    nCircles = 0;
    for (i = 0; i < nAxisSegments; i++) {
        double radius;

        radius = PolarGridSegmentValue(graphPtr, gridPtr, axisSegments + i);
        if ((!FINITE(radius)) || (radius <= 0.0) || (radius < rangePtr->min) || (radius > rangePtr->max)) {
            continue;
        }
        MapCircle(graphPtr, gridPtr, radius, circleSegments + (nCircles * POLAR_CIRCLE_SEGMENTS));
        nCircles++;
    }
    ckfree(axisSegments);
    gridPtr->x.segments = circleSegments;
    gridPtr->x.nSegments = nCircles * POLAR_CIRCLE_SEGMENTS;
}

static void MapPolarSpokes(Graph *graphPtr, Grid *gridPtr) {
    Segment2D *segments;
    Tcl_Size nMajor;
    Tcl_Size nMinor;
    Tcl_Size nSpokes;
    Tcl_Size index;
    Tcl_Size i;

    nMajor = graphPtr->nAngleMajorTicks;
    nMinor = gridPtr->minorGrid ? graphPtr->nAngleMinorTicks : 0;
    if (nMinor > TCL_SIZE_MAX - nMajor) {
        return;
    }
    nSpokes = nMajor + nMinor;
    if (nSpokes == 0) {
        return;
    }
    if ((Tcl_WideUInt)nSpokes > (Tcl_WideUInt)(SIZE_MAX / sizeof(Segment2D))) {
        return;
    }
    segments = Tcl_AttemptAlloc((size_t)nSpokes * sizeof(Segment2D));
    if (segments == NULL) {
        return;
    }
    index = 0;
    /*
     * Major spokes are always present.
     */
    for (i = 0; i < nMajor; i++) {
        if (MapPolarSpoke(graphPtr, gridPtr, graphPtr->angleMajorTicks[i], segments + index)) {
            index++;
        }
    }
    /*
     * Minor spokes are controlled by the existing grid -minor option.
     */
    for (i = 0; i < nMinor; i++) {
        if (MapPolarSpoke(graphPtr, gridPtr, graphPtr->angleMinorTicks[i], segments + index)) {
            index++;
        }
    }
    if (index == 0) {
        ckfree(segments);
        return;
    }
    gridPtr->y.segments = segments;
    gridPtr->y.nSegments = index;
}

void Rbc_MapPolarGrid(Graph *graphPtr, Grid *gridPtr) {
    PolarRadiusRange range;

    if (!GetPolarRadiusRange(gridPtr, &range)) {
        return;
    }
    MapPolarCircles(graphPtr, gridPtr, &range);
    MapPolarSpokes(graphPtr, gridPtr);
}

static Tk_Anchor GetPolarRadialLabelAnchor(Graph *graphPtr) {
    if (graphPtr->radialLabelAnchor.isAuto) {
        return TK_ANCHOR_SE;
    }
    return graphPtr->radialLabelAnchor.anchor;
}

static Tk_Anchor GetInwardLabelAnchor(double degrees) {
    if ((degrees < 22.5) || (degrees >= 337.5)) {
        return TK_ANCHOR_E;
    }
    if (degrees < 67.5) {
        return TK_ANCHOR_NE;
    }
    if (degrees < 112.5) {
        return TK_ANCHOR_N;
    }
    if (degrees < 157.5) {
        return TK_ANCHOR_NW;
    }
    if (degrees < 202.5) {
        return TK_ANCHOR_W;
    }
    if (degrees < 247.5) {
        return TK_ANCHOR_SW;
    }
    if (degrees < 292.5) {
        return TK_ANCHOR_S;
    }
    return TK_ANCHOR_SE;
}

static Tk_Anchor GetPolarAngleLabelAnchor(Graph *graphPtr, double degrees) {
    if (!graphPtr->angleLabelAnchor.isAuto) {
        return graphPtr->angleLabelAnchor.anchor;
    }
    return GetInwardLabelAnchor(degrees);
}

static Tk_Anchor GetPolarSpokeLabelAnchor(Graph *graphPtr, const Segment2D *segmentPtr) {
    double dx;
    double dy;
    double degrees;

    if (!graphPtr->angleLabelAnchor.isAuto) {
        return graphPtr->angleLabelAnchor.anchor;
    }
    /*
     * The label is placed at segmentPtr->q, the far end of the
     * visible ray.  Determine its direction in screen coordinates
     * and choose an anchor which makes the text extend back inward
     * along the spoke.
     *
     * Screen Y increases downward, therefore negate dy before
     * converting to the normal mathematical angle convention.
     */
    dx = segmentPtr->q.x - segmentPtr->p.x;
    dy = segmentPtr->q.y - segmentPtr->p.y;
    if ((fabs(dx) <= DBL_EPSILON) && (fabs(dy) <= DBL_EPSILON)) {
        return TK_ANCHOR_CENTER;
    }
    degrees = atan2(-dy, dx) / POLAR_DEG_TO_RAD;
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    return GetInwardLabelAnchor(degrees);
}

static int GetPolarAngularLabelPosition(Graph *graphPtr, Grid *gridPtr, double completeRadius, double degrees,
                                        Point2D *pointPtr, Tk_Anchor *anchorPtr) {
    if (completeRadius > 0.0) {
        double labelRadius;
        double theta;

        /*
         * A complete centered circle is visible.  Preserve the
         * traditional circular angular-label layout.
         */
        labelRadius = completeRadius * POLAR_ANGLE_LABEL_RADIUS;
        theta = degrees * POLAR_DEG_TO_RAD;
        *pointPtr = Rbc_Map2D(graphPtr, labelRadius * cos(theta), labelRadius * sin(theta), &gridPtr->axes);
        *anchorPtr = GetPolarAngleLabelAnchor(graphPtr, degrees);
        return FINITE(pointPtr->x) && FINITE(pointPtr->y);
    } else {
        Segment2D segment;

        /*
         * No complete centered circle is visible.  Put the label at
         * the far visible end of this angle's clipped spoke.
         *
         * If the positive ray does not cross the current viewport,
         * there is neither a visible spoke nor a useful angle label.
         */
        if (!MapPolarSpoke(graphPtr, gridPtr, degrees, &segment)) {
            return FALSE;
        }
        *pointPtr = segment.q;
        *anchorPtr = GetPolarSpokeLabelAnchor(graphPtr, &segment);
        return FINITE(pointPtr->x) && FINITE(pointPtr->y);
    }
}

static int GetPolarRadialLabelPosition(Graph *graphPtr, Grid *gridPtr, double radius, Point2D *pointPtr) {
    Axis *xAxisPtr;
    Axis *yAxisPtr;

    xAxisPtr = gridPtr->axes.x;
    yAxisPtr = gridPtr->axes.y;
    if ((xAxisPtr == NULL) || (yAxisPtr == NULL)) {
        return FALSE;
    }
    if (xAxisPtr->logScale || yAxisPtr->logScale) {
        return FALSE;
    }
    /*
     * Radial labels are attached to the positive zero-degree ray:
     *
     *     (x, y) = (radius, 0)
     *
     * The origin itself does not have to be visible.  The label is
     * useful whenever this particular point of the radial circle lies
     * inside the current Cartesian viewport.
     */
    if ((!FINITE(radius)) || (radius <= 0.0) || (radius < xAxisPtr->min) || (radius > xAxisPtr->max) ||
        (0.0 < yAxisPtr->min) || (0.0 > yAxisPtr->max)) {
        return FALSE;
    }
    *pointPtr = Rbc_Map2D(graphPtr, radius, 0.0, &gridPtr->axes);
    return FINITE(pointPtr->x) && FINITE(pointPtr->y);
}

static void DrawPolarRadialLabels(Graph *graphPtr, Drawable drawable, Grid *gridPtr) {
    Axis *axisPtr;
    TextStyle style;
    Ticks *ticksPtr;
    Tcl_Size i;

    axisPtr = gridPtr->axes.x;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    ticksPtr = Rbc_AllocAxisMajorTicks(axisPtr);
    if (ticksPtr == NULL) {
        return;
    }
    /*
     * Use the X-axis tick style, but don't modify the live style.
     */
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    style.anchor = GetPolarRadialLabelAnchor(graphPtr);
    for (i = 0; i < ticksPtr->nTicks; i++) {
        TickLabel *labelPtr;
        Point2D point;
        double radius;

        radius = ticksPtr->values[i];
        /*
         * Radius is unsigned.  Negative X ticks duplicate positive
         * circles, and zero is the origin rather than a ring.
         */
        if ((!FINITE(radius)) || (radius <= 0.0)) {
            continue;
        }
        if (!GetPolarRadialLabelPosition(graphPtr, gridPtr, radius, &point)) {
            continue;
        }
        labelPtr = Rbc_AllocAxisTickLabel(graphPtr, axisPtr, radius);
        if (labelPtr == NULL) {
            continue;
        }
        Rbc_DrawText(graphPtr->tkwin, drawable, labelPtr->string, &style, ROUND(point.x) - POLAR_RADIAL_LABEL_OFFSET,
                     ROUND(point.y) - POLAR_RADIAL_LABEL_OFFSET);
        ckfree(labelPtr);
    }
    ckfree(ticksPtr);
}

static char *FormatPolarAngleLabel(Graph *graphPtr, double degrees, Tcl_DString *dsPtr) {
    Tcl_Interp *interp;
    char valueString[64];
    int result;

    Tcl_DStringInit(dsPtr);
    /*
     * Keep the default representation consistent with the existing
     * Polar labels.
     *
     * The callback receives this numeric part without the degree
     * suffix so that it can easily interpret it as a number.
     */
    snprintf(valueString, sizeof(valueString), "%g", degrees);
    /*
     * Default label.
     */
    Tcl_DStringAppend(dsPtr, valueString, -1);
    Tcl_DStringAppend(dsPtr, "\xC2\xB0", 2);
    if (graphPtr->angleCommandObjPtr == NULL) {
        return Tcl_DStringValue(dsPtr);
    }
    interp = graphPtr->interp;
    {
        Tcl_Size prefixObjc;

        result = Tcl_ListObjLength(interp, graphPtr->angleCommandObjPtr, &prefixObjc);
        /*
         * ConfigureGraph() normally guarantees that this object has
         * a valid list representation.  Preserve drawing if that
         * invariant is somehow violated.
         */
        if (result != TCL_OK) {
            Tcl_BackgroundException(interp, result);
            Tcl_ResetResult(interp);
            return Tcl_DStringValue(dsPtr);
        }
        /*
         * An empty command prefix selects normal formatting.
         */
        if (prefixObjc == 0) {
            return Tcl_DStringValue(dsPtr);
        }
    }

    {
        Tcl_Obj *cmdObjPtr;
        Tcl_Obj **objv;
        Tcl_Size objc;

        /*
         * Never modify the Tk-managed option object itself.
         */
        cmdObjPtr = Tcl_DuplicateObj(graphPtr->angleCommandObjPtr);
        Tcl_IncrRefCount(cmdObjPtr);
        result = Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewStringObj(Tk_PathName(graphPtr->tkwin), -1));
        if (result == TCL_OK) {
            result = Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewStringObj(valueString, -1));
        }
        if (result == TCL_OK) {
            result = Tcl_ListObjGetElements(interp, cmdObjPtr, &objc, &objv);
        }
        if (result == TCL_OK) {
            Tcl_ResetResult(interp);
            /*
             * Match axis -command evaluation semantics.
             */
            result = Tcl_EvalObjv(interp, objc, objv, 0);
        }
        Tcl_DecrRefCount(cmdObjPtr);
        if (result != TCL_OK) {
            /*
             * Formatting happens during redraw, so there is no
             * synchronous widget command through which to return
             * this error.
             *
             * Report it asynchronously and retain the default label.
             */
            Tcl_BackgroundException(interp, result);
            Tcl_ResetResult(interp);
            return Tcl_DStringValue(dsPtr);
        }
    }
    /*
     * The command result is the complete replacement label.
     * An empty result deliberately suppresses the label.
     */
    Tcl_DStringSetLength(dsPtr, 0);
    Tcl_DStringAppend(dsPtr, Tcl_GetString(Tcl_GetObjResult(interp)), -1);
    Tcl_ResetResult(interp);
    return Tcl_DStringValue(dsPtr);
}

static void DrawPolarAngularLabels(Graph *graphPtr, Drawable drawable, Grid *gridPtr, double completeRadius) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->nAngleMajorTicks; i++) {
        double degrees;
        Point2D point;
        Tk_Anchor anchor;
        Tcl_DString label;
        char *string;

        degrees = graphPtr->angleMajorTicks[i];
        if (!GetPolarAngularLabelPosition(graphPtr, gridPtr, completeRadius, degrees, &point, &anchor)) {
            continue;
        }
        style.anchor = anchor;
        string = FormatPolarAngleLabel(graphPtr, degrees, &label);
        if (string[0] != '\0') {
            Rbc_DrawText(graphPtr->tkwin, drawable, string, &style, ROUND(point.x), ROUND(point.y));
        }
        Tcl_DStringFree(&label);
    }
}

void Rbc_DrawPolarLabels(Graph *graphPtr, Drawable drawable) {
    Grid *gridPtr;
    double completeRadius;

    gridPtr = graphPtr->gridPtr;
    if (gridPtr == NULL) {
        return;
    }
    /*
     * Radial labels don't require the origin or a complete circle to
     * be visible.  Each one is independently displayed when its point
     * on the positive zero-degree ray lies in the current viewport.
     */
    DrawPolarRadialLabels(graphPtr, drawable, gridPtr);
    /*
     * Angular labels still use the complete-circle radius to decide
     * between circular placement and clipped-spoke-end placement.
     */
    completeRadius = PolarCompleteCircleRadius(gridPtr);
    DrawPolarAngularLabels(graphPtr, drawable, gridPtr, completeRadius);
}

static void SmithImpedanceToGamma(double resistance, double reactance, double *realPtr, double *imagPtr) {
    double denominator;

    denominator = (resistance + 1.0) * (resistance + 1.0) + reactance * reactance;
    *realPtr = (resistance * resistance + reactance * reactance - 1.0) / denominator;
    *imagPtr = (2.0 * reactance) / denominator;
}

static void SmithGridValueToGamma(int admittance, double realValue, double imagValue, double *realPtr,
                                  double *imagPtr) {
    SmithImpedanceToGamma(realValue, imagValue, realPtr, imagPtr);
    /*
     * For normalized admittance:
     *
     *     Gamma = (1 - y) / (1 + y)
     *
     * which is the impedance Smith transform rotated
     * by 180 degrees in the Gamma plane.
     */
    if (admittance) {
        *realPtr = -*realPtr;
        *imagPtr = -*imagPtr;
    }
}

static int SmithUnitCircleVisible(Grid *gridPtr) {
    Axis *xAxisPtr;
    Axis *yAxisPtr;

    xAxisPtr = gridPtr->axes.x;
    yAxisPtr = gridPtr->axes.y;
    if ((xAxisPtr == NULL) || (yAxisPtr == NULL)) {
        return FALSE;
    }
    if (xAxisPtr->logScale || yAxisPtr->logScale) {
        return FALSE;
    }
    /*
     * The complete Smith unit circle is visible only when the
     * Gamma viewport contains [-1,+1] in both dimensions.
     */
    return (xAxisPtr->min <= -1.0) && (xAxisPtr->max >= 1.0) && (yAxisPtr->min <= -1.0) && (yAxisPtr->max >= 1.0);
}

static char *FormatSmithLabel(Graph *graphPtr, Tcl_Obj *commandObjPtr, int admittance, int imaginary, double value,
                              Tcl_DString *dsPtr) {
    Tcl_Interp *interp;
    const char *domain;
    char valueString[64];
    int result;

    Tcl_DStringInit(dsPtr);

    /*
     * The callback always receives the original numeric quantity.
     * For imaginary labels this is signed:
     *
     *     +x / -x for impedance
     *     +b / -b for admittance
     *
     * The built-in textual representation adds the "j" notation.
     */
    snprintf(valueString, sizeof(valueString), "%g", value);
    /*
     * Default label.
     */
    if (imaginary) {
        char labelString[64];

        snprintf(labelString, sizeof(labelString), (value > 0.0) ? "+j%g" : "-j%g", fabs(value));
        Tcl_DStringAppend(dsPtr, labelString, -1);
    } else {
        Tcl_DStringAppend(dsPtr, valueString, -1);
    }
    if (commandObjPtr == NULL) {
        return Tcl_DStringValue(dsPtr);
    }
    interp = graphPtr->interp;
    {
        Tcl_Size prefixObjc;

        result = Tcl_ListObjLength(interp, commandObjPtr, &prefixObjc);
        /*
         * ConfigureGraph() normally guarantees that this object has
         * a valid list representation.  Preserve drawing if that
         * invariant is somehow violated.
         */
        if (result != TCL_OK) {
            Tcl_BackgroundException(interp, result);
            Tcl_ResetResult(interp);
            return Tcl_DStringValue(dsPtr);
        }
        /*
         * An empty command prefix selects normal formatting.
         */
        if (prefixObjc == 0) {
            return Tcl_DStringValue(dsPtr);
        }
    }
    domain = admittance ? "admittance" : "impedance";
    {
        Tcl_Obj *cmdObjPtr;
        Tcl_Obj **objv;
        Tcl_Size objc;

        /*
         * Never modify the Tk-managed option object itself.
         */
        cmdObjPtr = Tcl_DuplicateObj(commandObjPtr);
        Tcl_IncrRefCount(cmdObjPtr);
        result = Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewStringObj(Tk_PathName(graphPtr->tkwin), -1));
        if (result == TCL_OK) {
            result = Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewStringObj(domain, -1));
        }
        if (result == TCL_OK) {
            result = Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewStringObj(valueString, -1));
        }
        if (result == TCL_OK) {
            result = Tcl_ListObjGetElements(interp, cmdObjPtr, &objc, &objv);
        }
        if (result == TCL_OK) {
            Tcl_ResetResult(interp);
            /*
             * Match axis -command and Polar -anglecommand
             * evaluation semantics.
             */
            result = Tcl_EvalObjv(interp, objc, objv, 0);
        }
        Tcl_DecrRefCount(cmdObjPtr);
        if (result != TCL_OK) {
            /*
             * Formatting happens during redraw, so there is no
             * synchronous widget command through which to return
             * this error.
             *
             * Report it asynchronously and retain the default label.
             */
            Tcl_BackgroundException(interp, result);
            Tcl_ResetResult(interp);
            return Tcl_DStringValue(dsPtr);
        }
    }
    /*
     * The command result is the complete replacement label.
     * An empty result deliberately suppresses the label.
     */
    Tcl_DStringSetLength(dsPtr, 0);
    Tcl_DStringAppend(dsPtr, Tcl_GetString(Tcl_GetObjResult(interp)), -1);
    Tcl_ResetResult(interp);
    return Tcl_DStringValue(dsPtr);
}

static void DrawSmithRealLabels(Graph *graphPtr, Drawable drawable, Grid *gridPtr, int admittance) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.x;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->nSmithRealMajorTicks; i++) {
        double value;
        double real;
        double imag;
        Point2D point;
        Tcl_DString label;
        char *string;

        value = graphPtr->smithRealMajorTicks[i];
        SmithGridValueToGamma(admittance, value, 0.0, &real, &imag);
        point = Rbc_Map2D(graphPtr, real, imag, &gridPtr->axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        string = FormatSmithLabel(graphPtr, graphPtr->smithRealCommandObjPtr, admittance, FALSE, value, &label);
        if (string[0] == '\0') {
            Tcl_DStringFree(&label);
            continue;
        }
        if (value == 0.0) {
            if (admittance) {
                /*
                 * g=0 -> Gamma=+1.
                 */
                style.anchor = TK_ANCHOR_E;
                Rbc_DrawText(graphPtr->tkwin, drawable, string, &style, ROUND(point.x) - SMITH_RESISTANCE_LABEL_OFFSET,
                             ROUND(point.y));
            } else {
                /*
                 * r=0 -> Gamma=-1.
                 */
                style.anchor = TK_ANCHOR_W;
                Rbc_DrawText(graphPtr->tkwin, drawable, string, &style, ROUND(point.x) + SMITH_RESISTANCE_LABEL_OFFSET,
                             ROUND(point.y));
            }
        } else if (admittance) {
            style.anchor = TK_ANCHOR_N;
            Rbc_DrawText(graphPtr->tkwin, drawable, string, &style, ROUND(point.x),
                         ROUND(point.y) + SMITH_RESISTANCE_LABEL_OFFSET);
        } else {
            style.anchor = TK_ANCHOR_S;
            Rbc_DrawText(graphPtr->tkwin, drawable, string, &style, ROUND(point.x),
                         ROUND(point.y) - SMITH_RESISTANCE_LABEL_OFFSET);
        }
        Tcl_DStringFree(&label);
    }
}

static void PolarRadialLabelsToPostScript(Graph *graphPtr, PsToken psToken, Grid *gridPtr) {
    Axis *axisPtr;
    TextStyle style;
    Ticks *ticksPtr;
    Tcl_Size i;

    axisPtr = gridPtr->axes.x;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    ticksPtr = Rbc_AllocAxisMajorTicks(axisPtr);
    if (ticksPtr == NULL) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    style.anchor = GetPolarRadialLabelAnchor(graphPtr);
    for (i = 0; i < ticksPtr->nTicks; i++) {
        TickLabel *labelPtr;
        Point2D point;
        double radius;

        radius = ticksPtr->values[i];
        if ((!FINITE(radius)) || (radius <= 0.0)) {
            continue;
        }
        if (!GetPolarRadialLabelPosition(graphPtr, gridPtr, radius, &point)) {
            continue;
        }
        labelPtr = Rbc_AllocAxisTickLabel(graphPtr, axisPtr, radius);
        if (labelPtr == NULL) {
            continue;
        }
        Rbc_TextToPostScript(psToken, labelPtr->string, &style, point.x - POLAR_RADIAL_LABEL_OFFSET,
                             point.y - POLAR_RADIAL_LABEL_OFFSET);
        ckfree(labelPtr);
    }
    ckfree(ticksPtr);
}

static void PolarAngularLabelsToPostScript(Graph *graphPtr, PsToken psToken, Grid *gridPtr, double completeRadius) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->nAngleMajorTicks; i++) {
        double degrees;
        Point2D point;
        Tk_Anchor anchor;
        Tcl_DString label;
        char *string;

        degrees = graphPtr->angleMajorTicks[i];
        if (!GetPolarAngularLabelPosition(graphPtr, gridPtr, completeRadius, degrees, &point, &anchor)) {
            continue;
        }
        style.anchor = anchor;
        string = FormatPolarAngleLabel(graphPtr, degrees, &label);
        if (string[0] != '\0') {
            Rbc_TextToPostScript(psToken, string, &style, point.x, point.y);
        }
        Tcl_DStringFree(&label);
    }
}

void Rbc_PolarLabelsToPostScript(Graph *graphPtr, PsToken psToken) {
    Grid *gridPtr;
    double completeRadius;

    gridPtr = graphPtr->gridPtr;
    if (gridPtr == NULL) {
        return;
    }
    PolarRadialLabelsToPostScript(graphPtr, psToken, gridPtr);
    completeRadius = PolarCompleteCircleRadius(gridPtr);
    PolarAngularLabelsToPostScript(graphPtr, psToken, gridPtr, completeRadius);
}

static void MapSmithResistanceCircle(Graph *graphPtr, Grid *gridPtr, double resistance, int admittance,
                                     Segment2D *segments) {
    double center;
    double radius;
    double factor;
    Point2D previous;
    Tcl_Size i;

    center = resistance / (resistance + 1.0);
    radius = 1.0 / (resistance + 1.0);
    factor = admittance ? -1.0 : 1.0;
    previous = Rbc_Map2D(graphPtr, factor * (center + radius), 0.0, &gridPtr->axes);
    for (i = 1; i <= SMITH_CIRCLE_SEGMENTS; i++) {
        double theta;
        Point2D next;

        theta = (2.0 * POLAR_PI * (double)i) / (double)SMITH_CIRCLE_SEGMENTS;
        next = Rbc_Map2D(graphPtr, factor * (center + radius * cos(theta)), factor * (radius * sin(theta)),
                         &gridPtr->axes);
        segments[i - 1].p = previous;
        segments[i - 1].q = next;
        previous = next;
    }
}

static Point2D SmithReactanceGamma(double reactance, double t, int admittance) {
    Point2D point;
    double resistance;

    if (t >= 1.0) {
        /*
         * Infinite resistance/conductance.
         */
        point.x = admittance ? -1.0 : 1.0;
        point.y = 0.0;
    } else {
        resistance = t / (1.0 - t);
        SmithGridValueToGamma(admittance, resistance, reactance, &point.x, &point.y);
    }
    return point;
}

static Point2D SmithReactancePoint(Graph *graphPtr, Grid *gridPtr, double reactance, double t, int admittance) {
    Point2D point;

    point = SmithReactanceGamma(reactance, t, admittance);
    return Rbc_Map2D(graphPtr, point.x, point.y, &gridPtr->axes);
}

static int ClipSegmentParameter(double p, double q, double *enterPtr, double *exitPtr) {
    double r;

    if (p == 0.0) {
        return q >= 0.0;
    }
    r = q / p;
    if (p < 0.0) {
        if (r > *exitPtr) {
            return FALSE;
        }
        if (r > *enterPtr) {
            *enterPtr = r;
        }
    } else {
        if (r < *enterPtr) {
            return FALSE;
        }
        if (r < *exitPtr) {
            *exitPtr = r;
        }
    }
    return TRUE;
}

static int ClipSegmentToGridViewport(Grid *gridPtr, Point2D p, Point2D q, Point2D *enterPointPtr,
                                     Point2D *exitPointPtr) {
    Axis *xAxisPtr;
    Axis *yAxisPtr;
    double dx;
    double dy;
    double enter;
    double exit;

    xAxisPtr = gridPtr->axes.x;
    yAxisPtr = gridPtr->axes.y;
    if ((xAxisPtr == NULL) || (yAxisPtr == NULL)) {
        return FALSE;
    }
    dx = q.x - p.x;
    dy = q.y - p.y;
    enter = 0.0;
    exit = 1.0;
    if (!ClipSegmentParameter(-dx, p.x - xAxisPtr->min, &enter, &exit) ||
        !ClipSegmentParameter(dx, xAxisPtr->max - p.x, &enter, &exit) ||
        !ClipSegmentParameter(-dy, p.y - yAxisPtr->min, &enter, &exit) ||
        !ClipSegmentParameter(dy, yAxisPtr->max - p.y, &enter, &exit)) {
        return FALSE;
    }
    if (enter > exit) {
        return FALSE;
    }
    enterPointPtr->x = p.x + enter * dx;
    enterPointPtr->y = p.y + enter * dy;
    exitPointPtr->x = p.x + exit * dx;
    exitPointPtr->y = p.y + exit * dy;
    return FINITE(enterPointPtr->x) && FINITE(enterPointPtr->y) && FINITE(exitPointPtr->x) && FINITE(exitPointPtr->y);
}

static Tk_Anchor GetSmithReactiveEdgeAnchor(const Point2D *enterPtr, const Point2D *exitPtr) {
    double dx;
    double dy;
    double degrees;

    /*
     * enterPtr is the outer visible end of the contour.
     * Reverse the local contour direction so the resulting anchor
     * makes the text extend back into the viewport.
     */
    dx = enterPtr->x - exitPtr->x;
    dy = enterPtr->y - exitPtr->y;
    if ((fabs(dx) <= DBL_EPSILON) && (fabs(dy) <= DBL_EPSILON)) {
        return TK_ANCHOR_CENTER;
    }
    /*
     * Screen Y grows downward.
     */
    degrees = atan2(-dy, dx) / POLAR_DEG_TO_RAD;
    if (degrees < 0.0) {
        degrees += 360.0;
    }
    return GetInwardLabelAnchor(degrees);
}

static int GetSmithReactiveLabelPosition(Graph *graphPtr, Grid *gridPtr, double reactive, int admittance,
                                         Point2D *pointPtr, Tk_Anchor *anchorPtr) {
    if (SmithUnitCircleVisible(gridPtr)) {
        double real;
        double imag;
        double degrees;

        /*
         * Preserve the traditional placement slightly inside the
         * Smith unit circle.
         */
        SmithGridValueToGamma(admittance, 0.0, reactive, &real, &imag);
        real *= SMITH_REACTANCE_LABEL_RADIUS;
        imag *= SMITH_REACTANCE_LABEL_RADIUS;
        *pointPtr = Rbc_Map2D(graphPtr, real, imag, &gridPtr->axes);
        if ((!FINITE(pointPtr->x)) || (!FINITE(pointPtr->y))) {
            return FALSE;
        }
        degrees = atan2(imag, real) / POLAR_DEG_TO_RAD;
        if (degrees < 0.0) {
            degrees += 360.0;
        }
        *anchorPtr = GetInwardLabelAnchor(degrees);
        return TRUE;
    } else {
        Point2D previous;
        Tcl_Size i;

        /*
         * The outer Smith boundary is no longer fully visible.
         * Walk the same polyline used to render this fixed
         * reactance/susceptance contour, starting at its r=0/g=0
         * unit-circle end.
         *
         * The first segment intersecting the viewport identifies the
         * visible outer end of the contour.
         */
        previous = SmithReactanceGamma(reactive, 0.0, admittance);
        for (i = 1; i <= SMITH_ARC_SEGMENTS; i++) {
            Point2D next;
            Point2D enter;
            Point2D exit;
            Point2D screenEnter;
            Point2D screenExit;
            double t;

            t = (double)i / (double)SMITH_ARC_SEGMENTS;
            next = SmithReactanceGamma(reactive, t, admittance);
            if (!ClipSegmentToGridViewport(gridPtr, previous, next, &enter, &exit)) {
                previous = next;
                continue;
            }
            screenEnter = Rbc_Map2D(graphPtr, enter.x, enter.y, &gridPtr->axes);
            screenExit = Rbc_Map2D(graphPtr, exit.x, exit.y, &gridPtr->axes);
            if ((!FINITE(screenEnter.x)) || (!FINITE(screenEnter.y)) || (!FINITE(screenExit.x)) ||
                (!FINITE(screenExit.y))) {
                return FALSE;
            }
            *pointPtr = screenEnter;
            *anchorPtr = GetSmithReactiveEdgeAnchor(&screenEnter, &screenExit);
            return TRUE;
        }
    }
    /*
     * This reactive contour does not cross the visible viewport.
     */
    return FALSE;
}

static void DrawSmithReactiveLabels(Graph *graphPtr, Drawable drawable, Grid *gridPtr, int admittance) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->nSmithImagMajorTicks; i++) {
        double magnitude;
        int sign;

        magnitude = graphPtr->smithImagMajorTicks[i];
        for (sign = 1; sign >= -1; sign -= 2) {
            double reactive;
            Point2D point;
            Tk_Anchor anchor;
            Tcl_DString label;
            char *string;

            reactive = (double)sign * magnitude;
            if (!GetSmithReactiveLabelPosition(graphPtr, gridPtr, reactive, admittance, &point, &anchor)) {
                continue;
            }
            style.anchor = anchor;
            string = FormatSmithLabel(graphPtr, graphPtr->smithImagCommandObjPtr, admittance, TRUE, reactive, &label);
            if (string[0] != '\0') {
                Rbc_DrawText(graphPtr->tkwin, drawable, string, &style, ROUND(point.x), ROUND(point.y));
            }
            Tcl_DStringFree(&label);
        }
    }
}

void Rbc_DrawSmithLabels(Graph *graphPtr, Drawable drawable) {
    Grid *gridPtr;

    gridPtr = graphPtr->gridPtr;
    if (gridPtr == NULL) {
        return;
    }
    if ((graphPtr->smithGrid == SMITH_GRID_IMPEDANCE) || (graphPtr->smithGrid == SMITH_GRID_BOTH)) {
        DrawSmithRealLabels(graphPtr, drawable, gridPtr, FALSE);
        DrawSmithReactiveLabels(graphPtr, drawable, gridPtr, FALSE);
    }
    if ((graphPtr->smithGrid == SMITH_GRID_ADMITTANCE) || (graphPtr->smithGrid == SMITH_GRID_BOTH)) {
        DrawSmithRealLabels(graphPtr, drawable, gridPtr, TRUE);
        DrawSmithReactiveLabels(graphPtr, drawable, gridPtr, TRUE);
    }
}

static void MapSmithReactanceArc(Graph *graphPtr, Grid *gridPtr, double reactance, int admittance,
                                 Segment2D *segments) {
    Point2D previous;
    Tcl_Size i;

    previous = SmithReactancePoint(graphPtr, gridPtr, reactance, 0.0, admittance);
    for (i = 1; i <= SMITH_ARC_SEGMENTS; i++) {
        Point2D next;
        double t;

        t = (double)i / (double)SMITH_ARC_SEGMENTS;
        next = SmithReactancePoint(graphPtr, gridPtr, reactance, t, admittance);
        segments[i - 1].p = previous;
        segments[i - 1].q = next;
        previous = next;
    }
}

static void MapSmithResistanceGrid(Graph *graphPtr, Grid *gridPtr) {
    Tcl_Size nMajor;
    Tcl_Size nMinor;
    Tcl_Size nCircles;
    Tcl_Size nSegments;
    Tcl_Size index;
    Tcl_Size i;
    Segment2D *segments;
    int doImpedance;
    int doAdmittance;

    doImpedance = (graphPtr->smithGrid == SMITH_GRID_IMPEDANCE) || (graphPtr->smithGrid == SMITH_GRID_BOTH);
    doAdmittance = (graphPtr->smithGrid == SMITH_GRID_ADMITTANCE) || (graphPtr->smithGrid == SMITH_GRID_BOTH);
    nMajor = graphPtr->nSmithRealMajorTicks;
    nMinor = gridPtr->minorGrid ? graphPtr->nSmithRealMinorTicks : 0;
    /*
     * Count the actual circles that will be drawn.
     *
     * In "both" mode r=0 and g=0 are the same unit circle,
     * so admittance zero contours are omitted when impedance
     * contours are also present.
     */
    nCircles = 0;
    if (doImpedance) {
        if (nMajor > TCL_SIZE_MAX - nCircles) {
            return;
        }
        nCircles += nMajor;
        if (nMinor > TCL_SIZE_MAX - nCircles) {
            return;
        }
        nCircles += nMinor;
    }
    if (doAdmittance) {
        for (i = 0; i < nMajor; i++) {
            double value;

            value = graphPtr->smithRealMajorTicks[i];
            if (doImpedance && (value == 0.0)) {
                continue;
            }
            if (nCircles == TCL_SIZE_MAX) {
                return;
            }
            nCircles++;
        }
        for (i = 0; i < nMinor; i++) {
            double value;

            value = graphPtr->smithRealMinorTicks[i];
            if (doImpedance && (value == 0.0)) {
                continue;
            }
            if (nCircles == TCL_SIZE_MAX) {
                return;
            }
            nCircles++;
        }
    }
    if (nCircles == 0) {
        return;
    }
    if (nCircles > TCL_SIZE_MAX / SMITH_CIRCLE_SEGMENTS) {
        return;
    }
    nSegments = nCircles * SMITH_CIRCLE_SEGMENTS;
    if ((size_t)nSegments > SIZE_MAX / sizeof(Segment2D)) {
        return;
    }
    segments = Tcl_AttemptAlloc((size_t)nSegments * sizeof(Segment2D));
    if (segments == NULL) {
        return;
    }
    index = 0;
    if (doImpedance) {
        for (i = 0; i < nMajor; i++) {
            MapSmithResistanceCircle(graphPtr, gridPtr, graphPtr->smithRealMajorTicks[i], FALSE, segments + index);
            index += SMITH_CIRCLE_SEGMENTS;
        }
        for (i = 0; i < nMinor; i++) {
            MapSmithResistanceCircle(graphPtr, gridPtr, graphPtr->smithRealMinorTicks[i], FALSE, segments + index);
            index += SMITH_CIRCLE_SEGMENTS;
        }
    }
    if (doAdmittance) {
        for (i = 0; i < nMajor; i++) {
            double value;

            value = graphPtr->smithRealMajorTicks[i];
            /*
             * If impedance is also present, r=0 has already drawn
             * the same unit circle as g=0.
             */
            if (doImpedance && (value == 0.0)) {
                continue;
            }
            MapSmithResistanceCircle(graphPtr, gridPtr, value, TRUE, segments + index);
            index += SMITH_CIRCLE_SEGMENTS;
        }
        for (i = 0; i < nMinor; i++) {
            double value;

            value = graphPtr->smithRealMinorTicks[i];
            if (doImpedance && (value == 0.0)) {
                continue;
            }
            MapSmithResistanceCircle(graphPtr, gridPtr, value, TRUE, segments + index);
            index += SMITH_CIRCLE_SEGMENTS;
        }
    }
    gridPtr->x.segments = segments;
    gridPtr->x.nSegments = index;
}

static void MapSmithReactanceGrid(Graph *graphPtr, Grid *gridPtr) {
    Tcl_Size nMajor;
    Tcl_Size nMinor;
    Tcl_Size nDomains;
    Tcl_Size nValues;
    Tcl_Size nArcs;
    Tcl_Size nSegments;
    Tcl_Size index;
    Tcl_Size i;
    Segment2D *segments;
    int doImpedance;
    int doAdmittance;

    doImpedance = (graphPtr->smithGrid == SMITH_GRID_IMPEDANCE) || (graphPtr->smithGrid == SMITH_GRID_BOTH);
    doAdmittance = (graphPtr->smithGrid == SMITH_GRID_ADMITTANCE) || (graphPtr->smithGrid == SMITH_GRID_BOTH);
    nMajor = graphPtr->nSmithImagMajorTicks;
    nMinor = gridPtr->minorGrid ? graphPtr->nSmithImagMinorTicks : 0;
    nDomains = doImpedance + doAdmittance;
    if (nMajor > TCL_SIZE_MAX - nMinor) {
        return;
    }
    nValues = nMajor + nMinor;
    /*
     * Each configured magnitude produces both its positive and
     * negative reactive contour in every selected Smith domain.
     */
    if (nValues > TCL_SIZE_MAX / 2) {
        return;
    }
    nArcs = 2 * nValues;
    if ((nDomains > 0) && (nArcs > TCL_SIZE_MAX / nDomains)) {
        return;
    }
    nArcs *= nDomains;
    if (nArcs > (TCL_SIZE_MAX - 1) / SMITH_ARC_SEGMENTS) {
        return;
    }
    nSegments = 1 + nArcs * SMITH_ARC_SEGMENTS;
    if ((size_t)nSegments > SIZE_MAX / sizeof(Segment2D)) {
        return;
    }
    segments = Tcl_AttemptAlloc((size_t)nSegments * sizeof(Segment2D));
    if (segments == NULL) {
        return;
    }
    /*
     * Zero reactance/susceptance is the same real diameter.
     */
    segments[0].p = Rbc_Map2D(graphPtr, -1.0, 0.0, &gridPtr->axes);
    segments[0].q = Rbc_Map2D(graphPtr, 1.0, 0.0, &gridPtr->axes);
    index = 1;

#define MAP_REACTIVE_DOMAIN(admittance_)                                                                               \
    do {                                                                                                               \
        for (i = 0; i < nMajor; i++) {                                                                                 \
            double value = graphPtr->smithImagMajorTicks[i];                                                           \
                                                                                                                       \
            MapSmithReactanceArc(graphPtr, gridPtr, value, admittance_, segments + index);                             \
            index += SMITH_ARC_SEGMENTS;                                                                               \
                                                                                                                       \
            MapSmithReactanceArc(graphPtr, gridPtr, -value, admittance_, segments + index);                            \
            index += SMITH_ARC_SEGMENTS;                                                                               \
        }                                                                                                              \
                                                                                                                       \
        for (i = 0; i < nMinor; i++) {                                                                                 \
            double value = graphPtr->smithImagMinorTicks[i];                                                           \
                                                                                                                       \
            MapSmithReactanceArc(graphPtr, gridPtr, value, admittance_, segments + index);                             \
            index += SMITH_ARC_SEGMENTS;                                                                               \
                                                                                                                       \
            MapSmithReactanceArc(graphPtr, gridPtr, -value, admittance_, segments + index);                            \
            index += SMITH_ARC_SEGMENTS;                                                                               \
        }                                                                                                              \
    } while (0)

    if (doImpedance) {
        MAP_REACTIVE_DOMAIN(FALSE);
    }
    if (doAdmittance) {
        MAP_REACTIVE_DOMAIN(TRUE);
    }

#undef MAP_REACTIVE_DOMAIN

    gridPtr->y.segments = segments;
    gridPtr->y.nSegments = index;
}

void Rbc_MapSmithGrid(Graph *graphPtr, Grid *gridPtr) {
    Axis *xAxisPtr;
    Axis *yAxisPtr;

    xAxisPtr = gridPtr->axes.x;
    yAxisPtr = gridPtr->axes.y;
    if ((xAxisPtr == NULL) || (yAxisPtr == NULL) || xAxisPtr->logScale || yAxisPtr->logScale) {
        return;
    }
    MapSmithResistanceGrid(graphPtr, gridPtr);
    MapSmithReactanceGrid(graphPtr, gridPtr);
}

static void SmithRealLabelsToPostScript(Graph *graphPtr, PsToken psToken, Grid *gridPtr, int admittance) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.x;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->nSmithRealMajorTicks; i++) {
        double value;
        double real;
        double imag;
        Point2D point;
        Tcl_DString label;
        char *string;

        value = graphPtr->smithRealMajorTicks[i];
        SmithGridValueToGamma(admittance, value, 0.0, &real, &imag);
        point = Rbc_Map2D(graphPtr, real, imag, &gridPtr->axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        string = FormatSmithLabel(graphPtr, graphPtr->smithRealCommandObjPtr, admittance, FALSE, value, &label);
        if (string[0] == '\0') {
            Tcl_DStringFree(&label);
            continue;
        }
        if (value == 0.0) {
            if (admittance) {
                /*
                 * g=0 -> Gamma=+1.
                 */
                style.anchor = TK_ANCHOR_E;
                Rbc_TextToPostScript(psToken, string, &style, point.x - SMITH_RESISTANCE_LABEL_OFFSET, point.y);
            } else {
                /*
                 * r=0 -> Gamma=-1.
                 */
                style.anchor = TK_ANCHOR_W;
                Rbc_TextToPostScript(psToken, string, &style, point.x + SMITH_RESISTANCE_LABEL_OFFSET, point.y);
            }
        } else if (admittance) {
            style.anchor = TK_ANCHOR_N;
            Rbc_TextToPostScript(psToken, string, &style, point.x, point.y + SMITH_RESISTANCE_LABEL_OFFSET);
        } else {
            style.anchor = TK_ANCHOR_S;
            Rbc_TextToPostScript(psToken, string, &style, point.x, point.y - SMITH_RESISTANCE_LABEL_OFFSET);
        }
        Tcl_DStringFree(&label);
    }
}

static void SmithReactiveLabelsToPostScript(Graph *graphPtr, PsToken psToken, Grid *gridPtr, int admittance) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->nSmithImagMajorTicks; i++) {
        double magnitude;
        int sign;

        magnitude = graphPtr->smithImagMajorTicks[i];
        for (sign = 1; sign >= -1; sign -= 2) {
            double reactive;
            Point2D point;
            Tk_Anchor anchor;
            Tcl_DString label;
            char *string;

            reactive = (double)sign * magnitude;
            if (!GetSmithReactiveLabelPosition(graphPtr, gridPtr, reactive, admittance, &point, &anchor)) {
                continue;
            }
            style.anchor = anchor;
            string = FormatSmithLabel(graphPtr, graphPtr->smithImagCommandObjPtr, admittance, TRUE, reactive, &label);
            if (string[0] != '\0') {
                Rbc_TextToPostScript(psToken, string, &style, point.x, point.y);
            }
            Tcl_DStringFree(&label);
        }
    }
}

void Rbc_SmithLabelsToPostScript(Graph *graphPtr, PsToken psToken) {
    Grid *gridPtr;

    gridPtr = graphPtr->gridPtr;
    if (gridPtr == NULL) {
        return;
    }
    if ((graphPtr->smithGrid == SMITH_GRID_IMPEDANCE) || (graphPtr->smithGrid == SMITH_GRID_BOTH)) {
        SmithRealLabelsToPostScript(graphPtr, psToken, gridPtr, FALSE);
        SmithReactiveLabelsToPostScript(graphPtr, psToken, gridPtr, FALSE);
    }
    if ((graphPtr->smithGrid == SMITH_GRID_ADMITTANCE) || (graphPtr->smithGrid == SMITH_GRID_BOTH)) {
        SmithRealLabelsToPostScript(graphPtr, psToken, gridPtr, TRUE);
        SmithReactiveLabelsToPostScript(graphPtr, psToken, gridPtr, TRUE);
    }
}
