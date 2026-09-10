/*
 * rbcRender.c -- Optional stroke renderer for mapped graph geometry.
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
Rbc_RenderContext *Rbc_RenderBegin(Graph *graphPtr, Drawable drawable,
                                   const XColor *colorPtr, double width,
                                   const Rbc_Dashes *dashesPtr, const XColor *offColorPtr) {
    Rbc_RenderContext *ctx;

    if ((graphPtr->renderer != RBC_RENDERER_CAIRO) || (drawable == None) ||
        (colorPtr == NULL) || !FINITE(width) || (width <= 0.0) ||
        (graphPtr->width <= 0) || (graphPtr->height <= 0)) {
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
        Tk_Visual(graphPtr->tkwin), graphPtr->width, graphPtr->height);
#endif
    if (cairo_surface_status(ctx->surface) != CAIRO_STATUS_SUCCESS) {
        goto fail;
    }
    ctx->cr = cairo_create(ctx->surface);
    cairo_rectangle(ctx->cr, graphPtr->left, graphPtr->top,
        MAX(0.0, (double)graphPtr->right - graphPtr->left + 1.0),
        MAX(0.0, (double)graphPtr->bottom - graphPtr->top + 1.0));
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
void Rbc_RenderEnd(Rbc_RenderContext *ctx) {
    (void)ctx;
}
#endif
