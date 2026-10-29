/* Cairo screen renderer and disabled-build fallbacks. See license.terms. */
#include "rbcRenderInt.h"
#include <stdint.h>
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

#ifdef RBC_HAVE_CAIRO
typedef struct {
    Rbc_RenderContext core;
    Rbc_RenderFillStyle fillStyle;
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
} CairoRenderContext;

#define CAIRO(ctx) ((CairoRenderContext *)(ctx))

static void CairoClipBegin(Rbc_RenderContext *ctx, int x, int y, int width, int height) {
    cairo_save(CAIRO(ctx)->cr);
    cairo_rectangle(CAIRO(ctx)->cr, x, y, MAX(0, width), MAX(0, height));
    cairo_clip(CAIRO(ctx)->cr);
}
static void CairoClipEnd(Rbc_RenderContext *ctx) { cairo_restore(CAIRO(ctx)->cr); }

static void CairoPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
static void CairoSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count);
static void CairoLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle);
static void CairoDashBackground(Rbc_RenderContext *ctx, const XColor *color);
static void CairoFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
static void CairoFillRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rectangles, Tcl_Size count);
static void CairoEnd(Rbc_RenderContext *ctx);
static void CairoArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc);
static void CairoFillArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc);

static const Rbc_RenderOps cairoOps = {
    CairoPolyline, CairoSegments, CairoLineStyle, CairoDashBackground, CairoFillPolygon, CairoFillRectangles, NULL,
    NULL,          CairoEnd,      CairoArc,       CairoFillArc,        CairoClipBegin,   CairoClipEnd};

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
    if (CAIRO(ctx)->bitmapPattern != NULL) {
        cairo_pattern_destroy(CAIRO(ctx)->bitmapPattern);
    }
    if (CAIRO(ctx)->cr != NULL) {
        cairo_destroy(CAIRO(ctx)->cr);
    }
    if (CAIRO(ctx)->surface != NULL) {
        cairo_surface_destroy(CAIRO(ctx)->surface);
    }
#ifdef WIN32
    if (CAIRO(ctx)->savedDC != 0) {
        RestoreDC(CAIRO(ctx)->dc, CAIRO(ctx)->savedDC);
    }
    Rbc_WinReleaseDrawableDC(CAIRO(ctx)->dcState);
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
    if (CAIRO(ctx)->doubleDash) {
        cairo_set_dash(CAIRO(ctx)->cr, NULL, 0, 0.0);
        SetStrokeColor(CAIRO(ctx)->cr, &CAIRO(ctx)->offColor);
        cairo_stroke_preserve(CAIRO(ctx)->cr);
    }
    SetStrokeColor(CAIRO(ctx)->cr, &CAIRO(ctx)->foreground);
    cairo_set_dash(CAIRO(ctx)->cr, CAIRO(ctx)->dashes, CAIRO(ctx)->nDashes, CAIRO(ctx)->dashOffset);
    cairo_stroke(CAIRO(ctx)->cr);
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
    ctx = Tcl_AttemptAlloc(sizeof(CairoRenderContext));
    if (ctx == NULL) {
        return NULL;
    }
    memset(ctx, 0, sizeof(CairoRenderContext));
    ctx->ops = &cairoOps;
    ctx->graphPtr = graphPtr;
    CAIRO(ctx)->foreground = *colorPtr;
    if (dashesPtr != NULL) {
        while ((CAIRO(ctx)->nDashes < RBC_MAX_DASH_VALUES) && (dashesPtr->values[CAIRO(ctx)->nDashes] != 0)) {
            CAIRO(ctx)->dashes[CAIRO(ctx)->nDashes] = dashesPtr->values[CAIRO(ctx)->nDashes];
            CAIRO(ctx)->nDashes++;
        }
        CAIRO(ctx)->dashOffset = dashesPtr->offset;
    }
    if ((CAIRO(ctx)->nDashes > 0) && (offColorPtr != NULL)) {
        if ((offColorPtr->red == colorPtr->red) && (offColorPtr->green == colorPtr->green) &&
            (offColorPtr->blue == colorPtr->blue)) {
            /* Equal dash colors form a solid stroke; avoid double coverage. */
            CAIRO(ctx)->nDashes = 0;
            CAIRO(ctx)->dashOffset = 0.0;
        } else {
            CAIRO(ctx)->offColor = *offColorPtr;
            CAIRO(ctx)->doubleDash = TRUE;
        }
    }
