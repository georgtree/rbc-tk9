/* Common screen adapters and rendering dispatch. See license.terms. */
#include "rbcRenderInt.h"
#include "rbcRenderNative.h"

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
static void NativeText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y) {
    Rbc_NativeText(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, string, style, (int)x, (int)y);
}

static void NativeTextLayout(Rbc_RenderContext *ctx, TextLayout *layout, TextStyle *style, int x, int y) {
    Rbc_NativeTextLayout(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, layout, style, x, y);
}

static void NativeTextSize(Rbc_RenderContext *ctx, const char *string, TextStyle *style, int x, int y, Dim2D *size) {
    Rbc_NativeTextSize(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, string, style, x, y, size);
}

static void NativeTextEnd(Rbc_RenderContext *ctx) {
    ckfree(ctx);
}

static const Rbc_RenderOutputOps nativeTextOutputOps = {
    .text = NativeText, .textLayout = NativeTextLayout, .textSize = NativeTextSize
};
static const Rbc_RenderOps nativeTextOps = {
    .output = &nativeTextOutputOps, .end = NativeTextEnd
};

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
static void ScreenMappedBitmap(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *r,
                                const Rbc_RenderBitmapStyle *style) {
    const XColor *foreground = style->foreground;
    XColor gcColor;

    if ((foreground == NULL) && Rbc_RenderGCForeground(ctx->graphPtr, style->gc, &gcColor)) {
        foreground = &gcColor;
    }
    if ((foreground != NULL) && Rbc_RenderBitmap(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, r, style->bitmap,
            style->mask, foreground, style->background, style->backgroundPoints, style->backgroundCount)) {
        return;
    }
    Rbc_NativeBitmap(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, r, style);
}

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

static void ScreenPixmap(Rbc_RenderContext *ctx, Pixmap pixmap, GC gc, const Rbc_RenderRectangle *r) {
    Rbc_NativePixmap(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, pixmap, gc, r);
}

static void ScreenRasterEnd(Rbc_RenderContext *ctx) {
    ckfree(ctx);
}

static const Rbc_RenderOutputOps screenRasterOutputOps = {
    .image = ScreenTkImage, .mappedBitmap = ScreenMappedBitmap, .pixmap = ScreenPixmap
};
static const Rbc_RenderOps screenRasterOps = {
    .output = &screenRasterOutputOps, .end = ScreenRasterEnd
};

Rbc_RenderContext *Rbc_RenderBeginScreenRaster(Graph *graphPtr, Drawable drawable) {
    Rbc_RenderContext *ctx = (Rbc_RenderContext *)ckalloc(sizeof(NativeRenderContext));
    memset(ctx, 0, sizeof(NativeRenderContext));
    ctx->ops = &screenRasterOps;
    ctx->graphPtr = graphPtr;
    NATIVE(ctx)->nativeDrawable = drawable;
    return ctx;
}

void Rbc_RenderMappedBitmap(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *r,
                            const Rbc_RenderBitmapStyle *style) {
    ctx->ops->output->mappedBitmap(ctx, r, style);
}

void Rbc_RenderPixmap(Rbc_RenderContext *ctx, Pixmap pixmap, GC gc, const Rbc_RenderRectangle *r) {
    ctx->ops->output->pixmap(ctx, pixmap, gc, r);
}

/* Small decorations need no retained surfaces or allocated contexts. Stack
 * contexts dispatch through the same presentation interface as export borders;
 * they never escape these one-shot calls and must not be passed to RenderEnd. */
static void ScreenBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y,
                          int width, int height, int borderWidth, int relief, int fill) {
    Rbc_NativeBorder(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, border, (int)x, (int)y,
                     width, height, borderWidth, relief, fill);
}

static void ScreenRectangle(Rbc_RenderContext *ctx, GC gc, Rbc_Tile tile, int x, int y, int width, int height) {
    Rbc_NativeRectangle(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, gc, tile, x, y, width, height);
}

static void ScreenSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Rbc_NativeScreenSegments(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeGC, segments, count);
}

static void ScreenCopyArea(Rbc_RenderContext *ctx, Drawable source, GC gc, int srcX, int srcY,
                           unsigned int width, unsigned int height, int dstX, int dstY) {
    Rbc_NativeCopyArea(NATIVE(ctx)->nativeWindow, source, NATIVE(ctx)->nativeDrawable, gc,
                       srcX, srcY, width, height, dstX, dstY);
}

static void ScreenFocus(Rbc_RenderContext *ctx, GC gc, int width) {
    Rbc_NativeFocus(NATIVE(ctx)->nativeWindow, NATIVE(ctx)->nativeDrawable, gc, width);
}

static const Rbc_RenderOutputOps screenDecorationOutputOps = {
    .border = ScreenBorder, .screenRectangle = ScreenRectangle, .focus = ScreenFocus, .copyArea = ScreenCopyArea
};
static const Rbc_RenderOps screenDecorationOps = {.segments = ScreenSegments, .output = &screenDecorationOutputOps};

