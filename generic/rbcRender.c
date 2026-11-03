/* Common screen adapters and rendering dispatch. See license.terms. */
/** @file rbcRender.c
 * Common renderer dispatch and narrowly scoped native screen adapters.
 */
/** @defgroup render_dispatch_internals Screen dispatch internals
 * Private adapters selected by capability-specific constructors. Graphs,
 * styles, drawables and component geometry remain borrowed.
 */
#include "rbcRenderInt.h"
#include "rbcRenderNative.h"

Tcl_Size Rbc_RenderPolygonPointLimit(Graph *graphPtr) {
    if (graphPtr->renderer == RBC_RENDERER_CAIRO || (graphPtr->flags & GRAPH_EXPORT)) {
        return TCL_SIZE_MAX;
    }
    return Rbc_NativePolygonPointLimit(graphPtr);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Shared storage for native screen adapter families.
 * core is the dispatch prefix. Trace, clip and polygon-batch records are owned;
 * window, drawable, GC, symbol style, tile, stipple and mapped arrays are borrowed.
 * nativeBarStyle is a shallow value copy, not ownership of its referenced resources.
 * Only the selected constructor's fields are meaningful. Some one-shot callers
 * place this record on the stack and must never pass it to Rbc_RenderEnd.
 */
typedef struct {
    Rbc_RenderContext core;
    double symbolSize;
    Rbc_NativeClip *nativeClip;
    /* Borrowed native GC; trace scratch storage and DC live until End. */
    Tk_Window nativeWindow; /* Borrowed text target, including external legends. */
    Drawable nativeDrawable;
    GC nativeGC;
    Rbc_NativeTrace *nativeTrace;
    Rbc_RenderNativeSymbolStyle *nativeSymbolStyle;
    Rbc_RenderBarStyle nativeBarStyle;
    Rbc_Tile nativeTile;
    Pixmap nativeStipple;
    const Point2D *nativeFillPoints;
    Tcl_Size nativeFillCount;
    const Segment2D *nativeArcSegments;
    Tcl_Size nativeArcCount;
    Rbc_NativePolygonBatch *nativePolygonBatch;
} NativeRenderContext;

#define NATIVE(ctx) ((NativeRenderContext *)(ctx))

/* Text stays on Tk's native font path for both screen renderers. A context
 * borrows the exact window/drawable supplied by the component; it does not
 * acquire a DC, allocate a surface or alter the surrounding clip scope. */
/**
 * @ingroup render_dispatch_internals
 * @brief Draw native text at integer-truncated screen coordinates.
 * Borrows the exact window/drawable captured by BeginScreenText and the text
 * style. Requires finite int-representable coordinates. Does not round, retain
 * the string or select Cairo even on a Cairo graph.
 */
static void NativeText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y) {
    Rbc_NativeText(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, string, style, (int)x, (int)y);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Draw a caller-owned cached layout on the captured native text target.
 * Preserves integer coordinates and delegates font/bitmap work to the native
 * backend. The layout, style and window must remain live for the call.
 */
static void NativeTextLayout(Rbc_RenderContext *ctx, TextLayout *layout, TextStyle *style, int x, int y) {
    Rbc_NativeTextLayout(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, layout, style, x, y);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Draw and measure text using the native text service.
 * Borrows text/style and writes the requested size through the backend. This
 * is not a measurement-only adapter; it uses the captured destination.
 */
static void NativeTextSize(Rbc_RenderContext *ctx, const char *string, TextStyle *style, int x, int y, Dim2D *size) {
    Rbc_NativeTextSize(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, string, style, x, y, size);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Free the screen-text adapter record.
 * Owns no font, window, layout or drawable. Called after common clip unwinding;
 * text contexts themselves expose no clip operations.
 */
static void NativeTextEnd(Rbc_RenderContext *ctx) { ckfree(ctx); }

static const Rbc_RenderOutputOps nativeTextOutputOps = {
    .text = NativeText, .textLayout = NativeTextLayout, .textSize = NativeTextSize};
static const Rbc_RenderOps nativeTextOps = {.output = &nativeTextOutputOps, .end = NativeTextEnd};

Rbc_RenderContext *Rbc_RenderBeginScreenText(Tk_Window tkwin, Drawable drawable) {
    Rbc_RenderContext *ctx = (Rbc_RenderContext *)ckalloc(sizeof(NativeRenderContext));
    memset(ctx, 0, sizeof(NativeRenderContext));
    ctx->ops = &nativeTextOps;
    NATIVE(ctx)->nativeWindow = tkwin;
    NATIVE(ctx)->nativeDrawable = drawable;
    return ctx;
}

void Rbc_RenderTextLayout(Rbc_RenderContext *ctx, TextLayout *layout, TextStyle *style, int x, int y) {
    ctx->ops->output->textLayout(ctx, layout, style, x, y);
}

void Rbc_RenderTextSize(Rbc_RenderContext *ctx, const char *string, TextStyle *style, int x, int y, Dim2D *size) {
    ctx->ops->output->textSize(ctx, string, style, x, y, size);
}

/* Raster presentation owns backend selection, including the existing Cairo
 * fallback. No native surface/DC is retained across a Tk image redraw. */
/**
 * @ingroup render_dispatch_internals
 * @brief Try the configured bitmap renderer before native presentation.
 * Uses the explicit foreground or queries the borrowed GC. With a usable
 * color, tries Rbc_RenderBitmap and returns when handled; otherwise delegates
 * the unchanged style to Rbc_NativeBitmap. Does not retain bitmap/mask/color
 * resources or establish an independent clipping scope.
 */
static void ScreenMappedBitmap(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *r,
                               const Rbc_RenderBitmapStyle *style) {
    const XColor *foreground = style->foreground;
    XColor gcColor;

    if ((foreground == NULL) && Rbc_RenderGCForeground(ctx->graphPtr, style->gc, &gcColor)) {
        foreground = &gcColor;
    }
    if ((foreground != NULL) &&
        Rbc_RenderBitmap(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, r, style->bitmap, style->mask, foreground,
                         style->background, style->backgroundPoints, style->backgroundCount)) {
        return;
    }
    Rbc_NativeBitmap(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, r, style);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Present a Tk image with a Cairo photo attempt and native fallback.
 * On a Cairo graph, resolves the image name to a photo and borrows its block
 * only during Rbc_RenderPhoto. Other images or failed attempts use the native
 * Tk image path. Coordinates truncate to int; no surface/DC is retained by
 * this adapter across Tk image redraw.
 */
static void ScreenTkImage(Rbc_RenderContext *ctx, Tk_Image image, double x, double y) {
    if (ctx->graphPtr->renderer == RBC_RENDERER_CAIRO) {
        Tk_PhotoHandle photo = Tk_FindPhoto(ctx->graphPtr->interp, Rbc_NameOfImage(image));
        Tk_PhotoImageBlock block;

        if ((photo != NULL) && Tk_PhotoGetImage(photo, &block) &&
            Rbc_RenderPhoto(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, &block, (int)x, (int)y)) {
            return;
        }
    }
    Rbc_NativeImage(NATIVE(ctx)->nativeDrawable, image, (int)x, (int)y);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Copy a borrowed pixmap using native presentation.
 * Delegates the mapped rectangle and GC unchanged. A Cairo graph still uses
 * this path because the input contains completed native pixels.
 */
static void ScreenPixmap(Rbc_RenderContext *ctx, Pixmap pixmap, GC gc, const Rbc_RenderRectangle *r) {
    Rbc_NativePixmap(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, pixmap, gc, r);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Free the screen-raster adapter record.
 * Does not release any caller-owned Tk image, pixmap, mask, GC or graph.
 */
static void ScreenRasterEnd(Rbc_RenderContext *ctx) { ckfree(ctx); }

static const Rbc_RenderOutputOps screenRasterOutputOps = {
    .image = ScreenTkImage, .mappedBitmap = ScreenMappedBitmap, .pixmap = ScreenPixmap};
static const Rbc_RenderOps screenRasterOps = {.output = &screenRasterOutputOps, .end = ScreenRasterEnd};

Rbc_RenderContext *Rbc_RenderBeginScreenRaster(Graph *graphPtr, Drawable drawable) {
    Rbc_RenderContext *ctx = (Rbc_RenderContext *)ckalloc(sizeof(NativeRenderContext));
    memset(ctx, 0, sizeof(NativeRenderContext));
    ctx->ops = &screenRasterOps;
    ctx->graphPtr = graphPtr;
    NATIVE(ctx)->nativeDrawable = drawable;
    return ctx;
}

void Rbc_RenderMappedBitmap(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *r, const Rbc_RenderBitmapStyle *style) {
    ctx->ops->output->mappedBitmap(ctx, r, style);
}

void Rbc_RenderPixmap(Rbc_RenderContext *ctx, Pixmap pixmap, GC gc, const Rbc_RenderRectangle *r) {
    ctx->ops->output->pixmap(ctx, pixmap, gc, r);
}

/* Small decorations need no retained surfaces or allocated contexts. Stack
 * contexts dispatch through the same presentation interface as export borders;
 * they never escape these one-shot calls and must not be passed to RenderEnd. */
/**
 * @ingroup render_dispatch_internals
 * @brief Draw a native Tk border or relief rectangle.
 * Truncates double origins to int and preserves size, fill and relief flags.
 * Called through a synchronous stack decoration context; owns no resources.
 */
static void ScreenBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y, int width, int height,
                         int borderWidth, int relief, int fill) {
    Rbc_NativeBorder(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, border, (int)x, (int)y, width, height,
                     borderWidth, relief, fill);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Fill an integer rectangle through the native rectangle service.
 * Borrows GC and optional tile and preserves their backend-specific behavior.
 * Does not select Cairo or retain resources after the call.
 */
static void ScreenRectangle(Rbc_RenderContext *ctx, GC gc, Rbc_Tile tile, int x, int y, int width, int height) {
    Rbc_NativeRectangle(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, gc, tile, x, y, width, height);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Draw native screen segments with the complete borrowed GC.
 * Preserves raster operation, clip, dash and cap settings. Used for overlays
 * including XOR; ordinary Cairo compositing is not an equivalent substitute.
 */
static void ScreenSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Rbc_NativeScreenSegments(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeGC, segments,
                             count);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Copy completed pixels to the captured destination.
 * Borrows both drawables and the GC, preserving integer source/destination
 * coordinates and dimensions. Does not rerender geometry or choose Cairo.
 */
static void ScreenCopyArea(Rbc_RenderContext *ctx, Drawable source, GC gc, int srcX, int srcY, unsigned int width,
                           unsigned int height, int dstX, int dstY) {
    Rbc_NativeCopyArea(NATIVE(ctx)->nativeWindow, source, NATIVE(ctx)->nativeDrawable, gc, srcX, srcY, width, height,
                       dstX, dstY);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Draw a native focus decoration on the captured target.
 * Borrows window and GC; the native backend owns the exact focus geometry.
 * The adapter neither changes graph geometry nor retains resources.
 */
static void ScreenFocus(Rbc_RenderContext *ctx, GC gc, int width) {
    Rbc_NativeFocus(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, gc, width);
}

static const Rbc_RenderOutputOps screenDecorationOutputOps = {
    .border = ScreenBorder, .screenRectangle = ScreenRectangle, .focus = ScreenFocus, .copyArea = ScreenCopyArea};
static const Rbc_RenderOps screenDecorationOps = {.segments = ScreenSegments, .output = &screenDecorationOutputOps};

void Rbc_RenderScreenBorder(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y, int width, int height,
                            int borderWidth, int relief, int fill) {
    NativeRenderContext ctx = {
        .core = {.ops = &screenDecorationOps}, .nativeWindow = tkwin, .nativeDrawable = drawable};
    Rbc_RenderBorder(&ctx.core, border, x, y, width, height, borderWidth, relief, fill);
}

void Rbc_RenderScreenRectangle(Tk_Window tkwin, Drawable drawable, GC gc, Rbc_Tile tile, int x, int y, int width,
                               int height) {
    NativeRenderContext ctx = {
        .core = {.ops = &screenDecorationOps}, .nativeWindow = tkwin, .nativeDrawable = drawable};
    ctx.core.ops->output->screenRectangle(&ctx.core, gc, tile, x, y, width, height);
}

/* Native-only presentation preserves the complete borrowed GC, including XOR,
 * clip masks, dash offsets and cap style. Do not select Cairo here: drawing the
 * same XOR geometry twice must restore the destination pixels exactly. */
void Rbc_RenderScreenSegments(Tk_Window tkwin, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size count) {
    NativeRenderContext ctx = {
        .core = {.ops = &screenDecorationOps}, .nativeWindow = tkwin, .nativeDrawable = drawable, .nativeGC = gc};
    Rbc_RenderSegments(&ctx.core, segments, count);
}

/* Copy completed screen pixels with the original GC and integer coordinates.
 * This is also used on Cairo graphs: there is no geometry to rerender. The
 * caller owns both drawables and determines clipping and presentation order. */
void Rbc_RenderScreenCopyArea(Tk_Window tkwin, Drawable source, Drawable destination, GC gc, int srcX, int srcY,
                              unsigned int width, unsigned int height, int dstX, int dstY) {
    NativeRenderContext ctx = {
        .core = {.ops = &screenDecorationOps}, .nativeWindow = tkwin, .nativeDrawable = destination};
    ctx.core.ops->output->copyArea(&ctx.core, source, gc, srcX, srcY, width, height, dstX, dstY);
}

void Rbc_RenderScreenFocus(Tk_Window tkwin, Drawable drawable, GC gc, int width) {
    NativeRenderContext ctx = {
        .core = {.ops = &screenDecorationOps}, .nativeWindow = tkwin, .nativeDrawable = drawable};
    ctx.core.ops->output->focus(&ctx.core, gc, width);
}

/* Screen clip scopes also enclose legacy Tk drawing that does not use a
 * geometry context yet. Platform resource handling stays in the backend. */
/**
 * @ingroup render_dispatch_internals
 * @brief Push a native rectangular clip frame.
 * Uses the owned native clip stack. Common dispatch has already incremented
 * clipDepth. Callers obtain the resulting target through Rbc_RenderClipDrawable
 * and must skip drawing for None while still balancing the frame.
 */
static void NativeClipBegin(Rbc_RenderContext *ctx, int x, int y, int width, int height) {
    Rbc_NativePushClip(NATIVE(ctx)->nativeClip, x, y, width, height);
}
/**
 * @ingroup render_dispatch_internals
 * @brief Pop and commit the current native clip frame.
 * Common dispatch decrements clipDepth first. Backend handling restores native
 * state or copies scratch pixels to the parent; no geometry is redrawn here.
 */
static void NativeClipEnd(Rbc_RenderContext *ctx) { Rbc_NativePopClip(NATIVE(ctx)->nativeClip); }

/**
 * @ingroup render_dispatch_internals
 * @brief Destroy the owned native clip stack and its adapter.
 * Invoked by Rbc_RenderEnd after common clip-depth unwinding. Borrowed graph,
 * base drawable and copy GC remain caller-owned.
 */
static void ScreenClipEnd(Rbc_RenderContext *ctx) {
    Rbc_NativeDestroyClip(NATIVE(ctx)->nativeClip);
    ckfree(ctx);
}

static const Rbc_RenderOps screenClipOps = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, ScreenClipEnd, NULL, NULL, NativeClipBegin, NativeClipEnd};

Rbc_RenderContext *Rbc_RenderBeginScreenClip(Graph *graphPtr, Drawable drawable, GC copyGC, int useDC) {
    Rbc_RenderContext *ctx = (Rbc_RenderContext *)ckalloc(sizeof(NativeRenderContext));
    memset(ctx, 0, sizeof(NativeRenderContext));
    ctx->ops = &screenClipOps;
    NATIVE(ctx)->nativeClip = Rbc_NativeCreateClip(graphPtr, drawable, copyGC, useDC);
    return ctx;
}

Drawable Rbc_RenderClipDrawable(Rbc_RenderContext *ctx) { return Rbc_NativeClipDrawable(NATIVE(ctx)->nativeClip); }

/* Native contexts dispatch once per trace/segment batch. The backend owns
 * trace scratch storage and platform drawing resources until End. */
/**
 * @ingroup render_dispatch_internals
 * @brief Submit one polyline to the retained native trace batch.
 * Requires a trace context initialized by Rbc_NativeBeginTrace. Borrows the
 * point array for this call; backend scratch storage and any DC survive until End.
 * Do not use the shared operation table as permission to call this on a
 * segments-only context whose trace pointer is NULL.
 */
static void NativePolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Rbc_NativePolyline(NATIVE(ctx)->nativeTrace, points, count);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Submit independent strip segments to the native strip path.
 * Borrows graph, drawable, GC and segment array. Keeps the strip-specific
 * backend batching behavior rather than translating it into joined polylines.
 */
static void NativeStrips(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Rbc_NativeStrips(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeGC, segments, count);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Submit independent segments using the borrowed native GC.
 * Uses the ordinary native segment backend, including its chunking and
 * platform resource handling. Does not own the array or a retained trace batch.
 */
static void NativeSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Rbc_NativeSegments(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeGC, segments, count);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Release any retained native trace and the stroke adapter.
 * Rbc_NativeEndTrace accepts NULL for strip/segment contexts. The caller GC,
 * graph and drawable remain borrowed and are not destroyed.
 */
static void NativeEnd(Rbc_RenderContext *ctx) {
    Rbc_NativeEndTrace(NATIVE(ctx)->nativeTrace);
    ckfree(ctx);
}

static const Rbc_RenderOps nativeStrokeOps = {NativePolyline, NativeSegments, NULL,      NULL, NULL, NULL,
                                              NULL,           NULL,           NativeEnd, NULL, NULL};
static const Rbc_RenderOps nativeStripOps = {NativePolyline, NativeStrips, NULL,      NULL, NULL, NULL,
                                             NULL,           NULL,         NativeEnd, NULL, NULL};

/* Begin an element stroke batch. Cairo retains its existing style setup; native
 * drawing uses the caller's GC unchanged. Only trace contexts allocate a point
 * buffer and hold a Windows DC/pen across all polylines in the batch. */
Rbc_RenderContext *Rbc_RenderBeginStroke(Graph *graphPtr, Drawable drawable, GC gc, Rbc_RenderStrokeKind kind,
                                         const XColor *color, double width, const Rbc_Dashes *dashes,
                                         const XColor *offColor) {
    Rbc_RenderContext *ctx;

    ctx = Rbc_RenderBegin(graphPtr, drawable, color, width, dashes, offColor);
    if (ctx != NULL) {
        return ctx;
    }
    if (drawable == None || gc == NULL) {
        return NULL;
    }
    ctx = (Rbc_RenderContext *)ckalloc(sizeof(NativeRenderContext));
    memset(ctx, 0, sizeof(NativeRenderContext));
    ctx->ops = kind == RBC_RENDER_STROKE_STRIPS ? &nativeStripOps : &nativeStrokeOps;
    ctx->graphPtr = graphPtr;
    NATIVE(ctx)->nativeDrawable = drawable;
    NATIVE(ctx)->nativeGC = gc;
    if (kind != RBC_RENDER_STROKE_TRACES) {
        return ctx;
    }
    NATIVE(ctx)->nativeTrace = Rbc_NativeBeginTrace(graphPtr, drawable, gc);
    if (NATIVE(ctx)->nativeTrace == NULL) {
        ckfree(ctx);
        return NULL;
    }
    return ctx;
}

/* Native screen capabilities use the same dispatch as Cairo and exporters.
 * These contexts borrow component resources; End releases only backend state. */
/**
 * @ingroup render_dispatch_internals
 * @brief Draw symbols with the retained native style and integer size.
 * The style pointer is borrowed, not copied. Native sampling may update its
 * counter; callers must keep style and referenced resources live until End.
 */
static void NativeSymbolPoints(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Rbc_NativeSymbolPoints(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeSymbolStyle,
                           (int)NATIVE(ctx)->symbolSize, count, points);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Dispatch a native fill through a retained batch or ordinary fill path.
 * Uses the polygon batch when available; otherwise passes the borrowed GC,
 * tile and stipple to Rbc_NativeFillPolygon. A failed or unsupported batch
 * constructor therefore retains the ordinary fill fallback.
 */
static void NativeFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    if (NATIVE(ctx)->nativePolygonBatch != NULL) {
        Rbc_NativeBatchPolygon(NATIVE(ctx)->nativePolygonBatch, points, count);
    } else {
        Rbc_NativeFillPolygon(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeGC,
                              NATIVE(ctx)->nativeTile, NATIVE(ctx)->nativeStipple, points, count);
    }
}

/**
 * @ingroup render_dispatch_internals
 * @brief Combine an optional Cairo fill with native bar decoration.
 * Copies the retained bar style locally. When positive-opacity fill is handled
 * by Rbc_RenderRectangles, sets only the local fill opacity to zero before
 * native bars draw relief/borders. Otherwise native bars retain fill ownership.
 * Neither the caller style nor the context copy is modified by this decision.
 */
static void ScreenBarRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rects, Tcl_Size count) {
    Rbc_RenderBarStyle style = NATIVE(ctx)->nativeBarStyle;
    if (style.fillOpacity > 0.0) {
        const XColor *background = style.border != NULL ? Tk_3DBorderColor(style.border) : NULL;
        const XColor *foreground = style.fgColor != NULL ? style.fgColor : background;
        if (Rbc_RenderRectangles(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, rects, count, foreground,
                                 style.fgColor != NULL ? background : NULL, style.stipple, style.fillOpacity)) {
            style.fillOpacity = 0.0; /* Fill complete; native bevel still applies. */
        }
    }
    Rbc_NativeBars(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, &style, rects, count);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Draw the native arc from borrowed precomputed segments.
 * The arc argument has passed common validation but is not consumed here.
 * Uses the arrays retained by BeginNativeArc; callers must keep those arrays
 * consistent with the validated geometry and alive until End.
 */
static void NativeArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    (void)arc;
    Rbc_NativeArcStroke(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeGC,
                        NATIVE(ctx)->nativeArcSegments, NATIVE(ctx)->nativeArcCount);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Fill the native arc from its borrowed precomputed polygon.
 * Ignores the analytic arc argument and delegates retained points to
 * NativeFillPolygon. Common dispatch has already rejected open-arc filling.
 */
static void NativeFillArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    (void)arc;
    NativeFillPolygon(ctx, NATIVE(ctx)->nativeFillPoints, NATIVE(ctx)->nativeFillCount);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Release a retained polygon batch and the geometry adapter.
 * Does not release borrowed symbol style, arc arrays, GC, tile or bar resources.
 * The batch, if present, restores and releases its platform-owned state.
 */
static void NativeGeometryEnd(Rbc_RenderContext *ctx) {
    if (NATIVE(ctx)->nativePolygonBatch != NULL)
        Rbc_NativeEndPolygonBatch(NATIVE(ctx)->nativePolygonBatch);
    ckfree(ctx);
}

static const Rbc_RenderOps nativeGeometryOps = {
    NULL, NativeSegments,    NULL,      NULL,         NativeFillPolygon, ScreenBarRectangles, NativeSymbolPoints,
    NULL, NativeGeometryEnd, NativeArc, NativeFillArc};

/**
 * @ingroup render_dispatch_internals
 * @brief Allocate a zeroed native geometry adapter with borrowed graph and target.
 * Returns an owned context requiring End. The shared table advertises several
 * callbacks, but each specialized constructor populates only its required
 * state. Calling another family's operation is not supported merely because
 * the shared callback pointer is non-NULL.
 */
static Rbc_RenderContext *BeginNativeGeometry(Graph *graphPtr, Drawable drawable) {
    Rbc_RenderContext *ctx = (Rbc_RenderContext *)ckalloc(sizeof(NativeRenderContext));
    memset(ctx, 0, sizeof(NativeRenderContext));
    ctx->ops = &nativeGeometryOps;
    ctx->graphPtr = graphPtr;
    NATIVE(ctx)->nativeDrawable = drawable;
    return ctx;
}

Rbc_RenderContext *Rbc_RenderBeginNativeSymbols(Graph *graphPtr, Drawable drawable, Rbc_RenderNativeSymbolStyle *style,
                                                int size) {
    Rbc_RenderContext *ctx = BeginNativeGeometry(graphPtr, drawable);
    NATIVE(ctx)->nativeSymbolStyle = style;
    NATIVE(ctx)->symbolSize = size;
    return ctx;
}

Rbc_RenderContext *Rbc_RenderBeginBars(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *style) {
    Rbc_RenderContext *ctx = BeginNativeGeometry(graphPtr, drawable);
    NATIVE(ctx)->nativeBarStyle = *style;
    return ctx;
}

Rbc_RenderContext *Rbc_RenderBeginNativeFill(Graph *graphPtr, Drawable drawable, GC gc, Rbc_Tile tile, Pixmap stipple) {
    Rbc_RenderContext *ctx = BeginNativeGeometry(graphPtr, drawable);
    NATIVE(ctx)->nativeGC = gc;
    NATIVE(ctx)->nativeTile = tile;
    NATIVE(ctx)->nativeStipple = stipple;
    return ctx;
}

Rbc_RenderContext *Rbc_RenderBeginNativeSegments(Graph *graphPtr, Drawable drawable, GC gc) {
    Rbc_RenderContext *ctx = BeginNativeGeometry(graphPtr, drawable);
    NATIVE(ctx)->nativeGC = gc;
    return ctx;
}

Rbc_RenderContext *Rbc_RenderBeginNativeArc(Graph *graphPtr, Drawable drawable, GC gc, const Point2D *fillPoints,
                                            Tcl_Size fillCount, const Segment2D *segments, Tcl_Size count) {
    Rbc_RenderContext *ctx = Rbc_RenderBeginNativeFill(graphPtr, drawable, gc, NULL, (Pixmap)1);
    NATIVE(ctx)->nativeFillPoints = fillPoints;
    NATIVE(ctx)->nativeFillCount = fillCount;
    NATIVE(ctx)->nativeArcSegments = segments;
    NATIVE(ctx)->nativeArcCount = count;
    return ctx;
}

Rbc_RenderContext *Rbc_RenderBeginNativePolygonBatch(Graph *graphPtr, Drawable drawable, GC gc, Tcl_Size maxPoints) {
    Rbc_RenderContext *ctx = Rbc_RenderBeginNativeFill(graphPtr, drawable, gc, NULL, (Pixmap)1);
    NATIVE(ctx)->nativePolygonBatch = Rbc_NativeBeginPolygonBatch(graphPtr, drawable, gc, maxPoints);
    return ctx;
}

/* Existing area clients retain opaque fills. */
int Rbc_RenderArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count, const XColor *foreground,
                   const XColor *background, Pixmap stipple) {
    return Rbc_RenderAreaOpacity(graphPtr, drawable, points, count, foreground, background, stipple, 1.0);
}

void Rbc_RenderSymbolPoints(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count) {
    if (count > 0) {
        ctx->ops->symbolPoints(ctx, centers, count);
    }
}

void Rbc_RenderFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    if (count >= 3) {
        ctx->ops->fillPolygon(ctx, points, count);
    }
}

void Rbc_RenderFillRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rectangles, Tcl_Size count) {
    if (count > 0) {
        ctx->ops->fillRectangles(ctx, rectangles, count);
    }
}

/**
 * @ingroup render_dispatch_internals
 * @brief Validate an ellipse arc and normalize its angle range.
 * Rejects NULL, nonfinite fields, nonpositive radii, zero extent and invalid
 * closure styles. Copies valid input, reduces start with fmod and clamps sweep
 * to [-360,360]. Negative start values remain negative. Output is only valid
 * on TRUE; this does not clip the ellipse or bound later integer conversions.
 */
static int NormalizeRenderArc(const Rbc_RenderArcGeometry *arc, Rbc_RenderArcGeometry *normalized) {
    if ((arc == NULL) || !FINITE(arc->cx) || !FINITE(arc->cy) || !FINITE(arc->rx) || !FINITE(arc->ry) ||
        !FINITE(arc->start) || !FINITE(arc->extent) || (arc->rx <= 0.0) || (arc->ry <= 0.0) || (arc->extent == 0.0) ||
        (arc->style < RBC_RENDER_ARC_OPEN) || (arc->style > RBC_RENDER_ARC_PIESLICE)) {
        return FALSE;
    }
    *normalized = *arc;
    normalized->start = fmod(arc->start, 360.0);
    normalized->extent = MAX(-360.0, MIN(360.0, arc->extent));
    return TRUE;
}

void Rbc_RenderArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    Rbc_RenderArcGeometry normalized;
    if ((ctx != NULL) && NormalizeRenderArc(arc, &normalized)) {
        ctx->ops->arc(ctx, &normalized);
    }
}

void Rbc_RenderFillArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    Rbc_RenderArcGeometry normalized;
    if ((ctx != NULL) && NormalizeRenderArc(arc, &normalized) && (normalized.style != RBC_RENDER_ARC_OPEN)) {
        ctx->ops->fillArc(ctx, &normalized);
    }
}

void Rbc_RenderPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    ctx->ops->polyline(ctx, points, count);
}

void Rbc_RenderSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    ctx->ops->segments(ctx, segments, count);
}

void Rbc_RenderLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle) {
    ctx->ops->lineStyle(ctx, capStyle, joinStyle);
}