#ifdef WIN32
    if ((graphPtr->renderTarget != NULL) && (graphPtr->renderTarget->drawable == drawable)) {
        /* Native text/fallbacks may have touched the shared DIB since the last draw. */
        GdiFlush();
        cairo_surface_mark_dirty(graphPtr->renderTarget->image);
        CAIRO(ctx)->surface = cairo_surface_reference(graphPtr->renderTarget->image);
    } else {
        CAIRO(ctx)->dc = Rbc_WinAcquireDrawableDC(graphPtr->display, drawable, &CAIRO(ctx)->dcState);
        if (CAIRO(ctx)->dc == NULL) {
            goto fail;
        }
        /* Keep metafile/printing output on its existing native path. */
        if ((GetObjectType(CAIRO(ctx)->dc) != OBJ_DC) && (GetObjectType(CAIRO(ctx)->dc) != OBJ_MEMDC)) {
            goto fail;
        }
        if (GetDeviceCaps(CAIRO(ctx)->dc, TECHNOLOGY) != DT_RASDISPLAY) {
            goto fail;
        }
        CAIRO(ctx)->savedDC = SaveDC(CAIRO(ctx)->dc);
        if (CAIRO(ctx)->savedDC == 0) {
            goto fail;
        }
        GdiFlush();
        CAIRO(ctx)->surface = cairo_win32_surface_create(CAIRO(ctx)->dc);
    }
#else
    CAIRO(ctx)->surface =
        cairo_xlib_surface_create(graphPtr->display, drawable, Tk_Visual(graphPtr->tkwin), targetWidth, targetHeight);
#endif
    if (cairo_surface_status(CAIRO(ctx)->surface) != CAIRO_STATUS_SUCCESS) {
        goto fail;
    }
    CAIRO(ctx)->cr = cairo_create(CAIRO(ctx)->surface);
    cairo_rectangle(CAIRO(ctx)->cr, left, top, MAX(0.0, (double)right - left + 1.0),
                    MAX(0.0, (double)bottom - top + 1.0));
    cairo_clip(CAIRO(ctx)->cr);
    /* RBC integer screen coordinates identify pixel centers. */
    cairo_translate(CAIRO(ctx)->cr, 0.5, 0.5);
    SetStrokeColor(CAIRO(ctx)->cr, colorPtr);
    cairo_set_line_width(CAIRO(ctx)->cr, width);
    cairo_set_line_cap(CAIRO(ctx)->cr, CAIRO_LINE_CAP_BUTT);
    cairo_set_line_join(CAIRO(ctx)->cr, CAIRO_LINE_JOIN_ROUND);
    cairo_set_antialias(CAIRO(ctx)->cr, GetAntialias(graphPtr->antialias));
    cairo_set_dash(CAIRO(ctx)->cr, CAIRO(ctx)->dashes, CAIRO(ctx)->nDashes, CAIRO(ctx)->dashOffset);
    if (cairo_status(CAIRO(ctx)->cr) != CAIRO_STATUS_SUCCESS) {
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
    cairo_move_to(CAIRO(ctx)->cr, points[0].x, points[0].y);
    for (i = 1; i < count; i++) {
        cairo_line_to(CAIRO(ctx)->cr, points[i].x, points[i].y);
    }
    StrokeRenderPath(ctx);
}

/* Restore the CTM before painting so ellipse aspect ratio changes geometry
 * only, not line width/dash lengths or the stipple's widget-space origin. */
