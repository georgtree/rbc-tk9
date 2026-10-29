/* Native Win geometry and batching. See license.terms.
 * Platform contexts remain private; shared Tk drawing is in rbcRenderNative.c. */
#include "rbcRenderNative.h"
#include <stdint.h>
#include <X11/Xutil.h>

struct Rbc_NativeTrace {
    Graph *graphPtr;
    Drawable nativeDrawable;
    GC nativeGC;
    void *nativePoints;
    int nativeMaxPoints;
    Rbc_WinDrawableDC *nativeDCState;
    HDC nativeDC;
    HPEN nativeOldPen;
    HBRUSH nativeOldBrush;
};

/* Native stroke contexts borrow the configured GC, including its clip and
 * raster operation. Keep the original request splitting and integer conversion;
 * dispatch occurs once per trace/segment batch, never once per point. */
void Rbc_NativePolyline(Rbc_NativeTrace *ctx, const Point2D *screenPts, Tcl_Size nPoints) {
    Tcl_Size count, j, remaining;
    int maxPoints;
    if (ctx == NULL) {
        return;
    }        
    maxPoints = ctx->nativeMaxPoints;
    if (screenPts == NULL || nPoints < 2 || maxPoints < 2) {
        return;
    }
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
}

void Rbc_NativeStrips(Graph *graphPtr, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size nSegments) {
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
}