void Rbc_RenderDashBackground(Rbc_RenderContext *ctx, const XColor *color) { ctx->ops->dashBackground(ctx, color); }

void Rbc_RenderEnd(Rbc_RenderContext *ctx) {
    while (ctx->clipDepth > 0)
        Rbc_RenderClipEnd(ctx);
    ctx->ops->end(ctx);
}

void Rbc_RenderText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y) {
    ctx->ops->output->text(ctx, string, style, x, y);
}

void Rbc_RenderTkImage(Rbc_RenderContext *ctx, Tk_Image image, double x, double y) {
    ctx->ops->output->image(ctx, image, x, y);
}

void Rbc_RenderPhotoImage(Rbc_RenderContext *ctx, Tk_PhotoHandle photo, double x, double y) {
    ctx->ops->output->photo(ctx, photo, x, y);
}

void Rbc_RenderWindow(Rbc_RenderContext *ctx, Tk_Window tkwin, double x, double y) {
    ctx->ops->output->window(ctx, tkwin, x, y);
}

void Rbc_RenderBackgroundPolygon(Rbc_RenderContext *ctx, const XColor *color, const Point2D *points, Tcl_Size count) {
    ctx->ops->output->backgroundPolygon(ctx, color, points, count);
}

void Rbc_RenderBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y, int width, int height,
                      int borderWidth, int relief, int fill) {
    ctx->ops->output->border(ctx, border, x, y, width, height, borderWidth, relief, fill);
}

