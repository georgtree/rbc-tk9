/*
 * rbcGrMisc.c --
 *
 *      This module implements miscellaneous routines for the rbc
 *      graph widget.
 *
 * Copyright (c) 2009 Samuel Green, Nicholas Hudson, Stanton Sievers, Jarrod Stormo
 * All rights reserved.
 *
 * See "license.terms" for details.
 */

#include "rbcGraph.h"
#include <X11/Xutil.h>

#include <math.h>
#include <stdarg.h>

static Tk_OptionParseProc StringToColorPair;
static Tk_OptionPrintProc ColorPairToString;
Tk_CustomOption rbcColorPairOption = {StringToColorPair, ColorPairToString, (ClientData)0};

static int GetColorPair(Tcl_Interp *interp, Tk_Window tkwin, const char *fgStr, const char *bgStr, ColorPair *pairPtr,
                        int allowDefault);
static const char *NameOfColor(XColor *colorPtr);
static int ClipTest(double ds, double dr, double *t1, double *t2);
static double FindSplit(const Point2D points[], Tcl_Size i, Tcl_Size j, Tcl_Size *split);

/* ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetXY --
 *
 *      Parses a position in the form "@x,y".
 *
 * Parameters:
 *      Tcl_Interp *interp - Interpreter used for error reporting.
 *      Tk_Window tkwin    - Window used to convert Tk distance units.
 *      const char *string - Position string.
 *      int *xPtr          - Receives the X coordinate.
 *      int *yPtr          - Receives the Y coordinate.
 *
 * Results:
 *      Returns TCL_OK and stores the converted coordinates in xPtr
 *      and yPtr on success.  Returns TCL_ERROR and sets the interpreter
 *      result if the position is malformed or cannot be converted.
 *
 * Side Effects:
 *      May set the interpreter result.
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetXY(Tcl_Interp *interp, Tk_Window tkwin, const char *string, int *xPtr, int *yPtr) {
    const char *comma;
    Tcl_DString xString;
    int result;
    int x, y;

    if ((string == NULL) || (*string == '\0')) {
        *xPtr = *yPtr = -SHRT_MAX;
        return TCL_OK;
    }
    if (*string != '@') {
        goto badFormat;
    }
    comma = strchr(string + 1, ',');
    if (comma == NULL) {
        goto badFormat;
    }
    /*
     * Copy the X component because string is read-only.  The previous
     * implementation temporarily replaced the comma with a NUL byte.
     */
    Tcl_DStringInit(&xString);
    Tcl_DStringAppend(&xString, string + 1, (Tcl_Size)(comma - (string + 1)));
    result = Tk_GetPixels(interp, tkwin, Tcl_DStringValue(&xString), &x);
    Tcl_DStringFree(&xString);
    if (result == TCL_OK) {
        result = Tk_GetPixels(interp, tkwin, comma + 1, &y);
    }
    if (result != TCL_OK) {
        Rbc_AppendResultStrings(interp, ": can't parse position \"", string, "\"", (char *)NULL);
        return TCL_ERROR;
    }
    *xPtr = x;
    *yPtr = y;
    return TCL_OK;

