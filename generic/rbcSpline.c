/*
 * rbcSpline.c --
 *
 *      TODO: Description
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include <stdint.h>
#include "rbcInt.h"

typedef int (*Rbc_SplineOp)(const Point2D *, Tcl_Size, Point2D *, Tcl_Size);

typedef struct {
    Rbc_OpSpecHeader header;
    Rbc_SplineOp proc;
} SplineOpSpec;

typedef double TriDiagonalMatrix[3];

typedef struct {
    double b, c, d;
} Cubic2D;

typedef struct {
    double b, c, d, e, f;
} Quint2D;

typedef struct {
    double t; /* Arc length of interval. */
    double x; /* 2nd derivative of X with respect to T */
    double y; /* 2nd derivative of Y with respect to T */
} CubicSpline;

#define SQR(x) ((x) * (x))

/*
 * Quadratic spline parameters
 */
#define QUAD_PARAM_COUNT 10
#define E1 param[0]
#define E2 param[1]
#define V1 param[2]
#define V2 param[3]
#define W1 param[4]
#define W2 param[5]
#define Z1 param[6]
#define Z2 param[7]
#define Y1 param[8]
#define Y2 param[9]

static Tcl_ObjCmdProc2 SplineObjCmd;
static Tcl_Size Search(const Point2D points[], Tcl_Size nPoints, double key, int *foundPtr);
static int QuadChoose(const Point2D *p, const Point2D *q, double m1, double m2, double epsilon);
static void QuadCases(const Point2D *p, const Point2D *q, double m1, double m2, double param[], int which);
static int QuadSelect(const Point2D *p, const Point2D *q, double m1, double m2, double epsilon, double param[]);
INLINE static double QuadGetImage(double p1, double p2, double p3, double x1, double x2, double x3);
static void QuadSpline(Point2D *intp, const Point2D *left, const Point2D *right, const double param[], int ncase);
static void QuadSlopes(const Point2D points[], double *m, Tcl_Size nPoints);
static int QuadEval(const Point2D origPts[], Tcl_Size nOrigPts, Point2D intpPts[], Tcl_Size nIntpPts, const double *m,
                    double epsilon);
static int SolveCubic1(TriDiagonalMatrix A[], Tcl_Size n);
static void SolveCubic2(const TriDiagonalMatrix A[], CubicSpline spline[], Tcl_Size nIntervals);
static CubicSpline *CubicSlopes(const Point2D points[], Tcl_Size nPoints, int isClosed, double unitX, double unitY);
static Tcl_Size CubicEval(const Point2D origPts[], Tcl_Size nOrigPts, Point2D intpPts[], Tcl_Size nIntpPts,
                          const CubicSpline spline[]);
static void CatromCoeffs(const Point2D *p, Point2D *a, Point2D *b, Point2D *c, Point2D *d);


static int GetSplineArrayByteCount(Tcl_Size count, size_t elementSize, size_t *byteCountPtr) {
    if ((count < 0) || (elementSize == 0) || ((Tcl_WideUInt)count > (Tcl_WideUInt)(SIZE_MAX / elementSize))) {
        return TCL_ERROR;
    }
    *byteCountPtr = (size_t)count * elementSize;
    return TCL_OK;
}

static int SplinePointsAreFinite(const Point2D *points, Tcl_Size nPoints) {
    Tcl_Size i;

    if ((points == NULL) || (nPoints < 0)) {
        return FALSE;
    }
    for (i = 0; i < nPoints; i++) {
        if (!FINITE(points[i].x) || !FINITE(points[i].y)) {
            return FALSE;
        }
    }
    return TRUE;
}

static int SplinePointsHaveIncreasingX(const Point2D *points, Tcl_Size nPoints) {
    Tcl_Size i;

    if ((points == NULL) || (nPoints < 2)) {
        return FALSE;
    }
    if (!SplinePointsAreFinite(points, nPoints)) {
        return FALSE;
    }
    for (i = 1; i < nPoints; i++) {
        if (!(points[i].x > points[i - 1].x)) {
            return FALSE;
        }
    }
    return TRUE;
}

