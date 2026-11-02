/*
 * rbcSpline.c --
 *
 *      Cartesian and parametric interpolation, and the Tcl spline command.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

/** @file rbcSpline.c
 * Spline implementation helpers.
 */
/** @defgroup spline_internals Spline implementation helpers
 * Private implementation helpers; not installed C API entry points.
 */
#include <stdint.h>
#include "rbcVectorInt.h"

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
static int ParametricSplineObjCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]);
static Tcl_Size NaturalParametricSplineCore(const Point2D origPts[], Tcl_Size nOrigPts, double unitX, double unitY,
                                            int isClosed, Point2D intpPts[], Tcl_Size nIntpPts);
static Tcl_Size QuadraticParametricSplineCore(const Point2D origPts[], Tcl_Size nOrigPts, Point2D intpPts[],
                                              Tcl_Size nIntpPts);


/**
 * @brief Check the byte size of a spline workspace array.
 * @ingroup spline_internals
 *
 * Accepts zero count but rejects negative count, zero elementSize or SIZE_MAX
 * overflow. Returns TCL_OK and writes byteCountPtr on success; otherwise returns
 * TCL_ERROR without changing the output. No allocation or interpreter result.
 */
static int GetSplineArrayByteCount(Tcl_Size count, size_t elementSize, size_t *byteCountPtr) {
    if ((count < 0) || (elementSize == 0) || ((Tcl_WideUInt)count > (Tcl_WideUInt)(SIZE_MAX / elementSize))) {
        return TCL_ERROR;
    }
    *byteCountPtr = (size_t)count * elementSize;
    return TCL_OK;
}

/**
 * @brief Validate both coordinates of a point array.
 * @ingroup spline_internals
 *
 * Returns FALSE for NULL storage, negative count or a non-finite coordinate.
 * A non-NULL array with zero points is valid. Does not modify the array.
 */
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

/**
 * @brief Validate finite source points with strictly increasing X.
 * @ingroup spline_internals
 *
 * Requires at least two points and validates both coordinates before comparing
 * adjacent abscissas. Returns boolean; the interpolation wrappers impose their
 * own stronger minimum of three source points. Does not modify storage.
 */
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

/**
 * @brief Validate Cartesian query abscissas without inspecting ordinates.
 * @ingroup spline_internals
 *
 * Rejects NULL storage, negative count and non-finite X. Accepts zero queries
 * with a non-NULL pointer. Query Y may be uninitialized because kernels replace
 * it. Returns boolean and does not change the array.
 */
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

/**
 * @brief Find an exact knot or insertion position in sorted source X.
 * @ingroup spline_internals
 *
 * Requires strictly increasing source abscissas and finite key. Callers search
 * inside the source domain. Sets foundPtr TRUE for an exact knot, otherwise FALSE
 * and returns the first position whose abscissa exceeds key (possibly nPoints
 * for an out-of-domain caller). Reads only X; no allocation or mutation.
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

/**
 * @brief Choose one of four quadratic interval constructions.
 * @ingroup spline_internals
 *
 * p and q are ordered knots with distinct X; m1 and m2 are endpoint slopes.
 * epsilon is a relative tolerance for slope comparisons (zero in the current
 * Cartesian wrapper). Returns case 1 through 4 without modifying inputs. Case 4
 * uses two interior knots; the other cases use one. Assumes valid finite inputs;
 * intermediate arithmetic is not checked here.
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

/**
 * @brief Populate the ten parameters of a selected quadratic interval.
 * @ingroup spline_internals
 *
 * Requires the case returned by QuadChoose for p, q, m1 and m2. Clears all
 * QUAD_PARAM_COUNT entries before filling the case-dependent knot/control-point
 * pairs V, W, Z, E and Y. The caller owns param and must supply ten doubles.
 * No allocation or finite-result validation; QuadSpline consumes the same case.
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

/**
 * @brief Choose a quadratic case and fill its parameter array.
 * @ingroup spline_internals
 *
 * Combines QuadChoose and QuadCases. Returns the case number and writes ten
 * caller-owned doubles to param. Preconditions and unchecked intermediate
 * arithmetic are the same as those helpers; no allocation or callbacks.
 */
static int QuadSelect(const Point2D *p, const Point2D *q, double m1, double m2, double epsilon, double param[]) {
    int ncase;

    ncase = QuadChoose(p, q, m1, m2, epsilon);
    QuadCases(p, q, m1, m2, param, ncase);
    return ncase;
}

