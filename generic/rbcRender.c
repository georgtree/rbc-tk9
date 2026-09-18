/*
 * rbcRender.c -- Screen and export rendering for mapped graph geometry.
 * See license.terms for details.
 */
#include "rbcRender.h"

#ifdef RBC_HAVE_CAIRO
#include <cairo.h>
#if CAIRO_VERSION < CAIRO_VERSION_ENCODE(1, 12, 0)
#error RBC requires Cairo 1.12 or newer.
#endif
#ifdef WIN32
#include <cairo-win32.h>
#if !CAIRO_HAS_WIN32_SURFACE
#error Cairo must be built with Win32 surface support.
#endif
#else
#include <cairo-xlib.h>
#endif
#endif /* RBC_HAVE_CAIRO: headers */

/* Presentation capabilities used by export contexts. */
typedef struct {
    void (*text)(Rbc_RenderContext *, char *string, TextStyle *style, double x, double y);
    void (*photo)(Rbc_RenderContext *, Tk_PhotoHandle photo, double x, double y);
    void (*window)(Rbc_RenderContext *, Tk_Window tkwin, double x, double y);
    void (*backgroundPolygon)(Rbc_RenderContext *, const XColor *color, const Point2D *points, Tcl_Size count);
    void (*border)(Rbc_RenderContext *, Tk_3DBorder border, double x, double y, int width, int height, int borderWidth,
                   int relief, int fill);
    void (*clearRectangle)(Rbc_RenderContext *, double x, double y, int width, int height);
    void (*backgroundRectangles)(Rbc_RenderContext *, const XColor *color, const Rbc_RenderRectangle *rectangles,
                                 Tcl_Size count);
    void (*plotBegin)(Rbc_RenderContext *, Tk_Font font, double x, double y, int width, int height,
                      const XColor *background);
    void (*plotEnd)(Rbc_RenderContext *);
    void (*bitmapMask)(Rbc_RenderContext *, Display *display, Pixmap bitmap, double x, double y, int width, int height,
                       const XColor *color, int background);
    void (*image)(Rbc_RenderContext *, Tk_Image image, double x, double y);
} Rbc_RenderOutputOps;

/* Geometry dispatch is shared by screen and export contexts. */
typedef struct {
    void (*polyline)(Rbc_RenderContext *, const Point2D *, Tcl_Size);
    void (*segments)(Rbc_RenderContext *, const Segment2D *, Tcl_Size);
    void (*lineStyle)(Rbc_RenderContext *, int, int);
    void (*dashBackground)(Rbc_RenderContext *, const XColor *);
    void (*fillPolygon)(Rbc_RenderContext *, const Point2D *, Tcl_Size);
    void (*fillRectangles)(Rbc_RenderContext *, const Rbc_RenderRectangle *, Tcl_Size);
    void (*symbolPoints)(Rbc_RenderContext *, const Point2D *, Tcl_Size);
    const Rbc_RenderOutputOps *output;
    void (*end)(Rbc_RenderContext *);
} Rbc_RenderOps;

struct Rbc_RenderContext {
    const Rbc_RenderOps *ops;
    PsToken psToken;
    Rbc_ExportContext *exportPtr;
    int postScriptDashed;
    Graph *graphPtr;
    Rbc_RenderFillStyle fillStyle;
    const char *symbolMacro;
    double symbolSize;
    XColor svgColor, svgOffColor;
    int svgHasColor, svgHasOffColor, svgWidth, svgCap, svgJoin;
    Rbc_Dashes svgDashes;
    Rbc_RenderSymbolStyle svgSymbol;
    int svgBarSymbol;
    unsigned int svgBitmapId;
    int svgBitmapWidth, svgBitmapHeight;
    unsigned int svgPatternId;
    Rbc_Tile svgTile;
#ifdef RBC_HAVE_CAIRO
    cairo_surface_t *surface;
    cairo_t *cr;
    cairo_pattern_t *bitmapPattern;
    int bitmapWidth, bitmapHeight;
    XColor foreground;
    XColor offColor;
    int doubleDash;
    int nDashes;
    double dashes[RBC_MAX_DASH_VALUES];
    double dashOffset;
#ifdef WIN32
    Rbc_WinDrawableDC *dcState;
    HDC dc;
    int savedDC;
#endif
#endif
};

static const Rbc_RenderOps svgOps;
static void SvgError(Rbc_ExportContext *token, const char *message);
static unsigned int SvgFillPattern(Rbc_RenderContext *ctx, const XColor *foreground);

#ifdef RBC_HAVE_CAIRO
static void CairoPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
static void CairoSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count);
static void CairoLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle);
static void CairoDashBackground(Rbc_RenderContext *ctx, const XColor *color);
static void CairoFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
static void CairoFillRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rectangles, Tcl_Size count);
static void CairoEnd(Rbc_RenderContext *ctx);

static const Rbc_RenderOps cairoOps = {
    CairoPolyline, CairoSegments, CairoLineStyle, CairoDashBackground,
    CairoFillPolygon, CairoFillRectangles, NULL, NULL, CairoEnd
};

#ifdef WIN32
struct Rbc_RenderTarget {
    Graph *graphPtr;
    cairo_surface_t *surface;
    cairo_surface_t *image;
    Rbc_WinDrawableDC *destinationState;
    HDC destinationDC, dc;
    Drawable drawable;
};

/* One DIB for the marker pass avoids per-marker DDB readback. */
Rbc_RenderTarget *Rbc_RenderBeginMarkerPass(Graph *graphPtr, Drawable *drawablePtr) {
    Rbc_RenderTarget *target;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (graphPtr->renderTarget != NULL) || (graphPtr->width <= 0) ||
        (graphPtr->height <= 0)) {
        return NULL;
    }
    target = Tcl_AttemptAlloc(sizeof(*target));
    if (target == NULL) {
        return NULL;
    }
    memset(target, 0, sizeof(*target));
    target->graphPtr = graphPtr;
    target->destinationDC = Rbc_WinAcquireDrawableDC(graphPtr->display, *drawablePtr, &target->destinationState);
    if ((target->destinationDC == NULL) ||
        ((GetObjectType(target->destinationDC) != OBJ_DC) && (GetObjectType(target->destinationDC) != OBJ_MEMDC)) ||
        (GetDeviceCaps(target->destinationDC, TECHNOLOGY) != DT_RASDISPLAY)) {
        goto fail;
    }
    target->surface = cairo_win32_surface_create_with_dib(CAIRO_FORMAT_RGB24, graphPtr->width, graphPtr->height);
    if (cairo_surface_status(target->surface) != CAIRO_STATUS_SUCCESS) {
        goto fail;
    }
    target->dc = cairo_win32_surface_get_dc(target->surface);
    target->image = cairo_win32_surface_get_image(target->surface);
    if ((target->dc == NULL) || (target->image == NULL) ||
        (cairo_surface_status(target->image) != CAIRO_STATUS_SUCCESS)) {
        goto fail;
    }
    GdiFlush();
    if (!BitBlt(target->dc, 0, 0, graphPtr->width, graphPtr->height, target->destinationDC, 0, 0, SRCCOPY)) {
        goto fail;
    }
    GdiFlush();
    cairo_surface_mark_dirty(target->image);
    target->drawable = Rbc_WinCreateDrawableFromDC(target->dc);
    if (target->drawable == None) {
        goto fail;
    }
    graphPtr->renderTarget = target;
    *drawablePtr = target->drawable;
    return target;

fail:
    if (target->surface != NULL) {
        cairo_surface_destroy(target->surface);
    }
    Rbc_WinReleaseDrawableDC(target->destinationState);
    ckfree(target);
    return NULL;
}

void Rbc_RenderEndMarkerPass(Rbc_RenderTarget *target) {
    if (target == NULL) {
        return;
    }
    cairo_surface_flush(target->image);
    GdiFlush();
    if (!BitBlt(target->destinationDC, 0, 0, target->graphPtr->width, target->graphPtr->height, target->dc, 0, 0,
                SRCCOPY)) {
        Tcl_Interp *interp = target->graphPtr->interp;
        Tcl_InterpState saved = Tcl_SaveInterpState(interp, TCL_OK);
        Tcl_SetObjResult(interp, Tcl_NewStringObj("Cairo marker target copy failed", -1));
        Tcl_BackgroundException(interp, TCL_ERROR);
        Tcl_RestoreInterpState(interp, saved);
    }
    GdiFlush();
    target->graphPtr->renderTarget = NULL;
    Rbc_WinFreeDrawableFromDC(target->drawable);
    cairo_surface_destroy(target->surface);
    Rbc_WinReleaseDrawableDC(target->destinationState);
    ckfree(target);
}
#else
Rbc_RenderTarget *Rbc_RenderBeginMarkerPass(Graph *graphPtr, Drawable *drawablePtr) {
    (void)graphPtr;
    (void)drawablePtr;
    return NULL;
}
void Rbc_RenderEndMarkerPass(Rbc_RenderTarget *target) { (void)target; }
#endif


static void FreeRenderContext(Rbc_RenderContext *ctx) {
    if (ctx->bitmapPattern != NULL) {
        cairo_pattern_destroy(ctx->bitmapPattern);
    }
    if (ctx->cr != NULL) {
        cairo_destroy(ctx->cr);
    }
    if (ctx->surface != NULL) {
        cairo_surface_destroy(ctx->surface);
    }
#ifdef WIN32
    if (ctx->savedDC != 0) {
        RestoreDC(ctx->dc, ctx->savedDC);
    }
    Rbc_WinReleaseDrawableDC(ctx->dcState);
#endif
    ckfree(ctx);
}

static cairo_antialias_t GetAntialias(int mode) {
    switch (mode) {
    case RBC_ANTIALIAS_NONE:
        return CAIRO_ANTIALIAS_NONE;
    case RBC_ANTIALIAS_GRAY:
        return CAIRO_ANTIALIAS_GRAY;
    case RBC_ANTIALIAS_FAST:
        return CAIRO_ANTIALIAS_FAST;
    case RBC_ANTIALIAS_GOOD:
        return CAIRO_ANTIALIAS_GOOD;
    case RBC_ANTIALIAS_BEST:
        return CAIRO_ANTIALIAS_BEST;
    default:
        return CAIRO_ANTIALIAS_DEFAULT;
    }
}

static void SetStrokeColor(cairo_t *cr, const XColor *colorPtr) {
    cairo_set_source_rgb(cr, colorPtr->red / 65535.0, colorPtr->green / 65535.0, colorPtr->blue / 65535.0);
}

/* Match the existing PostScript offdash-underlay convention. */
static void StrokeRenderPath(Rbc_RenderContext *ctx) {
    if (ctx->doubleDash) {
        cairo_set_dash(ctx->cr, NULL, 0, 0.0);
        SetStrokeColor(ctx->cr, &ctx->offColor);
        cairo_stroke_preserve(ctx->cr);
    }
    SetStrokeColor(ctx->cr, &ctx->foreground);
    cairo_set_dash(ctx->cr, ctx->dashes, ctx->nDashes, ctx->dashOffset);
    cairo_stroke(ctx->cr);
}

/* Resolve GC defaults without treating an empty option as transparency. */
int Rbc_RenderGCForeground(Graph *graphPtr, GC gc, XColor *color) {
    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (gc == NULL)) {
        return FALSE;
    }
    memset(color, 0, sizeof(*color));
#ifdef WIN32
    color->pixel = gc->foreground;
    color->red = GetRValue(color->pixel) * 257;
    color->green = GetGValue(color->pixel) * 257;
    color->blue = GetBValue(color->pixel) * 257;
#else
    XGCValues values;

    if (!XGetGCValues(graphPtr->display, gc, GCForeground, &values)) {
        return FALSE;
    }
    color->pixel = values.foreground;
    XQueryColors(graphPtr->display, Tk_Colormap(graphPtr->tkwin), color, 1);
#endif
    return TRUE;
}

/* Open one uninterrupted Cairo drawing batch; NULL requests native drawing. */
static Rbc_RenderContext *BeginRenderTarget(Graph *graphPtr, Drawable drawable, const XColor *colorPtr, double width,
                                            const Rbc_Dashes *dashesPtr, const XColor *offColorPtr, int targetWidth,
                                            int targetHeight, int left, int top, int right, int bottom) {
    Rbc_RenderContext *ctx;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (drawable == None) || (colorPtr == NULL) || !FINITE(width) ||
        (width <= 0.0) || (targetWidth <= 0) || (targetHeight <= 0)) {
        return NULL;
    }
    ctx = Tcl_AttemptAlloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->ops = &cairoOps;
    ctx->graphPtr = graphPtr;
    ctx->foreground = *colorPtr;
    if (dashesPtr != NULL) {
        while ((ctx->nDashes < RBC_MAX_DASH_VALUES) && (dashesPtr->values[ctx->nDashes] != 0)) {
            ctx->dashes[ctx->nDashes] = dashesPtr->values[ctx->nDashes];
            ctx->nDashes++;
        }
        ctx->dashOffset = dashesPtr->offset;
    }
    if ((ctx->nDashes > 0) && (offColorPtr != NULL)) {
        if ((offColorPtr->red == colorPtr->red) && (offColorPtr->green == colorPtr->green) &&
            (offColorPtr->blue == colorPtr->blue)) {
            /* Equal dash colors form a solid stroke; avoid double coverage. */
            ctx->nDashes = 0;
            ctx->dashOffset = 0.0;
        } else {
            ctx->offColor = *offColorPtr;
            ctx->doubleDash = TRUE;
        }
    }
