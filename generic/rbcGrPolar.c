/**
 * @file rbcGrPolar.c
 * @brief Polar and Smith grid geometry and label presentation.
 *
 * Polar and Smith graph data remain Cartesian: x = real, y = imaginary.
 */

/** @defgroup polar_geometry Polar grid geometry internals
 * Private data-plane helpers; axes and grid records are borrowed.
 * Mapping requires cleared destination caches and does not draw labels.
 */

/** @defgroup polar_labels Polar and Smith label internals
 * Positioning, formatting and screen/export presentation. Geometry helpers
 * borrow prepared graph state; formatting paths may evaluate Tcl callbacks.
 */

#include "rbcGraph.h"
#include "rbcRender.h"

#define POLAR_PI 3.14159265358979323846264338327950288
#define POLAR_DEG_TO_RAD (POLAR_PI / 180.0)
#define POLAR_CIRCLE_SEGMENTS 180
#define POLAR_ANGLE_LABEL_RADIUS 0.90
#define POLAR_RADIAL_LABEL_OFFSET 2
#define SMITH_CIRCLE_SEGMENTS 180
#define SMITH_ARC_SEGMENTS 90
#define SMITH_RESISTANCE_LABEL_OFFSET 2
#define SMITH_REACTANCE_LABEL_RADIUS 0.94

/** @brief Computed finite radial interval. */
typedef struct {
    double min; /**< Minimum finite radial value. */
    double max; /**< Maximum finite radial value. */
} PolarRadiusRange;

/**
 * @ingroup polar_geometry
 * @brief Bound radii intersecting the grid's Cartesian viewport rectangle.
 * @param gridPtr Borrowed grid with mapped X/Y axes.
 * @param rangePtr Receives nearest and farthest distances from the origin.
 * @return TRUE for a finite nonempty radial range; FALSE for missing axes,
 * logarithmic axes, invalid axis bounds or nonfinite distances.
 * @details A viewport spanning the origin has minimum radius zero. The maximum
 * is the farthest corner distance, not a fully contained circle radius.
 * Outputs are unspecified on failure. No mapping or allocation is performed.
 */
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

/**
 * @ingroup polar_geometry
 * @brief Find the largest origin-centered circle contained in the axis rectangle.
 * Returns zero for missing/logarithmic axes or an origin not strictly inside
 * both ranges. Borrows prepared axes; unlike GetPolarRadiusRange, this helper
 * does not independently validate finite bounds.
 *
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 */
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

/**
 * @ingroup polar_geometry
 * @brief Intersect a forward ray at a degree angle with the data viewport.
 * Requires prepared finite linear axis bounds and a finite angle. Uses slab
 * parameters starting at zero; directions within DBL_EPSILON of zero are
 * parallel. Returns FALSE for missing axes or no forward intersection. On
 * success writes data-space endpoints in increasing ray distance, not pixels.
 *
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 * @param[in] degrees Finite forward-ray angle in degrees.
 * @param[out] pPtr Required output for the nearer visible data-space endpoint, valid on TRUE.
 * @param[out] qPtr Required output for the farther visible data-space endpoint, valid on TRUE.
 */
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

/**
 * @ingroup polar_geometry
 * @brief Write one sampled polar circle into caller-owned segment storage.
 * Requires room for POLAR_CIRCLE_SEGMENTS entries and current mapped axes.
 * Maps consecutive angular samples through Rbc_Map2D; neither allocates nor
 * clips complete strokes. The caller owns the buffer and its eventual clipping.
 *
 * @param[in] graphPtr Borrowed graph with current axis mapping and inversion state.
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 * @param[in] radius Finite positive circle radius in Cartesian data units.
 * @param[out] segments Caller-owned buffer of at least POLAR_CIRCLE_SEGMENTS records; receives mapped
 *    pixel segments.
 */
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

/**
 * @ingroup polar_geometry
 * @brief Clip a degree-angle ray and map its endpoints to screen coordinates.
 * Returns FALSE if the ray misses the viewport; output is valid only on
 * success. Borrows graph/grid state, allocates nothing and does not separately
 * check mapped endpoints for finiteness.
 *
 * @param[in] graphPtr Borrowed graph with current axis mapping and inversion state.
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 * @param[in] degrees Finite ray angle in degrees.
 * @param[out] segmentPtr Required mapped pixel segment output; valid only on TRUE.
 */
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

/**
 * @ingroup polar_geometry
 * @brief Recover the data-X tick represented by an axis grid segment.
 * Uses the mapped origin for the other coordinate and selects screen X or Y
 * according to graph inversion. Requires a segment from the selected X axis;
 * it is not a general segment-to-radius conversion.
 *
 * @param[in] graphPtr Borrowed graph with current axis mapping and inversion state.
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 * @param[in] segmentPtr Borrowed mapped segment produced for this grid X axis; inversion selects the
 *    coordinate to invert.
 */
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

/**
 * @ingroup polar_geometry
 * @brief Build the radial segment cache from positive visible X-axis ticks.
 * Borrows a validated radial range. Frees temporary axis segments on every
 * allocation/count exit. Transfers the new array to gridPtr->x only on success;
 * callers must already have released old caches. Count overflow or attempted
 * allocation failure silently leaves the destination unchanged.
 *
 * @param[in] graphPtr Borrowed graph with current axis mapping and inversion state.
 * @param[in,out] gridPtr Grid supplying axes/minor policy and receiving owned x-segment storage; old x
 *    cache must be cleared.
 * @param[in] rangePtr Borrowed validated minimum/maximum radii intersecting the viewport.
 */
