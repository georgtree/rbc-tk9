/* Postscript renderer. See license.terms. */
/** @file rbcRenderPostscript.c
 * PostScript drawing backend.
 */
/** @defgroup postscript_backend PostScript drawing backend
 * Private renderer callbacks; components use the common renderer interface.
 */
#include "rbcRenderExportInt.h"

typedef struct {
    Rbc_RenderExportState exportState;
    PsToken psToken;
    int postScriptDashed;
    const char *symbolMacro;
} PostscriptRenderContext;

#define POSTSCRIPT(ctx) ((PostscriptRenderContext *)(ctx))

/**
 * @brief Open a saved rectangular PostScript clip.
 * @ingroup postscript_backend
 *
 * Emits gsave and a closed path followed by clip/newpath. Common renderer depth tracking owns pairing;
 * PostScriptClipEnd closes this scope.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Mapped rectangle width in display pixels.
 * @param[in] height Mapped rectangle height in display pixels.
 */
static void PostScriptClipBegin(Rbc_RenderContext *ctx, int x, int y, int width, int height) {
    Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr,
                     "gsave newpath %d %d moveto %d 0 rlineto 0 %d rlineto %d 0 rlineto closepath clip newpath\n", x, y,
                     width, height, -width);
}

/**
 * @brief Restore one rectangular PostScript clipping scope.
 * @ingroup postscript_backend
 *
 * Emits grestore without changing common clip depth. Requires a matching begin.
 *
 * @param[in,out] ctx Live Postscript context with a matching rectangular clip scope; common depth is not changed here.
 */
static void PostScriptClipEnd(Rbc_RenderContext *ctx) {
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "grestore\n", (char *)NULL);
}

/* The existing PostScript emitter remains responsible for PS syntax and maps. */
/**
 * @brief Stroke a connected path in bounded PostScript batches.
 * @ingroup postscript_backend
 *
 * Fewer than two points is a no-op. Splits at 1500 path components and restarts at the shared vertex.
 * Invokes the current DashesProc before each foreground stroke; caller ordering must preserve that
 * document-level definition.
 *
 * @param[in,out] ctx Live Postscript stroke context with configured color, width and dash state.
 * @param[in] points Borrowed mapped point array readable for count vertices.
 * @param[in] count Number of geometry records, not bytes.
 */
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

/**
 * @brief Delegate an independent segment batch to the legacy token.
 * @ingroup postscript_backend
 *
 * Positive counts pass borrowed mapped geometry to Rbc_2DSegmentsToPostScript. Does not own points or
 * establish a new line style.
 *
 * @param[in,out] ctx Live Postscript stroke context with configured color, width and dash state.
 * @param[in] segments Borrowed mapped endpoint pairs readable for count Segment2D records.
 * @param[in] count Number of geometry records, not bytes.
 */
static void PostScriptSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    if (count > 0) {
        Rbc_2DSegmentsToPostScript(POSTSCRIPT(ctx)->psToken, (Segment2D *)segments, count);
    }
}

/**
 * @brief Emit cap and join settings immediately.
 * @ingroup postscript_backend
 *
 * Delegates to the legacy token conversion functions. Unlike PDF/SVG setters, this mutates the output
 * graphics state rather than a cached style record.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] capStyle X/Tk line cap constant passed to the PostScript conversion helper.
 * @param[in] joinStyle X/Tk line join constant passed to the PostScript conversion helper.
 */
static void PostScriptLineStyle(Rbc_RenderContext *ctx, int capStyle, int joinStyle) {
    Rbc_CapStyleToPostScript(POSTSCRIPT(ctx)->psToken, capStyle);
    Rbc_JoinStyleToPostScript(POSTSCRIPT(ctx)->psToken, joinStyle);
}

/**
 * @brief Define the document's dash-background procedure.
 * @ingroup postscript_backend
 *
 * For a dashed stroke with a color, defines DashesProc to paint an undashed background inside
 * gsave/grestore. Otherwise defines an empty procedure. Dictionary definitions are not scoped by
 * gsave: emit a batch before another context changes this definition.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] color Optional borrowed off-dash color; NULL installs an empty dash-background procedure.
 */
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

