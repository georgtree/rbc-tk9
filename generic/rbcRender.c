/*
 * rbcRender.c -- Optional renderer for mapped graph geometry.
 * See license.terms for details.
 */
#include "rbcRender.h"

#ifdef RBC_HAVE_CAIRO
#include <cairo.h>
#ifdef WIN32
#include <cairo-win32.h>
#else
#include <cairo-xlib.h>
#endif

struct Rbc_RenderContext {
    Graph *graphPtr;
    cairo_surface_t *surface;
    cairo_t *cr;
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
};

static void FreeRenderContext(Rbc_RenderContext *ctx) {
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
    case RBC_ANTIALIAS_NONE: return CAIRO_ANTIALIAS_NONE;
    case RBC_ANTIALIAS_GRAY: return CAIRO_ANTIALIAS_GRAY;
    case RBC_ANTIALIAS_FAST: return CAIRO_ANTIALIAS_FAST;
    case RBC_ANTIALIAS_GOOD: return CAIRO_ANTIALIAS_GOOD;
    case RBC_ANTIALIAS_BEST: return CAIRO_ANTIALIAS_BEST;
    default: return CAIRO_ANTIALIAS_DEFAULT;
    }
}

static void SetStrokeColor(cairo_t *cr, const XColor *colorPtr) {
    cairo_set_source_rgb(cr, colorPtr->red / 65535.0,
        colorPtr->green / 65535.0, colorPtr->blue / 65535.0);
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

/* Open one uninterrupted Cairo drawing batch; NULL requests native drawing. */
static Rbc_RenderContext *BeginRenderTarget(Graph *graphPtr, Drawable drawable,
                                   const XColor *colorPtr, double width,
                                   const Rbc_Dashes *dashesPtr, const XColor *offColorPtr,
                                   int targetWidth, int targetHeight, int left, int top, int right, int bottom) {
    Rbc_RenderContext *ctx;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (drawable == None) ||
        (colorPtr == NULL) || !FINITE(width) || (width <= 0.0) ||
        (targetWidth <= 0) || (targetHeight <= 0)) {
        return NULL;
    }
    ctx = Tcl_AttemptAlloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->graphPtr = graphPtr;
    ctx->foreground = *colorPtr;
    if (dashesPtr != NULL) {
        while ((ctx->nDashes < RBC_MAX_DASH_VALUES) &&
               (dashesPtr->values[ctx->nDashes] != 0)) {
            ctx->dashes[ctx->nDashes] = dashesPtr->values[ctx->nDashes];
            ctx->nDashes++;
        }
        ctx->dashOffset = dashesPtr->offset;
    }
    if ((ctx->nDashes > 0) && (offColorPtr != NULL)) {
        if ((offColorPtr->red == colorPtr->red) &&
            (offColorPtr->green == colorPtr->green) &&
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
#else
    ctx->surface = cairo_xlib_surface_create(graphPtr->display, drawable,
        Tk_Visual(graphPtr->tkwin), targetWidth, targetHeight);
#endif
    if (cairo_surface_status(ctx->surface) != CAIRO_STATUS_SUCCESS) {
        goto fail;
    }
    ctx->cr = cairo_create(ctx->surface);
    cairo_rectangle(ctx->cr, left, top,
        MAX(0.0, (double)right - left + 1.0),
        MAX(0.0, (double)bottom - top + 1.0));
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

Rbc_RenderContext *Rbc_RenderBegin(Graph *graphPtr, Drawable drawable,
                                   const XColor *colorPtr, double width,
                                   const Rbc_Dashes *dashesPtr, const XColor *offColorPtr) {
    return BeginRenderTarget(graphPtr, drawable, colorPtr, width, dashesPtr, offColorPtr,
        graphPtr->width, graphPtr->height, graphPtr->left, graphPtr->top, graphPtr->right, graphPtr->bottom);
}

/* Draw in full drawable coordinates, outside the plot-area clip. */
Rbc_RenderContext *Rbc_RenderBeginDrawable(Graph *graphPtr, Drawable drawable, int width, int height,
                                         const XColor *color, double lineWidth,
                                         const Rbc_Dashes *dashes, const XColor *offColor) {
    if ((width <= 0) || (height <= 0)) return NULL;
    return BeginRenderTarget(graphPtr, drawable, color, lineWidth, dashes, offColor,
        width, height, 0, 0, width - 1, height - 1);
}

/* Each call is a separate trace; preserve joins within that trace. */
void Rbc_RenderPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
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
void Rbc_RenderLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle) {
    cairo_set_line_cap(ctx->cr, (capStyle == CapRound) ? CAIRO_LINE_CAP_ROUND :
        (capStyle == CapProjecting) ? CAIRO_LINE_CAP_SQUARE : CAIRO_LINE_CAP_BUTT);
    cairo_set_line_join(ctx->cr, (joinStyle == JoinRound) ? CAIRO_LINE_JOIN_ROUND :
        (joinStyle == JoinBevel) ? CAIRO_LINE_JOIN_BEVEL : CAIRO_LINE_JOIN_MITER);
}

/* Separate subpaths preserve strip-segment boundaries. Bound path storage. */
void Rbc_RenderSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
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
            cairo_line_to(cr, center->x + shape->points[i+1].x, center->y + shape->points[i+1].y);
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
void Rbc_RenderSymbols(Rbc_RenderContext *ctx, const Rbc_RenderShape *shape,
                       const Point2D *centers, Tcl_Size count, const XColor *fillColor, int outline) {
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
    return 0xff000000u | (((uint32_t)color->red + 128u) / 257u << 16) |
        (((uint32_t)color->green + 128u) / 257u << 8) | ((uint32_t)color->blue + 128u) / 257u;
}

/* Read the native bitmap before acquiring the destination drawing context. */
static cairo_pattern_t *CreateRenderStipple(Graph *graphPtr, Pixmap stipple,
                                           const XColor *foreground, const XColor *background) {
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;
    XImage *image;
    unsigned char *data;
    uint32_t fg = RenderAreaPixel(foreground), bg = RenderAreaPixel(background);
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
int Rbc_RenderLegendBar(Graph *graphPtr, Drawable drawable, int width, int height,
                        const Rbc_RenderRectangle *r, const XColor *foreground,
                        const XColor *background, Pixmap stipple) {
    Rbc_RenderContext *ctx;
    cairo_pattern_t *pattern = NULL;
    cairo_matrix_t matrix;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (foreground == NULL)) return FALSE;
    if ((r->width <= 0) || (r->height <= 0)) return TRUE;
    if (stipple != None) {
        pattern = CreateRenderStipple(graphPtr, stipple, foreground, background);
        if (pattern == NULL) return FALSE;
        cairo_matrix_init_translate(&matrix, -(double)r->x, -(double)r->y);
        cairo_pattern_set_matrix(pattern, &matrix);
    }
    ctx = Rbc_RenderBeginDrawable(graphPtr, drawable, width, height, foreground, 1.0, NULL, NULL);
    if (ctx == NULL) {
        if (pattern != NULL) cairo_pattern_destroy(pattern);
        return FALSE;
    }
    cairo_translate(ctx->cr, -0.5, -0.5);
    if (pattern != NULL) cairo_set_source(ctx->cr, pattern);
    cairo_rectangle(ctx->cr, r->x, r->y, r->width, r->height);
    cairo_fill(ctx->cr);
    if (pattern != NULL) cairo_pattern_destroy(pattern);
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/* Integer bar edges stay sharp; bound path storage independently of bar count. */
int Rbc_RenderRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *rectangles,
                         Tcl_Size count, const XColor *foreground, const XColor *background, Pixmap stipple) {
    Rbc_RenderContext *ctx;
    cairo_pattern_t *pattern = NULL;
    Tcl_Size i;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (foreground == NULL) || (count <= 0)) {
        return FALSE;
    }
    if (stipple != None) {
        pattern = CreateRenderStipple(graphPtr, stipple, foreground, background);
        if (pattern == NULL) {
            return FALSE;
        }
    }
    ctx = Rbc_RenderBegin(graphPtr, drawable, foreground, 1.0, NULL, NULL);
    if (ctx == NULL) {
        if (pattern != NULL) cairo_pattern_destroy(pattern);
        return FALSE;
    }
    cairo_translate(ctx->cr, -0.5, -0.5);
    cairo_set_fill_rule(ctx->cr, CAIRO_FILL_RULE_WINDING);
    if (pattern != NULL) cairo_set_source(ctx->cr, pattern);
    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *r = rectangles + i;

        if ((r->width > 0) && (r->height > 0)) {
            cairo_rectangle(ctx->cr, r->x, r->y, r->width, r->height);
        }
        if ((i + 1) % 8192 == 0) cairo_fill(ctx->cr);
    }
    cairo_fill(ctx->cr);
    if (pattern != NULL) cairo_pattern_destroy(pattern);
    Rbc_RenderEnd(ctx);
    return TRUE;
}

