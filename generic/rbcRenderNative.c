/* rbcRenderNative.c -- Native screen geometry and batching.
 * See license.terms for copyright and redistribution terms.
 *
 * Geometry is already mapped by the components. GCs, tiles and bitmap resources
 * are borrowed; the existing integer conversions and platform batching limits
 * are deliberately retained. */
#include "rbcRenderNative.h"
#include <stdint.h>
#include <X11/Xutil.h>

struct Rbc_NativeTrace {
    Graph *graphPtr;
    Drawable nativeDrawable;
    GC nativeGC;
    void *nativePoints;
    int nativeMaxPoints;
#ifdef WIN32
    Rbc_WinDrawableDC *nativeDCState;
    HDC nativeDC;
    HPEN nativeOldPen;
    HBRUSH nativeOldBrush;
#endif
};

/* Native stroke contexts borrow the configured GC, including its clip and
 * raster operation. Keep the original request splitting and integer conversion;
 * dispatch occurs once per trace/segment batch, never once per point. */
void Rbc_NativePolyline(Rbc_NativeTrace *ctx, const Point2D *screenPts, Tcl_Size nPoints) {
    Tcl_Size count, j, remaining;
    int maxPoints;
    if (ctx == NULL) return;
    maxPoints = ctx->nativeMaxPoints;
    if (screenPts == NULL || nPoints < 2 || maxPoints < 2) {
        return;
    }
#ifdef WIN32
    POINT *points = ctx->nativePoints;
    HDC dc = ctx->nativeDC;
    Tcl_Size firstCount;

    /*
     * A polyline requires at least two points.
     */
    if (nPoints < 2) {
        return;
    }
    /*
     * Step 1: draw the first section.  It may contain the entire
     * trace.
     */
    firstCount = MIN((Tcl_Size)maxPoints, nPoints);
    for (count = 0; count < firstCount; count++) {
        points[count].x = (int)screenPts[count].x;
        points[count].y = (int)screenPts[count].y;
    }
    Polyline(dc, points, (int)firstCount);
    /*
     * Step 2: draw full continuation chunks.  Each continuation
     * starts with the last point drawn by the preceding request.
     */
    while ((nPoints - count) > (Tcl_Size)maxPoints) {
        points[0].x = (int)screenPts[count - 1].x;
        points[0].y = (int)screenPts[count - 1].y;
        for (j = 0; j < (Tcl_Size)maxPoints; j++, count++) {
            points[j + 1].x = (int)screenPts[count].x;
            points[j + 1].y = (int)screenPts[count].y;
        }
        Polyline(dc, points, maxPoints + 1);
    }
    /*
     * Step 3: draw the final partial chunk.
     */
    remaining = nPoints - count;
    if (remaining > 0) {
        points[0].x = (int)screenPts[count - 1].x;
        points[0].y = (int)screenPts[count - 1].y;
        for (j = 0; j < remaining; j++, count++) {
            points[j + 1].x = (int)screenPts[count].x;
            points[j + 1].y = (int)screenPts[count].y;
        }
        Polyline(dc, points, (int)remaining + 1);
    }
#else
    XPoint *points = ctx->nativePoints;
    Tcl_Size firstCount;

    /*
     * XDrawLines requires at least two useful points to produce a
     * line.
     */
    if (nPoints < 2) {
        return;
    }
    /*
     * Step 1: draw the first section.  It may contain the entire
     * trace.
     */
    firstCount = MIN((Tcl_Size)maxPoints, nPoints);
    for (count = 0; count < firstCount; count++) {
        points[count].x = (short int)screenPts[count].x;
        points[count].y = (short int)screenPts[count].y;
    }
    XDrawLines(ctx->graphPtr->display, ctx->nativeDrawable, ctx->nativeGC, points, (int)firstCount, CoordModeOrigin);
    /*
     * Step 2: draw full continuation chunks.  Each continuation
     * starts with the last point drawn by the preceding request.
     */
    while ((nPoints - count) > (Tcl_Size)maxPoints) {
        points[0].x = (short int)screenPts[count - 1].x;
        points[0].y = (short int)screenPts[count - 1].y;
        for (j = 0; j < (Tcl_Size)maxPoints; j++, count++) {
            points[j + 1].x = (short int)screenPts[count].x;
            points[j + 1].y = (short int)screenPts[count].y;
        }
        XDrawLines(ctx->graphPtr->display, ctx->nativeDrawable, ctx->nativeGC, points, maxPoints + 1, CoordModeOrigin);
    }
    /*
     * Step 3: draw the final partial chunk.
     */
    remaining = nPoints - count;
    if (remaining > 0) {
        points[0].x = (short int)screenPts[count - 1].x;
        points[0].y = (short int)screenPts[count - 1].y;
        for (j = 0; j < remaining; j++, count++) {
            points[j + 1].x = (short int)screenPts[count].x;
            points[j + 1].y = (short int)screenPts[count].y;
        }
        XDrawLines(ctx->graphPtr->display, ctx->nativeDrawable, ctx->nativeGC, points, (int)remaining + 1, CoordModeOrigin);
    }
#endif
}

void Rbc_NativeStrips(Graph *graphPtr, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size nSegments) {
#ifdef WIN32
    enum {
        /*
         * GDI/EMF permits 16K points in one polyline operation.
         * Each strip is an independent two-point polyline.
         */
        STRIP_BATCH_SEGMENTS = 8192,
        /*
         * Devices without wide-line support may have a 1360-point
         * limit for wide lines.
         */
        STRIP_WIDE_BATCH_SEGMENTS = 680
    };
    Rbc_WinDrawableDC *dcStatePtr;
    HDC dc;
    HPEN pen, oldPen;
    POINT *points;
    DWORD *counts;
    Tcl_Size maxSegments;
    Tcl_Size first;
    Tcl_Size i;

    if ((drawable == None) || (gc == NULL) || (segments == NULL) || (nSegments <= 0)) {
        return;
    }
    /*
     * Long operations using wide geometric pens can themselves become
     * expensive in GDI.  Use a smaller batch for wide lines, matching
     * the same concern handled by Rbc_NativePolyline().
     */
    if (gc->line_width > 1) {
        maxSegments = STRIP_WIDE_BATCH_SEGMENTS;
    } else {
        maxSegments = STRIP_BATCH_SEGMENTS;
    }
    if (maxSegments > nSegments) {
        maxSegments = nSegments;
    }
    /*
     * Each independent strip consists of exactly two POINTs.
     */
    if ((size_t)maxSegments > SIZE_MAX / (2u * sizeof(*points))) {
        return;
    }
    points = Tcl_AttemptAlloc((size_t)maxSegments * 2u * sizeof(*points));
    if (points == NULL) {
        return;
    }
    if ((size_t)maxSegments > SIZE_MAX / sizeof(*counts)) {
        ckfree(points);
        return;
    }
    counts = Tcl_AttemptAlloc((size_t)maxSegments * sizeof(*counts));
    if (counts == NULL) {
        ckfree(points);
        return;
    }
    /*
     * Every PolyPolyline entry is a separate two-point polyline.
     * This array is invariant across all chunks.
     */
    for (i = 0; i < maxSegments; i++) {
        counts[i] = 2;
    }
    dc = Rbc_WinAcquireDrawableDC(graphPtr->display, drawable, &dcStatePtr);
    Rbc_WinSetROP2(dc, gc->function);
    pen = Rbc_GCToPen(dc, gc);
    oldPen = SelectPen(dc, pen);
    first = 0;
    while (first < nSegments) {
        Tcl_Size count;

        count = nSegments - first;
        if (count > maxSegments) {
            count = maxSegments;
        }
        for (i = 0; i < count; i++) {
            const Segment2D *segPtr;

            segPtr = segments + first + i;
            points[2 * i].x = (LONG)segPtr->p.x;
            points[2 * i].y = (LONG)segPtr->p.y;
            points[2 * i + 1].x = (LONG)segPtr->q.x;
            points[2 * i + 1].y = (LONG)segPtr->q.y;
        }
        PolyPolyline(dc, points, counts, (DWORD)count);
        first += count;
    }
    DeletePen(SelectPen(dc, oldPen));
    Rbc_WinReleaseDrawableDC(dcStatePtr);
    ckfree(counts);
    ckfree(points);
#else
    Rbc_Draw2DSegments(graphPtr->display, drawable, gc, segments, nSegments);
#endif
}


