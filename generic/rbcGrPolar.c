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
#define POLAR_MAJOR_ANGLE_STEP 30.0
#define POLAR_MINOR_ANGLE_STEP 15.0

#define POLAR_ANGLE_LABEL_RADIUS 0.90
#define POLAR_RADIAL_LABEL_OFFSET 2

#define SMITH_CIRCLE_SEGMENTS 180
#define SMITH_ARC_SEGMENTS 90

#define SMITH_RESISTANCE_LABEL_OFFSET 2
#define SMITH_REACTANCE_LABEL_RADIUS 0.94

static const double smithMajorResistance[] = {
    0.0, 0.2, 0.5, 1.0, 2.0, 5.0
};

static const double smithMinorResistance[] = {
    0.1, 0.3, 0.7, 1.5, 3.0, 10.0
};

static const double smithMajorReactance[] = {
    0.2, 0.5, 1.0, 2.0, 5.0
};

static const double smithMinorReactance[] = {
    0.1, 0.3, 0.7, 1.5, 3.0, 10.0
};

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
    double step;
    Tcl_Size nSpokes;
    Tcl_Size i;

    if (gridPtr->minorGrid) {
        step = POLAR_MINOR_ANGLE_STEP;
    } else {
        step = POLAR_MAJOR_ANGLE_STEP;
    }
    nSpokes = (Tcl_Size)(360.0 / step);
    segments = Tcl_AttemptAlloc((size_t)nSpokes * sizeof(Segment2D));
    if (segments == NULL) {
        return;
    }
    origin = Rbc_Map2D(graphPtr, 0.0, 0.0, &gridPtr->axes);
    for (i = 0; i < nSpokes; i++) {
        double theta;

        theta = ((double)i * step) * POLAR_DEG_TO_RAD;
        segments[i].p = origin;
        segments[i].q = Rbc_Map2D(graphPtr, maxRadius * cos(theta), maxRadius * sin(theta), &gridPtr->axes);
    }
    gridPtr->y.segments = segments;
    gridPtr->y.nSegments = nSpokes;
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
     * This avoids requiring additional Polar-specific margins.
     */
    labelRadius = maxRadius * POLAR_ANGLE_LABEL_RADIUS;
    for (i = 0; i < 12; i++) {
        char string[32];
        double degrees;
        double theta;
        Point2D point;

        degrees = (double)i * POLAR_MAJOR_ANGLE_STEP;
        theta = degrees * POLAR_DEG_TO_RAD;
        style.anchor = GetPolarAngleLabelAnchor(graphPtr, degrees);
        point = Rbc_Map2D(graphPtr, labelRadius * cos(theta), labelRadius * sin(theta), &gridPtr->axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        snprintf(string, sizeof(string), "%d\xC2\xB0", (int)degrees);
        Rbc_DrawText(graphPtr->tkwin, drawable, string, &style, ROUND(point.x), ROUND(point.y));
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


static void DrawSmithResistanceLabels(Graph *graphPtr, Drawable drawable, Grid *gridPtr) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.x;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < (Tcl_Size)(sizeof(smithMajorResistance) / sizeof(smithMajorResistance[0])); i++) {
        char string[32];
        double resistance;
        double real;
        double imag;
        Point2D point;
        resistance = smithMajorResistance[i];
        SmithImpedanceToGamma(resistance, 0.0, &real, &imag);
        point = Rbc_Map2D(graphPtr, real, imag, &gridPtr->axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        snprintf(string, sizeof(string), "%g", resistance);
        if (resistance == 0.0) {
            /*
             * Γ=-1 lies on the left edge.  Keep the label inside.
             */
            style.anchor = TK_ANCHOR_W;
            Rbc_DrawText(graphPtr->tkwin, drawable, string, &style, ROUND(point.x) + SMITH_RESISTANCE_LABEL_OFFSET,
                         ROUND(point.y));
        } else {
            /*
             * Put the resistance labels immediately above the
             * zero-reactance diameter.
             */
            style.anchor = TK_ANCHOR_S;
            Rbc_DrawText(graphPtr->tkwin, drawable, string, &style, ROUND(point.x),
                         ROUND(point.y) - SMITH_RESISTANCE_LABEL_OFFSET);
        }
    }
}

static void DrawSmithReactanceLabels(Graph *graphPtr, Drawable drawable, Grid *gridPtr) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < (Tcl_Size)(sizeof(smithMajorReactance) / sizeof(smithMajorReactance[0])); i++) {
        double magnitude;
        int sign;

        magnitude = smithMajorReactance[i];
        for (sign = 1; sign >= -1; sign -= 2) {
            char string[32];
            double reactance;
            double real;
            double imag;
            double degrees;
            Point2D point;

            reactance = sign * magnitude;
            /*
             * Intersection of this reactance arc with the
             * r=0 unit-circle boundary.
             */
            SmithImpedanceToGamma(0.0, reactance, &real, &imag);
            /*
             * Pull the label slightly inside the unit circle.
             */
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
            if (reactance > 0.0) {
                snprintf(string, sizeof(string), "+j%g", magnitude);
            } else {
                snprintf(string, sizeof(string), "-j%g", magnitude);
            }
            Rbc_DrawText(graphPtr->tkwin, drawable, string, &style, ROUND(point.x), ROUND(point.y));
        }
    }
}

