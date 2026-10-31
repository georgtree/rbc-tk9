/* Pdf renderer. See license.terms. */
#include "rbcRenderExportInt.h"

static void PdfClipBegin(Rbc_RenderContext *ctx, int x, int y, int width, int height) {
    Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "q %d %d %d %d re W n\n", x, y, width, height);
}
static void PdfClipEnd(Rbc_RenderContext *ctx) { Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "Q\n", (char *)NULL); }

/* ----------------------------------------------------------------------
 * PDF export. Geometry stays in graph coordinates; the document's initial
 * matrix converts pixels to points and reverses the PDF Y axis. Every
 * primitive saves/restores graphics state, independently of Cairo.
 * ---------------------------------------------------------------------- */
static void PdfColor(Rbc_ExportContext *token, const XColor *color, int stroke) {
    if (color != NULL) {
        Rbc_ExportFormat(token, "%.8f %.8f %.8f %s\n", color->red / 65535.0, color->green / 65535.0,
                         color->blue / 65535.0, stroke ? "RG" : "rg");
    }
}

static void PdfPath(Rbc_ExportContext *token, const Point2D *points, Tcl_Size count, int close) {
    Tcl_Size i;
    for (i = 0; i < count; i++) {
        Rbc_ExportFormat(token, "%.8f %.8f %s\n", points[i].x, points[i].y, i == 0 ? "m" : "l");
    }
    if (close) {
        Rbc_ExportAppend(token, "h\n", (char *)NULL);
    }
}

static void PdfRectangle(Rbc_ExportContext *token, double x, double y, double width, double height,
                         const XColor *color) {
    if (color != NULL && width > 0 && height > 0) {
        Rbc_ExportAppend(token, "q\n", (char *)NULL);
        PdfColor(token, color, FALSE);
        Rbc_ExportFormat(token, "%.8f %.8f %.8f %.8f re f\nQ\n", x, y, width, height);
    }
}

/* Elliptical arcs use cubic Beziers of at most 90 degrees each. */
static void PdfArcPath(Rbc_ExportContext *token, const Rbc_RenderArcGeometry *arc) {
    double a = -arc->start * M_PI / 180.0;
    int i, steps = (int)ceil(fabs(arc->extent) / 90.0);
    double step = -arc->extent * M_PI / (180.0 * steps);
    int full = fabs(arc->extent) >= 360.0;

    Rbc_ExportFormat(token, "%.8f %.8f m\n", arc->cx + arc->rx * cos(a), arc->cy + arc->ry * sin(a));
    for (i = 0; i < steps; i++) {
        double b = a + step, k = 4.0 / 3.0 * tan(step / 4.0);
        Rbc_ExportFormat(token, "%.8f %.8f %.8f %.8f %.8f %.8f c\n", arc->cx + arc->rx * (cos(a) - k * sin(a)),
                         arc->cy + arc->ry * (sin(a) + k * cos(a)), arc->cx + arc->rx * (cos(b) + k * sin(b)),
                         arc->cy + arc->ry * (sin(b) - k * cos(b)), arc->cx + arc->rx * cos(b),
                         arc->cy + arc->ry * sin(b));
        a = b;
    }
    if (!full && arc->style == RBC_RENDER_ARC_PIESLICE) {
        Rbc_ExportFormat(token, "%.8f %.8f l\n", arc->cx, arc->cy);
    }
    if (full || arc->style != RBC_RENDER_ARC_OPEN) {
        Rbc_ExportAppend(token, "h\n", (char *)NULL);
    }
}

static void PdfStrokeStyle(Rbc_RenderContext *ctx, int background) {
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
    int i;
    PdfColor(token, background ? &RENDER_EXPORT(ctx)->vectorOffColor : &RENDER_EXPORT(ctx)->vectorColor, TRUE);
    Rbc_ExportFormat(
        token, "%d w %d J %d j\n[", RENDER_EXPORT(ctx)->vectorWidth,
        RENDER_EXPORT(ctx)->vectorCap == CapRound ? 1 : (RENDER_EXPORT(ctx)->vectorCap == CapProjecting ? 2 : 0),
        RENDER_EXPORT(ctx)->vectorJoin == JoinRound ? 1 : (RENDER_EXPORT(ctx)->vectorJoin == JoinBevel ? 2 : 0));
    if (!background) {
        for (i = 0; i < RBC_MAX_DASH_VALUES && RENDER_EXPORT(ctx)->vectorDashes.values[i]; i++) {
            Rbc_ExportFormat(token, "%u ", (unsigned char)RENDER_EXPORT(ctx)->vectorDashes.values[i]);
        }
    }
    Rbc_ExportFormat(token, "] %d d\n", background ? 0 : RENDER_EXPORT(ctx)->vectorDashes.offset);
}