/* Area vertices are boundaries, not the pixel centers used by strokes. */
static void FillRenderArea(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count,
                           cairo_pattern_t *pattern) {
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
static cairo_pattern_t *CreateRenderBitmap(Graph *graphPtr, Pixmap bitmap, Pixmap mask,
                                            int width, int height, const XColor *foreground,
                                            const XColor *background) {
    XImage *bits, *maskBits = NULL;
    cairo_surface_t *surface;
    cairo_pattern_t *pattern;
    unsigned char *data;
    int x, y, stride;
    uint32_t fg = RenderAreaPixel(foreground), bg = RenderAreaPixel(background);

    bits = XGetImage(graphPtr->display, bitmap, 0, 0, width, height, 1, XYPixmap);
    if (bits == NULL) return NULL;
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
        if ((maskBits != NULL) && (maskBits != bits)) XDestroyImage(maskBits);
        XDestroyImage(bits);
        return NULL;
    }
    cairo_surface_flush(surface);
    data = cairo_image_surface_get_data(surface);
    stride = cairo_image_surface_get_stride(surface);
    for (y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(data + (size_t)y * stride);
        for (x = 0; x < width; x++) {
            row[x] = ((maskBits != NULL) && !XGetPixel(maskBits, x, y)) ? 0 :
                (XGetPixel(bits, x, y) ? fg : bg);
        }
    }
    if ((maskBits != NULL) && (maskBits != bits)) XDestroyImage(maskBits);
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

/* Prepare all bitmap resources before drawing the optional rotated background. */
int Rbc_RenderBitmap(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *r,
                      Pixmap bitmap, Pixmap mask, const XColor *foreground, const XColor *background,
                      const Point2D *polygon, Tcl_Size nPoints) {
    cairo_pattern_t *pattern;
    cairo_matrix_t matrix;
    Rbc_RenderContext *ctx;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (bitmap == None) || (foreground == NULL)) return FALSE;
    if ((r->width <= 0) || (r->height <= 0)) return TRUE;
    pattern = CreateRenderBitmap(graphPtr, bitmap, mask, r->width, r->height,
        foreground, (nPoints >= 3) ? NULL : background);
    if (pattern == NULL) return FALSE;
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
int Rbc_RenderArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                   const XColor *foreground, const XColor *background, Pixmap stipple) {
    Rbc_RenderContext *ctx;
    cairo_pattern_t *pattern = NULL;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (foreground == NULL) || (count < 3)) {
        return FALSE;
    }
    if (stipple != None) {
        pattern = CreateRenderStipple(graphPtr, stipple, foreground, background);
        if (pattern == NULL) {
            return FALSE;
        }
    }
    ctx = Rbc_RenderBegin(graphPtr, drawable, foreground, 1.0, NULL, NULL);
    if (ctx == NULL) {
        if (pattern != NULL) {
            cairo_pattern_destroy(pattern);
        }
        return FALSE;
    }
    FillRenderArea(ctx, points, count, pattern);
    if (pattern != NULL) {
        cairo_pattern_destroy(pattern);
    }
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
void Rbc_RenderEnd(Rbc_RenderContext *ctx) {
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
        Tcl_SetObjResult(interp, Tcl_ObjPrintf("Cairo rendering failed: %s",
            cairo_status_to_string(status)));
        Tcl_BackgroundException(interp, TCL_ERROR);
        Tcl_RestoreInterpState(interp, saved);
    }
}
#else
Rbc_RenderContext *Rbc_RenderBeginDrawable(Graph *graphPtr, Drawable drawable, int width, int height,
                                         const XColor *color, double lineWidth,
                                         const Rbc_Dashes *dashes, const XColor *offColor) {
    (void)graphPtr; (void)drawable; (void)width; (void)height;
    (void)color; (void)lineWidth; (void)dashes; (void)offColor;
    return NULL;
}
int Rbc_RenderLegendBar(Graph *graphPtr, Drawable drawable, int width, int height,
                        const Rbc_RenderRectangle *r, const XColor *foreground,
                        const XColor *background, Pixmap stipple) {
    (void)graphPtr; (void)drawable; (void)width; (void)height; (void)r;
    (void)foreground; (void)background; (void)stipple;
    return FALSE;
}
Rbc_RenderContext *Rbc_RenderBegin(Graph *graphPtr, Drawable drawable,
                                   const XColor *colorPtr, double width,
                                   const Rbc_Dashes *dashesPtr, const XColor *offColorPtr) {
    (void)graphPtr; (void)drawable; (void)colorPtr; (void)width;
    (void)dashesPtr; (void)offColorPtr;
    return NULL;
}
void Rbc_RenderPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    (void)ctx; (void)points; (void)count;
}
void Rbc_RenderSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    (void)ctx; (void)segments; (void)count;
}
void Rbc_RenderLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle) {
    (void)ctx; (void)capStyle; (void)joinStyle;
}
void Rbc_RenderSymbols(Rbc_RenderContext *ctx, const Rbc_RenderShape *shape,
                       const Point2D *centers, Tcl_Size count, const XColor *fillColor, int outline) {
    (void)ctx; (void)shape; (void)centers; (void)count; (void)fillColor; (void)outline;
}
int Rbc_RenderRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *rectangles,
                         Tcl_Size count, const XColor *foreground, const XColor *background, Pixmap stipple) {
    (void)graphPtr; (void)drawable; (void)rectangles; (void)count;
    (void)foreground; (void)background; (void)stipple;
    return FALSE;
}
int Rbc_RenderBitmap(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *r,
                      Pixmap bitmap, Pixmap mask, const XColor *foreground, const XColor *background,
                      const Point2D *polygon, Tcl_Size nPoints) {
    (void)graphPtr; (void)drawable; (void)r; (void)bitmap; (void)mask;
    (void)foreground; (void)background; (void)polygon; (void)nPoints;
    return FALSE;
}
int Rbc_RenderPhoto(Graph *graphPtr, Drawable drawable, const Tk_PhotoImageBlock *block, int x, int y) {
    (void)graphPtr; (void)drawable; (void)block; (void)x; (void)y;
    return FALSE;
}
void Rbc_RenderEnd(Rbc_RenderContext *ctx) {
    (void)ctx;
}
int Rbc_RenderArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                   const XColor *foreground, const XColor *background, Pixmap stipple) {
    (void)graphPtr; (void)drawable; (void)points; (void)count;
    (void)foreground; (void)background; (void)stipple;
    return FALSE;
}
int Rbc_RenderTileArea(Graph *graphPtr, Drawable drawable, const Point2D *points, Tcl_Size count,
                       const Tk_PhotoImageBlock *block) {
    (void)graphPtr; (void)drawable; (void)points; (void)count; (void)block;
    return FALSE;
}
#endif