static void MapPolarCircles(Graph *graphPtr, Grid *gridPtr, const PolarRadiusRange *rangePtr) {
    Segment2D *axisSegments;
    Segment2D *circleSegments;
    Tcl_Size nAxisSegments;
    Tcl_Size nCircles;
    Tcl_Size needed;
    Tcl_Size i;

    axisSegments = NULL;
    nAxisSegments = 0;
    Rbc_GetAxisSegments(graphPtr, gridPtr->axes.x, gridPtr->minorGrid, &axisSegments, &nAxisSegments);
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

/**
 * @ingroup polar_geometry
 * @brief Build the angular segment cache from global major and optional minor ticks.
 * Uses gridPtr axes/minor policy and graphPtr->gridPtr angular arrays.
 * Transfers visible segments to gridPtr->y; frees an allocation with no visible
 * rays. Old caches must be cleared by the caller. Overflow/allocation failure
 * returns without a diagnostic or a destination update.
 *
 * @param[in] graphPtr Borrowed mapped graph whose global grid supplies angular tick arrays.
 * @param[in,out] gridPtr Grid supplying axes/minor policy and receiving owned y-segment storage; old y
 *    cache must be cleared.
 */
static void MapPolarSpokes(Graph *graphPtr, Grid *gridPtr) {
    Segment2D *segments;
    Tcl_Size nMajor;
    Tcl_Size nMinor;
    Tcl_Size nSpokes;
    Tcl_Size index;
    Tcl_Size i;

    nMajor = graphPtr->gridPtr->nAngleMajorTicks;
    nMinor = gridPtr->minorGrid ? graphPtr->gridPtr->nAngleMinorTicks : 0;
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
        if (MapPolarSpoke(graphPtr, gridPtr, graphPtr->gridPtr->angleMajorTicks[i], segments + index)) {
            index++;
        }
    }
    /*
     * Minor spokes are controlled by the existing grid -minor option.
     */
    for (i = 0; i < nMinor; i++) {
        if (MapPolarSpoke(graphPtr, gridPtr, graphPtr->gridPtr->angleMinorTicks[i], segments + index)) {
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

/**
 * @ingroup polar_geometry
 * @brief Populate polar circle and spoke caches using prepared linear axes.
 * Requires empty destination caches and a live global grid for angular ticks.
 * An invalid radial viewport returns without mapping. The two allocations are
 * independent, so partial geometry is possible on allocation failure. Does not
 * draw, notify, schedule layout or free old caches.
 *
 * @param[in] graphPtr Borrowed graph with prepared mappings and a live global grid for angular settings.
 * @param[in,out] gridPtr Grid with empty x/y caches; receives owned mapped segments independently for
 *    circles and spokes.
 */
void Rbc_MapPolarGrid(Graph *graphPtr, Grid *gridPtr) {
    PolarRadiusRange range;

    if (!GetPolarRadiusRange(gridPtr, &range)) {
        return;
    }
    MapPolarCircles(graphPtr, gridPtr, &range);
    MapPolarSpokes(graphPtr, gridPtr);
}

/**
 * @ingroup polar_labels
 * @brief Resolve the radial anchor from the global grid.
 * Automatic selection is TK_ANCHOR_SE; otherwise returns the configured anchor.
 * Requires a live global grid and does not modify it.
 *
 * @param[in] graphPtr Borrowed graph with live global grid anchor settings.
 */
static Tk_Anchor GetPolarRadialLabelAnchor(Graph *graphPtr) {
    if (graphPtr->gridPtr->radialLabelAnchor.isAuto) {
        return TK_ANCHOR_SE;
    }
    return graphPtr->gridPtr->radialLabelAnchor.anchor;
}

/**
 * @ingroup polar_labels
 * @brief Choose one of eight inward text anchors from a degree direction.
 * Callers provide a normalized angle in [0,360). Sector boundaries lie at
 * 22.5 degrees plus multiples of 45. This is a direction lookup only; it does
 * not inspect text extents or guarantee the label fits inside a viewport.
 *
 * @param[in] degrees Finite data angle in degrees, normalized to [0,360) for automatic anchor selection.
 */
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

/**
 * @ingroup polar_labels
 * @brief Resolve a circular angular label anchor.
 * An explicit global-grid anchor wins; automatic mode uses the supplied data
 * angle directly. No screen-axis inversion or descending-axis adjustment is
 * performed by this helper.
 *
 * @param[in] graphPtr Borrowed graph with live global grid angular-anchor settings.
 * @param[in] degrees Finite data angle in degrees, normalized to [0,360) for automatic anchor selection.
 */
static Tk_Anchor GetPolarAngleLabelAnchor(Graph *graphPtr, double degrees) {
    if (!graphPtr->gridPtr->angleLabelAnchor.isAuto) {
        return graphPtr->gridPtr->angleLabelAnchor.anchor;
    }
    return GetInwardLabelAnchor(degrees);
}

/**
 * @ingroup polar_labels
 * @brief Anchor a label inward from the far end of a mapped spoke.
 * An explicit anchor wins. Otherwise derives direction from screen endpoints,
 * negating screen Y; a degenerate segment selects CENTER. Borrows the segment
 * and does not alter its geometry.
 *
 * @param[in] graphPtr Borrowed graph with live global grid angular-anchor settings.
 * @param[in] segmentPtr Borrowed mapped pixel spoke ordered from near to far; label belongs at its q
 *    endpoint.
 */
static Tk_Anchor GetPolarSpokeLabelAnchor(Graph *graphPtr, const Segment2D *segmentPtr) {
    double dx;
    double dy;
    double degrees;

    if (!graphPtr->gridPtr->angleLabelAnchor.isAuto) {
        return graphPtr->gridPtr->angleLabelAnchor.anchor;
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

/**
 * @ingroup polar_labels
 * @brief Choose a circular or clipped-spoke angular label position.
 * A positive completeRadius selects 90 percent of that radius and the
 * data-angle anchor. Otherwise selects the far visible mapped ray endpoint
 * and a screen-direction anchor. Returns FALSE for a missing ray or nonfinite
 * position; outputs must only be consumed on success.
 *
 * @param[in] graphPtr Borrowed graph with current axis mapping and inversion state.
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 * @param[in] completeRadius Radius of a fully contained origin-centered circle; nonpositive selects
 *    clipped-spoke placement.
 * @param[in] degrees Finite data angle in degrees, normalized to [0,360) for automatic anchor selection.
 * @param[out] pointPtr Required output for mapped pixel position; consume only when TRUE is returned.
 * @param[out] anchorPtr Required output for the text anchor; consume only when TRUE is returned.
 */
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

/**
 * @ingroup polar_labels
 * @brief Map a positive radial tick on the zero-degree ray when visible.
 * Requires linear axes and a finite positive radius whose point (radius,0)
 * lies inside both data ranges. The origin itself need not be visible.
 * Returns TRUE only for finite mapped coordinates; output is success-only.
 *
 * @param[in] graphPtr Borrowed graph with current axis mapping and inversion state.
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 * @param[in] radius Finite positive data radius; label point is (radius,0).
 * @param[out] pointPtr Required output for mapped pixel position; consume only when TRUE is returned.
 */
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

/**
 * @ingroup polar_labels
 * @brief Draw radial labels through a temporary screen text renderer.
 * Allocates an X-axis major-tick snapshot and formatted tick labels, freeing
 * each after use. A NULL formatted label terminates the loop.
 * Borrows graph/grid/drawable and copies the relevant axis text style with
 * rotation zero. Requires showTicks on that axis. Screen positions are rounded;
 * the renderer is ended before returning. Formatting can evaluate Tcl; callers
 * must retain graph lifetime. A cancelled formatter stops the loop before
 * copied style or component resources are used again.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in] drawable Borrowed compatible screen destination; no ownership is acquired.
 * @param[in] gridPtr Borrowed prepared grid/axis resources; no cached resource may be reused after
 *    formatter cancellation.
 */
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
    Rbc_RenderContext *text = Rbc_RenderBeginScreenText(graphPtr->tkwin, drawable);
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
            break;
        }
        Rbc_RenderText(text, labelPtr->string, &style, ROUND(point.x) - POLAR_RADIAL_LABEL_OFFSET,
                     ROUND(point.y) - POLAR_RADIAL_LABEL_OFFSET);
        ckfree(labelPtr);
    }
    ckfree(ticksPtr);
    Rbc_RenderEnd(text);
}

/**
 * @ingroup polar_labels
 * @brief Evaluate a grid label prefix while guarding the current drawing pass.
 * Borrows an initialized default-label DString, prefix and numeric text. Appends
 * widget path, optional domain and value to an owned prefix copy. Preserves
 * interpreter state; errors report a background exception and keep default text.
 * Uses the existing axis-format guard to reject recursive rendering and detect
 * mutation. Returns NULL on invalidation, scheduling a fresh redraw; callers
 * must free the DString and stop before accessing cached grid/style resources.
 * The enclosing widget command or display wrapper must preserve graph lifetime.
 *
 * @param[in,out] graphPtr Caller-protected graph with live interpreter/window; formatting may invalidate
 *    its drawing state.
 * @param[in] prefixObjPtr Borrowed command-prefix object, or NULL for default text; duplicated before
 *    arguments are appended.
 * @param[in] domain Borrowed NUL-terminated domain argument, or NULL to omit that argument.
 * @param[in] value Borrowed NUL-terminated numeric spelling copied into the callback command.
 * @param[in,out] dsPtr Initialized caller-owned DString containing default text; caller frees it even
 *    when return is NULL.
 */
static char *FormatGridLabel(Graph *graphPtr, Tcl_Obj *prefixObjPtr, const char *domain,
                            const char *value, Tcl_DString *dsPtr) {
    Tcl_Interp *interp;
    Tcl_InterpState state;
    Tcl_Obj *cmdObjPtr;
    Tcl_Obj **objv;
    Tcl_Size prefixObjc, objc;
    int result;

    if (prefixObjPtr == NULL) {
        return Tcl_DStringValue(dsPtr);
    }
    interp = graphPtr->interp;
    state = Tcl_SaveInterpState(interp, TCL_OK);
    result = Tcl_ListObjLength(interp, prefixObjPtr, &prefixObjc);
    if (result != TCL_OK || prefixObjc == 0) {
        if (result != TCL_OK) {
            Tcl_BackgroundException(interp, result);
        }
        Tcl_RestoreInterpState(interp, state);
        return Tcl_DStringValue(dsPtr);
    }
    cmdObjPtr = Tcl_DuplicateObj(prefixObjPtr);
    Tcl_IncrRefCount(cmdObjPtr);
    result = Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewStringObj(Tk_PathName(graphPtr->tkwin), -1));
    if (result == TCL_OK && domain != NULL) {
        result = Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewStringObj(domain, -1));
    }
    if (result == TCL_OK) {
        result = Tcl_ListObjAppendElement(interp, cmdObjPtr, Tcl_NewStringObj(value, -1));
    }
    if (result == TCL_OK) {
        result = Tcl_ListObjGetElements(interp, cmdObjPtr, &objc, &objv);
    }
    if (result == TCL_OK) {
        Tcl_ResetResult(interp);
        graphPtr->axisFormatActive = TRUE;
        result = Tcl_EvalObjv(interp, objc, objv, 0);
        graphPtr->axisFormatActive = FALSE;
    }
    Tcl_DecrRefCount(cmdObjPtr);
    if (result != TCL_OK) {
        Tcl_BackgroundException(interp, result);
    } else {
        Tcl_DStringSetLength(dsPtr, 0);
        Tcl_DStringAppend(dsPtr, Tcl_GetString(Tcl_GetObjResult(interp)), -1);
    }
    Tcl_RestoreInterpState(interp, state);
    /* No prefix, axis, grid or copied text-style resource is safe to use after
     * a formatter invalidates the graph, even when its result was TCL_OK. */
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        Rbc_EventuallyRedrawGraph(graphPtr);
        return NULL;
    }
    return Tcl_DStringValue(dsPtr);
}

