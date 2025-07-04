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

#include <stdarg.h>

/*
typedef struct {
    double hue, sat, val;
} HSV;
*/

static Tk_OptionParseProc StringToPoint;
static Tk_OptionPrintProc PointToString;
static Tk_OptionParseProc StringToColorPair;
static Tk_OptionPrintProc ColorPairToString;
Tk_CustomOption rbcPointOption = {StringToPoint, PointToString, (ClientData)0};
Tk_CustomOption rbcColorPairOption = {StringToColorPair, ColorPairToString, (ClientData)0};

// static void XColorToHSV (XColor *colorPtr, HSV *hsvPtr);
// static void HSVToXColor (HSV *hsvPtr, XColor *colorPtr);
static int GetColorPair(Tcl_Interp *interp, Tk_Window tkwin, const char *fgStr, const char *bgStr, ColorPair *pairPtr,
                        int allowDefault);
static const char *NameOfColor(XColor *colorPtr);
static int ClipTest(double ds, double dr, double *t1, double *t2);
static double FindSplit(Point2D points[], int i, int j, int *split);

/* ----------------------------------------------------------------------
 * Custom option parse and print procedures
 * ----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetXY --
 *
 *      Converts a string in the form "@x,y" into an XPoint structure
 *      of the x and y coordinates.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Tk_Window tkwin
 *      const char *string
 *      int *xPtr
 *      int *yPtr
 *
 * Results:
 *      A standard Tcl result. If the string represents a valid position
 *      *pointPtr* will contain the converted x and y coordinates and
 *      TCL_OK is returned.  Otherwise, TCL_ERROR is returned and
 *      the interpreter result will contain an error message.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_GetXY(Tcl_Interp *interp, Tk_Window tkwin, const char *string, int *xPtr, int *yPtr) {
    char *comma;
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
    *comma = '\0';
    result = ((Tk_GetPixels(interp, tkwin, string + 1, &x) == TCL_OK) &&
              (Tk_GetPixels(interp, tkwin, comma + 1, &y) == TCL_OK));
    *comma = ',';
    if (!result) {
        Tcl_AppendResult(interp, ": can't parse position \"", string, "\"", (char *)NULL);
        return TCL_ERROR;
    }
    *xPtr = x, *yPtr = y;
    return TCL_OK;

badFormat:
    Tcl_AppendResult(interp, "bad position \"", string, "\": should be \"@x,y\"", (char *)NULL);
    return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * StringToPoint --
 *
 *      Convert the string representation of a legend XY position into
 *      window coordinates.  The form of the string must be "@x,y" or
 *      none.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tcl_Interp *interp - Interpreter to send results back to
 *      Tk_Window tkwin - Not used.
 *      const char *string - New legend position string
 *      char *widgRec - Widget record
 *      Tcl_Size offset - offset to XPoint structure
 *
 * Results:
 *      A standard Tcl result.  The symbol type is written into the
 *      widget record.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static int StringToPoint(ClientData clientData, Tcl_Interp *interp, Tk_Window tkwin, const char *string, char *widgRec,
                         Tcl_Size offset) {
    XPoint *pointPtr = (XPoint *)(widgRec + offset);
    int x, y;

    if (Rbc_GetXY(interp, tkwin, string, &x, &y) != TCL_OK) {
        return TCL_ERROR;
    }
    pointPtr->x = x, pointPtr->y = y;
    return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * PointToString --
 *
 *      Convert the window coordinates into a string.
 *
 * Parameters:
 *      ClientData clientData - Not used.
 *      Tk_Window tkwin - Not used.
 *      char *widgRec - Widget record
 *      Tcl_Size offset - Not used.
 *      Tcl_FreeProc **freeProcPtr - Memory deallocation scheme to use
 *
 * Results:
 *      The string representing the coordinate position is returned.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static const char *PointToString(ClientData clientData, Tk_Window tkwin, char *widgRec, Tcl_Size offset,
                                 Tcl_FreeProc **freeProcPtr) {
    char *result;
    XPoint *pointPtr = (XPoint *)(widgRec + offset);

    result = "";
    if ((pointPtr->x != -SHRT_MAX) && (pointPtr->y != -SHRT_MAX)) {
        char string[200];

        sprintf(string, "@%d,%d", pointPtr->x, pointPtr->y);
        result = RbcStrdup(string);
        assert(result);
        *freeProcPtr = (Tcl_FreeProc *)Tcl_Free;
    }
    return result;
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
    unsigned int length;
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
            Tcl_AppendResult(interp, "too many names in colors list", (char *)NULL);
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
int Rbc_PointInSegments(Point2D *samplePtr, Segment2D *segments, int nSegments, double halo) {
    register Segment2D *segPtr, *endPtr;
    double left, right, top, bottom;
    Point2D p, t;
    double dist, minDist;

    minDist = DBL_MAX;
    for (segPtr = segments, endPtr = segments + nSegments; segPtr < endPtr; segPtr++) {
        t = Rbc_GetProjection((int)samplePtr->x, (int)samplePtr->y, &segPtr->p, &segPtr->q);
        if (segPtr->p.x > segPtr->q.x) {
            right = segPtr->p.x, left = segPtr->q.x;
        } else {
            right = segPtr->q.x, left = segPtr->p.x;
        }
        if (segPtr->p.y > segPtr->q.y) {
            bottom = segPtr->p.y, top = segPtr->q.y;
        } else {
            bottom = segPtr->q.y, top = segPtr->p.y;
        }
        p.x = BOUND(t.x, left, right);
        p.y = BOUND(t.y, top, bottom);
        dist = hypot(p.x - samplePtr->x, p.y - samplePtr->y);
        if (dist < minDist) {
            minDist = dist;
        }
    }
    return (minDist < halo);
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
int Rbc_PointInPolygon(Point2D *samplePtr, Point2D *points, int nPoints) {
    double b;
    register Point2D *p, *q, *endPtr;
    register int count;

    count = 0;
    for (p = points, q = p + 1, endPtr = p + nPoints; q < endPtr; p++, q++) {
        if (((p->y <= samplePtr->y) && (samplePtr->y < q->y)) || ((q->y <= samplePtr->y) && (samplePtr->y < p->y))) {
            b = (q->x - p->x) * (samplePtr->y - p->y) / (q->y - p->y) + p->x;
            if (samplePtr->x < b) {
                count++; /* Count the number of intersections. */
            }
        }
    }
    return (count & 0x01);
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
int Rbc_RegionInPolygon(Extents2D *extsPtr, Point2D *points, int nPoints, int enclosed) {
    register Point2D *pointPtr, *endPtr;

    if (enclosed) {
        /*
         * All points of the polygon must be inside the rectangle.
         */
        for (pointPtr = points, endPtr = points + nPoints; pointPtr < endPtr; pointPtr++) {
            if ((pointPtr->x < extsPtr->left) || (pointPtr->x > extsPtr->right) || (pointPtr->y < extsPtr->top) ||
                (pointPtr->y > extsPtr->bottom)) {
                return FALSE; /* One point is exterior. */
            }
        }
        return TRUE;
    } else {
        Point2D p, q;

        /*
         * If any segment of the polygon clips the bounding region, the
         * polygon overlaps the rectangle.
         */
        points[nPoints] = points[0];
        for (pointPtr = points, endPtr = points + nPoints; pointPtr < endPtr; pointPtr++) {
            p = *pointPtr;
            q = *(pointPtr + 1);
            if (Rbc_LineRectClip(extsPtr, &p, &q)) {
                return TRUE;
            }
        }
        /*
         * Otherwise the polygon and rectangle are either disjoint
         * or enclosed.  Check if one corner of the rectangle is
         * inside the polygon.
         */
        p.x = extsPtr->left;
        p.y = extsPtr->top;
        return Rbc_PointInPolygon(&p, points, nPoints);
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
    extsPtr->left = (double)(graphPtr->hOffset - graphPtr->padX.side1);
    extsPtr->top = (double)(graphPtr->vOffset - graphPtr->padY.side1);
    extsPtr->right = (double)(graphPtr->hOffset + graphPtr->hRange + graphPtr->padX.side2);
    extsPtr->bottom = (double)(graphPtr->vOffset + graphPtr->vRange + graphPtr->padY.side2);
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
 *      Clips the given line segment to a rectangular region.  The
 *      coordinates of the clipped line segment are returned.  The
 *      original coordinates are overwritten.
 *
 *      Reference:  Liang-Barsky Line Clipping Algorithm.
 *
 * Parameters:
 *      Extents2D *extsPtr - Rectangular region to clip.
 *      Point2D *p - (in/out) Coordinates of original and clipped line segment.
 *      Point2D *q - (in/out) Coordinates of original and clipped line segment.
 *
 * Results:
 *      Returns if line segment is visible within the region. The
 *      coordinates of the original line segment are overwritten
 *      by the clipped coordinates.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_LineRectClip(Extents2D *extsPtr, Point2D *p, Point2D *q) {
    double t1, t2;
    double dx, dy;

    t1 = 0.0;
    t2 = 1.0;
    dx = q->x - p->x;
    if ((ClipTest(-dx, p->x - extsPtr->left, &t1, &t2)) && (ClipTest(dx, extsPtr->right - p->x, &t1, &t2))) {
        dy = q->y - p->y;
        if ((ClipTest(-dy, p->y - extsPtr->top, &t1, &t2)) && (ClipTest(dy, extsPtr->bottom - p->y, &t1, &t2))) {
            if (t2 < 1.0) {
                q->x = p->x + t2 * dx;
                q->y = p->y + t2 * dy;
            }
            if (t1 > 0.0) {
                p->x += t1 * dx;
                p->y += t1 * dy;
            }
            return TRUE;
        }
    }
    return FALSE;
}

#define EPSILON FLT_EPSILON
#define AddVertex(vx, vy) r->x = (vx), r->y = (vy), r++, count++
#define LastVertex(vx, vy) r->x = (vx), r->y = (vy), count++

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
int Rbc_PolyRectClip(Extents2D *extsPtr, Point2D *points, int nPoints, Point2D *clipPts) {
    Point2D *endPtr;
    double dx, dy;
    double tin1, tin2;
    double tinx, tiny;
    double xin, yin, xout, yout;
    int count;
    register Point2D *p; /* First vertex of input polygon edge. */
    register Point2D *q; /* Last vertex of input polygon edge. */
    register Point2D *r;

    r = clipPts;
    count = 0; /* Counts # of vertices in output polygon. */

    points[nPoints] = points[0];

    for (p = points, q = p + 1, endPtr = p + nPoints; p < endPtr; p++, q++) {
        dx = q->x - p->x; /* X-direction */
        dy = q->y - p->y; /* Y-direction */

        if (FABS(dx) < EPSILON) {
            dx = (p->x > extsPtr->left) ? -EPSILON : EPSILON;
        }
        if (FABS(dy) < EPSILON) {
            dy = (p->y > extsPtr->top) ? -EPSILON : EPSILON;
        }

        if (dx > 0.0) { /* Left */
            xin = extsPtr->left;
            xout = extsPtr->right + 1.0;
        } else { /* Right */
            xin = extsPtr->right + 1.0;
            xout = extsPtr->left;
        }
        if (dy > 0.0) { /* Top */
            yin = extsPtr->top;
            yout = extsPtr->bottom + 1.0;
        } else { /* Bottom */
            yin = extsPtr->bottom + 1.0;
            yout = extsPtr->top;
        }

        tinx = (xin - p->x) / dx;
        tiny = (yin - p->y) / dy;

        if (tinx < tiny) { /* Hits x first */
            tin1 = tinx;
            tin2 = tiny;
        } else { /* Hits y first */
            tin1 = tiny;
            tin2 = tinx;
        }

        if (tin1 <= 1.0) {
            if (tin1 > 0.0) {
                AddVertex(xin, yin);
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
                                AddVertex(xin, p->y + tinx * dy);
                            } else {
                                AddVertex(p->x + tiny * dx, yin);
                            }
                        }
                        if (tout1 < 1.0) {
                            if (toutx < touty) {
                                AddVertex(xout, p->y + toutx * dy);
                            } else {
                                AddVertex(p->x + touty * dx, yout);
                            }
                        } else {
                            AddVertex(q->x, q->y);
                        }
                    } else {
                        if (tinx > tiny) {
                            AddVertex(xin, yout);
                        } else {
                            AddVertex(xout, yin);
                        }
                    }
                }
            }
        }
    }
    if (count > 0) {
        LastVertex(clipPts[0].x, clipPts[0].y);
    }
    return count;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetProjection --
 *
 *      Computes the projection of a point on a line.  The line (given
 *      by two points), is assumed the be infinite.
 *
 *      Compute the slope (angle) of the line and rotate it 90 degrees.
 *      Using the slope-intercept method (we know the second line from
 *      the sample test point and the computed slope), then find the
 *      intersection of both lines. This will be the projection of the
 *      sample point on the first line.
 *
 * Parameters:
 *      int x - Screen coordinate of the sample point.
 *      int y - Screen coordinate of the sample point.
 *      Point2D *p - Line segment to project point onto
 *      Point2D *q - Line segment to project point onto
 *
 * Results:
 *      Returns the coordinates of the projection on the line.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Point2D Rbc_GetProjection(int x, int y, Point2D *p, Point2D *q) {
    double dx, dy;
    Point2D t;

    dx = p->x - q->x;
    dy = p->y - q->y;

    /* Test for horizontal and vertical lines */
    if (FABS(dx) < DBL_EPSILON) {
        t.x = p->x, t.y = (double)y;
    } else if (FABS(dy) < DBL_EPSILON) {
        t.x = (double)x, t.y = p->y;
    } else {
        double m1, m2;     /* Slope of both lines */
        double b1, b2;     /* y-intercepts */
        double midX, midY; /* Midpoint of line segment. */
        double ax, ay, bx, by;

        /* Compute the slop and intercept of the line segment. */
        m1 = (dy / dx);
        b1 = p->y - (p->x * m1);

        /*
         * Compute the slope and intercept of a second line segment:
         * one that intersects through sample X-Y coordinate with a
         * slope perpendicular to original line.
         */

        /* Find midpoint of original segment. */
        midX = (p->x + q->x) * 0.5;
        midY = (p->y + q->y) * 0.5;

        /* Rotate the line 90 degrees */
        ax = midX - (0.5 * dy);
        ay = midY - (0.5 * -dx);
        bx = midX + (0.5 * dy);
        by = midY + (0.5 * -dx);

        m2 = (ay - by) / (ax - bx);
        b2 = y - (x * m2);

        /*
         * Given the equations of two lines which contain the same point,
         *
         *    y = m1 * x + b1
         *    y = m2 * x + b2
         *
         * solve for the intersection.
         *
         *    x = (b2 - b1) / (m1 - m2)
         *    y = m1 * x + b1
         *
         */

        t.x = (b2 - b1) / (m1 - m2);
        t.y = m1 * t.x + b1;
    }
    return t;
}