/**
 * @brief Paint the current path using legacy fill and stipple helpers.
 * @ingroup postscript_backend
 *
 * Paints an optional background via the prolog Fill procedure, then selects foreground. backgroundOnly
 * returns after that selection. Otherwise paints stipple or solid fill. Borrows graph/display and
 * style resources; does not apply fill opacity or image tiles.
 *
 * @param[in,out] ctx Live Postscript fill context with initialized fillStyle and any required graph/tile resources.
 */
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

/**
 * @brief Close and fill a supplied polygonal path.
 * @ingroup postscript_backend
 *
 * Delegates path creation to the legacy token and fill policy to PostScriptFillPath. The caller
 * supplies valid mapped geometry and retains its storage.
 *
 * @param[in,out] ctx Live Postscript fill context with initialized fillStyle and any required graph/tile resources.
 * @param[in] points Borrowed mapped point array readable for count vertices.
 * @param[in] count Number of geometry records, not bytes.
 */
static void PostScriptFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Rbc_PathToPostScript(POSTSCRIPT(ctx)->psToken, (Point2D *)points, count);
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "closepath\n", (char *)NULL);
    PostScriptFillPath(ctx);
}

/* The current path survives setmatrix, unlike grestore. Save the CTM on the
 * operand stack while constructing the unit-circle arc, then restore it before
 * stroking/filling so widths, dash lengths and stipples stay in display units. */
/**
 * @brief Build an elliptical arc while restoring display-space line units.
 * @ingroup postscript_backend
 *
 * Requires normalized nonzero arc geometry. Saves the current matrix on the operand stack, constructs
 * a scaled unit-circle arc, then restores the matrix without losing the path. Negated angles preserve
 * screen-space orientation. Closes full ellipses/chords and joins partial pieslices to the center.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] arc Borrowed normalized finite ellipse geometry with positive radii and nonzero extent.
 */
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

/**
 * @brief Stroke an arc with the current dash-background procedure.
 * @ingroup postscript_backend
 *
 * Builds the path and invokes DashesProc before stroke. Does not independently establish line style.
 *
 * @param[in,out] ctx Live Postscript stroke context with configured color, width and dash state.
 * @param[in] arc Borrowed normalized finite ellipse geometry with positive radii and nonzero extent.
 */
static void PostScriptArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    PostScriptArcPath(ctx, arc);
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "DashesProc stroke\n", (char *)NULL);
}

/**
 * @brief Fill an arc with the legacy fill policy.
 * @ingroup postscript_backend
 *
 * Builds normalized geometry then delegates to PostScriptFillPath; does not emit an outline.
 *
 * @param[in,out] ctx Live Postscript fill context with initialized fillStyle and any required graph/tile resources.
 * @param[in] arc Borrowed normalized closed ellipse segment; common dispatch excludes open arcs.
 */
static void PostScriptFillArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    PostScriptArcPath(ctx, arc);
    PostScriptFillPath(ctx);
}

/**
 * @brief Paint mapped rectangles using legacy solid/stipple behavior.
 * @ingroup postscript_backend
 *
 * Stipple fills paint optional background then foreground, falling back to background color when
 * foreground is absent. Solid rectangles require foreground. This path does not implement image tiles
 * or opacity, and does not share every polygon backgroundOnly rule.
 *
 * @param[in,out] ctx Live Postscript fill context with initialized fillStyle and any required graph/tile resources.
 * @param[in] rectangles Borrowed mapped rectangle array readable for count entries.
 * @param[in] count Number of geometry records, not bytes.
 */
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

/**
 * @brief Emit centers using the context's configured prolog macro.
 * @ingroup postscript_backend
 *
 * Requires symbol setup to have selected a valid macro and size and defined DrawSymbolProc. Emits
 * centers in order without retaining them or redefining the procedure.
 *
 * @param[in,out] ctx Live Postscript symbol context with selected symbol style and document resources.
 * @param[in] centers Borrowed mapped symbol centers readable for count entries.
 * @param[in] count Number of geometry records, not bytes.
 */