/**
 * @brief Evaluate the quadratic blend used within one spline piece.
 * @ingroup spline_internals
 *
 * Uses weights from x1-x2 and x2-x3 over the squared span x1-x3; p1, p2 and p3
 * are the corresponding ordinates/control value. Requires distinct x1 and x3.
 * Returns a scalar without finite checks, allocation or side effects.
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

/**
 * @brief Replace one query ordinate using a prepared quadratic interval.
 * @ingroup spline_internals
 *
 * Reads intp->x and writes only intp->y. left/right, param and ncase must describe
 * the same interval selected by QuadSelect. Selects the appropriate subpiece,
 * preserving an internal knot ordinate exactly. Allows extrapolation and performs
 * no allocation or finite-result check.
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

/**
 * @brief Estimate shape-preserving quadratic knot derivatives.
 * @ingroup spline_internals
 *
 * Requires at least three finite points with strictly increasing X and caller
 * storage for nPoints derivatives. Uses adjacent secants, sets interior slopes
 * to zero at flat or sign-changing joins, then derives endpoint slopes. Fills m
 * without allocation; the caller must reject non-finite derivatives afterward.
 * The construction preserves shape where compatible with the supplied data.
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

/**
 * @brief Evaluate ordered-source quadratic splines at arbitrary query X.
 * @ingroup spline_internals
 *
 * Source X must increase strictly and m must supply one derivative per source
 * knot. Query X need not be sorted. Preserves exact source knots, uses binary
 * search otherwise, and caches parameters only for the last used interval.
 * Outside-domain queries use the first or final interval for extrapolation.
 *
 * Returns 0 for interpolation only, 1 if any query extrapolated, or 2 for an
 * internal interval lookup failure. Replaces query Y incrementally, never X;
 * outputs can be partially written on failure. The wrapper validates finite
 * results afterward. No allocation or callbacks.
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

/* Contract and caller ownership are documented in rbcInt.h. */
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

/* Numerical reference: Burden, Faires and Reynolds, Numerical Analysis (1981), p. 112. */
/* Contract and caller ownership are documented in rbcInt.h. */
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
                                         {{"parametric", 6, 6, "operation source result samples"}, NULL},
                                         {{"quadratic", 6, 6, "x y splx sply"}, Rbc_QuadraticSpline},
                                         {{NULL, 0, 0, NULL}, NULL}};

/**
 * @brief Look up a real vector for the spline command.
 * @ingroup spline_internals
 *
 * Returns TCL_OK for an existing real vector, otherwise TCL_ERROR with an
 * interpreter message. vecPtrPtr is borrowed and may already be assigned if the
 * type check fails. Does not create, resize, preserve or check writability.
 */
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

/**
 * @brief Look up a complex vector for the parametric command.
 * @ingroup spline_internals
 *
 * Returns TCL_OK for an existing complex vector, otherwise TCL_ERROR with an
 * interpreter message. vecPtrPtr is borrowed and may already be assigned if the
 * type check fails. Does not create, resize, preserve or check writability.
 */
static int GetComplexSplineVector(Tcl_Interp *interp, const char *name, Rbc_Vector **vecPtrPtr) {
    if (Rbc_GetVector(interp, name, vecPtrPtr) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Rbc_VectorGetType(*vecPtrPtr) != RBC_VECTOR_COMPLEX) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("spline vector \"%s\" must be complex", name));
        return TCL_ERROR;
    }
    return TCL_OK;
}

/**
 * @brief Copy completed points and commit a typed spline result.
 * @ingroup spline_internals
 *
 * Requires validated points and a real or complex type. Copies Y into real
 * storage, or X/Y into complex storage, before resolving or creating the output.
 * Existing outputs must match type and be writable. Installs the complete owned
 * buffer before custom old-storage release, array mapping/traces or notification.
 * Preserves record/interpreter lifetime and checks destruction after callbacks.
 *
 * New real outputs receive automatic mapping unless their names contain an
 * opening parenthesis. Mapping failure deletes only the newly created original
 * record, never a callback-created replacement. Returns TCL_OK with an empty
 * result or TCL_ERROR with diagnostics. Precommit failures leave an existing
 * output unchanged; callback mutations after commit are not rolled back.
 * The caller retains ownership of points.
 */