#ifdef WIN32
    if ((graphPtr->renderTarget != NULL) && (graphPtr->renderTarget->drawable == drawable)) {
        /* Native text/fallbacks may have touched the shared DIB since the last draw. */
        GdiFlush();
        cairo_surface_mark_dirty(graphPtr->renderTarget->image);
        ctx->surface = cairo_surface_reference(graphPtr->renderTarget->image);
    } else {
        ctx->dc = Rbc_WinAcquireDrawableDC(graphPtr->display, drawable, &ctx->dcState);
        if (ctx->dc == NULL) {
            goto fail;
        }
        /* Keep metafile/printing output on its existing native path. */
        if ((GetObjectType(ctx->dc) != OBJ_DC) && (GetObjectType(ctx->dc) != OBJ_MEMDC)) {
            goto fail;
        }
        if (GetDeviceCaps(ctx->dc, TECHNOLOGY) != DT_RASDISPLAY) {
            goto fail;
        }
        ctx->savedDC = SaveDC(ctx->dc);
        if (ctx->savedDC == 0) {
            goto fail;
        }
        GdiFlush();
        ctx->surface = cairo_win32_surface_create(ctx->dc);
    }
#else
    ctx->surface =
        cairo_xlib_surface_create(graphPtr->display, drawable, Tk_Visual(graphPtr->tkwin), targetWidth, targetHeight);
#endif
    if (cairo_surface_status(ctx->surface) != CAIRO_STATUS_SUCCESS) {
        goto fail;
    }
    ctx->cr = cairo_create(ctx->surface);
    cairo_rectangle(ctx->cr, left, top, MAX(0.0, (double)right - left + 1.0), MAX(0.0, (double)bottom - top + 1.0));
    cairo_clip(ctx->cr);
    /* RBC integer screen coordinates identify pixel centers. */
    cairo_translate(ctx->cr, 0.5, 0.5);
    SetStrokeColor(ctx->cr, colorPtr);
    cairo_set_line_width(ctx->cr, width);
    cairo_set_line_cap(ctx->cr, CAIRO_LINE_CAP_BUTT);
    cairo_set_line_join(ctx->cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_antialias(ctx->cr, GetAntialias(graphPtr->antialias));
    cairo_set_dash(ctx->cr, ctx->dashes, ctx->nDashes, ctx->dashOffset);
    if (cairo_status(ctx->cr) != CAIRO_STATUS_SUCCESS) {
        goto fail;
    }
    return ctx;

fail:
    FreeRenderContext(ctx);
    return NULL;
}

Rbc_RenderContext *Rbc_RenderBegin(Graph *graphPtr, Drawable drawable, const XColor *colorPtr, double width,
                                   const Rbc_Dashes *dashesPtr, const XColor *offColorPtr) {
    return BeginRenderTarget(graphPtr, drawable, colorPtr, width, dashesPtr, offColorPtr, graphPtr->width,
                             graphPtr->height, graphPtr->left, graphPtr->top, graphPtr->right, graphPtr->bottom);
}

/* Draw in full drawable coordinates, outside the plot-area clip. */
Rbc_RenderContext *Rbc_RenderBeginDrawable(Graph *graphPtr, Drawable drawable, int width, int height,
                                           const XColor *color, double lineWidth, const Rbc_Dashes *dashes,
                                           const XColor *offColor) {
    if ((width <= 0) || (height <= 0)) {
        return NULL;
    }
    return BeginRenderTarget(graphPtr, drawable, color, lineWidth, dashes, offColor, width, height, 0, 0, width - 1,
                             height - 1);
}

/* Each call is a separate trace; preserve joins within that trace. */
static void CairoPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Tcl_Size i;

    if (count < 2) {
        return;
    }
    cairo_move_to(ctx->cr, points[0].x, points[0].y);
    for (i = 1; i < count; i++) {
        cairo_line_to(ctx->cr, points[i].x, points[i].y);
    }
    StrokeRenderPath(ctx);
}

/* Marker options use X cap/join constants; keep them out of Cairo callers. */
static void CairoLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle) {
    cairo_set_line_cap(ctx->cr, (capStyle == CapRound)        ? CAIRO_LINE_CAP_ROUND
                                : (capStyle == CapProjecting) ? CAIRO_LINE_CAP_SQUARE
                                                              : CAIRO_LINE_CAP_BUTT);
    cairo_set_line_join(ctx->cr, (joinStyle == JoinRound)   ? CAIRO_LINE_JOIN_ROUND
                                 : (joinStyle == JoinBevel) ? CAIRO_LINE_JOIN_BEVEL
                                                            : CAIRO_LINE_JOIN_MITER);
}

/* Change only the off-dash underlay; retain the configured dash pattern. */
static void CairoDashBackground(Rbc_RenderContext *ctx, const XColor *color) {
    ctx->doubleDash = (ctx->nDashes > 0) && (color != NULL);
    if (ctx->doubleDash) {
        ctx->offColor = *color;
    }
}

/* Separate subpaths preserve strip-segment boundaries. Bound path storage. */
static void CairoSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        cairo_move_to(ctx->cr, segments[i].p.x, segments[i].p.y);
        cairo_line_to(ctx->cr, segments[i].q.x, segments[i].q.y);
        /* Preserve painter order for intersecting two-color segments. */
        if (ctx->doubleDash || ((i % 8192) == 8191)) {
            StrokeRenderPath(ctx);
        }
    }
    StrokeRenderPath(ctx);
}

/* Append independent shapes; never connect adjacent circle or segment symbols. */
static void AppendRenderSymbol(cairo_t *cr, const Rbc_RenderShape *shape, const Point2D *center) {
    int i;

    if (shape->type == RBC_RENDER_CIRCLE) {
        cairo_new_sub_path(cr);
        cairo_arc(cr, center->x, center->y, shape->radius, 0.0, 2.0 * M_PI);
        cairo_close_path(cr);
    } else if (shape->type == RBC_RENDER_SEGMENTS) {
        for (i = 0; i + 1 < shape->nPoints; i += 2) {
            cairo_move_to(cr, center->x + shape->points[i].x, center->y + shape->points[i].y);
            cairo_line_to(cr, center->x + shape->points[i + 1].x, center->y + shape->points[i + 1].y);
        }
    } else if (shape->nPoints > 0) {
        cairo_move_to(cr, center->x + shape->points[0].x, center->y + shape->points[0].y);
        for (i = 1; i < shape->nPoints; i++) {
            cairo_line_to(cr, center->x + shape->points[i].x, center->y + shape->points[i].y);
        }
        cairo_close_path(cr);
    }
}

/* Callers supply bounded batches. Symbol outlines are solid with miter joins. */
/* Tiny symbols retain their integer, one-pixel footprint in every AA mode. */
void Rbc_RenderPoints(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Tcl_Size i;

    cairo_save(ctx->cr);
    cairo_translate(ctx->cr, -0.5, -0.5);
    cairo_set_fill_rule(ctx->cr, CAIRO_FILL_RULE_WINDING);
    for (i = 0; i < count; i++) {
        cairo_rectangle(ctx->cr, (int)points[i].x, (int)points[i].y, 1, 1);
        if ((i % 8192) == 8191) {
            cairo_fill(ctx->cr);
        }
    }
    cairo_fill(ctx->cr);
    cairo_restore(ctx->cr);
}

void Rbc_RenderSymbols(Rbc_RenderContext *ctx, const Rbc_RenderShape *shape, const Point2D *centers, Tcl_Size count,
                       const XColor *fillColor, int outline) {
    Tcl_Size i;

    if ((count <= 0) || ((fillColor == NULL) && !outline)) {
        return;
    }
    cairo_save(ctx->cr);
    cairo_set_dash(ctx->cr, NULL, 0, 0.0);
    cairo_set_line_join(ctx->cr, CAIRO_LINE_JOIN_MITER);
    cairo_set_fill_rule(ctx->cr, CAIRO_FILL_RULE_WINDING);
    for (i = 0; i < count; i++) {
        AppendRenderSymbol(ctx->cr, shape, centers + i);
    }
    if ((fillColor != NULL) && (shape->type != RBC_RENDER_SEGMENTS)) {
        SetStrokeColor(ctx->cr, fillColor);
        cairo_fill_preserve(ctx->cr);
    }
    if (outline) {
        SetStrokeColor(ctx->cr, &ctx->foreground);
        cairo_stroke(ctx->cr);
    } else {
        cairo_new_path(ctx->cr);
    }
    cairo_restore(ctx->cr);
}

static uint32_t RenderAreaPixel(const XColor *color) {
    if (color == NULL) {
        return 0; /* Transparent stipple gap. */
    }
    return 0xff000000u | (((uint32_t)color->red + 128u) / 257u << 16) | (((uint32_t)color->green + 128u) / 257u << 8) |
           ((uint32_t)color->blue + 128u) / 257u;
}

/* Read the native bitmap before acquiring the destination drawing context. */
static cairo_pattern_t *CreateRenderStipple(Graph *graphPtr, Pixmap stipple, const XColor *foreground,
                                            const XColor *background) {
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;
    XImage *image;
    unsigned char *data;
    uint32_t fg = RenderAreaPixel(foreground);
    uint32_t bg = RenderAreaPixel(background);
    int width, height, stride, x, y;

    Tk_SizeOfBitmap(graphPtr->display, stipple, &width, &height);
    if ((width <= 0) || (height <= 0)) {
        return NULL;
    }
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    /* Read stipple bits directly; Tk/Windows ZPixmap inverts monochrome data. */
    image = XGetImage(graphPtr->display, stipple, 0, 0, width, height, 1, XYPixmap);
    if (image == NULL) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    cairo_surface_flush(surface);
    data = cairo_image_surface_get_data(surface);
    stride = cairo_image_surface_get_stride(surface);
    for (y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * stride);
        for (x = 0; x < width; x++) {
            row[x] = XGetPixel(image, x, y) ? fg : bg;
        }
    }
    XDestroyImage(image);
    cairo_surface_mark_dirty(surface);
    pattern = cairo_pattern_create_for_surface(surface);
    cairo_surface_destroy(surface);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
    if (cairo_pattern_status(pattern) != CAIRO_STATUS_SUCCESS) {
        cairo_pattern_destroy(pattern);
        return NULL;
    }
    return pattern;
}

/* Bar swatches restart the stipple at their top-left corner, as Tk does. */
int Rbc_RenderLegendBar(Graph *graphPtr, Drawable drawable, int width, int height, const Rbc_RenderRectangle *r,
                        const XColor *foreground, const XColor *background, Pixmap stipple) {
    Rbc_RenderContext *ctx;
    cairo_pattern_t *pattern = NULL;
    cairo_matrix_t matrix;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (foreground == NULL)) {
        return FALSE;
    }
    if ((r->width <= 0) || (r->height <= 0)) {
        return TRUE;
    }
    if (stipple != None) {
        pattern = CreateRenderStipple(graphPtr, stipple, foreground, background);
        if (pattern == NULL) {
            return FALSE;
        }
        cairo_matrix_init_translate(&matrix, -(double)r->x, -(double)r->y);
        cairo_pattern_set_matrix(pattern, &matrix);
    }
    ctx = Rbc_RenderBeginDrawable(graphPtr, drawable, width, height, foreground, 1.0, NULL, NULL);
    if (ctx == NULL) {
        if (pattern != NULL) {
            cairo_pattern_destroy(pattern);
        }
        return FALSE;
    }
    cairo_translate(ctx->cr, -0.5, -0.5);
    if (pattern != NULL) {
        cairo_set_source(ctx->cr, pattern);
    }
    cairo_rectangle(ctx->cr, r->x, r->y, r->width, r->height);
    cairo_fill(ctx->cr);
    if (pattern != NULL) {
        cairo_pattern_destroy(pattern);
    }
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/* Integer bar edges stay sharp; bound path storage independently of bar count. */
int Rbc_RenderRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *rectangles, Tcl_Size count,
                         const XColor *foreground, const XColor *background, Pixmap stipple) {
    Rbc_RenderFillStyle style = {foreground, background, stipple, 1.0, FALSE};
    Rbc_RenderContext *ctx;

    if (count <= 0) {
        return FALSE;
    }
    ctx = Rbc_RenderBeginFill(graphPtr, drawable, &style);
    if (ctx == NULL) {
        return FALSE;
    }
    Rbc_RenderFillRectangles(ctx, rectangles, count);
    Rbc_RenderEnd(ctx);
    return TRUE;
}