static int SplineEvaluationPointsAreFinite(const Point2D *points, Tcl_Size nPoints) {
    Tcl_Size i;

    if ((points == NULL) || (nPoints < 0)) {
        return FALSE;
    }
    for (i = 0; i < nPoints; i++) {
        if (!FINITE(points[i].x)) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * -----------------------------------------------------------------------
 *
 * Search --
 *
 *      Conducts a binary search for a value.  This routine is called
 *      only if key is between x(0) and x(len - 1).
 *
 * Parameters:
 *      Point2D points[] - Contains the abscissas of the data points of interpolation.
 *      int nPoints - Dimension of x.
 *      double key - Value whose relative position in x is to be located.
 *      int *foundPtr - (out) Returns 1 if s is found in x and 0 otherwise.
 *
 * Results:
 *      Returns the index of key when it is found.  Otherwise,
 *      returns the insertion position: the index of the smallest
 *      source value greater than key.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static Tcl_Size Search(const Point2D points[], Tcl_Size nPoints, double key, int *foundPtr) {
    Tcl_Size high;
    Tcl_Size low;
    Tcl_Size mid;

    low = 0;
    high = nPoints - 1;
    while (high >= low) {
        /*
         * Avoid overflow from (high + low) / 2.
         */
        mid = low + ((high - low) / 2);
        if (key > points[mid].x) {
            low = mid + 1;
        } else if (key < points[mid].x) {
            high = mid - 1;
        } else {
            *foundPtr = TRUE;
            return mid;
        }
    }
    *foundPtr = FALSE;
    return low;
}

/*
 *-----------------------------------------------------------------------
 *
 * QuadChoose --
 *
 *      Determines the case needed for the computation of the parame-
 *      ters of the quadratic spline.
 *
 * Parameters:
 *      Point2D *p - Coordinates of one of the points of interpolation
 *      Point2D *q - Coordinates of one of the points of interpolation
 *      double m1 - Derivative condition at point P
 *      double m2 - Derivative condition at point Q
 *      double epsilon - Error tolerance used to distinguish cases when m1 or m2 is relatively close to the slope or 
 *                       twice the slope of the line segment joining the points P and Q. If epsilon is not 0.0, then 
 *                       epsilon should be greater than or equal to machine epsilon.
 *
 * Results:
 *      Returns a case number (1-4) which controls how the parameters
 *      of the quadratic spline are evaluated.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *-----------------------------------------------------------------------
 */
static int QuadChoose(const Point2D *p, const Point2D *q, double m1, double m2, double epsilon) {
    double slope;

    /* Calculate the slope of the line joining P and Q. */
    slope = (q->y - p->y) / (q->x - p->x);
    if (slope != 0.0) {
        double relerr;
        double mref, mref1, mref2, prod1, prod2;

        prod1 = slope * m1;
        prod2 = slope * m2;
        /* Find the absolute values of the slopes slope, m1, and m2. */
        mref = FABS(slope);
        mref1 = FABS(m1);
        mref2 = FABS(m2);
        /*
         * If the relative deviation of m1 or m2 from slope is less than
         * epsilon, then choose case 2 or case 3.
         */
        relerr = epsilon * mref;
        if ((FABS(slope - m1) > relerr) && (FABS(slope - m2) > relerr) && (prod1 >= 0.0) && (prod2 >= 0.0)) {
            double prod;

            prod = (mref - mref1) * (mref - mref2);
            if (prod < 0.0) {
                /*
                 * l1, the line through (x1,y1) with slope m1, and l2,
                 * the line through (x2,y2) with slope m2, intersect
                 * at a point whose abscissa is between x1 and x2.
                 * The abscissa becomes a knot of the spline.
                 */
                return 1;
            }
            if (mref1 > (mref * 2.0)) {
                if (mref2 <= ((2.0 - epsilon) * mref)) {
                    return 3;
                }
            } else if (mref2 <= (mref * 2.0)) {
                /*
                 * Both l1 and l2 cross the line through
                 * (x1+x2)/2.0,y1 and (x1+x2)/2.0,y2, which is the
                 * midline of the rectangle formed by P and Q or both
                 * m1 and m2 have signs different than the sign of
                 * slope, or one of m1 and m2 has opposite sign from
                 * slope and l1 and l2 intersect to the left of x1 or
                 * to the right of x2.  The point (x1+x2)/2. is a knot
                 * of the spline.
                 */
                return 2;
            } else if (mref1 <= ((2.0 - epsilon) * mref)) {
                /*
                 * In cases 3 and 4, sign(m1)=sign(m2)=sign(slope).
                 * Either l1 or l2 crosses the midline, but not both.
                 * Choose case 4 if mref1 is greater than
                 * (2.-epsilon)*mref; otherwise, choose case 3.
                 */
                return 3;
            }
            /*
             * If neither l1 nor l2 crosses the midline, the spline
             * requires two knots between x1 and x2.
             */
            return 4;
        } else {
            /*
             * The sign of at least one of the slopes m1 or m2 does not
             * agree with the sign of *slope*.
             */
            if ((prod1 < 0.0) && (prod2 < 0.0)) {
                return 2;
            } else if (prod1 < 0.0) {
                if (mref2 > ((epsilon + 1.0) * mref)) {
                    return 1;
                } else {
                    return 2;
                }
            } else if (mref1 > ((epsilon + 1.0) * mref)) {
                return 1;
            } else {
                return 2;
            }
        }
    } else if ((m1 * m2) >= 0.0) {
        return 2;
    } else {
        return 1;
    }
}

/*
 * -----------------------------------------------------------------------
 *
 * QuadCases --
 *
 *       Computes the knots and other parameters of the spline on the
 *       interval PQ.
 *
 *
 *       On input--
 *
 *         P and Q    the coordinates of the points of interpolation.
 *         m1         the slope at P.
 *         m2         the slope at Q.
 *         ncase      controls the number and location of the knots.
 *
 *
 *      On output--
 *
 *        (v1,v2),(w1,w2),(z1,z2), (e1,e2)    the coordinates of the
 *                                            knots and other
 *                                            parameters of the spline
 *                                            on P.  (e1,e2) and Q
 *                                            are used only if ncase=4.
 *
 * Parameters:
 *      Point2D *p
 *      Point2D *q
 *      double m1
 *      double m2
 *      double param[]
 *      int which
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static void QuadCases(const Point2D *p, const Point2D *q, double m1, double m2, double param[], int which) {
    Tcl_Size i;

    /*
     * QuadSpline uses a case-dependent subset of these parameters.
     * Initialize the complete parameter set so that every call to
     * QuadCases produces a fully defined output array.
     */
    for (i = 0; i < QUAD_PARAM_COUNT; i++) {
        param[i] = 0.0;
    }
    if ((which == 3) || (which == 4)) {
        double mbar1, mbar2, mbar3, c1, d1, h1, j1, k1;

        c1 = p->x + (q->y - p->y) / m1;
        d1 = q->x + (p->y - q->y) / m2;
        h1 = c1 * 2.0 - p->x;
        j1 = d1 * 2.0 - q->x;
        mbar1 = (q->y - p->y) / (h1 - p->x);
        mbar2 = (p->y - q->y) / (j1 - q->x);
        if (which == 4) { /* Case 4. */
            Y1 = (p->x + c1) / 2.0;
            V1 = (p->x + Y1) / 2.0;
            V2 = m1 * (V1 - p->x) + p->y;
            Z1 = (d1 + q->x) / 2.0;
            W1 = (q->x + Z1) / 2.0;
            W2 = m2 * (W1 - q->x) + q->y;
            mbar3 = (W2 - V2) / (W1 - V1);
            Y2 = mbar3 * (Y1 - V1) + V2;
            Z2 = mbar3 * (Z1 - V1) + V2;
            E1 = (Y1 + Z1) / 2.0;
            E2 = mbar3 * (E1 - V1) + V2;
        } else { /* Case 3. */
            k1 = (p->y - q->y + q->x * mbar2 - p->x * mbar1) / (mbar2 - mbar1);
            if (FABS(m1) > FABS(m2)) {
                Z1 = (k1 + p->x) / 2.0;
            } else {
                Z1 = (k1 + q->x) / 2.0;
            }
            V1 = (p->x + Z1) / 2.0;
            V2 = p->y + m1 * (V1 - p->x);
            W1 = (q->x + Z1) / 2.0;
            W2 = q->y + m2 * (W1 - q->x);
            Z2 = V2 + (W2 - V2) / (W1 - V1) * (Z1 - V1);
        }
    } else if (which == 2) { /* Case 2. */
        Z1 = (p->x + q->x) / 2.0;
        V1 = (p->x + Z1) / 2.0;
        V2 = p->y + m1 * (V1 - p->x);
        W1 = (Z1 + q->x) / 2.0;
        W2 = q->y + m2 * (W1 - q->x);
        Z2 = (V2 + W2) / 2.0;
    } else { /* Case 1. */
        double ztwo;

        Z1 = (p->y - q->y + m2 * q->x - m1 * p->x) / (m2 - m1);
        ztwo = p->y + m1 * (Z1 - p->x);
        V1 = (p->x + Z1) / 2.0;
        V2 = (p->y + ztwo) / 2.0;
        W1 = (Z1 + q->x) / 2.0;
        W2 = (ztwo + q->y) / 2.0;
        Z2 = V2 + (W2 - V2) / (W1 - V1) * (Z1 - V1);
    }
}

/*
 *--------------------------------------------------------------
 *
 * QuadSelect --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Point2D *p
 *      Point2D *q
 *      double m1
 *      double m2
 *      double epsilon
 *      double param[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int QuadSelect(const Point2D *p, const Point2D *q, double m1, double m2, double epsilon, double param[]) {
    int ncase;

    ncase = QuadChoose(p, q, m1, m2, epsilon);
    QuadCases(p, q, m1, m2, param, ncase);
    return ncase;
}

/*
 *--------------------------------------------------------------
 *
 * QuadGetImage --
 *
 *      TODO: Description
 *
 * Parameters:
 *      double p1
 *      double p2
 *      double p3
 *      double x1
 *      double x2
 *      double x3
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
INLINE static double QuadGetImage(double p1, double p2, double p3, double x1, double x2, double x3) {
    double A, B, C;
    double y;

    A = x1 - x2;
    B = x2 - x3;
    C = x1 - x3;
    y = (p1 * (A * A) + p2 * 2.0 * B * A + p3 * (B * B)) / (C * C);
    return y;
}

/*
 * -----------------------------------------------------------------------
 *
 * QuadSpline --
 *
 *      Finds the image of a point in x.
 *
 *      On input
 *
 *        x                             Contains the value at which
 *                                      the spline is evaluated.
 *        leftX, leftY                  Coordinates of the left-hand
 *                                      data point used in the
 *                                      evaluation of x values.
 *        rightX, rightY                Coordinates of the right-hand
 *                                      data point used in the
 *                                      evaluation of x values.
 *        Z1, Z2, Y1, Y2, E2, W2, V2    Parameters of the spline.
 *        ncase                         Controls the evaluation of
 *                                      the spline by indicating
 *                                      whether one or two knots
 *                                      were placed in the interval
 *                                      (xtabs,xtabs1).
 *
 * Parameters:
 *      Point2D *intp - Value at which spline is evaluated 
 *      Point2D *left - Point to the left of the data point to be evaluated
 *      Point2D *right - Point to the right of the data point to be evaluated
 *      double param[] - Parameters of the spline
 *      int ncase - Controls the evaluation of the spline by indicating whether one or two knots were placed in the
 *                  interval (leftX,rightX)
 *
 * Results:
 *      The image of the spline at x.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static void QuadSpline(Point2D *intp, const Point2D *left, const Point2D *right, const double param[], int ncase) {
    double y;

    if (ncase == 4) {
        /*
         * Case 4:  More than one knot was placed in the interval.
         */
        /*
         * Determine the location of data point relative to the 1st knot.
         */
        if (Y1 > intp->x) {
            y = QuadGetImage(left->y, V2, Y2, Y1, intp->x, left->x);
        } else if (Y1 < intp->x) {
            /*
             * Determine the location of the data point relative to
             * the 2nd knot.
             */
            if (Z1 > intp->x) {
                y = QuadGetImage(Y2, E2, Z2, Z1, intp->x, Y1);
            } else if (Z1 < intp->x) {
                y = QuadGetImage(Z2, W2, right->y, right->x, intp->x, Z1);
            } else {
                y = Z2;
            }
        } else {
            y = Y2;
        }
    } else {
        /*
         * Cases 1, 2, or 3:
         *
         * Determine the location of the data point relative to the
         * knot.
         */
        if (Z1 < intp->x) {
            y = QuadGetImage(Z2, W2, right->y, right->x, intp->x, Z1);
        } else if (Z1 > intp->x) {
            y = QuadGetImage(left->y, V2, Z2, Z1, intp->x, left->x);
        } else {
            y = Z2;
        }
    }
    intp->y = y;
}