static int SplineStoreResult(Tcl_Interp *interp, const char *name, Rbc_VectorType type,
                             const Point2D *points, Tcl_Size length) {
    VectorObject *vPtr;
    Rbc_Vector *vector;
    void *data, *oldData;
    Tcl_FreeProc *oldFree;
    Tcl_InterpState state;
    size_t bytes;
    Tcl_Size i;
    int isNew = FALSE, result = TCL_OK;
    char *mapName = NULL;

    if (GetSplineArrayByteCount(length, type == RBC_VECTOR_REAL ? sizeof(double) : sizeof(Rbc_Complex),
                                &bytes) != TCL_OK) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("spline result is too large", -1));
        return TCL_ERROR;
    }
    data = Tcl_AttemptAlloc(bytes > 0 ? bytes : 1);
    if (data == NULL) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("can't allocate spline result", -1));
        return TCL_ERROR;
    }
    for (i = 0; i < length; i++) {
        if (type == RBC_VECTOR_REAL) {
            ((double *)data)[i] = points[i].y;
        } else {
            ((Rbc_Complex *)data)[i].real = points[i].x;
            ((Rbc_Complex *)data)[i].imag = points[i].y;
        }
    }
    if (Rbc_GetVector(interp, name, &vector) == TCL_OK) {
        vPtr = (VectorObject *)vector;
        if (Rbc_VectorCheckWritable(interp, vector) != TCL_OK) {
            ckfree(data);
            return TCL_ERROR;
        }
        if (vPtr->type != type) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("spline vector \"%s\" must be %s", name,
                                                  type == RBC_VECTOR_REAL ? "real" : "complex"));
            ckfree(data);
            return TCL_ERROR;
        }
    } else {
        Tcl_ResetResult(interp);
        vPtr = Rbc_VectorCreate(Rbc_VectorGetInterpData(interp), name, name, NULL, type, &isNew);
        if (vPtr == NULL) {
            ckfree(data);
            return TCL_ERROR;
        }
    }
    Tcl_Preserve(interp);
    Tcl_Preserve(vPtr);
    if (isNew && type == RBC_VECTOR_REAL && strchr(vPtr->name, '(') == NULL) {
        mapName = RbcStrdup(vPtr->name);
    }
    oldData = vPtr->data.raw;
    oldFree = vPtr->freeProc;
    vPtr->data.raw = data;
    vPtr->freeProc = TCL_DYNAMIC;
    vPtr->length = vPtr->size = length;
    vPtr->first = 0;
    vPtr->last = length - 1;
    vPtr->min = vPtr->max = rbcNaN;
    vPtr->minIndex = vPtr->maxIndex = -1;
    vPtr->notifyFlags |= UPDATE_RANGE;
    state = Tcl_SaveInterpState(interp, TCL_OK);
    if (oldData != NULL && oldFree != TCL_STATIC) {
        if (oldFree == TCL_DYNAMIC) ckfree(oldData);
        else oldFree(oldData);
    }
    if (!Tcl_InterpDeleted(interp) && !vPtr->destroying) {
        if (mapName != NULL) {
            result = Rbc_VectorMapVariable(interp, vPtr, mapName);
        } else if (vPtr->flush) {
            Rbc_VectorFlushCache(vPtr);
        }
        if (result == TCL_OK && !Tcl_InterpDeleted(interp) && !vPtr->destroying) {
            Rbc_VectorUpdateClients(vPtr);
        }
    }
    if (result == TCL_OK) {
        Tcl_RestoreInterpState(interp, state);
        Tcl_ResetResult(interp);
    } else {
        Tcl_DiscardInterpState(state);
        if (isNew && !vPtr->destroying) Rbc_VectorFree(vPtr);
    }
    if (mapName != NULL) ckfree(mapName);
    Tcl_Release(vPtr);
    Tcl_Release(interp);
    return result;
}

/**
 * @brief Evaluate the six-argument complex-vector spline command.
 * @ingroup spline_internals
 *
 * The dispatcher must validate objc before entry. Accepts natural or quadratic,
 * at least three finite source points without consecutive duplicates, and at
 * least two output samples. Copies source coordinates before computation, stages
 * the complete output and commits through SplineStoreResult only if the requested
 * sample count was produced. Supports source/result aliasing. Frees both scratch
 * arrays on every path; returns Tcl status and diagnostics.
 */
static int ParametricSplineObjCmd(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Vector *source;
    const Rbc_Complex *sourceArr;
    Point2D *origPts;
    Point2D *intpPts;
    const char *operation;
    const char *sourceName;
    const char *resultName;
    Tcl_Size nOrigPts;
    Tcl_Size nIntpPts;
    Tcl_Size nGenerated;
    Tcl_Size i;
    size_t origBytes;
    size_t intpBytes;
    int result;

    (void)objc;
    origPts = NULL;
    intpPts = NULL;
    result = TCL_ERROR;
    operation = Tcl_GetString(objv[2]);
    if ((strcmp(operation, "natural") != 0) && (strcmp(operation, "quadratic") != 0)) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("bad parametric spline operation \"%s\": "
                                               "must be natural or quadratic",
                                               operation));
        return TCL_ERROR;
    }
    sourceName = Tcl_GetString(objv[3]);
    resultName = Tcl_GetString(objv[4]);
    if (GetComplexSplineVector(interp, sourceName, &source) != TCL_OK) {
        return TCL_ERROR;
    }
    if (Tcl_GetSizeIntFromObj(interp, objv[5], &nIntpPts) != TCL_OK) {
        return TCL_ERROR;
    }
    if (nIntpPts < 2) {
        Tcl_SetObjResult(interp, Tcl_NewStringObj("number of parametric spline samples must be >= 2", -1));
        return TCL_ERROR;
    }
    nOrigPts = Rbc_VectorLength(source);
    if (nOrigPts < 3) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("length of vector \"%s\" is < 3", sourceName));
        return TCL_ERROR;
    }
    /*
     * Validate the complete source before creating or modifying the
     * result vector.
     */
    sourceArr = Rbc_VectorComplexData(source);
    for (i = 0; i < nOrigPts; i++) {
        if ((!FINITE(sourceArr[i].real)) || (!FINITE(sourceArr[i].imag))) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector \"%s\" must contain only finite values", sourceName));
            return TCL_ERROR;
        }
    }
    /*
     * Chord-length parameterization requires every interval to have
     * non-zero length.
     */
    for (i = 1; i < nOrigPts; i++) {
        if ((sourceArr[i].real == sourceArr[i - 1].real) && (sourceArr[i].imag == sourceArr[i - 1].imag)) {
            Tcl_SetObjResult(interp, Tcl_ObjPrintf("vector \"%s\" contains consecutive duplicate points", sourceName));
            return TCL_ERROR;
        }
    }
    /*
     * Make private copies before touching the result vector.  This
     * makes source/result aliasing safe.
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
    intpPts = Tcl_AttemptAlloc(intpBytes);
    if (intpPts == NULL) {
        Tcl_SetObjResult(interp,
                         Tcl_ObjPrintf("can't allocate %" TCL_SIZE_MODIFIER "d interpolation points", nIntpPts));
        goto cleanup;
    }
    for (i = 0; i < nOrigPts; i++) {
        origPts[i].x = sourceArr[i].real;
        origPts[i].y = sourceArr[i].imag;
    }
    /*
     * Complex-vector splines use the ordinary Euclidean metric in
     * the complex plane.  Real and imaginary coordinates therefore
     * have equal scale.
     */
    if (strcmp(operation, "natural") == 0) {
        nGenerated = NaturalParametricSplineCore(origPts, nOrigPts, 1.0, 1.0, FALSE, intpPts, nIntpPts);
    } else {
        nGenerated = QuadraticParametricSplineCore(origPts, nOrigPts, intpPts, nIntpPts);
    }
    if (nGenerated != nIntpPts) {
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("error generating parametric spline for \"%s\"", resultName));
        goto cleanup;
    }
    result = SplineStoreResult(interp, resultName, RBC_VECTOR_COMPLEX, intpPts, nIntpPts);