static void CairoFillRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rectangles, Tcl_Size count) {
    Tcl_Size i;

    cairo_save(ctx->cr);
    cairo_translate(ctx->cr, -0.5, -0.5);
    cairo_set_fill_rule(ctx->cr, CAIRO_FILL_RULE_WINDING);
    if (ctx->bitmapPattern != NULL) {
        cairo_set_source(ctx->cr, ctx->bitmapPattern);
    }
    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *r = rectangles + i;
        if ((r->width > 0) && (r->height > 0)) {
            cairo_rectangle(ctx->cr, r->x, r->y, r->width, r->height);
        }
        if ((i + 1) % 8192 == 0) {
            cairo_fill(ctx->cr);
        }
    }
    cairo_fill(ctx->cr);
    cairo_restore(ctx->cr);
}

/* Area vertices are boundaries, not the pixel centers used by strokes. */
static void FillRenderArea(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count, cairo_pattern_t *pattern) {
    Tcl_Size i;

    cairo_translate(ctx->cr, -0.5, -0.5);
    cairo_set_fill_rule(ctx->cr, CAIRO_FILL_RULE_EVEN_ODD);
    if (pattern != NULL) {
        cairo_set_source(ctx->cr, pattern);
    }
    cairo_move_to(ctx->cr, points[0].x, points[0].y);
    for (i = 1; i < count; i++) {
        cairo_line_to(ctx->cr, points[i].x, points[i].y);
    }
    cairo_close_path(ctx->cr);
    cairo_fill(ctx->cr);
}

/* Mapped bitmaps are raw pixmaps, not entries in Tk's named bitmap cache. */
static cairo_pattern_t *CreateRenderBitmap(Graph *graphPtr, Pixmap bitmap, Pixmap mask, int width, int height,
                                           const XColor *foreground, const XColor *background) {
    XImage *bits, *maskBits = NULL;
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;
    unsigned char *data;
    int x, y, stride;
    uint32_t fg = RenderAreaPixel(foreground);
    uint32_t bg = RenderAreaPixel(background);

    bits = XGetImage(graphPtr->display, bitmap, 0, 0, width, height, 1, XYPixmap);
    if (bits == NULL) {
        return NULL;
    }
    if (mask == bitmap) {
        maskBits = bits;
    } else if (mask != None) {
        maskBits = XGetImage(graphPtr->display, mask, 0, 0, width, height, 1, XYPixmap);
        if (maskBits == NULL) {
            XDestroyImage(bits);
            return NULL;
        }
    }
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        if ((maskBits != NULL) && (maskBits != bits)) {
            XDestroyImage(maskBits);
        }
        XDestroyImage(bits);
        return NULL;
    }
    cairo_surface_flush(surface);
    data = cairo_image_surface_get_data(surface);
    stride = cairo_image_surface_get_stride(surface);
    for (y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * stride);
        for (x = 0; x < width; x++) {
            row[x] = ((maskBits != NULL) && !XGetPixel(maskBits, x, y)) ? 0 : (XGetPixel(bits, x, y) ? fg : bg);
        }
    }
    if ((maskBits != NULL) && (maskBits != bits)) {
        XDestroyImage(maskBits);
    }
    XDestroyImage(bits);
    cairo_surface_mark_dirty(surface);
    pattern = cairo_pattern_create_for_surface(surface);
    cairo_surface_destroy(surface);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_NONE);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
    if (cairo_pattern_status(pattern) != CAIRO_STATUS_SUCCESS) {
        cairo_pattern_destroy(pattern);
        return NULL;
    }
    return pattern;
}

/* Convert once per pen pass; zero target dimensions select the plot clip. */
Rbc_RenderContext *Rbc_RenderBeginBitmapSymbols(Graph *graphPtr, Drawable drawable, Pixmap bitmap, Pixmap mask,
                                                int width, int height, const XColor *foreground,
                                                const XColor *background, int targetWidth, int targetHeight) {
    cairo_pattern_t *pattern;
    Rbc_RenderContext *ctx;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (bitmap == None) || (foreground == NULL) || (width <= 0) ||
        (height <= 0)) {
        return NULL;
    }
    pattern = CreateRenderBitmap(graphPtr, bitmap, mask, width, height, foreground, background);
    if (pattern == NULL) {
        return NULL;
    }
    ctx = (targetWidth > 0)
              ? Rbc_RenderBeginDrawable(graphPtr, drawable, targetWidth, targetHeight, foreground, 1.0, NULL, NULL)
              : Rbc_RenderBegin(graphPtr, drawable, foreground, 1.0, NULL, NULL);
    if (ctx == NULL) {
        cairo_pattern_destroy(pattern);
        return NULL;
    }
    ctx->bitmapPattern = pattern;
    ctx->bitmapWidth = width;
    ctx->bitmapHeight = height;
    cairo_translate(ctx->cr, -0.5, -0.5);
    return ctx;
}

/* Paint in source order without repeating or resampling bitmap pixels. */
void Rbc_RenderBitmapSymbols(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count) {
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        int x = (int)centers[i].x - ctx->bitmapWidth / 2;
        int y = (int)centers[i].y - ctx->bitmapHeight / 2;
        cairo_save(ctx->cr);
        cairo_translate(ctx->cr, x, y);
        cairo_set_source(ctx->cr, ctx->bitmapPattern);
        cairo_rectangle(ctx->cr, 0, 0, ctx->bitmapWidth, ctx->bitmapHeight);
        cairo_fill(ctx->cr);
        cairo_restore(ctx->cr);
    }
}

/* Prepare all bitmap resources before drawing the optional rotated background. */
int Rbc_RenderBitmap(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *r, Pixmap bitmap, Pixmap mask,
                     const XColor *foreground, const XColor *background, const Point2D *polygon, Tcl_Size nPoints) {
    cairo_pattern_t *pattern;
    cairo_matrix_t matrix;
    Rbc_RenderContext *ctx;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (bitmap == None) || (foreground == NULL)) {
        return FALSE;
    }
    if ((r->width <= 0) || (r->height <= 0)) {
        return TRUE;
    }
    pattern =
        CreateRenderBitmap(graphPtr, bitmap, mask, r->width, r->height, foreground, (nPoints >= 3) ? NULL : background);
    if (pattern == NULL) {
        return FALSE;
    }
    ctx = Rbc_RenderBegin(graphPtr, drawable, foreground, 1.0, NULL, NULL);
    if (ctx == NULL) {
        cairo_pattern_destroy(pattern);
        return FALSE;
    }
    if ((polygon != NULL) && (nPoints >= 3) && (background != NULL)) {
        SetStrokeColor(ctx->cr, background);
        FillRenderArea(ctx, polygon, nPoints, NULL);
    } else {
        cairo_translate(ctx->cr, -0.5, -0.5);
    }
    cairo_matrix_init_translate(&matrix, -(double)r->x, -(double)r->y);
    cairo_pattern_set_matrix(pattern, &matrix);
    cairo_set_source(ctx->cr, pattern);
    cairo_rectangle(ctx->cr, r->x, r->y, r->width, r->height);
    cairo_fill(ctx->cr);
    cairo_pattern_destroy(pattern);
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/* Fill one mapped polygon with the native even-odd rule and widget pattern origin. */
Rbc_RenderContext *Rbc_RenderBeginFill(Graph *graphPtr, Drawable drawable, const Rbc_RenderFillStyle *style) {
    Rbc_RenderContext *ctx;
    cairo_pattern_t *pattern = NULL;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (style->foreground == NULL) || style->backgroundOnly) {
        return NULL;
    }
    /* Read native stipple bits before acquiring the destination context. */
    if (style->stipple != None) {
        pattern = CreateRenderStipple(graphPtr, style->stipple, style->foreground, style->background);
        if (pattern == NULL) {
            return NULL;
        }
    }
    ctx = Rbc_RenderBegin(graphPtr, drawable, style->foreground, 1.0, NULL, NULL);
    if (ctx == NULL) {
        if (pattern != NULL) {
            cairo_pattern_destroy(pattern);
        }
        return NULL;
    }
    ctx->fillStyle = *style;
    ctx->bitmapPattern = pattern;
    return ctx;
}

static void CairoFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    const Rbc_RenderFillStyle *style = &ctx->fillStyle;
    const XColor *foreground = style->foreground;

    cairo_save(ctx->cr);
    if ((style->stipple == None) && (style->opacity != 1.0)) {
        cairo_set_source_rgba(ctx->cr, foreground->red / 65535.0, foreground->green / 65535.0,
                              foreground->blue / 65535.0, style->opacity);
    }
    FillRenderArea(ctx, points, count, ctx->bitmapPattern);
    cairo_restore(ctx->cr);
}