Rbc_NativeTrace *Rbc_NativeBeginTrace(Graph *graphPtr, Drawable drawable, GC gc) {
    Rbc_NativeTrace *ctx;
    int maxPoints;
    size_t pointSize;

    pointSize = sizeof(POINT);
    maxPoints = gc->line_width > 1 ? 100 : Rbc_MaxRequestSize(graphPtr->display, pointSize) - 1;
    if (maxPoints < 2 || ((size_t)maxPoints + 1u) > (SIZE_MAX - sizeof(*ctx)) / pointSize) {
        return NULL;
    }
    /* Keep state and point storage in one allocation, preserving the number
     * of allocations per trace batch before the backend was separated. */
    ctx = (Rbc_NativeTrace *)Tcl_AttemptAlloc(sizeof(*ctx) + ((size_t)maxPoints + 1u) * pointSize);
    if (ctx == NULL) {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->graphPtr = graphPtr;
    ctx->nativeDrawable = drawable;
    ctx->nativeGC = gc;
    ctx->nativePoints = ctx + 1;
    ctx->nativeMaxPoints = maxPoints;
    ctx->nativeDC = Rbc_WinAcquireDrawableDC(graphPtr->display, drawable, &ctx->nativeDCState);
    ctx->nativeOldPen = SelectPen(ctx->nativeDC, Rbc_GCToPen(ctx->nativeDC, gc));
    ctx->nativeOldBrush = SelectBrush(ctx->nativeDC, CreateSolidBrush(gc->foreground));
    Rbc_WinSetROP2(ctx->nativeDC, gc->function);
    return ctx;
}

void Rbc_NativeEndTrace(Rbc_NativeTrace *ctx) {
    if (ctx == NULL) {
        return;
    }        
    DeletePen(SelectPen(ctx->nativeDC, ctx->nativeOldPen));
    DeleteBrush(SelectBrush(ctx->nativeDC, ctx->nativeOldBrush));
    Rbc_WinReleaseDrawableDC(ctx->nativeDCState);
    ckfree(ctx);
}

INLINE static int Round(double x) { return (int)(x + ((x < 0.0) ? -0.5 : 0.5)); }

#define DRAW_SYMBOL(style) (((style)->counter % (style)->interval) == 0)

#define MAX_DRAWLINES(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWPOLYGON(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWSEGMENTS(d) Rbc_MaxRequestSize(d, sizeof(XSegment))
#define MAX_DRAWRECTANGLES(d) Rbc_MaxRequestSize(d, sizeof(XRectangle))
#define MAX_DRAWARCS(d) Rbc_MaxRequestSize(d, sizeof(XArc))

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
 * Rbc_NativeCircles --
 *
 *      Batched Windows circle-symbol renderer.
 *
 *----------------------------------------------------------------------
 */
void Rbc_NativeCircles(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, Tcl_Size nSymbolPts,
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

void Rbc_NativePolygonSymbols(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style,
                              const XPoint *polygons, Tcl_Size nPolygons, int stride, int nVertices);

/*
 *----------------------------------------------------------------------
 *
 * Rbc_NativeSquares --
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
void Rbc_NativeSquares(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, Tcl_Size nSymbolPts,
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
    Rbc_NativePolygonSymbols(display, drawable, style, polygons, count, 5, 4);
    ckfree(polygons);
}

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
 * Rbc_NativePolygonSymbols --
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
void Rbc_NativePolygonSymbols(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style,
                              const XPoint *polygons, Tcl_Size nPolygons, int stride, int nVertices) {
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

int Rbc_NativePolygonPointCount(Display *display, Tcl_Size nPoints) {
    if ((nPoints < 3) || (nPoints > (Tcl_Size)INT_MAX)) {
        return 0;
    }
    (void)display;
    return (int)nPoints;
}

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
 * Rbc_NativeBarBorders1 --
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
int Rbc_NativeBarBorders(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr,
                         const Rbc_RenderRectangle *rectangles, Tcl_Size nRects) {
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

void Rbc_NativeBarRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr,
                             XRectangle *rectangles, int nRects) {
    if (Rbc_WinFillOpaqueStippledRectangles(graphPtr->display, drawable, penPtr->gc, rectangles, nRects)) {
        return;
    }
    XFillRectangles(graphPtr->display, drawable, penPtr->gc, rectangles, nRects);
}

/* A fill-only marker run holds one native DC and brush. The component retains
 * style grouping and painter order; the backend only consumes mapped polygons. */
struct Rbc_NativePolygonBatch {
    Rbc_WinDrawableDC *dcState;
    HDC dc;
    HBRUSH oldBrush;
    HPEN oldPen;
    POINT *points;
    int oldFillMode;
    Tcl_Size capacity;
    Display *display;
};

Rbc_NativePolygonBatch *Rbc_NativeBeginPolygonBatch(Graph *graphPtr, Drawable drawable, GC gc, Tcl_Size maxPoints) {
    Rbc_NativePolygonBatch *batch;
    HBRUSH brush;
    if (maxPoints <= 0 || (size_t)maxPoints > SIZE_MAX / sizeof(POINT)) {
        return NULL;
    }        
    batch = (Rbc_NativePolygonBatch *)ckalloc(sizeof(*batch));
    memset(batch, 0, sizeof(*batch));
    batch->points = Tcl_AttemptAlloc((size_t)maxPoints * sizeof(POINT));
    if (batch->points == NULL) {
        ckfree(batch);
        return NULL;
    }
    brush = CreateSolidBrush(gc->foreground);
    if (brush == NULL) {
        ckfree(batch->points);
        ckfree(batch);
        return NULL;
    }
    batch->display = graphPtr->display;
    batch->capacity = maxPoints;
    batch->dc = Rbc_WinAcquireDrawableDC(graphPtr->display, drawable, &batch->dcState);
    Rbc_WinSetROP2(batch->dc, gc->function);
    batch->oldBrush = SelectBrush(batch->dc, brush);
    batch->oldPen = SelectPen(batch->dc, GetStockPen(NULL_PEN));
    batch->oldFillMode = SetPolyFillMode(batch->dc, gc->fill_rule == EvenOddRule ? ALTERNATE : WINDING);
    return batch;
}

void Rbc_NativeBatchPolygon(Rbc_NativePolygonBatch *batch, const Point2D *points, Tcl_Size count) {
    Tcl_Size i;
    int n = Rbc_NativePolygonPointCount(batch->display, count);
    if (n <= 0 || count > batch->capacity) {
        return;
    }        
    for (i = 0; i < count; i++) {
        batch->points[i].x = (LONG)(short int)points[i].x;
        batch->points[i].y = (LONG)(short int)points[i].y;
    }
    Polygon(batch->dc, batch->points, n);
}

void Rbc_NativeEndPolygonBatch(Rbc_NativePolygonBatch *batch) {
    if (batch->oldFillMode != 0) {
        SetPolyFillMode(batch->dc, batch->oldFillMode);
    }        
    SelectPen(batch->dc, batch->oldPen);
    DeleteBrush(SelectBrush(batch->dc, batch->oldBrush));
    Rbc_WinReleaseDrawableDC(batch->dcState);
    ckfree(batch->points);
    ckfree(batch);
}

/* State follows the shared clip frame in its single allocation. */
typedef struct NativeDCClip {
    Rbc_WinDrawableDC *dcState;
    HDC dc;
    int saved;
} NativeDCClip;

size_t Rbc_NativeClipStateSize(void) { return sizeof(NativeDCClip); }

int Rbc_NativePlatformClip(Graph *graphPtr, Drawable parent, int left, int top, int right, int bottom, void *clientData,
                           Drawable *drawablePtr) {
    NativeDCClip *state = clientData;
    memset(state, 0, sizeof(*state));
    state->dc = Rbc_WinAcquireDrawableDC(graphPtr->display, parent, &state->dcState);
    if (state->dc == NULL) {
        return TRUE;
    }        
    state->saved = SaveDC(state->dc);
    if (state->saved == 0) {
        return TRUE;
    }        
    /* A meta region survives the clip masks installed by bitmap drawing. */
    if (IntersectClipRect(state->dc, left, top, right, bottom) != ERROR && SetMetaRgn(state->dc) != ERROR) {
        *drawablePtr = Rbc_WinCreateDrawableFromDC(state->dc);
    }
    return TRUE;
}

void Rbc_NativePlatformUnclip(void *clientData, Drawable drawable) {
    NativeDCClip *state = clientData;
    if (drawable != None) {
        Rbc_WinFreeDrawableFromDC(drawable);
    }        
    if (state->saved != 0) {
        RestoreDC(state->dc, state->saved);
    }        
    if (state->dcState != NULL) {
        Rbc_WinReleaseDrawableDC(state->dcState);
    }        
}

/* Acquire one destination DC for the entire run. If either acquisition or
 * wrapping fails, replay on the original drawable in the same painter order. */
void Rbc_NativeRun(Graph *graphPtr, Drawable drawable, void *data, Tcl_Size count, Rbc_RenderRunProc *proc) {
    Rbc_WinDrawableDC *state;
    HDC dc;
    Drawable target;
    if (data == NULL || count <= 0) {
        return;
    }        
    dc = Rbc_WinAcquireDrawableDC(graphPtr->display, drawable, &state);
    target = dc != NULL ? Rbc_WinCreateDrawableFromDC(dc) : None;
    if (target == None) {
        Rbc_WinReleaseDrawableDC(state);
        proc(data, drawable, count);
        return;
    }
    proc(data, target, count);
    Rbc_WinFreeDrawableFromDC(target);
    Rbc_WinReleaseDrawableDC(state);
}

int Rbc_NativeOpaqueBitmapRun(Graph *graphPtr, Drawable drawable, Pixmap bitmap, GC gc, int width, int height,
                              const Rbc_RenderPosition *positions, Tcl_Size count) {
    return Rbc_WinCopyOpaqueBitmapBatch(graphPtr->display, drawable, bitmap, gc, width, height, positions, count);
}
