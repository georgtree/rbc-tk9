/**
 * @file rbcRenderNativeX11.c
 * @brief X11 request batching and platform fallback decisions.
 *
 * See license.terms.
 * Platform contexts remain private; shared Tk drawing is in rbcRenderNative.c.
 */

#include "rbcRenderNative.h"

/** @defgroup native_x11_drawing X11 native backend
 * Private X11 implementation of the shared native backend interface.
 */
#include <stdint.h>
#include <X11/Xutil.h>

/**
 * @brief Return the raw X11 polygon request capacity.
 * @ingroup native_x11_drawing
 *
 * Uses XPoint request sizing for the borrowed graph display. Component clipping
 * allowances are not subtracted here; this is not a promise that any polygon
 * can be split safely.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 */
Tcl_Size Rbc_NativePolygonPointLimit(Graph *graphPtr) {
    return Rbc_MaxRequestSize(graphPtr->display, sizeof(XPoint));
}

/** @brief Owned X11 trace context with trailing point workspace.
 * Borrows graph, target and GC; nativeMaxPoints excludes the extra slot used
 * for a repeated continuation endpoint. One allocation owns state and points.
 */
struct Rbc_NativeTrace {
    Graph *graphPtr; /**< Borrowed owning graph; outlives the batch. */
    Drawable nativeDrawable; /**< Borrowed destination drawable. */
    GC nativeGC; /**< Borrowed configured graphics context. */
    void *nativePoints; /**< Point workspace stored after this record in the same allocation. */
    int nativeMaxPoints; /**< Maximum vertices per chunk, excluding the continuation slot. */
};

/**
 * @brief Draw a connected trace in request-sized X11 chunks.
 * @ingroup native_x11_drawing
 *
 * Borrows context and mapped points; NULL context/points, fewer than two points
 * or insufficient capacity are no-ops. Converts directly to short coordinates.
 * Each continuation repeats the previous endpoint, using the extra workspace
 * slot reserved by BeginTrace. Separate requests preserve connectivity but do
 * not carry an explicit accumulated dash offset. GC state remains borrowed.
 *
 * @param[in,out] ctx Live native trace context returned by Rbc_NativeBeginTrace; NULL is accepted.
 * @param[in] screenPts Borrowed mapped polyline vertices with nPoints entries; NULL suppresses drawing.
 * @param[in] nPoints Number of polyline vertices; fewer than two draws nothing.
 */
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

/**
 * @brief Delegate independent X11 strips to shared segment drawing.
 * @ingroup native_x11_drawing
 *
 * Rbc_NativeSegments handles protocol clipping, conversion and request
 * batching. No additional context or resource is acquired here.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] gc Borrowed configured drawing GC, including raster operation, clip and style.
 * @param[in] segments Borrowed mapped endpoint pairs; readable for the supplied segment count.
 * @param[in] nSegments Number of Segment2D records, not endpoints.
 */
void Rbc_NativeStrips(Graph *graphPtr, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size nSegments) {
    Rbc_NativeSegments(graphPtr, drawable, gc, segments, nSegments);
}

/**
 * @brief Allocate X11 trace state and point workspace together.
 * @ingroup native_x11_drawing
 *
 * Borrows graph, drawable and configured GC. Reserves request capacity minus
 * one as the base chunk size, plus one continuation endpoint slot. Returns
 * NULL for insufficient capacity, size overflow or allocation failure.
 * Release the owned allocation with Rbc_NativeEndTrace.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive until the returned context is released.
 * @param[in] drawable Borrowed target, kept valid until the returned context is released.
 * @param[in] gc Borrowed configured GC, kept valid until the returned batch is released.
 */
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

/**
 * @brief Free the X11 trace allocation.
 * @ingroup native_x11_drawing
 *
 * Accepts NULL. Does not free the borrowed graph, drawable or GC and does
 * not flush or synchronize the X server.
 *
 * @param[in] ctx Owned trace context consumed by this call; NULL is accepted.
 */
void Rbc_NativeEndTrace(Rbc_NativeTrace *ctx) {
    if (ctx == NULL) {
        return;
    }        
    ckfree(ctx);
}