int Rbc_RenderAreaOpacity(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                          const XColor *foreground, const XColor *background, Pixmap stipple, double opacity) {
    Rbc_RenderFillStyle style = {foreground, background, stipple, opacity, FALSE};
    Rbc_RenderContext *ctx;

    if (count < 3) {
        return FALSE;
    }
    ctx = Rbc_RenderBeginFill(graphPtr, drawable, &style);
    if (ctx == NULL) {
        return FALSE;
    }
    Rbc_RenderFillPolygon(ctx, points, count);
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/* Copy straight-alpha Tk pixels into an owned, premultiplied Cairo tile. */
static cairo_pattern_t *CreateRenderPhoto(const Tk_PhotoImageBlock *block) {
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;
    unsigned char *data;
    int x, y, stride, hasAlpha;

    if ((block->pixelPtr == NULL) || (block->pixelSize <= 0)) {
        return NULL;
    }
    for (x = 0; x < 3; x++) {
        if ((block->offset[x] < 0) || (block->offset[x] >= block->pixelSize)) {
            return NULL;
        }
    }
    hasAlpha = (block->offset[3] >= 0) && (block->offset[3] < block->pixelSize) &&
               (block->offset[3] != block->offset[0]) && (block->offset[3] != block->offset[1]) &&
               (block->offset[3] != block->offset[2]);
    surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, block->width, block->height);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }
    cairo_surface_flush(surface);
    data = cairo_image_surface_get_data(surface);
    stride = cairo_image_surface_get_stride(surface);
    for (y = 0; y < block->height; y++) {
        const unsigned char *src = block->pixelPtr + (ptrdiff_t)y * block->pitch;
        uint32_t *dst = (uint32_t *)(data + (size_t)y * stride);
        for (x = 0; x < block->width; x++, src += block->pixelSize) {
            uint32_t a = hasAlpha ? src[block->offset[3]] : 255;
            uint32_t r = (src[block->offset[0]] * a + 127) / 255;
            uint32_t g = (src[block->offset[1]] * a + 127) / 255;
            uint32_t b = (src[block->offset[2]] * a + 127) / 255;
            dst[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    cairo_surface_mark_dirty(surface);
    pattern = cairo_pattern_create_for_surface(surface);
    cairo_surface_destroy(surface);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    cairo_pattern_set_filter(pattern, CAIRO_FILTER_NEAREST);
    if (cairo_pattern_status(pattern) != CAIRO_STATUS_SUCCESS) {
        cairo_pattern_destroy(pattern);
        return NULL;
    }
    return pattern;
}

/* Repeat photo tiles from the toplevel origin, as Rbc_SetTileOrigin does. */
int Rbc_RenderTileArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                       const Tk_PhotoImageBlock *block) {
    Rbc_RenderContext *ctx;
    cairo_pattern_t *pattern;
    cairo_matrix_t matrix;
    Tk_Window tkwin;
    XColor unusedColor = {0};
    double x = 0.0, y = 0.0;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (count < 3)) {
        return FALSE;
    }
    if ((block->width <= 0) || (block->height <= 0)) {
        return TRUE; /* Empty or deleted photo; preserve tile precedence. */
    }
    pattern = CreateRenderPhoto(block);
    if (pattern == NULL) {
        return FALSE;
    }
    for (tkwin = graphPtr->tkwin; !Tk_IsTopLevel(tkwin); tkwin = Tk_Parent(tkwin)) {
        x += Tk_X(tkwin) + Tk_Changes(tkwin)->border_width;
        y += Tk_Y(tkwin) + Tk_Changes(tkwin)->border_width;
    }
    cairo_matrix_init_translate(&matrix, x, y);
    cairo_pattern_set_matrix(pattern, &matrix);
    ctx = Rbc_RenderBegin(graphPtr, drawable, &unusedColor, 1.0, NULL, NULL);
    if (ctx == NULL) {
        cairo_pattern_destroy(pattern);
        return FALSE;
    }
    FillRenderArea(ctx, points, count, pattern);
    cairo_pattern_destroy(pattern);
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/* Paint a mapped photo once, at the same integer origin as Tk. */
int Rbc_RenderPhoto(Graph *graphPtr, Drawable drawable, const Tk_PhotoImageBlock *block, int x, int y) {
    Rbc_RenderContext *ctx;
    cairo_pattern_t *pattern;
    cairo_matrix_t matrix;
    XColor unusedColor = {0};

    if (graphPtr->renderer != RBC_RENDERER_CAIRO) {
        return FALSE;
    }
    if ((block->width <= 0) || (block->height <= 0)) {
        return TRUE;
    }
    pattern = CreateRenderPhoto(block);
    if (pattern == NULL) {
        return FALSE;
    }
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_NONE);
    cairo_matrix_init_translate(&matrix, -(double)x, -(double)y);
    cairo_pattern_set_matrix(pattern, &matrix);
    ctx = Rbc_RenderBegin(graphPtr, drawable, &unusedColor, 1.0, NULL, NULL);
    if (ctx == NULL) {
        cairo_pattern_destroy(pattern);
        return FALSE;
    }
    /* Photos already contain pixel coverage; do not offset or resample it. */
    cairo_translate(ctx->cr, -0.5, -0.5);
    cairo_set_source(ctx->cr, pattern);
    cairo_rectangle(ctx->cr, x, y, block->width, block->height);
    cairo_fill(ctx->cr);
    cairo_pattern_destroy(pattern);
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/* Flush before any native drawing resumes on the same drawable. */
static void CairoEnd(Rbc_RenderContext *ctx) {
    cairo_status_t status;
    Tcl_Interp *interp = ctx->graphPtr->interp;

    cairo_surface_flush(ctx->surface);
    status = cairo_status(ctx->cr);
    if (status == CAIRO_STATUS_SUCCESS) {
        status = cairo_surface_status(ctx->surface);
    }
    FreeRenderContext(ctx);
    if (status != CAIRO_STATUS_SUCCESS) {
        Tcl_InterpState saved = Tcl_SaveInterpState(interp, TCL_OK);
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("Cairo rendering failed: %s", cairo_status_to_string(status)));
        Tcl_BackgroundException(interp, TCL_ERROR);
        Tcl_RestoreInterpState(interp, saved);
    }
}
#else
Rbc_RenderContext *Rbc_RenderBeginFill(Graph *graphPtr, Drawable drawable, const Rbc_RenderFillStyle *style) {
    (void)graphPtr;
    (void)drawable;
    (void)style;
    return NULL;
}
Rbc_RenderContext *Rbc_RenderBeginDrawable(Graph *graphPtr, Drawable drawable, int width, int height,
                                           const XColor *color, double lineWidth, const Rbc_Dashes *dashes,
                                           const XColor *offColor) {
    (void)graphPtr;
    (void)drawable;
    (void)width;
    (void)height;
    (void)color;
    (void)lineWidth;
    (void)dashes;
    (void)offColor;
    return NULL;
}
int Rbc_RenderLegendBar(Graph *graphPtr, Drawable drawable, int width, int height, const Rbc_RenderRectangle *r,
                        const XColor *foreground, const XColor *background, Pixmap stipple) {
    (void)graphPtr;
    (void)drawable;
    (void)width;
    (void)height;
    (void)r;
    (void)foreground;
    (void)background;
    (void)stipple;
    return FALSE;
}
Rbc_RenderTarget *Rbc_RenderBeginMarkerPass(Graph *graphPtr, Drawable *drawablePtr) {
    (void)graphPtr;
    (void)drawablePtr;
    return NULL;
}
void Rbc_RenderEndMarkerPass(Rbc_RenderTarget *target) { (void)target; }
Rbc_RenderContext *Rbc_RenderBegin(Graph *graphPtr, Drawable drawable, const XColor *colorPtr, double width,
                                   const Rbc_Dashes *dashesPtr, const XColor *offColorPtr) {
    (void)graphPtr;
    (void)drawable;
    (void)colorPtr;
    (void)width;
    (void)dashesPtr;
    (void)offColorPtr;
    return NULL;
}
int Rbc_RenderGCForeground(Graph *graphPtr, GC gc, XColor *color) {
    (void)graphPtr;
    (void)gc;
    (void)color;
    return FALSE;
}
void Rbc_RenderPoints(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    (void)ctx;
    (void)points;
    (void)count;
}
void Rbc_RenderSymbols(Rbc_RenderContext *ctx, const Rbc_RenderShape *shape, const Point2D *centers, Tcl_Size count,
                       const XColor *fillColor, int outline) {
    (void)ctx;
    (void)shape;
    (void)centers;
    (void)count;
    (void)fillColor;
    (void)outline;
}
int Rbc_RenderRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *rectangles, Tcl_Size count,
                         const XColor *foreground, const XColor *background, Pixmap stipple) {
    (void)graphPtr;
    (void)drawable;
    (void)rectangles;
    (void)count;
    (void)foreground;
    (void)background;
    (void)stipple;
    return FALSE;
}
Rbc_RenderContext *Rbc_RenderBeginBitmapSymbols(Graph *graphPtr, Drawable drawable, Pixmap bitmap, Pixmap mask,
                                                int width, int height, const XColor *foreground,
                                                const XColor *background, int targetWidth, int targetHeight) {
    (void)graphPtr;
    (void)drawable;
    (void)bitmap;
    (void)mask;
    (void)width;
    (void)height;
    (void)foreground;
    (void)background;
    (void)targetWidth;
    (void)targetHeight;
    return NULL;
}
void Rbc_RenderBitmapSymbols(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count) {
    (void)ctx;
    (void)centers;
    (void)count;
}
int Rbc_RenderBitmap(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *r, Pixmap bitmap, Pixmap mask,
                     const XColor *foreground, const XColor *background, const Point2D *polygon, Tcl_Size nPoints) {
    (void)graphPtr;
    (void)drawable;
    (void)r;
    (void)bitmap;
    (void)mask;
    (void)foreground;
    (void)background;
    (void)polygon;
    (void)nPoints;
    return FALSE;
}
int Rbc_RenderPhoto(Graph *graphPtr, Drawable drawable, const Tk_PhotoImageBlock *block, int x, int y) {
    (void)graphPtr;
    (void)drawable;
    (void)block;
    (void)x;
    (void)y;
    return FALSE;
}
int Rbc_RenderAreaOpacity(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                          const XColor *foreground, const XColor *background, Pixmap stipple, double opacity) {
    (void)graphPtr;
    (void)drawable;
    (void)points;
    (void)count;
    (void)foreground;
    (void)background;
    (void)stipple;
    (void)opacity;
    return FALSE;
}
int Rbc_RenderTileArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                       const Tk_PhotoImageBlock *block) {
    (void)graphPtr;
    (void)drawable;
    (void)points;
    (void)count;
    (void)block;
    return FALSE;
}
#endif

/* Existing area clients retain opaque fills. */
int Rbc_RenderArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count, const XColor *foreground,
                   const XColor *background, Pixmap stipple) {
    return Rbc_RenderAreaOpacity(graphPtr, drawable, points, count, foreground, background, stipple, 1.0);
}

/* The existing PostScript emitter remains responsible for PS syntax and maps. */
static void PostScriptPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Tcl_Size i;
    int components;

    if (count < 2) {
        return;
    }
    Rbc_FormatToPostScript(ctx->psToken, " newpath %g %g moveto\n", points[0].x, points[0].y);
    components = 1;
    for (i = 1; i < count - 1; i++) {
        Rbc_FormatToPostScript(ctx->psToken, " %g %g lineto\n", points[i].x, points[i].y);
        components++;
        /* Keep the legacy level-1 path limit and restart at the shared vertex. */
        if (components >= 1500) {
            Rbc_FormatToPostScript(ctx->psToken, "DashesProc stroke\n newpath %g %g moveto\n", points[i].x,
                                   points[i].y);
            components = 1;
        }
    }
    Rbc_FormatToPostScript(ctx->psToken, " %g %g lineto\n", points[i].x, points[i].y);
    Rbc_AppendToPostScript(ctx->psToken, "DashesProc stroke\n", (char *)NULL);
}

static void PostScriptSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    if (count > 0) {
        Rbc_2DSegmentsToPostScript(ctx->psToken, (Segment2D *)segments, count);
    }
}

static void PostScriptLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle) {
    Rbc_CapStyleToPostScript(ctx->psToken, capStyle);
    Rbc_JoinStyleToPostScript(ctx->psToken, joinStyle);
}

static void PostScriptDashBackground(Rbc_RenderContext *ctx, const XColor *color) {
    if (ctx->postScriptDashed && (color != NULL)) {
        Rbc_AppendToPostScript(ctx->psToken, "/DashesProc {\n  gsave\n    ", (char *)NULL);
        Rbc_BackgroundToPostScript(ctx->psToken, (XColor *)color);
        Rbc_AppendToPostScript(ctx->psToken, "    ", (char *)NULL);
        Rbc_LineDashesToPostScript(ctx->psToken, NULL);
        Rbc_AppendToPostScript(ctx->psToken, "stroke\n  grestore\n} def\n", (char *)NULL);
    } else {
        Rbc_AppendToPostScript(ctx->psToken, "/DashesProc {} def\n", (char *)NULL);
    }
}

static void PostScriptFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    const Rbc_RenderFillStyle *style = &ctx->fillStyle;

    Rbc_PathToPostScript(ctx->psToken, (Point2D *)points, count);
    Rbc_AppendToPostScript(ctx->psToken, "closepath\n", (char *)NULL);
    if (style->background != NULL) {
        Rbc_BackgroundToPostScript(ctx->psToken, (XColor *)style->background);
        Rbc_AppendToPostScript(ctx->psToken, "Fill\n", (char *)NULL);
    }
    Rbc_ForegroundToPostScript(ctx->psToken, (XColor *)style->foreground);
    if (style->backgroundOnly) {
        return;
    }
    if (style->stipple != None) {
        Rbc_StippleToPostScript(ctx->psToken, ctx->graphPtr->display, style->stipple);
    } else {
        Rbc_AppendToPostScript(ctx->psToken, "Fill\n", (char *)NULL);
    }
}

static void PostScriptFillRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rectangles, Tcl_Size count) {
    const Rbc_RenderFillStyle *style = &ctx->fillStyle;
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *r = rectangles + i;
        if (style->stipple != None) {
            Rbc_RegionToPostScript(ctx->psToken, (double)r->x, (double)r->y, r->width, r->height);
            if (style->background != NULL) {
                Rbc_BackgroundToPostScript(ctx->psToken, (XColor *)style->background);
                Rbc_AppendToPostScript(ctx->psToken, "Fill\n", (char *)NULL);
            }
            Rbc_ForegroundToPostScript(ctx->psToken,
                                       (XColor *)((style->foreground != NULL) ? style->foreground : style->background));
            Rbc_StippleToPostScript(ctx->psToken, ctx->graphPtr->display, style->stipple);
        } else if (style->foreground != NULL) {
            Rbc_ForegroundToPostScript(ctx->psToken, (XColor *)style->foreground);
            Rbc_RectangleToPostScript(ctx->psToken, (double)r->x, (double)r->y, r->width, r->height);
        }
    }
}

static void PostScriptSymbolPoints(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count) {
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        Rbc_FormatToPostScript(ctx->psToken, "%g %g %g %s\n", centers[i].x, centers[i].y, ctx->symbolSize,
                               ctx->symbolMacro);
    }
}

static void PostScriptText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y) {
    Rbc_TextToPostScript(ctx->psToken, string, style, x, y);
}

/* Legacy PostScript silently omits non-photo image markers. */
static void PostScriptTkImage(Rbc_RenderContext *ctx, Tk_Image image, double x, double y) {
    (void)ctx;
    (void)image;
    (void)x;
    (void)y;
}

static void PostScriptPhoto(Rbc_RenderContext *ctx, Tk_PhotoHandle photo, double x, double y) {
    if (photo != NULL) {
        Rbc_PhotoToPostScript(ctx->psToken, photo, x, y);
    }
}

static void PostScriptWindow(Rbc_RenderContext *ctx, Tk_Window tkwin, double x, double y) {
    Rbc_WindowToPostScript(ctx->psToken, tkwin, x, y);
}

static void PostScriptBackgroundPolygon(Rbc_RenderContext *ctx, const XColor *color, const Point2D *points,
                                        Tcl_Size count) {
    Rbc_BackgroundToPostScript(ctx->psToken, (XColor *)color);
    Rbc_PolygonToPostScript(ctx->psToken, (Point2D *)points, count);
}

static void PostScriptBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y, int width, int height,
                             int borderWidth, int relief, int fill) {
    if (fill) {
        Rbc_Fill3DRectangleToPostScript(ctx->psToken, border, x, y, width, height, borderWidth, relief);
    } else {
        Rbc_Draw3DRectangleToPostScript(ctx->psToken, border, x, y, width, height, borderWidth, relief);
    }
}

static void PostScriptClearRectangle(Rbc_RenderContext *ctx, double x, double y, int width, int height) {
    Rbc_ClearBackgroundToPostScript(ctx->psToken);
    Rbc_RectangleToPostScript(ctx->psToken, x, y, width, height);
}