#define SetColor(c, r, g, b)                                                                                           \
    ((c)->red = (int)((r) * 65535.0), (c)->green = (int)((g) * 65535.0), (c)->blue = (int)((b) * 65535.0))

#ifdef notdef
/*
 *----------------------------------------------------------------------
 *
 * XColorToHSV --
 *
 *      TODO: Description
 *
 * Parameters:
 *      XColor *colorPtr
 *      HSV *hsvPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void XColorToHSV(XColor *colorPtr, HSV *hsvPtr) {
    unsigned short max, min;
    double range;
    unsigned short *colorValues;

    /* Find the minimum and maximum RGB intensities */
    colorValues = (unsigned short *)&colorPtr->red;
    max = MAX3(colorValues[0], colorValues[1], colorValues[2]);
    min = MIN3(colorValues[0], colorValues[1], colorValues[2]);

    hsvPtr->val = (double)max / 65535.0;
    hsvPtr->hue = hsvPtr->sat = 0.0;

    range = (double)(max - min);
    if (max != min) {
        hsvPtr->sat = range / (double)max;
    }
    if (hsvPtr->sat > 0.0) {
        double red, green, blue;

        /* Normalize the RGB values */
        red = (double)(max - colorPtr->red) / range;
        green = (double)(max - colorPtr->green) / range;
        blue = (double)(max - colorPtr->blue) / range;

        if (colorPtr->red == max) {
            hsvPtr->hue = (blue - green);
        } else if (colorPtr->green == max) {
            hsvPtr->hue = 2 + (red - blue);
        } else if (colorPtr->blue == max) {
            hsvPtr->hue = 4 + (green - red);
        }
        hsvPtr->hue *= 60.0;
    } else {
        hsvPtr->sat = 0.5;
    }
    if (hsvPtr->hue < 0.0) {
        hsvPtr->hue += 360.0;
    }
}