void Rbc_RenderClearRectangle(Rbc_RenderContext *ctx, double x, double y, int width, int height) {
    ctx->ops->output->clearRectangle(ctx, x, y, width, height);
}

void Rbc_RenderBackgroundRectangles(Rbc_RenderContext *ctx, const XColor *color, const Rbc_RenderRectangle *rectangles,
                                    Tcl_Size count) {
    ctx->ops->output->backgroundRectangles(ctx, color, rectangles, count);
}

void Rbc_RenderPlotBegin(Rbc_RenderContext *ctx, Tk_Font font, double x, double y, int width, int height,
                         const XColor *background) {
    ctx->ops->output->plotBegin(ctx, font, x, y, width, height, background);
}

void Rbc_RenderPlotEnd(Rbc_RenderContext *ctx) { ctx->ops->output->plotEnd(ctx); }

void Rbc_RenderBitmapMask(Rbc_RenderContext *ctx, Display *display, Pixmap bitmap, double x, double y, int width,
                          int height, const XColor *color, int background) {
    ctx->ops->output->bitmapMask(ctx, display, bitmap, x, y, width, height, color, background);
}

/* SVG writes mapped geometry directly; it is independent of the screen backend. */
void Rbc_RenderClipBegin(Rbc_RenderContext *ctx, int x, int y, int width, int height) {
    ++ctx->clipDepth;
    ctx->ops->clipBegin(ctx, x, y, width, height);
}