static void PostScriptBackgroundRectangles(Rbc_RenderContext *ctx, const XColor *color,
                                           const Rbc_RenderRectangle *rectangles, Tcl_Size count) {
    Tcl_Size i;

    if (color != NULL) {
        Rbc_BackgroundToPostScript(ctx->psToken, (XColor *)color);
    } else {
        Rbc_ClearBackgroundToPostScript(ctx->psToken);
    }
    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *rect = rectangles + i;
        Rbc_RectangleToPostScript(ctx->psToken, rect->x, rect->y, rect->width, rect->height);
    }
}

static void PostScriptPlotBegin(Rbc_RenderContext *ctx, Tk_Font font, double x, double y, int width, int height,
                                const XColor *background) {
    Rbc_FontToPostScript(ctx->psToken, font);
    Rbc_RegionToPostScript(ctx->psToken, x, y, width, height);
    if (background != NULL) {
        Rbc_BackgroundToPostScript(ctx->psToken, (XColor *)background);
    } else {
        Rbc_ClearBackgroundToPostScript(ctx->psToken);
    }
    Rbc_AppendToPostScript(ctx->psToken, "Fill\n", "gsave clip\n\n", (char *)NULL);
}

static void PostScriptPlotEnd(Rbc_RenderContext *ctx) {
    Rbc_AppendToPostScript(ctx->psToken, "\n", "% Unset clipping\n", "grestore\n\n", (char *)NULL);
}

static void PostScriptBitmapMask(Rbc_RenderContext *ctx, Display *display, Pixmap bitmap, double x, double y, int width,
                                 int height, const XColor *color, int background) {
    if ((bitmap == None) || (width < 1) || (height < 1)) {
        return;
    }
    if (background) {
        Rbc_BackgroundToPostScript(ctx->psToken, (XColor *)color);
    } else {
        Rbc_ForegroundToPostScript(ctx->psToken, (XColor *)color);
    }
    Rbc_FormatToPostScript(ctx->psToken,
                           " gsave\n"
                           " %g %g translate\n"
                           " %d %d scale\n",
                           x, y + height, width, -height);
    Rbc_FormatToPostScript(ctx->psToken, " %d %d true [%d 0 0 %d 0 %d] {", width, height, width, -height, height);
    Rbc_BitmapDataToPostScript(ctx->psToken, display, bitmap, width, height);
    Rbc_AppendToPostScript(ctx->psToken, " } imagemask\n", " grestore\n", (char *)NULL);
}

static const Rbc_RenderOutputOps postScriptOutputOps = {PostScriptText,
                                                        PostScriptPhoto,
                                                        PostScriptWindow,
                                                        PostScriptBackgroundPolygon,
                                                        PostScriptBorder,
                                                        PostScriptClearRectangle,
                                                        PostScriptBackgroundRectangles,
                                                        PostScriptPlotBegin,
                                                        PostScriptPlotEnd,
                                                        PostScriptBitmapMask,
                                                        PostScriptTkImage};

static void PostScriptEnd(Rbc_RenderContext *ctx) {
    /* The export owns its token and clipping/page state. */
    ckfree(ctx);
}

static const Rbc_RenderOps postScriptOps = {PostScriptPolyline,       PostScriptSegments,    PostScriptLineStyle,
                                            PostScriptDashBackground, PostScriptFillPolygon, PostScriptFillRectangles,
                                            PostScriptSymbolPoints,   &postScriptOutputOps,  PostScriptEnd};

Rbc_RenderContext *Rbc_RenderBeginExport(Rbc_ExportContext *exportPtr, const XColor *color, int lineWidth,
                                         const Rbc_Dashes *dashes, int capStyle, int joinStyle) {
    Rbc_RenderContext *ctx = (Rbc_RenderContext *)ckalloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    ctx->ops = &postScriptOps;
    ctx->exportPtr = exportPtr;
    ctx->psToken = (PsToken)exportPtr->backendData;
    if (exportPtr->backend == RBC_EXPORT_SVG) {
        ctx->ops = &svgOps;
        ctx->svgHasColor = color != NULL;
        if (color != NULL) {
            ctx->svgColor = *color;
        }
        ctx->svgWidth = MAX(1, lineWidth);
        ctx->svgCap = capStyle;
        ctx->svgJoin = joinStyle;
        if (dashes != NULL) {
            ctx->svgDashes = *dashes;
        }
        return ctx;
    }
    ctx->postScriptDashed = (dashes != NULL) && (dashes->values[0] != 0);
    Rbc_LineAttributesToPostScript(ctx->psToken, (XColor *)color, lineWidth, (Rbc_Dashes *)dashes, capStyle, joinStyle);
    return ctx;
}

Rbc_RenderContext *Rbc_RenderBeginExportFill(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                             const Rbc_RenderFillStyle *style) {
    Rbc_RenderContext *ctx = (Rbc_RenderContext *)ckalloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    ctx->ops = &postScriptOps;
    ctx->exportPtr = exportPtr;
    ctx->psToken = (PsToken)exportPtr->backendData;
    ctx->graphPtr = graphPtr;
    ctx->fillStyle = *style;
    if (exportPtr->backend == RBC_EXPORT_SVG) {
        ctx->ops = &svgOps;
    }
    return ctx;
}

/* Tiles are borrowed for the lifetime of the fill context. The PS backend
 * retains the configured background-only fallback. */
void Rbc_RenderSetFillTile(Rbc_RenderContext *ctx, Rbc_Tile tile) {
    if (ctx->exportPtr != NULL && ctx->exportPtr->backend == RBC_EXPORT_SVG) {
        ctx->svgTile = tile;
        ctx->svgPatternId = 0;
    }
}

static int PostScriptSymbolRound(double value) { return (int)(value + ((value < 0.0) ? -0.5 : 0.5)); }

/* Retain the legacy prolog shapes and size corrections, including bitmap masks. */
Rbc_RenderContext *Rbc_RenderBeginExportSymbol(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                               const Rbc_RenderSymbolStyle *style) {
    static const char *macros[] = {"Sq", "Ci", "Di", "Pl", "Cr", "Sp", "Sc", "Tr", "Ar", "Bm"};
    Rbc_RenderContext *ctx = (Rbc_RenderContext *)ckalloc(sizeof(*ctx));
    XColor *outlineColor = (XColor *)style->outlineColor;
    XColor *fillColor = (XColor *)style->fillColor;
    memset(ctx, 0, sizeof(*ctx));
    ctx->ops = &postScriptOps;
    ctx->exportPtr = exportPtr;
    ctx->psToken = (PsToken)exportPtr->backendData;
    if (exportPtr->backend == RBC_EXPORT_SVG) {
        ctx->ops = &svgOps;
        ctx->svgSymbol = *style;
        ctx->graphPtr = graphPtr;
        return ctx;
    }
    ctx->symbolMacro = macros[style->type];
    ctx->symbolSize = (double)style->size;
    switch (style->type) {
    case RBC_RENDER_SYMBOL_SQUARE:
    case RBC_RENDER_SYMBOL_CROSS:
    case RBC_RENDER_SYMBOL_PLUS:
    case RBC_RENDER_SYMBOL_SCROSS:
    case RBC_RENDER_SYMBOL_SPLUS:
        ctx->symbolSize = (double)PostScriptSymbolRound(style->size * 0.886226925452758);
        break;
    case RBC_RENDER_SYMBOL_TRIANGLE:
    case RBC_RENDER_SYMBOL_ARROW:
        ctx->symbolSize = (double)PostScriptSymbolRound(style->size * 0.7);
        break;
    case RBC_RENDER_SYMBOL_DIAMOND:
        ctx->symbolSize = (double)PostScriptSymbolRound(style->size * M_SQRT1_2);
        break;
    default:
        break;
    }
    Rbc_LineWidthToPostScript(ctx->psToken, style->outlineWidth);
    Rbc_LineDashesToPostScript(ctx->psToken, (Rbc_Dashes *)NULL);
    Rbc_AppendToPostScript(ctx->psToken, "\n/DrawSymbolProc {\n", (char *)NULL);
    if (style->type == RBC_RENDER_SYMBOL_BITMAP) {
        int width, height;
        double sx, sy, scale;

        Tk_SizeOfBitmap(graphPtr->display, style->bitmap, &width, &height);
        sx = (double)style->size / (double)width;
        sy = (double)style->size / (double)height;
        scale = MIN(sx, sy);
        if ((style->mask != None) && (fillColor != NULL)) {
            Rbc_AppendToPostScript(ctx->psToken, "\n  % Bitmap mask is \"",
                                   Tk_NameOfBitmap(graphPtr->display, style->mask), "\"\n\n  ", (char *)NULL);
            Rbc_BackgroundToPostScript(ctx->psToken, fillColor);
            Rbc_BitmapToPostScript(ctx->psToken, graphPtr->display, style->mask, scale, scale);
        }
        Rbc_AppendToPostScript(ctx->psToken, "\n  % Bitmap symbol is \"",
                               Tk_NameOfBitmap(graphPtr->display, style->bitmap), "\"\n\n  ", (char *)NULL);
        Rbc_ForegroundToPostScript(ctx->psToken, outlineColor);
        Rbc_BitmapToPostScript(ctx->psToken, graphPtr->display, style->bitmap, scale, scale);
    } else {
        if (fillColor != NULL) {
            Rbc_AppendToPostScript(ctx->psToken, "  ", (char *)NULL);
            Rbc_BackgroundToPostScript(ctx->psToken, fillColor);
            Rbc_AppendToPostScript(ctx->psToken, "  Fill\n", (char *)NULL);
        }
        if ((outlineColor != NULL) && (style->outlineWidth > 0)) {
            Rbc_AppendToPostScript(ctx->psToken, "  ", (char *)NULL);
            Rbc_ForegroundToPostScript(ctx->psToken, outlineColor);
            Rbc_AppendToPostScript(ctx->psToken, "  stroke\n", (char *)NULL);
        }
    }
    Rbc_AppendToPostScript(ctx->psToken, "} def\n\n", (char *)NULL);
    return ctx;
}

/* Legend bar swatches use the unscaled square prolog shape. */
Rbc_RenderContext *Rbc_RenderBeginExportBarSymbol(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                                  const Rbc_RenderFillStyle *style, int size) {
    Rbc_RenderContext *ctx = Rbc_RenderBeginExportFill(graphPtr, exportPtr, style);

    if (exportPtr->backend == RBC_EXPORT_SVG) {
        ctx->svgBarSymbol = TRUE;
        ctx->symbolSize = size;
        return ctx;
    }
    ctx->symbolMacro = "Sq";
    ctx->symbolSize = (double)size;
    Rbc_AppendToPostScript(ctx->psToken, "\n", "/DrawSymbolProc {\n", "  gsave\n    ", (char *)NULL);
    if (style->stipple != None) {
        if (style->background != NULL) {
            Rbc_BackgroundToPostScript(ctx->psToken, (XColor *)style->background);
            Rbc_AppendToPostScript(ctx->psToken, "    Fill\n    ", (char *)NULL);
        }
        Rbc_ForegroundToPostScript(ctx->psToken,
                                   (XColor *)((style->foreground != NULL) ? style->foreground : style->background));
        Rbc_StippleToPostScript(ctx->psToken, graphPtr->display, style->stipple);
    } else if (style->foreground != NULL) {
        Rbc_ForegroundToPostScript(ctx->psToken, (XColor *)style->foreground);
        Rbc_AppendToPostScript(ctx->psToken, "    fill\n", (char *)NULL);
    }
    Rbc_AppendToPostScript(ctx->psToken, "  grestore\n", (char *)NULL);
    Rbc_AppendToPostScript(ctx->psToken, "} def\n\n", (char *)NULL);
    return ctx;
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
    ctx->ops->end(ctx);
}