/**
 * @ingroup polar_labels
 * @brief Format a degree label into a caller-released DString.
 * Initializes dsPtr and returns its borrowed buffer. Default text is %g plus
 * a degree sign. A nonempty command prefix receives widget path and numeric
 * degrees in the current evaluation context. Success replaces all text, including
 * an empty result; failure reports a background exception and retains default
 * text. Preserves interpreter state and returns NULL if formatting invalidates the
 * graph. Caller must still free dsPtr on NULL and abandon cached drawing state.
 *
 * @param[in,out] graphPtr Caller-protected graph whose global grid supplies the angular command prefix.
 * @param[in] degrees Finite data angle in degrees, normalized to [0,360) for automatic anchor selection.
 * @param[out] dsPtr Unowned DString storage initialized here; caller must free it on every return path.
 */
static char *FormatPolarAngleLabel(Graph *graphPtr, double degrees, Tcl_DString *dsPtr) {
    char valueString[64];

    Tcl_DStringInit(dsPtr);
    snprintf(valueString, sizeof(valueString), "%g", degrees);
    Tcl_DStringAppend(dsPtr, valueString, -1);
    Tcl_DStringAppend(dsPtr, "\xC2\xB0", 2);
    return FormatGridLabel(graphPtr, graphPtr->gridPtr->angleCommandObjPtr, NULL, valueString, dsPtr);
}

/**
 * @ingroup polar_labels
 * @brief Draw angular labels through a temporary screen text renderer.
 * Reads global major angle ticks and invokes FormatPolarAngleLabel.
 * Empty formatted strings suppress drawing.
 * Borrows graph/grid/drawable and copies the relevant axis text style with
 * rotation zero. Requires showTicks on that axis. Screen positions are rounded;
 * the renderer is ended before returning. Formatting can evaluate Tcl; callers
 * must retain graph lifetime. A cancelled formatter stops the loop before
 * copied style or component resources are used again.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in] drawable Borrowed compatible screen destination; no ownership is acquired.
 * @param[in] gridPtr Borrowed prepared grid/axis resources; no cached resource may be reused after
 *    formatter cancellation.
 * @param[in] completeRadius Radius of a fully contained origin-centered circle; nonpositive selects
 *    clipped-spoke placement.
 */
