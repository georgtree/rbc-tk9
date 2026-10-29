/* Svg renderer. See license.terms. */
#include "rbcRenderExportInt.h"

typedef struct {
    Rbc_RenderExportState exportState;
    unsigned int bitmapId;
    int bitmapWidth, bitmapHeight;
} SvgRenderContext;

#define SVG(ctx) ((SvgRenderContext *)(ctx))

static void SvgClipBegin(Rbc_RenderContext *ctx, int x, int y, int width, int height) {
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
    unsigned int id = ++token->nextResourceId;
    Rbc_ExportFormat(token,
                     "<defs><clipPath id=\"rbcClip%u\" clipPathUnits=\"userSpaceOnUse\"><rect x=\"%d\" y=\"%d\" "
                     "width=\"%d\" height=\"%d\"/></clipPath></defs>\n<g clip-path=\"url(#rbcClip%u)\">\n",
                     id, x, y, width, height, id);
}

static void SvgClipEnd(Rbc_RenderContext *ctx) {
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "</g>\n", (char *)NULL);
}

static unsigned int SvgFillPattern(Rbc_RenderContext *, const XColor *);

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
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
    int i;

    Rbc_ExportAppend(token, " fill=\"none\" stroke=\"", (char *)NULL);
    SvgColor(token, underlay ? &RENDER_EXPORT(ctx)->vectorOffColor
                             : (RENDER_EXPORT(ctx)->vectorHasColor ? &RENDER_EXPORT(ctx)->vectorColor : NULL));
    Rbc_ExportFormat(token, "\" stroke-width=\"%d\" stroke-linecap=\"%s\" stroke-linejoin=\"%s\"",
                     RENDER_EXPORT(ctx)->vectorWidth,
                     RENDER_EXPORT(ctx)->vectorCap == CapRound
                         ? "round"
                         : (RENDER_EXPORT(ctx)->vectorCap == CapProjecting ? "square" : "butt"),
                     RENDER_EXPORT(ctx)->vectorJoin == JoinRound
                         ? "round"
                         : (RENDER_EXPORT(ctx)->vectorJoin == JoinBevel ? "bevel" : "miter"));
    if (!underlay && RENDER_EXPORT(ctx)->vectorDashes.values[0] != 0) {
        Rbc_ExportAppend(token, " stroke-dasharray=\"", (char *)NULL);
        for (i = 0; i < RBC_MAX_DASH_VALUES && RENDER_EXPORT(ctx)->vectorDashes.values[i] != 0; i++) {
            Rbc_ExportFormat(token, "%s%d", i ? " " : "", RENDER_EXPORT(ctx)->vectorDashes.values[i]);
        }
        Rbc_ExportAppend(token, "\"", (char *)NULL);
    }
}

static void SvgPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    int pass, first = RENDER_EXPORT(ctx)->vectorHasOffColor && RENDER_EXPORT(ctx)->vectorDashes.values[0] ? 0 : 1;
    Tcl_Size i;

    if (count < 2) {
        return;
    }
    for (pass = first; pass < 2; pass++) {
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "<polyline points=\"", (char *)NULL);
        for (i = 0; i < count; i++) {
            Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "%g,%g ", points[i].x, points[i].y);
        }
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "\"", (char *)NULL);
        SvgStrokeAttributes(ctx, pass == 0);
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "/>\n", (char *)NULL);
    }
}

static void SvgSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Tcl_Size i;

    for (i = 0; i < count; i++) {
        Point2D points[2] = {segments[i].p, segments[i].q};

        SvgPolyline(ctx, points, 2);
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
    foreground = RENDER_EXPORT(ctx)->fillStyle.foreground ? RENDER_EXPORT(ctx)->fillStyle.foreground : &black;
    if (RENDER_EXPORT(ctx)->fillStyle.stipple != None || RENDER_EXPORT(ctx)->fillStyle.backgroundOnly) {
        unsigned int id = SvgFillPattern(ctx, foreground);
        if (id == 0) {
            return;
        }
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "<polygon points=\"", (char *)NULL);
        for (i = 0; i < count; i++) {
            Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "%g,%g ", points[i].x, points[i].y);
        }
        Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "\" fill=\"url(#rbcPattern%u)\" fill-rule=\"evenodd\"/>\n", id);
    } else {
        SvgPolygon(RENDER_EXPORT(ctx)->exportPtr, points, count, foreground, RENDER_EXPORT(ctx)->fillStyle.opacity);
    }
}