Rbc_RenderContext *Rbc_RenderBeginExportOutput(Rbc_ExportContext *exportPtr) {
    Rbc_RenderContext *ctx = (Rbc_RenderContext *)ckalloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    ctx->ops = exportPtr->backend == RBC_EXPORT_SVG ? &svgOps : &postScriptOps;
    ctx->exportPtr = exportPtr;
    ctx->psToken = (PsToken)exportPtr->backendData;
    return ctx;
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
static void SvgError(Rbc_ExportContext *token, const char *message) {
    if (token->error == NULL) {
        token->error = message;
    }
}

static void SvgString(Rbc_ExportContext *token, const char *string, Tcl_Size length) {
    Tcl_Size i;

    if (length < 0) {
        length = (Tcl_Size)strlen(string);
    }
    for (i = 0; i < length; i++) {
        unsigned char ch = (unsigned char)string[i];
        switch (ch) {
        case '&':
            Rbc_ExportAppend(token, "&amp;", (char *)NULL);
            break;
        case '<':
            Rbc_ExportAppend(token, "&lt;", (char *)NULL);
            break;
        case '>':
            Rbc_ExportAppend(token, "&gt;", (char *)NULL);
            break;
        case '"':
            Rbc_ExportAppend(token, "&quot;", (char *)NULL);
            break;
        case '\'':
            Rbc_ExportAppend(token, "&apos;", (char *)NULL);
            break;
        default: {
            Tcl_UniChar codepoint;
            
            int bytes = Tcl_UtfToUniChar(string + i, &codepoint);
            if (bytes > length - i || (codepoint < 32 && codepoint != '\n' && codepoint != '\r' && codepoint != '\t') ||
                (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint == 0xfffe || codepoint == 0xffff) {
                SvgError(token, "text contains a character that XML cannot represent");
            } else {
                Tcl_DStringAppend(token->buffer, string + i, bytes);
            }
            i += bytes - 1;
        } break;
        }
    }
}

static void SvgColor(Rbc_ExportContext *token, const XColor *color) {
    if (color == NULL) {
        Rbc_ExportAppend(token, "none", (char *)NULL);
    } else {
        Rbc_ExportFormat(token, "#%02x%02x%02x", color->red >> 8, color->green >> 8, color->blue >> 8);
    }
}

static void SvgStrokeAttributes(Rbc_RenderContext *ctx, int underlay) {
    Rbc_ExportContext *token = ctx->exportPtr;
    int i;

    Rbc_ExportAppend(token, " fill=\"none\" stroke=\"", (char *)NULL);
    SvgColor(token, underlay ? &ctx->svgOffColor : (ctx->svgHasColor ? &ctx->svgColor : NULL));
    Rbc_ExportFormat(token, "\" stroke-width=\"%d\" stroke-linecap=\"%s\" stroke-linejoin=\"%s\"", ctx->svgWidth,
                     ctx->svgCap == CapRound ? "round" : (ctx->svgCap == CapProjecting ? "square" : "butt"),
                     ctx->svgJoin == JoinRound ? "round" : (ctx->svgJoin == JoinBevel ? "bevel" : "miter"));
    if (!underlay && ctx->svgDashes.values[0] != 0) {
        Rbc_ExportAppend(token, " stroke-dasharray=\"", (char *)NULL);
        for (i = 0; i < RBC_MAX_DASH_VALUES && ctx->svgDashes.values[i] != 0; i++) {
            Rbc_ExportFormat(token, "%s%d", i ? " " : "", ctx->svgDashes.values[i]);
        }
        Rbc_ExportAppend(token, "\"", (char *)NULL);
    }
}

static void SvgPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    int pass, first = ctx->svgHasOffColor && ctx->svgDashes.values[0] ? 0 : 1;
    Tcl_Size i;

    if (count < 2) {
        return;
    }
    for (pass = first; pass < 2; pass++) {
        Rbc_ExportAppend(ctx->exportPtr, "<polyline points=\"", (char *)NULL);
        for (i = 0; i < count; i++) {
            Rbc_ExportFormat(ctx->exportPtr, "%g,%g ", points[i].x, points[i].y);
        }
        Rbc_ExportAppend(ctx->exportPtr, "\"", (char *)NULL);
        SvgStrokeAttributes(ctx, pass == 0);
        Rbc_ExportAppend(ctx->exportPtr, "/>\n", (char *)NULL);
    }
}

static void SvgSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        Point2D points[2] = {segments[i].p, segments[i].q};

        SvgPolyline(ctx, points, 2);
    }
}

static void SvgLineStyle(Rbc_RenderContext *ctx, int cap, int join) {
    ctx->svgCap = cap;
    ctx->svgJoin = join;
}

static void SvgDashBackground(Rbc_RenderContext *ctx, const XColor *color) {
    ctx->svgHasOffColor = color != NULL;
    if (color != NULL) {
        ctx->svgOffColor = *color;
    }
}

static void SvgPolygon(Rbc_ExportContext *token, const Point2D *points, Tcl_Size count, const XColor *color,
                       double opacity) {
    Tcl_Size i;

    if (count < 3) {
        return;
    }
    Rbc_ExportAppend(token, "<polygon points=\"", (char *)NULL);
    for (i = 0; i < count; i++) {
        Rbc_ExportFormat(token, "%g,%g ", points[i].x, points[i].y);
    }
    Rbc_ExportAppend(token, "\" fill=\"", (char *)NULL);
    SvgColor(token, color);
    Rbc_ExportFormat(token, "\" fill-opacity=\"%g\" fill-rule=\"evenodd\"/>\n", opacity);
}

static void SvgFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    XColor black;
    const XColor *foreground;
    Tcl_Size i;

    if (count < 3) {
        return;
    }
    memset(&black, 0, sizeof(black));
    foreground = ctx->fillStyle.foreground ? ctx->fillStyle.foreground : &black;
    if (ctx->fillStyle.stipple != None || ctx->fillStyle.backgroundOnly) {
        unsigned int id = SvgFillPattern(ctx, foreground);
        if (id == 0) {
            return;
        }
        Rbc_ExportAppend(ctx->exportPtr, "<polygon points=\"", (char *)NULL);
        for (i = 0; i < count; i++) {
            Rbc_ExportFormat(ctx->exportPtr, "%g,%g ", points[i].x, points[i].y);
        }
        Rbc_ExportFormat(ctx->exportPtr, "\" fill=\"url(#rbcPattern%u)\" fill-rule=\"evenodd\"/>\n", id);
    } else {
        SvgPolygon(ctx->exportPtr, points, count, foreground, ctx->fillStyle.opacity);
    }
}

static void SvgRectangle(Rbc_ExportContext *token, double x, double y, int width, int height, const XColor *color) {
    if (width <= 0 || height <= 0) {
        return;
    }
    Rbc_ExportFormat(token, "<rect x=\"%g\" y=\"%g\" width=\"%d\" height=\"%d\" fill=\"", x, y, width, height);
    SvgColor(token, color);
    Rbc_ExportAppend(token, "\"/>\n", (char *)NULL);
}

static void SvgFillRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rectangles, Tcl_Size count) {
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *rect = rectangles + i;

        if (rect->width <= 0 || rect->height <= 0) {
            continue;
        }
        if (ctx->fillStyle.stipple != None || ctx->fillStyle.backgroundOnly) {
            const XColor *foreground =
                ctx->fillStyle.foreground ? ctx->fillStyle.foreground : ctx->fillStyle.background;
            unsigned int id = SvgFillPattern(ctx, foreground);

            if (id == 0) {
                return;
            }
            Rbc_ExportFormat(ctx->exportPtr,
                             "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"url(#rbcPattern%u)\"/>\n",
                             rect->x, rect->y, rect->width, rect->height, id);
        } else {
            SvgRectangle(ctx->exportPtr, rect->x, rect->y, rect->width, rect->height, ctx->fillStyle.foreground);
        }
    }
}

/* Store bitmap rows as vector runs, preserving mapped masks without screenshots. */
static void SvgBitmapRuns(Rbc_ExportContext *token, XImage *bits, XImage *mask, const XColor *color) {
    int x, y;

    if (color == NULL) {
        return;
    }
    Rbc_ExportAppend(token, "<path fill=\"", (char *)NULL);
    SvgColor(token, color);
    Rbc_ExportAppend(token, "\" stroke=\"none\" d=\"", (char *)NULL);
    for (y = 0; y < bits->height; y++) {
        x = 0;
        while (x < bits->width) {
            int start;

            while (x < bits->width && (!XGetPixel(bits, x, y) || (mask != NULL && !XGetPixel(mask, x, y)))) {
                x++;
            }
            start = x;
            while (x < bits->width && XGetPixel(bits, x, y) && (mask == NULL || XGetPixel(mask, x, y))) {
                x++;
            }
            if (x > start) {
                Rbc_ExportFormat(token, "M%d %dh%dv1h%dZ", start, y, x - start, start - x);
            }
        }
    }
    Rbc_ExportAppend(token, "\"/>\n", (char *)NULL);
}

static int SvgDefineBitmapSymbol(Rbc_RenderContext *ctx) {
    const Rbc_RenderSymbolStyle *style = &ctx->svgSymbol;
    Rbc_ExportContext *token = ctx->exportPtr;
    Display *display = ctx->graphPtr->display;
    XImage *bits, *mask = NULL;
    int width, height;

    Tk_SizeOfBitmap(display, style->bitmap, &width, &height);
    if (width <= 0 || height <= 0) {
        return FALSE;
    }
    bits = XGetImage(display, style->bitmap, 0, 0, width, height, 1, XYPixmap);
    if (bits == NULL) {
        SvgError(token, "cannot read bitmap symbol for SVG");
        return FALSE;
    }
    /* A transparent symbol uses its own bits as the native clipping mask. */
    if (style->mask != None && style->fillColor != NULL) {
        int maskWidth, maskHeight;

        Tk_SizeOfBitmap(display, style->mask, &maskWidth, &maskHeight);
        if (maskWidth != width || maskHeight != height) {
            SvgError(token, "SVG bitmap symbol and mask dimensions differ");
            XDestroyImage(bits);
            return FALSE;
        }
        mask = XGetImage(display, style->mask, 0, 0, width, height, 1, XYPixmap);
        if (mask == NULL) {
            SvgError(token, "cannot read bitmap symbol mask for SVG");
            XDestroyImage(bits);
            return FALSE;
        }
    }
    ctx->svgBitmapId = ++token->nextResourceId;
    ctx->svgBitmapWidth = width;
    ctx->svgBitmapHeight = height;
    Rbc_ExportFormat(token, "<defs><g id=\"rbcBitmap%u\">\n", ctx->svgBitmapId);
    if (mask != NULL) {
        SvgBitmapRuns(token, mask, NULL, style->fillColor);
    } else {
        SvgRectangle(token, 0, 0, width, height, style->fillColor);
    }
    SvgBitmapRuns(token, bits, mask, style->outlineColor);
    Rbc_ExportAppend(token, "</g></defs>\n", (char *)NULL);
    if (mask != NULL) {
        XDestroyImage(mask);
    }
    XDestroyImage(bits);
    return TRUE;
}

/* PNG uses Tcl's built-in zlib; no Cairo or additional image library is needed. */
static void SvgPngWord(unsigned char *bytes, unsigned int value) {
    bytes[0] = (unsigned char)(value >> 24);
    bytes[1] = (unsigned char)(value >> 16);
    bytes[2] = (unsigned char)(value >> 8);
    bytes[3] = (unsigned char)value;
}

static void SvgPngChunk(Tcl_DString *png, const char *type, const unsigned char *data, Tcl_Size length) {
    unsigned char word[4];
    unsigned int crc;

    SvgPngWord(word, (unsigned int)length);
    Tcl_DStringAppend(png, (const char *)word, 4);
    Tcl_DStringAppend(png, type, 4);
    if (length > 0) {
        Tcl_DStringAppend(png, (const char *)data, length);
    }
    crc = Tcl_ZlibCRC32(0, (const unsigned char *)type, 4);
    if (length > 0) {
        crc = Tcl_ZlibCRC32(crc, data, length);
    }
    SvgPngWord(word, crc);
    Tcl_DStringAppend(png, (const char *)word, 4);
}

static void SvgBase64(Rbc_ExportContext *token, const unsigned char *bytes, Tcl_Size length) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    Tcl_Size i;

    for (i = 0; i < length; i += 3) {
        unsigned int value = (unsigned int)bytes[i] << 16;
        char encoded[4];

        if (i + 1 < length) {
            value |= (unsigned int)bytes[i + 1] << 8;
        }
        if (i + 2 < length) {
            value |= bytes[i + 2];
        }
        encoded[0] = alphabet[(value >> 18) & 63];
        encoded[1] = alphabet[(value >> 12) & 63];
        encoded[2] = i + 1 < length ? alphabet[(value >> 6) & 63] : '=';
        encoded[3] = i + 2 < length ? alphabet[value & 63] : '=';
        Tcl_DStringAppend(token->buffer, encoded, 4);
    }
}