static void PostScriptSymbolPoints(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count) {
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        Rbc_FormatToPostScript(POSTSCRIPT(ctx)->psToken, "%g %g %g %s\n", centers[i].x, centers[i].y,
                               RENDER_EXPORT(ctx)->symbolSize, POSTSCRIPT(ctx)->symbolMacro);
    }
}

/**
 * @brief Delegate mapped text to the legacy PostScript text writer.
 * @ingroup postscript_backend
 *
 * Borrows the string and TextStyle. Uses token font/color maps and legacy encoding, not the PDF/SVG
 * document font pipeline.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] string Borrowed NUL-terminated Tcl UTF text consumed during the call.
 * @param[in] style Borrowed text style with live font/color resources; layout and placement follow its fields.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 */
static void PostScriptText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y) {
    Rbc_TextToPostScript(POSTSCRIPT(ctx)->psToken, string, style, x, y);
}

/* Legacy PostScript silently omits non-photo image markers. */
/**
 * @brief Retain the legacy omission of non-photo Tk images.
 * @ingroup postscript_backend
 *
 * Intentionally emits nothing. Photo markers use PostScriptPhoto; generic Tk-image raster capture is
 * not attempted here.
 *
 * @param[in,out] ctx Unused; generic Tk images are intentionally omitted by this legacy callback.
 * @param[in] image Unused; generic Tk images are intentionally omitted by this legacy callback.
 * @param[in] x Unused; generic Tk images are intentionally omitted by this legacy callback.
 * @param[in] y Unused; generic Tk images are intentionally omitted by this legacy callback.
 */
static void PostScriptTkImage(Rbc_RenderContext *ctx, Tk_Image image, double x, double y) {
    (void)ctx;
    (void)image;
    (void)x;
    (void)y;
}

/**
 * @brief Delegate a non-NULL photo to the legacy image writer.
 * @ingroup postscript_backend
 *
 * Borrows the handle and emits at the supplied mapped origin. NULL is a no-op; does not use the PDF
 * alpha-resource path.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] photo Borrowed Tk photo handle, read synchronously.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 */
static void PostScriptPhoto(Rbc_RenderContext *ctx, Tk_PhotoHandle photo, double x, double y) {
    if (photo != NULL) {
        Rbc_PhotoToPostScript(POSTSCRIPT(ctx)->psToken, photo, x, y);
    }
}

/**
 * @brief Delegate window capture to the legacy PostScript writer.
 * @ingroup postscript_backend
 *
 * Borrows the live window. Capture behavior and errors belong to Rbc_WindowToPostScript; this adapter
 * does not introduce a clipping container.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] tkwin Borrowed live Tk window passed to the legacy capture writer.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 */
static void PostScriptWindow(Rbc_RenderContext *ctx, Tk_Window tkwin, double x, double y) {
    Rbc_WindowToPostScript(POSTSCRIPT(ctx)->psToken, tkwin, x, y);
}

/**
 * @brief Select background color and emit a filled polygon.
 * @ingroup postscript_backend
 *
 * Uses the token's background/color-map policy and borrows the mapped point array. Does not establish
 * an independent saved graphics scope.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] color Borrowed background color interpreted by the legacy PostScript color-map policy.
 * @param[in] points Borrowed mapped point array readable for count vertices.
 * @param[in] count Number of geometry records, not bytes.
 */
static void PostScriptBackgroundPolygon(Rbc_RenderContext *ctx, const XColor *color, const Point2D *points,
                                        Tcl_Size count) {
    Rbc_BackgroundToPostScript(POSTSCRIPT(ctx)->psToken, (XColor *)color);
    Rbc_PolygonToPostScript(POSTSCRIPT(ctx)->psToken, (Point2D *)points, count);
}

