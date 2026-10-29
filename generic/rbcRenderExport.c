/* Export renderer. See license.terms. */
#include "rbcRenderExportInt.h"

Rbc_RenderContext *Rbc_RenderBeginExport(Rbc_ExportContext *exportPtr, const XColor *color, int lineWidth,
                                         const Rbc_Dashes *dashes, int capStyle, int joinStyle) {
    Rbc_RenderContext *ctx = Rbc_RenderBeginExportOutput(exportPtr);
    if (exportPtr->backend == RBC_EXPORT_SVG || exportPtr->backend == RBC_EXPORT_PDF) {
        RENDER_EXPORT(ctx)->vectorHasColor = color != NULL;
        if (color != NULL) {
            RENDER_EXPORT(ctx)->vectorColor = *color;
        }
        RENDER_EXPORT(ctx)->vectorWidth = MAX(1, lineWidth);
        RENDER_EXPORT(ctx)->vectorCap = capStyle;
        RENDER_EXPORT(ctx)->vectorJoin = joinStyle;
        if (dashes != NULL) {
            RENDER_EXPORT(ctx)->vectorDashes = *dashes;
        }
        return ctx;
    }
    Rbc_RenderPostscriptStroke(ctx, color, lineWidth, dashes, capStyle, joinStyle);
    return ctx;
}

Rbc_RenderContext *Rbc_RenderBeginExportFill(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                             const Rbc_RenderFillStyle *style) {
    Rbc_RenderContext *ctx = Rbc_RenderBeginExportOutput(exportPtr);
    ctx->graphPtr = graphPtr;
    RENDER_EXPORT(ctx)->fillStyle = *style;
    return ctx;
}

/* Tiles are borrowed for the lifetime of the fill context. The PS backend
 * retains the configured background-only fallback. */
void Rbc_RenderSetFillTile(Rbc_RenderContext *ctx, Rbc_Tile tile) {
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
    if (token != NULL && (token->backend == RBC_EXPORT_SVG || token->backend == RBC_EXPORT_PDF)) {
        RENDER_EXPORT(ctx)->vectorTile = tile;
        RENDER_EXPORT(ctx)->vectorPatternId = 0;
    }
}

/* Retain the legacy prolog shapes and size corrections, including bitmap masks. */
Rbc_RenderContext *Rbc_RenderBeginExportSymbol(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                               const Rbc_RenderSymbolStyle *style) {
    Rbc_RenderContext *ctx = Rbc_RenderBeginExportOutput(exportPtr);
    ctx->graphPtr = graphPtr;
    RENDER_EXPORT(ctx)->vectorSymbol = *style;
    if (exportPtr->backend == RBC_EXPORT_POSTSCRIPT) Rbc_RenderPostscriptSymbol(ctx, style);
    return ctx;
}

/* Legend bar swatches use the unscaled square prolog shape. */
Rbc_RenderContext *Rbc_RenderBeginExportBarSymbol(Graph *graphPtr, Rbc_ExportContext *exportPtr,
                                                  const Rbc_RenderFillStyle *style, int size) {
    Rbc_RenderContext *ctx = Rbc_RenderBeginExportFill(graphPtr, exportPtr, style);
    RENDER_EXPORT(ctx)->vectorBarSymbol = TRUE;
    RENDER_EXPORT(ctx)->symbolSize = size;
    if (exportPtr->backend == RBC_EXPORT_POSTSCRIPT) Rbc_RenderPostscriptBarSymbol(ctx, style, size);
    return ctx;
}

Rbc_RenderContext *Rbc_RenderBeginExportOutput(Rbc_ExportContext *exportPtr) {
    switch (exportPtr->backend) {
    case RBC_EXPORT_SVG: return Rbc_RenderCreateSvg(exportPtr);
    case RBC_EXPORT_PDF: return Rbc_RenderCreatePdf(exportPtr);
    case RBC_EXPORT_POSTSCRIPT: return Rbc_RenderCreatePostscript(exportPtr);
    }
    Tcl_Panic("unknown graph export backend");
    return NULL;
}

void Rbc_RenderExportEnd(Rbc_RenderContext *ctx) {
    /* The export owns its token and clipping/page state. */
    ckfree(ctx);
}

void Rbc_RenderExportLineStyle(Rbc_RenderContext *ctx, int cap, int join) {
    RENDER_EXPORT(ctx)->vectorCap = cap;
    RENDER_EXPORT(ctx)->vectorJoin = join;
}

void Rbc_RenderExportDashBackground(Rbc_RenderContext *ctx, const XColor *color) {
    RENDER_EXPORT(ctx)->vectorHasOffColor = color != NULL;
    if (color != NULL) {
        RENDER_EXPORT(ctx)->vectorOffColor = *color;
    }
}

Rbc_ColorImage Rbc_RenderExportRasterImage(Rbc_ExportContext *token, Tk_Image image) {
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
        Rbc_RenderExportError(token, "cannot allocate export image capture pixmap");
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
            Rbc_RenderExportError(token, "cannot capture Tk image for export");
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

void Rbc_RenderExportError(Rbc_ExportContext *token, const char *message) {
    if (token->error == NULL) token->error = message;
}

int Rbc_RenderExportSymbolRound(double value) { return (int)(value + ((value < 0.0) ? -0.5 : 0.5)); }
