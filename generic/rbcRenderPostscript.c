/* Postscript renderer. See license.terms. */
#include "rbcRenderExportInt.h"

typedef struct {
    Rbc_RenderExportState exportState;
    PsToken psToken;
    int postScriptDashed;
    const char *symbolMacro;
} PostscriptRenderContext;

#define POSTSCRIPT(ctx) ((PostscriptRenderContext *)(ctx))

static void PostScriptClipBegin(Rbc_RenderContext *ctx, int x, int y, int width, int height) {
    Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr,
                     "gsave newpath %d %d moveto %d 0 rlineto 0 %d rlineto %d 0 rlineto closepath clip newpath\n", x, y,
                     width, height, -width);
}

static void PostScriptClipEnd(Rbc_RenderContext *ctx) {
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "grestore\n", (char *)NULL);
}

/* The existing PostScript emitter remains responsible for PS syntax and maps. */
static void PostScriptPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Tcl_Size i;
    int components;

    if (count < 2) {
        return;
    }
    Rbc_FormatToPostScript(POSTSCRIPT(ctx)->psToken, " newpath %g %g moveto\n", points[0].x, points[0].y);
    components = 1;
    for (i = 1; i < count - 1; i++) {
        Rbc_FormatToPostScript(POSTSCRIPT(ctx)->psToken, " %g %g lineto\n", points[i].x, points[i].y);
        components++;
        /* Keep the legacy level-1 path limit and restart at the shared vertex. */
        if (components >= 1500) {
            Rbc_FormatToPostScript(POSTSCRIPT(ctx)->psToken, "DashesProc stroke\n newpath %g %g moveto\n", points[i].x,
                                   points[i].y);
            components = 1;
        }
    }
    Rbc_FormatToPostScript(POSTSCRIPT(ctx)->psToken, " %g %g lineto\n", points[i].x, points[i].y);
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "DashesProc stroke\n", (char *)NULL);
}

static void PostScriptSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    if (count > 0) {
        Rbc_2DSegmentsToPostScript(POSTSCRIPT(ctx)->psToken, (Segment2D *)segments, count);
    }
}

static void PostScriptLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle) {
    Rbc_CapStyleToPostScript(POSTSCRIPT(ctx)->psToken, capStyle);
    Rbc_JoinStyleToPostScript(POSTSCRIPT(ctx)->psToken, joinStyle);
}

static void PostScriptDashBackground(Rbc_RenderContext *ctx, const XColor *color) {
    if (POSTSCRIPT(ctx)->postScriptDashed && (color != NULL)) {
        Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "/DashesProc {\n  gsave\n    ", (char *)NULL);
        Rbc_BackgroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)color);
        Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "    ", (char *)NULL);
        Rbc_LineDashesToPostScript(POSTSCRIPT(ctx)->psToken, NULL);
        Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "stroke\n  grestore\n} def\n", (char *)NULL);
    } else {
        Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "/DashesProc {} def\n", (char *)NULL);
    }
}

static void PostScriptFillPath(Rbc_RenderContext *ctx) {
    const Rbc_RenderFillStyle *style = &RENDER_EXPORT(ctx)->fillStyle;

    if (style->background != NULL) {
        Rbc_BackgroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)style->background);
        Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "Fill\n", (char *)NULL);
    }
    Rbc_ForegroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)style->foreground);
    if (style->backgroundOnly) {
        return;
    }
    if (style->stipple != None) {
        Rbc_StippleToPostScript(POSTSCRIPT(ctx)->psToken, ctx->graphPtr->display, style->stipple);
    } else {
        Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "Fill\n", (char *)NULL);
    }
}

static void PostScriptFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Rbc_PathToPostScript(POSTSCRIPT(ctx)->psToken, (Point2D *)points, count);
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "closepath\n", (char *)NULL);
    PostScriptFillPath(ctx);
}

/* The current path survives setmatrix, unlike grestore. Save the CTM on the
 * operand stack while constructing the unit-circle arc, then restore it before
 * stroking/filling so widths, dash lengths and stipples stay in display units. */