static void CairoArcPath(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    double start = -arc->start * M_PI / 180.0;
    double end = -(arc->start + arc->extent) * M_PI / 180.0;

    cairo_new_path(CAIRO(ctx)->cr);
    cairo_save(CAIRO(ctx)->cr);
    cairo_translate(CAIRO(ctx)->cr, arc->cx, arc->cy);
    cairo_scale(CAIRO(ctx)->cr, arc->rx, arc->ry);
    if (arc->extent > 0.0) {
        cairo_arc_negative(CAIRO(ctx)->cr, 0.0, 0.0, 1.0, start, end);
    } else {
        cairo_arc(CAIRO(ctx)->cr, 0.0, 0.0, 1.0, start, end);
    }
    cairo_restore(CAIRO(ctx)->cr);
    if (fabs(arc->extent) == 360.0) {
        cairo_close_path(CAIRO(ctx)->cr);
    } else if (arc->style != RBC_RENDER_ARC_OPEN) {
        if (arc->style == RBC_RENDER_ARC_PIESLICE) {
            cairo_line_to(CAIRO(ctx)->cr, arc->cx, arc->cy);
        }
        cairo_close_path(CAIRO(ctx)->cr);
    }
}

static void CairoArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    CairoArcPath(ctx, arc);
    StrokeRenderPath(ctx);
}

static void CairoFillArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    const Rbc_RenderFillStyle *style = &CAIRO(ctx)->fillStyle;
    const XColor *foreground = style->foreground;

    cairo_save(CAIRO(ctx)->cr);
    /* Match existing area fills and their widget-aligned stipple origin. */
    cairo_translate(CAIRO(ctx)->cr, -0.5, -0.5);
    cairo_set_fill_rule(CAIRO(ctx)->cr, CAIRO_FILL_RULE_EVEN_ODD);
    if (CAIRO(ctx)->bitmapPattern != NULL) {
        cairo_set_source(CAIRO(ctx)->cr, CAIRO(ctx)->bitmapPattern);
    } else if (style->opacity != 1.0) {
        cairo_set_source_rgba(CAIRO(ctx)->cr, foreground->red / 65535.0, foreground->green / 65535.0,
                              foreground->blue / 65535.0, style->opacity);
    }
    CairoArcPath(ctx, arc);
    cairo_fill(CAIRO(ctx)->cr);
    cairo_restore(CAIRO(ctx)->cr);
}

/* Marker options use X cap/join constants; keep them out of Cairo callers. */
static void CairoLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle) {
    cairo_set_line_cap(CAIRO(ctx)->cr, (capStyle == CapRound)        ? CAIRO_LINE_CAP_ROUND
                                       : (capStyle == CapProjecting) ? CAIRO_LINE_CAP_SQUARE
                                                                     : CAIRO_LINE_CAP_BUTT);
    cairo_set_line_join(CAIRO(ctx)->cr, (joinStyle == JoinRound)   ? CAIRO_LINE_JOIN_ROUND
                                        : (joinStyle == JoinBevel) ? CAIRO_LINE_JOIN_BEVEL
                                                                   : CAIRO_LINE_JOIN_MITER);
}

/* Change only the off-dash underlay; retain the configured dash pattern. */
static void CairoDashBackground(Rbc_RenderContext *ctx, const XColor *color) {
    CAIRO(ctx)->doubleDash = (CAIRO(ctx)->nDashes > 0) && (color != NULL);
    if (CAIRO(ctx)->doubleDash) {
        CAIRO(ctx)->offColor = *color;
    }
}

/* Separate subpaths preserve strip-segment boundaries. Bound path storage. */
static void CairoSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        cairo_move_to(CAIRO(ctx)->cr, segments[i].p.x, segments[i].p.y);
        cairo_line_to(CAIRO(ctx)->cr, segments[i].q.x, segments[i].q.y);
        /* Preserve painter order for intersecting two-color segments. */
        if (CAIRO(ctx)->doubleDash || ((i % 8192) == 8191)) {
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

    cairo_save(CAIRO(ctx)->cr);
    cairo_translate(CAIRO(ctx)->cr, -0.5, -0.5);
    cairo_set_fill_rule(CAIRO(ctx)->cr, CAIRO_FILL_RULE_WINDING);
    for (i = 0; i < count; i++) {
        cairo_rectangle(CAIRO(ctx)->cr, (int)points[i].x, (int)points[i].y, 1, 1);
        if ((i % 8192) == 8191) {
            cairo_fill(CAIRO(ctx)->cr);
        }
    }
    cairo_fill(CAIRO(ctx)->cr);
    cairo_restore(CAIRO(ctx)->cr);
}