cleanup:
    if (intpPts != NULL) {
        ckfree(intpPts);
    }
    if (origPts != NULL) {
        ckfree(origPts);
    }
    return result;
}

/**
 * @brief Dispatch Cartesian or parametric Tcl spline operations.
 * @ingroup spline_internals
 *
 * Validates operation/arity through splineOps. Cartesian inputs are real vectors
 * with equal X/Y lengths, at least three finite source points, strictly increasing
 * X, and finite query X in any order. Empty queries are supported. Copies all
 * source/query values before running a kernel, so output may alias any input.
 *
 * Commits only a completely successful numerical result through SplineStoreResult;
 * all temporary arrays remain owned by this call and are released on exit.
 * Returns TCL_OK with an empty result or TCL_ERROR with diagnostics. clientData
 * is unused. Parametric operations delegate after argument-count validation.
 */
static int SplineObjCmd(ClientData clientData, Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[]) {
    Rbc_Vector *x;
    Rbc_Vector *y;
    Rbc_Vector *splX;
    Rbc_SplineOp proc;
    const char *xName;
    const char *yName;
    const char *splXName;
    const char *splYName;
    const double *xArr;
    const double *yArr;
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
    if (proc == NULL) {
        return ParametricSplineObjCmd(interp, objc, objv);
    }
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
    result = SplineStoreResult(interp, splYName, RBC_VECTOR_REAL, intpPts, nIntpPts);

cleanup:
    if (intpPts != NULL) {
        ckfree(intpPts);
    }
    if (origPts != NULL) {
        ckfree(origPts);
    }
    return result;
}

/**
 * @brief Register the Tcl spline command.
 * @ingroup spline_internals
 *
 * Creates ::rbc::spline with SplineObjCmd and no client data or delete callback.
 * Returns TCL_OK. Package initialization supplies the namespace; this routine
 * allocates no persistent spline workspace.
 */
int Rbc_SplineInit(Tcl_Interp *interp) {
    Tcl_CreateObjCommand2(interp, "::rbc::spline", SplineObjCmd, NULL, NULL);
    return TCL_OK;
}

