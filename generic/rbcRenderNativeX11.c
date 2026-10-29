/* Native X11 geometry and batching. See license.terms.
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
        XDrawLines(ctx->graphPtr->display, ctx->nativeDrawable, ctx->nativeGC, points, (int)remaining + 1,
                   CoordModeOrigin);
    }
}

void Rbc_NativeStrips(Graph *graphPtr, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size nSegments) {
    Rbc_Draw2DSegments(graphPtr->display, drawable, gc, segments, nSegments);
}

Rbc_NativeTrace *Rbc_NativeBeginTrace(Graph *graphPtr, Drawable drawable, GC gc) {
    Rbc_NativeTrace *ctx;
    int maxPoints;
    size_t pointSize;

    pointSize = sizeof(XPoint);
    maxPoints = Rbc_MaxRequestSize(graphPtr->display, pointSize) - 1;
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
    return ctx;
}

void Rbc_NativeEndTrace(Rbc_NativeTrace *ctx) {
    if (ctx == NULL) {
        return;
    }        
    ckfree(ctx);
}

INLINE static int Round(double x) { return (int)(x + ((x < 0.0) ? -0.5 : 0.5)); }

#define DRAW_SYMBOL(style) (((style)->counter % (style)->interval) == 0)

#define MAX_DRAWLINES(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWPOLYGON(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWSEGMENTS(d) Rbc_MaxRequestSize(d, sizeof(XSegment))
#define MAX_DRAWRECTANGLES(d) Rbc_MaxRequestSize(d, sizeof(XRectangle))
#define MAX_DRAWARCS(d) Rbc_MaxRequestSize(d, sizeof(XArc))

/*
 *----------------------------------------------------------------------
 *
 * Rbc_NativeCircles --
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
void Rbc_NativeCircles(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, Tcl_Size nSymbolPts,
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

/*
 *----------------------------------------------------------------------
 *
 * Rbc_NativeSquares --
 *
 *      Draw square symbols using native X11 rectangle operations.
 *
 *----------------------------------------------------------------------
 */
void Rbc_NativeSquares(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, Tcl_Size nSymbolPts,
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

int Rbc_NativePolygonPointCount(Display *display, Tcl_Size nPoints) {
    if ((nPoints < 3) || (nPoints > (Tcl_Size)INT_MAX)) {
        return 0;
    }
    {
        int maxPoints;
        maxPoints = Rbc_MaxRequestSize(display, sizeof(XPoint));
        if ((maxPoints < 3) || (nPoints > (Tcl_Size)maxPoints)) {
            return 0;
        }
    }
    return (int)nPoints;
}

void Rbc_NativeBarRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr,
                             XRectangle *rectangles, int nRects) {
    XFillRectangles(graphPtr->display, drawable, penPtr->gc, rectangles, nRects);
}
/* A fill-only marker run holds one native DC and brush. The component retains
 * style grouping and painter order; the backend only consumes mapped polygons. */
struct Rbc_NativePolygonBatch {
    Display *display;
};

Rbc_NativePolygonBatch *Rbc_NativeBeginPolygonBatch(Graph *graphPtr, Drawable drawable, GC gc, Tcl_Size maxPoints) {
    (void)graphPtr;
    (void)drawable;
    (void)gc;
    (void)maxPoints;
    return NULL;
}

void Rbc_NativeBatchPolygon(Rbc_NativePolygonBatch *batch, const Point2D *points, Tcl_Size count) {
    (void)batch;
    (void)points;
    (void)count;
}

void Rbc_NativeEndPolygonBatch(Rbc_NativePolygonBatch *batch) {
    (void)batch;
    ckfree(batch);
}

void Rbc_NativePolygonSymbols(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style,
                              const XPoint *polygons, Tcl_Size count, int stride, int vertices) {
    Tcl_Size i;
    const XPoint *p;
    int shape = vertices == 12 ? Complex : Convex;
    if (style->fillGC != NULL) {
        for (p = polygons, i = 0; i < count; i++, p += stride) {
            XFillPolygon(display, drawable, style->fillGC, (XPoint *)p, stride, shape, CoordModeOrigin);
        }
    }
    if (style->outlineWidth > 0) {
        for (p = polygons, i = 0; i < count; i++, p += stride) {
            XDrawLines(display, drawable, style->outlineGC, (XPoint *)p, stride, CoordModeOrigin);
        }
    }
}

int Rbc_NativeBarBorders(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *style,
                         const Rbc_RenderRectangle *rectangles, Tcl_Size count) {
    (void)graphPtr;
    (void)drawable;
    (void)style;
    (void)rectangles;
    (void)count;
    return FALSE;
}

size_t Rbc_NativeClipStateSize(void) { return 0; }

int Rbc_NativePlatformClip(Graph *graphPtr, Drawable parent, int left, int top, int right, int bottom, void *state,
                           Drawable *drawablePtr) {
    (void)graphPtr;
    (void)parent;
    (void)left;
    (void)top;
    (void)right;
    (void)bottom;
    (void)state;
    (void)drawablePtr;
    return FALSE;
}

void Rbc_NativePlatformUnclip(void *state, Drawable drawable) {
    (void)state;
    (void)drawable;
}

void Rbc_NativeRun(Graph *graphPtr, Drawable drawable, void *data, Tcl_Size count, Rbc_RenderRunProc *proc) {
    (void)graphPtr;
    if (data != NULL && count > 0) {
        proc(data, drawable, count);
    }        
}

int Rbc_NativeOpaqueBitmapRun(Graph *graphPtr, Drawable drawable, Pixmap bitmap, GC gc, int width, int height,
                              const Rbc_RenderPosition *positions, Tcl_Size count) {
    (void)graphPtr;
    (void)drawable;
    (void)bitmap;
    (void)gc;
    (void)width;
    (void)height;
    (void)positions;
    (void)count;
    return FALSE;
}