Rbc_NativeTrace *Rbc_NativeBeginTrace(Graph *graphPtr, Drawable drawable, GC gc) {
    Rbc_NativeTrace *ctx;
    int maxPoints;
    size_t pointSize;

#ifdef WIN32
    pointSize = sizeof(POINT);
    maxPoints = gc->line_width > 1 ? 100 : Rbc_MaxRequestSize(graphPtr->display, pointSize) - 1;
#else
    pointSize = sizeof(XPoint);
    maxPoints = Rbc_MaxRequestSize(graphPtr->display, pointSize) - 1;
#endif
    if (maxPoints < 2 || ((size_t)maxPoints + 1u) > (SIZE_MAX - sizeof(*ctx)) / pointSize) {
        return NULL;
    }
    /* Keep state and point storage in one allocation, preserving the number
     * of allocations per trace batch before the backend was separated. */
    ctx = (Rbc_NativeTrace *)Tcl_AttemptAlloc(sizeof(*ctx) + ((size_t)maxPoints + 1u) * pointSize);
    if (ctx == NULL) return NULL;
    memset(ctx, 0, sizeof(*ctx));
    ctx->graphPtr = graphPtr;
    ctx->nativeDrawable = drawable;
    ctx->nativeGC = gc;
    ctx->nativePoints = ctx + 1;
    ctx->nativeMaxPoints = maxPoints;
#ifdef WIN32
    ctx->nativeDC = Rbc_WinAcquireDrawableDC(graphPtr->display, drawable, &ctx->nativeDCState);
    ctx->nativeOldPen = SelectPen(ctx->nativeDC, Rbc_GCToPen(ctx->nativeDC, gc));
    ctx->nativeOldBrush = SelectBrush(ctx->nativeDC, CreateSolidBrush(gc->foreground));
    Rbc_WinSetROP2(ctx->nativeDC, gc->function);
#endif
    return ctx;
}

void Rbc_NativeEndTrace(Rbc_NativeTrace *ctx) {
    if (ctx == NULL) return;
#ifdef WIN32
    DeletePen(SelectPen(ctx->nativeDC, ctx->nativeOldPen));
    DeleteBrush(SelectBrush(ctx->nativeDC, ctx->nativeOldBrush));
    Rbc_WinReleaseDrawableDC(ctx->nativeDCState);
#endif
    ckfree(ctx);
}

void Rbc_NativeSegments(Graph *graphPtr, Drawable drawable, GC gc,
    const Segment2D *segments, Tcl_Size count) {
    Rbc_Draw2DSegments(graphPtr->display, drawable, gc, segments, count);
}

INLINE static int Round(double x) { return (int)(x + ((x < 0.0) ? -0.5 : 0.5)); }

#define DRAW_SYMBOL(style) (((style)->counter % (style)->interval) == 0)

#define MAX_DRAWLINES(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWPOLYGON(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWSEGMENTS(d) Rbc_MaxRequestSize(d, sizeof(XSegment))
#define MAX_DRAWRECTANGLES(d) Rbc_MaxRequestSize(d, sizeof(XRectangle))
#define MAX_DRAWARCS(d) Rbc_MaxRequestSize(d, sizeof(XArc))
#ifdef WIN32

enum {
    /*
     * Filled paths have the same overlap-complexity issue we saw with
     * PolyPolygon, so keep their batches deliberately small.
     */
    CIRCLE_FILL_BATCH = 32,

    /*
     * Outline-only PolyDraw does not perform polygon/path filling and
     * can therefore use much larger batches.
     */
    CIRCLE_OUTLINE_BATCH = 512,
    CIRCLE_WIDE_OUTLINE_BATCH = 64
};

/*
 *----------------------------------------------------------------------
 *
 * GetCirclePathTemplate --
 *
 *      Ask GDI to construct one Ellipse path and retrieve the POINT/type
 *      representation that GDI itself generated.
 *
 *      The ellipse is generated in the rectangle:
 *
 *          0,0 .. 2*radius+1,2*radius+1
 *
 *      Therefore reproducing it at center x,y requires translating the
 *      returned path by x-radius,y-radius.
 *
 *----------------------------------------------------------------------
 */
static int GetCirclePathTemplate(HDC dc, int radius, POINT **pointsPtrPtr, BYTE **typesPtrPtr, int *nPointsPtr) {
    POINT *points;
    BYTE *types;
    int diameter;
    int nPoints;
    int result;

    *pointsPtrPtr = NULL;
    *typesPtrPtr = NULL;
    *nPointsPtr = 0;
    if (radius < 1) {
        return FALSE;
    }
    diameter = radius + radius + 1;
    if (!BeginPath(dc)) {
        return FALSE;
    }
    if (!Ellipse(dc, 0, 0, diameter, diameter)) {
        AbortPath(dc);
        return FALSE;
    }
    if (!EndPath(dc)) {
        AbortPath(dc);
        return FALSE;
    }
    /*
     * With a zero buffer size GetPath returns the number of path points
     * without writing any output.
     */
    nPoints = GetPath(dc, NULL, NULL, 0);
    if (nPoints <= 0) {
        AbortPath(dc);
        return FALSE;
    }
    if ((size_t)nPoints > SIZE_MAX / sizeof(*points)) {
        AbortPath(dc);
        return FALSE;
    }
    points = Tcl_AttemptAlloc((size_t)nPoints * sizeof(*points));
    if (points == NULL) {
        AbortPath(dc);
        return FALSE;
    }
    types = Tcl_AttemptAlloc((size_t)nPoints * sizeof(*types));
    if (types == NULL) {
        ckfree(points);
        AbortPath(dc);
        return FALSE;
    }
    result = GetPath(dc, points, types, nPoints);
    /*
     * We only needed the path as a reusable template.
     */
    AbortPath(dc);
    if (result != nPoints) {
        ckfree(types);
        ckfree(points);
        return FALSE;
    }
    *pointsPtrPtr = points;
    *typesPtrPtr = types;
    *nPointsPtr = nPoints;
    return TRUE;
}

/*
 *----------------------------------------------------------------------
 *
 * LoadCirclePath --
 *
 *      Copy one GDI-generated ellipse path into a PolyDraw batch,
 *      translating it to the requested symbol center.
 *
 *----------------------------------------------------------------------
 */
static void LoadCirclePath(POINT *dstPoints, BYTE *dstTypes, const POINT *templatePoints, const BYTE *templateTypes,
                           int nTemplatePoints, LONG x, LONG y, int radius) {
    LONG dx;
    LONG dy;
    int i;

    dx = x - radius;
    dy = y - radius;
    for (i = 0; i < nTemplatePoints; i++) {
        dstPoints[i].x = templatePoints[i].x + dx;
        dstPoints[i].y = templatePoints[i].y + dy;
    }
    memcpy(dstTypes, templateTypes, (size_t)nTemplatePoints * sizeof(*dstTypes));
}

/*
 *----------------------------------------------------------------------
 *
 * DrawCircleCentersDirect --
 *
 *      Existing one-Ellipse-per-symbol fallback.
 *
 *----------------------------------------------------------------------
 */