/*
 * -----------------------------------------------------------------------
 *
 * QuadSlopes --
 *
 *       Calculates the derivative at each of the data points.  The
 *       slopes computed will insure that an osculatory quadratic
 *       spline will have one additional knot between two adjacent
 *       points of interpolation.  Convexity and monotonicity are
 *       preserved wherever these conditions are compatible with the
 *       data.
 *
 * Parameters:
 *      Point2D points[]
 *      double *m - (out) To be filled with the first derivative at each data point.
 *      int nPoints - Number of data points (dimension of x, y, and m).
 *
 * Results:
 *      The output array "m" is filled with the derivates at each
 *      data point.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------------
 */
static void QuadSlopes(const Point2D points[], double *m, Tcl_Size nPoints) {
    double xbar, xmid, xhat, ydif1, ydif2;
    double yxmid;
    double m1, m2;
    double m1s, m2s;
    Tcl_Size i, n, l;

    m1s = m2s = m1 = m2 = 0;
    for (l = 0, i = 1, n = 2; i < (nPoints - 1); l++, i++, n++) {
        /*
         * Calculate the slopes of the two lines joining three
         * consecutive data points.
         */
        ydif1 = points[i].y - points[l].y;
        ydif2 = points[n].y - points[i].y;
        m1 = ydif1 / (points[i].x - points[l].x);
        m2 = ydif2 / (points[n].x - points[i].x);
        if (i == 1) {
            m1s = m1, m2s = m2; /* Save slopes of starting point */
        }
        /*
         * If one of the preceding slopes is zero or if they have opposite
         * sign, assign the value zero to the derivative at the middle
         * point.
         */
        if ((m1 == 0.0) || (m2 == 0.0) || ((m1 * m2) <= 0.0)) {
            m[i] = 0.0;
        } else if (FABS(m1) > FABS(m2)) {
            /*
             * Calculate the slope by extending the line with slope m1.
             */
            xbar = ydif2 / m1 + points[i].x;
            xhat = (xbar + points[n].x) / 2.0;
            m[i] = ydif2 / (xhat - points[i].x);
        } else {
            /*
             * Calculate the slope by extending the line with slope m2.
             */
            xbar = -ydif1 / m2 + points[i].x;
            xhat = (points[l].x + xbar) / 2.0;
            m[i] = ydif1 / (points[i].x - xhat);
        }
    }
    /* Calculate the slope at the last point, x(n). */
    i = nPoints - 2;
    n = nPoints - 1;
    if ((m1 * m2) < 0.0) {
        m[n] = m2 * 2.0;
    } else {
        xmid = (points[i].x + points[n].x) / 2.0;
        yxmid = m[i] * (xmid - points[i].x) + points[i].y;
        m[n] = (points[n].y - yxmid) / (points[n].x - xmid);
        if ((m[n] * m2) < 0.0) {
            m[n] = 0.0;
        }
    }
    /* Calculate the slope at the first point, x(0). */
    if ((m1s * m2s) < 0.0) {
        m[0] = m1s * 2.0;
    } else {
        xmid = (points[0].x + points[1].x) / 2.0;
        yxmid = m[1] * (xmid - points[1].x) + points[1].y;
        m[0] = (yxmid - points[0].y) / (xmid - points[0].x);
        if ((m[0] * m1s) < 0.0) {
            m[0] = 0.0;
        }
    }
}

/*
 *--------------------------------------------------------------
 *
 * QuadEval --
 *
 *      QuadEval controls the evaluation of an osculatory
 *      quadratic spline.  The user may provide his own
 *      slopes at the points of interpolation or use the
 *      subroutine 'QuadSlopes' to calculate slopes which
 *      are consistent with the shape of the data.
 *
 *      ON INPUT--
 *
 *        intpPts      must be a nondecreasing vector of
 *                     points at which the spline will be
 *                     evaluated.
 *        origPts      contains the abscissas of the data
 *                     points to be interpolated. xtab must
 *                     be increasing.
 *        y            contains the ordinates of the data
 *                     points to be interpolated.
 *        m            contains the slope of the spline at
 *                     each point of interpolation.
 *        nPoints      number of data points (dimension of
 *                     xtab and y).
 *        numEval      the number of points of evaluation
 *                     (dimension of xval and yval).
 *        epsilon      a relative error tolerance used in
 *                     subroutine 'QuadChoose' to distinguish
 *                     the situation m(i) or m(i+1) is
 *                     relatively close to the slope or twice
 *                     the slope of the linear segment
 *                     between xtab(i) and xtab(i+1).  If
 *                     this situation occurs, roundoff may
 *                     cause a change in convexity or
 *                     monotonicity of the resulting spline
 *                     and a change in the case number
 *                     provided by 'QuadChoose'.  If epsilon
 *                     is not equal to zero, then epsilon
 *                     should be greater than or equal to
 *                     machine epsilon.
 *
 *
 * Results:
 *      Returns 0 when all evaluation points are inside the source
 *      domain, 1 when one or more points are extrapolated, and 2
 *      on an internal evaluation error.
 *
 *      Evaluation points may appear in any order. Points outside
 *      the source domain are extrapolated using the first or last
 *      spline interval.
 *
 *
 *      QuadEval calls the following subroutines or functions:
 *
 *        Search
 *        QuadCases
 *        QuadChoose
 *        QuadSpline
 *
 * Parameters:
 *      Point2D origPts[]
 *      int nOrigPts
 *      Point2D intpPts[]
 *      int nIntpPts
 *      double *m - Slope of the spline at each point of interpolation.
 *      double epsilon - Relative error tolerance (see choose)
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int QuadEval(const Point2D origPts[], Tcl_Size nOrigPts, Point2D intpPts[], Tcl_Size nIntpPts, const double *m,
                    double epsilon) {
    double param[QUAD_PARAM_COUNT];
    Tcl_Size left;
    Tcl_Size right;
    Tcl_Size selectedLeft;
    Tcl_Size last;
    Tcl_Size i;
    int extrapolated;
    int ncase;

    extrapolated = FALSE;
    last = nOrigPts - 1;
    selectedLeft = -1;
    ncase = 0;
    for (i = 0; i < nIntpPts; i++) {
        double x;

        x = intpPts[i].x;
        if (x < origPts[0].x) {
            /*
             * Extrapolate using the first source interval.
             */
            extrapolated = TRUE;
            left = 0;
            right = 1;
        } else if (x > origPts[last].x) {
            /*
             * Extrapolate using the final source interval.
             */
            extrapolated = TRUE;
            left = last - 1;
            right = last;
        } else {
            Tcl_Size position;
            int isKnot;

            position = Search(origPts, nOrigPts, x, &isKnot);
            /*
             * Preserve source knots exactly instead of evaluating
             * the polynomial at them.
             */
            if (isKnot) {
                intpPts[i].y = origPts[position].y;
                continue;
            }
            /*
             * Search returns the insertion position. Since x is
             * inside the source domain and is not a knot, it is
             * the right endpoint of the interval containing x.
             */
            if ((position == 0) || (position >= nOrigPts)) {
                return 2;
            }
            left = position - 1;
            right = position;
        }
        /*
         * Recalculate spline parameters only when the source
         * interval changes.
         */
        if (selectedLeft != left) {
            ncase = QuadSelect(origPts + left, origPts + right, m[left], m[right], epsilon, param);
            selectedLeft = left;
        }
        QuadSpline(intpPts + i, origPts + left, origPts + right, param, ncase);
    }
    return extrapolated ? 1 : 0;
}