static void PostScriptArcPath(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    Rbc_FormatToPostScript(POSTSCRIPT(ctx)->psToken,
                           "newpath matrix currentmatrix\n%g %g translate %g %g scale\n0 0 1 %g %g %s\nsetmatrix\n",
                           arc->cx, arc->cy, arc->rx, arc->ry, -arc->start, -(arc->start + arc->extent),
                           arc->extent > 0.0 ? "arcn" : "arc");
    if (fabs(arc->extent) == 360.0) {
        Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "closepath\n", (char *)NULL);
    } else if (arc->style != RBC_RENDER_ARC_OPEN) {
        if (arc->style == RBC_RENDER_ARC_PIESLICE) {
            Rbc_FormatToPostScript(POSTSCRIPT(ctx)->psToken, "%g %g lineto\n", arc->cx, arc->cy);
        }
        Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "closepath\n", (char *)NULL);
    }
}

static void PostScriptArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    PostScriptArcPath(ctx, arc);
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "DashesProc stroke\n", (char *)NULL);
}

static void PostScriptFillArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    PostScriptArcPath(ctx, arc);
    PostScriptFillPath(ctx);
}

static void PostScriptFillRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rectangles, Tcl_Size count) {
    const Rbc_RenderFillStyle *style = &RENDER_EXPORT(ctx)->fillStyle;
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *r = rectangles + i;
        if (style->stipple != None) {
            Rbc_RegionToPostScript(POSTSCRIPT(ctx)->psToken, (double)r->x, (double)r->y, r->width, r->height);
            if (style->background != NULL) {
                Rbc_BackgroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)style->background);
                Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "Fill\n", (char *)NULL);
            }
            Rbc_ForegroundToPostScript(POSTSCRIPT(ctx)->psToken,
                                       (XColor *)((style->foreground != NULL) ? style->foreground : style->background));
            Rbc_StippleToPostScript(POSTSCRIPT(ctx)->psToken, ctx->graphPtr->display, style->stipple);
        } else if (style->foreground != NULL) {
            Rbc_ForegroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)style->foreground);
            Rbc_RectangleToPostScript(POSTSCRIPT(ctx)->psToken, (double)r->x, (double)r->y, r->width, r->height);
        }
    }
}

static void PostScriptSymbolPoints(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count) {
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        Rbc_FormatToPostScript(POSTSCRIPT(ctx)->psToken, "%g %g %g %s\n", centers[i].x, centers[i].y,
                               RENDER_EXPORT(ctx)->symbolSize, POSTSCRIPT(ctx)->symbolMacro);
    }
}

static void PostScriptText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y) {
    Rbc_TextToPostScript(POSTSCRIPT(ctx)->psToken, string, style, x, y);
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
        Rbc_PhotoToPostScript(POSTSCRIPT(ctx)->psToken, photo, x, y);
    }
}

static void PostScriptWindow(Rbc_RenderContext *ctx, Tk_Window tkwin, double x, double y) {
    Rbc_WindowToPostScript(POSTSCRIPT(ctx)->psToken, tkwin, x, y);
}

static void PostScriptBackgroundPolygon(Rbc_RenderContext *ctx, const XColor *color, const Point2D *points,
                                        Tcl_Size count) {
    Rbc_BackgroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)color);
    Rbc_PolygonToPostScript(POSTSCRIPT(ctx)->psToken, (Point2D *)points, count);
}

static void PostScriptBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y, int width, int height,
                             int borderWidth, int relief, int fill) {
    if (fill) {
        Rbc_Fill3DRectangleToPostScript(POSTSCRIPT(ctx)->psToken, border, x, y, width, height, borderWidth, relief);
    } else {
        Rbc_Draw3DRectangleToPostScript(POSTSCRIPT(ctx)->psToken, border, x, y, width, height, borderWidth, relief);
    }
}

static void PostScriptClearRectangle(Rbc_RenderContext *ctx, double x, double y, int width, int height) {
    Rbc_ClearBackgroundToPostScript(POSTSCRIPT(ctx)->psToken);
    Rbc_RectangleToPostScript(POSTSCRIPT(ctx)->psToken, x, y, width, height);
}