void Rbc_RenderSymbols(Rbc_RenderContext *ctx, const Rbc_RenderShape *shape, const Point2D *centers, Tcl_Size count,
                       const XColor *fillColor, int outline) {
    Tcl_Size i;

    if ((count <= 0) || ((fillColor == NULL) && !outline)) {
        return;
    }
    cairo_save(CAIRO(ctx)->cr);
    cairo_set_dash(CAIRO(ctx)->cr, NULL, 0, 0.0);
    cairo_set_line_join(CAIRO(ctx)->cr, CAIRO_LINE_JOIN_MITER);
    cairo_set_fill_rule(CAIRO(ctx)->cr, CAIRO_FILL_RULE_WINDING);
    for (i = 0; i < count; i++) {
        AppendRenderSymbol(CAIRO(ctx)->cr, shape, centers + i);
    }
    if ((fillColor != NULL) && (shape->type != RBC_RENDER_SEGMENTS)) {
        SetStrokeColor(CAIRO(ctx)->cr, fillColor);
        cairo_fill_preserve(CAIRO(ctx)->cr);
    }
    if (outline) {
        SetStrokeColor(CAIRO(ctx)->cr, &CAIRO(ctx)->foreground);
        cairo_stroke(CAIRO(ctx)->cr);
    } else {
        cairo_new_path(CAIRO(ctx)->cr);
    }
    cairo_restore(CAIRO(ctx)->cr);
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
                        const XColor *foreground, const XColor *background, Pixmap stipple, double opacity) {
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
    if (opacity != 1.0) {
        cairo_push_group(CAIRO(ctx)->cr);
    }
    cairo_translate(CAIRO(ctx)->cr, -0.5, -0.5);
    if (pattern != NULL) {
        cairo_set_source(CAIRO(ctx)->cr, pattern);
    }
    cairo_rectangle(CAIRO(ctx)->cr, r->x, r->y, r->width, r->height);
    cairo_fill(CAIRO(ctx)->cr);
    if (opacity != 1.0) {
        cairo_pop_group_to_source(CAIRO(ctx)->cr);
        cairo_paint_with_alpha(CAIRO(ctx)->cr, opacity);
    }
    if (pattern != NULL) {
        cairo_pattern_destroy(pattern);
    }
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/* Integer bar edges stay sharp; bound path storage independently of bar count. */
int Rbc_RenderRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *rectangles, Tcl_Size count,
                         const XColor *foreground, const XColor *background, Pixmap stipple, double opacity) {
    Rbc_RenderFillStyle style = {foreground, background, stipple, opacity, FALSE};
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

    cairo_save(CAIRO(ctx)->cr);
    if (CAIRO(ctx)->fillStyle.opacity != 1.0) {
        cairo_push_group(CAIRO(ctx)->cr);
    }
    cairo_translate(CAIRO(ctx)->cr, -0.5, -0.5);
    cairo_set_fill_rule(CAIRO(ctx)->cr, CAIRO_FILL_RULE_WINDING);
    if (CAIRO(ctx)->bitmapPattern != NULL) {
        cairo_set_source(CAIRO(ctx)->cr, CAIRO(ctx)->bitmapPattern);
    }
    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *r = rectangles + i;
        if ((r->width > 0) && (r->height > 0)) {
            cairo_rectangle(CAIRO(ctx)->cr, r->x, r->y, r->width, r->height);
        }
        if ((i + 1) % 8192 == 0) {
            cairo_fill(CAIRO(ctx)->cr);
        }
    }
    cairo_fill(CAIRO(ctx)->cr);
    if (CAIRO(ctx)->fillStyle.opacity != 1.0) {
        cairo_pop_group_to_source(CAIRO(ctx)->cr);
        cairo_paint_with_alpha(CAIRO(ctx)->cr, CAIRO(ctx)->fillStyle.opacity);
    }
    cairo_restore(CAIRO(ctx)->cr);
}