/*
 *----------------------------------------------------------------------
 *
 * HSVToXColor --
 *
 *      TODO: Description
 *
 * Parameters:
 *      HSV *hsvPtr
 *      XColor *colorPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void HSVToXColor(HSV *hsvPtr, XColor *colorPtr) {
    double hue, p, q, t;
    double frac;
    int quadrant;

    if (hsvPtr->val < 0.0) {
        hsvPtr->val = 0.0;
    } else if (hsvPtr->val > 1.0) {
        hsvPtr->val = 1.0;
    }
    if (hsvPtr->sat == 0.0) {
        SetColor(colorPtr, hsvPtr->val, hsvPtr->val, hsvPtr->val);
        return;
    }
    hue = FMOD(hsvPtr->hue, 360.0) / 60.0;
    quadrant = (int)floor(hue);
    frac = hsvPtr->hue - quadrant;
    p = hsvPtr->val * (1 - (hsvPtr->sat));
    q = hsvPtr->val * (1 - (hsvPtr->sat * frac));
    t = hsvPtr->val * (1 - (hsvPtr->sat * (1 - frac)));

    switch (quadrant) {
    case 0:
        SetColor(colorPtr, hsvPtr->val, t, p);
        break;
    case 1:
        SetColor(colorPtr, q, hsvPtr->val, p);
        break;
    case 2:
        SetColor(colorPtr, p, hsvPtr->val, t);
        break;
    case 3:
        SetColor(colorPtr, p, q, hsvPtr->val);
        break;
    case 4:
        SetColor(colorPtr, t, p, hsvPtr->val);
        break;
    case 5:
        SetColor(colorPtr, hsvPtr->val, p, q);
        break;
    }
}
#endif /* notdef */

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
    switch (scrollMode) {
    case RBC_SCROLL_MODE_CANVAS:

        /*
         * Canvas-style scrolling allows the world to be scrolled
         * within the window.
         */

        if (worldSize < windowSize) {
            if ((worldSize - offset) > windowSize) {
                offset = worldSize - windowSize;
            }
            if (offset > 0) {
                offset = 0;
            }
        } else {
            if ((offset + windowSize) > worldSize) {
                offset = worldSize - windowSize;
            }
            if (offset < 0) {
                offset = 0;
            }
        }
        break;

    case RBC_SCROLL_MODE_LISTBOX:
        if (offset < 0) {
            offset = 0;
        }
        if (offset >= worldSize) {
            offset = worldSize - scrollUnits;
        }
        break;

    case RBC_SCROLL_MODE_HIERBOX:

        /*
         * Hierbox-style scrolling allows the world to be scrolled
         * within the window.
         */
        if ((offset + windowSize) > worldSize) {
            offset = worldSize - windowSize;
        }
        if (offset < 0) {
            offset = 0;
        }
        break;
    }
    return offset;
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_GetScrollInfo --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      int argc
 *      char **argv
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
int Rbc_GetScrollInfo(Tcl_Interp *interp, int argc, char **argv, int *offsetPtr, int worldSize, int windowSize,
                      int scrollUnits, int scrollMode) {
    char c;
    unsigned int length;
    int offset;
    int count;
    double fract;

    offset = *offsetPtr;
    c = argv[0][0];
    length = strlen(argv[0]);
    if ((c == 's') && (strncmp(argv[0], "scroll", length) == 0)) {
        if (argc != 3) {
            return TCL_ERROR;
        }
        /* scroll number unit/page */
        if (Tcl_GetInt(interp, argv[1], &count) != TCL_OK) {
            return TCL_ERROR;
        }
        c = argv[2][0];
        length = strlen(argv[2]);
        if ((c == 'u') && (strncmp(argv[2], "units", length) == 0)) {
            fract = (double)count * scrollUnits;
        } else if ((c == 'p') && (strncmp(argv[2], "pages", length) == 0)) {
            /* A page is 90% of the view-able window. */
            fract = (double)count * windowSize * 0.9;
        } else {
            Tcl_AppendResult(interp, "unknown \"scroll\" units \"", argv[2], "\"", (char *)NULL);
            return TCL_ERROR;
        }
        offset += (int)fract;
    } else if ((c == 'm') && (strncmp(argv[0], "moveto", length) == 0)) {
        if (argc != 2) {
            return TCL_ERROR;
        }
        /* moveto fraction */
        if (Tcl_GetDouble(interp, argv[1], &fract) != TCL_OK) {
            return TCL_ERROR;
        }
        offset = (int)(worldSize * fract);
    } else {
        /* Treat like "scroll units" */
        if (Tcl_GetInt(interp, argv[0], &count) != TCL_OK) {
            return TCL_ERROR;
        }
        fract = (double)count * scrollUnits;
        offset += (int)fract;
    }
    *offsetPtr = Rbc_AdjustViewport(offset, worldSize, windowSize, scrollUnits, scrollMode);
    return TCL_OK;
}