/* Paint each segment's dash background immediately before its foreground. */
static void PdfPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    int pass;
    if (!RENDER_EXPORT(ctx)->vectorHasColor || count < 2) {
        return;
    }
    for (pass = RENDER_EXPORT(ctx)->vectorHasOffColor && RENDER_EXPORT(ctx)->vectorDashes.values[0] ? 1 : 0; pass >= 0;
         pass--) {
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "q\n", (char *)NULL);
        PdfStrokeStyle(ctx, pass);
        PdfPath(RENDER_EXPORT(ctx)->exportPtr, points, count, FALSE);
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "S\nQ\n", (char *)NULL);
    }
}

static void PdfSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Tcl_Size i;
    for (i = 0; i < count; i++) {
        Point2D points[2] = {segments[i].p, segments[i].q};
        PdfPolyline(ctx, points, 2);
    }
}

static void PdfArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    int pass;
    if (!RENDER_EXPORT(ctx)->vectorHasColor) {
        return;
    }
    for (pass = RENDER_EXPORT(ctx)->vectorHasOffColor && RENDER_EXPORT(ctx)->vectorDashes.values[0] ? 1 : 0; pass >= 0;
         pass--) {
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "q\n", (char *)NULL);
        PdfStrokeStyle(ctx, pass);
        PdfArcPath(RENDER_EXPORT(ctx)->exportPtr, arc);
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "S\nQ\n", (char *)NULL);
    }
}

/* Bitmap runs remain vector rectangles, including transparent stipples. */
static void PdfBitmapRuns(Rbc_ExportContext *token, XImage *bits, XImage *mask, const XColor *color) {
    int x, y;
    if (color == NULL) {
        return;
    }
    PdfColor(token, color, FALSE);
    for (y = 0; y < bits->height; y++) {
        for (x = 0; x < bits->width;) {
            int start;
            while (x < bits->width && (!XGetPixel(bits, x, y) || (mask && !XGetPixel(mask, x, y)))) {
                x++;
            }
            start = x;
            while (x < bits->width && XGetPixel(bits, x, y) && (!mask || XGetPixel(mask, x, y))) {
                x++;
            }
            if (x > start) {
                Rbc_ExportFormat(token, "%d %d %d 1 re\n", start, y, x - start);
            }
        }
    }
    Rbc_ExportAppend(token, "f\n", (char *)NULL);
}

/* RGB image plus an optional grayscale soft mask preserves photo alpha. */
static int PdfPhotoResource(Rbc_ExportContext *token, const Tk_PhotoImageBlock *block) {
    Tcl_DString rgb, alpha;
    int x, y, hasAlpha, translucent = FALSE, mask = 0, id;
    char dictionary[256];

    if (block->width <= 0 || block->height <= 0) {
        return 0;
    }
    if (block->pixelSize <= 0 || block->pixelPtr == NULL || (Tcl_WideInt)block->width * block->height > INT_MAX / 4) {
        Rbc_ExportError(token, "photo is too large or invalid for PDF");
        return 0;
    }
    Tcl_DStringInit(&rgb);
    Tcl_DStringInit(&alpha);
    hasAlpha = block->offset[3] >= 0 && block->offset[3] < block->pixelSize && block->offset[3] != block->offset[0] &&
               block->offset[3] != block->offset[1] && block->offset[3] != block->offset[2];
    for (y = 0; y < block->height; y++) {
        const unsigned char *p = block->pixelPtr + (ptrdiff_t)y * block->pitch;
        for (x = 0; x < block->width; x++, p += block->pixelSize) {
            unsigned char color[3] = {p[block->offset[0]], p[block->offset[1]], p[block->offset[2]]};
            unsigned char a = hasAlpha ? p[block->offset[3]] : 255;
            Tcl_DStringAppend(&rgb, (const char *)color, 3);
            Tcl_DStringAppend(&alpha, (const char *)&a, 1);
            translucent |= a != 255;
        }
    }
    if (translucent) {
        snprintf(dictionary, sizeof(dictionary),
                 "/Type /XObject /Subtype /Image /Width %d /Height %d "
                 "/ColorSpace /DeviceGray /BitsPerComponent 8",
                 block->width, block->height);
        mask = Rbc_PdfStream(token, 'I', dictionary, Tcl_DStringValue(&alpha), Tcl_DStringLength(&alpha));
    }
    snprintf(dictionary, sizeof(dictionary),
             "/Type /XObject /Subtype /Image /Width %d /Height %d "
             "/ColorSpace /DeviceRGB /BitsPerComponent 8",
             block->width, block->height);
    if (mask != 0) {
        size_t n = strlen(dictionary);
        snprintf(dictionary + n, sizeof(dictionary) - n, " /SMask %d 0 R", mask);
    }
    id = Rbc_PdfStream(token, 'I', dictionary, Tcl_DStringValue(&rgb), Tcl_DStringLength(&rgb));
    Tcl_DStringFree(&rgb);
    Tcl_DStringFree(&alpha);
    return id;
}