static void DrawPolarAngularLabels(Graph *graphPtr, Drawable drawable, Grid *gridPtr, double completeRadius) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    Rbc_RenderContext *text = Rbc_RenderBeginScreenText(graphPtr->tkwin, drawable);
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->gridPtr->nAngleMajorTicks; i++) {
        double degrees;
        Point2D point;
        Tk_Anchor anchor;
        Tcl_DString label;
        char *string;

        degrees = graphPtr->gridPtr->angleMajorTicks[i];
        if (!GetPolarAngularLabelPosition(graphPtr, gridPtr, completeRadius, degrees, &point, &anchor)) {
            continue;
        }
        style.anchor = anchor;
        string = FormatPolarAngleLabel(graphPtr, degrees, &label);
        if (string == NULL) {
            Tcl_DStringFree(&label);
            break;
        }
        if (string[0] != '\0') {
            Rbc_RenderText(text, string, &style, ROUND(point.x), ROUND(point.y));
        }
        Tcl_DStringFree(&label);
    }
    Rbc_RenderEnd(text);
}

/**
 * @ingroup polar_labels
 * @brief Draw the visible global polar grid labels.
 * Returns when the global grid is missing or hidden. Borrows current mapped
 * graph resources and the destination drawable. Does not map segments.
 * Runs radial labels, checks Rbc_GraphCallbackValid, then chooses the circular
 * or clipped-spoke angular layout. Inner loops stop immediately when their formatter
 * reports invalidation; the caller retains graph record lifetime.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in] drawable Borrowed compatible screen destination; no ownership is acquired.
 */
void Rbc_DrawPolarLabels(Graph *graphPtr, Drawable drawable) {
    Grid *gridPtr;
    double completeRadius;

    gridPtr = graphPtr->gridPtr;
    if ((gridPtr == NULL) || gridPtr->hidden) {
        return;
    }
    /*
     * Radial labels don't require the origin or a complete circle to
     * be visible.  Each one is independently displayed when its point
     * on the positive zero-degree ray lies in the current viewport.
     */
    DrawPolarRadialLabels(graphPtr, drawable, gridPtr);
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        return;
    }
    /*
     * Angular labels still use the complete-circle radius to decide
     * between circular placement and clipped-spoke-end placement.
     */
    completeRadius = PolarCompleteCircleRadius(gridPtr);
    DrawPolarAngularLabels(graphPtr, drawable, gridPtr, completeRadius);
}

/**
 * @ingroup polar_geometry
 * @brief Convert normalized grid impedance to Cartesian reflection coordinates.
 * Writes both output scalars using direct squared terms. Intended for validated
 * grid values, not arbitrary external complex data: no denominator, overflow
 * or finite-result check is provided. Owns no storage.
 *
 * @param[in] resistance Normalized nonnegative finite grid resistance; arithmetic must remain within the
 *    unchecked formula domain.
 * @param[in] reactance Normalized finite grid reactance; this is not a physical impedance input.
 * @param[out] realPtr Required output for the Cartesian real Gamma component; no finite-result
 *    validation.
 * @param[out] imagPtr Required output for the Cartesian imaginary Gamma component; no finite-result
 *    validation.
 */
static void SmithImpedanceToGamma(double resistance, double reactance, double *realPtr, double *imagPtr) {
    double denominator;

    denominator = (resistance + 1.0) * (resistance + 1.0) + reactance * reactance;
    *realPtr = (resistance * resistance + reactance * reactance - 1.0) / denominator;
    *imagPtr = (2.0 * reactance) / denominator;
}

/**
 * @ingroup polar_geometry
 * @brief Convert normalized impedance or admittance grid coordinates.
 * Delegates to SmithImpedanceToGamma, then negates both outputs for admittance.
 * No reference-impedance scaling is applied; inputs are already normalized.
 * Inherits the unchecked arithmetic domain of the underlying grid helper.
 *
 * @param[in] admittance Nonzero selects admittance orientation; zero selects impedance.
 * @param[in] realValue Normalized nonnegative resistance or conductance for the chosen domain.
 * @param[in] imagValue Normalized signed reactance or susceptance for the chosen domain.
 * @param[out] realPtr Required real Gamma output; no reference-impedance scaling or finite-result
 *    validation.
 * @param[out] imagPtr Required imaginary Gamma output; no reference-impedance scaling or finite-result
 *    validation.
 */
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

/**
 * @ingroup polar_geometry
 * @brief Test whether linear axis bounds contain the entire Smith unit circle.
 * Returns FALSE for missing/logarithmic axes; otherwise requires both ranges
 * to contain [-1,1], inclusively. Does not test graph visibility or text bounds.
 *
 * @param[in] gridPtr Borrowed grid with prepared bounds; missing or logarithmic axes return FALSE.
 */
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

/**
 * @ingroup polar_labels
 * @brief Format a normalized Smith value into a caller-released DString.
 * Initializes dsPtr and returns its borrowed buffer. Real defaults use %g;
 * reactive defaults add signed j notation. A nonempty prefix receives widget
 * path, impedance/admittance domain and original signed numeric value. Empty
 * success suppresses text; errors report a background exception and keep the
 * default. Preserves interpreter state; returns NULL on graph invalidation. The caller
 * frees dsPtr even on NULL and stops using cached drawing state.
 *
 * @param[in,out] graphPtr Caller-protected graph with live interpreter/window; formatting may cancel its
 *    drawing pass.
 * @param[in] commandObjPtr Borrowed command prefix, or NULL for default formatting.
 * @param[in] admittance Nonzero selects admittance orientation; zero selects impedance.
 * @param[in] imaginary Nonzero requests signed j notation in default text; zero requests real-value
 *    notation.
 * @param[in] value Original signed normalized value passed numerically to the callback, without j
 *    decoration.
 * @param[out] dsPtr Unowned DString storage initialized here; caller frees it even after a NULL return.
 */
static char *FormatSmithLabel(Graph *graphPtr, Tcl_Obj *commandObjPtr, int admittance, int imaginary, double value,
                              Tcl_DString *dsPtr) {
    char valueString[64];

    Tcl_DStringInit(dsPtr);
    snprintf(valueString, sizeof(valueString), "%g", value);
    if (imaginary) {
        char labelString[64];

        snprintf(labelString, sizeof(labelString), (value > 0.0) ? "+j%g" : "-j%g", fabs(value));
        Tcl_DStringAppend(dsPtr, labelString, -1);
    } else {
        Tcl_DStringAppend(dsPtr, valueString, -1);
    }
    return FormatGridLabel(graphPtr, commandObjPtr, admittance ? "admittance" : "impedance", valueString, dsPtr);
}

/**
 * @ingroup polar_labels
 * @brief Draw Smith real labels through a temporary screen text renderer.
 * Reads global real major ticks for one normalized domain and invokes
 * FormatSmithLabel; empty strings suppress drawing. Zero values use inward
 * horizontal anchors, other values are above/below the real axis by domain.
 * Borrows graph/grid/drawable and copies the relevant axis text style with
 * rotation zero. Requires showTicks on that axis. Screen positions are rounded;
 * the renderer is ended before returning. Formatting can evaluate Tcl; callers
 * must retain graph lifetime. A cancelled formatter stops the loop before
 * copied style or component resources are used again.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in] drawable Borrowed compatible screen destination; no ownership is acquired.
 * @param[in] gridPtr Borrowed prepared grid/axis resources; no cached resource may be reused after
 *    formatter cancellation.
 * @param[in] admittance Nonzero selects admittance orientation; zero selects impedance.
 */