static void DrawCircleCentersDirect(HDC dc, const POINT *centers, int nCenters, int radius) {
    int i;

    for (i = 0; i < nCenters; i++) {
        Ellipse(dc, centers[i].x - radius, centers[i].y - radius, centers[i].x + radius + 1, centers[i].y + radius + 1);
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawCirclePathBatch --
 *
 *      Render several translated copies of the GDI-generated ellipse
 *      path.
 *
 *      Outline-only circles can be sent directly to PolyDraw.
 *
 *      Filled circles require a path bracket so FillPath or
 *      StrokeAndFillPath can consume the combined figures.
 *
 *----------------------------------------------------------------------
 */
static int DrawCirclePathBatch(HDC dc, const POINT *points, const BYTE *types, int nPoints, int doFill, int doOutline) {
    if (!doFill) {
        /*
         * PolyDraw directly strokes all disjoint figures.
         */
        return PolyDraw(dc, points, types, nPoints);
    }
    if (!BeginPath(dc)) {
        return FALSE;
    }
    if (!PolyDraw(dc, points, types, nPoints)) {
        AbortPath(dc);
        return FALSE;
    }
    if (!EndPath(dc)) {
        AbortPath(dc);
        return FALSE;
    }
    if (doOutline) {
        return StrokeAndFillPath(dc);
    }
    return FillPath(dc);
}

/*
 *----------------------------------------------------------------------
 *
 * DrawCircles --
 *
 *      Batched Windows circle-symbol renderer.
 *
 *----------------------------------------------------------------------
 */
static void DrawCircles(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, Tcl_Size nSymbolPts,
                        const Point2D *symbolPts, int radius) {
    Rbc_WinDrawableDC *dcStatePtr;
    const Point2D *pointPtr;
    const Point2D *endPtr;
    POINT *templatePoints;
    POINT *batchPoints;
    POINT *batchCenters;
    BYTE *templateTypes;
    BYTE *batchTypes;
    HBRUSH brush;
    HBRUSH oldBrush;
    HPEN pen;
    HPEN oldPen;
    HDC dc;
    Tcl_Size maxBatch;
    Tcl_Size nBuffered;
    size_t nBatchPoints;
    int nTemplatePoints;
    int doFill;
    int doOutline;
    int oldFillMode;
    int useBatch;

    if ((drawable == None) || (nSymbolPts <= 0)) {
        return;
    }
    doFill = (style->fillGC != NULL);
    doOutline = ((style->outlineGC != NULL) && (style->outlineWidth > 0));
    if (!doFill && !doOutline) {
        return;
    }
    templatePoints = NULL;
    templateTypes = NULL;
    batchPoints = NULL;
    batchTypes = NULL;
    batchCenters = NULL;
    dc = Rbc_WinAcquireDrawableDC(display, drawable, &dcStatePtr);
    /*
     * Set up the same brush and pen used by the old Ellipse renderer.
     */
    if (doFill) {
        brush = CreateSolidBrush(style->fillGC->foreground);
    } else {
        brush = GetStockBrush(NULL_BRUSH);
    }
    if (doOutline) {
        pen = Rbc_GCToPen(dc, style->outlineGC);
    } else {
        pen = GetStockPen(NULL_PEN);
    }
    oldBrush = SelectBrush(dc, brush);
    oldPen = SelectPen(dc, pen);
    /*
     * Combining overlapping symbols into one filled path is only
     * equivalent to the old renderer for the normal GXcopy case.
     *
     * When both fill and outline are used, require equal colors too:
     * otherwise drawing all figures as one path would change which
     * outlines remain visible where symbols overlap.
     */
    useBatch = TRUE;
    if (doFill && (style->fillGC->function != GXcopy)) {
        useBatch = FALSE;
    }
    if (doOutline && (style->outlineGC->function != GXcopy)) {
        useBatch = FALSE;
    }
    if (doFill && doOutline && (style->fillGC->foreground != style->outlineGC->foreground)) {
        useBatch = FALSE;
    }
    if (useBatch) {
        Rbc_WinSetROP2(dc, GXcopy);
        if (!GetCirclePathTemplate(dc, radius, &templatePoints, &templateTypes, &nTemplatePoints)) {
            useBatch = FALSE;
        }
    }
    if (useBatch) {
        if (doFill) {
            maxBatch = CIRCLE_FILL_BATCH;
        } else if (style->outlineWidth > 1) {
            maxBatch = CIRCLE_WIDE_OUTLINE_BATCH;
        } else {
            maxBatch = CIRCLE_OUTLINE_BATCH;
        }
        if ((size_t)nTemplatePoints > SIZE_MAX / (size_t)maxBatch) {
            useBatch = FALSE;
        } else {
            nBatchPoints = (size_t)nTemplatePoints * (size_t)maxBatch;
            if (nBatchPoints > SIZE_MAX / sizeof(*batchPoints)) {
                useBatch = FALSE;
            }
        }
        if (useBatch) {
            batchPoints = Tcl_AttemptAlloc(nBatchPoints * sizeof(*batchPoints));
            batchTypes = Tcl_AttemptAlloc(nBatchPoints * sizeof(*batchTypes));
            batchCenters = Tcl_AttemptAlloc((size_t)maxBatch * sizeof(*batchCenters));
            if ((batchPoints == NULL) || (batchTypes == NULL) || (batchCenters == NULL)) {
                useBatch = FALSE;
            }
        }
    }
    if (!useBatch) {
        /*
         * Preserve the old implementation for unusual GCs or if the
         * batching setup cannot be constructed.
         */
        for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
            int draw;

            if (style->interval > 0) {
                draw = DRAW_SYMBOL(style);
                style->counter++;
            } else {
                draw = TRUE;
            }
            if (!draw) {
                continue;
            }
            Ellipse(dc, (int)pointPtr->x - radius, (int)pointPtr->y - radius, (int)pointPtr->x + radius + 1,
                    (int)pointPtr->y + radius + 1);
        }
        goto done;
    }
    oldFillMode = 0;
    if (doFill) {
        /*
         * All circle figures have the same winding direction.  WINDING
         * therefore gives their union instead of parity cancellation in
         * overlapping areas.
         */
        oldFillMode = SetPolyFillMode(dc, WINDING);
    }
    nBuffered = 0;
    for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
        POINT center;
        POINT *dstPoints;
        BYTE *dstTypes;
        int draw;

        if (style->interval > 0) {
            draw = DRAW_SYMBOL(style);
            style->counter++;
        } else {
            draw = TRUE;
        }
        if (!draw) {
            continue;
        }
        center.x = (LONG)pointPtr->x;
        center.y = (LONG)pointPtr->y;
        batchCenters[nBuffered] = center;
        dstPoints = batchPoints + nBuffered * nTemplatePoints;
        dstTypes = batchTypes + nBuffered * nTemplatePoints;
        LoadCirclePath(dstPoints, dstTypes, templatePoints, templateTypes, nTemplatePoints, center.x, center.y, radius);
        nBuffered++;
        if (nBuffered == maxBatch) {
            int nPoints;

            nPoints = (int)(nBuffered * (Tcl_Size)nTemplatePoints);
            if (!DrawCirclePathBatch(dc, batchPoints, batchTypes, nPoints, doFill, doOutline)) {
                /*
                 * GXcopy makes it safe to redraw a failed batch with
                 * the old implementation.
                 */
                DrawCircleCentersDirect(dc, batchCenters, (int)nBuffered, radius);
            }
            nBuffered = 0;
        }
    }
    if (nBuffered > 0) {
        int nPoints;

        nPoints = (int)(nBuffered * (Tcl_Size)nTemplatePoints);
        if (!DrawCirclePathBatch(dc, batchPoints, batchTypes, nPoints, doFill, doOutline)) {
            DrawCircleCentersDirect(dc, batchCenters, (int)nBuffered, radius);
        }
    }
    if (doFill && (oldFillMode != 0)) {
        SetPolyFillMode(dc, oldFillMode);
    }

done:
    if (batchCenters != NULL) {
        ckfree(batchCenters);
    }
    if (batchTypes != NULL) {
        ckfree(batchTypes);
    }
    if (batchPoints != NULL) {
        ckfree(batchPoints);
    }
    if (templateTypes != NULL) {
        ckfree(templateTypes);
    }
    if (templatePoints != NULL) {
        ckfree(templatePoints);
    }
    SelectPen(dc, oldPen);
    SelectBrush(dc, oldBrush);
    if (doOutline) {
        DeletePen(pen);
    }
    if (doFill) {
        DeleteBrush(brush);
    }
    Rbc_WinReleaseDrawableDC(dcStatePtr);
}

#else

/*
 *----------------------------------------------------------------------
 *
 * DrawCircles --
 *
 *      TODO: Description
 *
 * Parameters:
 *      Display *display
 *      Drawable drawable
 *      Line *linePtr
 *      Rbc_RenderNativeSymbolStyle *style
 *      int nSymbolPts
 *      const Point2D *symbolPts
 *      int radius
 *
 * Results:
 *      TODO: Results
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 *----------------------------------------------------------------------
 */
static void DrawCircles(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, Tcl_Size nSymbolPts,
                        const Point2D *symbolPts, int radius) {
    Tcl_Size i;
    XArc *arcArr; /* Array of arcs (circle) */
    XArc *arcPtr;
    int reqSize, nArcs;
    int s;
    Tcl_Size count;
    const Point2D *pointPtr, *endPtr;

    s = radius + radius;
    if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / sizeof(*arcArr))) {
        return;
    }
    arcArr = Tcl_AttemptAlloc((size_t)nSymbolPts * sizeof(*arcArr));
    if (arcArr == NULL) {
        return;
    }
    arcPtr = arcArr;
    if (style->interval > 0) {
        count = 0;
        for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
            if (DRAW_SYMBOL(style)) {
                arcPtr->x = (short int)pointPtr->x - radius;
                arcPtr->y = (short int)pointPtr->y - radius;
                arcPtr->width = arcPtr->height = (unsigned short)s;
                arcPtr->angle1 = 0;
                arcPtr->angle2 = 23040;
                arcPtr++, count++;
            }
            style->counter++;
        }
    } else {
        count = nSymbolPts;
        for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
            arcPtr->x = (short int)pointPtr->x - radius;
            arcPtr->y = (short int)pointPtr->y - radius;
            arcPtr->width = arcPtr->height = (unsigned short)s;
            arcPtr->angle1 = 0;
            arcPtr->angle2 = 23040;
            arcPtr++;
        }
    }
    reqSize = MAX_DRAWARCS(display);
    if (reqSize < 1) {
        ckfree(arcArr);
        return;
    }
    for (i = 0; i < count; i += (Tcl_Size)reqSize) {
        Tcl_Size remaining;

        remaining = count - i;
        nArcs = (remaining > (Tcl_Size)reqSize) ? reqSize : (int)remaining;
        if (style->fillGC != NULL) {
            XFillArcs(display, drawable, style->fillGC, arcArr + i, nArcs);
        }
        if (style->outlineWidth > 0) {
            XDrawArcs(display, drawable, style->outlineGC, arcArr + i, nArcs);
        }
    }
    ckfree((char *)arcArr);
}

#endif

#ifdef WIN32

static void DrawPolygonSymbols(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, const XPoint *polygons,
                               Tcl_Size nPolygons, int stride, int nVertices);

#endif /* WIN32 */

#ifdef WIN32

/*
 *----------------------------------------------------------------------
 *
 * DrawSquares --
 *
 *      Draw square symbols on Windows using the batched polygon-symbol
 *      renderer.  X11 retains its native XFillRectangles/XDrawRectangles
 *      implementation below.
 *
 *      Each square is represented by four vertices plus a repeated first
 *      vertex for the closed outline:
 *
 *          0 ----- 1
 *          |       |
 *          |       |
 *          3 ----- 2
 *
 *          4 == 0
 *
 *----------------------------------------------------------------------
 */