void Rbc_RenderScreenBorder(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y,
                            int width, int height, int borderWidth, int relief, int fill) {
    NativeRenderContext ctx = {.core = {.ops = &screenDecorationOps}, .nativeWindow = tkwin, .nativeDrawable = drawable};
    Rbc_RenderBorder(&ctx.core, border, x, y, width, height, borderWidth, relief, fill);
}

void Rbc_RenderScreenRectangle(Tk_Window tkwin, Drawable drawable, GC gc, Rbc_Tile tile,
                               int x, int y, int width, int height) {
    NativeRenderContext ctx = {.core = {.ops = &screenDecorationOps}, .nativeWindow = tkwin, .nativeDrawable = drawable};
    ctx.core.ops->output->screenRectangle(&ctx.core, gc, tile, x, y, width, height);
}

/* Native-only presentation preserves the complete borrowed GC, including XOR,
 * clip masks, dash offsets and cap style. Do not select Cairo here: drawing the
 * same XOR geometry twice must restore the destination pixels exactly. */
void Rbc_RenderScreenSegments(Tk_Window tkwin, Drawable drawable, GC gc,
    const Segment2D *segments, Tcl_Size count) {
    NativeRenderContext ctx = {.core = {.ops = &screenDecorationOps}, .nativeWindow = tkwin,
                            .nativeDrawable = drawable, .nativeGC = gc};
    Rbc_RenderSegments(&ctx.core, segments, count);
}

/* Copy completed screen pixels with the original GC and integer coordinates.
 * This is also used on Cairo graphs: there is no geometry to rerender. The
 * caller owns both drawables and determines clipping and presentation order. */
void Rbc_RenderScreenCopyArea(Tk_Window tkwin, Drawable source, Drawable destination, GC gc,
                              int srcX, int srcY, unsigned int width, unsigned int height, int dstX, int dstY) {
    NativeRenderContext ctx = {.core = {.ops = &screenDecorationOps}, .nativeWindow = tkwin, .nativeDrawable = destination};
    ctx.core.ops->output->copyArea(&ctx.core, source, gc, srcX, srcY, width, height, dstX, dstY);
}

void Rbc_RenderScreenFocus(Tk_Window tkwin, Drawable drawable, GC gc, int width) {
    NativeRenderContext ctx = {.core = {.ops = &screenDecorationOps}, .nativeWindow = tkwin, .nativeDrawable = drawable};
    ctx.core.ops->output->focus(&ctx.core, gc, width);
}

/* Screen clip scopes also enclose legacy Tk drawing that does not use a
 * geometry context yet. Platform resource handling stays in the backend. */
static void NativeClipBegin(Rbc_RenderContext *ctx, int x, int y, int width, int height) {
    Rbc_NativePushClip(NATIVE(ctx)->nativeClip, x, y, width, height);
}
static void NativeClipEnd(Rbc_RenderContext *ctx) {
    Rbc_NativePopClip(NATIVE(ctx)->nativeClip);
}

static void ScreenClipEnd(Rbc_RenderContext *ctx) {
    Rbc_NativeDestroyClip(NATIVE(ctx)->nativeClip);
    ckfree(ctx);
}

static const Rbc_RenderOps screenClipOps = {
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, ScreenClipEnd, NULL, NULL, NativeClipBegin, NativeClipEnd
};

Rbc_RenderContext *Rbc_RenderBeginScreenClip(Graph *graphPtr, Drawable drawable, GC copyGC, int useDC) {
    Rbc_RenderContext *ctx = (Rbc_RenderContext *)ckalloc(sizeof(NativeRenderContext));
    memset(ctx, 0, sizeof(NativeRenderContext));
    ctx->ops = &screenClipOps;
    NATIVE(ctx)->nativeClip = Rbc_NativeCreateClip(graphPtr, drawable, copyGC, useDC);
    return ctx;
}

Drawable Rbc_RenderClipDrawable(Rbc_RenderContext *ctx) {
    return Rbc_NativeClipDrawable(NATIVE(ctx)->nativeClip);
}

/* Native contexts dispatch once per trace/segment batch. The backend owns
 * trace scratch storage and platform drawing resources until End. */
static void NativePolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Rbc_NativePolyline(NATIVE(ctx)->nativeTrace, points, count);
}

static void NativeStrips(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Rbc_NativeStrips(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeGC, segments, count);
}

static void NativeSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Rbc_NativeSegments(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeGC, segments, count);
}

static void NativeEnd(Rbc_RenderContext *ctx) {
    Rbc_NativeEndTrace(NATIVE(ctx)->nativeTrace);
    ckfree(ctx);
}

static const Rbc_RenderOps nativeStrokeOps = {
    NativePolyline, NativeSegments, NULL, NULL, NULL, NULL, NULL, NULL, NativeEnd, NULL, NULL
};
static const Rbc_RenderOps nativeStripOps = {
    NativePolyline, NativeStrips, NULL, NULL, NULL, NULL, NULL, NULL, NativeEnd, NULL, NULL
};