static void PostScriptBackgroundRectangles(Rbc_RenderContext *ctx, const XColor *color,
                                           const Rbc_RenderRectangle *rectangles, Tcl_Size count) {
    Tcl_Size i;

    if (color != NULL) {
        Rbc_BackgroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)color);
    } else {
        Rbc_ClearBackgroundToPostScript(POSTSCRIPT(ctx)->psToken);
    }
    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *rect = rectangles + i;
        Rbc_RectangleToPostScript(POSTSCRIPT(ctx)->psToken, rect->x, rect->y, rect->width, rect->height);
    }
}

static void PostScriptPlotBegin(Rbc_RenderContext *ctx, Tk_Font font, double x, double y, int width, int height,
                                const XColor *background) {
    Rbc_FontToPostScript(POSTSCRIPT(ctx)->psToken, font);
    Rbc_RegionToPostScript(POSTSCRIPT(ctx)->psToken, x, y, width, height);
    if (background != NULL) {
        Rbc_BackgroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)background);
    } else {
        Rbc_ClearBackgroundToPostScript(POSTSCRIPT(ctx)->psToken);
    }
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "Fill\n", "gsave clip\n\n", (char *)NULL);
}

static void PostScriptPlotEnd(Rbc_RenderContext *ctx) {
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "\n", "% Unset clipping\n", "grestore\n\n", (char *)NULL);
}

static void PostScriptBitmapMask(Rbc_RenderContext *ctx, Display *display, Pixmap bitmap, double x, double y, int width,
                                 int height, const XColor *color, int background) {
    if ((bitmap == None) || (width < 1) || (height < 1)) {
        return;
    }
    if (background) {
        Rbc_BackgroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)color);
    } else {
        Rbc_ForegroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)color);
    }
    Rbc_FormatToPostScript(POSTSCRIPT(ctx)->psToken,
                           " gsave\n"
                           " %g %g translate\n"
                           " %d %d scale\n",
                           x, y + height, width, -height);
    Rbc_FormatToPostScript(POSTSCRIPT(ctx)->psToken, " %d %d true [%d 0 0 %d 0 %d] {", width, height, width, -height,
                           height);
    Rbc_BitmapDataToPostScript(POSTSCRIPT(ctx)->psToken, display, bitmap, width, height);
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, " } imagemask\n", " grestore\n", (char *)NULL);
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
                                                        PostScriptTkImage,
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        NULL,
                                                        NULL};

static const Rbc_RenderOps rbcPostScriptOps = {PostScriptPolyline,     PostScriptSegments,
                                               PostScriptLineStyle,    PostScriptDashBackground,
                                               PostScriptFillPolygon,  PostScriptFillRectangles,
                                               PostScriptSymbolPoints, &postScriptOutputOps,
                                               Rbc_RenderExportEnd,    PostScriptArc,
                                               PostScriptFillArc,      PostScriptClipBegin,
                                               PostScriptClipEnd};