static void DrawSquares(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, Tcl_Size nSymbolPts,
                        const Point2D *symbolPts, int r) {
    XPoint *polygons;
    XPoint *p;
    const Point2D *pointPtr;
    const Point2D *endPtr;
    Tcl_Size count;
    int s;

    if ((drawable == None) || (nSymbolPts <= 0)) {
        return;
    }
    if ((style->fillGC == NULL) && ((style->outlineGC == NULL) || (style->outlineWidth <= 0))) {
        return;
    }
    if ((size_t)nSymbolPts > SIZE_MAX / (5u * sizeof(*polygons))) {
        return;
    }
    polygons = Tcl_AttemptAlloc((size_t)nSymbolPts * 5u * sizeof(*polygons));
    if (polygons == NULL) {
        return;
    }
    s = r + r;
    p = polygons;
    if (style->interval > 0) {
        count = 0;

        for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
            if (DRAW_SYMBOL(style)) {
                short x;
                short y;

                x = (short)(pointPtr->x - r);
                y = (short)(pointPtr->y - r);
                p[0].x = x;
                p[0].y = y;
                p[1].x = (short)(x + s);
                p[1].y = y;
                p[2].x = (short)(x + s);
                p[2].y = (short)(y + s);
                p[3].x = x;
                p[3].y = (short)(y + s);
                p[4] = p[0];
                p += 5;
                count++;
            }
            style->counter++;
        }
    } else {
        count = nSymbolPts;
        for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
            short x;
            short y;

            x = (short)(pointPtr->x - r);
            y = (short)(pointPtr->y - r);
            p[0].x = x;
            p[0].y = y;
            p[1].x = (short)(x + s);
            p[1].y = y;
            p[2].x = (short)(x + s);
            p[2].y = (short)(y + s);
            p[3].x = x;
            p[3].y = (short)(y + s);
            p[4] = p[0];
            p += 5;
        }
    }
    DrawPolygonSymbols(display, drawable, style, polygons, count, 5, 4);
    ckfree(polygons);
}

#else

/*
 *----------------------------------------------------------------------
 *
 * DrawSquares --
 *
 *      Draw square symbols using native X11 rectangle operations.
 *
 *----------------------------------------------------------------------
 */
static void DrawSquares(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, Tcl_Size nSymbolPts,
                        const Point2D *symbolPts, int r) {
    XRectangle *rectArr;
    const Point2D *pointPtr, *endPtr;
    XRectangle *rectPtr;
    int reqSize;
    int nRects;
    int s;
    Tcl_Size i;
    Tcl_Size count;

    s = r + r;
    if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / sizeof(*rectArr))) {
        return;
    }
    rectArr = Tcl_AttemptAlloc((size_t)nSymbolPts * sizeof(*rectArr));
    if (rectArr == NULL) {
        return;
    }
    rectPtr = rectArr;
    if (style->interval > 0) {
        count = 0;
        for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
            if (DRAW_SYMBOL(style)) {
                rectPtr->x = (short int)(pointPtr->x - r);
                rectPtr->y = (short int)(pointPtr->y - r);
                rectPtr->width = rectPtr->height = (unsigned short)s;
                rectPtr++;
                count++;
            }
            style->counter++;
        }
    } else {
        count = nSymbolPts;
        for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
            rectPtr->x = (short int)(pointPtr->x - r);
            rectPtr->y = (short int)(pointPtr->y - r);
            rectPtr->width = rectPtr->height = (unsigned short)s;
            rectPtr++;
        }
    }
    reqSize = MAX_DRAWRECTANGLES(display);
    if (reqSize < 1) {
        ckfree(rectArr);
        return;
    }
    for (i = 0; i < count; i += (Tcl_Size)reqSize) {
        Tcl_Size remaining;

        remaining = count - i;
        nRects = (remaining > (Tcl_Size)reqSize) ? reqSize : (int)remaining;
        if (style->fillGC != NULL) {
            XFillRectangles(display, drawable, style->fillGC, rectArr + i, nRects);
        }
        if (style->outlineWidth > 0) {
            XDrawRectangles(display, drawable, style->outlineGC, rectArr + i, nRects);
        }
    }
    ckfree(rectArr);
}

#endif /* WIN32 */

#ifdef WIN32

/*
 *----------------------------------------------------------------------
 *
 * LoadPolygonBatch --
 *
 *      Convert a batch of RBC XPoint polygon data to Windows POINTs.
 *
 *      "stride" is the number of XPoints occupied by one source
 *      polygon.  "nPoints" is the number copied for this operation.
 *
 *      For fills, nPoints excludes RBC's repeated closing point.
 *      For outlines, nPoints == stride and therefore includes it.
 *
 *----------------------------------------------------------------------
 */
static void LoadPolygonBatch(POINT *points, const XPoint *polygons, Tcl_Size first, Tcl_Size count, int stride,
                             int nPoints) {
    Tcl_Size i;
    int j;

    for (i = 0; i < count; i++) {
        const XPoint *srcPtr;
        POINT *dstPtr;

        srcPtr = polygons + (first + i) * stride;
        dstPtr = points + i * nPoints;
        for (j = 0; j < nPoints; j++) {
            dstPtr[j].x = (LONG)srcPtr[j].x;
            dstPtr[j].y = (LONG)srcPtr[j].y;
        }
    }
}

/*
 *----------------------------------------------------------------------
 *
 * DrawPolygonSymbols --
 *
 *      Batch Windows rendering of independent polygon symbols.
 *
 *      Under GXcopy, overlapping solid fills are visually equivalent
 *      to the union of the individual polygons.  Use WINDING mode so
 *      overlapping polygons do not cancel one another as they would
 *      if thousands of independent symbols were combined under the
 *      default EvenOddRule.
 *
 *      Outlines are independent closed polylines and therefore map
 *      naturally to PolyPolyline.
 *
 *      For non-GXcopy raster operations, retain per-polygon drawing so
 *      overlapping symbols preserve repeated raster-operation effects.
 *
 *----------------------------------------------------------------------
 */