/**
 * @brief Factor a cyclic symmetric tridiagonal system in place.
 * @ingroup spline_internals
 *
 * A stores lower/diagonal/upper entries in slots 0/1/2, including the cyclic
 * corner entries A[0][0] and A[n-1][2]. Requires symmetric positive-definite input
 * and n >= 1. Forms C-transpose * D * C with unit-diagonal upper C, overwriting A.
 * Returns FALSE on detected invalid pivots and may leave partial factors.
 * No allocation; later stages still must validate numerical finiteness.
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

/**
 * @brief Solve two right-hand sides using the cyclic factors.
 * @ingroup spline_internals
 *
 * Requires factors from successful SolveCubic1 and nIntervals >= 1. Forward,
 * diagonal and backward substitution replace spline[].x and spline[].y with
 * solutions, leaving t unchanged. Does not allocate, check finiteness or return
 * status; CubicSlopes validates the solved values before using them.
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

/**
 * @brief Build chord lengths and limited parametric cubic derivatives.
 * @ingroup spline_internals
 *
 * Requires at least three finite points and positive finite unitX/unitY.
 * Closed paths must already repeat the first point at the end. Divides coordinate
 * differences by those units for the chord metric and rejects lengths no greater
 * than DBL_EPSILON. Builds and solves a cyclic or open tridiagonal system.
 *
 * The historical construction limits the scaled derivative right-hand-side norm
 * to 8.5. Open endpoints copy adjacent solved derivatives rather than imposing
 * zero second derivatives; this is distinct from the Cartesian natural kernel.
 * Returns an owned nPoints CubicSpline array to release with ckfree, or NULL on
 * validation, allocation or detected arithmetic failure. Only the first
 * nPoints-1 t entries are interval lengths. Inputs are never modified.
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
     * Reject arithmetic overflow in the n solved second derivatives.
     * Endpoint derivatives are populated below; the remaining entries
     * do not yet all contain initialized values.
     */
    for (i = 0; i < n; i++) {
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

/**
 * @brief Evaluate one parametric cubic at local chord distance.
 * @ingroup spline_internals
 *
 * Caller guarantees valid interval and interval+1 indices in origPts/spline.
 * Validates positive finite interval length and local t in [0,length], preserving
 * endpoint coordinates exactly. Writes pointPtr and returns finite-result status;
 * FALSE can follow non-finite output writes. No allocation or vector callbacks.
 */
static int CubicEvalPoint(const Point2D origPts[], const CubicSpline spline[], Tcl_Size interval, double t,
                          Point2D *pointPtr) {
    Point2D p;
    Point2D q;
    double d;
    double hx;
    double hy;
    double dx0;
    double dy0;
    double dx01;
    double dy01;

    p = origPts[interval];
    q = origPts[interval + 1];
    d = spline[interval].t;
    if ((!FINITE(d)) || (d <= 0.0) || (!FINITE(t)) || (t < 0.0) || (t > d)) {
        return FALSE;
    }
    /*
     * Preserve the original knots exactly.
     */
    if (t == 0.0) {
        *pointPtr = p;
        return TRUE;
    }
    if (t == d) {
        *pointPtr = q;
        return TRUE;
    }
    hx = (q.x - p.x) / d;
    hy = (q.y - p.y) / d;
    dx0 = (spline[interval + 1].x + 2.0 * spline[interval].x) / 6.0;
    dy0 = (spline[interval + 1].y + 2.0 * spline[interval].y) / 6.0;
    dx01 = (spline[interval + 1].x - spline[interval].x) / (6.0 * d);
    dy01 = (spline[interval + 1].y - spline[interval].y) / (6.0 * d);
    pointPtr->x = p.x + t * (hx + (t - d) * (dx0 + t * dx01));
    pointPtr->y = p.y + t * (hy + (t - d) * (dy0 + t * dy01));
    return FINITE(pointPtr->x) && FINITE(pointPtr->y);
}

/**
 * @brief Sample a cubic path uniformly in its chord parameter.
 * @ingroup spline_internals
 *
 * Requires non-NULL arrays, at least two source points and two output slots,
 * and spline interval lengths matching the source. Validates the accumulated
 * length, then uses (1-1e-7)*total/(samples-1) to avoid losing the last sample.
 * Returns the number generated, or zero for detected failure. Failure can leave
 * partial output; the caller must compare the count with its request. Does not
 * restore the exact final endpoint (NaturalParametricSplineCore does that).
 * No allocation; output must not overwrite source or coefficient storage.
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
        double d;

        d = spline[i].t;
        while ((t <= d) && (count < nIntpPts)) {
            Point2D value;

            if (!CubicEvalPoint(origPts, spline, i, t, &value)) {
                return 0;
            }
            intpPts[count++] = value;
            t += tSkip;
        }
        t -= d;
    }
    return count;
}

int Rbc_NaturalParametricSplineEval(const Point2D origPts[], Tcl_Size nOrigPts, Point2D intpPts[], Tcl_Size nIntpPts) {
    CubicSpline *spline;
    Point2D *resultPts;
    Tcl_Size i;
    size_t resultBytes;
    int result;

    spline = NULL;
    resultPts = NULL;
    result = FALSE;
    if ((origPts == NULL) || (intpPts == NULL) || (nOrigPts < 3) || (nIntpPts < 0) ||
        !SplinePointsAreFinite(origPts, nOrigPts)) {
        return FALSE;
    }
    /*
     * Validate every evaluation parameter before modifying intpPts.
     *
     * x = source interval number
     * y = normalized position in the interval.
     *
     * The final source point uses interval nOrigPts - 1, t == 0.
     */
    for (i = 0; i < nIntpPts; i++) {
        double intervalValue;
        double t;

        intervalValue = intpPts[i].x;
        t = intpPts[i].y;
        if ((!FINITE(intervalValue)) || (!FINITE(t)) || (intervalValue < 0.0) ||
            (intervalValue > (double)(nOrigPts - 1)) || (floor(intervalValue) != intervalValue) || (t < 0.0) ||
            (t > 1.0) || ((intervalValue == (double)(nOrigPts - 1)) && (t != 0.0))) {
            return FALSE;
        }
    }
    if (nIntpPts == 0) {
        return TRUE;
    }
    if (GetSplineArrayByteCount(nIntpPts, sizeof(*resultPts), &resultBytes) != TCL_OK) {
        return FALSE;
    }
    resultPts = Tcl_AttemptAlloc(resultBytes);
    if (resultPts == NULL) {
        return FALSE;
    }
    /*
     * origPts are already mapped screen coordinates here, so use
     * isotropic pixel units.
     */
    spline = CubicSlopes(origPts, nOrigPts, FALSE, 1.0, 1.0);
    if (spline == NULL) {
        goto cleanup;
    }
    for (i = 0; i < nIntpPts; i++) {
        Tcl_Size interval;
        double fraction;
        double localT;

        interval = (Tcl_Size)intpPts[i].x;
        fraction = intpPts[i].y;
        if (interval == nOrigPts - 1) {
            resultPts[i] = origPts[nOrigPts - 1];
            continue;
        }
        if (fraction == 0.0) {
            resultPts[i] = origPts[interval];
            continue;
        }
        if (fraction == 1.0) {
            resultPts[i] = origPts[interval + 1];
            continue;
        }
        localT = fraction * spline[interval].t;
        if ((!FINITE(localT)) || (!CubicEvalPoint(origPts, spline, interval, localT, resultPts + i))) {
            goto cleanup;
        }
    }
    memcpy(intpPts, resultPts, (size_t)nIntpPts * sizeof(*intpPts));
    result = TRUE;

cleanup:
    if (spline != NULL) {
        ckfree(spline);
    }
    if (resultPts != NULL) {
        ckfree(resultPts);
    }
    return result;
}