static void DrawSmithRealLabels(Graph *graphPtr, Drawable drawable, Grid *gridPtr, int admittance) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.x;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    Rbc_RenderContext *text = Rbc_RenderBeginScreenText(graphPtr->tkwin, drawable);
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->gridPtr->nSmithRealMajorTicks; i++) {
        double value;
        double real;
        double imag;
        Point2D point;
        Tcl_DString label;
        char *string;

        value = graphPtr->gridPtr->smithRealMajorTicks[i];
        SmithGridValueToGamma(admittance, value, 0.0, &real, &imag);
        point = Rbc_Map2D(graphPtr, real, imag, &gridPtr->axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        string = FormatSmithLabel(graphPtr, graphPtr->gridPtr->smithRealCommandObjPtr, admittance, FALSE, value, &label);
        if (string == NULL) {
            Tcl_DStringFree(&label);
            break;
        }
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
                Rbc_RenderText(text, string, &style, ROUND(point.x) - SMITH_RESISTANCE_LABEL_OFFSET,
                             ROUND(point.y));
            } else {
                /*
                 * r=0 -> Gamma=-1.
                 */
                style.anchor = TK_ANCHOR_W;
                Rbc_RenderText(text, string, &style, ROUND(point.x) + SMITH_RESISTANCE_LABEL_OFFSET,
                             ROUND(point.y));
            }
        } else if (admittance) {
            style.anchor = TK_ANCHOR_N;
            Rbc_RenderText(text, string, &style, ROUND(point.x),
                         ROUND(point.y) + SMITH_RESISTANCE_LABEL_OFFSET);
        } else {
            style.anchor = TK_ANCHOR_S;
            Rbc_RenderText(text, string, &style, ROUND(point.x),
                         ROUND(point.y) - SMITH_RESISTANCE_LABEL_OFFSET);
        }
        Tcl_DStringFree(&label);
    }
    Rbc_RenderEnd(text);
}

/**
 * @ingroup polar_labels
 * @brief Export radial labels through a temporary output renderer.
 * Allocates/frees X-axis ticks and each formatted tick label; NULL formatting
 * stops the loop. Applies the same two-pixel offset as screen drawing.
 * Borrows the export context; ends the renderer on every return. Requires
 * showTicks on the relevant axis and copies its style with rotation zero.
 * Retains fractional coordinates rather than rounding as on screen. May
 * evaluate Tcl; formatter invalidation stops traversal before cached resources are reused. Export errors remain owned by the export context.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in,out] exportPtr Borrowed initialized document context; outer caller owns clipping, error
 *    handling and finalization.
 * @param[in] gridPtr Borrowed prepared grid/axis resources; no cached resource may be reused after
 *    formatter cancellation.
 */
static void PolarRadialLabelsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Grid *gridPtr) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    Axis *axisPtr;
    TextStyle style;
    Ticks *ticksPtr;
    Tcl_Size i;

    axisPtr = gridPtr->axes.x;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        Rbc_RenderEnd(output);
        return;
    }
    ticksPtr = Rbc_AllocAxisMajorTicks(axisPtr);
    if (ticksPtr == NULL) {
        Rbc_RenderEnd(output);
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
            break;
        }
        Rbc_RenderText(output, labelPtr->string, &style, point.x - POLAR_RADIAL_LABEL_OFFSET,
                             point.y - POLAR_RADIAL_LABEL_OFFSET);
        ckfree(labelPtr);
    }
    ckfree(ticksPtr);
    Rbc_RenderEnd(output);
}

/**
 * @ingroup polar_labels
 * @brief Export angular labels through a temporary output renderer.
 * Uses global major angle ticks, the shared position/anchor helpers and
 * FormatPolarAngleLabel; empty formatted text is omitted.
 * Borrows the export context; ends the renderer on every return. Requires
 * showTicks on the relevant axis and copies its style with rotation zero.
 * Retains fractional coordinates rather than rounding as on screen. May
 * evaluate Tcl; formatter invalidation stops traversal before cached resources are reused. Export errors remain owned by the export context.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in,out] exportPtr Borrowed initialized document context; outer caller owns clipping, error
 *    handling and finalization.
 * @param[in] gridPtr Borrowed prepared grid/axis resources; no cached resource may be reused after
 *    formatter cancellation.
 * @param[in] completeRadius Radius of a fully contained origin-centered circle; nonpositive selects
 *    clipped-spoke placement.
 */
static void PolarAngularLabelsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Grid *gridPtr, double completeRadius) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        Rbc_RenderEnd(output);
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->gridPtr->nAngleMajorTicks; i++) {
        double degrees;
        Point2D point;
        Tk_Anchor anchor;
        Tcl_DString label;
        char *string;

        degrees = graphPtr->gridPtr->angleMajorTicks[i];
        if (!GetPolarAngularLabelPosition(graphPtr, gridPtr, completeRadius, degrees, &point, &anchor)) {
            continue;
        }
        style.anchor = anchor;
        string = FormatPolarAngleLabel(graphPtr, degrees, &label);
        if (string == NULL) {
            Tcl_DStringFree(&label);
            break;
        }
        if (string[0] != '\0') {
            Rbc_RenderText(output, string, &style, point.x, point.y);
        }
        Tcl_DStringFree(&label);
    }
    Rbc_RenderEnd(output);
}

/**
 * @ingroup polar_labels
 * @brief Export the visible global polar grid labels.
 * Returns when the global grid is missing or hidden. Borrows current mapped
 * graph resources and the export context. Does not map segments.
 * Runs radial labels, checks Rbc_GraphCallbackValid, then chooses the circular
 * or clipped-spoke angular layout. Inner loops stop immediately when their formatter
 * reports invalidation; the caller retains graph record lifetime.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in,out] exportPtr Borrowed initialized document context; outer caller owns clipping, error
 *    handling and finalization.
 */
void Rbc_PolarLabelsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr) {
    Grid *gridPtr;
    double completeRadius;

    gridPtr = graphPtr->gridPtr;
    if ((gridPtr == NULL) || gridPtr->hidden) {
        return;
    }
    PolarRadialLabelsExport(graphPtr, exportPtr, gridPtr);
    if (!Rbc_GraphCallbackValid(graphPtr)) {
        return;
    }
    completeRadius = PolarCompleteCircleRadius(gridPtr);
    PolarAngularLabelsExport(graphPtr, exportPtr, gridPtr, completeRadius);
}

