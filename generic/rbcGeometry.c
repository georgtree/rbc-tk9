/*
 * rbcGeometry.c --
 *
 *      Point/segment/polygon geometry, clipping and line simplification.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 * See "license.terms" for details.
 */

#include "rbcInt.h"
#include <math.h>

static int IsFinitePoint(const Point2D *pointPtr) {
    return ((pointPtr != NULL) && FINITE(pointPtr->x) && FINITE(pointPtr->y));
}

static int IsValidExtents(const Extents2D *extsPtr) {
    return ((extsPtr != NULL) && FINITE(extsPtr->left) && FINITE(extsPtr->right) && FINITE(extsPtr->top) &&
            FINITE(extsPtr->bottom) && (extsPtr->left <= extsPtr->right) && (extsPtr->top <= extsPtr->bottom));
}

double Rbc_GetClosestPointOnSegment(const Point2D *samplePtr, const Point2D *p, const Point2D *q, Point2D *closestPtr) {
    double dx, dy;
    double sx, sy;
    double length;
    double ux, uy;
    double along;

    if ((!IsFinitePoint(samplePtr)) || (!IsFinitePoint(p)) || (!IsFinitePoint(q)) || (closestPtr == NULL)) {
        return DBL_MAX;
    }
    dx = q->x - p->x;
    dy = q->y - p->y;
    if ((!FINITE(dx)) || (!FINITE(dy))) {
        return DBL_MAX;
    }
    length = hypot(dx, dy);
    if (!FINITE(length)) {
        return DBL_MAX;
    }
    if (length <= DBL_EPSILON) {
        *closestPtr = *p;
        return hypot(samplePtr->x - p->x, samplePtr->y - p->y);
    }
    sx = samplePtr->x - p->x;
    sy = samplePtr->y - p->y;
    if ((!FINITE(sx)) || (!FINITE(sy))) {
        return DBL_MAX;
    }
    ux = dx / length;
    uy = dy / length;
    along = (sx * ux) + (sy * uy);
    if (!FINITE(along)) {
        return DBL_MAX;
    }
    if (along <= 0.0) {
        *closestPtr = *p;
    } else if (along >= length) {
        *closestPtr = *q;
    } else {
        closestPtr->x = p->x + (along * ux);
        closestPtr->y = p->y + (along * uy);
    }
    return hypot(samplePtr->x - closestPtr->x, samplePtr->y - closestPtr->y);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PointInSegments --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Point2D *samplePtr
 *      Segment2D *segments
 *      int nSegments
 *      double halo
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_PointInSegments(const Point2D *samplePtr, const Segment2D *segments, Tcl_Size nSegments, double halo) {
    const Segment2D *segPtr;
    const Segment2D *endPtr;

    if ((!IsFinitePoint(samplePtr)) || (segments == NULL) || (nSegments <= 0) || (!FINITE(halo)) || (halo <= 0.0)) {
        return FALSE;
    }
    endPtr = segments + nSegments;
    for (segPtr = segments; segPtr < endPtr; segPtr++) {
        Point2D closest;
        double dist;

        dist = Rbc_GetClosestPointOnSegment(samplePtr, &segPtr->p, &segPtr->q, &closest);
        if (dist < halo) {
            return TRUE;
        }
    }
    return FALSE;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PointInPolygon --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Point2D *samplePtr
 *      Point2D *points
 *      int nPoints
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_PointInPolygon(const Point2D *samplePtr, const Point2D *points, Tcl_Size nPoints) {
    const Point2D *p;
    const Point2D *q;
    const Point2D *endPtr;
    int inside;

    if ((!IsFinitePoint(samplePtr)) || (points == NULL) || (nPoints < 3)) {
        return FALSE;
    }
    p = points + nPoints - 1;
    if (!IsFinitePoint(p)) {
        return FALSE;
    }
    endPtr = points + nPoints;
    inside = FALSE;
    for (q = points; q < endPtr; p = q, q++) {
        double xIntersection;

        if (!IsFinitePoint(q)) {
            return FALSE;
        }
        if (((p->y <= samplePtr->y) && (samplePtr->y < q->y)) || ((q->y <= samplePtr->y) && (samplePtr->y < p->y))) {
            double dx;
            double dy;
            double fraction;

            dx = q->x - p->x;
            dy = q->y - p->y;
            if ((!FINITE(dx)) || (!FINITE(dy)) || (dy == 0.0)) {
                return FALSE;
            }
            fraction = (samplePtr->y - p->y) / dy;
            xIntersection = p->x + (fraction * dx);
            if (!FINITE(xIntersection)) {
                return FALSE;
            }
            if (samplePtr->x < xIntersection) {
                inside = !inside;
            }
        }
    }
    return inside;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_RegionInPolygon --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Extents2D *extsPtr
 *      Point2D *points
 *      int nPoints
 *      int enclosed
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_RegionInPolygon(const Extents2D *extsPtr, const Point2D *points, Tcl_Size nPoints, int enclosed) {
    const Point2D *pointPtr;
    const Point2D *endPtr;

    if ((!IsValidExtents(extsPtr)) || (points == NULL) || (nPoints < 3)) {
        return FALSE;
    }
    endPtr = points + nPoints;
    /*
     * Reject malformed polygons before doing any geometric test.
     */
    for (pointPtr = points; pointPtr < endPtr; pointPtr++) {
        if (!IsFinitePoint(pointPtr)) {
            return FALSE;
        }
    }
    if (enclosed) {
        /*
         * Every polygon vertex must be inside the region.
         */
        for (pointPtr = points; pointPtr < endPtr; pointPtr++) {

            if ((pointPtr->x < extsPtr->left) || (pointPtr->x > extsPtr->right) || (pointPtr->y < extsPtr->top) ||
                (pointPtr->y > extsPtr->bottom)) {
                return FALSE;
            }
        }
        return TRUE;
    } else {
        const Point2D *pPtr;
        const Point2D *qPtr;
        Point2D sample;

        /*
         * Test every polygon edge, including the closing edge.
         */
        pPtr = points + nPoints - 1;
        for (qPtr = points; qPtr < endPtr; pPtr = qPtr, qPtr++) {
            if (Rbc_LineRectClip(extsPtr, pPtr, qPtr, NULL)) {
                return TRUE;
            }
        }
        /*
         * No polygon edge crossed the rectangle.  The rectangle
         * may still lie completely inside the polygon.
         */
        sample.x = extsPtr->left;
        sample.y = extsPtr->top;
        return Rbc_PointInPolygon(&sample, points, nPoints);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ClipTest --
 *
 *      TODO: Description
 *
 * Parameters:
 *      double ds
 *      double dr
 *      double *t1
 *      double *t2
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int ClipTest(double ds, double dr, double *t1, double *t2) {
    double t;

    if (ds < 0.0) {
        t = dr / ds;
        if (t > *t2) {
            return FALSE;
        }
        if (t > *t1) {
            *t1 = t;
        }
    } else if (ds > 0.0) {
        t = dr / ds;
        if (t < *t1) {
            return FALSE;
        }
        if (t < *t2) {
            *t2 = t;
        }
    } else {
        /* d = 0, so line is parallel to this clipping edge */
        if (dr < 0.0) { /* Line is outside clipping edge */
            return FALSE;
        }
    }
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_LineRectClip --
 *
 *      Clips a line segment to a rectangular region using the
 *      Liang-Barsky line clipping algorithm.
 *
 * Parameters:
 *      const Extents2D *extsPtr
 *          Rectangular clipping region.
 *
 *      const Point2D *p
 *      const Point2D *q
 *          Original line segment endpoints.
 *
 *      Segment2D *clippedPtr
 *          Receives the clipped line segment when non-NULL and the
 *          segment intersects the clipping region.  May be NULL when
 *          only visibility is required.
 *
 * Results:
 *      Returns TRUE if any portion of the line segment lies within the
 *      clipping region, FALSE otherwise.
 *
 * Side Effects:
 *      Writes the clipped segment to clippedPtr on success when
 *      clippedPtr is non-NULL.  The input endpoints are never modified.
 *
 *----------------------------------------------------------------------
 */
int Rbc_LineRectClip(const Extents2D *extsPtr, const Point2D *p, const Point2D *q, Segment2D *clippedPtr) {
    double dx;
    double dy;
    double t1;
    double t2;

    if ((!IsValidExtents(extsPtr)) || (!IsFinitePoint(p)) || (!IsFinitePoint(q))) {
        return FALSE;
    }
    t1 = 0.0;
    t2 = 1.0;
    dx = q->x - p->x;
    dy = q->y - p->y;
    if ((!FINITE(dx)) || (!FINITE(dy))) {
        return FALSE;
    }
    if ((!ClipTest(-dx, p->x - extsPtr->left, &t1, &t2)) || (!ClipTest(dx, extsPtr->right - p->x, &t1, &t2)) ||
        (!ClipTest(-dy, p->y - extsPtr->top, &t1, &t2)) || (!ClipTest(dy, extsPtr->bottom - p->y, &t1, &t2))) {
        return FALSE;
    }
    if (clippedPtr != NULL) {
        clippedPtr->p.x = p->x + (t1 * dx);
        clippedPtr->p.y = p->y + (t1 * dy);
        clippedPtr->q.x = p->x + (t2 * dx);
        clippedPtr->q.y = p->y + (t2 * dy);
    }
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_PolyRectClip --
 *
 *      Clips the given polygon to a rectangular region.  The resulting
 *      polygon is returned. Note that the resulting polyon may be
 *      complex, connected by zero width/height segments.  The drawing
 *      routine (such as XFillPolygon) will not draw a connecting
 *      segment.
 *      inputPts is read-only and contains exactly nInputPts vertices.
 *      The closing edge from the last vertex to the first is generated
 *      internally.
 *
 *      outputCapacity is the number of Point2D entries available in
 *      outputPts.
 *
 *      Reference:  Liang-Barsky Polygon Clipping Algorithm
 *
 * Parameters:
 *      Extents2D *extsPtr
 *      Point2D *points
 *      int nPoints
 *      Point2D *clipPts
 *
 * Results:
 *      Returns the number of points in the clipped polygon. The
 *      points of the clipped polygon are stored in *outputPts*.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int AppendClipVertex(Point2D *points, Tcl_Size capacity, Tcl_Size *countPtr, double x, double y) {
    Tcl_Size count;

    count = *countPtr;
    if ((points == NULL) || (count < 0) || (count >= capacity)) {
        return FALSE;
    }
    points[count].x = x;
    points[count].y = y;
    *countPtr = count + 1;
    return TRUE;
}

Tcl_Size Rbc_PolyRectClip(const Extents2D *extsPtr, const Point2D *points, Tcl_Size nPoints, Point2D *clipPts,
                          Tcl_Size clipCapacity) {
    double dx, dy;
    double tin1, tin2;
    double tinx, tiny;
    double xin, yin, xout, yout;
    Tcl_Size count;
    Tcl_Size i;

    if ((!IsValidExtents(extsPtr)) || (points == NULL) || (clipPts == NULL) || (nPoints <= 0) || (clipCapacity <= 0)) {
        return 0;
    }
    for (i = 0; i < nPoints; i++) {
        if (!IsFinitePoint(points + i)) {
            return 0;
        }
    }
    count = 0;
    for (i = 0; i < nPoints; i++) {
        const Point2D *p;
        const Point2D *q;

        p = points + i;
        q = (i + 1 < nPoints) ? (points + i + 1) : points;
        dx = q->x - p->x;
        dy = q->y - p->y;
        if (dx == 0.0) {
            dx = (p->x > extsPtr->left) ? -DBL_EPSILON : DBL_EPSILON;
        }
        if (dy == 0.0) {
            dy = (p->y > extsPtr->top) ? -DBL_EPSILON : DBL_EPSILON;
        }
        if (dx > 0.0) {
            xin = extsPtr->left;
            xout = extsPtr->right + 1.0;
        } else {
            xin = extsPtr->right + 1.0;
            xout = extsPtr->left;
        }
        if (dy > 0.0) {
            yin = extsPtr->top;
            yout = extsPtr->bottom + 1.0;
        } else {
            yin = extsPtr->bottom + 1.0;
            yout = extsPtr->top;
        }
        tinx = (xin - p->x) / dx;
        tiny = (yin - p->y) / dy;
        if (tinx < tiny) {
            tin1 = tinx;
            tin2 = tiny;
        } else {
            tin1 = tiny;
            tin2 = tinx;
        }
        if (tin1 <= 1.0) {
            if (tin1 > 0.0) {
                if (!AppendClipVertex(clipPts, clipCapacity, &count, xin, yin)) {
                    return 0;
                }
            }
            if (tin2 <= 1.0) {
                double toutx, touty, tout1;

                toutx = (xout - p->x) / dx;
                touty = (yout - p->y) / dy;
                tout1 = MIN(toutx, touty);
                if ((tin2 > 0.0) || (tout1 > 0.0)) {
                    if (tin2 <= tout1) {
                        if (tin2 > 0.0) {
                            if (tinx > tiny) {
                                if (!AppendClipVertex(clipPts, clipCapacity, &count, xin, p->y + tinx * dy)) {
                                    return 0;
                                }
                            } else {
                                if (!AppendClipVertex(clipPts, clipCapacity, &count, p->x + tiny * dx, yin)) {
                                    return 0;
                                }
                            }
                        }
                        if (tout1 < 1.0) {
                            if (toutx < touty) {
                                if (!AppendClipVertex(clipPts, clipCapacity, &count, xout, p->y + toutx * dy)) {
                                    return 0;
                                }
                            } else {
                                if (!AppendClipVertex(clipPts, clipCapacity, &count, p->x + touty * dx, yout)) {
                                    return 0;
                                }
                            }
                        } else {
                            if (!AppendClipVertex(clipPts, clipCapacity, &count, q->x, q->y)) {
                                return 0;
                            }
                        }
                    } else {
                        if (tinx > tiny) {
                            if (!AppendClipVertex(clipPts, clipCapacity, &count, xin, yout)) {
                                return 0;
                            }
                        } else {
                            if (!AppendClipVertex(clipPts, clipCapacity, &count, xout, yin)) {
                                return 0;
                            }
                        }
                    }
                }
            }
        }
    }
    /*
     * Preserve the historical explicitly closed output polygon.
     */
    if (count > 0) {
        if (!AppendClipVertex(clipPts, clipCapacity, &count, clipPts[0].x, clipPts[0].y)) {
            return 0;
        }
    }
    return count;
}

/*
 *----------------------------------------------------------------------
 *
 * FindSplit --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Point2D points[]
 *      int i - Indices specifying the range of points.
 *      int j - Indices specifying the range of points.
 *      int *split - (out) Index of next split.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static double FindSplit(const Point2D points[], Tcl_Size i, Tcl_Size j, Tcl_Size *split) {
    Tcl_Size k;
    double dx, dy;
    double length;
    double maxSqDist;

    if (split == NULL) {
        return -1.0;
    }
    *split = -1;
    if ((points == NULL) || (i < 0) || (j <= i) || ((j - i) <= 1)) {
        return -1.0;
    }
    dx = points[j].x - points[i].x;
    dy = points[j].y - points[i].y;
    length = hypot(dx, dy);
    maxSqDist = -1.0;
    if (length > DBL_EPSILON) {
        /*
         * Compute the perpendicular distance from each intermediate
         * point to the infinite line through the two endpoints.
         *
         * The segment length is common to every candidate, but doing
         * the normalization here avoids the old division-by-zero case
         * when the endpoints coincide.
         */
        for (k = i + 1; k < j; k++) {
            double cross;
            double dist;
            double sqDist;

            cross = ((points[k].x - points[i].x) * dy) - ((points[k].y - points[i].y) * dx);
            dist = fabs(cross) / length;
            sqDist = dist * dist;
            if (sqDist > maxSqDist) {
                maxSqDist = sqDist;
                *split = k;
            }
        }
    } else {
        /*
         * Coincident endpoints do not define a unique line.  Measure
         * each intermediate point from the common endpoint instead.
         * This preserves excursions that return to their starting
         * point.
         */
        for (k = i + 1; k < j; k++) {
            double dist;
            double sqDist;

            dist = hypot(points[k].x - points[i].x, points[k].y - points[i].y);
            sqDist = dist * dist;
            if (sqDist > maxSqDist) {
                maxSqDist = sqDist;
                *split = k;
            }
        }
    }
    return maxSqDist;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_SimplifyLine --
 *
 *      Douglas-Peucker line simplification algorithm
 *
 * Parameters:
 *      Point2D inputPts[]
 *      Tcl_Size low
 *      Tcl_Size high
 *      double tolerance
 *      Tcl_Size indices[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Tcl_Size Rbc_SimplifyLine(const Point2D inputPts[], Tcl_Size low, Tcl_Size high, double tolerance, Tcl_Size indices[]) {
    Tcl_Size *stack;
    Tcl_Size nPoints;
    Tcl_Size top;
    Tcl_Size count;
    double sqTolerance;

    if ((inputPts == NULL) || (indices == NULL) || (low < 0) || (high < low)) {
        return 0;
    }
    /*
     * A one-point line is already fully simplified.
     */
    if (low == high) {
        indices[0] = low;
        return 1;
    }
    /*
     * high - low is safe because both are non-negative and high >= low.
     * Guard the final +1 separately.
     */
    if ((high - low) == TCL_SIZE_MAX) {
        return 0;
    }
    nPoints = (high - low) + 1;
    if ((size_t)nPoints > SIZE_MAX / sizeof(*stack)) {
        return 0;
    }
    stack = Tcl_AttemptAlloc((size_t)nPoints * sizeof(*stack));
    if (stack == NULL) {
        return 0;
    }
    /*
     * The first point is always retained.  Stack entries represent
     * right-hand endpoints of intervals still to process.
     */
    indices[0] = low;
    count = 1;
    top = 0;
    stack[0] = high;
    sqTolerance = tolerance * tolerance;
    while (top >= 0) {
        Tcl_Size end;
        Tcl_Size split;
        double sqDist;

        end = stack[top];
        split = -1;
        sqDist = FindSplit(inputPts, low, end, &split);
        if ((split >= 0) && (sqDist > sqTolerance)) {
            /*
             * Process [low, split] first while retaining "end" below
             * it on the stack.
             */
            if ((top + 1) >= nPoints) {
                ckfree(stack);
                return 0;
            }
            stack[++top] = split;
        } else {
            /*
             * This interval needs no further split.  Its right endpoint
             * becomes the next retained point.
             */
            if (count >= nPoints) {
                ckfree(stack);
                return 0;
            }
            indices[count++] = end;
            low = end;
            top--;
        }
    }
    ckfree(stack);
    return count;
}