/* Area vertices are boundaries, not the pixel centers used by strokes. */
static void FillRenderArea(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count, cairo_pattern_t *pattern) {
    Tcl_Size i;

    cairo_translate(CAIRO(ctx)->cr, -0.5, -0.5);
    cairo_set_fill_rule(CAIRO(ctx)->cr, CAIRO_FILL_RULE_EVEN_ODD);
    if (pattern != NULL) {
        cairo_set_source(CAIRO(ctx)->cr, pattern);
    }
    cairo_move_to(CAIRO(ctx)->cr, points[0].x, points[0].y);
    for (i = 1; i < count; i++) {
        cairo_line_to(CAIRO(ctx)->cr, points[i].x, points[i].y);
    }
    cairo_close_path(CAIRO(ctx)->cr);
    cairo_fill(CAIRO(ctx)->cr);
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
    CAIRO(ctx)->bitmapPattern = pattern;
    CAIRO(ctx)->bitmapWidth = width;
    CAIRO(ctx)->bitmapHeight = height;
    cairo_translate(CAIRO(ctx)->cr, -0.5, -0.5);
    return ctx;
}

/* Paint in source order without repeating or resampling bitmap pixels. */
void Rbc_RenderBitmapSymbols(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count) {
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        int x = (int)centers[i].x - CAIRO(ctx)->bitmapWidth / 2;
        int y = (int)centers[i].y - CAIRO(ctx)->bitmapHeight / 2;
        cairo_save(CAIRO(ctx)->cr);
        cairo_translate(CAIRO(ctx)->cr, x, y);
        cairo_set_source(CAIRO(ctx)->cr, CAIRO(ctx)->bitmapPattern);
        cairo_rectangle(CAIRO(ctx)->cr, 0, 0, CAIRO(ctx)->bitmapWidth, CAIRO(ctx)->bitmapHeight);
        cairo_fill(CAIRO(ctx)->cr);
        cairo_restore(CAIRO(ctx)->cr);
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
        SetStrokeColor(CAIRO(ctx)->cr, background);
        FillRenderArea(ctx, polygon, nPoints, NULL);
    } else {
        cairo_translate(CAIRO(ctx)->cr, -0.5, -0.5);
    }
    cairo_matrix_init_translate(&matrix, -(double)r->x, -(double)r->y);
    cairo_pattern_set_matrix(pattern, &matrix);
    cairo_set_source(CAIRO(ctx)->cr, pattern);
    cairo_rectangle(CAIRO(ctx)->cr, r->x, r->y, r->width, r->height);
    cairo_fill(CAIRO(ctx)->cr);
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
    CAIRO(ctx)->fillStyle = *style;
    CAIRO(ctx)->bitmapPattern = pattern;
    return ctx;
}

static void CairoFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    const Rbc_RenderFillStyle *style = &CAIRO(ctx)->fillStyle;
    const XColor *foreground = style->foreground;

    cairo_save(CAIRO(ctx)->cr);
    if ((style->stipple == None) && (style->opacity != 1.0)) {
        cairo_set_source_rgba(CAIRO(ctx)->cr, foreground->red / 65535.0, foreground->green / 65535.0,
                              foreground->blue / 65535.0, style->opacity);
    }
    FillRenderArea(ctx, points, count, CAIRO(ctx)->bitmapPattern);
    cairo_restore(CAIRO(ctx)->cr);
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
    cairo_translate(CAIRO(ctx)->cr, -0.5, -0.5);
    cairo_set_source(CAIRO(ctx)->cr, pattern);
    cairo_rectangle(CAIRO(ctx)->cr, x, y, block->width, block->height);
    cairo_fill(CAIRO(ctx)->cr);
    cairo_pattern_destroy(pattern);
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/* Flush before any native drawing resumes on the same drawable. */
static void CairoEnd(Rbc_RenderContext *ctx) {
    cairo_status_t status;
    Tcl_Interp *interp = ctx->graphPtr->interp;

    cairo_surface_flush(CAIRO(ctx)->surface);
    status = cairo_status(CAIRO(ctx)->cr);
    if (status == CAIRO_STATUS_SUCCESS) {
        status = cairo_surface_status(CAIRO(ctx)->surface);
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
                        const XColor *foreground, const XColor *background, Pixmap stipple, double opacity) {
    (void)graphPtr;
    (void)drawable;
    (void)width;
    (void)height;
    (void)r;
    (void)foreground;
    (void)background;
    (void)stipple;
    (void)opacity;
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
                         const XColor *foreground, const XColor *background, Pixmap stipple, double opacity) {
    (void)graphPtr;
    (void)drawable;
    (void)rectangles;
    (void)count;
    (void)foreground;
    (void)background;
    (void)stipple;
    (void)opacity;
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