/**
 * @ingroup polar_geometry
 * @brief Write a sampled normalized resistance or conductance circle.
 * Requires SMITH_CIRCLE_SEGMENTS writable entries and validated nonnegative
 * resistance. Center is r/(r+1), radius 1/(r+1); admittance negates both
 * coordinates before mapping. Allocates nothing and leaves clipping to drawing.
 *
 * @param[in] graphPtr Borrowed graph with current axis mapping and inversion state.
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 * @param[in] resistance Validated normalized nonnegative resistance or conductance.
 * @param[in] admittance Nonzero selects admittance orientation; zero selects impedance.
 * @param[out] segments Caller-owned buffer of at least SMITH_CIRCLE_SEGMENTS records; receives mapped
 *    pixel segments.
 */
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

/**
 * @ingroup polar_geometry
 * @brief Parameterize a constant-reactance or constant-susceptance grid arc.
 * @param reactance Normalized finite reactive value chosen by grid mapping.
 * @param t Arc parameter from zero through one.
 * @param admittance Nonzero selects the admittance orientation.
 * @return A point in the Cartesian Gamma plane, before screen mapping.
 * @details For t below one, the real component of normalized impedance or
 * admittance is t/(1-t). At t >= 1 the infinite-real endpoint is supplied
 * explicitly: (+1,0) for impedance, (-1,0) for admittance. The helper does not
 * validate arbitrary external inputs and owns no resources.
 */
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

/**
 * @ingroup polar_geometry
 * @brief Screen-map one point of a normalized reactive contour.
 * Borrows prepared axes and delegates parameter handling to SmithReactanceGamma.
 * Returns mapped coordinates without an additional finite-result check.
 *
 * @param[in] graphPtr Borrowed graph with current axis mapping and inversion state.
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 * @param[in] reactance Normalized finite signed reactance or susceptance.
 * @param[in] t Contour parameter in [0,1]; one selects the explicit infinite-real endpoint.
 * @param[in] admittance Nonzero selects admittance orientation; zero selects impedance.
 */
static Point2D SmithReactancePoint(Graph *graphPtr, Grid *gridPtr, double reactance, double t, int admittance) {
    Point2D point;

    point = SmithReactanceGamma(reactance, t, admittance);
    return Rbc_Map2D(graphPtr, point.x, point.y, &gridPtr->axes);
}

/**
 * @ingroup polar_geometry
 * @brief Tighten a segment parameter interval against one half-plane.
 * Updates caller-owned enter/exit bounds, conventionally initialized to [0,1].
 * A parallel segment is accepted iff q is nonnegative. Returns FALSE on
 * rejection; callers must discard the accumulated interval on failure.
 *
 * @param[in] p Signed half-plane denominator: negative tightens entry, positive tightens exit, zero is
 *    parallel.
 * @param[in] q Signed half-plane numerator; a parallel segment is accepted when q is nonnegative.
 * @param[in,out] enterPtr Required lower parameter bound, initially zero for a fresh segment clip.
 * @param[in,out] exitPtr Required upper parameter bound, initially one; discard the interval after
 *    rejection.
 */
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

/**
 * @ingroup polar_geometry
 * @brief Clip data-space segment endpoints against the selected axis rectangle.
 * Requires prepared bounds and finite input points. Preserves segment order
 * and writes entry/exit points on success. Missing axes, rejection or nonfinite
 * computed outputs return FALSE; output values are only valid on TRUE.
 * Does not map to screen coordinates or clip stroke thickness.
 *
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 * @param[in] p Finite data-space segment start.
 * @param[in] q Finite data-space segment end.
 * @param[out] enterPointPtr Required clipped data-space entry output; valid only on TRUE.
 * @param[out] exitPointPtr Required clipped data-space exit output; valid only on TRUE.
 */
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

/**
 * @ingroup polar_labels
 * @brief Choose an inward anchor at a clipped contour entry point.
 * Uses reversed local screen direction from exit to entry, compensating for
 * downward screen Y. Degenerate segments select CENTER. No text measurement
 * or viewport containment guarantee is made.
 *
 * @param[in] enterPtr Borrowed finite mapped pixel entry point.
 * @param[in] exitPtr Borrowed finite mapped pixel exit point defining inward direction.
 */
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

/**
 * @ingroup polar_labels
 * @brief Locate a reactive label at the unit circle or first visible contour entry.
 * When the complete unit circle is visible, scales its r=0/g=0 point by 0.94
 * and uses its data-angle anchor. Otherwise scans the same 90-segment contour
 * used for drawing, clips each chord in data space, and takes the first visible
 * entry with a screen-direction anchor. FALSE means no usable finite position;
 * outputs are success-only. No Tcl callback or allocation occurs.
 *
 * @param[in] graphPtr Borrowed graph with current axis mapping and inversion state.
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 * @param[in] reactive Validated normalized signed reactance or susceptance.
 * @param[in] admittance Nonzero selects admittance orientation; zero selects impedance.
 * @param[out] pointPtr Required output for mapped pixel position; consume only when TRUE is returned.
 * @param[out] anchorPtr Required output for the text anchor; consume only when TRUE is returned.
 */
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

/**
 * @ingroup polar_labels
 * @brief Draw Smith reactive labels through a temporary screen text renderer.
 * Formats both signs of each global major reactive magnitude for one domain.
 * Uses GetSmithReactiveLabelPosition and skips invisible contours.
 * Borrows graph/grid/drawable and copies the relevant axis text style with
 * rotation zero. Requires showTicks on that axis. Screen positions are rounded;
 * the renderer is ended before returning. Formatting can evaluate Tcl; callers
 * must retain graph lifetime. A cancelled formatter stops the loop before
 * copied style or component resources are used again.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in] drawable Borrowed compatible screen destination; no ownership is acquired.
 * @param[in] gridPtr Borrowed prepared grid/axis resources; no cached resource may be reused after
 *    formatter cancellation.
 * @param[in] admittance Nonzero selects admittance orientation; zero selects impedance.
 */
static void DrawSmithReactiveLabels(Graph *graphPtr, Drawable drawable, Grid *gridPtr, int admittance) {
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        return;
    }
    Rbc_RenderContext *text = Rbc_RenderBeginScreenText(graphPtr->tkwin, drawable);
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->gridPtr->nSmithImagMajorTicks; i++) {
        double magnitude;
        int sign;

        magnitude = graphPtr->gridPtr->smithImagMajorTicks[i];
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
            string = FormatSmithLabel(graphPtr, graphPtr->gridPtr->smithImagCommandObjPtr, admittance, TRUE, reactive, &label);
            if (string == NULL) {
                Tcl_DStringFree(&label);
                break;
            }
            if (string[0] != '\0') {
                Rbc_RenderText(text, string, &style, ROUND(point.x), ROUND(point.y));
            }
            Tcl_DStringFree(&label);
        }
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            break;
        }
    }
    Rbc_RenderEnd(text);
}