#if (TCL_MAJOR_VERSION >= 8)
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
int Rbc_GetScrollInfoFromObj(Tcl_Interp *interp, int objc, Tcl_Obj *const *objv, int *offsetPtr, int worldSize,
                             int windowSize, int scrollUnits, int scrollMode) {
    char c;
    unsigned int length;
    int offset;
    int count;
    double fract;
    char *string;

    offset = *offsetPtr;

    string = Tcl_GetString(objv[0]);
    c = string[0];
    length = strlen(string);
    if ((c == 's') && (strncmp(string, "scroll", length) == 0)) {
        if (objc != 3) {
            return TCL_ERROR;
        }
        /* scroll number unit/page */
        if (Tcl_GetIntFromObj(interp, objv[1], &count) != TCL_OK) {
            return TCL_ERROR;
        }
        string = Tcl_GetString(objv[2]);
        c = string[0];
        length = strlen(string);
        if ((c == 'u') && (strncmp(string, "units", length) == 0)) {
            fract = (double)count * scrollUnits;
        } else if ((c == 'p') && (strncmp(string, "pages", length) == 0)) {
            /* A page is 90% of the view-able window. */
            fract = (double)count * windowSize * 0.9;
        } else {
            Tcl_AppendResult(interp, "unknown \"scroll\" units \"", Tcl_GetString(objv[2]), "\"", (char *)NULL);
            return TCL_ERROR;
        }
        offset += (int)fract;
    } else if ((c == 'm') && (strncmp(string, "moveto", length) == 0)) {
        if (objc != 2) {
            return TCL_ERROR;
        }
        /* moveto fraction */
        if (Tcl_GetDoubleFromObj(interp, objv[1], &fract) != TCL_OK) {
            return TCL_ERROR;
        }
        offset = (int)(worldSize * fract);
    } else {
        /* Treat like "scroll units" */
        if (Tcl_GetIntFromObj(interp, objv[0], &count) != TCL_OK) {
            return TCL_ERROR;
        }
        fract = (double)count * scrollUnits;
        offset += (int)fract;
    }
    *offsetPtr = Rbc_AdjustViewport(offset, worldSize, windowSize, scrollUnits, scrollMode);
    return TCL_OK;
}
#endif /* TCL_MAJOR_VERSION >= 8 */