/*
 * -----------------------------------------------------------------------
 *
 *          Shape preserving quadratic splines
 *           by D.F.Mcallister & J.A.Roulier
 *            Coded by S.L.Dodd & M.Roulier
 *             N.C.State University
 *
 * -----------------------------------------------------------------------
 */

/*
 *--------------------------------------------------------------
 *
 * Rbc_QuadraticSpline --
 *
 *      Driver routine for quadratic spline package
 *
 *      On input--
 *
 *        X,Y    Contain n-long arrays of data (x is
 *               increasing)
 *        XM     XM Contains m-long array of finite x values. Values may be
 *                 in any order and may lie outside the source domain.
 *        eps    Relative error tolerance
 *        n      Number of input data points
 *        m      Number of output data points
 *
 *      On output--
 *
 *        work   Contains the value of the first derivative
 *               at each data point
 *        ym     Contains the interpolated spline value at
 *               each data point
 *
 * Parameters:
 *      Point2D origPts[]
 *      int nOrigPts
 *      Point2D intpPts[]
 *      int nIntpPts
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int Rbc_QuadraticSpline(const Point2D origPts[], Tcl_Size nOrigPts, Point2D intpPts[], Tcl_Size nIntpPts) {
    double epsilon;
    double *work;
    size_t byteCount;
    int result;
    Tcl_Size i;

    if ((nOrigPts < 3) || (nIntpPts < 0) || !SplinePointsHaveIncreasingX(origPts, nOrigPts) ||
        !SplineEvaluationPointsAreFinite(intpPts, nIntpPts)) {
        return FALSE;
    }
    if (nIntpPts == 0) {
        return TRUE;
    }
    if (GetSplineArrayByteCount(nOrigPts, sizeof(*work), &byteCount) != TCL_OK) {
        return FALSE;
    }
    work = Tcl_AttemptAlloc(byteCount);
    if (work == NULL) {
        return FALSE;
    }
    epsilon = 0.0;
    QuadSlopes(origPts, work, nOrigPts);
    for (i = 0; i < nOrigPts; i++) {
        if (!FINITE(work[i])) {
            ckfree(work);
            return FALSE;
        }
    }
    result = QuadEval(origPts, nOrigPts, intpPts, nIntpPts, work, epsilon);
    ckfree(work);
    if (result > 1) {
        return FALSE;
    }
    return SplinePointsAreFinite(intpPts, nIntpPts);
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_NaturalSpline --
 *
 *      Reference:
 *
 *        Numerical Analysis, R. Burden, J. Faires
 *          and A. Reynolds.
 *        Prindle, Weber & Schmidt 1981 pp 112
 *
 *      Parameters:
 *
 *        origPts - vector of points, assumed to be
 *                  sorted along x.
 *        intpPts - vector of new points.
 *
 * Parameters:
 *      Point2D origPts[]
 *      int nOrigPts
 *      Point2D intpPts[]
 *      int nIntpPts
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int Rbc_NaturalSpline(const Point2D origPts[], Tcl_Size nOrigPts, Point2D intpPts[], Tcl_Size nIntpPts) {
    Cubic2D *eq;
    TriDiagonalMatrix *A;
    double *dx;
    Tcl_Size nIntervals;
    Tcl_Size i;
    Tcl_Size j;
    size_t dxBytes;
    size_t matrixBytes;
    size_t equationBytes;
    int result;

    eq = NULL;
    A = NULL;
    dx = NULL;
    result = FALSE;
    if ((nOrigPts < 3) || (nIntpPts < 0) || !SplinePointsHaveIncreasingX(origPts, nOrigPts) ||
        !SplineEvaluationPointsAreFinite(intpPts, nIntpPts)) {
        return FALSE;
    }
    if (nIntpPts == 0) {
        return TRUE;
    }
    nIntervals = nOrigPts - 1;
    if ((GetSplineArrayByteCount(nIntervals, sizeof(*dx), &dxBytes) != TCL_OK) ||
        (GetSplineArrayByteCount(nOrigPts, sizeof(*A), &matrixBytes) != TCL_OK) ||
        (GetSplineArrayByteCount(nOrigPts, sizeof(*eq), &equationBytes) != TCL_OK)) {
        return FALSE;
    }
    dx = Tcl_AttemptAlloc(dxBytes);
    if (dx == NULL) {
        goto cleanup;
    }
    for (i = 0; i < nIntervals; i++) {
        dx[i] = origPts[i + 1].x - origPts[i].x;
        /*
         * Strictly increasing X values were checked above, but retain
         * the local check before every later division.
         */
        if (!FINITE(dx[i]) || (dx[i] <= 0.0)) {
            goto cleanup;
        }
    }
    A = Tcl_AttemptAlloc(matrixBytes);
    if (A == NULL) {
        goto cleanup;
    }
    A[0][0] = 1.0;
    A[0][1] = 0.0;
    A[0][2] = 0.0;
    A[nIntervals][0] = 1.0;
    A[nIntervals][1] = 0.0;
    A[nIntervals][2] = 0.0;
    for (i = 0, j = 1; j < nIntervals; i++, j++) {
        double alpha;

        alpha = 3.0 * ((origPts[j + 1].y - origPts[j].y) / dx[j] - (origPts[j].y - origPts[i].y) / dx[i]);
        if (!FINITE(alpha)) {
            goto cleanup;
        }
        A[j][0] = 2.0 * (dx[j] + dx[i]) - dx[i] * A[i][1];
        if ((!FINITE(A[j][0])) || (A[j][0] == 0.0)) {
            goto cleanup;
        }
        A[j][1] = dx[j] / A[j][0];
        A[j][2] = (alpha - dx[i] * A[i][2]) / A[j][0];
        if ((!FINITE(A[j][1])) || (!FINITE(A[j][2]))) {
            goto cleanup;
        }
    }
    eq = Tcl_AttemptAlloc(equationBytes);
    if (eq == NULL) {
        goto cleanup;
    }
    eq[0].c = 0.0;
    eq[nIntervals].c = 0.0;
    /*
     * Back-substitute from interval nIntervals - 1 down to zero.
     * This form avoids relying on a signed index becoming negative.
     */
    for (i = nIntervals; i-- > 0;) {
        double dy;

        j = i + 1;
        eq[i].c = A[i][2] - A[i][1] * eq[j].c;
        dy = origPts[i + 1].y - origPts[i].y;
        eq[i].b = dy / dx[i] - dx[i] * (eq[j].c + 2.0 * eq[i].c) / 3.0;
        eq[i].d = (eq[j].c - eq[i].c) / (3.0 * dx[i]);
        if ((!FINITE(eq[i].b)) || (!FINITE(eq[i].c)) || (!FINITE(eq[i].d))) {
            goto cleanup;
        }
    }
    for (i = 0; i < nIntpPts; i++) {
        double x;
        double localX;
        Tcl_Size interval;
        int isKnot;

        x = intpPts[i].x;
        if (x < origPts[0].x) {
            /*
             * Extrapolate using the first spline interval.
             */
            interval = 0;
        } else if (x > origPts[nIntervals].x) {
            /*
             * Extrapolate using the final spline interval.
             */
            interval = nIntervals - 1;
        } else {
            interval = Search(origPts, nOrigPts, x, &isKnot);
            /*
             * Preserve source knots exactly instead of evaluating
             * the polynomial at them.
             */
            if (isKnot) {
                intpPts[i].y = origPts[interval].y;
                if (!FINITE(intpPts[i].y)) {
                    goto cleanup;
                }
                continue;
            }
            /*
             * Search returns the insertion position. Since x is
             * inside the source domain and is not a knot, this must
             * be the right endpoint of a valid interval.
             */
            if (interval == 0) {
                goto cleanup;
            }
            interval--;
        }
        localX = x - origPts[interval].x;
        intpPts[i].y =
            origPts[interval].y + localX * (eq[interval].b + localX * (eq[interval].c + localX * eq[interval].d));
        if (!FINITE(intpPts[i].y)) {
            goto cleanup;
        }
    }
    result = TRUE;