static void SvgPhotoBlock(Rbc_ExportContext *token, const Tk_PhotoImageBlock *block, double x, double y) {
    Tcl_Obj *raw, *compressed;
    Tcl_InterpState saved;
    Tcl_DString png;
    unsigned char header[13], *target, *data;
    Tcl_Size rowSize, length;
    int row, column, alpha, result;

    if (block->width <= 0 || block->height <= 0) {
        return;
    }
    if (block->pixelSize <= 0 || block->pixelPtr == NULL || (Tcl_WideInt)block->width * 4 + 1 > INT_MAX ||
        ((Tcl_WideInt)block->width * 4 + 1) * block->height > INT_MAX) {
        SvgError(token, "photo is too large or invalid for SVG PNG encoding");
        return;
    }
    rowSize = (Tcl_Size)block->width * 4 + 1;
    alpha = block->offset[3];
    if (alpha < 0 || alpha >= block->pixelSize || alpha == block->offset[0] || alpha == block->offset[1] ||
        alpha == block->offset[2]) {
        alpha = -1;
    }
    raw = Tcl_NewObj();
    Tcl_IncrRefCount(raw);
    target = Tcl_SetByteArrayLength(raw, rowSize * block->height);
    for (row = 0; row < block->height; row++) {
        const unsigned char *source = block->pixelPtr + (ptrdiff_t)row * block->pitch;
        *target++ = 0; /* PNG filter: none. */
        for (column = 0; column < block->width; column++, source += block->pixelSize) {
            *target++ = source[block->offset[0]];
            *target++ = source[block->offset[1]];
            *target++ = source[block->offset[2]];
            *target++ = alpha < 0 ? 255 : source[alpha];
        }
    }
    saved = Tcl_SaveInterpState(token->interp, TCL_OK);
    result = Tcl_ZlibDeflate(token->interp, TCL_ZLIB_FORMAT_ZLIB, raw, -1, NULL);
    Tcl_DecrRefCount(raw);
    if (result != TCL_OK) {
        SvgError(token, "cannot compress photo for SVG PNG encoding");
        Tcl_RestoreInterpState(token->interp, saved);
        return;
    }
    compressed = Tcl_GetObjResult(token->interp);
    Tcl_IncrRefCount(compressed);
    Tcl_RestoreInterpState(token->interp, saved);
    data = Tcl_GetByteArrayFromObj(compressed, &length);
    if (length > INT_MAX) {
        SvgError(token, "compressed photo is too large for SVG PNG encoding");
        Tcl_DecrRefCount(compressed);
        return;
    }
    Tcl_DStringInit(&png);
    Tcl_DStringAppend(&png, "\211PNG\r\n\032\n", 8);
    SvgPngWord(header, (unsigned int)block->width);
    SvgPngWord(header + 4, (unsigned int)block->height);
    header[8] = 8;
    header[9] = 6; /* Eight-bit, straight-alpha RGBA. */
    header[10] = header[11] = header[12] = 0;
    SvgPngChunk(&png, "IHDR", header, 13);
    SvgPngChunk(&png, "IDAT", data, length);
    SvgPngChunk(&png, "IEND", NULL, 0);
    Tcl_DecrRefCount(compressed);
    Rbc_ExportFormat(token,
                     "<image x=\"%g\" y=\"%g\" width=\"%d\" height=\"%d\" preserveAspectRatio=\"none\" "
                     "href=\"data:image/png;base64,",
                     x, y, block->width, block->height);
    SvgBase64(token, (const unsigned char *)Tcl_DStringValue(&png), Tcl_DStringLength(&png));
    Rbc_ExportAppend(token, "\"/>\n", (char *)NULL);
    Tcl_DStringFree(&png);
}

/* Tk image types expose drawing rather than a common pixel buffer. Render
 * conventional source-over images on two mattes to recover RGB and alpha. */
static Rbc_ColorImage SvgRasterImage(Rbc_ExportContext *token, Tk_Image image) {
    Tk_Window tkwin = token->tkwin;
    Display *display = Tk_Display(tkwin);
    Rbc_ColorImage black = NULL, white = NULL;
    Pixmap pixmap;
    GC gc;
    XGCValues values;
    int width, height, pass;
    size_t i, count;

    if (image == NULL || Tk_ImageIsDeleted(image)) {
        return NULL;
    }
    Tk_SizeOfImage(image, &width, &height);
    if (width <= 0 || height <= 0) {
        return NULL;
    }
    Tk_MakeWindowExist(tkwin);
    pixmap = Tk_GetPixmap(display, Tk_WindowId(tkwin), width, height, Tk_Depth(tkwin));
    if (pixmap == None) {
        SvgError(token, "cannot allocate SVG image capture pixmap");
        return NULL;
    }
    values.foreground = BlackPixelOfScreen(Tk_Screen(tkwin));
    gc = Tk_GetGC(tkwin, GCForeground, &values);
    for (pass = 0; pass < 2; pass++) {
        Rbc_ColorImage captured;

        if (pass == 1) {
            Tk_FreeGC(display, gc);
            values.foreground = WhitePixelOfScreen(Tk_Screen(tkwin));
            gc = Tk_GetGC(tkwin, GCForeground, &values);
        }
        XFillRectangle(display, pixmap, gc, 0, 0, width, height);
        Tk_RedrawImage(image, 0, 0, width, height, pixmap, 0, 0);
        captured = Rbc_DrawableToColorImage(tkwin, pixmap, 0, 0, width, height, GAMMA);
        if (captured == NULL) {
            SvgError(token, "cannot capture Tk image for SVG");
            break;
        }
        if (pass == 0) {
            black = captured;
        } else {
            white = captured;
        }
    }
    Tk_FreeGC(display, gc);
    Tk_FreePixmap(display, pixmap);
    if (white == NULL) {
        if (black != NULL) {
            Rbc_FreeColorImage(black);
        }
        return NULL;
    }
    count = (size_t)width * height;
    for (i = 0; i < count; i++) {
        Pix32 *b = Rbc_ColorImageBits(black) + i;
        const Pix32 *w = Rbc_ColorImageBits(white) + i;
        int difference = MAX((int)w->Red - b->Red, MAX((int)w->Green - b->Green, (int)w->Blue - b->Blue));
        int alpha = 255 - MIN(255, MAX(0, difference));

        b->Red = alpha ? (unsigned char)MIN(255, ((int)b->Red * 255 + alpha / 2) / alpha) : 0;
        b->Green = alpha ? (unsigned char)MIN(255, ((int)b->Green * 255 + alpha / 2) / alpha) : 0;
        b->Blue = alpha ? (unsigned char)MIN(255, ((int)b->Blue * 255 + alpha / 2) / alpha) : 0;
        b->Alpha = (unsigned char)alpha;
    }
    Rbc_FreeColorImage(white);
    return black;
}

static void SvgColorImage(Rbc_ExportContext *token, Rbc_ColorImage image, double x, double y) {
    Tk_PhotoImageBlock block;

    if ((size_t)Rbc_ColorImageWidth(image) > (size_t)INT_MAX / sizeof(Pix32)) {
        SvgError(token, "captured image is too large for SVG");
        return;
    }
    block.width = Rbc_ColorImageWidth(image);
    block.height = Rbc_ColorImageHeight(image);
    block.pixelSize = sizeof(Pix32);
    block.pitch = block.width * block.pixelSize;
    block.pixelPtr = (unsigned char *)Rbc_ColorImageBits(image);
    block.offset[0] = offsetof(Pix32, Red);
    block.offset[1] = offsetof(Pix32, Green);
    block.offset[2] = offsetof(Pix32, Blue);
    block.offset[3] = offsetof(Pix32, Alpha);
    SvgPhotoBlock(token, &block, x, y);
}

static void SvgTkImage(Rbc_RenderContext *ctx, Tk_Image image, double x, double y) {
    Rbc_ColorImage captured = SvgRasterImage(ctx->exportPtr, image);

    if (captured != NULL) {
        SvgColorImage(ctx->exportPtr, captured, x, y);
        Rbc_FreeColorImage(captured);
    }
}

/* Stipples use graph pixels; photo tiles retain their toplevel-relative
 * phase. A translated legend sample supplies its own local origin. */
static unsigned int SvgFillPattern(Rbc_RenderContext *ctx, const XColor *foreground) {
    Rbc_ExportContext *token = ctx->exportPtr;
    const Rbc_RenderFillStyle *style = &ctx->fillStyle;
    Tk_PhotoImageBlock block;
    XImage *bits = NULL;
    Rbc_ColorImage captured = NULL;
    int width, height;
    double originX = 0.0, originY = 0.0;

    if (ctx->svgPatternId != 0) {
        return ctx->svgPatternId;
    }
    if (style->backgroundOnly) {
        Tk_Window tkwin;

        if (ctx->svgTile == NULL) {
            SvgError(token, "missing tile image for SVG");
            return 0;
        }
        if (Rbc_GetTilePhoto(ctx->svgTile, &block)) {
            width = block.width;
            height = block.height;
        } else {
            captured = SvgRasterImage(token, Rbc_ImageOfTile(ctx->svgTile));
            if (captured == NULL) {
                return 0;
            }
            width = Rbc_ColorImageWidth(captured);
            height = Rbc_ColorImageHeight(captured);
        }
        for (tkwin = ctx->graphPtr->tkwin; !Tk_IsTopLevel(tkwin); tkwin = Tk_Parent(tkwin)) {
            originX -= Tk_X(tkwin) + Tk_Changes(tkwin)->border_width;
            originY -= Tk_Y(tkwin) + Tk_Changes(tkwin)->border_width;
        }
    } else {
        Tk_SizeOfBitmap(ctx->graphPtr->display, style->stipple, &width, &height);
        if (width > 0 && height > 0) {
            bits = XGetImage(ctx->graphPtr->display, style->stipple, 0, 0, width, height, 1, XYPixmap);
            if (bits == NULL) {
                SvgError(token, "cannot read stipple for SVG pattern");
                return 0;
            }
        }
    }
    if (width <= 0 || height <= 0) {
        return 0;
    }
    ctx->svgPatternId = ++token->nextResourceId;
    Rbc_ExportFormat(
        token,
        "<defs><pattern id=\"rbcPattern%u\" patternUnits=\"userSpaceOnUse\" patternContentUnits=\"userSpaceOnUse\" "
        "x=\"0\" y=\"0\" width=\"%d\" height=\"%d\" patternTransform=\"translate(%g %g)\">\n",
        ctx->svgPatternId, width, height, originX, originY);
    if (style->backgroundOnly) {
        if (captured != NULL) {
            SvgColorImage(token, captured, 0, 0);
            Rbc_FreeColorImage(captured);
        } else {
            SvgPhotoBlock(token, &block, 0, 0);
        }
    } else {
        SvgRectangle(token, 0, 0, width, height, style->background);
        SvgBitmapRuns(token, bits, NULL, foreground);
        XDestroyImage(bits);
    }
    Rbc_ExportAppend(token, "</pattern></defs>\n", (char *)NULL);
    return ctx->svgPatternId;
}

static void SvgSymbolPoints(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count) {
    const Rbc_RenderSymbolStyle *style = &ctx->svgSymbol;
    Tcl_Size i;
    double size = style->size;

    if (ctx->svgBarSymbol) {
        for (i = 0; i < count; i++) {
            Rbc_RenderRectangle rect = {0, 0, (int)ctx->symbolSize, (int)ctx->symbolSize};

            Rbc_ExportFormat(ctx->exportPtr, "<g transform=\"translate(%g %g)\">\n", centers[i].x - ctx->symbolSize / 2,
                             centers[i].y - ctx->symbolSize / 2);
            SvgFillRectangles(ctx, &rect, 1);
            Rbc_ExportAppend(ctx->exportPtr, "</g>\n", (char *)NULL);
        }
        return;
    }
    if (style->type == RBC_RENDER_SYMBOL_BITMAP) {
        double scale;

        if (count <= 0 || style->size <= 0) {
            return;
        }
        if (ctx->svgBitmapId == 0 && !SvgDefineBitmapSymbol(ctx)) {
            return;
        }
        scale = MIN(size / ctx->svgBitmapWidth, size / ctx->svgBitmapHeight);
        for (i = 0; i < count; i++) {
            Rbc_ExportFormat(ctx->exportPtr,
                             "<use href=\"#rbcBitmap%u\" transform=\"translate(%g %g) scale(%g) translate(%g %g)\"/>\n",
                             ctx->svgBitmapId, centers[i].x, centers[i].y, scale, -ctx->svgBitmapWidth / 2.0,
                             -ctx->svgBitmapHeight / 2.0);
        }
        return;
    }
    for (i = 0; i < count; i++) {
        double r = size / 2.0;
        int skinny = style->type == RBC_RENDER_SYMBOL_SPLUS || style->type == RBC_RENDER_SYMBOL_SCROSS;
        int cross = style->type == RBC_RENDER_SYMBOL_CROSS || style->type == RBC_RENDER_SYMBOL_SCROSS;
        Rbc_ExportContext *token = ctx->exportPtr;

        Rbc_ExportFormat(token, "<g transform=\"translate(%g %g)%s\">", centers[i].x, centers[i].y,
                         cross ? " rotate(45)" : "");
        if (style->type == RBC_RENDER_SYMBOL_CIRCLE) {
            Rbc_ExportFormat(token, "<circle r=\"%g\"", r);
        } else {
            Rbc_ExportAppend(token, "<path d=\"", (char *)NULL);
            switch (style->type) {
            case RBC_RENDER_SYMBOL_SQUARE:
                r = PostScriptSymbolRound(size * 0.886226925452758) / 2.0;
                Rbc_ExportFormat(token, "M%g %gH%gV%gH%gZ", -r, -r, r, r, -r);
                break;
            case RBC_RENDER_SYMBOL_DIAMOND:
                r = PostScriptSymbolRound(size * M_SQRT1_2) * M_SQRT1_2;
                Rbc_ExportFormat(token, "M0 %gL%g 0 0 %g %g 0Z", -r, r, r, -r);
                break;
            case RBC_RENDER_SYMBOL_TRIANGLE:
            case RBC_RENDER_SYMBOL_ARROW: {
                double b = PostScriptSymbolRound(size * 0.7) * 1.3467736870885982 * 0.5;
                double h = b * 0.86602540378443871;
                double sign = style->type == RBC_RENDER_SYMBOL_ARROW ? -1.0 : 1.0;

                Rbc_ExportFormat(token, "M0 %gL%g %g %g %gZ", -h * sign, b, b * 0.57735026918962573 * sign, -b,
                                 b * 0.57735026918962573 * sign);
                break;
            }
            default: {
                double s = PostScriptSymbolRound(size * 0.886226925452758);
                double w = (int)s / 6;
                r = (int)s / 2;
                if (skinny) {
                    Rbc_ExportFormat(token, "M%g 0H%gM0 %gV%g", -r, r, -r, r);
                } else {
                    Rbc_ExportFormat(token, "M%g %gH%gV%gH%gV%gH%gV%gH%gV%gH%gV%gH%gZ", -r, -w, -w, -r, w, -w, r, w, w,
                                     r, -w, w, -r);
                }
                break;
            }
            }
            Rbc_ExportAppend(token, "\"", (char *)NULL);
        }
        Rbc_ExportAppend(token, " fill=\"", (char *)NULL);
        SvgColor(token, skinny ? NULL : style->fillColor);
        Rbc_ExportAppend(token, "\" stroke=\"", (char *)NULL);
        SvgColor(token, style->outlineWidth > 0 ? style->outlineColor : NULL);
        Rbc_ExportFormat(token, "\" stroke-width=\"%d\"/></g>\n", MAX(1, style->outlineWidth));
    }
}