void Rbc_RenderClipEnd(Rbc_RenderContext *ctx) {
    if (ctx->clipDepth == 0)
        return;
    --ctx->clipDepth;
    ctx->ops->clipEnd(ctx);
}

/* Narrow native presentation keeps original batching and GC semantics. */
/**
 * @ingroup render_dispatch_internals
 * @brief Draw a native text-marker background using mapped geometry.
 * Borrows outline and anchor; delegates their original conversion order to
 * the native backend rather than converting them separately in this layer.
 */
static void ScreenTextBackground(Rbc_RenderContext *ctx, GC gc, const Point2D *outline, const Point2D *anchor) {
    Rbc_NativeTextBackground(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, gc, outline, anchor);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Draw a native arrowhead through scan-converted spans.
 * Borrows the mapped point array and GC. Retains native span batching and
 * integer conversion policy; no Cairo geometry replacement is attempted.
 */
static void ScreenArrowHead(Rbc_RenderContext *ctx, GC gc, const Point2D *points) {
    Rbc_NativeArrowHead(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, gc, points);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Draw a native line legend sample.
 * Preserves integer geometry and the borrowed GC, including stipple-origin
 * behavior implemented by the native helper.
 */
static void ScreenLegendLine(Rbc_RenderContext *ctx, GC gc, int x, int y, int size) {
    Rbc_NativeLegendLine(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, gc, x, y, size);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Draw a native bar legend sample.
 * Borrows graph, GC and target; passes integer geometry to the native helper
 * without changing the graph's renderer or component options.
 */
static void ScreenLegendBar(Rbc_RenderContext *ctx, GC gc, int x, int y, int size) {
    Rbc_NativeLegendBar(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, gc, x, y, size);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Execute a synchronous native marker run.
 * Borrows callback data and function for the duration of native batching.
 * Nothing is retained after return; backend-specific DC lifetime remains in
 * Rbc_NativeRun. This adapter adds no Tcl callback lifetime protection.
 */
static void ScreenRun(Rbc_RenderContext *ctx, void *data, Tcl_Size count, Rbc_RenderRunProc *proc) {
    Rbc_NativeRun(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, data, count, proc);
}

/**
 * @ingroup render_dispatch_internals
 * @brief Draw repeated native bitmap positions as one backend run.
 * Borrows bitmap, GC and position array. Leaves opaque bitmap/DC batching
 * and resource restoration to the native backend.
 */
static void ScreenBitmapRun(Rbc_RenderContext *ctx, Pixmap bitmap, GC gc, int width, int height,
                            const Rbc_RenderPosition *positions, Tcl_Size count) {
    Rbc_NativeBitmapRun(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, bitmap, gc, width, height, positions, count);
}

static const Rbc_RenderOutputOps screenDetailOutputOps = {.textBackground = ScreenTextBackground,
                                                          .arrowHead = ScreenArrowHead,
                                                          .legendLine = ScreenLegendLine,
                                                          .legendBar = ScreenLegendBar,
                                                          .run = ScreenRun,
                                                          .bitmapRun = ScreenBitmapRun};
static const Rbc_RenderOps screenDetailOps = {.output = &screenDetailOutputOps};

void Rbc_RenderScreenTextBackground(Graph *graphPtr, Drawable drawable, GC gc, const Point2D *outline,
                                    const Point2D *anchor) {
    NativeRenderContext ctx = {.core = {.ops = &screenDetailOps, .graphPtr = graphPtr}, .nativeDrawable = drawable};
    ctx.core.ops->output->textBackground(&ctx.core, gc, outline, anchor);
}

void Rbc_RenderScreenArrowHead(Graph *graphPtr, Drawable drawable, GC gc, const Point2D *points) {
    NativeRenderContext ctx = {.core = {.ops = &screenDetailOps, .graphPtr = graphPtr}, .nativeDrawable = drawable};
    ctx.core.ops->output->arrowHead(&ctx.core, gc, points);
}

void Rbc_RenderScreenLegendLine(Graph *graphPtr, Drawable drawable, GC gc, int x, int y, int size) {
    NativeRenderContext ctx = {.core = {.ops = &screenDetailOps, .graphPtr = graphPtr}, .nativeDrawable = drawable};
    ctx.core.ops->output->legendLine(&ctx.core, gc, x, y, size);
}

void Rbc_RenderScreenLegendBar(Graph *graphPtr, Drawable drawable, GC gc, int x, int y, int size) {
    NativeRenderContext ctx = {.core = {.ops = &screenDetailOps, .graphPtr = graphPtr}, .nativeDrawable = drawable};
    ctx.core.ops->output->legendBar(&ctx.core, gc, x, y, size);
}

void Rbc_RenderScreenRun(Graph *graphPtr, Drawable drawable, void *data, Tcl_Size count, Rbc_RenderRunProc *proc) {
    NativeRenderContext ctx = {.core = {.ops = &screenDetailOps, .graphPtr = graphPtr}, .nativeDrawable = drawable};
    ctx.core.ops->output->run(&ctx.core, data, count, proc);
}

void Rbc_RenderScreenBitmapRun(Graph *graphPtr, Drawable drawable, Pixmap bitmap, GC gc, int width, int height,
                               const Rbc_RenderPosition *positions, Tcl_Size count) {
    NativeRenderContext ctx = {.core = {.ops = &screenDetailOps, .graphPtr = graphPtr}, .nativeDrawable = drawable};
    ctx.core.ops->output->bitmapRun(&ctx.core, bitmap, gc, width, height, positions, count);
}