cleanup:
    if (eq != NULL) {
        ckfree(eq);
    }
    if (A != NULL) {
        ckfree(A);
    }
    if (dx != NULL) {
        ckfree(dx);
    }
    return result;
}

static const SplineOpSpec splineOps[] = {{{"natural", 6, 6, "x y splx sply"}, Rbc_NaturalSpline},
                                         {{"quadratic", 6, 6, "x y splx sply"}, Rbc_QuadraticSpline},
                                         {{NULL, 0, 0, NULL}, NULL}};

static int GetRealSplineVector(Tcl_Interp *interp, const char *name, Rbc_Vector **vecPtrPtr) {
    if (Rbc_GetVector(interp, name, vecPtrPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Rbc_VectorGetType(*vecPtrPtr) != RBC_VECTOR_REAL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("spline vector \"%s\" must be real", name));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * SplineObjCmd --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ClientData clientData
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const objv[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int SplineObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Vector *x;
    Rbc_Vector *y;
    Rbc_Vector *splX;
    Rbc_Vector *splY;
    Rbc_SplineOp proc;
    const char *xName;
    const char *yName;
    const char *splXName;
    const char *splYName;
    const double *xArr;
    const double *yArr;
    double *resultArr;
    Point2D *origPts;
    Point2D *intpPts;
    Tcl_Size nOrigPts;
    Tcl_Size nIntpPts;
    Tcl_Size i;
    size_t origBytes;
    size_t intpBytes;
    int index;
    int result;

    (void)clientData;
    origPts = NULL;
    intpPts = NULL;
    result = TCL_ERROR;
    if (Rbc_GetOpIndexFromObj(interp, splineOps, (Tcl_Size)sizeof(splineOps[0]), RBC_OP_ARG1, objc, objv, &index) !=
        TCL_OK) {
        return TCL_ERROR;
    }
    proc = splineOps[index].proc;
    xName = Tcl_GetString(objv[2]);
    yName = Tcl_GetString(objv[3]);
    splXName = Tcl_GetString(objv[4]);
    splYName = Tcl_GetString(objv[5]);
    if ((GetRealSplineVector(interp, xName, &x) != TCL_OK) || (GetRealSplineVector(interp, yName, &y) != TCL_OK) ||
        (GetRealSplineVector(interp, splXName, &splX) != TCL_OK)) {
        return TCL_ERROR;
    }
    nOrigPts = Rbc_VectorLength(x);
    if (nOrigPts < 3) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("length of vector \"%s\" is < 3", xName));
        return TCL_ERROR;
    }
    if (nOrigPts != Rbc_VectorLength(y)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("vectors \"%s\" and \"%s\" have different lengths", xName, yName));
        return TCL_ERROR;
    }
    /*
     * Validate source data before doing any work that can modify
     * another vector.
     */
    xArr = Rbc_VectorData(x);
    yArr = Rbc_VectorData(y);
    for (i = 0; i < nOrigPts; i++) {
        if ((!FINITE(xArr[i])) || (!FINITE(yArr[i]))) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("vectors \"%s\" and \"%s\" "
                                                   "must contain only finite values",
                                                   xName, yName));
            return TCL_ERROR;
        }
    }
    for (i = 1; i < nOrigPts; i++) {
        if (xArr[i] <= xArr[i - 1]) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("x vector \"%s\" must be strictly increasing", xName));
            return TCL_ERROR;
        }
    }
    /*
     * Validate all interpolation abscissas before creating or
     * resizing the result vector.
     */
    nIntpPts = Rbc_VectorLength(splX);
    xArr = Rbc_VectorData(splX);
    for (i = 0; i < nIntpPts; i++) {
        if (!FINITE(xArr[i])) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("interpolation vector \"%s\" "
                                                   "must contain only finite values",
                                                   splXName));
            return TCL_ERROR;
        }
    }
    /*
     * Allocate and populate private copies of all spline inputs
     * before touching splY.  This also makes output aliasing with
     * x, y, or splX safe.
     */
    if ((GetSplineArrayByteCount(nOrigPts, sizeof(*origPts), &origBytes) != TCL_OK) ||
        (GetSplineArrayByteCount(nIntpPts, sizeof(*intpPts), &intpBytes) != TCL_OK)) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("spline point array is too large", -1));
        return TCL_ERROR;
    }
    origPts = Tcl_AttemptAlloc(origBytes);
    if (origPts == NULL) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("can't allocate %" TCL_SIZE_MODIFIER "d source points", nOrigPts));
        return TCL_ERROR;
    }
    /*
     * Keep a non-NULL interpolation array even for an empty vector,
     * since the spline helpers accept nIntpPts == 0 but still
     * validate the array pointer.
     */
    intpPts = Tcl_AttemptAlloc((intpBytes > 0) ? intpBytes : sizeof(*intpPts));
    if (intpPts == NULL) {
        Tcl_SetObjResult(interp,
                         Tcl_ObjPrintf("can't allocate %" TCL_SIZE_MODIFIER "d interpolation points", nIntpPts));

        goto cleanup;
    }
    xArr = Rbc_VectorData(x);
    yArr = Rbc_VectorData(y);
    for (i = 0; i < nOrigPts; i++) {
        origPts[i].x = xArr[i];
        origPts[i].y = yArr[i];
    }
    xArr = Rbc_VectorData(splX);
    for (i = 0; i < nIntpPts; i++) {
        intpPts[i].x = xArr[i];
        intpPts[i].y = 0.0;
    }
    /*
     * Generate the complete result in private storage first.
     * Failure must leave splY untouched.
     */
    if (!proc(origPts, nOrigPts, intpPts, nIntpPts)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("error generating spline for \"%s\"", splYName));
        goto cleanup;
    }
    /*
     * Only now resolve/create/resize the result vector.
     */
    if (Rbc_GetVector(interp, splYName, &splY) != TCL_OK) {
        Tcl_ResetResult(interp);
        if (Rbc_CreateVector(interp, splYName, nIntpPts, &splY) != TCL_OK) {
            goto cleanup;
        }
    } else {
        if (Rbc_VectorGetType(splY) != RBC_VECTOR_REAL) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("spline vector \"%s\" must be real", splYName));
            goto cleanup;
        }
        if (nIntpPts != Rbc_VectorLength(splY)) {
            if (Rbc_ResizeVector(splY, nIntpPts) != TCL_OK) {
                goto cleanup;
            }
        }
    }
    resultArr = Rbc_VectorData(splY);
    for (i = 0; i < nIntpPts; i++) {
        resultArr[i] = intpPts[i].y;
    }
    if (Rbc_ResetVector(splY, Rbc_VectorData(splY), Rbc_VectorLength(splY), Rbc_VectorSize(splY), TCL_STATIC) != TCL_OK) {
        goto cleanup;
    }
    result = TCL_OK;