badFormat:
    Rbc_AppendResultStrings(interp, "bad position \"", string, "\": should be \"@x,y\"", (char *)NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * GetColorPair --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp 
 *      Tk_Window tkwin
 *      const char *fgStr
 *      const char *bgStr
 *      ColorPair *pairPtr
 *      int allowDefault
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int GetColorPair(Tcl_Interp *interp, Tk_Window tkwin, const char *fgStr, const char *bgStr, ColorPair *pairPtr,
                        int allowDefault) {
    size_t length;
    XColor *fgColor, *bgColor;

    length = strlen(fgStr);
    if (fgStr[0] == '\0') {
        fgColor = NULL;
    } else if ((allowDefault) && (fgStr[0] == 'd') && (strncmp(fgStr, "defcolor", length) == 0)) {
        fgColor = COLOR_DEFAULT;
    } else {
        fgColor = Tk_GetColor(interp, tkwin, Tk_GetUid(fgStr));
        if (fgColor == NULL) {
            return TCL_ERROR;
        }
    }
    length = strlen(bgStr);
    if (bgStr[0] == '\0') {
        bgColor = NULL;
    } else if ((allowDefault) && (bgStr[0] == 'd') && (strncmp(bgStr, "defcolor", length) == 0)) {
        bgColor = COLOR_DEFAULT;
    } else {
        bgColor = Tk_GetColor(interp, tkwin, Tk_GetUid(bgStr));
        if (bgColor == NULL) {
            return TCL_ERROR;
        }
    }
    pairPtr->fgColor = fgColor;
    pairPtr->bgColor = bgColor;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreeColorPair --
 *
 *      TODO: Description
 *
 * Parameters:
 *      ColorPair *pairPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreeColorPair(ColorPair *pairPtr) {
    if ((pairPtr->bgColor != NULL) && (pairPtr->bgColor != COLOR_DEFAULT)) {
        Tk_FreeColor(pairPtr->bgColor);
    }
    if ((pairPtr->fgColor != NULL) && (pairPtr->fgColor != COLOR_DEFAULT)) {
        Tk_FreeColor(pairPtr->fgColor);
    }
    pairPtr->bgColor = pairPtr->fgColor = NULL;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToColorPair --
 *
 *      Convert the color names into pair of XColor pointers.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - String representing color
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Offset of color field in record
 *
 * Results:
 *      A standard Tcl result.  The color pointer is written into the
 *      widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToColorPair(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string,
                             char *widgRec, Tcl_Size offset) {
    ColorPair *pairPtr = (ColorPair *)(widgRec + offset);
    ColorPair sample;
    Tcl_Size allowDefault = (Tcl_Size)clientData;

    sample.fgColor = sample.bgColor = NULL;
    if ((string != NULL) && (*string != '\0')) {
        Tcl_Size nColors;
        const char **colors;
        int result;

        if (Tcl_SplitList(interp, string, &nColors, &colors) != TCL_OK) {
            return TCL_ERROR;
        }
        switch (nColors) {
        case 0:
            result = TCL_OK;
            break;
        case 1:
            result = GetColorPair(interp, tkwin, colors[0], "", &sample, (int)allowDefault);
            break;
        case 2:
            result = GetColorPair(interp, tkwin, colors[0], colors[1], &sample, (int)allowDefault);
            break;
        default:
            result = TCL_ERROR;
            Rbc_AppendResultStrings(interp, "too many names in colors list", (char *)NULL);
        }
        ckfree((char *)colors);
        if (result != TCL_OK) {
            return TCL_ERROR;
        }
    }
    Rbc_FreeColorPair(pairPtr);
    *pairPtr = sample;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * NameOfColor --
 *
 *      Convert the color option value into a string.
 *
 * Parameters:
 *      XColor *colorPtr
 *
 * Results:
 *      The static string representing the color option is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *NameOfColor(XColor *colorPtr) {
    if (colorPtr == NULL) {
        return "";
    } else if (colorPtr == COLOR_DEFAULT) {
        return "defcolor";
    } else {
        return Tk_NameOfColor(colorPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * ColorPairToString --
 *
 *      Convert the color pairs into color names.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Element information record
 *      Tcl_Size offset - Offset of symbol type field in record
 *      Tcl_FreeProc **freeProcPtr - Not used.
 *
 * Results:
 *      The string representing the symbol color is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *ColorPairToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                     Tcl_FreeProc **freeProcPtr) {
    ColorPair *pairPtr = (ColorPair *)(widgRec + offset);
    Tcl_DString dString;
    char *result;

    Tcl_DStringInit(&dString);
    Tcl_DStringAppendElement(&dString, NameOfColor(pairPtr->fgColor));
    Tcl_DStringAppendElement(&dString, NameOfColor(pairPtr->bgColor));
    result = Tcl_DStringValue(&dString);
    if (result == dString.staticSpace) {
        result = RbcStrdup(result);
    }
    *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    return result;
}

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
 * Rbc_GraphExtents --
 *
 *      Generates a bounding box representing the plotting area of
 *      the graph. This data structure is used to clip the points and
 *      line segments of the line element.
 *
 *      The clip region is the plotting area plus such arbitrary extra
 *      space.  The reason we clip with a bounding box larger than the
 *      plot area is so that symbols will be drawn even if their center
 *      point isn't in the plotting area.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Extents2D *extsPtr
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      The bounding box is filled with the dimensions of the plotting
 *      area.
 *
 *----------------------------------------------------------------------
 */
void Rbc_GraphExtents(Graph *graphPtr, Extents2D *extsPtr) {
    extsPtr->left = (double)graphPtr->hOffset - (double)graphPtr->padX.side1;
    extsPtr->top = (double)graphPtr->vOffset - (double)graphPtr->padY.side1;
    extsPtr->right = (double)graphPtr->hOffset + (double)graphPtr->hRange + (double)graphPtr->padX.side2;
    extsPtr->bottom = (double)graphPtr->vOffset + (double)graphPtr->vRange + (double)graphPtr->padY.side2;
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

#define EPSILON FLT_EPSILON

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
        if (FABS(dx) < EPSILON) {
            dx = (p->x > extsPtr->left) ? -EPSILON : EPSILON;
        }
        if (FABS(dy) < EPSILON) {
            dy = (p->y > extsPtr->top) ? -EPSILON : EPSILON;
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

static int ClampScrollValue(long double value) {
    if (value > (long double)INT_MAX) {
        return INT_MAX;
    }
    if (value < (long double)INT_MIN) {
        return INT_MIN;
    }
    return (int)value;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_AdjustViewport --
 *
 *      Adjusts the offsets of the viewport according to the scroll mode.
 *      This is to accommodate both "listbox" and "canvas" style scrolling.
 *
 *      "canvas"    The viewport scrolls within the range of world
 *              coordinates.  This way the viewport always displays
 *              a full page of the world.  If the world is smaller
 *              than the viewport, then (bizarrely) the world and
 *              viewport are inverted so that the world moves up
 *              and down within the viewport.
 *
 *      "listbox"    The viewport can scroll beyond the range of world
 *              coordinates.  Every entry can be displayed at the
 *              top of the viewport.  This also means that the
 *              scrollbar thumb weirdly shrinks as the last entry
 *              is scrolled upward.
 *
 * Parameters:
 *      int offset
 *      int worldSize
 *      int windowSize
 *      int scrollUnits
 *      int scrollMode
 *
 * Results:
 *      The corrected offset is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_AdjustViewport(int offset, int worldSize, int windowSize, int scrollUnits, int scrollMode) {
    Tcl_WideInt newOffset;
    Tcl_WideInt world;
    Tcl_WideInt window;
    Tcl_WideInt units;

    newOffset = offset;
    world = worldSize;
    window = windowSize;
    units = scrollUnits;
    switch (scrollMode) {
    case RBC_SCROLL_MODE_CANVAS:
        /*
         * Canvas-style scrolling allows the world to be scrolled
         * within the window.
         */
        if (world < window) {
            if ((world - newOffset) > window) {
                newOffset = world - window;
            }
            if (newOffset > 0) {
                newOffset = 0;
            }
        } else {
            if ((newOffset + window) > world) {
                newOffset = world - window;
            }
            if (newOffset < 0) {
                newOffset = 0;
            }
        }
        break;
    case RBC_SCROLL_MODE_LISTBOX:
        if (newOffset >= world) {
            newOffset = world - units;
        }
        if (newOffset < 0) {
            newOffset = 0;
        }
        break;
    case RBC_SCROLL_MODE_HIERBOX:
        /*
         * Hierbox-style scrolling allows the world to be scrolled
         * within the window.
         */
        if ((newOffset + window) > world) {
            newOffset = world - window;
        }
        if (newOffset < 0) {
            newOffset = 0;
        }
        break;
    }
    if (newOffset > INT_MAX) {
        return INT_MAX;
    }
    if (newOffset < INT_MIN) {
        return INT_MIN;
    }
    return (int)newOffset;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetScrollInfoFromObj --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      int objc
 *      Tcl_Obj *const *objv
 *      int *offsetPtr
 *      int worldSize
 *      int windowSize
 *      int scrollUnits
 *      int scrollMode
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetScrollInfoFromObj(Tcl_Interp *interp, Tcl_Size objc, Tcl_Obj *const objv[], int *offsetPtr, int worldSize,
                             int windowSize, int scrollUnits, int scrollMode) {
    const char *string;
    size_t length;
    char c;
    int offset;
    int count;
    int delta;
    double fract;

    if ((objc < 1) || (objv == NULL)) {
        return TCL_ERROR;
    }
    offset = *offsetPtr;
    string = Tcl_GetString(objv[0]);
    c = string[0];
    length = strlen(string);
    if ((c == 's') && (strncmp(string, "scroll", length) == 0)) {
        if (objc != 3) {
            return TCL_ERROR;
        }
        /*
         * scroll number units|pages
         */
        if (Tcl_GetIntFromObj(interp, objv[1], &count) != TCL_OK) {
            return TCL_ERROR;
        }
        string = Tcl_GetString(objv[2]);
        c = string[0];
        length = strlen(string);
        if ((c == 'u') && (strncmp(string, "units", length) == 0)) {
            fract = (double)count * (double)scrollUnits;
        } else if ((c == 'p') && (strncmp(string, "pages", length) == 0)) {
            /*
             * A page is 90% of the visible window.
             */
            fract = (double)count * (double)windowSize * 0.9;
        } else {
            Rbc_AppendResultStrings(interp, "unknown \"scroll\" units \"", Tcl_GetString(objv[2]), "\"", (char *)NULL);
            return TCL_ERROR;
        }
        /*
         * Preserve the historical truncation of the scroll delta
         * before adding it to the current integer offset.
         */
        delta = ClampScrollValue((long double)fract);
        offset = ClampScrollValue((long double)offset + (long double)delta);

    } else if ((c == 'm') && (strncmp(string, "moveto", length) == 0)) {
        long double position;

        if (objc != 2) {
            return TCL_ERROR;
        }
        if (Tcl_GetDoubleFromObj(interp, objv[1], &fract) != TCL_OK) {
            return TCL_ERROR;
        }
        if (!isfinite(fract)) {
            Rbc_AppendResultStrings(interp, "bad scroll fraction \"", Tcl_GetString(objv[1]), "\": must be finite",
                             (char *)NULL);
            return TCL_ERROR;
        }
        position = (long double)worldSize * (long double)fract;
        offset = ClampScrollValue(position);
    } else {
        /*
         * Treat like "scroll units".
         */
        if (Tcl_GetIntFromObj(interp, objv[0], &count) != TCL_OK) {
            return TCL_ERROR;
        }
        fract = (double)count * (double)scrollUnits;
        delta = ClampScrollValue((long double)fract);
        offset = ClampScrollValue((long double)offset + (long double)delta);
    }
    *offsetPtr = Rbc_AdjustViewport(offset, worldSize, windowSize, scrollUnits, scrollMode);
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetPrivateGCFromDrawable --
 *
 *      Like Tk_GetGC, but doesn't share the GC with any other widget.
 *      This is needed because the certain GC parameters (like dashes)
 *      can not be set via XCreateGC, therefore there is no way for
 *      Tk's hashing mechanism to recognize that two such GCs differ.
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      unsigned long gcMask
 *      XGCValues *valuePtr
 *
 * Results:
 *      A new GC is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
GC Rbc_GetPrivateGCFromDrawable(Display *display, Drawable drawable, unsigned long gcMask, XGCValues *valuePtr) {
    return XCreateGC(display, drawable, gcMask, valuePtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetPrivateGC --
 *
 *      Like Tk_GetGC, but doesn't share the GC with any other widget.
 *      This is needed because the certain GC parameters (like dashes)
 *      can not be set via XCreateGC, therefore there is no way for
 *      Tk's hashing mechanism to recognize that two such GCs differ.
 *
 * Parameters:
 *      Tk_Window tkwin
 *      unsigned long gcMask
 *      XGCValues *valuePtr
 *
 * Results:
 *      A new GC is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
GC Rbc_GetPrivateGC(Tk_Window tkwin, unsigned long gcMask, XGCValues *valuePtr) {
    GC gc;
    Pixmap pixmap;
    Drawable drawable;
    Display *display;

    pixmap = None;
    drawable = Tk_WindowId(tkwin);
    display = Tk_Display(tkwin);

    if (drawable == None) {
        Drawable root;
        int depth;

        root = RootWindow(display, Tk_ScreenNumber(tkwin));
        depth = Tk_Depth(tkwin);

        if (depth == DefaultDepth(display, Tk_ScreenNumber(tkwin))) {
            drawable = root;
        } else {
            pixmap = Tk_GetPixmap(display, root, 1, 1, depth);
            drawable = pixmap;
        }
    }
    gc = Rbc_GetPrivateGCFromDrawable(display, drawable, gcMask, valuePtr);
    if (pixmap != None) {
        Tk_FreePixmap(display, pixmap);
    }
    return gc;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_FreePrivateGC --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      GC gc
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_FreePrivateGC(Display *display, GC gc) {
    /* Tk 9 removed Tk_FreeXId; previously on 8.6 it was a NOOP

    Tk_FreeXId(display, (XID) XGContextFromGC(gc));

    This is the comment in the empty body of Tk_FreeXId on 8.6

     * This does nothing, because the XC-MISC extension takes care of
     * freeing XIDs for us.  It has been a standard X11 extension for
     * about 15 years as of 2008.  Keith Packard and another X.org
     * developer suggested that we remove the previous code that used:
     * #define XLIB_ILLEGAL_ACCESS.
     */
    XFreeGC(display, gc);
}

static int GetDashCount(const Rbc_Dashes *dashesPtr) {
    int n;

    for (n = 0; n < RBC_MAX_DASH_VALUES; n++) {
        if (dashesPtr->values[n] == 0) {
            break;
        }
    }
    return n;
}

void Rbc_SetDashes(Display *display, GC gc, Rbc_Dashes *dashesPtr) {
    int nValues;

    nValues = GetDashCount(dashesPtr);
    XSetDashes(display, gc, dashesPtr->offset, (const char *)dashesPtr->values, nValues);
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_Draw2DSegments --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      GC gc
 *      register Segment2D *segPtr
 *      int nSegments
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_Draw2DSegments(Display *display, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size nSegments) {
    XSegment *xSegments;
    Extents2D protocolExtents;
    int maxSegments;

    if ((display == NULL) || (segments == NULL) || (nSegments <= 0)) {
        return;
    }
    maxSegments = Rbc_MaxRequestSize(display, sizeof(XSegment));
    if (maxSegments < 1) {
        return;
    }
    xSegments = Tcl_AttemptAlloc((size_t)maxSegments * sizeof(*xSegments));
    if (xSegments == NULL) {
        return;
    }
    /*
     * X11 PolySegment coordinates are signed 16-bit values.
     * Clip geometry to that representable region before converting
     * Point2D coordinates to XSegment coordinates.
     */
    protocolExtents.left = (double)SHRT_MIN;
    protocolExtents.right = (double)SHRT_MAX;
    protocolExtents.top = (double)SHRT_MIN;
    protocolExtents.bottom = (double)SHRT_MAX;
    while (nSegments > 0) {
        Tcl_Size remaining;
        int chunk;
        int nDrawn;
        int i;

        remaining = nSegments;
        chunk = (remaining > (Tcl_Size)maxSegments) ? maxSegments : (int)remaining;
        nDrawn = 0;
        for (i = 0; i < chunk; i++) {
            Segment2D clipped;

            if (!Rbc_LineRectClip(&protocolExtents, &segments[i].p, &segments[i].q, &clipped)) {
                continue;
            }
            xSegments[nDrawn].x1 = (short)clipped.p.x;
            xSegments[nDrawn].y1 = (short)clipped.p.y;
            xSegments[nDrawn].x2 = (short)clipped.q.x;
            xSegments[nDrawn].y2 = (short)clipped.q.y;
            nDrawn++;
        }
        if (nDrawn > 0) {
            XDrawSegments(display, drawable, gc, xSegments, nDrawn);
        }
        segments += chunk;
        nSegments -= chunk;
    }
    ckfree(xSegments);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_MaxRequestSize --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      unsigned int elemSize
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_MaxRequestSize(Display *display, unsigned int elemSize) {
    long size;

#ifdef HAVE_XEXTENDEDMAXREQUESTSIZE
    size = XExtendedMaxRequestSize(display);
    if (size == 0) {
        size = XMaxRequestSize(display);
    }
#else
    size = XMaxRequestSize(display);
#endif
    if ((elemSize == 0) || (size <= 4)) {
        return 0;
    }
    size -= 4;
    if (size > (LONG_MAX / 4)) {
        return 0;
    }
    size *= 4;
    if ((unsigned long)size / elemSize > INT_MAX) {
        return INT_MAX;
    }
    return (int)((unsigned long)size / elemSize);
}

#undef Rbc_Fill3DRectangle
/*
 *----------------------------------------------------------------------
 *
 * Rbc_Fill3DRectangle --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin - Window for which border was allocated.
 *      Drawable drawable - X window or pixmap in which to draw.
 *      Tk_3DBorder border - Token for border to draw.
 *      int x - -
 *      int y - -
 *      int width - -
 *      int height - Outside area of rectangular region. 
 *      int borderWidth - Desired width for border, in pixels. Border will be *inside* region.
 *      int relief - Indicates 3D effect: TK_RELIEF_FLAT, TK_RELIEF_RAISED, or TK_RELIEF_SUNKEN.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_Fill3DRectangle(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y, int width, int height,
                         int borderWidth, int relief) {
    if ((borderWidth > 1) && (width > 2) && (height > 2) &&
        ((relief == TK_RELIEF_SUNKEN) || (relief == TK_RELIEF_RAISED))) {
        GC lightGC, darkGC;
        int x2, y2;

        x2 = x + width - 1;
        y2 = y + height - 1;
        if (relief == TK_RELIEF_RAISED) {
            lightGC = Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC);
#ifdef WIN32
            darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
#else
            darkGC = DefaultGC(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
#endif
        } else {
#ifdef WIN32
            lightGC = Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC);
#else
            lightGC = DefaultGC(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
#endif
            darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC);
        }
        XDrawLine(Tk_Display(tkwin), drawable, lightGC, x, y, x2, y);
        XDrawLine(Tk_Display(tkwin), drawable, darkGC, x2, y2, x2, y);
        XDrawLine(Tk_Display(tkwin), drawable, darkGC, x2, y2, x, y2);
        XDrawLine(Tk_Display(tkwin), drawable, lightGC, x, y, x, y2);
        x++;
        y++;
        width -= 2;
        height -= 2;
        borderWidth--;
    }
    Tk_Fill3DRectangle(tkwin, drawable, border, x, y, width, height, borderWidth, relief);
}

#undef Rbc_Draw3DRectangle
/*
 *----------------------------------------------------------------------
 *
 * Rbc_Draw3DRectangle --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tk_Window tkwin - Window for which border was allocated.
 *      Drawable drawable - X window or pixmap in which to draw.
 *      Tk_3DBorder border - Token for border to draw.
 *      int x - -
 *      int y - -
 *      int width - -
 *      int height - Outside area of rectangular region. 
 *      int borderWidth - Desired width for border, in pixels. Border will be *inside* region.
 *      int relief - Indicates 3D effect: TK_RELIEF_FLAT, TK_RELIEF_RAISED, or TK_RELIEF_SUNKEN.
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_Draw3DRectangle(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y, int width, int height,
                         int borderWidth, int relief) {
    if ((borderWidth > 1) && (width > 2) && (height > 2) &&
        ((relief == TK_RELIEF_SUNKEN) || (relief == TK_RELIEF_RAISED))) {
        GC lightGC, darkGC;
        int x2, y2;

        x2 = x + width - 1;
        y2 = y + height - 1;
        if (relief == TK_RELIEF_RAISED) {
            lightGC = Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC);
#ifdef WIN32
            darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
#else
            darkGC = DefaultGC(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
#endif
        } else {
#ifdef WIN32
            lightGC = Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC);
#else
            lightGC = DefaultGC(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
#endif
            darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC);
        }
        XDrawLine(Tk_Display(tkwin), drawable, darkGC, x2, y2, x2, y);
        XDrawLine(Tk_Display(tkwin), drawable, lightGC, x, y, x2, y);
        XDrawLine(Tk_Display(tkwin), drawable, darkGC, x2, y2, x, y2);
        XDrawLine(Tk_Display(tkwin), drawable, lightGC, x, y, x, y2);
        x++;
        y++;
        width -= 2;
        height -= 2;
        borderWidth--;
    }
    Tk_Draw3DRectangle(tkwin, drawable, border, x, y, width, height, borderWidth, relief);
}