/**
 * @ingroup polar_labels
 * @brief Draw the visible global Smith grid labels.
 * Returns when the global grid is missing or hidden. Borrows current mapped
 * graph resources and the destination drawable. Does not map segments.
 * Dispatches real then reactive labels for each selected Smith domain.
 * Checks callback validity between phases and domains; inner loops stop on
 * cancellation. The enclosing caller retains graph record lifetime.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in] drawable Borrowed compatible screen destination; no ownership is acquired.
 */
void Rbc_DrawSmithLabels(Graph *graphPtr, Drawable drawable) {
    Grid *gridPtr;

    gridPtr = graphPtr->gridPtr;
    if ((gridPtr == NULL) || gridPtr->hidden) {
        return;
    }
    if ((graphPtr->gridPtr->smithGrid == SMITH_GRID_IMPEDANCE) || (graphPtr->gridPtr->smithGrid == SMITH_GRID_BOTH)) {
        DrawSmithRealLabels(graphPtr, drawable, gridPtr, FALSE);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return;
        }
        DrawSmithReactiveLabels(graphPtr, drawable, gridPtr, FALSE);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return;
        }
    }
    if ((graphPtr->gridPtr->smithGrid == SMITH_GRID_ADMITTANCE) || (graphPtr->gridPtr->smithGrid == SMITH_GRID_BOTH)) {
        DrawSmithRealLabels(graphPtr, drawable, gridPtr, TRUE);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return;
        }
        DrawSmithReactiveLabels(graphPtr, drawable, gridPtr, TRUE);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return;
        }
    }
}

/**
 * @ingroup polar_geometry
 * @brief Write a sampled constant-reactance or constant-susceptance contour.
 * Requires SMITH_ARC_SEGMENTS writable entries and prepared axes. Samples
 * t from zero to one, including the explicit infinite-real endpoint.
 * Allocates nothing; mapping and clipping responsibilities remain separate.
 *
 * @param[in] graphPtr Borrowed graph with current axis mapping and inversion state.
 * @param[in] gridPtr Borrowed grid with prepared finite linear axis bounds and mappings.
 * @param[in] reactance Validated normalized signed reactance or susceptance.
 * @param[in] admittance Nonzero selects admittance orientation; zero selects impedance.
 * @param[out] segments Caller-owned buffer of at least SMITH_ARC_SEGMENTS records; receives mapped pixel
 *    chords.
 */
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

/**
 * @ingroup polar_geometry
 * @brief Allocate and publish Smith real-component circle segments.
 * Uses global major/minor lists and domain selection with the supplied grid
 * axes/minor policy. In both-domain mode omits admittance zero circles already
 * represented by impedance. Requires cleared destination caches. Checked count
 * overflow or attempted-allocation failure returns silently; successful storage
 * is owned by gridPtr->x.
 *
 * @param[in] graphPtr Borrowed mapped graph whose global grid supplies Smith tick lists and domain
 *    selection.
 * @param[in,out] gridPtr Grid supplying axes/minor policy; empty x cache receives owned circle segments
 *    on success.
 */
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

    doImpedance = (graphPtr->gridPtr->smithGrid == SMITH_GRID_IMPEDANCE) || (graphPtr->gridPtr->smithGrid == SMITH_GRID_BOTH);
    doAdmittance = (graphPtr->gridPtr->smithGrid == SMITH_GRID_ADMITTANCE) || (graphPtr->gridPtr->smithGrid == SMITH_GRID_BOTH);
    nMajor = graphPtr->gridPtr->nSmithRealMajorTicks;
    nMinor = gridPtr->minorGrid ? graphPtr->gridPtr->nSmithRealMinorTicks : 0;
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

            value = graphPtr->gridPtr->smithRealMajorTicks[i];
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

            value = graphPtr->gridPtr->smithRealMinorTicks[i];
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
            MapSmithResistanceCircle(graphPtr, gridPtr, graphPtr->gridPtr->smithRealMajorTicks[i], FALSE, segments + index);
            index += SMITH_CIRCLE_SEGMENTS;
        }
        for (i = 0; i < nMinor; i++) {
            MapSmithResistanceCircle(graphPtr, gridPtr, graphPtr->gridPtr->smithRealMinorTicks[i], FALSE, segments + index);
            index += SMITH_CIRCLE_SEGMENTS;
        }
    }
    if (doAdmittance) {
        for (i = 0; i < nMajor; i++) {
            double value;

            value = graphPtr->gridPtr->smithRealMajorTicks[i];
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

            value = graphPtr->gridPtr->smithRealMinorTicks[i];
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

/**
 * @ingroup polar_geometry
 * @brief Allocate Smith reactive arcs and the shared real diameter.
 * Each configured magnitude supplies positive and negative arcs in each
 * selected domain. The zero-reactive diameter is emitted once, even when no
 * nonzero magnitudes exist. Requires cleared caches; overflow/allocation failure
 * leaves them unchanged. Successful storage transfers to gridPtr->y.
 *
 * @param[in] graphPtr Borrowed mapped graph whose global grid supplies Smith tick lists and domain
 *    selection.
 * @param[in,out] gridPtr Grid supplying axes/minor policy; empty y cache receives owned arcs and the
 *    shared real diameter.
 */
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

    doImpedance = (graphPtr->gridPtr->smithGrid == SMITH_GRID_IMPEDANCE) || (graphPtr->gridPtr->smithGrid == SMITH_GRID_BOTH);
    doAdmittance = (graphPtr->gridPtr->smithGrid == SMITH_GRID_ADMITTANCE) || (graphPtr->gridPtr->smithGrid == SMITH_GRID_BOTH);
    nMajor = graphPtr->gridPtr->nSmithImagMajorTicks;
    nMinor = gridPtr->minorGrid ? graphPtr->gridPtr->nSmithImagMinorTicks : 0;
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
            double value = graphPtr->gridPtr->smithImagMajorTicks[i];                                                           \
                                                                                                                       \
            MapSmithReactanceArc(graphPtr, gridPtr, value, admittance_, segments + index);                             \
            index += SMITH_ARC_SEGMENTS;                                                                               \
                                                                                                                       \
            MapSmithReactanceArc(graphPtr, gridPtr, -value, admittance_, segments + index);                            \
            index += SMITH_ARC_SEGMENTS;                                                                               \
        }                                                                                                              \
                                                                                                                       \
        for (i = 0; i < nMinor; i++) {                                                                                 \
            double value = graphPtr->gridPtr->smithImagMinorTicks[i];                                                           \
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

/**
 * @ingroup polar_geometry
 * @brief Populate Smith grid caches for the selected global domain.
 * Returns for missing/logarithmic axes. Requires prepared bounds, validated
 * tick arrays and empty caches. Circle and reactive allocations are independent
 * and can produce partial geometry on failure. Does not draw or free old data.
 *
 * @param[in] graphPtr Borrowed mapped graph with validated global Smith tick lists and domain settings.
 * @param[in,out] gridPtr Grid with empty x/y caches; receives independently allocated circle and
 *    reactive geometry.
 */
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

/**
 * @ingroup polar_labels
 * @brief Export Smith real labels through a temporary output renderer.
 * Uses the selected domain and global real major ticks. Shares normalized
 * conversion, formatting and domain-dependent offsets with screen drawing.
 * Borrows the export context; ends the renderer on every return. Requires
 * showTicks on the relevant axis and copies its style with rotation zero.
 * Retains fractional coordinates rather than rounding as on screen. May
 * evaluate Tcl; formatter invalidation stops traversal before cached resources are reused. Export errors remain owned by the export context.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in,out] exportPtr Borrowed initialized document context; outer caller owns clipping, error
 *    handling and finalization.
 * @param[in] gridPtr Borrowed prepared grid/axis resources; no cached resource may be reused after
 *    formatter cancellation.
 * @param[in] admittance Nonzero selects admittance orientation; zero selects impedance.
 */
static void SmithRealLabelsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Grid *gridPtr, int admittance) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.x;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        Rbc_RenderEnd(output);
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->gridPtr->nSmithRealMajorTicks; i++) {
        double value;
        double real;
        double imag;
        Point2D point;
        Tcl_DString label;
        char *string;

        value = graphPtr->gridPtr->smithRealMajorTicks[i];
        SmithGridValueToGamma(admittance, value, 0.0, &real, &imag);
        point = Rbc_Map2D(graphPtr, real, imag, &gridPtr->axes);
        if ((!FINITE(point.x)) || (!FINITE(point.y))) {
            continue;
        }
        string = FormatSmithLabel(graphPtr, graphPtr->gridPtr->smithRealCommandObjPtr, admittance, FALSE, value, &label);
        if (string == NULL) {
            Tcl_DStringFree(&label);
            break;
        }
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
                Rbc_RenderText(output, string, &style, point.x - SMITH_RESISTANCE_LABEL_OFFSET, point.y);
            } else {
                /*
                 * r=0 -> Gamma=-1.
                 */
                style.anchor = TK_ANCHOR_W;
                Rbc_RenderText(output, string, &style, point.x + SMITH_RESISTANCE_LABEL_OFFSET, point.y);
            }
        } else if (admittance) {
            style.anchor = TK_ANCHOR_N;
            Rbc_RenderText(output, string, &style, point.x, point.y + SMITH_RESISTANCE_LABEL_OFFSET);
        } else {
            style.anchor = TK_ANCHOR_S;
            Rbc_RenderText(output, string, &style, point.x, point.y - SMITH_RESISTANCE_LABEL_OFFSET);
        }
        Tcl_DStringFree(&label);
    }
    Rbc_RenderEnd(output);
}