/**
 * @brief Round a representable coordinate away from zero at ties.
 * @ingroup native_x11_drawing
 *
 * Adds or subtracts one half before int conversion. Requires finite input
 * within int range. This local helper is currently unused by X11 drawing.
 *
 * @param[in] x Finite coordinate whose adjustment by plus or minus one half fits int; no range check occurs.
 */
INLINE static int Round(double x) { return (int)(x + ((x < 0.0) ? -0.5 : 0.5)); }

#define DRAW_SYMBOL(style) (((style)->counter % (style)->interval) == 0)

#define MAX_DRAWLINES(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWPOLYGON(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWSEGMENTS(d) Rbc_MaxRequestSize(d, sizeof(XSegment))
#define MAX_DRAWRECTANGLES(d) Rbc_MaxRequestSize(d, sizeof(XRectangle))
#define MAX_DRAWARCS(d) Rbc_MaxRequestSize(d, sizeof(XArc))

/**
 * @brief Draw selected circle symbols in X11 arc batches.
 * @ingroup native_x11_drawing
 *
 * Owns one XArc array sized for all input centers, then submits request-sized
 * chunks with fill before outline per chunk. Positive interval advances the
 * style counter for every examined center. Converts centers to short before
 * subtracting radius; diameter becomes unsigned short. Nonpositive count,
 * size overflow, allocation failure or unusable request capacity skips output.
 * Borrows style GCs and requires mapped coordinates and valid radius.
 *
 * @param[in] display Borrowed display owning the drawable and GC resources.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in,out] style Borrowed symbol style; interval-aware branches advance its counter for examined centers.
 * @param[in] nSymbolPts Nonnegative number of mapped symbol centers.
 * @param[in] symbolPts Borrowed mapped center array with nSymbolPts entries.
 * @param[in] radius Circle radius in pixels; caller supplies representable center/radius arithmetic.
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

/**
 * @brief Draw selected square symbols in X11 rectangle batches.
 * @ingroup native_x11_drawing
 *
 * Owns a temporary array sized for all centers, then emits bounded requests
 * with fill before outline per chunk. Positive interval advances the mutable
 * style counter for every center examined. Subtracts radius before short
 * conversion, unlike circles; diameter becomes unsigned short. Nonpositive
 * count, size overflow, allocation failure or unusable capacity skips output.
 *
 * @param[in] display Borrowed display owning the drawable and GC resources.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in,out] style Borrowed symbol style; interval-aware branches advance its counter for examined centers.
 * @param[in] nSymbolPts Nonnegative number of mapped symbol centers.
 * @param[in] symbolPts Borrowed mapped center array with nSymbolPts entries.
 * @param[in] r Square half-size in pixels; caller supplies representable center/radius arithmetic.
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

/**
 * @brief Validate a single X11 polygon request length.
 * @ingroup native_x11_drawing
 *
 * Returns the int count only when at least three vertices fit both INT_MAX
 * and the display request limit. Otherwise returns zero. Does not partition
 * a polygon or inspect its geometry.
 *
 * @param[in] display Borrowed display owning the drawable and GC resources.
 * @param[in] nPoints Number of polygon vertices, including any repeated closing point.
 */
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

/**
 * @brief Submit one prepared bar-fill rectangle batch.
 * @ingroup native_x11_drawing
 *
 * Borrows graph, drawable, style GC and XRectangle array. Caller has already
 * validated count and packed coordinates to fit the request; no allocation
 * or additional clipping is performed here.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] penPtr Borrowed bar style and its live color, border and GC resources.
 * @param[in] rectangles Borrowed prepacked XRectangle array; caller has bounded count for the platform request.
 * @param[in] nRects Number of rectangle records, not bytes.
 */
void Rbc_NativeBarRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr,
                             XRectangle *rectangles, int nRects) {
    XFillRectangles(graphPtr->display, drawable, penPtr->gc, rectangles, nRects);
}
/** @brief Placeholder for the unsupported X11 polygon-batch optimization.
 * No instance is created by this backend. This is not a Windows DC owner.
 */