static void DrawPolygonSymbols(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, const XPoint *polygons,
                               Tcl_Size nPolygons, int stride, int nVertices) {
    enum {
        /*
         * GDI/EMF point budget for polygon outlines.  PolyPolyline scales
         * well with large batches.
         */
        POLYGON_BATCH_POINTS = 16384,
        /*
         * Filled polygon symbols behave very differently under
         * PolyPolygon when many symbols overlap.  These batch sizes were
         * selected from the Windows rendering benchmark.
         */
        POLYGON_PLUS_FILL_BATCH = 16,
        POLYGON_CROSS_FILL_BATCH = 8,
        POLYGON_CONVEX_FILL_BATCH = 32,
        /*
         * Conservative point budget for wide geometric outline pens.
         */
        POLYGON_WIDE_BATCH_POINTS = 1360
    };
    Rbc_WinDrawableDC *dcStatePtr;
    HDC dc;
    POINT *points;
    INT *polygonCounts;
    DWORD *polylineCounts;
    Tcl_Size maxPolygons;
    Tcl_Size maxFillPolygons;
    Tcl_Size first;
    Tcl_Size i;
    int maxPoints;

    if ((drawable == None) || (polygons == NULL) || (nPolygons <= 0) || (nVertices < 3) || (stride < nVertices)) {
        return;
    }
    if ((style->fillGC == NULL) && ((style->outlineGC == NULL) || (style->outlineWidth <= 0))) {
        return;
    }

    /*
     * Size the shared buffer for the larger, closed outline
     * representation.
     */
    if (style->outlineWidth > 1) {
        maxPoints = POLYGON_WIDE_BATCH_POINTS;
    } else {
        maxPoints = POLYGON_BATCH_POINTS;
    }
    maxPolygons = maxPoints / stride;
    if (maxPolygons < 1) {
        maxPolygons = 1;
    }
    if (maxPolygons > nPolygons) {
        maxPolygons = nPolygons;
    }
    /*
     * Polygon outlines scale well with large PolyPolyline batches, but
     * large overlapping PolyPolygon fills are extremely expensive on
     * Windows.  Limit only the fill batch size.
     */
    switch (style->type) {
    case RBC_RENDER_SYMBOL_PLUS:
        maxFillPolygons = POLYGON_PLUS_FILL_BATCH;
        break;
    case RBC_RENDER_SYMBOL_CROSS:
        maxFillPolygons = POLYGON_CROSS_FILL_BATCH;
        break;
    case RBC_RENDER_SYMBOL_SQUARE:
    case RBC_RENDER_SYMBOL_DIAMOND:
    case RBC_RENDER_SYMBOL_TRIANGLE:
    case RBC_RENDER_SYMBOL_ARROW:
    default:
        maxFillPolygons = POLYGON_CONVEX_FILL_BATCH;
        break;
    }
    if (maxFillPolygons > maxPolygons) {
        maxFillPolygons = maxPolygons;
    }
    if ((size_t)maxPolygons > SIZE_MAX / (size_t)stride / sizeof(*points)) {
        return;
    }
    points = Tcl_AttemptAlloc((size_t)maxPolygons * (size_t)stride * sizeof(*points));
    if (points == NULL) {
        return;
    }
    if ((size_t)maxPolygons > SIZE_MAX / sizeof(*polygonCounts)) {
        ckfree(points);
        return;
    }
    polygonCounts = Tcl_AttemptAlloc((size_t)maxPolygons * sizeof(*polygonCounts));
    if (polygonCounts == NULL) {
        ckfree(points);
        return;
    }
    if ((size_t)maxPolygons > SIZE_MAX / sizeof(*polylineCounts)) {
        ckfree(polygonCounts);
        ckfree(points);
        return;
    }
    polylineCounts = Tcl_AttemptAlloc((size_t)maxPolygons * sizeof(*polylineCounts));
    if (polylineCounts == NULL) {
        ckfree(polygonCounts);
        ckfree(points);
        return;
    }
    for (i = 0; i < maxPolygons; i++) {
        polygonCounts[i] = nVertices;
        polylineCounts[i] = (DWORD)stride;
    }
    dc = Rbc_WinAcquireDrawableDC(display, drawable, &dcStatePtr);
    /*
     * Fill pass.
     */
    if (style->fillGC != NULL) {
        GC gc;
        HBRUSH brush, oldBrush;
        HPEN oldPen;
        int oldFillMode;

        gc = style->fillGC;
        Rbc_WinSetROP2(dc, gc->function);
        brush = CreateSolidBrush(gc->foreground);
        oldBrush = SelectBrush(dc, brush);
        oldPen = SelectPen(dc, GetStockPen(NULL_PEN));
        if (gc->function == GXcopy) {
            /*
             * Every RBC symbol polygon is an independent simple
             * polygon with the same orientation.  For GXcopy,
             * combining them under WINDING gives the same final
             * solid-color result as painting them independently,
             * including where symbols overlap.
             */
            oldFillMode = SetPolyFillMode(dc, WINDING);
            first = 0;
            while (first < nPolygons) {
                Tcl_Size count;

                count = nPolygons - first;
                if (count > maxFillPolygons) {
                    count = maxFillPolygons;
                }
                LoadPolygonBatch(points, polygons, first, count, stride, nVertices);
                if (!PolyPolygon(dc, points, polygonCounts, (int)count)) {
                    for (i = 0; i < count; i++) {
                        Polygon(dc, points + i * nVertices, nVertices);
                    }
                }
                first += count;
            }
        } else {
            int fillMode;

            /*
             * Non-copy raster operations are order-sensitive when
             * symbols overlap.  Draw each polygon separately.
             */
            fillMode = (gc->fill_rule == EvenOddRule) ? ALTERNATE : WINDING;
            oldFillMode = SetPolyFillMode(dc, fillMode);
            first = 0;
            while (first < nPolygons) {
                Tcl_Size count;

                count = nPolygons - first;
                if (count > maxFillPolygons) {
                    count = maxFillPolygons;
                }
                LoadPolygonBatch(points, polygons, first, count, stride, nVertices);
                for (i = 0; i < count; i++) {
                    Polygon(dc, points + i * nVertices, nVertices);
                }
                first += count;
            }
        }
        if (oldFillMode != 0) {
            SetPolyFillMode(dc, oldFillMode);
        }
        SelectPen(dc, oldPen);
        DeleteBrush(SelectBrush(dc, oldBrush));
    }
    /*
     * Outline pass.
     */
    if ((style->outlineGC != NULL) && (style->outlineWidth > 0)) {
        GC gc;
        HPEN pen, oldPen;
        HBRUSH oldBrush;

        gc = style->outlineGC;
        Rbc_WinSetROP2(dc, gc->function);
        pen = Rbc_GCToPen(dc, gc);
        oldPen = SelectPen(dc, pen);
        oldBrush = SelectBrush(dc, GetStockBrush(NULL_BRUSH));
        first = 0;
        while (first < nPolygons) {
            Tcl_Size count;

            count = nPolygons - first;
            if (count > maxPolygons) {
                count = maxPolygons;
            }
            /*
             * The source polygon includes RBC's repeated first
             * vertex, so each PolyPolyline entry is already closed.
             */
            LoadPolygonBatch(points, polygons, first, count, stride, stride);
            if (gc->function == GXcopy) {
                if (!PolyPolyline(dc, points, polylineCounts, (DWORD)count)) {
                    for (i = 0; i < count; i++) {
                        Polyline(dc, points + i * stride, stride);
                    }
                }
            } else {
                /*
                 * Preserve repeated raster-operation effects for
                 * overlapping non-copy outlines.
                 */
                for (i = 0; i < count; i++) {
                    Polyline(dc, points + i * stride, stride);
                }
            }
            first += count;
        }
        SelectBrush(dc, oldBrush);
        DeletePen(SelectPen(dc, oldPen));
    }
    Rbc_WinReleaseDrawableDC(dcStatePtr);
    ckfree(polylineCounts);
    ckfree(polygonCounts);
    ckfree(points);
}

#endif /* WIN32 */

void Rbc_NativeSymbolPoints(Graph *graphPtr, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, int size,
                                 Tcl_Size nSymbolPts, const Point2D *symbolPts) {
    XPoint pattern[13]; /* Template for polygon symbols */
    int r1, r2;
    Tcl_Size i, n;
    Tcl_Size count;
    const Point2D *pointPtr, *endPtr;
#define SQRT_PI 1.77245385090552
#define S_RATIO 0.886226925452758

    if (size < 3) {
        if ((style->fillGC != NULL) && (nSymbolPts > 0)) {
            XPoint *points;
            Tcl_Size offset;
            int maxPoints;

            maxPoints = Rbc_MaxRequestSize(graphPtr->display, sizeof(XPoint));
            if (maxPoints < 1) {
                return;
            }
            if ((size_t)maxPoints > SIZE_MAX / sizeof(*points)) {
                return;
            }
            points = Tcl_AttemptAlloc((size_t)maxPoints * sizeof(*points));
            if (points == NULL) {
                return;
            }
            offset = 0;
            while (offset < nSymbolPts) {
                Tcl_Size remaining;
                int chunk;
                int j;

                remaining = nSymbolPts - offset;
                chunk = (remaining > (Tcl_Size)maxPoints) ? maxPoints : (int)remaining;
                for (j = 0; j < chunk; j++) {
                    points[j].x = (short int)symbolPts[offset + j].x;
                    points[j].y = (short int)symbolPts[offset + j].y;
                }
                XDrawPoints(graphPtr->display, drawable, style->fillGC, points, chunk, CoordModeOrigin);
                offset += chunk;
            }
            ckfree(points);
        }
        return;
    }
    r1 = (int)ceil(size * 0.5);
    r2 = (int)ceil(size * S_RATIO * 0.5);
    switch (style->type) {
    case RBC_RENDER_SYMBOL_NONE:
        break;
    case RBC_RENDER_SYMBOL_SQUARE:
        DrawSquares(graphPtr->display, drawable, style, nSymbolPts, symbolPts, r2);
        break;
    case RBC_RENDER_SYMBOL_CIRCLE:
        DrawCircles(graphPtr->display, drawable, style, nSymbolPts, symbolPts, r1);
        break;
    case RBC_RENDER_SYMBOL_SPLUS:
    case RBC_RENDER_SYMBOL_SCROSS: {
        XSegment *segArr; /* Array of line segments (splus, scross) */
        register XSegment *segPtr;
        Tcl_Size nSegs;
        int reqSize;
        int chunk;

        if (style->type == RBC_RENDER_SYMBOL_SCROSS) {
            r2 = Round(r2 * M_SQRT1_2);
            pattern[3].y = pattern[2].x = pattern[0].x = pattern[0].y = -r2;
            pattern[3].x = pattern[2].y = pattern[1].y = pattern[1].x = r2;
        } else {
            pattern[0].y = pattern[1].y = pattern[2].x = pattern[3].x = 0;
            pattern[0].x = pattern[2].y = -r2;
            pattern[1].x = pattern[3].y = r2;
        }
        if ((nSymbolPts > TCL_SIZE_MAX / 2) || ((size_t)nSymbolPts > SIZE_MAX / (2 * sizeof(*segArr)))) {
            return;
        }
        segArr = Tcl_AttemptAlloc((size_t)nSymbolPts * 2 * sizeof(*segArr));
        if (segArr == NULL) {
            return;
        }
        segPtr = segArr;
        if (style->interval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(style)) {
                    segPtr->x1 = pattern[0].x + (short int)pointPtr->x;
                    segPtr->y1 = pattern[0].y + (short int)pointPtr->y;
                    segPtr->x2 = pattern[1].x + (short int)pointPtr->x;
                    segPtr->y2 = pattern[1].y + (short int)pointPtr->y;
                    segPtr++;
                    segPtr->x1 = pattern[2].x + (short int)pointPtr->x;
                    segPtr->y1 = pattern[2].y + (short int)pointPtr->y;
                    segPtr->x2 = pattern[3].x + (short int)pointPtr->x;
                    segPtr->y2 = pattern[3].y + (short int)pointPtr->y;
                    segPtr++;
                    count++;
                }
                style->counter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                segPtr->x1 = pattern[0].x + (short int)pointPtr->x;
                segPtr->y1 = pattern[0].y + (short int)pointPtr->y;
                segPtr->x2 = pattern[1].x + (short int)pointPtr->x;
                segPtr->y2 = pattern[1].y + (short int)pointPtr->y;
                segPtr++;
                segPtr->x1 = pattern[2].x + (short int)pointPtr->x;
                segPtr->y1 = pattern[2].y + (short int)pointPtr->y;
                segPtr->x2 = pattern[3].x + (short int)pointPtr->x;
                segPtr->y2 = pattern[3].y + (short int)pointPtr->y;
                segPtr++;
            }
        }
        nSegs = count * 2;
        reqSize = MAX_DRAWSEGMENTS(graphPtr->display);
        if (reqSize < 1) {
            ckfree(segArr);
            return;
        }
        for (i = 0; i < nSegs; i += (Tcl_Size)reqSize) {
            Tcl_Size remaining;

            remaining = nSegs - i;
            chunk = (remaining > (Tcl_Size)reqSize) ? reqSize : (int)remaining;
            XDrawSegments(graphPtr->display, drawable, style->outlineGC, segArr + i, chunk);
        }
        ckfree((char *)segArr);
    } break;
    case RBC_RENDER_SYMBOL_PLUS:
    case RBC_RENDER_SYMBOL_CROSS: {
        XPoint *polygon;
        register XPoint *p;
        int d; /* Small delta for cross/plus thickness */

        d = (r2 / 3);
        /*
         *
         *          2   3       The plus/cross symbol is a closed polygon
         *                      of 12 points. The diagram to the left
         *    0,12  1   4    5  represents the positions of the points
         *           x,y        which are computed below. The extra
         *     11  10   7    6  (thirteenth) point connects the first and
         *                      last points.
         *          9   8
         */
        pattern[0].x = pattern[11].x = pattern[12].x = -r2;
        pattern[2].x = pattern[1].x = pattern[10].x = pattern[9].x = -d;
        pattern[3].x = pattern[4].x = pattern[7].x = pattern[8].x = d;
        pattern[5].x = pattern[6].x = r2;
        pattern[2].y = pattern[3].y = -r2;
        pattern[0].y = pattern[1].y = pattern[4].y = pattern[5].y = pattern[12].y = -d;
        pattern[11].y = pattern[10].y = pattern[7].y = pattern[6].y = d;
        pattern[9].y = pattern[8].y = r2;
        if (style->type == RBC_RENDER_SYMBOL_CROSS) {
            double dx, dy;

            /* For the cross symbol, rotate the points by 45 degrees. */
            for (n = 0; n < 12; n++) {
                dx = (double)pattern[n].x * M_SQRT1_2;
                dy = (double)pattern[n].y * M_SQRT1_2;
                pattern[n].x = Round(dx - dy);
                pattern[n].y = Round(dx + dy);
            }
            pattern[12] = pattern[0];
        }
        if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / (13u * sizeof(*polygon)))) {
            return;
        }
        polygon = Tcl_AttemptAlloc((size_t)nSymbolPts * 13u * sizeof(*polygon));
        if (polygon == NULL) {
            return;
        }
        p = polygon;
        if (style->interval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(style)) {
                    for (n = 0; n < 13; n++) {
                        p->x = pattern[n].x + (short int)pointPtr->x;
                        p->y = pattern[n].y + (short int)pointPtr->y;
                        p++;
                    }
                    count++;
                }
                style->counter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                for (n = 0; n < 13; n++) {
                    p->x = pattern[n].x + (short int)pointPtr->x;
                    p->y = pattern[n].y + (short int)pointPtr->y;
                    p++;
                }
            }
        }