static void PdfImageUse(Rbc_ExportContext *token, int id, double x, double y, int width, int height) {
    if (id) {
        Rbc_ExportFormat(token, "q\n%d 0 0 %d %.8f %.8f cm /I%d Do\nQ\n", width, -height, x, y + height, id);
    }
}

static int PdfColorImageResource(Rbc_ExportContext *token, Rbc_ColorImage image) {
    Tk_PhotoImageBlock block;
    if ((size_t)Rbc_ColorImageWidth(image) > (size_t)INT_MAX / sizeof(Pix32)) {
        Rbc_ExportError(token, "captured image is too large for PDF");
        return 0;
    }
    block.width = Rbc_ColorImageWidth(image);
    block.height = Rbc_ColorImageHeight(image);
    block.pixelSize = sizeof(Pix32);
    block.pitch = block.width * block.pixelSize;
    block.pixelPtr = (unsigned char *)Rbc_ColorImageBits(image);
    block.offset[0] = offsetof(Pix32, rgba.red);
    block.offset[1] = offsetof(Pix32, rgba.green);
    block.offset[2] = offsetof(Pix32, rgba.blue);
    block.offset[3] = offsetof(Pix32, rgba.alpha);
    return PdfPhotoResource(token, &block);
}

static unsigned int PdfPattern(Rbc_RenderContext *ctx, const XColor *foreground) {
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
    Tcl_DString content;
    Tcl_DString *saved = token->buffer;
    char dictionary[512], resources[80] = "";
    int width = 0, height = 0, imageId = 0;
    XImage *bits = NULL;
    double x = 0, y = 0, scale = Rbc_PdfScale(token);

    if (RENDER_EXPORT(ctx)->vectorPatternId) {
        return RENDER_EXPORT(ctx)->vectorPatternId;
    }
    if (RENDER_EXPORT(ctx)->fillStyle.backgroundOnly) {
        Tk_PhotoImageBlock block;
        Tk_Window window;
        if (RENDER_EXPORT(ctx)->vectorTile == NULL) {
            Rbc_ExportError(token, "missing tile image for PDF");
            return 0;
        }
        if (Rbc_GetTilePhoto(RENDER_EXPORT(ctx)->vectorTile, &block)) {
            width = block.width;
            height = block.height;
            imageId = PdfPhotoResource(token, &block);
        } else {
            Rbc_ColorImage image = Rbc_RenderExportRasterImage(token, Rbc_ImageOfTile(RENDER_EXPORT(ctx)->vectorTile));
            if (image != NULL) {
                width = Rbc_ColorImageWidth(image);
                height = Rbc_ColorImageHeight(image);
                imageId = PdfColorImageResource(token, image);
                Rbc_FreeColorImage(image);
            }
        }
        for (window = ctx->graphPtr->tkwin; !Tk_IsTopLevel(window); window = Tk_Parent(window)) {
            x -= Tk_X(window) + Tk_Changes(window)->border_width;
            y -= Tk_Y(window) + Tk_Changes(window)->border_width;
        }
        snprintf(resources, sizeof(resources), "/XObject << /I%d %d 0 R >>", imageId, imageId);
    } else {
        Tk_SizeOfBitmap(ctx->graphPtr->display, RENDER_EXPORT(ctx)->fillStyle.stipple, &width, &height);
        if (width > 0 && height > 0) {
            bits = XGetImage(ctx->graphPtr->display, RENDER_EXPORT(ctx)->fillStyle.stipple, 0, 0, width, height, 1,
                             XYPixmap);
            if (bits == NULL) {
                Rbc_ExportError(token, "cannot read PDF stipple");
                return 0;
            }
        }
    }
    if (width <= 0 || height <= 0) {
        return 0;
    }
    Tcl_DStringInit(&content);
    token->buffer = &content;
    if (bits != NULL) {
        PdfRectangle(token, 0, 0, width, height, RENDER_EXPORT(ctx)->fillStyle.background);
        PdfBitmapRuns(token, bits, NULL, foreground);
        XDestroyImage(bits);
    } else {
        PdfImageUse(token, imageId, 0, 0, width, height);
    }
    token->buffer = saved;
    snprintf(dictionary, sizeof(dictionary),
             "/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 "
             "/BBox [0 0 %d %d] /XStep %d /YStep %d /Resources << %s >> "
             "/Matrix [%.8f 0 0 %.8f %.8f %.8f]",
             width, height, width, height, resources, scale, -scale, x * scale, (Rbc_PdfHeight(token) - y) * scale);
    RENDER_EXPORT(ctx)->vectorPatternId =
        Rbc_PdfStream(token, 'P', dictionary, Tcl_DStringValue(&content), Tcl_DStringLength(&content));
    Tcl_DStringFree(&content);
    return RENDER_EXPORT(ctx)->vectorPatternId;
}

