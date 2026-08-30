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