#ifdef WIN32
        DrawPolygonSymbols(graphPtr->display, drawable, style, polygon, count, 13, 12);
#else
        if (style->fillGC != NULL) {
            for (p = polygon, i = 0; i < count; i++, p += 13) {
                XFillPolygon(graphPtr->display, drawable, style->fillGC, p, 13, Complex, CoordModeOrigin);
            }
        }
        if (style->outlineWidth > 0) {
            for (p = polygon, i = 0; i < count; i++, p += 13) {
                XDrawLines(graphPtr->display, drawable, style->outlineGC, p, 13, CoordModeOrigin);
            }
        }
#endif
        ckfree((char *)polygon);
    } break;
    case RBC_RENDER_SYMBOL_DIAMOND: {
        XPoint *polygon;
        register XPoint *p;
        /*
         *
         *                      The plus symbol is a closed polygon
         *            1         of 4 points. The diagram to the left
         *                      represents the positions of the points
         *       0,4 x,y  2     which are computed below. The extra
         *                      (fifth) point connects the first and
         *            3         last points.
         *
         */
        pattern[1].y = pattern[0].x = -r1;
        pattern[2].y = pattern[3].x = pattern[0].y = pattern[1].x = 0;
        pattern[3].y = pattern[2].x = r1;
        pattern[4] = pattern[0];
        if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / (5u * sizeof(*polygon)))) {
            return;
        }
        polygon = Tcl_AttemptAlloc((size_t)nSymbolPts * 5u * sizeof(*polygon));
        if (polygon == NULL) {
            return;
        }
        p = polygon;
        if (style->interval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(style)) {
                    for (n = 0; n < 5; n++, p++) {
                        p->x = pattern[n].x + (short int)pointPtr->x;
                        p->y = pattern[n].y + (short int)pointPtr->y;
                    }
                    count++;
                }
                style->counter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                for (n = 0; n < 5; n++, p++) {
                    p->x = pattern[n].x + (short int)pointPtr->x;
                    p->y = pattern[n].y + (short int)pointPtr->y;
                }
            }
        }
#ifdef WIN32
        DrawPolygonSymbols(graphPtr->display, drawable, style, polygon, count, 5, 4);
#else
        if (style->fillGC != NULL) {
            for (p = polygon, i = 0; i < count; i++, p += 5) {
                XFillPolygon(graphPtr->display, drawable, style->fillGC, p, 5, Convex, CoordModeOrigin);
            }
        }
        if (style->outlineWidth > 0) {
            for (p = polygon, i = 0; i < count; i++, p += 5) {
                XDrawLines(graphPtr->display, drawable, style->outlineGC, p, 5, CoordModeOrigin);
            }
        }
#endif
        ckfree((char *)polygon);
    } break;
    case RBC_RENDER_SYMBOL_TRIANGLE:
    case RBC_RENDER_SYMBOL_ARROW: {
        XPoint *polygon;
        register XPoint *p;
        double b;
        int b2, h1, h2;
#define H_RATIO 1.1663402261671607
#define B_RATIO 1.3467736870885982
#define TAN30 0.57735026918962573
#define COS30 0.86602540378443871

        b = Round(size * B_RATIO * 0.7);
        b2 = Round(b * 0.5);
        h2 = Round(TAN30 * b2);
        h1 = Round(b2 / COS30);
        /*
         *
         *                      The triangle symbol is a closed polygon
         *           0,3         of 3 points. The diagram to the left
         *                      represents the positions of the points
         *           x,y        which are computed below. The extra
         *                      (fourth) point connects the first and
         *      2           1   last points.
         *
         */
        if (style->type == RBC_RENDER_SYMBOL_ARROW) {
            pattern[3].x = pattern[0].x = 0;
            pattern[3].y = pattern[0].y = h1;
            pattern[1].x = b2;
            pattern[2].y = pattern[1].y = -h2;
            pattern[2].x = -b2;
        } else {
            pattern[3].x = pattern[0].x = 0;
            pattern[3].y = pattern[0].y = -h1;
            pattern[1].x = b2;
            pattern[2].y = pattern[1].y = h2;
            pattern[2].x = -b2;
        }
        if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / (4u * sizeof(*polygon)))) {
            return;
        }
        polygon = Tcl_AttemptAlloc((size_t)nSymbolPts * 4u * sizeof(*polygon));
        if (polygon == NULL) {
            return;
        }
        p = polygon;
        if (style->interval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(style)) {
                    for (n = 0; n < 4; n++) {
                        p->x = pattern[n].x + (short int)pointPtr->x;
                        p->y = pattern[n].y + (short int)pointPtr->y;
                        p++;
                    }
                    count++;
                }
                style->counter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                for (n = 0; n < 4; n++) {
                    p->x = pattern[n].x + (short int)pointPtr->x;
                    p->y = pattern[n].y + (short int)pointPtr->y;
                    p++;
                }
            }
        }
#ifdef WIN32
        DrawPolygonSymbols(graphPtr->display, drawable, style, polygon, count, 4, 3);
#else
        if (style->fillGC != NULL) {
            for (p = polygon, i = 0; i < count; i++, p += 4) {
                XFillPolygon(graphPtr->display, drawable, style->fillGC, p, 4, Convex, CoordModeOrigin);
            }
        }
        if (style->outlineWidth > 0) {
            for (p = polygon, i = 0; i < count; i++, p += 4) {
                XDrawLines(graphPtr->display, drawable, style->outlineGC, p, 4, CoordModeOrigin);
            }
        }