cleanup:
    if (intpPts != NULL) {
        ckfree(intpPts);
    }
    if (origPts != NULL) {
        ckfree(origPts);
    }
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_SplineInit --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
int Rbc_SplineInit(Tcl_Interp *interp) {
    Tcl_CreateObjCommand2(interp, "::rbc::spline", SplineObjCmd, NULL, NULL);
    return TCL_OK;
}

/*
 *--------------------------------------------------------------
 *
 * SolveCubic1 --
 *
 *      The Cholesky decomposition A = C^T * D * C (C is
 *      upper triangle with unit diagonal, D is diagonal)
 *      is calculated.
 *
 *      If x is assumed cyclic ( x[i]=x[n+i] ) the
 *      equations can be written as (i=0,1,...,n-1):
 *
 *        m[i][0] * x[i-1] + m[i][1] * x[i] + m[i][2] *
 *        x[i+1] = b[i] .
 *
 *      In matrix notation one gets A * x = b, where the
 *      matrix A is tridiagonal with additional elements
 *      in the upper right and lower left position:
 *
 *        A[i][0] = A_{i,i-1}  for i=1,2,...,n-1
 *          and    m[0][0] = A_{0,n-1} ,
 *
 *        A[i][1] = A_{i, i }  for i=0,1,...,n-1
 *
 *        A[i][2] = A_{i,i+1}  for i=0,1,...,n-2
 *          and    m[n-1][2] = A_{n-1,0}.
 *
 *      A should be symmetric (A[i+1][0] == A[i][2]) and positive definite.
 *      The size of the system is given in n (n>=1).
 *
 * Parameters:
 *      TriDiagonalMatrix A[]
 *      int n
 *
 * Results:
 *      Return TRUE if decomposition exist.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static int SolveCubic1(TriDiagonalMatrix A[], Tcl_Size n) {
    Tcl_Size i;
    double m_ij, m_n, m_nn, d;

    if (n < 1) {
        return FALSE; /* Dimension should be at least 1 */
    }
    d = A[0][1]; /* D_{0,0} = A_{0,0} */
    if ((!FINITE(d)) || (d <= 0.0)) {
        return FALSE; /* A (or D) should be positive definite */
    }
    m_n = A[0][0];      /*  A_{0,n-1}  */
    m_nn = A[n - 1][1]; /* A_{n-1,n-1} */
    for (i = 0; i < n - 2; i++) {
        m_ij = A[i][2];                   /*  A_{i,1}  */
        A[i][2] = m_ij / d;               /* C_{i,i+1} */
        A[i][0] = m_n / d;                /* C_{i,n-1} */
        m_nn -= A[i][0] * m_n;            /* to get C_{n-1,n-1} */
        m_n = -A[i][2] * m_n;             /* to get C_{i+1,n-1} */
        d = A[i + 1][1] - A[i][2] * m_ij; /* D_{i+1,i+1} */
        if (d <= 0.0) {
            return FALSE; /* Elements of D should be positive */
        }
        A[i + 1][1] = d;
    }
    if (n >= 2) {                                   /* Complete last column */
        m_n += A[n - 2][2];                         /* add A_{n-2,n-1} */
        A[n - 2][0] = m_n / d;                      /* C_{n-2,n-1} */
        A[n - 1][1] = d = m_nn - A[n - 2][0] * m_n; /* D_{n-1,n-1} */
        if (d <= 0.0) {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 *--------------------------------------------------------------
 *
 * SolveCubic2 --
 *
 *      The second procedure solves the linear system, with
 *      the Cholesky decomposition calculated above (in m[][])
 *      and the right side b given in x[]. The solution x
 *      overwrites the right side in x[].
 *
 *      If x is assumed cyclic ( x[i]=x[n+i] ) the
 *      equations can be written as (i=0,1,...,n-1):
 *
 *        m[i][0] * x[i-1] + m[i][1] * x[i] + m[i][2] *
 *        x[i+1] = b[i] .
 *
 *      In matrix notation one gets A * x = b, where the
 *      matrix A is tridiagonal with additional elements
 *      in the upper right and lower left position:
 *
 *        A[i][0] = A_{i,i-1}  for i=1,2,...,n-1
 *          and    m[0][0] = A_{0,n-1} ,
 *
 *        A[i][1] = A_{i, i }  for i=0,1,...,n-1
 *
 *        A[i][2] = A_{i,i+1}  for i=0,1,...,n-2
 *          and    m[n-1][2] = A_{n-1,0}.
 *
 *      A should be symmetric (A[i+1][0] == A[i][2]) and positive definite.
 *      The size of the system is given in n (n>=1).
 *
 * Parameters:
 *      TriDiagonalMatrix A[]
 *      CubicSpline spline[]
 *      int nIntervals
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void SolveCubic2(const TriDiagonalMatrix A[], CubicSpline spline[], Tcl_Size nIntervals) {
    Tcl_Size i;
    double x, y;
    Tcl_Size n, m;

    n = nIntervals - 2;
    m = nIntervals - 1;
    /* Division by transpose of C : b = C^{-T} * b */
    x = spline[m].x;
    y = spline[m].y;
    for (i = 0; i < n; i++) {
        spline[i + 1].x -= A[i][2] * spline[i].x; /* C_{i,i+1} * x(i) */
        spline[i + 1].y -= A[i][2] * spline[i].y; /* C_{i,i+1} * x(i) */
        x -= A[i][0] * spline[i].x;               /* C_{i,n-1} * x(i) */
        y -= A[i][0] * spline[i].y;               /* C_{i,n-1} * x(i) */
    }
    if (n >= 0) {
        /* C_{n-2,n-1} * x_{n-1} */
        spline[m].x = x - A[n][0] * spline[n].x;
        spline[m].y = y - A[n][0] * spline[n].y;
    }
    /* Division by D: b = D^{-1} * b */
    for (i = 0; i < nIntervals; i++) {
        spline[i].x /= A[i][1];
        spline[i].y /= A[i][1];
    }
    /* Division by C: b = C^{-1} * b */
    x = spline[m].x;
    y = spline[m].y;
    if (n >= 0) {
        /* C_{n-2,n-1} * x_{n-1} */
        spline[n].x -= A[n][0] * x;
        spline[n].y -= A[n][0] * y;
    }
    for (i = (n - 1); i >= 0; i--) {
        /* C_{i,i+1} * x_{i+1} + C_{i,n-1} * x_{n-1} */
        spline[i].x -= A[i][2] * spline[i + 1].x + A[i][0] * x;
        spline[i].y -= A[i][2] * spline[i + 1].y + A[i][0] * y;
    }
}

/*
 *--------------------------------------------------------------
 *
 * CubicSlopes --
 *
 *      Find second derivatives (x''(t_i),y''(t_i)) of cubic
 *      spline interpolation through list of points (x_i,y_i).
 *      The parameter t is calculated as the length of the
 *      linear stroke. The number of points must be at least 3.
 *
 *      Note: For CLOSED_CONTOURs the first and last point
 *      must be equal.
 *
 * Parameters:
 *      Point2D points[]
 *      int nPoints - Number of points (nPoints>=3)
 *      int isClosed - CLOSED_CONTOUR or OPEN_CONTOUR
 *      double unitX -  Unit length in x (norm=1)
 *      double unitY -  Unit length in y (norm=1)
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static CubicSpline *CubicSlopes(const Point2D points[], Tcl_Size nPoints, int isClosed, double unitX, double unitY) {
    CubicSpline *spline;
    CubicSpline *s1;
    CubicSpline *s2;
    TriDiagonalMatrix *A;
    Tcl_Size n;
    Tcl_Size i;
    size_t splineBytes;
    size_t matrixBytes;

    spline = NULL;
    A = NULL;
    if ((nPoints < 3) || !SplinePointsAreFinite(points, nPoints) || !FINITE(unitX) || !FINITE(unitY) ||
        (unitX <= 0.0) || (unitY <= 0.0)) {
        return NULL;
    }
    if (isClosed && ((points[0].x != points[nPoints - 1].x) || (points[0].y != points[nPoints - 1].y))) {
        return NULL;
    }
    if ((GetSplineArrayByteCount(nPoints, sizeof(*spline), &splineBytes) != TCL_OK) ||
        (GetSplineArrayByteCount(nPoints, sizeof(*A), &matrixBytes) != TCL_OK)) {
        return NULL;
    }
    spline = Tcl_AttemptAlloc(splineBytes);
    if (spline == NULL) {
        return NULL;
    }
    A = Tcl_AttemptAlloc(matrixBytes);
    if (A == NULL) {
        ckfree(spline);
        return NULL;
    }
    s1 = spline;
    for (i = 0; i < (nPoints - 1); i++, s1++) {
        double dx;
        double dy;

        s1->x = points[i + 1].x - points[i].x;
        s1->y = points[i + 1].y - points[i].y;
        dx = s1->x / unitX;
        dy = s1->y / unitY;
        s1->t = hypot(dx, dy);
        /*
         * Consecutive coincident points create a zero-length interval,
         * which makes the normalized first difference undefined.
         */
        if (!FINITE(s1->t) || (s1->t <= DBL_EPSILON)) {
            ckfree(A);
            ckfree(spline);
            return NULL;
        }
        s1->x /= s1->t;
        s1->y /= s1->t;
    }
    n = nPoints - 2;
    if (isClosed) {
        spline[nPoints - 1].t = spline[0].t;
        spline[nPoints - 1].x = spline[0].x;
        spline[nPoints - 1].y = spline[0].y;
        n++;
    }
    s1 = spline;
    s2 = spline + 1;
    for (i = 0; i < n; i++, s1++, s2++) {
        double dx;
        double dy;
        double norm;

        A[i][0] = s1->t;
        A[i][1] = 2.0 * (s1->t + s2->t);
        A[i][2] = s2->t;
        s1->x = (s2->x - s1->x) * 6.0;
        s1->y = (s2->y - s1->y) * 6.0;
        dx = s1->x / unitX;
        dy = s1->y / unitY;
        norm = hypot(dx, dy) / 8.5;
        if (!FINITE(norm)) {
            ckfree(A);
            ckfree(spline);
            return NULL;
        }
        if (norm > 1.0) {
            s1->x /= norm;
            s1->y /= norm;
        }
    }
    if (!isClosed) {
        A[0][1] += A[0][0];
        A[0][0] = 0.0;
        A[n - 1][1] += A[n - 1][2];
        A[n - 1][2] = 0.0;
    }
    if (!SolveCubic1(A, n)) {
        ckfree(A);
        ckfree(spline);
        return NULL;
    }
    SolveCubic2(A, spline, n);
    /*
     * Reject arithmetic overflow in the solved second derivatives.
     */
    for (i = 0; i < nPoints; i++) {
        if ((!FINITE(spline[i].x)) || (!FINITE(spline[i].y))) {
            ckfree(A);
            ckfree(spline);
            return NULL;
        }
    }
    s2 = spline + n;
    s1 = s2 - 1;
    while (s2 > spline) {
        s2->x = s1->x;
        s2->y = s1->y;
        s2--;
        s1--;
    }
    if (isClosed) {
        spline[0].x = spline[n].x;
        spline[0].y = spline[n].y;
    } else {
        spline[0].x = spline[1].x;
        spline[0].y = spline[1].y;
        spline[n + 1].x = spline[n].x;
        spline[n + 1].y = spline[n].y;
    }
    ckfree(A);
    return spline;
}