int Rbc_QuadraticParametricSplineEval(const Point2D origPts[], Tcl_Size nOrigPts, Point2D intpPts[],
                                      Tcl_Size nIntpPts) {
    Point2D *realPts;
    Point2D *imagPts;
    Point2D *realEval;
    Point2D *imagEval;
    Point2D *resultPts;
    Tcl_Size i;
    size_t sourceBytes;
    size_t evalBytes;
    double tMax;
    int result;

    realPts = NULL;
    imagPts = NULL;
    realEval = NULL;
    imagEval = NULL;
    resultPts = NULL;
    result = FALSE;
    if ((origPts == NULL) || (intpPts == NULL) || (nOrigPts < 3) || (nIntpPts < 0) ||
        !SplinePointsAreFinite(origPts, nOrigPts)) {
        return FALSE;
    }
    /*
     * Validate all interval parameters before modifying anything.
     */
    for (i = 0; i < nIntpPts; i++) {
        double intervalValue;
        double fraction;

        intervalValue = intpPts[i].x;
        fraction = intpPts[i].y;
        if ((!FINITE(intervalValue)) || (!FINITE(fraction)) || (intervalValue < 0.0) ||
            (intervalValue > (double)(nOrigPts - 1)) || (floor(intervalValue) != intervalValue) || (fraction < 0.0) ||
            (fraction > 1.0) || ((intervalValue == (double)(nOrigPts - 1)) && (fraction != 0.0))) {
            return FALSE;
        }
    }
    if (nIntpPts == 0) {
        return TRUE;
    }
    if ((GetSplineArrayByteCount(nOrigPts, sizeof(*realPts), &sourceBytes) != TCL_OK) ||
        (GetSplineArrayByteCount(nIntpPts, sizeof(*realEval), &evalBytes) != TCL_OK)) {
        return FALSE;
    }
    realPts = Tcl_AttemptAlloc(sourceBytes);
    if (realPts == NULL) {
        goto cleanup;
    }
    imagPts = Tcl_AttemptAlloc(sourceBytes);
    if (imagPts == NULL) {
        goto cleanup;
    }
    realEval = Tcl_AttemptAlloc(evalBytes);
    if (realEval == NULL) {
        goto cleanup;
    }
    imagEval = Tcl_AttemptAlloc(evalBytes);
    if (imagEval == NULL) {
        goto cleanup;
    }
    resultPts = Tcl_AttemptAlloc(evalBytes);
    if (resultPts == NULL) {
        goto cleanup;
    }
    /*
     * Construct the common chord-length parameter.
     */
    tMax = 0.0;
    realPts[0].x = 0.0;
    realPts[0].y = origPts[0].x;
    imagPts[0].x = 0.0;
    imagPts[0].y = origPts[0].y;
    for (i = 1; i < nOrigPts; i++) {
        double dx;
        double dy;
        double dt;
        double nextT;

        dx = origPts[i].x - origPts[i - 1].x;
        dy = origPts[i].y - origPts[i - 1].y;
        dt = hypot(dx, dy);
        if ((!FINITE(dt)) || (dt <= DBL_EPSILON)) {
            goto cleanup;
        }
        if (tMax > DBL_MAX - dt) {
            goto cleanup;
        }
        nextT = tMax + dt;
        if ((!FINITE(nextT)) || (!(nextT > tMax))) {
            goto cleanup;
        }
        tMax = nextT;
        realPts[i].x = tMax;
        realPts[i].y = origPts[i].x;
        imagPts[i].x = tMax;
        imagPts[i].y = origPts[i].y;
    }
    /*
     * Convert interval/fraction parameters to the common absolute
     * chord-length parameter.
     */
    for (i = 0; i < nIntpPts; i++) {
        Tcl_Size interval;
        double fraction;
        double t;

        interval = (Tcl_Size)intpPts[i].x;
        fraction = intpPts[i].y;
        if (interval == nOrigPts - 1) {
            t = tMax;
        } else {
            double left;
            double right;

            left = realPts[interval].x;
            right = realPts[interval + 1].x;
            t = left + fraction * (right - left);
        }
        if (!FINITE(t)) {
            goto cleanup;
        }
        realEval[i].x = t;
        realEval[i].y = 0.0;
        imagEval[i].x = t;
        imagEval[i].y = 0.0;
    }
    if (!Rbc_QuadraticSpline(realPts, nOrigPts, realEval, nIntpPts)) {
        goto cleanup;
    }
    if (!Rbc_QuadraticSpline(imagPts, nOrigPts, imagEval, nIntpPts)) {
        goto cleanup;
    }
    for (i = 0; i < nIntpPts; i++) {
        Tcl_Size interval;
        double fraction;

        interval = (Tcl_Size)intpPts[i].x;
        fraction = intpPts[i].y;
        /*
         * Preserve source knots exactly.
         */
        if (interval == nOrigPts - 1) {
            resultPts[i] = origPts[nOrigPts - 1];
        } else if (fraction == 0.0) {
            resultPts[i] = origPts[interval];
        } else if (fraction == 1.0) {
            resultPts[i] = origPts[interval + 1];
        } else {
            resultPts[i].x = realEval[i].y;
            resultPts[i].y = imagEval[i].y;
            if ((!FINITE(resultPts[i].x)) || (!FINITE(resultPts[i].y))) {
                goto cleanup;
            }
        }
    }
    memcpy(intpPts, resultPts, (size_t)nIntpPts * sizeof(*intpPts));
    result = TRUE;

cleanup:
    if (resultPts != NULL) {
        ckfree(resultPts);
    }
    if (imagEval != NULL) {
        ckfree(imagEval);
    }
    if (realEval != NULL) {
        ckfree(realEval);
    }
    if (imagPts != NULL) {
        ckfree(imagPts);
    }
    if (realPts != NULL) {
        ckfree(realPts);
    }
    return result;
}