/**
 * @ingroup polar_labels
 * @brief Export Smith reactive labels through a temporary output renderer.
 * Formats both signs of global major reactive magnitudes and uses the same
 * visible contour placement as screen drawing.
 * Borrows the export context; ends the renderer on every return. Requires
 * showTicks on the relevant axis and copies its style with rotation zero.
 * Retains fractional coordinates rather than rounding as on screen. May
 * evaluate Tcl; formatter invalidation stops traversal before cached resources are reused. Export errors remain owned by the export context.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in,out] exportPtr Borrowed initialized document context; outer caller owns clipping, error
 *    handling and finalization.
 * @param[in] gridPtr Borrowed prepared grid/axis resources; no cached resource may be reused after
 *    formatter cancellation.
 * @param[in] admittance Nonzero selects admittance orientation; zero selects impedance.
 */
static void SmithReactiveLabelsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr, Grid *gridPtr, int admittance) {
    Rbc_RenderContext *output = Rbc_RenderBeginExportOutput(exportPtr);
    Axis *axisPtr;
    TextStyle style;
    Tcl_Size i;

    axisPtr = gridPtr->axes.y;
    if ((axisPtr == NULL) || (!axisPtr->showTicks)) {
        Rbc_RenderEnd(output);
        return;
    }
    style = axisPtr->tickTextStyle;
    style.theta = 0.0;
    for (i = 0; i < graphPtr->gridPtr->nSmithImagMajorTicks; i++) {
        double magnitude;
        int sign;

        magnitude = graphPtr->gridPtr->smithImagMajorTicks[i];
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
            string = FormatSmithLabel(graphPtr, graphPtr->gridPtr->smithImagCommandObjPtr, admittance, TRUE, reactive, &label);
            if (string == NULL) {
                Tcl_DStringFree(&label);
                break;
            }
            if (string[0] != '\0') {
                Rbc_RenderText(output, string, &style, point.x, point.y);
            }
            Tcl_DStringFree(&label);
        }
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            break;
        }
    }
    Rbc_RenderEnd(output);
}

/**
 * @ingroup polar_labels
 * @brief Export the visible global Smith grid labels.
 * Returns when the global grid is missing or hidden. Borrows current mapped
 * graph resources and the export context. Does not map segments.
 * Dispatches real then reactive labels for each selected Smith domain.
 * Checks callback validity between phases and domains; inner loops stop on
 * cancellation. The enclosing caller retains graph record lifetime.
 *
 * @param[in,out] graphPtr Caller-protected graph with prepared label geometry; formatter callbacks can
 *    invalidate the pass.
 * @param[in,out] exportPtr Borrowed initialized document context; outer caller owns clipping, error
 *    handling and finalization.
 */
void Rbc_SmithLabelsExport(Graph *graphPtr, Rbc_ExportContext *exportPtr) {
    Grid *gridPtr;

    gridPtr = graphPtr->gridPtr;
    if ((gridPtr == NULL) || gridPtr->hidden) {
        return;
    }
    if ((graphPtr->gridPtr->smithGrid == SMITH_GRID_IMPEDANCE) || (graphPtr->gridPtr->smithGrid == SMITH_GRID_BOTH)) {
        SmithRealLabelsExport(graphPtr, exportPtr, gridPtr, FALSE);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return;
        }
        SmithReactiveLabelsExport(graphPtr, exportPtr, gridPtr, FALSE);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return;
        }
    }
    if ((graphPtr->gridPtr->smithGrid == SMITH_GRID_ADMITTANCE) || (graphPtr->gridPtr->smithGrid == SMITH_GRID_BOTH)) {
        SmithRealLabelsExport(graphPtr, exportPtr, gridPtr, TRUE);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return;
        }
        SmithReactiveLabelsExport(graphPtr, exportPtr, gridPtr, TRUE);
        if (!Rbc_GraphCallbackValid(graphPtr)) {
            return;
        }
    }
}