struct Rbc_NativePolygonBatch {
    Display *display; /**< Unused placeholder; this backend never allocates a polygon batch. */
};

/**
 * @brief Decline the optional fill-only polygon optimization on X11.
 * @ingroup native_x11_drawing
 *
 * Always returns NULL without acquiring resources. The shared caller must
 * use its ordinary polygon path; NULL does not mean an empty clipping region.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive until the returned context is released. Unused by this X11 fallback stub.
 * @param[in] drawable Borrowed target, kept valid until the returned context is released. Unused by this X11 fallback stub.
 * @param[in] gc Borrowed configured GC, kept valid until the returned batch is released. Unused by this X11 fallback stub.
 * @param[in] maxPoints Maximum number of vertices in any polygon submitted to the returned batch. Unused by this X11 fallback stub.
 */
Rbc_NativePolygonBatch *Rbc_NativeBeginPolygonBatch(Graph *graphPtr, Drawable drawable, GC gc, Tcl_Size maxPoints) {
    (void)graphPtr;
    (void)drawable;
    (void)gc;
    (void)maxPoints;
    return NULL;
}

/**
 * @brief Provide the unused X11 polygon-batch interface entry.
 * @ingroup native_x11_drawing
 *
 * No operation. X11 BeginPolygonBatch never returns a context, so callers
 * should select the ordinary drawing path instead.
 *
 * @param[in,out] batch Live owned polygon batch returned by Rbc_NativeBeginPolygonBatch. Unused by this X11 fallback stub.
 * @param[in] points Borrowed mapped polygon vertices with count entries. Unused by this X11 fallback stub.
 * @param[in] count Number of input records, not bytes. Unused by this X11 fallback stub.
 */
void Rbc_NativeBatchPolygon(Rbc_NativePolygonBatch *batch, const Point2D *points, Tcl_Size count) {
    (void)batch;
    (void)points;
    (void)count;
}

/**
 * @brief Release storage passed to the X11 polygon-batch endpoint.
 * @ingroup native_x11_drawing
 *
 * Calls ckfree on the argument. No context is normally created by the X11
 * constructor; this interface exists to match the platform backend contract.
 *
 * @param[in] batch Owned polygon batch consumed by this call; Windows requires non-NULL.
 */
void Rbc_NativeEndPolygonBatch(Rbc_NativePolygonBatch *batch) {
    (void)batch;
    ckfree(batch);
}

/**
 * @brief Fill all polygon symbols, then draw all outlines.
 * @ingroup native_x11_drawing
 *
 * Borrows preconverted polygons and style GCs. Stride includes the closing
 * point and is used for both fill and line requests. Twelve-vertex templates
 * use Complex fill; others use Convex. Interval selection was performed by
 * the shared caller. Does not allocate, clip or split individual polygons.
 *
 * @param[in] display Borrowed display owning the drawable and GC resources.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] style Borrowed symbol style and GCs; caller has already selected the symbol intervals.
 * @param[in] polygons Borrowed packed XPoint polygons, with stride points per polygon.
 * @param[in] count Nonnegative number of packed polygon records.
 * @param[in] stride Points per polygon record, including its repeated closing vertex.
 * @param[in] vertices Distinct vertices per polygon; twelve selects the concave plus/cross fill policy.
 */
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

/**
 * @brief Decline optimized bar borders on X11.
 * @ingroup native_x11_drawing
 *
 * Always returns FALSE without drawing. The shared bar helper then draws
 * relief borders individually.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call. Unused by this X11 fallback stub.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates. Unused by this X11 fallback stub.
 * @param[in] style Borrowed bar style and its live color, border and GC resources. Unused by this X11 fallback stub.
 * @param[in] rectangles Borrowed mapped rectangle array with the supplied rectangle count. Unused by this X11 fallback stub.
 * @param[in] count Number of mapped bar rectangles. Unused by this X11 fallback stub.
 */