/*
 *--------------------------------------------------------------
 *
 * CubicEval --
 *
 *      Calculate interpolated values of the spline function
 *      (defined via p_cntr and the second derivatives
 *      dxdt2[] and dydt2[]). The number of tabulated values
 *      is n. On an equidistant grid n_intpol values are
 *      calculated.
 *
 * Parameters:
 *      Point2D origPts[]
 *      int nOrigPts
 *      Point2D intpPts[]
 *      int nIntpPts
 *      CubicSpline spline[]
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static Tcl_Size CubicEval(const Point2D origPts[], Tcl_Size nOrigPts, Point2D intpPts[], Tcl_Size nIntpPts,
                          const CubicSpline spline[]) {
    double t;
    double tSkip;
    double tMax;
    Tcl_Size i;
    Tcl_Size j;
    Tcl_Size count;

    if ((nOrigPts < 2) || (nIntpPts < 2) || (origPts == NULL) || (intpPts == NULL) || (spline == NULL)) {
        return 0;
    }
    tMax = 0.0;
    for (i = 0; i < (nOrigPts - 1); i++) {
        if (!FINITE(spline[i].t) || (spline[i].t <= 0.0) || (tMax > DBL_MAX - spline[i].t)) {
            return 0;
        }
        tMax += spline[i].t;
    }
    if (!FINITE(tMax) || (tMax <= 0.0)) {
        return 0;
    }
    tSkip = (1.0 - 1.0e-7) * tMax / (double)(nIntpPts - 1);
    if (!FINITE(tSkip) || (tSkip <= 0.0)) {
        return 0;
    }
    count = 0;
    intpPts[count++] = origPts[0];
    t = tSkip;
    for (i = 0, j = 1; j < nOrigPts; i++, j++) {
        Point2D p;
        Point2D q;
        double d;
        double hx;
        double hy;
        double dx0;
        double dy0;
        double dx01;
        double dy01;

        p = origPts[i];
        q = origPts[j];
        d = spline[i].t;
        hx = (q.x - p.x) / d;
        hy = (q.y - p.y) / d;
        dx0 = (spline[j].x + 2.0 * spline[i].x) / 6.0;
        dy0 = (spline[j].y + 2.0 * spline[i].y) / 6.0;
        dx01 = (spline[j].x - spline[i].x) / (6.0 * d);
        dy01 = (spline[j].y - spline[i].y) / (6.0 * d);
        while ((t <= d) && (count < nIntpPts)) {
            Point2D value;
            /*
             * Evaluate each point relative to the fixed start of the
             * interval. Do not accumulate on the previous generated
             * point.
             */
            value.x = p.x + t * (hx + (t - d) * (dx0 + t * dx01));
            value.y = p.y + t * (hy + (t - d) * (dy0 + t * dy01));
            if ((!FINITE(value.x)) || (!FINITE(value.y))) {
                return 0;
            }
            intpPts[count++] = value;
            t += tSkip;
        }
        t -= d;
    }
    return count;
}