static void SvgArcPath(Rbc_ExportContext *token, const Rbc_RenderArcGeometry *arc) {
    double start = arc->start * M_PI / 180.0;
    double end = (arc->start + arc->extent) * M_PI / 180.0;
    double x = arc->cx + arc->rx * cos(start);
    double y = arc->cy - arc->ry * sin(start);
    int sweep = arc->extent < 0.0;

    Rbc_ExportFormat(token, "<path d=\"M %g %g", x, y);
    if (fabs(arc->extent) == 360.0) {
        Rbc_ExportFormat(token, " A %g %g 0 0 %d %g %g A %g %g 0 0 %d %g %g Z", arc->rx, arc->ry, sweep,
                         arc->cx - arc->rx * cos(start), arc->cy + arc->ry * sin(start), arc->rx, arc->ry, sweep, x, y);
    } else {
        Rbc_ExportFormat(token, " A %g %g 0 %d %d %g %g", arc->rx, arc->ry, fabs(arc->extent) > 180.0, sweep,
                         arc->cx + arc->rx * cos(end), arc->cy - arc->ry * sin(end));
        if (arc->style == RBC_RENDER_ARC_PIESLICE) {
            Rbc_ExportFormat(token, " L %g %g", arc->cx, arc->cy);
        }
        if (arc->style != RBC_RENDER_ARC_OPEN) {
            Rbc_ExportAppend(token, " Z", (char *)NULL);
        }
    }
    Rbc_ExportAppend(token, "\"", (char *)NULL);
}

static void SvgArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    int pass, first = RENDER_EXPORT(ctx)->vectorHasOffColor && RENDER_EXPORT(ctx)->vectorDashes.values[0] ? 0 : 1;

    for (pass = first; pass < 2; pass++) {
        SvgArcPath(RENDER_EXPORT(ctx)->exportPtr, arc);
        SvgStrokeAttributes(ctx, pass == 0);
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "/>\n", (char *)NULL);
    }
}

static void SvgFillArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    unsigned int id = 0;
    const Rbc_RenderFillStyle *style = &RENDER_EXPORT(ctx)->fillStyle;

    if (style->stipple != None || style->backgroundOnly) {
        id = SvgFillPattern(ctx, style->foreground);
        if (id == 0) {
            return;
        }
    }
    SvgArcPath(RENDER_EXPORT(ctx)->exportPtr, arc);
    if (id != 0) {
        Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, " fill=\"url(#rbcPattern%u)\" fill-rule=\"evenodd\"/>\n", id);
    } else {
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, " fill=\"", (char *)NULL);
        SvgColor(RENDER_EXPORT(ctx)->exportPtr, style->foreground);
        Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "\" fill-opacity=\"%g\" fill-rule=\"evenodd\"/>\n",
                         style->opacity);
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
        if (RENDER_EXPORT(ctx)->fillStyle.opacity != 1.0) {
            Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "<g opacity=\"%g\">\n",
                             RENDER_EXPORT(ctx)->fillStyle.opacity);
        }
        if (RENDER_EXPORT(ctx)->fillStyle.stipple != None || RENDER_EXPORT(ctx)->fillStyle.backgroundOnly) {
            const XColor *foreground = RENDER_EXPORT(ctx)->fillStyle.foreground
                                           ? RENDER_EXPORT(ctx)->fillStyle.foreground
                                           : RENDER_EXPORT(ctx)->fillStyle.background;
            unsigned int id = SvgFillPattern(ctx, foreground);

            if (id == 0) {
                if (RENDER_EXPORT(ctx)->fillStyle.opacity != 1.0) {
                    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "</g>\n", (char *)NULL);
                }
                return;
            }
            Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr,
                             "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"url(#rbcPattern%u)\"/>\n",
                             rect->x, rect->y, rect->width, rect->height, id);
        } else {
            SvgRectangle(RENDER_EXPORT(ctx)->exportPtr, rect->x, rect->y, rect->width, rect->height,
                         RENDER_EXPORT(ctx)->fillStyle.foreground);
        }
        if (RENDER_EXPORT(ctx)->fillStyle.opacity != 1.0) {
            Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "</g>\n", (char *)NULL);
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
    const Rbc_RenderSymbolStyle *style = &RENDER_EXPORT(ctx)->vectorSymbol;
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
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
    SVG(ctx)->bitmapId = ++token->nextResourceId;
    SVG(ctx)->bitmapWidth = width;
    SVG(ctx)->bitmapHeight = height;
    Rbc_ExportFormat(token, "<defs><g id=\"rbcBitmap%u\">\n", SVG(ctx)->bitmapId);
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
    Rbc_ColorImage captured = Rbc_RenderExportRasterImage(RENDER_EXPORT(ctx)->exportPtr, image);

    if (captured != NULL) {
        SvgColorImage(RENDER_EXPORT(ctx)->exportPtr, captured, x, y);
        Rbc_FreeColorImage(captured);
    }
}