int Rbc_NativeBarBorders(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *style,
                         const Rbc_RenderRectangle *rectangles, Tcl_Size count) {
    (void)graphPtr;
    (void)drawable;
    (void)style;
    (void)rectangles;
    (void)count;
    return FALSE;
}

/**
 * @brief Report that X11 needs no platform clip payload.
 * @ingroup native_x11_drawing
 *
 * Returns zero. Shared clip frames still own bounds, links and scratch
 * pixmaps; no extra platform state follows the frame.
 */
size_t Rbc_NativeClipStateSize(void) { return 0; }

/**
 * @brief Decline direct platform clipping on X11.
 * @ingroup native_x11_drawing
 *
 * Returns FALSE and leaves the output drawable untouched. The shared clip
 * stack then creates or reuses its scratch target. This is a fallback signal,
 * not a successfully installed empty clip.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call. Unused by this X11 fallback stub.
 * @param[in] parent Borrowed parent drawable whose clipping and pixels must remain live through unclip. Unused by this X11 fallback stub.
 * @param[in] left Inclusive left clip edge in absolute pixels. Unused by this X11 fallback stub.
 * @param[in] top Inclusive top clip edge in absolute pixels. Unused by this X11 fallback stub.
 * @param[in] right Exclusive right clip edge in absolute pixels. Unused by this X11 fallback stub.
 * @param[in] bottom Exclusive bottom clip edge in absolute pixels. Unused by this X11 fallback stub.
 * @param[in,out] state Unused X11 platform payload; no storage is accessed. Unused by this X11 fallback stub.
 * @param[in,out] drawablePtr Unused output pointer; left unchanged on X11. Unused by this X11 fallback stub.
 */
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

/**
 * @brief Provide the unused X11 direct-clip cleanup entry.
 * @ingroup native_x11_drawing
 *
 * No operation; shared scratch clipping handles commit and resource lifetime.
 *
 * @param[in,out] state Unused X11 platform payload; no storage is accessed. Unused by this X11 fallback stub.
 * @param[in] drawable Temporary platform wrapper to release, or None after partial setup; ignored on X11. Unused by this X11 fallback stub.
 */
void Rbc_NativePlatformUnclip(void *state, Drawable drawable) {
    (void)state;
    (void)drawable;
}

/**
 * @brief Invoke a native run callback synchronously on its original target.
 * @ingroup native_x11_drawing
 *
 * Calls proc only when data is non-NULL and count is positive. Borrows data
 * and drawable, with no wrapper or DC acquisition. A valid callback is
 * required for nonempty runs; it must not retain temporary drawing resources.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] data Borrowed callback-specific run data; NULL suppresses the run.
 * @param[in] count Number of run items; nonpositive counts suppress drawing.
 * @param[in] proc Required synchronous callback for a nonempty run; must not retain the target or destroy drawing resources.
 */
void Rbc_NativeRun(Graph *graphPtr, Drawable drawable, void *data, Tcl_Size count, Rbc_RenderRunProc *proc) {
    (void)graphPtr;
    if (data != NULL && count > 0) {
        proc(data, drawable, count);
    }        
}

/**
 * @brief Decline optimized opaque bitmap runs on X11.
 * @ingroup native_x11_drawing
 *
 * Always returns FALSE without drawing or modifying GC state. The shared
 * bitmap-run helper performs the complete XCopyPlane fallback.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call. Unused by this X11 fallback stub.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates. Unused by this X11 fallback stub.
 * @param[in] bitmap Borrowed one-bit source bitmap, readable over width by height pixels. Unused by this X11 fallback stub.
 * @param[in] gc Borrowed opaque GXcopy-compatible GC; masked or XOR copies are outside this interface contract. Unused by this X11 fallback stub.
 * @param[in] width Positive source bitmap width in pixels. Unused by this X11 fallback stub.
 * @param[in] height Positive source bitmap height in pixels. Unused by this X11 fallback stub.
 * @param[in] positions Borrowed array of count integer destination origins. Unused by this X11 fallback stub.
 * @param[in] count Number of run items; nonpositive counts suppress drawing. Unused by this X11 fallback stub.
 */
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