/* Establish a fill independently of path construction; never lose a path
 * while creating a stipple/image resource. */
static int PdfFillStyle(Rbc_RenderContext *ctx) {
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
    const Rbc_RenderFillStyle *style = &RENDER_EXPORT(ctx)->fillStyle;
    const XColor *color = style->foreground;
    if (style->opacity <= 0) {
        return FALSE;
    }
    if (style->opacity < 1) {
        char dictionary[100];
        int id;
        snprintf(dictionary, sizeof(dictionary), "<< /Type /ExtGState /ca %.8f >>", style->opacity);
        id = Rbc_PdfResource(token, 'G', dictionary, -1);
        Rbc_ExportFormat(token, "/G%d gs\n", id);
    }
    if (style->backgroundOnly || style->stipple != None) {
        unsigned int id;
        if (color == NULL) {
            color = style->background;
        }
        id = PdfPattern(ctx, color);
        if (!id) {
            return FALSE;
        }
        Rbc_ExportFormat(token, "/Pattern cs /P%u scn\n", id);
    } else {
        if (color == NULL) {
            return FALSE;
        }
        PdfColor(token, color, FALSE);
    }
    return TRUE;
}

static void PdfFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "q\n", (char *)NULL);
    if (PdfFillStyle(ctx)) {
        PdfPath(RENDER_EXPORT(ctx)->exportPtr, points, count, TRUE);
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "f*\n", (char *)NULL);
    }
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "Q\n", (char *)NULL);
}

static void PdfFillRectangles(Rbc_RenderContext *ctx, const Rbc_RenderRectangle *rectangles, Tcl_Size count) {
    Tcl_Size i;
    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *r = rectangles + i;
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "q\n", (char *)NULL);
        if (PdfFillStyle(ctx)) {
            Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "%d %d %d %d re f\n", r->x, r->y, r->width, r->height);
        }
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "Q\n", (char *)NULL);
    }
}

static void PdfFillArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "q\n", (char *)NULL);
    if (PdfFillStyle(ctx)) {
        PdfArcPath(RENDER_EXPORT(ctx)->exportPtr, arc);
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "f*\n", (char *)NULL);
    }
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "Q\n", (char *)NULL);
}

