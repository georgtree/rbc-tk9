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


static double PolarMaximumRadius(Grid *gridPtr) {
    Axis *xAxisPtr;
    Axis *yAxisPtr;
    double radius;

    xAxisPtr = gridPtr->axes.x;
    yAxisPtr = gridPtr->axes.y;
    if ((xAxisPtr == NULL) || (yAxisPtr == NULL)) {
        return 0.0;
    }
    /*
     * A Cartesian log axis cannot represent the full complex plane,
     * because the Polar representation requires values on both sides
     * of zero.
     */
    if (xAxisPtr->logScale || yAxisPtr->logScale) {
        return 0.0;
    }
    /*
     * The origin must be visible on both axes.  Limit the radius to
     * the largest complete circle contained in the current viewport.
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

static void MapPolarSpoke(Graph *graphPtr, Grid *gridPtr, Point2D origin, double maxRadius, double degrees,
                          Segment2D *segmentPtr) {
    double theta;

    theta = degrees * POLAR_DEG_TO_RAD;
    segmentPtr->p = origin;
    segmentPtr->q = Rbc_Map2D(graphPtr, maxRadius * cos(theta), maxRadius * sin(theta), &gridPtr->axes);
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

static void MapPolarCircles(Graph *graphPtr, Grid *gridPtr, double maxRadius) {
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
        if ((!FINITE(radius)) || (radius <= 0.0) || (radius > maxRadius)) {
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
        if ((!FINITE(radius)) || (radius <= 0.0) || (radius > maxRadius)) {
            continue;
        }
        MapCircle(graphPtr, gridPtr, radius, circleSegments + (nCircles * POLAR_CIRCLE_SEGMENTS));
        nCircles++;
    }
    ckfree(axisSegments);
    gridPtr->x.segments = circleSegments;
    gridPtr->x.nSegments = nCircles * POLAR_CIRCLE_SEGMENTS;
}

static void MapPolarSpokes(Graph *graphPtr, Grid *gridPtr, double maxRadius) {
    Segment2D *segments;
    Point2D origin;
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
    origin = Rbc_Map2D(graphPtr, 0.0, 0.0, &gridPtr->axes);
    index = 0;
    /*
     * Major spokes are always present.
     */
    for (i = 0; i < nMajor; i++) {
        MapPolarSpoke(graphPtr, gridPtr, origin, maxRadius, graphPtr->angleMajorTicks[i], segments + index);
        index++;
    }
    /*
     * Minor spokes are controlled by the existing grid -minor
     * option.
     */
    for (i = 0; i < nMinor; i++) {
        MapPolarSpoke(graphPtr, gridPtr, origin, maxRadius, graphPtr->angleMinorTicks[i], segments + index);
        index++;
    }
    gridPtr->y.segments = segments;
    gridPtr->y.nSegments = index;
}