/**
 * @brief Dispatch filled or outline-only Tk relief output.
 * @ingroup postscript_backend
 *
 * Borrows the border and delegates to the matching legacy 3D rectangle helper. Geometry and relief are
 * already selected by the component.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] border Borrowed Tk 3D border; lazy shadow-color initialization may occur.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Mapped rectangle width in display pixels.
 * @param[in] height Mapped rectangle height in display pixels.
 * @param[in] borderWidth Relief thickness in display pixels; caller keeps size arithmetic representable.
 * @param[in] relief Tk relief constant.
 * @param[in] fill Nonzero paints the interior before relief; zero requests edges only.
 */
static void PostScriptBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y, int width, int height,
                             int borderWidth, int relief, int fill) {
    if (fill) {
        Rbc_Fill3DRectangleToPostScript(POSTSCRIPT(ctx)->psToken, border, x, y, width, height, borderWidth, relief);
    } else {
        Rbc_Draw3DRectangleToPostScript(POSTSCRIPT(ctx)->psToken, border, x, y, width, height, borderWidth, relief);
    }
}

/**
 * @brief Paint a rectangle using the token's clear-background policy.
 * @ingroup postscript_backend
 *
 * Clearing emits paint through legacy helpers; it does not remove prior document content.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Mapped rectangle width in display pixels.
 * @param[in] height Mapped rectangle height in display pixels.
 */
static void PostScriptClearRectangle(Rbc_RenderContext *ctx, double x, double y, int width, int height) {
    Rbc_ClearBackgroundToPostScript(POSTSCRIPT(ctx)->psToken);
    Rbc_RectangleToPostScript(POSTSCRIPT(ctx)->psToken, x, y, width, height);
}

/**
 * @brief Select one background and paint a rectangle batch.
 * @ingroup postscript_backend
 *
 * NULL color uses the clear-background policy. Otherwise selects background once, then emits all
 * borrowed rectangles in order.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] color Optional borrowed background color; NULL paints using the format clear-background policy.
 * @param[in] rectangles Borrowed mapped rectangle array readable for count entries.
 * @param[in] count Number of geometry records, not bytes.
 */
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

/**
 * @brief Paint plot background and open its saved clipping scope.
 * @ingroup postscript_backend
 *
 * Selects the supplied font, constructs the region and paints via prolog Fill before gsave/clip. NULL
 * background uses the token's clear policy. PostScriptPlotEnd balances this plot scope.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] font Borrowed Tk font for plot setup.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Mapped rectangle width in display pixels.
 * @param[in] height Mapped rectangle height in display pixels.
 * @param[in] background Borrowed plot background color; NULL selects the format's clear-background policy.
 */
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

/**
 * @brief Close the saved plot clipping scope.
 * @ingroup postscript_backend
 *
 * Emits grestore separately from the common renderer's explicit clip-depth mechanism. Requires a
 * matching plot begin.
 *
 * @param[in,out] ctx Live Postscript context with a matching plot scope; common depth is not changed here.
 */
static void PostScriptPlotEnd(Rbc_RenderContext *ctx) {
    Rbc_AppendToPostScript(POSTSCRIPT(ctx)->psToken, "\n", "% Unset clipping\n", "grestore\n\n", (char *)NULL);
}

/**
 * @brief Emit a translated and Y-corrected imagemask.
 * @ingroup postscript_backend
 *
 * Missing bitmap or nonpositive dimensions produce no output. Selects foreground/background according
 * to the flag, emits bitmap bytes through the legacy helper and balances gsave/grestore. Borrows
 * display, pixmap and color.
 *
 * @param[in,out] ctx Live Postscript renderer context borrowing the active export document.
 * @param[in] display Borrowed display owning the source bitmap.
 * @param[in] bitmap Borrowed one-bit pixmap readable over width by height pixels; None suppresses output.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Source bitmap width and mapped extent in pixels; nonpositive values emit nothing.
 * @param[in] height Source bitmap height and mapped extent in pixels; nonpositive values emit nothing.
 * @param[in] color Borrowed paint color; omission follows this helper's format-specific policy.
 * @param[in] background Nonzero selects the legacy background-color policy; zero selects foreground.
 */
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