/* Preserve Tk layout and baseline positions while keeping SVG text editable. */
static void SvgText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y) {
    Rbc_ExportContext *token = ctx->exportPtr;
    Tcl_InterpState saved;
    Tcl_Obj *args[5], *attributes;
    Tcl_Obj **values;
    Tcl_Size n, i;
    const char *family = "sans-serif", *weight = "normal", *slant = "normal";
    int size = 12, pass;
    double pixelSize, width, height;
    TextLayout *layout;
    Point2D anchor = {x, y};

    if (string == NULL || *string == '\0') {
        return;
    }
    args[0] = Tcl_NewStringObj("font", -1);
    args[1] = Tcl_NewStringObj("actual", -1);
    args[2] = Tcl_NewStringObj(Tk_NameOfFont(style->font), -1);
    args[3] = Tcl_NewStringObj("-displayof", -1);
    args[4] = Tcl_NewStringObj(Tk_PathName(token->tkwin), -1);
    saved = Tcl_SaveInterpState(token->interp, TCL_OK);
    for (i = 0; i < 5; i++) {
        Tcl_IncrRefCount(args[i]);
    }
    if (Tcl_EvalObjv(token->interp, 5, args, TCL_EVAL_GLOBAL) != TCL_OK) {
        SvgError(token, "cannot resolve Tk font attributes for SVG");
        for (i = 0; i < 5; i++) {
            Tcl_DecrRefCount(args[i]);
        }
        Tcl_RestoreInterpState(token->interp, saved);
        return;
    }
    attributes = Tcl_GetObjResult(token->interp);
    Tcl_IncrRefCount(attributes);
    for (i = 0; i < 5; i++) {
        Tcl_DecrRefCount(args[i]);
    }
    Tcl_RestoreInterpState(token->interp, saved);
    if (Tcl_ListObjGetElements(NULL, attributes, &n, &values) != TCL_OK) {
        SvgError(token, "invalid Tk font attributes for SVG");
        Tcl_DecrRefCount(attributes);
        return;
    }
    for (i = 0; i + 1 < n; i += 2) {
        const char *key = Tcl_GetString(values[i]);
        if (strcmp(key, "-family") == 0) {
            family = Tcl_GetString(values[i + 1]);
        }
        if (strcmp(key, "-weight") == 0) {
            weight = Tcl_GetString(values[i + 1]);
        }
        if (strcmp(key, "-slant") == 0 && strcmp(Tcl_GetString(values[i + 1]), "italic") == 0) {
            slant = "italic";
        }
        if (strcmp(key, "-size") == 0) {
            Tcl_GetIntFromObj(NULL, values[i + 1], &size);
        }
    }
    pixelSize = size < 0 ? -(double)size
                         : size * 25.4 / 72.0 * HeightOfScreen(Tk_Screen(token->tkwin)) /
                               HeightMMOfScreen(Tk_Screen(token->tkwin));
    layout = Rbc_GetTextLayout(string, style);
    Rbc_GetBoundingBox(layout->width, layout->height, style->theta, &width, &height, NULL);
    anchor = Rbc_TranslatePoint(&anchor, ROUND(width), ROUND(height), style->anchor);
    Rbc_ExportFormat(token, "<g transform=\"translate(%g %g) rotate(%g) translate(%g %g)\"", anchor.x + width / 2,
                     anchor.y + height / 2, -style->theta, -layout->width / 2.0, -layout->height / 2.0);
    Rbc_ExportAppend(token, " font-family=\"", (char *)NULL);
    SvgString(token, family, -1);
    Rbc_ExportFormat(token, "\" font-size=\"%g\" font-weight=\"%s\" font-style=\"%s\">\n", pixelSize,
                     strcmp(weight, "bold") == 0 ? "bold" : "normal", slant);
    for (pass = 0; pass < 2; pass++) {
        const XColor *color =
            pass == 0 ? style->shadow.color : ((style->state & STATE_ACTIVE) ? style->activeColor : style->color);
        int offset = pass == 0 ? style->shadow.offset : 0;

        if (color == NULL || (pass == 0 && offset <= 0)) {
            continue;
        }
        for (i = 0; i < layout->nFrags; i++) {
            TextFragment *fragment = layout->fragArr + i;
            if (fragment->count <= 0) {
                continue;
            }
            Rbc_ExportFormat(token,
                             "<text xml:space=\"preserve\" x=\"%d\" y=\"%d\" textLength=\"%d\" "
                             "lengthAdjust=\"spacingAndGlyphs\" fill=\"",
                             fragment->x + offset, fragment->y + offset,
                             Tk_TextWidth(style->font, fragment->text, fragment->count));
            SvgColor(token, color);
            Rbc_ExportAppend(token, "\">", (char *)NULL);
            SvgString(token, fragment->text, fragment->count);
            Rbc_ExportAppend(token, "</text>\n", (char *)NULL);
        }
    }
    Rbc_ExportAppend(token, "</g>\n", (char *)NULL);
    ckfree(layout);
    Tcl_DecrRefCount(attributes);
}

static void SvgPhoto(Rbc_RenderContext *ctx, Tk_PhotoHandle photo, double x, double y) {
    Tk_PhotoImageBlock block;

    if (photo == NULL) {
        SvgError(ctx->exportPtr, "missing photo image for SVG");
        return;
    }
    if (!Tk_PhotoGetImage(photo, &block)) {
        SvgError(ctx->exportPtr, "cannot read photo image for SVG");
        return;
    }
    SvgPhotoBlock(ctx->exportPtr, &block, x, y);
}

static void SvgWindow(Rbc_RenderContext *ctx, Tk_Window window, double x, double y) {
    Rbc_ColorImage captured;

    if (!Tk_IsMapped(window) || Tk_Width(window) <= 0 || Tk_Height(window) <= 0) {
        return;
    }
    captured = Rbc_DrawableToColorImage(window, Tk_WindowId(window), 0, 0, Tk_Width(window), Tk_Height(window), GAMMA);
    if (captured == NULL) {
        SvgError(ctx->exportPtr, "cannot capture window marker for SVG");
        return;
    }
    SvgColorImage(ctx->exportPtr, captured, x, y);
    Rbc_FreeColorImage(captured);
}

static void SvgBitmapMask(Rbc_RenderContext *ctx, Display *display, Pixmap bitmap, double x, double y, int width,
                          int height, const XColor *color, int background) {
    XImage *bits;

    (void)background;
    if (bitmap == None || color == NULL || width <= 0 || height <= 0) {
        return;
    }
    bits = XGetImage(display, bitmap, 0, 0, width, height, 1, XYPixmap);
    if (bits == NULL) {
        SvgError(ctx->exportPtr, "cannot read bitmap marker mask for SVG");
        return;
    }
    Rbc_ExportFormat(ctx->exportPtr, "<g transform=\"translate(%g %g)\">\n", x, y);
    SvgBitmapRuns(ctx->exportPtr, bits, NULL, color);
    Rbc_ExportAppend(ctx->exportPtr, "</g>\n", (char *)NULL);
    XDestroyImage(bits);
}

static void SvgBackgroundPolygon(Rbc_RenderContext *ctx, const XColor *color, const Point2D *points, Tcl_Size count) {
    SvgPolygon(ctx->exportPtr, points, count, color, 1.0);
}

static void SvgBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y, int width, int height,
                      int borderWidth, int relief, int fill) {
    XColor bg, dark, light;
    const XColor *top, *bottom;
    Point2D points[6];

    Tk_Get3DBorderColors(border, &bg, &dark, &light);
    if (fill) {
        SvgRectangle(ctx->exportPtr, x, y, width, height, &bg);
    }
    if (borderWidth <= 0 || width < 2 * borderWidth || height < 2 * borderWidth || relief == TK_RELIEF_FLAT) {
        return;
    }
    if (relief == TK_RELIEF_GROOVE || relief == TK_RELIEF_RIDGE) {
        int half = borderWidth / 2, offset = borderWidth - half;
        SvgBorder(ctx, border, x, y, width, height, half,
                  relief == TK_RELIEF_GROOVE ? TK_RELIEF_SUNKEN : TK_RELIEF_RAISED, FALSE);
        SvgBorder(ctx, border, x + offset, y + offset, width - 2 * offset, height - 2 * offset, half,
                  relief == TK_RELIEF_GROOVE ? TK_RELIEF_RAISED : TK_RELIEF_SUNKEN, FALSE);
        return;
    }
    if (relief == TK_RELIEF_SOLID) {
        memset(&dark, 0, sizeof(dark));
        light = dark;
    }
    top = relief == TK_RELIEF_RAISED ? &light : &dark;
    bottom = relief == TK_RELIEF_RAISED ? &dark : &light;
    SvgRectangle(ctx->exportPtr, x, y + height - borderWidth, width, borderWidth, bottom);
    SvgRectangle(ctx->exportPtr, x + width - borderWidth, y, borderWidth, height, bottom);
    points[0] = (Point2D){x, y + height};
    points[1] = (Point2D){x, y};
    points[2] = (Point2D){x + width, y};
    points[3] = (Point2D){x + width - borderWidth, y + borderWidth};
    points[4] = (Point2D){x + borderWidth, y + borderWidth};
    points[5] = (Point2D){x + borderWidth, y + height - borderWidth};
    SvgPolygon(ctx->exportPtr, points, 6, top, 1.0);
}

static void SvgClearRectangle(Rbc_RenderContext *ctx, double x, double y, int width, int height) {
    XColor white;
    memset(&white, 0, sizeof(white));
    white.red = white.green = white.blue = 65535;
    SvgRectangle(ctx->exportPtr, x, y, width, height, &white);
}

static void SvgBackgroundRectangles(Rbc_RenderContext *ctx, const XColor *color, const Rbc_RenderRectangle *rectangles,
                                    Tcl_Size count) {
    Tcl_Size i;
    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *r = rectangles + i;
        if (color == NULL) {
            SvgClearRectangle(ctx, r->x, r->y, r->width, r->height);
        } else {
            SvgRectangle(ctx->exportPtr, r->x, r->y, r->width, r->height, color);
        }
    }
}

static void SvgPlotBegin(Rbc_RenderContext *ctx, Tk_Font font, double x, double y, int width, int height,
                         const XColor *background) {
    (void)font;
    if (background == NULL) {
        SvgClearRectangle(ctx, x, y, width, height);
    } else {
        SvgRectangle(ctx->exportPtr, x, y, width, height, background);
    }
    Rbc_ExportFormat(ctx->exportPtr,
                     "<defs><clipPath id=\"rbcPlot\" clipPathUnits=\"userSpaceOnUse\"><rect x=\"%g\" y=\"%g\" "
                     "width=\"%d\" height=\"%d\"/></clipPath></defs>\n<g clip-path=\"url(#rbcPlot)\">\n",
                     x, y, width, height);
}

static void SvgPlotEnd(Rbc_RenderContext *ctx) { Rbc_ExportAppend(ctx->exportPtr, "</g>\n", (char *)NULL); }

static const Rbc_RenderOutputOps svgOutputOps = {SvgText,
                                                 SvgPhoto,
                                                 SvgWindow,
                                                 SvgBackgroundPolygon,
                                                 SvgBorder,
                                                 SvgClearRectangle,
                                                 SvgBackgroundRectangles,
                                                 SvgPlotBegin,
                                                 SvgPlotEnd,
                                                 SvgBitmapMask,
                                                 SvgTkImage};

static const Rbc_RenderOps svgOps = {SvgPolyline,       SvgSegments,    SvgLineStyle,
                                     SvgDashBackground, SvgFillPolygon, SvgFillRectangles,
                                     SvgSymbolPoints,   &svgOutputOps,  PostScriptEnd};