/**
 * @brief Sample coordinate splines on a shared Euclidean chord parameter.
 * @ingroup spline_internals
 *
 * Requires at least three finite source points and two output slots. Allocates
 * private coordinate/imaginary-query arrays, rejects degenerate chords and
 * non-increasing cumulative parameters, and evaluates both quadratic coordinates.
 * Uses intpPts itself as real-query workspace before converting to X/Y, so a zero
 * failure result may leave temporary parameters or partial coordinates there.
 * Returns nIntpPts on success and restores both endpoints exactly. Releases all
 * private arrays; caller-owned source and output must not overlap.
 */
static Tcl_Size QuadraticParametricSplineCore(const Point2D origPts[], Tcl_Size nOrigPts, Point2D intpPts[],
                                              Tcl_Size nIntpPts) {
    Point2D *realPts;
    Point2D *imagPts;
    Point2D *imagEval;
    Tcl_Size i;
    Tcl_Size result;
    size_t sourceBytes;
    size_t evalBytes;
    double tMax;

    realPts = NULL;
    imagPts = NULL;
    imagEval = NULL;
    result = 0;
    if ((origPts == NULL) || (intpPts == NULL) || (nOrigPts < 3) || (nIntpPts < 2) ||
        !SplinePointsAreFinite(origPts, nOrigPts)) {
        return 0;
    }
    if ((GetSplineArrayByteCount(nOrigPts, sizeof(*realPts), &sourceBytes) != TCL_OK) ||
        (GetSplineArrayByteCount(nIntpPts, sizeof(*imagEval), &evalBytes) != TCL_OK)) {
        return 0;
    }
    realPts = Tcl_AttemptAlloc(sourceBytes);
    if (realPts == NULL) {
        goto cleanup;
    }
    imagPts = Tcl_AttemptAlloc(sourceBytes);
    if (imagPts == NULL) {
        goto cleanup;
    }
    imagEval = Tcl_AttemptAlloc(evalBytes);
    if (imagEval == NULL) {
        goto cleanup;
    }
    /*
     * Build a strictly increasing chord-length parameter.
     *
     * Both coordinate splines use this same parameter:
     *
     *     real = f(t)
     *     imag = g(t)
     */
    tMax = 0.0;
    realPts[0].x = 0.0;
    realPts[0].y = origPts[0].x;
    imagPts[0].x = 0.0;
    imagPts[0].y = origPts[0].y;
    for (i = 1; i < nOrigPts; i++) {
        double dx;
        double dy;
        double dt;
        double nextT;

        dx = origPts[i].x - origPts[i - 1].x;
        dy = origPts[i].y - origPts[i - 1].y;
        dt = hypot(dx, dy);
        if ((!FINITE(dt)) || (dt <= DBL_EPSILON)) {
            goto cleanup;
        }
        if (tMax > DBL_MAX - dt) {
            goto cleanup;
        }
        nextT = tMax + dt;
        if ((!FINITE(nextT)) || (!(nextT > tMax))) {
            goto cleanup;
        }
        tMax = nextT;
        realPts[i].x = tMax;
        realPts[i].y = origPts[i].x;
        imagPts[i].x = tMax;
        imagPts[i].y = origPts[i].y;
    }
    if ((!FINITE(tMax)) || (tMax <= 0.0)) {
        goto cleanup;
    }
    /*
     * Evaluate uniformly in chord-length parameter space.
     *
     * Calculate the fraction first so the multiplication cannot
     * exceed tMax merely because of an intermediate i*tMax.
     */
    for (i = 0; i < nIntpPts; i++) {
        double t;

        if (i == nIntpPts - 1) {
            t = tMax;
        } else {
            double fraction;

            fraction = (double)i / (double)(nIntpPts - 1);
            t = tMax * fraction;
        }
        if (!FINITE(t)) {
            goto cleanup;
        }
        intpPts[i].x = t;
        intpPts[i].y = 0.0;
        imagEval[i].x = t;
        imagEval[i].y = 0.0;
    }
    /*
     * Rbc_QuadraticSpline treats Point2D.x as the independent
     * coordinate and writes the interpolated value to Point2D.y.
     */
    if (!Rbc_QuadraticSpline(realPts, nOrigPts, intpPts, nIntpPts)) {
        goto cleanup;
    }
    if (!Rbc_QuadraticSpline(imagPts, nOrigPts, imagEval, nIntpPts)) {
        goto cleanup;
    }
    /*
     * intpPts currently contains:
     *
     *     x = t
     *     y = interpolated real
     *
     * Convert it into ordinary 2-D coordinates.
     */
    for (i = 0; i < nIntpPts; i++) {
        double real;
        double imag;

        real = intpPts[i].y;
        imag = imagEval[i].y;
        if ((!FINITE(real)) || (!FINITE(imag))) {
            goto cleanup;
        }
        intpPts[i].x = real;
        intpPts[i].y = imag;
    }
    /*
     * Preserve endpoints exactly.
     */
    intpPts[0] = origPts[0];
    intpPts[nIntpPts - 1] = origPts[nOrigPts - 1];
    result = nIntpPts;

cleanup:
    if (imagEval != NULL) {
        ckfree(imagEval);
    }
    if (imagPts != NULL) {
        ckfree(imagPts);
    }
    if (realPts != NULL) {
        ckfree(realPts);
    }
    return result;
}