void Rbc_MapPolarGrid(Graph *graphPtr, Grid *gridPtr) {
    double maxRadius;

    maxRadius = PolarMaximumRadius(gridPtr);
    if ((!FINITE(maxRadius)) || (maxRadius <= 0.0)) {
        return;
    }
    MapPolarCircles(graphPtr, gridPtr, maxRadius);
    MapPolarSpokes(graphPtr, gridPtr, maxRadius);
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

static void DrawPolarRadialLabels(Graph *graphPtr, Drawable drawable, Grid *gridPtr, double maxRadius) {
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
        if ((!FINITE(radius)) || (radius <= 0.0) || (radius > maxRadius)) {
            continue;
        }
        labelPtr = Rbc_AllocAxisTickLabel(graphPtr, axisPtr, radius);
        if (labelPtr == NULL) {
            continue;
        }
        point = Rbc_Map2D(graphPtr, radius, 0.0, &gridPtr->axes);
        if (FINITE(point.x) && FINITE(point.y)) {
            Rbc_DrawText(graphPtr->tkwin, drawable, labelPtr->string, &style,
                         ROUND(point.x) - POLAR_RADIAL_LABEL_OFFSET, ROUND(point.y) - POLAR_RADIAL_LABEL_OFFSET);
        }
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

static void DrawPolarAngularLabels(Graph *graphPtr, Drawable drawable, Grid *gridPtr, double maxRadius) {
    Axis *axisPtr;
    TextStyle style;
    double labelRadius;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    /*
     * Keep angular labels slightly inside the outer circle.
     */
    labelRadius = maxRadius * POLAR_ANGLE_LABEL_RADIUS;
    for (i = 0; i < graphPtr->nAngleMajorTicks; i++) {
        double degrees;
        double theta;
        Point2D point;

        degrees = graphPtr->angleMajorTicks[i];
        theta = degrees * POLAR_DEG_TO_RAD;
        style.anchor = GetPolarAngleLabelAnchor(graphPtr, degrees);
        point = Rbc_Map2D(graphPtr, labelRadius * cos(theta), labelRadius * sin(theta), &gridPtr->axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        {
            Tcl_DString label;
            char *string;

            string = FormatPolarAngleLabel(graphPtr, degrees, &label);
            if (string[0] != '\0') {
                Rbc_DrawText(graphPtr->tkwin, drawable, string, &style, ROUND(point.x), ROUND(point.y));
            }
            Tcl_DStringFree(&label);
        }
    }
}

void Rbc_DrawPolarLabels(Graph *graphPtr, Drawable drawable) {
    Grid *gridPtr;
    double maxRadius;

    gridPtr = graphPtr->gridPtr;
    if (gridPtr == NULL) {
        return;
    }
    maxRadius = PolarMaximumRadius(gridPtr);
    if ((!FINITE(maxRadius)) || (maxRadius <= 0.0)) {
        return;
    }
    DrawPolarRadialLabels(graphPtr, drawable, gridPtr, maxRadius);
    DrawPolarAngularLabels(graphPtr, drawable, gridPtr, maxRadius);
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
            double real;
            double imag;
            double degrees;
            Point2D point;
            Tcl_DString label;
            char *string;

            reactive = (double)sign * magnitude;
            SmithGridValueToGamma(admittance, 0.0, reactive, &real, &imag);
            /*
             * Pull the label slightly inside the unit circle.
             */
            real *= SMITH_REACTANCE_LABEL_RADIUS;
            imag *= SMITH_REACTANCE_LABEL_RADIUS;
            point = Rbc_Map2D(graphPtr, real, imag, &gridPtr->axes);
            if ((!FINITE(point.x)) || (!FINITE(point.y))) {
                continue;
            }
            /*
             * Choose an inward-facing anchor from the actual
             * position in the Gamma plane.
             */
            degrees = atan2(imag, real) / POLAR_DEG_TO_RAD;
            if (degrees < 0.0) {
                degrees += 360.0;
            }
            style.anchor = GetInwardLabelAnchor(degrees);
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

static void PolarRadialLabelsToPostScript(Graph *graphPtr, PsToken psToken, Grid *gridPtr, double maxRadius) {
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
        if ((!FINITE(radius)) || (radius <= 0.0) || (radius > maxRadius)) {
            continue;
        }
        labelPtr = Rbc_AllocAxisTickLabel(graphPtr, axisPtr, radius);
        if (labelPtr == NULL) {
            continue;
        }
        point = Rbc_Map2D(graphPtr, radius, 0.0, &gridPtr->axes);
        if (FINITE(point.x) && FINITE(point.y)) {
            Rbc_TextToPostScript(psToken, labelPtr->string, &style, point.x - POLAR_RADIAL_LABEL_OFFSET,
                                 point.y - POLAR_RADIAL_LABEL_OFFSET);
        }
        ckfree(labelPtr);
    }
    ckfree(ticksPtr);
}

static void PolarAngularLabelsToPostScript(Graph *graphPtr, PsToken psToken, Grid *gridPtr, double maxRadius) {
    Axis *axisPtr;
    TextStyle style;
    double labelRadius;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    labelRadius = maxRadius * POLAR_ANGLE_LABEL_RADIUS;
    for (i = 0; i < graphPtr->nAngleMajorTicks; i++) {
        double degrees;
        double theta;
        Point2D point;

        degrees = graphPtr->angleMajorTicks[i];
        theta = degrees * POLAR_DEG_TO_RAD;
        style.anchor = GetPolarAngleLabelAnchor(graphPtr, degrees);
        point = Rbc_Map2D(graphPtr, labelRadius * cos(theta), labelRadius * sin(theta), &gridPtr->axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        {
            Tcl_DString label;
            char *string;

            string = FormatPolarAngleLabel(graphPtr, degrees, &label);
            if (string[0] != '\0') {
                Rbc_TextToPostScript(psToken, string, &style, point.x, point.y);
            }
            Tcl_DStringFree(&label);
        }
    }
}

void Rbc_PolarLabelsToPostScript(Graph *graphPtr, PsToken psToken) {
    Grid *gridPtr;
    double maxRadius;

    gridPtr = graphPtr->gridPtr;
    if (gridPtr == NULL) {
        return;
    }
    maxRadius = PolarMaximumRadius(gridPtr);
    if ((!FINITE(maxRadius)) || (maxRadius <= 0.0)) {
        return;
    }
    PolarRadialLabelsToPostScript(graphPtr, psToken, gridPtr, maxRadius);
    PolarAngularLabelsToPostScript(graphPtr, psToken, gridPtr, maxRadius);
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

static Point2D SmithReactancePoint(Graph *graphPtr, Grid *gridPtr, double reactance, double t, int admittance) {
    double resistance;
    double real;
    double imag;

    if (t >= 1.0) {
        /*
         * Infinite resistance/conductance.
         */
        if (admittance) {
            real = -1.0;
        } else {
            real = 1.0;
        }
        imag = 0.0;
    } else {
        resistance = t / (1.0 - t);
        SmithGridValueToGamma(admittance, resistance, reactance, &real, &imag);
    }
    return Rbc_Map2D(graphPtr, real, imag, &gridPtr->axes);
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
            double real;
            double imag;
            double degrees;
            Point2D point;
            Tcl_DString label;
            char *string;

            reactive = (double)sign * magnitude;
            SmithGridValueToGamma(admittance, 0.0, reactive, &real, &imag);
            real *= SMITH_REACTANCE_LABEL_RADIUS;
            imag *= SMITH_REACTANCE_LABEL_RADIUS;
            point = Rbc_Map2D(graphPtr, real, imag, &gridPtr->axes);
            if ((!FINITE(point.x)) || (!FINITE(point.y))) {
                continue;
            }
            degrees = atan2(imag, real) / POLAR_DEG_TO_RAD;
            if (degrees < 0.0) {
                degrees += 360.0;
            }
            style.anchor = GetInwardLabelAnchor(degrees);
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