/* Stipples use graph pixels; photo tiles retain their toplevel-relative
 * phase. A translated legend sample supplies its own local origin. */
static unsigned int SvgFillPattern(Rbc_RenderContext *ctx, const XColor *foreground) {
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
    const Rbc_RenderFillStyle *style = &RENDER_EXPORT(ctx)->fillStyle;
    Tk_PhotoImageBlock block;
    XImage *bits = NULL;
    Rbc_ColorImage captured = NULL;
    int width, height;
    double originX = 0.0, originY = 0.0;

    if (RENDER_EXPORT(ctx)->vectorPatternId != 0) {
        return RENDER_EXPORT(ctx)->vectorPatternId;
    }
    if (style->backgroundOnly) {
        Tk_Window tkwin;

        if (RENDER_EXPORT(ctx)->vectorTile == NULL) {
            SvgError(token, "missing tile image for SVG");
            return 0;
        }
        if (Rbc_GetTilePhoto(RENDER_EXPORT(ctx)->vectorTile, &block)) {
            width = block.width;
            height = block.height;
        } else {
            captured = Rbc_RenderExportRasterImage(token, Rbc_ImageOfTile(RENDER_EXPORT(ctx)->vectorTile));
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
    RENDER_EXPORT(ctx)->vectorPatternId = ++token->nextResourceId;
    Rbc_ExportFormat(
        token,
        "<defs><pattern id=\"rbcPattern%u\" patternUnits=\"userSpaceOnUse\" patternContentUnits=\"userSpaceOnUse\" "
        "x=\"0\" y=\"0\" width=\"%d\" height=\"%d\" patternTransform=\"translate(%g %g)\">\n",
        RENDER_EXPORT(ctx)->vectorPatternId, width, height, originX, originY);
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
    return RENDER_EXPORT(ctx)->vectorPatternId;
}

static void SvgSymbolPoints(Rbc_RenderContext *ctx, const Point2D *centers, Tcl_Size count) {
    const Rbc_RenderSymbolStyle *style = &RENDER_EXPORT(ctx)->vectorSymbol;
    Tcl_Size i;
    double size = style->size;

    if (RENDER_EXPORT(ctx)->vectorBarSymbol) {
        for (i = 0; i < count; i++) {
            Rbc_RenderRectangle rect = {0, 0, (int)RENDER_EXPORT(ctx)->symbolSize, (int)RENDER_EXPORT(ctx)->symbolSize};

            Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "<g transform=\"translate(%g %g)\">\n",
                             centers[i].x - RENDER_EXPORT(ctx)->symbolSize / 2,
                             centers[i].y - RENDER_EXPORT(ctx)->symbolSize / 2);
            SvgFillRectangles(ctx, &rect, 1);
            Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "</g>\n", (char *)NULL);
        }
        return;
    }
    if (style->type == RBC_RENDER_SYMBOL_BITMAP) {
        double scale;

        if (count <= 0 || style->size <= 0) {
            return;
        }
        if (SVG(ctx)->bitmapId == 0 && !SvgDefineBitmapSymbol(ctx)) {
            return;
        }
        scale = MIN(size / SVG(ctx)->bitmapWidth, size / SVG(ctx)->bitmapHeight);
        for (i = 0; i < count; i++) {
            Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr,
                             "<use href=\"#rbcBitmap%u\" transform=\"translate(%g %g) scale(%g) translate(%g %g)\"/>\n",
                             SVG(ctx)->bitmapId, centers[i].x, centers[i].y, scale, -SVG(ctx)->bitmapWidth / 2.0,
                             -SVG(ctx)->bitmapHeight / 2.0);
        }
        return;
    }
    for (i = 0; i < count; i++) {
        double r = size / 2.0;
        int skinny = style->type == RBC_RENDER_SYMBOL_SPLUS || style->type == RBC_RENDER_SYMBOL_SCROSS;
        int cross = style->type == RBC_RENDER_SYMBOL_CROSS || style->type == RBC_RENDER_SYMBOL_SCROSS;
        Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;

        Rbc_ExportFormat(token, "<g transform=\"translate(%g %g)%s\">", centers[i].x, centers[i].y,
                         cross ? " rotate(45)" : "");
        if (style->type == RBC_RENDER_SYMBOL_CIRCLE) {
            Rbc_ExportFormat(token, "<circle r=\"%g\"", r);
        } else {
            Rbc_ExportAppend(token, "<path d=\"", (char *)NULL);
            switch (style->type) {
            case RBC_RENDER_SYMBOL_SQUARE:
                r = Rbc_RenderExportSymbolRound(size * 0.886226925452758) / 2.0;
                Rbc_ExportFormat(token, "M%g %gH%gV%gH%gZ", -r, -r, r, r, -r);
                break;
            case RBC_RENDER_SYMBOL_DIAMOND:
                r = Rbc_RenderExportSymbolRound(size * M_SQRT1_2) * M_SQRT1_2;
                Rbc_ExportFormat(token, "M0 %gL%g 0 0 %g %g 0Z", -r, r, r, -r);
                break;
            case RBC_RENDER_SYMBOL_TRIANGLE:
            case RBC_RENDER_SYMBOL_ARROW: {
                double b = Rbc_RenderExportSymbolRound(size * 0.7) * 1.3467736870885982 * 0.5;
                double h = b * 0.86602540378443871;
                double sign = style->type == RBC_RENDER_SYMBOL_ARROW ? -1.0 : 1.0;

                Rbc_ExportFormat(token, "M0 %gL%g %g %g %gZ", -h * sign, b, b * 0.57735026918962573 * sign, -b,
                                 b * 0.57735026918962573 * sign);
                break;
            }
            default: {
                double s = Rbc_RenderExportSymbolRound(size * 0.886226925452758);
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
/* Define document-local webfonts after collecting the used fallback faces.
 * Hash the text document so inline SVG documents do not share incompatible
 * character maps merely because their font resource numbers happen to match. */
int Rbc_SvgFontsFinish(Rbc_ExportContext *token) {
    Rbc_ExportFont *font;
    const unsigned char *bytes = (const unsigned char *)Tcl_DStringValue(token->buffer);
    Tcl_Size i, length = Tcl_DStringLength(token->buffer);
    uint64_t hash = UINT64_C(14695981039346656037);
    char prefix[64];

    if (!token->embedFonts) {
        return TCL_OK;
    }
    for (i = 0; i < length; i++) {
        hash = (hash ^ bytes[i]) * UINT64_C(1099511628211);
    }
    for (font = Rbc_FirstExportFont(token); font != NULL; font = font->next) {
        size_t j;
        for (j = 0; j < font->length; j++) {
            hash = (hash ^ font->bytes[j]) * UINT64_C(1099511628211);
        }
    }
    snprintf(prefix, sizeof(prefix), "RbcFont%08x%08x", (unsigned int)(hash >> 32), (unsigned int)hash);
    for (font = Rbc_FirstExportFont(token); font != NULL; font = font->next) {
        Tcl_DString replaced;
        char oldName[64], newName[128];
        const char *start, *match;
        snprintf(oldName, sizeof(oldName), "font-family=\"RbcEmbeddedFont%d\"", font->id);
        snprintf(newName, sizeof(newName), "font-family=\"%s-%d\"", prefix, font->id);
        Tcl_DStringInit(&replaced);
        start = Tcl_DStringValue(token->buffer);
        while ((match = strstr(start, oldName)) != NULL) {
            Tcl_DStringAppend(&replaced, start, match - start);
            Tcl_DStringAppend(&replaced, newName, -1);
            start = match + strlen(oldName);
        }
        Tcl_DStringAppend(&replaced, start, -1);
        Tcl_DStringSetLength(token->buffer, 0);
        Tcl_DStringAppend(token->buffer, Tcl_DStringValue(&replaced), Tcl_DStringLength(&replaced));
        Tcl_DStringFree(&replaced);
    }
    Rbc_ExportAppend(token, "<defs><style type=\"text/css\">\n", (char *)NULL);
    for (font = Rbc_FirstExportFont(token); font != NULL; font = font->next) {
        Tcl_DString program;
        Tcl_DStringInit(&program);
        if (Rbc_FontUnicodeProgram(token, font, &program) != TCL_OK) {
            Tcl_DStringFree(&program);
            return TCL_ERROR;
        }
        Rbc_ExportFormat(token, "@font-face { font-family: '%s-%d'; src: url(data:font/%s;base64,", prefix, font->id,
                         font->isCff ? "otf" : "ttf");
        SvgBase64(token, (const unsigned char *)Tcl_DStringValue(&program), Tcl_DStringLength(&program));
        Rbc_ExportFormat(token, ") format('%s'); font-weight: normal; font-style: normal; }\n",
                         font->isCff ? "opentype" : "truetype");
        Tcl_DStringFree(&program);
    }
    Rbc_ExportAppend(token, "</style></defs>\n", (char *)NULL);
    return TCL_OK;
}

static void SvgEmbeddedText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y) {
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
    TextLayout *layout;
    Point2D anchor = {x, y};
    double width, height, size, advance;
    Rbc_ExportFont *font;
    int cid, pass;
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
            if (Rbc_ExportFontCharacter(token, style->font, ch, &font, &cid, &size, &advance) != TCL_OK) {
                ckfree(layout);
                return;
            }
            if (Tk_TextWidth(style->font, fragment->text + n, consumed) == 0 && advance > 0) {
                double extra = ceil(advance);
                if (extra > INT_MAX - fragment->width - (double)style->padLeft - style->padRight) {
                    SvgError(token, "SVG fallback text is too wide");
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
    Rbc_ExportFormat(token,
                     "<g transform=\"translate(%g %g) rotate(%g) translate(%g %g)\" "
                     "font-weight=\"normal\" font-style=\"normal\">\n",
                     anchor.x + width / 2, anchor.y + height / 2, -style->theta, -layout->width / 2.0,
                     -layout->height / 2.0);
    for (pass = 0; pass < 2; pass++) {
        const XColor *color =
            pass == 0 ? style->shadow.color : ((style->state & STATE_ACTIVE) ? style->activeColor : style->color);
        int offset = pass == 0 ? style->shadow.offset : 0;
        if (color == NULL || (pass == 0 && offset <= 0)) {
            continue;
        }

        for (i = 0; i < layout->nFrags; i++) {
            TextFragment *fragment = layout->fragArr + i;
            Tcl_Size n = 0;
            int extra = 0;
            Rbc_ExportAppend(token, "<text xml:space=\"preserve\" fill=\"", (char *)NULL);
            SvgColor(token, color);
            Rbc_ExportAppend(token, "\">", (char *)NULL);
            while (n < fragment->count) {
                Tcl_UniChar ch;
                int consumed = Tcl_UtfToUniChar(fragment->text + n, &ch);
                if (Rbc_ExportFontCharacter(token, style->font, ch, &font, &cid, &size, &advance) != TCL_OK) {
                    break;
                }
                Rbc_ExportFormat(
                    token, "<tspan font-family=\"RbcEmbeddedFont%d\" font-size=\"%.8f\" x=\"%d\" y=\"%d\">", font->id,
                    size, fragment->x + offset + extra + Tk_TextWidth(style->font, fragment->text, n),
                    fragment->y + offset);
                SvgString(token, fragment->text + n, consumed);
                Rbc_ExportAppend(token, "</tspan>", (char *)NULL);
                if (Tk_TextWidth(style->font, fragment->text + n, consumed) == 0 && advance > 0) {
                    extra += (int)ceil(advance);
                }
                n += consumed;
            }
            Rbc_ExportAppend(token, "</text>\n", (char *)NULL);
        }
    }
    Rbc_ExportAppend(token, "</g>\n", (char *)NULL);
    ckfree(layout);
}

static void SvgText(Rbc_RenderContext *ctx, char *string, TextStyle *style, double x, double y) {
    Rbc_ExportContext *token = RENDER_EXPORT(ctx)->exportPtr;
    Tcl_InterpState saved;
    Tcl_Obj *args[5], *attributes;
    Tcl_Obj **values;
    Tcl_Size n, i;
    const char *family = "sans-serif", *weight = "normal", *slant = "normal";
    int size = 12, pass;
    double pixelSize, width, height;
    TextLayout *layout;
    Point2D anchor = {x, y};

    if (token->embedFonts) {
        SvgEmbeddedText(ctx, string, style, x, y);
        return;
    }
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
        SvgError(RENDER_EXPORT(ctx)->exportPtr, "missing photo image for SVG");
        return;
    }
    if (!Tk_PhotoGetImage(photo, &block)) {
        SvgError(RENDER_EXPORT(ctx)->exportPtr, "cannot read photo image for SVG");
        return;
    }
    SvgPhotoBlock(RENDER_EXPORT(ctx)->exportPtr, &block, x, y);
}

static void SvgWindow(Rbc_RenderContext *ctx, Tk_Window window, double x, double y) {
    Rbc_ColorImage captured;

    if (!Tk_IsMapped(window) || Tk_Width(window) <= 0 || Tk_Height(window) <= 0) {
        return;
    }
    captured = Rbc_DrawableToColorImage(window, Tk_WindowId(window), 0, 0, Tk_Width(window), Tk_Height(window), GAMMA);
    if (captured == NULL) {
        SvgError(RENDER_EXPORT(ctx)->exportPtr, "cannot capture window marker for SVG");
        return;
    }
    SvgColorImage(RENDER_EXPORT(ctx)->exportPtr, captured, x, y);
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
        SvgError(RENDER_EXPORT(ctx)->exportPtr, "cannot read bitmap marker mask for SVG");
        return;
    }
    Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "<g transform=\"translate(%g %g)\">\n", x, y);
    SvgBitmapRuns(RENDER_EXPORT(ctx)->exportPtr, bits, NULL, color);
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "</g>\n", (char *)NULL);
    XDestroyImage(bits);
}

static void SvgBackgroundPolygon(Rbc_RenderContext *ctx, const XColor *color, const Point2D *points, Tcl_Size count) {
    SvgPolygon(RENDER_EXPORT(ctx)->exportPtr, points, count, color, 1.0);
}

static void SvgBorder(Rbc_RenderContext *ctx, Tk_3DBorder border, double x, double y, int width, int height,
                      int borderWidth, int relief, int fill) {
    XColor bg, dark, light;
    const XColor *top, *bottom;
    Point2D points[6];

    /* Shadow colors are allocated lazily, possibly after the first export. */
    (void)Tk_3DBorderGC(RENDER_EXPORT(ctx)->exportPtr->tkwin, border, TK_3D_LIGHT_GC);
    Tk_Get3DBorderColors(border, &bg, &dark, &light);
    if (fill) {
        SvgRectangle(RENDER_EXPORT(ctx)->exportPtr, x, y, width, height, &bg);
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
    SvgRectangle(RENDER_EXPORT(ctx)->exportPtr, x, y + height - borderWidth, width, borderWidth, bottom);
    SvgRectangle(RENDER_EXPORT(ctx)->exportPtr, x + width - borderWidth, y, borderWidth, height, bottom);
    points[0] = (Point2D){x, y + height};
    points[1] = (Point2D){x, y};
    points[2] = (Point2D){x + width, y};
    points[3] = (Point2D){x + width - borderWidth, y + borderWidth};
    points[4] = (Point2D){x + borderWidth, y + borderWidth};
    points[5] = (Point2D){x + borderWidth, y + height - borderWidth};
    SvgPolygon(RENDER_EXPORT(ctx)->exportPtr, points, 6, top, 1.0);
}

static void SvgClearRectangle(Rbc_RenderContext *ctx, double x, double y, int width, int height) {
    XColor white;
    memset(&white, 0, sizeof(white));
    white.red = white.green = white.blue = 65535;
    SvgRectangle(RENDER_EXPORT(ctx)->exportPtr, x, y, width, height, &white);
}

static void SvgBackgroundRectangles(Rbc_RenderContext *ctx, const XColor *color, const Rbc_RenderRectangle *rectangles,
                                    Tcl_Size count) {
    Tcl_Size i;
    for (i = 0; i < count; i++) {
        const Rbc_RenderRectangle *r = rectangles + i;
        if (color == NULL) {
            SvgClearRectangle(ctx, r->x, r->y, r->width, r->height);
        } else {
            SvgRectangle(RENDER_EXPORT(ctx)->exportPtr, r->x, r->y, r->width, r->height, color);
        }
    }
}

static void SvgPlotBegin(Rbc_RenderContext *ctx, Tk_Font font, double x, double y, int width, int height,
                         const XColor *background) {
    (void)font;
    if (background == NULL) {
        SvgClearRectangle(ctx, x, y, width, height);
    } else {
        SvgRectangle(RENDER_EXPORT(ctx)->exportPtr, x, y, width, height, background);
    }
    Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr,
                     "<defs><clipPath id=\"rbcPlot\" clipPathUnits=\"userSpaceOnUse\"><rect x=\"%g\" y=\"%g\" "
                     "width=\"%d\" height=\"%d\"/></clipPath></defs>\n<g clip-path=\"url(#rbcPlot)\">\n",
                     x, y, width, height);
}

static void SvgPlotEnd(Rbc_RenderContext *ctx) {
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "</g>\n", (char *)NULL);
}

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
                                                 SvgTkImage,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL,
                                                 NULL};

static const Rbc_RenderOps rbcSvgOps = {SvgPolyline,
                                        SvgSegments,
                                        Rbc_RenderExportLineStyle,
                                        Rbc_RenderExportDashBackground,
                                        SvgFillPolygon,
                                        SvgFillRectangles,
                                        SvgSymbolPoints,
                                        &svgOutputOps,
                                        Rbc_RenderExportEnd,
                                        SvgArc,
                                        SvgFillArc,
                                        SvgClipBegin,
                                        SvgClipEnd};

Rbc_RenderContext *Rbc_RenderCreateSvg(Rbc_ExportContext *token) {
    SvgRenderContext *ctx = ckalloc(sizeof(*ctx));
    memset(ctx, 0, sizeof(*ctx));
    ctx->exportState.core.ops = &rbcSvgOps;
    ctx->exportState.exportPtr = token;
    return &ctx->exportState.core;
}