void Rbc_RenderPostscriptSymbol(Rbc_RenderContext *ctx, const Rbc_RenderSymbolStyle *style) {
    static const char *macros[] = {"Sq", "Ci", "Di", "Pl", "Cr", "Sp", "Sc", "Tr", "Ar", "Bm"};
    Graph *graphPtr = ctx->graphPtr;
    XColor *outlineColor = (XColor *)style->outlineColor;
    XColor *fillColor = (XColor *)style->fillColor;
    POSTSCRIPT(ctx)->symbolMacro = macros[style->type];
    RENDER_EXPORT(ctx)->symbolSize = (double)style->size;
    switch (style->type) {
    case RBC_RENDER_SYMBOL_SQUARE:
    case RBC_RENDER_SYMBOL_CROSS:
    case RBC_RENDER_SYMBOL_PLUS:
    case RBC_RENDER_SYMBOL_SCROSS:
    case RBC_RENDER_SYMBOL_SPLUS:
        RENDER_EXPORT(ctx)->symbolSize = (double)Rbc_RenderExportSymbolRound(style->size * 0.886226925452758);
        break;
    case RBC_RENDER_SYMBOL_TRIANGLE:
    case RBC_RENDER_SYMBOL_ARROW:
        RENDER_EXPORT(ctx)->symbolSize = (double)Rbc_RenderExportSymbolRound(style->size * 0.7);
        break;
    case RBC_RENDER_SYMBOL_DIAMOND:
        RENDER_EXPORT(ctx)->symbolSize = (double)Rbc_RenderExportSymbolRound(style->size * M_SQRT1_2);
        break;
    default:
        break;
    }
    Rbc_LineWidthToPostScript(POSTSCRIPT(ctx)->psToken, style->outlineWidth);
    Rbc_LineDashesToPostScript(POSTSCRIPT(ctx)->psToken, (Rbc_Dashes *)NULL);
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "\n/DrawSymbolProc {\n", (char *)NULL);
    if (style->type == RBC_RENDER_SYMBOL_BITMAP) {
        int width, height;
        double sx, sy, scale;

        Tk_SizeOfBitmap(graphPtr->display, style->bitmap, &width, &height);
        sx = (double)style->size / (double)width;
        sy = (double)style->size / (double)height;
        scale = MIN(sx, sy);
        if ((style->mask != None) && (fillColor != NULL)) {
            Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "\n  % Bitmap mask is \"",
                                   Tk_NameOfBitmap(graphPtr->display, style->mask), "\"\n\n  ", (char *)NULL);
            Rbc_BackgroundToPostScript(POSTSCRIPT(ctx)->psToken, fillColor);
            Rbc_BitmapToPostScript(POSTSCRIPT(ctx)->psToken, graphPtr->display, style->mask, scale, scale);
        }
        Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "\n  % Bitmap symbol is \"",
                               Tk_NameOfBitmap(graphPtr->display, style->bitmap), "\"\n\n  ", (char *)NULL);
        Rbc_ForegroundToPostScript(POSTSCRIPT(ctx)->psToken, outlineColor);
        Rbc_BitmapToPostScript(POSTSCRIPT(ctx)->psToken, graphPtr->display, style->bitmap, scale, scale);
    } else {
        if (fillColor != NULL) {
            Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "  ", (char *)NULL);
            Rbc_BackgroundToPostScript(POSTSCRIPT(ctx)->psToken, fillColor);
            Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "  Fill\n", (char *)NULL);
        }
        if ((outlineColor != NULL) && (style->outlineWidth > 0)) {
            Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "  ", (char *)NULL);
            Rbc_ForegroundToPostScript(POSTSCRIPT(ctx)->psToken, outlineColor);
            Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "  stroke\n", (char *)NULL);
        }
    }
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "} def\n\n", (char *)NULL);
}

void Rbc_RenderPostscriptBarSymbol(Rbc_RenderContext *ctx, const Rbc_RenderFillStyle *style, int size) {
    Graph *graphPtr = ctx->graphPtr;
    POSTSCRIPT(ctx)->symbolMacro = "Sq";
    RENDER_EXPORT(ctx)->symbolSize = (double)size;
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "\n", "/DrawSymbolProc {\n", "  gsave\n    ", (char *)NULL);
    if (style->stipple != None) {
        if (style->background != NULL) {
            Rbc_BackgroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)style->background);
            Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "    Fill\n    ", (char *)NULL);
        }
        Rbc_ForegroundToPostScript(POSTSCRIPT(ctx)->psToken,
                                   (XColor *)((style->foreground != NULL) ? style->foreground : style->background));
        Rbc_StippleToPostScript(POSTSCRIPT(ctx)->psToken, graphPtr->display, style->stipple);
    } else if (style->foreground != NULL) {
        Rbc_ForegroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)style->foreground);
        Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "    fill\n", (char *)NULL);
    }
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "  grestore\n", (char *)NULL);
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "} def\n\n", (char *)NULL);
}

void Rbc_RenderPostscriptStroke(Rbc_RenderContext *ctx, const XColor *color, int lineWidth, const Rbc_Dashes *dashes,
                                int capStyle, int joinStyle) {
    POSTSCRIPT(ctx)->postScriptDashed = (dashes != NULL) && (dashes->values[0] != 0);
    Rbc_LineAttributesToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)color, lineWidth, (Rbc_Dashes *)dashes, capStyle,
                                   joinStyle);
}

Rbc_RenderContext *Rbc_RenderCreatePostscript(Rbc_ExportContext *token) {
    PostscriptRenderContext *ctx = ckalloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    ctx->exportState.core.ops = &rbcPostScriptOps;
    ctx->exportState.exportPtr = token;
    ctx->psToken = (PsToken)token->backendData;
    return &ctx->exportState.core;
}