/*
 *--------------------------------------------------------------
 *
 * Rbc_NaturalParametricSpline --
 *
 *      Generate a cubic spline curve through the points
 *      (x_i,y_i) which are stored in the linked list
 *      p_cntr.
 *
 *      The spline is defined as a 2d-function
 *      s(t) = (x(t),y(t)), where the parameter t is the
 *      length of the linear stroke.
 *
 * Parameters:
 *      Point2D origPts[]
 *      int nOrigPts
 *      Extents2D *extsPtr
 *      int isClosed
 *      Point2D *intpPts
 *      int nIntpPts
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
Tcl_Size Rbc_NaturalParametricSpline(const Point2D origPts[], Tcl_Size nOrigPts, const Extents2D *extsPtr, int isClosed,
                                     Point2D *intpPts, Tcl_Size nIntpPts) {
    const Point2D *workPts;
    Point2D *ownedPts;
    CubicSpline *spline;
    Tcl_Size workCount;
    Tcl_Size result;
    double unitX;
    double unitY;

    workPts = origPts;
    ownedPts = NULL;
    spline = NULL;
    workCount = nOrigPts;
    result = 0;
    if ((origPts == NULL) || (intpPts == NULL) || (extsPtr == NULL) || (nOrigPts < 3) || (nIntpPts < 2) ||
        !SplinePointsAreFinite(origPts, nOrigPts)) {
        return 0;
    }
    if (isClosed) {
        int alreadyClosed;

        alreadyClosed = (origPts[0].x == origPts[nOrigPts - 1].x) && (origPts[0].y == origPts[nOrigPts - 1].y);
        if (!alreadyClosed) {
            Tcl_Size closedCount;
            size_t byteCount;
            if (nOrigPts == TCL_SIZE_MAX) {
                return 0;
            }
            closedCount = nOrigPts + 1;
            if (GetSplineArrayByteCount(closedCount, sizeof(*ownedPts), &byteCount) != TCL_OK) {
                return 0;
            }
            ownedPts = Tcl_AttemptAlloc(byteCount);
            if (ownedPts == NULL) {
                return 0;
            }
            memcpy(ownedPts, origPts, (size_t)nOrigPts * sizeof(*ownedPts));
            ownedPts[nOrigPts] = origPts[0];
            workPts = ownedPts;
            workCount = closedCount;
        }
    }
    unitX = fabs(extsPtr->right - extsPtr->left);
    unitY = fabs(extsPtr->bottom - extsPtr->top);
    if (!FINITE(unitX) || (unitX < FLT_EPSILON)) {
        unitX = FLT_EPSILON;
    }
    if (!FINITE(unitY) || (unitY < FLT_EPSILON)) {
        unitY = FLT_EPSILON;
    }
    spline = CubicSlopes(workPts, workCount, isClosed, unitX, unitY);
    if (spline != NULL) {
        result = CubicEval(workPts, workCount, intpPts, nIntpPts, spline);
    }
    if (spline != NULL) {
        ckfree(spline);
    }
    if (ownedPts != NULL) {
        ckfree(ownedPts);
    }
    return result;
}

/*
 *--------------------------------------------------------------
 *
 * CatromCoeffs --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Point2D *p
 *      Point2D *a
 *      Point2D *b
 *      Point2D *c
 *      Point2D *d
 *
 * Results:
 *      TODO: Results
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *--------------------------------------------------------------
 */
static void CatromCoeffs(const Point2D *p, Point2D *a, Point2D *b, Point2D *c, Point2D *d) {
    a->x = -p[0].x + 3.0 * p[1].x - 3.0 * p[2].x + p[3].x;
    b->x = 2.0 * p[0].x - 5.0 * p[1].x + 4.0 * p[2].x - p[3].x;
    c->x = -p[0].x + p[2].x;
    d->x = 2.0 * p[1].x;
    a->y = -p[0].y + 3.0 * p[1].y - 3.0 * p[2].y + p[3].y;
    b->y = 2.0 * p[0].y - 5.0 * p[1].y + 4.0 * p[2].y - p[3].y;
    c->y = -p[0].y + p[2].y;
    d->y = 2.0 * p[1].y;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_CatromParametricSpline --
 *
 *      Computes a spline based upon the data points, returning a new
 *      (larger) coordinate array or points.
 *
 * Parameters:
 *      Point2D *points
 *      int nPoints
 *      Point2D *intpPts
 *      int nIntpPts
 *
 * Results:
 *      None.
 *
 * Side effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_CatromParametricSpline(const Point2D *points, Tcl_Size nPoints, Point2D *intpPts, Tcl_Size nIntpPts) {
    Point2D *origPts;
    Point2D a;
    Point2D b;
    Point2D c;
    Point2D d;
    Tcl_Size extendedCount;
    Tcl_Size i;
    size_t byteCount;

    if ((points == NULL) || (intpPts == NULL) || (nPoints < 1) || (nIntpPts < 0) ||
        !SplinePointsAreFinite(points, nPoints)) {
        return FALSE;
    }
    if (nPoints > (TCL_SIZE_MAX - 4)) {
        return FALSE;
    }
    /*
     * Validate every interpolation parameter before modifying
     * any element of intpPts.
     *
     * intpPts[i].x is the source interval number and
     * intpPts[i].y is the normalized position within it.
     *
     * The final source point is represented specially as
     * interval nPoints - 1 with t == 0.0.
     */
    for (i = 0; i < nIntpPts; i++) {
        double intervalValue;
        double t;

        intervalValue = intpPts[i].x;
        t = intpPts[i].y;
        if ((!FINITE(intervalValue)) || (!FINITE(t)) || (intervalValue < 0.0) ||
            (intervalValue > (double)(nPoints - 1)) || (floor(intervalValue) != intervalValue) || (t < 0.0) ||
            (t > 1.0) || ((intervalValue == (double)(nPoints - 1)) && (t != 0.0))) {
            return FALSE;
        }
    }
    extendedCount = nPoints + 4;
    if (GetSplineArrayByteCount(extendedCount, sizeof(*origPts), &byteCount) != TCL_OK) {
        return FALSE;
    }
    origPts = Tcl_AttemptAlloc(byteCount);
    if (origPts == NULL) {
        return FALSE;
    }
    memcpy(origPts + 1, points, (size_t)nPoints * sizeof(*origPts));
    origPts[0] = origPts[1];
    origPts[nPoints + 1] = origPts[nPoints];
    origPts[nPoints + 2] = origPts[nPoints];
    /*
     * All operations that can fail are complete.  It is now safe
     * to replace the interpolation parameters with generated
     * coordinates.
     */
    for (i = 0; i < nIntpPts; i++) {
        Tcl_Size interval;
        double t;

        interval = (Tcl_Size)intpPts[i].x;
        t = intpPts[i].y;
        CatromCoeffs(origPts + interval, &a, &b, &c, &d);
        intpPts[i].x = (d.x + t * (c.x + t * (b.x + t * a.x))) / 2.0;
        intpPts[i].y = (d.y + t * (c.y + t * (b.y + t * a.y))) / 2.0;
        if ((!FINITE(intpPts[i].x)) || (!FINITE(intpPts[i].y))) {
            ckfree(origPts);
            return FALSE;
        }
    }
    ckfree(origPts);
    return TRUE;
}
