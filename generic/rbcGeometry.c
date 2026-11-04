/*
 * rbcGeometry.c --
 *
 *      Point/segment/polygon geometry, clipping and line simplification.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 * See "license.terms" for details.
 */

/** @file rbcGeometry.c
 * Geometry validation and subdivision.
 */
/** @defgroup geometry_internals Geometry validation and subdivision
 * Private helpers; callers retain storage and lifetime responsibilities.
 */
#include "rbcInt.h"
#include <math.h>

/**
 * @brief Test a point pointer and both coordinates.
 * @param[in] pointPtr Borrowed point to test; NULL is accepted as an invalid input.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup geometry_internals
 *
 * Returns FALSE for NULL or either non-finite coordinate; otherwise TRUE.
 * No allocation, mutation or coordinate transformation.
 */
static int IsFinitePoint(const Point2D *pointPtr) {
    return ((pointPtr != NULL) && FINITE(pointPtr->x) && FINITE(pointPtr->y));
}

/**
 * @brief Test finite ordered rectangular extents.
 * @param[in] extsPtr Borrowed rectangle to test; NULL, reversed or nonfinite bounds are rejected.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup geometry_internals
 *
 * Requires left <= right and top <= bottom after checking all coordinates.
 * NULL or non-finite bounds return FALSE; zero width/height is accepted. Does
 * not normalize reversed bounds or apply pixel-area expansion.
 */
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

/**
 * @brief Intersect a Liang-Barsky parameter interval with one boundary.
 * @param[in] ds Signed segment-direction coefficient for one clipping boundary.
 * @param[in] dr Signed boundary distance in the same coordinate units as ds.
 * @param[in,out] t1 Required initialized entry fraction, normally zero before the first boundary.
 * @param[in,out] t2 Required initialized exit fraction, normally one; this call changes neither fraction on
 *     rejection.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup geometry_internals
 *
 * Caller supplies initialized t1/t2, normally 0/1. Negative ds can raise the
 * entry parameter; positive ds can lower the exit parameter. A parallel edge
 * (ds zero) rejects only when dr is negative. Returns boolean; this call leaves
 * parameters untouched when it rejects, but earlier successful boundary calls
 * may already have narrowed them. No allocation or independent finite checks.
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

/**
 * @brief Append one vertex when the caller-provided buffer has room.
 * @param[out] points Caller-owned output buffer; only the entry at the old count is written on success.
 * @param[in] capacity Number of available Point2D slots, not bytes.
 * @param[in,out] countPtr Required current vertex count; incremented once on success and retained on failure.
 * @param[in] x X coordinate to append without finite validation.
 * @param[in] y Y coordinate to append without finite validation.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup geometry_internals
 *
 * Requires a valid countPtr; rejects NULL points, negative count or count at
 * or above capacity without changing storage/count. On success writes X/Y and
 * increments count. Does not test coordinate finiteness or suppress duplicates.
 * The clipper must discard the whole polygon on a failed append.
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

/**
 * @brief Find the first farthest interior point for line simplification.
 * @param[in] points Borrowed source array accessible through j; caller validates finite coordinates and
 *     capacity.
 * @param[in] i Original index of the left endpoint, nonnegative.
 * @param[in] j Original index of the right endpoint; a split requires at least one interior point.
 * @param[out] split Original index of the first farthest interior point; initialized to -1. NULL causes a -1
 *     distance result.
 * @see @ref rbc_calling_conventions
 *
 * @ingroup geometry_internals
 *
 * Requires valid source indices and caller storage for split. Returns maximum
 * squared distance and writes its original source index; ties keep the first.
 * Uses distance to the infinite endpoint line, not the bounded segment. If the
 * endpoint distance is at most DBL_EPSILON, measures from the first endpoint.
 *
 * NULL split returns -1. Otherwise initializes split to -1, also returning -1
 * for invalid pointers/ranges or no interior point. Does not know array capacity
 * or validate every coordinate; extreme intermediate arithmetic may overflow.
 * No allocation or input mutation.
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