static void PdfText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y) {
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
    TextLayout *layout;
    Point2D anchor = {x, y};
    double width, height, angle, size, advance;
    int fontId, cid, digits, pass;
    Tcl_Size i;

    if (string == NULL || *string == '\0') {
        return;
    }
    layout = Rbc_GetTextLayout(string, style);
    /* Some core-X11 fonts measure unsupported Unicode as zero pixels. When
     * Fontconfig finds a real fallback glyph, give it its outline advance
     * instead of overprinting the next character. Normal Tk advances and
     * genuine zero-width combining glyphs retain their original positions. */
    for (i = 0; i < layout->nFrags; i++) {
        TextFragment *fragment = layout->fragArr + i;
        Tcl_Size n = 0;
        while (n < fragment->count) {
            Tcl_UniChar ch;
            int consumed = Tcl_UtfToUniChar(fragment->text + n, &ch);
            if (Rbc_PdfFontCharacter(token, style->font, ch, &fontId, &cid, &size, &digits, &advance) != TCL_OK) {
                ckfree(layout);
                return;
            }
            if (Tk_TextWidth(style->font, fragment->text + n, consumed) == 0 && advance > 0) {
                double extra = ceil(advance);
                if (extra > INT_MAX - fragment->width - (double)style->padLeft - style->padRight) {
                    Rbc_ExportError(token, "PDF fallback text is too wide");
                    ckfree(layout);
                    return;
                }
                fragment->width += (int)extra;
            }
            n += consumed;
        }
        layout->width = MAX(layout->width, fragment->width + style->padLeft + style->padRight);
    }
    for (i = 0; i < layout->nFrags; i++) {
        TextFragment *fragment = layout->fragArr + i;
        switch (style->justify) {
        case TK_JUSTIFY_RIGHT:
            fragment->x = layout->width - fragment->width - style->padRight;
            break;
        case TK_JUSTIFY_CENTER:
            fragment->x = (layout->width - fragment->width) / 2;
            break;
        default:
            fragment->x = style->padLeft;
        }
    }
    Rbc_GetBoundingBox(layout->width, layout->height, style->theta, &width, &height, NULL);
    anchor = Rbc_TranslatePoint(&anchor, ROUND(width), ROUND(height), style->anchor);
    angle = -style->theta * M_PI / 180.0;
    Rbc_ExportFormat(token, "q\n1 0 0 1 %.8f %.8f cm\n%.8f %.8f %.8f %.8f 0 0 cm\n1 0 0 1 %.8f %.8f cm\n",
                     anchor.x + width / 2, anchor.y + height / 2, cos(angle), sin(angle), -sin(angle), cos(angle),
                     -layout->width / 2.0, -layout->height / 2.0);
    for (pass = 0; pass < 2; pass++) {
        const XColor *color =
            pass == 0 ? style->shadow.color : ((style->state & STATE_ACTIVE) ? style->activeColor : style->color);
        int offset = pass == 0 ? style->shadow.offset : 0;
        if (color == NULL || (pass == 0 && offset <= 0)) {
            continue;
        }
        PdfColor(token, color, FALSE);
        Rbc_ExportAppend(token, "BT\n", (char *)NULL);
        for (i = 0; i < layout->nFrags; i++) {
            TextFragment *fragment = layout->fragArr + i;
            Tcl_Size n = 0;
            int extra = 0;
            while (n < fragment->count) {
                Tcl_UniChar ch;
                int consumed = Tcl_UtfToUniChar(fragment->text + n, &ch);
                if (Rbc_PdfFontCharacter(token, style->font, ch, &fontId, &cid, &size, &digits, &advance) != TCL_OK) {
                    break;
                }
                Rbc_ExportFormat(token, "/F%d %.8f Tf\n1 0 0 -1 %d %d Tm <%0*X> Tj\n", fontId, size,
                                 fragment->x + offset + extra + Tk_TextWidth(style->font, fragment->text, n),
                                 fragment->y + offset, digits, cid);
                if (Tk_TextWidth(style->font, fragment->text + n, consumed) == 0 && advance > 0) {
                    extra += (int)ceil(advance);
                }
                n += consumed;
            }
        }
        Rbc_ExportAppend(token, "ET\n", (char *)NULL);
    }
    Rbc_ExportAppend(token, "Q\n", (char *)NULL);
    ckfree(layout);
}

static void PdfPhoto(Rbc_RenderContext *ctx, Tk_PhotoHandle photo, double x, double y) {
    Tk_PhotoImageBlock block;
    if (photo == NULL || !Tk_PhotoGetImage(photo, &block)) {
        Rbc_ExportError(RENDER_EXPORT(ctx)->exportPtr, "cannot read photo image for PDF");
        return;
    }
    PdfImageUse(RENDER_EXPORT(ctx)->exportPtr, PdfPhotoResource(RENDER_EXPORT(ctx)->exportPtr, &block), x, y,
                block.width, block.height);
}

static void PdfTkImage(Rbc_RenderContext *ctx, Tk_Image image, double x, double y) {
    Rbc_ColorImage captured = Rbc_RenderExportRasterImage(RENDER_EXPORT(ctx)->exportPtr, image);
    if (captured != NULL) {
        PdfImageUse(RENDER_EXPORT(ctx)->exportPtr, PdfColorImageResource(RENDER_EXPORT(ctx)->exportPtr, captured), x, y,
                    Rbc_ColorImageWidth(captured), Rbc_ColorImageHeight(captured));
        Rbc_FreeColorImage(captured);
    }
}

static void PdfWindow(Rbc_RenderContext *ctx, Tk_Window window, double x, double y) {
    Rbc_ColorImage captured;
    if (!Tk_IsMapped(window) || Tk_Width(window) <= 0 || Tk_Height(window) <= 0) {
        return;
    }
    captured = Rbc_DrawableToColorImage(window, Tk_WindowId(window), 0, 0, Tk_Width(window), Tk_Height(window), GAMMA);
    if (captured == NULL) {
        Rbc_ExportError(RENDER_EXPORT(ctx)->exportPtr, "cannot capture window marker for PDF");
        return;
    }
    PdfImageUse(RENDER_EXPORT(ctx)->exportPtr, PdfColorImageResource(RENDER_EXPORT(ctx)->exportPtr, captured), x, y,
                Rbc_ColorImageWidth(captured), Rbc_ColorImageHeight(captured));
    Rbc_FreeColorImage(captured);
}