/*
 * ----------------------------------------------------------------------
 *
 * Rbc_UpdateScrollbar --
 *
 *      Invoke a Tcl command to the scrollbar, defining the new
 *      position and length of the scroll. See the Tk documentation
 *      for further information on the scrollbar.  It is assumed the
 *      scrollbar command prefix is valid.
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      char *scrollCmd - scrollbar command
 *      double firstFract
 *      double lastFract
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      Scrollbar is commanded to change position and/or size.
 *
 * ----------------------------------------------------------------------
 */
void Rbc_UpdateScrollbar(Tcl_Interp *interp, char *scrollCmd, double firstFract, double lastFract) {
    char string[200];
    Tcl_DString dString;

    Tcl_DStringInit(&dString);
    Tcl_DStringAppend(&dString, scrollCmd, -1);
    sprintf(string, " %f %f", firstFract, lastFract);
    Tcl_DStringAppend(&dString, string, -1);
    if (Tcl_GlobalEval(interp, Tcl_DStringValue(&dString)) != TCL_OK) {
        Tcl_BackgroundError(interp);
    }
    Tcl_DStringFree(&dString);
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
    GC newGC;

#ifdef WIN32
    newGC = Rbc_EmulateXCreateGC(display, drawable, gcMask, valuePtr);
#else
    newGC = XCreateGC(display, drawable, gcMask, valuePtr);
#endif
    return newGC;
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

#ifndef WIN32
/*
 *----------------------------------------------------------------------
 *
 * Rbc_SetDashes --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      GC gc
 *      Rbc_Dashes *dashesPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_SetDashes(Display *display, GC gc, Rbc_Dashes *dashesPtr) {
    XSetDashes(display, gc, dashesPtr->offset, (const char *)dashesPtr->values, strlen((char *)dashesPtr->values));
}
#endif

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
static double FindSplit(Point2D points[], int i, int j, int *split) {
    double maxDist;

    maxDist = -1.0;
    if ((i + 1) < j) {
        register int k;
        double a, b, c;
        double sqDist;

        /*
         *
         * sqDist P(k) =  |  1  P(i).x  P(i).y  |
         *          |  1  P(j).x  P(j).y  |
         *                |  1  P(k).x  P(k).y  |
         *              ---------------------------
         *       (P(i).x - P(j).x)^2 + (P(i).y - P(j).y)^2
         */

        a = points[i].y - points[j].y;
        b = points[j].x - points[i].x;
        c = (points[i].x * points[j].y) - (points[i].y * points[j].x);
        for (k = (i + 1); k < j; k++) {
            sqDist = (points[k].x * a) + (points[k].y * b) + c;
            if (sqDist < 0.0) {
                sqDist = -sqDist;
            }
            if (sqDist > maxDist) {
                maxDist = sqDist; /* Track the maximum. */
                *split = k;
            }
        }
        /* Correction for segment length---should be redone if can == 0 */
        maxDist *= maxDist / (a * a + b * b);
    }
    return maxDist;
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
 *      int low
 *      int high
 *      double tolerance
 *      int indices[]
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
int Rbc_SimplifyLine(Point2D inputPts[], int low, int high, double tolerance, int indices[]) {
#define StackPush(a) s++, stack[s] = (a)
#define StackPop(a) (a) = stack[s], s--
#define StackEmpty() (s < 0)
#define StackTop() stack[s]
    int *stack;
    int split = -1;
    double sqDist, sqTolerance;
    int s = -1; /* Points to top stack item. */
    int count;

    stack = (int *)ckalloc(sizeof(int) * (high - low + 1));
    StackPush(high);
    count = 0;
    indices[count++] = 0;
    sqTolerance = tolerance * tolerance;
    while (!StackEmpty()) {
        sqDist = FindSplit(inputPts, low, StackTop(), &split);
        if (sqDist > sqTolerance) {
            StackPush(split);
        } else {
            indices[count++] = StackTop();
            StackPop(low);
        }
    }
    ckfree((char *)stack);
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
void Rbc_Draw2DSegments(Display *display, Drawable drawable, GC gc, register Segment2D *segPtr, int nSegments) {
    XSegment *xSegPtr, *xSegArr;
    Segment2D *endPtr;

    xSegArr = (XSegment *)ckalloc(nSegments * sizeof(XSegment));
    if (xSegArr == NULL) {
        return;
    }
    xSegPtr = xSegArr;
    for (endPtr = segPtr + nSegments; segPtr < endPtr; segPtr++) {
        xSegPtr->x1 = (short int)segPtr->p.x;
        xSegPtr->y1 = (short int)segPtr->p.y;
        xSegPtr->x2 = (short int)segPtr->q.x;
        xSegPtr->y2 = (short int)segPtr->q.y;
        xSegPtr++;
    }
    XDrawSegments(display, drawable, gc, xSegArr, nSegments);
    ckfree((char *)xSegArr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_DrawArrow --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      GC gc
 *      int x
 *      int y
 *      int arrowHeight
 *      int orientation
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_DrawArrow(Display *display, Drawable drawable, GC gc, int x, int y, int arrowHeight, int orientation) {
    XPoint arrow[5];
    int a, b;

    a = arrowHeight / 2 + 1;
    b = arrowHeight;
    switch (orientation) {
    case ARROW_UP:
        /*
         *            0
         *            +
         *           / \
         *          /   \
         *         /     \  a
         *        /       \
         *   x,y /         \
         *      +-----------+
         *     1      b      2
         */
        arrow[0].x = x;
        arrow[0].y = y - a;
        arrow[1].x = arrow[0].x - b;
        arrow[1].y = arrow[0].y + b;
        arrow[2].x = arrow[0].x + b;
        arrow[2].y = arrow[0].y + b;
        arrow[3].x = arrow[0].x;
        arrow[3].y = arrow[0].y;
        break;

    case ARROW_DOWN:
        /*
         *     1      b      2
         *      +-----------+
         *       \         /
         *        \  x,y  /
         *         \     /  a
         *          \   /
         *           \ /
         *            +
         *            0
         */
        arrow[0].x = x;
        arrow[0].y = y + a;
        arrow[1].x = arrow[0].x - b;
        arrow[1].y = arrow[0].y - b;
        arrow[2].x = arrow[0].x + b;
        arrow[2].y = arrow[0].y - b;
        arrow[3].x = arrow[0].x;
        arrow[3].y = arrow[0].y;
        break;

    case ARROW_RIGHT:
        /*
         *       2
         *       +
         *       |\
         *       | \
         *       |  \
         *       |   \
         *       |    \
         *       | x,y + 0
         *       |    /
         *       |   /
         *       |  /
         *       | /
         *       |/
         *       +
         *       1
         */
        arrow[0].x = x + a;
        arrow[0].y = y;
        arrow[1].x = arrow[0].x - b;
        arrow[1].y = arrow[0].y + b;
        arrow[2].x = arrow[0].x - b;
        arrow[2].y = arrow[0].y - b;
        arrow[3].x = arrow[0].x;
        arrow[3].y = arrow[0].y;
        break;

    case ARROW_LEFT:
        /*
         *              2
         *              +
         *             /|
         *            / |
         *           /  |
         *          /   |
         *         /    |
         *       0+ x,y |
         *         \    |
         *          \   |
         *           \  |
         *            \ |
         *             \|
         *              +
         *              1
         */
        arrow[0].x = x - a;
        arrow[0].y = y;
        arrow[1].x = arrow[0].x + b;
        arrow[1].y = arrow[0].y + b;
        arrow[2].x = arrow[0].x + b;
        arrow[2].y = arrow[0].y - b;
        arrow[3].x = arrow[0].x;
        arrow[3].y = arrow[0].y;
        break;
    }
    XFillPolygon(display, drawable, gc, arrow, 4, Convex, CoordModeOrigin);
    XDrawLines(display, drawable, gc, arrow, 4, CoordModeOrigin);
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
    size -= 4;
    return ((size * 4) / elemSize);
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
#ifndef notdef
    if ((borderWidth > 1) && (width > 2) && (height > 2) &&
        ((relief == TK_RELIEF_SUNKEN) || (relief == TK_RELIEF_RAISED))) {
        GC lightGC, darkGC;
        int x2, y2;

        x2 = x + width - 1;
        y2 = y + height - 1;
#define TK_3D_LIGHT2_GC TK_3D_DARK_GC + 1
#define TK_3D_DARK2_GC TK_3D_DARK_GC + 2
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
        x++, y++, width -= 2, height -= 2, borderWidth--;
    }
#endif
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
#ifndef notdef
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
        x++, y++, width -= 2, height -= 2, borderWidth--;
    }
#endif
    Tk_Draw3DRectangle(tkwin, drawable, border, x, y, width, height, borderWidth, relief);
}

#ifdef notdef
typedef struct {
    Screen *screen;
    Visual *visual;
    Colormap colormap;
    Tk_Uid nameUid;
} BorderKey;

typedef struct {
    Screen *screen;     /* Screen on which the border will be used. */
    Visual *visual;     /* Visual for all windows and pixmaps using
                         * the border. */
    int depth;          /* Number of bits per pixel of drawables where
                         * the border will be used. */
    Colormap colormap;  /* Colormap out of which pixels are
                         * allocated. */
    int refCount;       /* Number of active uses of this color
                         * (each active use corresponds to a
                         * call to Rbc_Get3DBorder).  If this
                         * count is 0, then this structure is
                         * no longer valid and it isn't
                         * present in borderTable: it is being
                         * kept around only because there are
                         * objects referring to it.  The
                         * structure is freed when refCount is
                         * 0. */
    XColor *bgColorPtr; /* Color of face. */
    XColor *shadows[4];
    Pixmap darkStipple;  /* Stipple pattern to use for drawing
                          * shadows areas.  Used for displays with
                          * <= 64 colors or where colormap has filled
                          * up. */
    Pixmap lightStipple; /* Stipple pattern to use for drawing
                          * shadows areas.  Used for displays with
                          * <= 64 colors or where colormap has filled
                          * up. */
    GC bgGC;             /* Used (if necessary) to draw areas in
                          * the background color. */
    GC lightGC, darkGC;
    Tcl_HashEntry *hashPtr; /* Entry in borderTable (needed in
                             * order to delete structure). */
    struct Rbc_3DBorderStruct *nextPtr;
} Border, *Rbc_3DBorder;

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
    if ((width > (2 * borderWidth)) && (height > (2 * borderWidth))) {
        int x2, y2;
        int i;

        x2 = x + width - 1;
        y2 = y + height - 1;

        XSetForeground(borderPtr->lightGC, borderPtr->shadows[0]);
        XSetForeground(borderPtr->darkGC, borderPtr->shadows[3]);
        XDrawLine(Tk_Display(tkwin), drawable, borderPtr->lightGC, x, y, x2, y);
        XDrawLine(Tk_Display(tkwin), drawable, borderPtr->lightGC, x, y, x, y2);
        XDrawLine(Tk_Display(tkwin), drawable, borderPtr->darkGC, x2, y, x2, y2);
        XDrawLine(Tk_Display(tkwin), drawable, borderPtr->darkGC, x2, y2, x, y2);
        XSetForeground(borderPtr->lightGC, borderPtr->shadows[1]);
        XSetForeground(borderPtr->darkGC, borderPtr->shadows[2]);
        for (i = 1; i < (borderWidth - 1); i++) {

            /*
             *  +---------
             *  |+-------
             *  ||+-----
             *  |||
             *  |||
             *  ||
             *  |
             */
            x++, y++, x2--, y2--;
            XDrawLine(Tk_Display(tkwin), drawable, borderPtr->lightGC, x, y, x2, y);
            XDrawLine(Tk_Display(tkwin), drawable, borderPtr->lightGC, x, y, x, y2);
            XDrawLine(Tk_Display(tkwin), drawable, borderPtr->darkGC, x2, y, x2, y2);
            XDrawLine(Tk_Display(tkwin), drawable, borderPtr->darkGC, x2, y2, x, y2);
        }
    }
}

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
    Rbc_3DBorder *borderPtr;

    if (borderPtr != NULL) {
        XFillRectangle(Tk_Display(tkwin), drawable, borderPtr->bgGC, x, y, width, height);
    }
    if ((borderWidth > 0) && (relief != TK_RELIEF_FLAT)) {
        Rbc_Draw3DRectangle(tkwin, drawable, borderPtr, x, y, width, height, borderWidth, relief);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * FreeBorder --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      Border *borderPtr
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void FreeBorder(Display *display, Border *borderPtr) {
    int i;

    if (borderPtr->bgColorPtr != NULL) {
        Tk_FreeColor(display, borderPtr->bgColorPtr);
    }
    for (i = 0; i < 4; i++) {
        Tk_FreeColor(display, borderPtr->shadows[i]);
    }
    if (borderPtr->tile != NULL) {
        Rbc_FreeTile(tile);
    }
    if (borderPtr->darkGC != NULL) {
        Rbc_FreePrivateGC(display, borderPtr->darkGC);
    }
    if (borderPtr->lightGC != NULL) {
        Rbc_FreePrivateGC(tkwin, borderPtr->lightGC);
    }
    if (borderPtr->bgGC != NULL) {
        Rbc_FreePrivateGC(tkwin, borderPtr->bgGC);
    }
    ckree((char *)borderPtr);
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_Free3DBorder --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      Rbc_3DBorder border
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
void Rbc_Free3DBorder(Display *display, Rbc_3DBorder border) {
    Border *borderPtr = (Border *)border;

    borderPtr->refCount--;
    if (borderPtr->refCount >= 0) {
        /* Search for the border in the bucket. Start at the head. */
        headPtr = Tcl_GetHashValue(borderPtr->hashPtr);
        lastPtr = NULL;
        while ((headPtr != borderPtr) && (headPtr != NULL)) {
            lastPtr = headPtr;
            headPtr = headPtr->next;
        }
        if (headPtr == NULL) {
            return; /* This can't happen. It means that
                     * we could not find the border. */
        }
        if (lastPtr != NULL) {
            lastPtr->next = borderPtr->next;
        } else {
            Tcl_DeleteHashEntry(borderPtr->hashPtr);
        }
        FreeBorder(display, borderPtr);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * Rbc_Get3DBorder --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Tcl_Interp *interp
 *      Tk_Window tkwin
 *      char *borderName
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
Rbc_3DBorder *Rbc_Get3DBorder(Tcl_Interp *interp, Tk_Window tkwin, char *borderName) {
    Rbc_3DBorder *borderPtr, *lastBorderPtr;
    Tcl_HashEntry *hPtr;
    Rbc_Tile tile;
    XColor *bgColorPtr;
    char **argv;
    char *colorName;
    int argc;
    int isNew;

    lastBorderPtr = NULL;
    hPtr = Tcl_CreateHashEntry(&dataPtr->borderTable, borderName, &isNew);
    if (!isNew) {
        borderPtr = lastBorderPtr = Tcl_GetHashValue(hPtr);
        while (borderPtr != NULL) {
            if ((Tk_Screen(tkwin) == borderPtr->screen) && (Tk_Colormap(tkwin) == borderPtr->colormap)) {
                borderPtr->refCount++;
                return borderPtr;
            }
            borderPtr = borderPtr->nextPtr;
        }
    }
    /* Create a new border. */
    argv = NULL;
    bgColorPtr = NULL;
    tile = NULL;

    if (Tcl_SplitList(interp, borderName, &argc, &argv) != TCL_OK) {
        goto error;
    }
    colorName = borderName;
    if ((argc == 2) && (Rbc_GetTile(interp, tkwin, argv[0], &tile) == TCL_OK)) {
        colorName = argv[1];
    }
    bgColorPtr = Tk_GetColor(interp, tkwin, colorName);
    if (bgColorPtr == NULL) {
        goto error;
    }

    /* Create a new border */
    borderPtr = Rbc_Calloc(1, sizeof(Rbc_3DBorder));
    assert(borderPtr);
    borderPtr->screen = Tk_Screen(tkwin);
    borderPtr->visual = Tk_Visual(tkwin);
    borderPtr->depth = Tk_Depth(tkwin);
    borderPtr->colormap = Tk_Colormap(tkwin);
    borderPtr->refCount = 1;
    borderPtr->bgColorPtr = bgColorPtr;
    borderPtr->tile = tile;
    borderPtr->darkGC = Rbc_GetPrivateGC(tkwin, 0, NULL);
    borderPtr->lightGC = Rbc_GetPrivateGC(tkwin, 0, NULL);
    borderPtr->hashPtr = lastBorderPtr->hashPtr;
    lastBorderPtr->nextPtr = lastBorderPtr;
    {
        HSV hsv;
        XColor color;
        double sat, sat0, diff, step, hstep;
        int count;

        /* Convert the face (background) color to HSV */
        XColorToHSV(borderPtr->bgColorPtr, &hsv);

        /* Using the color as the baseline intensity, pick a set of
         * colors around the intensity. */
#define UFLOOR(x, u) (floor((x) * (u)) / (u))
        diff = hsv.sat - UFLOOR(hsv.sat, 0.2);
        sat = 0.1 + (diff - 0.1);
        sat0 = hsv.sat;
        count = 0;
        for (sat = 0.1 + (diff - 0.1); sat <= 1.0; sat += 0.2) {
            if (FABS(sat0 - sat) >= 0.1) {
                hsv.sat = sat;
                HSVToXColor(&hsv, &color);
                borderPtr->shadows[count] = Tk_GetColorByValue(tkwin, &color);
                count++;
            }
        }
    }
    Tcl_SetHashValue(hPtr, borderPtr);
    if (argv != NULL) {
        ckfree((char *)argv);
    }
    return TCL_OK;

error:
    if (argv != NULL) {
        ckfree((char *)argv);
    }
    if (tile != NULL) {
        Rbc_FreeTile(tile);
    }
    if (bgColorPtr != NULL) {
        Tk_FreeColor(bgColorPtr);
    }
    if (isNew) {
        Tcl_DeleteHashEntry(&borderTable, hPtr);
    }
    return NULL;
}

#endif