/* Begin an element stroke batch. Cairo retains its existing style setup; native
 * drawing uses the caller's GC unchanged. Only trace contexts allocate a point
 * buffer and hold a Windows DC/pen across all polylines in the batch. */
Rbc_RenderContext *Rbc_RenderBeginStroke(Graph *graphPtr, Drawable drawable, GC gc,
    Rbc_RenderStrokeKind kind, const XColor *color, double width,
    const Rbc_Dashes *dashes, const XColor *offColor) {
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
static void NativeSymbolPoints(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Rbc_NativeSymbolPoints(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeSymbolStyle,
        (int)NATIVE(ctx)->symbolSize, count, points);
}

static void NativeFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    if (NATIVE(ctx)->nativePolygonBatch != NULL) {
        Rbc_NativeBatchPolygon(NATIVE(ctx)->nativePolygonBatch, points, count);
    } else {
        Rbc_NativeFillPolygon(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeGC, NATIVE(ctx)->nativeTile,
            NATIVE(ctx)->nativeStipple, points, count);
    }
}

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

static void NativeArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    (void)arc;
    Rbc_NativeArcStroke(ctx->graphPtr, NATIVE(ctx)->nativeDrawable, NATIVE(ctx)->nativeGC,
        NATIVE(ctx)->nativeArcSegments, NATIVE(ctx)->nativeArcCount);
}

static void NativeFillArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    (void)arc;
    NativeFillPolygon(ctx, NATIVE(ctx)->nativeFillPoints, NATIVE(ctx)->nativeFillCount);
}

static void NativeGeometryEnd(Rbc_RenderContext *ctx) {
    if (NATIVE(ctx)->nativePolygonBatch != NULL) Rbc_NativeEndPolygonBatch(NATIVE(ctx)->nativePolygonBatch);
    ckfree(ctx);
}

static const Rbc_RenderOps nativeGeometryOps = {
    NULL, NativeSegments, NULL, NULL, NativeFillPolygon, ScreenBarRectangles, NativeSymbolPoints, NULL,
    NativeGeometryEnd, NativeArc, NativeFillArc
};

static Rbc_RenderContext *BeginNativeGeometry(Graph *graphPtr, Drawable drawable) {
    Rbc_RenderContext *ctx = (Rbc_RenderContext *)ckalloc(sizeof(NativeRenderContext));
    memset(ctx, 0, sizeof(NativeRenderContext));
    ctx->ops = &nativeGeometryOps;
    ctx->graphPtr = graphPtr;
    NATIVE(ctx)->nativeDrawable = drawable;
    return ctx;
}

Rbc_RenderContext *Rbc_RenderBeginNativeSymbols(Graph *graphPtr, Drawable drawable,
    Rbc_RenderNativeSymbolStyle *style, int size) {
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

Rbc_RenderContext *Rbc_RenderBeginNativeFill(Graph *graphPtr, Drawable drawable, GC gc, Rbc_Tile tile,
    Pixmap stipple) {
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

Rbc_RenderContext *Rbc_RenderBeginNativeArc(Graph *graphPtr, Drawable drawable, GC gc,
    const Point2D *fillPoints, Tcl_Size fillCount, const Segment2D *segments, Tcl_Size count) {
    Rbc_RenderContext *ctx = Rbc_RenderBeginNativeFill(graphPtr, drawable, gc, NULL, (Pixmap)1);
    NATIVE(ctx)->nativeFillPoints = fillPoints;
    NATIVE(ctx)->nativeFillCount = fillCount;
    NATIVE(ctx)->nativeArcSegments = segments;
    NATIVE(ctx)->nativeArcCount = count;
    return ctx;
}

Rbc_RenderContext *Rbc_RenderBeginNativePolygonBatch(Graph *graphPtr, Drawable drawable, GC gc,
    Tcl_Size maxPoints) {
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

static int NormalizeRenderArc(const Rbc_RenderArcGeometry *arc, Rbc_RenderArcGeometry *normalized) {
    if ((arc == NULL) || !FINITE(arc->cx) || !FINITE(arc->cy) || !FINITE(arc->rx) || !FINITE(arc->ry) ||
        !FINITE(arc->start) || !FINITE(arc->extent) || (arc->rx <= 0.0) || (arc->ry <= 0.0) ||
        (arc->extent == 0.0) || (arc->style < RBC_RENDER_ARC_OPEN) || (arc->style > RBC_RENDER_ARC_PIESLICE)) {
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

void Rbc_RenderDashBackground(Rbc_RenderContext *ctx, const XColor *color) {
    ctx->ops->dashBackground(ctx, color);
}

void Rbc_RenderEnd(Rbc_RenderContext *ctx) {
    while (ctx->clipDepth > 0) Rbc_RenderClipEnd(ctx);
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



void Rbc_RenderPlotEnd(Rbc_RenderContext *ctx) {
    ctx->ops->output->plotEnd(ctx);
}

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
    if (ctx->clipDepth == 0) return;
    --ctx->clipDepth;
    ctx->ops->clipEnd(ctx);
}