static void PdfBitmapMask(Rbc_RenderContext *ctx, Display *display, Pixmap bitmap, double x, double y, int width,
                          int height, const XColor *color, int background) {
    XImage *bits;
    (void)background;
    if (bitmap == None || color == NULL || width <= 0 || height <= 0) {
        return;
    }
    bits = XGetImage(display, bitmap, 0, 0, width, height, 1, XYPixmap);
    if (bits == NULL) {
        Rbc_ExportError(RENDER_EXPORT(ctx)->exportPtr, "cannot read bitmap for PDF");
        return;
    }
    Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "q 1 0 0 1 %.8f %.8f cm\n", x, y);
    PdfBitmapRuns(RENDER_EXPORT(ctx)->exportPtr, bits, NULL, color);
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "Q\n", (char *)NULL);
    XDestroyImage(bits);
}

static void PdfBackgroundPolygon(Rbc_RenderContext *ctx, const XColor *color, const Point2D *points, Tcl_Size count) {
    if (color != NULL) {
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "q\n", (char *)NULL);
        PdfColor(RENDER_EXPORT(ctx)->exportPtr, color, FALSE);
        PdfPath(RENDER_EXPORT(ctx)->exportPtr, points, count, TRUE);
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "f* Q\n", (char *)NULL);
    }
}

static void PdfPolygon(Rbc_ExportContext *token, const Point2D *points, Tcl_Size count, const XColor *color,
                       double opacity) {
    Rbc_RenderExportState ctx;
    (void)opacity;
    memset(&ctx, 0, sizeof(ctx));
    ctx.exportPtr = token;
    PdfBackgroundPolygon(&ctx.core, color, points, count);
}

static void PdfBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y, int width, int height,
                      int borderWidth, int relief, int fill) {
    XColor bg, dark, light;
    const XColor *top, *bottom;
    Point2D points[6];

    /* Shadow colors are allocated lazily, possibly after the first export. */
    (void)Tk_3DBorderGC(RENDER_EXPORT(ctx)->exportPtr->tkwin, border, TK_3D_LIGHT_GC);
    Tk_Get3DBorderColors(border, &bg, &dark, &light);
    if (fill) {
        PdfRectangle(RENDER_EXPORT(ctx)->exportPtr, x, y, width, height, &bg);
    }
    if (borderWidth <= 0 || width < 2 * borderWidth || height < 2 * borderWidth || relief == TK_RELIEF_FLAT) {
        return;
    }
    if (relief == TK_RELIEF_GROOVE || relief == TK_RELIEF_RIDGE) {
        int half = borderWidth / 2, offset = borderWidth - half;
        PdfBorder(ctx, border, x, y, width, height, half,
                  relief == TK_RELIEF_GROOVE ? TK_RELIEF_SUNKEN : TK_RELIEF_RAISED, FALSE);
        PdfBorder(ctx, border, x + offset, y + offset, width - 2 * offset, height - 2 * offset, half,
                  relief == TK_RELIEF_GROOVE ? TK_RELIEF_RAISED : TK_RELIEF_SUNKEN, FALSE);
        return;
    }
    if (relief == TK_RELIEF_SOLID) {
        memset(&dark, 0, sizeof(dark));
        light = dark;
    }
    top = relief == TK_RELIEF_RAISED ? &light : &dark;
    bottom = relief == TK_RELIEF_RAISED ? &dark : &light;
    PdfRectangle(RENDER_EXPORT(ctx)->exportPtr, x, y + height - borderWidth, width, borderWidth, bottom);
    PdfRectangle(RENDER_EXPORT(ctx)->exportPtr, x + width - borderWidth, y, borderWidth, height, bottom);
    points[0] = (Point2D){x, y + height};
    points[1] = (Point2D){x, y};
    points[2] = (Point2D){x + width, y};
    points[3] = (Point2D){x + width - borderWidth, y + borderWidth};
    points[4] = (Point2D){x + borderWidth, y + borderWidth};
    points[5] = (Point2D){x + borderWidth, y + height - borderWidth};
    PdfPolygon(RENDER_EXPORT(ctx)->exportPtr, points, 6, top, 1.0);
}

static void PdfClearRectangle(Rbc_RenderContext *ctx, double x, double y, int width, int height) {
    XColor white;
    memset(&white, 0, sizeof(white));
    white.red = white.green = white.blue = 65535;
    PdfRectangle(RENDER_EXPORT(ctx)->exportPtr, x, y, width, height, &white);
}