void Rbc_DrawSmithLabels(Graph *graphPtr, Drawable drawable) {
    Grid *gridPtr;

    gridPtr = graphPtr->gridPtr;
    if (gridPtr == NULL) {
        return;
    }
    DrawSmithResistanceLabels(graphPtr, drawable, gridPtr);
    DrawSmithReactanceLabels(graphPtr, drawable, gridPtr);
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
    for (i = 0; i < 12; i++) {
        char string[32];
        double degrees;
        double theta;
        Point2D point;

        degrees = (double)i * POLAR_MAJOR_ANGLE_STEP;
        theta = degrees * POLAR_DEG_TO_RAD;
        style.anchor = GetPolarAngleLabelAnchor(graphPtr, degrees);
        point = Rbc_Map2D(graphPtr, labelRadius * cos(theta), labelRadius * sin(theta), &gridPtr->axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        snprintf(string, sizeof(string), "%d\xC2\xB0", (int)degrees);
        Rbc_TextToPostScript(psToken, string, &style, point.x, point.y);
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

static void MapSmithResistanceCircle(Graph *graphPtr, Grid *gridPtr, double resistance, Segment2D *segments) {
    double center;
    double radius;
    Point2D previous;
    Tcl_Size i;

    center = resistance / (resistance + 1.0);
    radius = 1.0 / (resistance + 1.0);
    previous = Rbc_Map2D(graphPtr, center + radius, 0.0, &gridPtr->axes);
    for (i = 1; i <= SMITH_CIRCLE_SEGMENTS; i++) {
        double theta;
        Point2D next;

        theta = (2.0 * POLAR_PI * (double)i) / (double)SMITH_CIRCLE_SEGMENTS;
        next = Rbc_Map2D(graphPtr, center + radius * cos(theta), radius * sin(theta), &gridPtr->axes);
        segments[i - 1].p = previous;
        segments[i - 1].q = next;
        previous = next;
    }
}

static Point2D SmithReactancePoint(Graph *graphPtr, Grid *gridPtr, double reactance, double t) {
    double resistance;
    double real;
    double imag;

    if (t >= 1.0) {
        real = 1.0;
        imag = 0.0;
    } else {
        resistance = t / (1.0 - t);
        SmithImpedanceToGamma(resistance, reactance, &real, &imag);
    }
    return Rbc_Map2D(graphPtr, real, imag, &gridPtr->axes);
}

static void MapSmithReactanceArc(Graph *graphPtr, Grid *gridPtr, double reactance, Segment2D *segments) {
    Point2D previous;
    Tcl_Size i;

    previous = SmithReactancePoint(graphPtr, gridPtr, reactance, 0.0);
    for (i = 1; i <= SMITH_ARC_SEGMENTS; i++) {
        Point2D next;
        double t;

        t = (double)i / (double)SMITH_ARC_SEGMENTS;
        next = SmithReactancePoint(graphPtr, gridPtr, reactance, t);
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
    Segment2D *segments;
    Tcl_Size i;
    Tcl_Size index;

    nMajor = sizeof(smithMajorResistance) / sizeof(smithMajorResistance[0]);
    nMinor = gridPtr->minorGrid ? sizeof(smithMinorResistance) / sizeof(smithMinorResistance[0]) : 0;
    nCircles = nMajor + nMinor;
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
    for (i = 0; i < nMajor; i++) {
        MapSmithResistanceCircle(graphPtr, gridPtr, smithMajorResistance[i], segments + index);
        index += SMITH_CIRCLE_SEGMENTS;
    }
    for (i = 0; i < nMinor; i++) {
        MapSmithResistanceCircle(graphPtr, gridPtr, smithMinorResistance[i], segments + index);
        index += SMITH_CIRCLE_SEGMENTS;
    }
    gridPtr->x.segments = segments;
    gridPtr->x.nSegments = index;
}

static void MapSmithReactanceGrid(Graph *graphPtr, Grid *gridPtr) {
    Tcl_Size nMajor;
    Tcl_Size nMinor;
    Tcl_Size nArcs;
    Tcl_Size nSegments;
    Segment2D *segments;
    Tcl_Size i;
    Tcl_Size index;

    nMajor = sizeof(smithMajorReactance) / sizeof(smithMajorReactance[0]);
    nMinor = gridPtr->minorGrid ? sizeof(smithMinorReactance) / sizeof(smithMinorReactance[0]) : 0;
    /*
     * Positive and negative reactance for each value,
     * plus one segment for x = 0.
     */
    nArcs = 2 * (nMajor + nMinor);
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
     * Zero-reactance line.
     */
    segments[0].p = Rbc_Map2D(graphPtr, -1.0, 0.0, &gridPtr->axes);
    segments[0].q = Rbc_Map2D(graphPtr, 1.0, 0.0, &gridPtr->axes);
    index = 1;
    for (i = 0; i < nMajor; i++) {
        double x;

        x = smithMajorReactance[i];
        MapSmithReactanceArc(graphPtr, gridPtr, x, segments + index);
        index += SMITH_ARC_SEGMENTS;
        MapSmithReactanceArc(graphPtr, gridPtr, -x, segments + index);
        index += SMITH_ARC_SEGMENTS;
    }
    for (i = 0; i < nMinor; i++) {
        double x;

        x = smithMinorReactance[i];
        MapSmithReactanceArc(graphPtr, gridPtr, x, segments + index);
        index += SMITH_ARC_SEGMENTS;
        MapSmithReactanceArc(graphPtr, gridPtr, -x, segments + index);
        index += SMITH_ARC_SEGMENTS;
    }
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

static void SmithResistanceLabelsToPostScript(Graph *graphPtr, PsToken psToken, Grid *gridPtr) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.x;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < (Tcl_Size)(sizeof(smithMajorResistance) / sizeof(smithMajorResistance[0])); i++) {
        char string[32];
        double resistance;
        double real;
        double imag;
        Point2D point;

        resistance = smithMajorResistance[i];
        SmithImpedanceToGamma(resistance, 0.0, &real, &imag);
        point = Rbc_Map2D(graphPtr, real, imag, &gridPtr->axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        snprintf(string, sizeof(string), "%g", resistance);
        if (resistance == 0.0) {
            style.anchor = TK_ANCHOR_W;
            Rbc_TextToPostScript(psToken, string, &style, point.x + SMITH_RESISTANCE_LABEL_OFFSET, point.y);
        } else {
            style.anchor = TK_ANCHOR_S;
            Rbc_TextToPostScript(psToken, string, &style, point.x, point.y - SMITH_RESISTANCE_LABEL_OFFSET);
        }
    }
}

static void SmithReactanceLabelsToPostScript(Graph *graphPtr, PsToken psToken, Grid *gridPtr) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < (Tcl_Size)(sizeof(smithMajorReactance) / sizeof(smithMajorReactance[0])); i++) {
        double magnitude;
        int sign;

        magnitude = smithMajorReactance[i];
        for (sign = 1; sign >= -1; sign -= 2) {
            char string[32];
            double reactance;
            double real;
            double imag;
            double degrees;
            Point2D point;

            reactance = sign * magnitude;
            SmithImpedanceToGamma(0.0, reactance, &real, &imag);
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
            snprintf(string, sizeof(string), (reactance > 0.0) ? "+j%g" : "-j%g", magnitude);
            Rbc_TextToPostScript(psToken, string, &style, point.x, point.y);
        }
    }
}

void Rbc_SmithLabelsToPostScript(Graph *graphPtr, PsToken psToken) {
    Grid *gridPtr;

    gridPtr = graphPtr->gridPtr;
    if (gridPtr == NULL) {
        return;
    }
    SmithResistanceLabelsToPostScript(graphPtr, psToken, gridPtr);
    SmithReactanceLabelsToPostScript(graphPtr, psToken, gridPtr);
}