#endif
        ckfree((char *)polygon);
    } break;
    case RBC_RENDER_SYMBOL_BITMAP: {
        Pixmap bitmap, mask;
        int width, height, bmWidth, bmHeight;
        double scale, sx, sy;
        int dx, dy;
        int x, y;

        Tk_SizeOfBitmap(graphPtr->display, style->bitmap, &width, &height);
        mask = None;
        /*
         * Compute the size of the scaled bitmap.  Stretch the
         * bitmap to fit a nxn bounding box.
         */
        sx = (double)size / (double)width;
        sy = (double)size / (double)height;
        scale = MIN(sx, sy);
        bmWidth = (int)(width * scale);
        bmHeight = (int)(height * scale);
        XSetClipMask(graphPtr->display, style->outlineGC, None);
        if (style->mask != None) {
            mask = Rbc_ScaleBitmap(graphPtr->tkwin, style->mask, width, height, bmWidth, bmHeight);
            XSetClipMask(graphPtr->display, style->outlineGC, mask);
        }
        bitmap = Rbc_ScaleBitmap(graphPtr->tkwin, style->bitmap, width, height, bmWidth, bmHeight);
        if (style->fillGC == NULL) {
            XSetClipMask(graphPtr->display, style->outlineGC, bitmap);
        }
        dx = bmWidth / 2;
        dy = bmHeight / 2;
        if (style->interval > 0) {
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(style)) {
                    x = (int)pointPtr->x - dx;
                    y = (int)pointPtr->y - dy;
                    if ((style->fillGC == NULL) || (mask != None)) {
                        XSetClipOrigin(graphPtr->display, style->outlineGC, x, y);
                    }
                    XCopyPlane(graphPtr->display, bitmap, drawable, style->outlineGC, 0, 0, bmWidth, bmHeight,
                               x, y, 1);
                }
                style->counter++;
            }
        } else {
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                x = (int)pointPtr->x - dx;
                y = (int)pointPtr->y - dy;
                if ((style->fillGC == NULL) || (mask != None)) {
                    XSetClipOrigin(graphPtr->display, style->outlineGC, x, y);
                }
                XCopyPlane(graphPtr->display, bitmap, drawable, style->outlineGC, 0, 0, bmWidth, bmHeight, x, y,
                           1);
            }
        }
        Tk_FreePixmap(graphPtr->display, bitmap);
        if (mask != None) {
            Tk_FreePixmap(graphPtr->display, mask);
        }
    } break;
    }
}

int Rbc_NativePolygonPointCount(Display *display, Tcl_Size nPoints) {
    if ((nPoints < 3) || (nPoints > (Tcl_Size)INT_MAX)) {
        return 0;
    }
#ifndef WIN32
    {
        int maxPoints;
        maxPoints = Rbc_MaxRequestSize(display, sizeof(XPoint));
        if ((maxPoints < 3) || (nPoints > (Tcl_Size)maxPoints)) {
            return 0;
        }
    }
#else
    (void)display;
#endif
    return (int)nPoints;
}

void Rbc_NativeFillPolygon(Graph *graphPtr, Drawable drawable, GC gc, Rbc_Tile tile,
    Pixmap stipple, const Point2D *points, Tcl_Size count) {
    XPoint *nativePoints;
    Tcl_Size i;
    int n;
    if (points == NULL || (tile != NULL && Rbc_PixmapOfTile(tile) == None)) return;
    n = Rbc_NativePolygonPointCount(graphPtr->display, count);
    if (n == 0) {
        return;
    }
    nativePoints = Tcl_AttemptAlloc((size_t)n * sizeof(*nativePoints));
    if (nativePoints == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        nativePoints[i].x = (short int)points[i].x;
        nativePoints[i].y = (short int)points[i].y;
    }
    if (tile != NULL) {
        Rbc_SetTileOrigin(graphPtr->tkwin, tile, 0, 0);
        Rbc_TilePolygon(graphPtr->tkwin, drawable, tile, nativePoints, n);
    } else if (stipple != None) {
        XFillPolygon(graphPtr->display, drawable, gc, nativePoints, n, Complex, CoordModeOrigin);
    }
    ckfree(nativePoints);
}

#ifdef WIN32

/*
 * Draw the Windows geometry produced by:
 *
 *     Tk_Draw3DRectangle(..., borderWidth=1, ...)
 *
 * using an already-acquired HDC.
 */
static void DrawWin3DRectangle1(HDC dc, const Rbc_Win3DBorderColors *colorsPtr, int x, int y, int width, int height,
                                int relief) {
    COLORREF nearColor;
    COLORREF farColor;
    int borderWidth;

    borderWidth = 1;
    /*
     * Same border-width clamping as Tk_Draw3DRectangle.
     */
    if (width < 2 * borderWidth) {
        borderWidth = width / 2;
    }
    if (height < 2 * borderWidth) {
        borderWidth = height / 2;
    }
    if (borderWidth == 0) {
        return;
    }
    switch (relief) {
    case TK_RELIEF_RAISED:
        nearColor = colorsPtr->light;
        farColor = colorsPtr->dark2;
        break;
    case TK_RELIEF_SUNKEN:
        nearColor = colorsPtr->dark;
        farColor = colorsPtr->light;
        break;
    default:
        return;
    }
    /*
     * Same order as Tk_Draw3DRectangle:
     *
     *     left vertical
     *     right vertical
     *     top horizontal
     *     bottom horizontal
     */
    Rbc_WinFillRect(dc, x, y, 1, height, nearColor);
    Rbc_WinFillRect(dc, x + width - 1, y, 1, height, farColor);
    Rbc_WinFillRect(dc, x + 1, y, width - 2, 1, nearColor);
    Rbc_WinFillRect(dc, x, y + height - 1, width - 1, 1, farColor);
}

static void DrawWinRbcOuter3DBorder(HDC dc, const Rbc_Win3DBorderColors *colorsPtr, int x, int y, int width, int height,
                                    int relief) {
    COLORREF lightColor;
    COLORREF darkColor;
    int x2;
    int y2;
    if ((width <= 2) || (height <= 2)) {
        return;
    }
    switch (relief) {
    case TK_RELIEF_RAISED:
        /*
         * Current Rbc_Draw3DRectangle Windows behavior:
         *
         *     "light" = flat GC
         *     "dark"  = dark GC
         */
        lightColor = colorsPtr->flat;
        darkColor = colorsPtr->dark;
        break;
    case TK_RELIEF_SUNKEN:
        /*
         * Current Windows behavior:
         *
         *     "light" = light GC
         *     "dark"  = flat GC
         */
        lightColor = colorsPtr->light;
        darkColor = colorsPtr->flat;
        break;
    default:
        return;
    }
    x2 = x + width - 1;
    y2 = y + height - 1;
    /*
     * Preserve the exact XDrawLine ordering from
     * Rbc_Draw3DRectangle.
     */
    /* Right. */
    Rbc_WinFillRect(dc, x2, y, 1, height, darkColor);
    /* Top. */
    Rbc_WinFillRect(dc, x, y, width, 1, lightColor);
    /* Bottom. */
    Rbc_WinFillRect(dc, x, y2, width, 1, darkColor);
    /* Left, drawn last. */
    Rbc_WinFillRect(dc, x, y, 1, height, lightColor);
}

static int Clamp3DBorderWidth(int width, int height, int borderWidth) {
    if (width < 2 * borderWidth) {
        borderWidth = width / 2;
    }
    if (height < 2 * borderWidth) {
        borderWidth = height / 2;
    }
    return borderWidth;
}

/*
 *----------------------------------------------------------------------
 *
 * DrawWin3DBarBorders1 --
 *
 *      Draw one-pixel raised/sunken bar borders using a single Windows
 *      device context for the complete bar set.
 *
 *      This reproduces Tk_Draw3DRectangle(..., borderWidth=1) on
 *      Windows, but avoids four drawable-DC acquire/release cycles for
 *      every bar.
 *
 * Results:
 *      TRUE if the border configuration was handled here.
 *      FALSE if the caller must use Rbc_Draw3DRectangle instead.
 *
 *----------------------------------------------------------------------
 */
static int DrawWin3DBarBorders(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr, const Rbc_RenderRectangle *rectangles,
                               Tcl_Size nRects) {
    Rbc_Win3DBorderColors colors;
    Rbc_WinDrawableDC *dcStatePtr;
    HDC dc;
    Tcl_Size i;

    if (penPtr->border == NULL) {
        return FALSE;
    }
    if ((penPtr->relief != TK_RELIEF_RAISED) && (penPtr->relief != TK_RELIEF_SUNKEN)) {
        return FALSE;
    }
    /*
     * Optimize only the cases whose exact Windows geometry we
     * reproduce here.  Larger border widths still use Tk.
     */
    if ((penPtr->borderWidth != 1) && (penPtr->borderWidth != 2)) {
        return FALSE;
    }
    Rbc_WinGet3DBorderColors(graphPtr->tkwin, penPtr->border, &colors);
    dc = Rbc_WinAcquireDrawableDC(graphPtr->display, drawable, &dcStatePtr);
    for (i = 0; i < nRects; i++) {
        const Rbc_RenderRectangle *rectPtr;
        int x;
        int y;
        int width;
        int height;

        rectPtr = rectangles + i;
        x = rectPtr->x;
        y = rectPtr->y;
        width = rectPtr->width;
        height = rectPtr->height;
        if ((width <= 0) || (height <= 0)) {
            continue;
        }
        /*
         * borderWidth == 1:
         * reproduce Tk_Draw3DRectangle(..., 1, ...).
         */
        if (penPtr->borderWidth == 1) {
            DrawWin3DRectangle1(dc, &colors, x, y, width, height, penPtr->relief);
            continue;
        }
        /*
         * borderWidth == 2.
         */
        if (penPtr->borderWidth == 2) {
            if ((width > 2) && (height > 2)) {
                /*
                 * Rbc_Draw3DRectangle first draws its special outer
                 * one-pixel border.
                 */
                DrawWinRbcOuter3DBorder(dc, &colors, x, y, width, height, penPtr->relief);
                /*
                 * Then it shrinks the rectangle and reduces
                 * borderWidth from 2 to 1.
                 */
                x++;
                y++;
                width -= 2;
                height -= 2;
                DrawWin3DRectangle1(dc, &colors, x, y, width, height, penPtr->relief);
            } else {
                int effectiveWidth;

                /*
                 * For tiny rectangles Rbc_Draw3DRectangle skips its
                 * special outer layer and calls Tk_Draw3DRectangle
                 * directly with borderWidth == 2.  Reproduce Tk's
                 * border-width clamping here.
                 */
                effectiveWidth = Clamp3DBorderWidth(width, height, 2);
                if (effectiveWidth == 1) {
                    DrawWin3DRectangle1(dc, &colors, x, y, width, height, penPtr->relief);
                }
                /*
                 * effectiveWidth == 0 means Tk draws no bevel.
                 */
            }
        }
    }
    Rbc_WinReleaseDrawableDC(dcStatePtr);
    return TRUE;
}