/**
 * @brief Sample an open or closed limited cubic path.
 * @ingroup spline_internals
 *
 * Requires at least three finite source points, two output slots and positive
 * finite metric units. Appends a private closing point if requested and missing,
 * then owns/reclaims CubicSlopes workspace. Returns the CubicEval count (zero on
 * detected failure); restores the last endpoint exactly only for a full result.
 * Output may be partially written on failure and must not overlap input. No
 * vector lookup, notification or Tcl callback occurs.
 */
static Tcl_Size NaturalParametricSplineCore(const Point2D origPts[], Tcl_Size nOrigPts, double unitX, double unitY,
                                            int isClosed, Point2D intpPts[], Tcl_Size nIntpPts) {
    const Point2D *workPts;
    Point2D *ownedPts;
    CubicSpline *spline;
    Tcl_Size workCount;
    Tcl_Size result;

    workPts = origPts;
    ownedPts = NULL;
    spline = NULL;
    workCount = nOrigPts;
    result = 0;
    if ((origPts == NULL) || (intpPts == NULL) || (nOrigPts < 3) || (nIntpPts < 2) || (!FINITE(unitX)) ||
        (!FINITE(unitY)) || (unitX <= 0.0) || (unitY <= 0.0) || !SplinePointsAreFinite(origPts, nOrigPts)) {
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
    spline = CubicSlopes(workPts, workCount, isClosed, unitX, unitY);
    if (spline != NULL) {
        result = CubicEval(workPts, workCount, intpPts, nIntpPts, spline);

        /*
         * CubicEval deliberately undershoots the total parameter
         * length very slightly to avoid floating-point loss of the
         * final sample.  When the requested number of samples was
         * produced, make the endpoint exact.
         */
        if (result == nIntpPts) {
            intpPts[nIntpPts - 1] = workPts[workCount - 1];
        }
    }
    if (spline != NULL) {
        ckfree(spline);
    }
    if (ownedPts != NULL) {
        ckfree(ownedPts);
    }
    return result;
}

Tcl_Size Rbc_NaturalParametricSpline(const Point2D origPts[], Tcl_Size nOrigPts, const Extents2D *extsPtr, int isClosed,
                                     Point2D intpPts[], Tcl_Size nIntpPts) {
    double unitX;
    double unitY;

    if (extsPtr == NULL) {
        return 0;
    }
    unitX = fabs(extsPtr->right - extsPtr->left);
    unitY = fabs(extsPtr->bottom - extsPtr->top);
    if ((!FINITE(unitX)) || (unitX < FLT_EPSILON)) {
        unitX = FLT_EPSILON;
    }
    if ((!FINITE(unitY)) || (unitY < FLT_EPSILON)) {
        unitY = FLT_EPSILON;
    }
    return NaturalParametricSplineCore(origPts, nOrigPts, unitX, unitY, isClosed, intpPts, nIntpPts);
}

/**
 * @brief Build twice-scaled Catmull-Rom polynomial coefficients.
 * @ingroup spline_internals
 *
 * Reads four consecutive control points and writes X/Y coefficients to a, b,
 * c and d. The caller evaluates (d+t*(c+t*(b+t*a)))/2. Output records must be
 * distinct and must not overlap control points. No validation or allocation;
 * the caller handles non-finite evaluation results.
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

/* Contract and caller ownership are documented in rbcInt.h. */
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
     * Validation and allocation are complete. Evaluation below can still
     * fail on non-finite arithmetic after replacing some parameters; callers
     * must discard output when this routine returns FALSE.
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