static void PdfBackgroundRectangles(Rbc_RenderContext *ctx, const XColor *color, const Rbc_RenderRectangle *rectangles,
                                    Tcl_Size count) {
    Tcl_Size i;
    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *r = rectangles + i;
        if (color == NULL) {
            PdfClearRectangle(ctx, r->x, r->y, r->width, r->height);
        } else {
            PdfRectangle(RENDER_EXPORT(ctx)->exportPtr, r->x, r->y, r->width, r->height, color);
        }
    }
}

static void PdfPlotBegin(Rbc_RenderContext *ctx, Tk_Font font, double x, double y, int width, int height,
                         const XColor *background) {
    (void)font;
    if (background == NULL) {
        PdfClearRectangle(ctx, x, y, width, height);
    } else {
        PdfRectangle(RENDER_EXPORT(ctx)->exportPtr, x, y, width, height, background);
    }
    Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "q %.8f %.8f %d %d re W n\n", x, y, width, height);
}

static void PdfPlotEnd(Rbc_RenderContext *ctx) { Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "Q\n", (char *)NULL); }

/* Match the SVG/PostScript symbol size corrections and painter order. */
static void PdfSymbolPoints(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count) {
    const Rbc_RenderSymbolStyle *style = &RENDER_EXPORT(ctx)->vectorSymbol;
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
    Tcl_Size i;
    XImage *bits = NULL, *mask = NULL;
    int bitmapWidth = 0, bitmapHeight = 0;

    if (RENDER_EXPORT(ctx)->vectorBarSymbol) {
        for (i = 0; i < count; i++) {
            Rbc_RenderRectangle rect = {centers[i].x - RENDER_EXPORT(ctx)->symbolSize / 2,
                                        centers[i].y - RENDER_EXPORT(ctx)->symbolSize / 2,
                                        (int)RENDER_EXPORT(ctx)->symbolSize, (int)RENDER_EXPORT(ctx)->symbolSize};
            PdfFillRectangles(ctx, &rect, 1);
        }
        return;
    }
    if (style->type == RBC_RENDER_SYMBOL_BITMAP) {
        Tk_SizeOfBitmap(ctx->graphPtr->display, style->bitmap, &bitmapWidth, &bitmapHeight);
        if (bitmapWidth <= 0 || bitmapHeight <= 0) {
            return;
        }
        bits = XGetImage(ctx->graphPtr->display, style->bitmap, 0, 0, bitmapWidth, bitmapHeight, 1, XYPixmap);
        if (bits == NULL) {
            Rbc_ExportError(token, "cannot read PDF bitmap symbol");
            return;
        }
        if (style->mask != None && style->fillColor != NULL) {
            int width, height;
            Tk_SizeOfBitmap(ctx->graphPtr->display, style->mask, &width, &height);
            if (width != bitmapWidth || height != bitmapHeight) {
                Rbc_ExportError(token, "PDF bitmap symbol and mask dimensions differ");
                XDestroyImage(bits);
                return;
            }
            mask = XGetImage(ctx->graphPtr->display, style->mask, 0, 0, width, height, 1, XYPixmap);
            if (mask == NULL) {
                Rbc_ExportError(token, "cannot read PDF bitmap symbol mask");
                XDestroyImage(bits);
                return;
            }
        }
    }
    for (i = 0; i < count; i++) {
        double size = style->size, r = size / 2.0;
        int skinny = style->type == RBC_RENDER_SYMBOL_SPLUS || style->type == RBC_RENDER_SYMBOL_SCROSS;
        int cross = style->type == RBC_RENDER_SYMBOL_CROSS || style->type == RBC_RENDER_SYMBOL_SCROSS;
        int fill = !skinny && style->fillColor != NULL;
        int stroke = style->outlineColor != NULL && style->outlineWidth > 0;
        Point2D points[12];
        int n = 0;
        Rbc_ExportFormat(token, "q 1 0 0 1 %.8f %.8f cm\n", centers[i].x, centers[i].y);
        if (bits != NULL) {
            double scale = MIN(size / bitmapWidth, size / bitmapHeight);
            Rbc_ExportFormat(token, "%.8f 0 0 %.8f %.8f %.8f cm\n", scale, scale, -bitmapWidth * scale / 2,
                             -bitmapHeight * scale / 2);
            if (mask != NULL) {
                PdfBitmapRuns(token, mask, NULL, style->fillColor);
            } else {
                PdfRectangle(token, 0, 0, bitmapWidth, bitmapHeight, style->fillColor);
            }
            PdfBitmapRuns(token, bits, mask, style->outlineColor);
            Rbc_ExportAppend(token, "Q\n", (char *)NULL);
            continue;
        }
        if (cross) {
            Rbc_ExportAppend(token, "0.70710678 0.70710678 -0.70710678 0.70710678 0 0 cm\n", (char *)NULL);
        }
        PdfColor(token, style->fillColor, FALSE);
        PdfColor(token, style->outlineColor, TRUE);
        Rbc_ExportFormat(token, "%d w 0 J 0 j [] 0 d\n", MAX(1, style->outlineWidth));
        switch (style->type) {
        case RBC_RENDER_SYMBOL_CIRCLE: {
            Rbc_RenderArcGeometry arc = {0, 0, r, r, 0, 360, RBC_RENDER_ARC_CHORD};
            PdfArcPath(token, &arc);
            break;
        }
        case RBC_RENDER_SYMBOL_SQUARE:
            r = Rbc_RenderExportSymbolRound(size * 0.886226925452758) / 2.0;
            Rbc_ExportFormat(token, "%.8f %.8f %.8f %.8f re\n", -r, -r, 2 * r, 2 * r);
            break;
        case RBC_RENDER_SYMBOL_DIAMOND:
            r = Rbc_RenderExportSymbolRound(size * M_SQRT1_2) * M_SQRT1_2;
            points[0] = (Point2D){0, -r};
            points[1] = (Point2D){r, 0};
            points[2] = (Point2D){0, r};
            points[3] = (Point2D){-r, 0};
            n = 4;
            break;
        case RBC_RENDER_SYMBOL_TRIANGLE:
        case RBC_RENDER_SYMBOL_ARROW: {
            double b = Rbc_RenderExportSymbolRound(size * 0.7) * 1.3467736870885982 * 0.5;
            double h = b * 0.86602540378443871;
            double sign = style->type == RBC_RENDER_SYMBOL_ARROW ? -1 : 1;
            points[0] = (Point2D){0, -h * sign};
            points[1] = (Point2D){b, b * 0.57735026918962573 * sign};
            points[2] = (Point2D){-b, b * 0.57735026918962573 * sign};
            n = 3;
            break;
        }
        default: {
            int s = Rbc_RenderExportSymbolRound(size * 0.886226925452758);
            double w = s / 6;
            r = s / 2;
            if (skinny) {
                Rbc_ExportFormat(token, "%.8f 0 m %.8f 0 l 0 %.8f m 0 %.8f l\n", -r, r, -r, r);
            } else {
                points[0] = (Point2D){-r, -w};
                points[1] = (Point2D){-w, -w};
                points[2] = (Point2D){-w, -r};
                points[3] = (Point2D){w, -r};
                points[4] = (Point2D){w, -w};
                points[5] = (Point2D){r, -w};
                points[6] = (Point2D){r, w};
                points[7] = (Point2D){w, w};
                points[8] = (Point2D){w, r};
                points[9] = (Point2D){-w, r};
                points[10] = (Point2D){-w, w};
                points[11] = (Point2D){-r, w};
                n = 12;
            }
        }
        }
        if (n) {
            PdfPath(token, points, n, TRUE);
        }
        Rbc_ExportAppend(token, fill ? (stroke ? "B\n" : "f\n") : (stroke ? "S\n" : "n\n"), "Q\n", (char *)NULL);
    }
    if (mask != NULL) {
        XDestroyImage(mask);
    }
    if (bits != NULL) {
        XDestroyImage(bits);
    }
}

static const Rbc_RenderOutputOps pdfOutputOps = {PdfText,
                                                 PdfPhoto,
                                                 PdfWindow,
                                                 PdfBackgroundPolygon,
                                                 PdfBorder,
                                                 PdfClearRectangle,
                                                 PdfBackgroundRectangles,
                                                 PdfPlotBegin,
                                                 PdfPlotEnd,
                                                 PdfBitmapMask,
                                                 PdfTkImage,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL};

static const Rbc_RenderOps rbcPdfOps = {PdfPolyline,
                                        PdfSegments,
                                        Rbc_RenderExportLineStyle,
                                        Rbc_RenderExportDashBackground,
                                        PdfFillPolygon,
                                        PdfFillRectangles,
                                        PdfSymbolPoints,
                                        &pdfOutputOps,
                                        Rbc_RenderExportEnd,
                                        PdfArc,
                                        PdfFillArc,
                                        PdfClipBegin,
                                        PdfClipEnd};

Rbc_RenderContext *Rbc_RenderCreatePdf(Rbc_ExportContext *token) {
    Rbc_RenderExportState *ctx = ckalloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    ctx->core.ops = &rbcPdfOps;
    ctx->exportPtr = token;
    return &ctx->core;
}