#endif /* WIN32 */

static void FillBarRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr, XRectangle *rectangles, int nRects) {
#ifdef WIN32
    if (Rbc_WinFillOpaqueStippledRectangles(graphPtr->display, drawable, penPtr->gc, rectangles, nRects)) {
        return;
    }
#endif
    XFillRectangles(graphPtr->display, drawable, penPtr->gc, rectangles, nRects);
}

/*
 * -----------------------------------------------------------------
 *
 * DrawBarSegments --
 *
 *      Draws each of the rectangular segments for the element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *      const Rbc_RenderBarStyle *penPtr
 *      const Rbc_RenderRectangle *rectangles
 *      int nRects
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
void Rbc_NativeBars(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr, const Rbc_RenderRectangle *rectangles,
                            Tcl_Size nRects) {
    const Rbc_RenderRectangle *rectPtr;
    const Rbc_RenderRectangle *endPtr;
    XRectangle *xRects;
    Tcl_Size i;
    int maxRects;
    int nBuffered;

    if ((nRects <= 0) || ((penPtr->border == NULL) && (penPtr->fgColor == NULL))) {
        return;
    }
    if (penPtr->fillOpacity > 0.0) {
        {
            maxRects = Rbc_MaxRequestSize(graphPtr->display, sizeof(XRectangle));
            if (maxRects < 1) {
                return;
            }
            xRects = Tcl_AttemptAlloc((size_t)maxRects * sizeof(*xRects));
            if (xRects == NULL) {
                return;
            }
            nBuffered = 0;
            for (i = 0; i < nRects; i++) {
                const Rbc_RenderRectangle *srcPtr;

                srcPtr = rectangles + i;
                /*
                 * XRectangle itself uses signed 16-bit coordinates and
                 * unsigned 16-bit dimensions.  Preserve batching for the
                 * normal case and fall back to the int-based Xlib call when
                 * a rectangle does not fit that representation.
                 */
                if ((srcPtr->x >= SHRT_MIN) && (srcPtr->x <= SHRT_MAX) && (srcPtr->y >= SHRT_MIN) &&
                    (srcPtr->y <= SHRT_MAX) && (srcPtr->width >= 0) && (srcPtr->width <= USHRT_MAX) &&
                    (srcPtr->height >= 0) && (srcPtr->height <= USHRT_MAX)) {
                    xRects[nBuffered].x = (short)srcPtr->x;
                    xRects[nBuffered].y = (short)srcPtr->y;
                    xRects[nBuffered].width = (unsigned short)srcPtr->width;
                    xRects[nBuffered].height = (unsigned short)srcPtr->height;
                    nBuffered++;
                    if (nBuffered == maxRects) {
                        FillBarRectangles(graphPtr, drawable, penPtr, xRects, nBuffered);
                        nBuffered = 0;
                    }
                } else {
                    /*
                     * Flush the batched XRectangle request before issuing the
                     * wider int-coordinate API call.
                     */
                    if (nBuffered > 0) {
                        FillBarRectangles(graphPtr, drawable, penPtr, xRects, nBuffered);
                        nBuffered = 0;
                    }
                    if ((srcPtr->width > 0) && (srcPtr->height > 0)) {
                        XFillRectangle(graphPtr->display, drawable, penPtr->gc, srcPtr->x, srcPtr->y,
                                       (unsigned int)srcPtr->width, (unsigned int)srcPtr->height);
                    }
                }
            }
            if (nBuffered > 0) {
                FillBarRectangles(graphPtr, drawable, penPtr, xRects, nBuffered);
            }
            ckfree(xRects);
        }
    }
    if ((penPtr->border != NULL) && (penPtr->borderWidth > 0) && (penPtr->relief != TK_RELIEF_FLAT)) {
#ifdef WIN32
        if (!DrawWin3DBarBorders(graphPtr, drawable, penPtr, rectangles, nRects)) {
#endif
            for (rectPtr = rectangles, endPtr = rectangles + nRects; rectPtr < endPtr; rectPtr++) {
                Rbc_Draw3DRectangle(graphPtr->tkwin, drawable, penPtr->border, rectPtr->x, rectPtr->y, rectPtr->width,
                                    rectPtr->height, penPtr->borderWidth, penPtr->relief);
            }
#ifdef WIN32
        }
#endif
    }
}


void Rbc_NativeArcStroke(Graph *graphPtr, Drawable drawable, GC gc,
    const Segment2D *segments, Tcl_Size count) {
    Point2D *points;
    Tcl_Size i, n;
    if (count == 0) return;
    points = Tcl_AttemptAlloc((size_t)(count + 1) * sizeof(*points));
    if (points == NULL) return;
    for (i = 0; i < count;) {
        n = 0;
        points[n++] = segments[i].p;
        do {
            points[n++] = segments[i++].q;
        } while (i < count && points[n-1].x == segments[i].p.x &&
                 points[n-1].y == segments[i].p.y);
        {
            XPoint *xp = Tcl_AttemptAlloc((size_t)n * sizeof(*xp));
            Tcl_Size j;
            if (xp == NULL) continue;
            for (j = 0; j < n; j++) {
                xp[j].x = (short)points[j].x;
                xp[j].y = (short)points[j].y;
            }
            XDrawLines(graphPtr->display, drawable, gc, xp, (int)n, CoordModeOrigin);
            ckfree(xp);
        }
    }
    ckfree(points);
}

/* A fill-only marker run holds one native DC and brush. The component retains
 * style grouping and painter order; the backend only consumes mapped polygons. */
struct Rbc_NativePolygonBatch {
#ifdef WIN32
    Rbc_WinDrawableDC *dcState;
    HDC dc;
    HBRUSH oldBrush;
    HPEN oldPen;
    POINT *points;
    int oldFillMode;
    Tcl_Size capacity;
#endif
    Display *display;
};

Rbc_NativePolygonBatch *Rbc_NativeBeginPolygonBatch(Graph *graphPtr, Drawable drawable, GC gc, Tcl_Size maxPoints) {
#ifdef WIN32
    Rbc_NativePolygonBatch *batch;
    HBRUSH brush;
    if (maxPoints <= 0 || (size_t)maxPoints > SIZE_MAX / sizeof(POINT)) return NULL;
    batch = (Rbc_NativePolygonBatch *)ckalloc(sizeof(*batch));
    memset(batch, 0, sizeof(*batch));
    batch->points = Tcl_AttemptAlloc((size_t)maxPoints * sizeof(POINT));
    if (batch->points == NULL) {ckfree(batch); return NULL;}
    brush = CreateSolidBrush(gc->foreground);
    if (brush == NULL) {ckfree(batch->points); ckfree(batch); return NULL;}
    batch->display = graphPtr->display;
    batch->capacity = maxPoints;
    batch->dc = Rbc_WinAcquireDrawableDC(graphPtr->display, drawable, &batch->dcState);
    Rbc_WinSetROP2(batch->dc, gc->function);
    batch->oldBrush = SelectBrush(batch->dc, brush);
    batch->oldPen = SelectPen(batch->dc, GetStockPen(NULL_PEN));
    batch->oldFillMode = SetPolyFillMode(batch->dc, gc->fill_rule == EvenOddRule ? ALTERNATE : WINDING);
    return batch;
#else
    (void)graphPtr; (void)drawable; (void)gc; (void)maxPoints;
    return NULL;
#endif
}

void Rbc_NativeBatchPolygon(Rbc_NativePolygonBatch *batch, const Point2D *points, Tcl_Size count) {
#ifdef WIN32
    Tcl_Size i;
    int n = Rbc_NativePolygonPointCount(batch->display, count);
    if (n <= 0 || count > batch->capacity) return;
    for (i = 0; i < count; i++) {
        batch->points[i].x = (LONG)(short int)points[i].x;
        batch->points[i].y = (LONG)(short int)points[i].y;
    }
    Polygon(batch->dc, batch->points, n);
#else
    (void)batch; (void)points; (void)count;
#endif
}

void Rbc_NativeEndPolygonBatch(Rbc_NativePolygonBatch *batch) {
#ifdef WIN32
    if (batch->oldFillMode != 0) SetPolyFillMode(batch->dc, batch->oldFillMode);
    SelectPen(batch->dc, batch->oldPen);
    DeleteBrush(SelectBrush(batch->dc, batch->oldBrush));
    Rbc_WinReleaseDrawableDC(batch->dcState);
    ckfree(batch->points);
#else
    (void)batch;
#endif
    ckfree(batch);
}
