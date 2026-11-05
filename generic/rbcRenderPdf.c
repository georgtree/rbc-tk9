/* Pdf renderer. See license.terms. */
/** @file rbcRenderPdf.c
 * PDF drawing backend.
 */
/** @defgroup pdf_backend PDF drawing backend
 * Private renderer callbacks; components use the common renderer interface.
 */
#include "rbcRenderExportInt.h"

/**
 * @brief Save graphics state and intersect a rectangular clip.
 * @ingroup pdf_backend
 *
 * Coordinates are mapped display pixels. Emits q/re/W/n; PdfClipEnd must balance the save. Common
 * renderer clip-depth tracking owns pairing, not this callback.
 *
 * @param[in,out] ctx Live Pdf renderer context borrowing the active export document.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Mapped rectangle width in display pixels.
 * @param[in] height Mapped rectangle height in display pixels.
 */
static void PdfClipBegin(Rbc_RenderContext *ctx, int x, int y, int width, int height) {
    Rbc_ExportFormat(RENDER_EXPORT(ctx)->exportPtr, "q %d %d %d %d re W n\n", x, y, width, height);
}
/**
 * @brief Restore one rectangular clipping scope.
 * @ingroup pdf_backend
 *
 * Emits Q only. Requires a matching begin; does not inspect or change common clip depth.
 *
 * @param[in,out] ctx Live Pdf context with a matching rectangular clip scope; common depth is not changed here.
 */
static void PdfClipEnd(Rbc_RenderContext *ctx) { Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "Q\n", (char *)NULL); }

/* ----------------------------------------------------------------------
 * PDF export. Geometry stays in graph coordinates; the document's initial
 * matrix converts pixels to points and reverses the PDF Y axis. Every
 * primitive saves/restores graphics state, independently of Cairo.
 * ---------------------------------------------------------------------- */
/**
 * @brief Select DeviceRGB stroke or fill color.
 * @ingroup pdf_backend
 *
 * Converts 16-bit XColor components to unit values. A NULL color leaves the current color unchanged;
 * this helper does not save graphics state.
 *
 * @param[in,out] token Borrowed initialized export document; output and resource/error state may be updated.
 * @param[in] color Optional borrowed Tk color; NULL leaves the current PDF color unchanged.
 * @param[in] stroke Nonzero selects stroking RGB; zero selects nonstroking RGB.
 */
static void PdfColor(Rbc_ExportContext *token, const XColor *color, int stroke) {
    if (color != NULL) {
        Rbc_ExportFormat(token, "%.8f %.8f %.8f %s\n", color->red / 65535.0, color->green / 65535.0,
                         color->blue / 65535.0, stroke ? "RG" : "rg");
    }
}

/**
 * @brief Append a polygonal path without painting it.
 * @ingroup pdf_backend
 *
 * Borrows count points in mapped pixels and optionally emits h. The caller supplies valid geometry,
 * chooses the fill rule and owns graphics-state scope.
 *
 * @param[in,out] token Borrowed initialized export document; output and resource/error state may be updated.
 * @param[in] points Borrowed mapped point array readable for count vertices.
 * @param[in] count Number of geometry records, not bytes.
 * @param[in] close Nonzero appends a close-path operator; zero leaves the path open.
 */
static void PdfPath(Rbc_ExportContext *token, const Point2D *points, Tcl_Size count, int close) {
    Tcl_Size i;
    for (i = 0; i < count; i++) {
        Rbc_ExportFormat(token, "%.8f %.8f %s\n", points[i].x, points[i].y, i == 0 ? "m" : "l");
    }
    if (close) {
        Rbc_ExportAppend(token, "h\n", (char *)NULL);
    }
}

/**
 * @brief Paint a positive-size solid rectangle in an isolated scope.
 * @ingroup pdf_backend
 *
 * NULL color or nonpositive dimensions produce no output. Uses nonstroking RGB and restores the
 * previous graphics state.
 *
 * @param[in,out] token Borrowed initialized export document; output and resource/error state may be updated.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Mapped rectangle width in display pixels.
 * @param[in] height Mapped rectangle height in display pixels.
 * @param[in] color Borrowed rectangle color; NULL emits no paint.
 */
static void PdfRectangle(Rbc_ExportContext *token, double x, double y, double width, double height,
                         const XColor *color) {
    if (color != NULL && width > 0 && height > 0) {
        Rbc_ExportAppend(token, "q\n", (char *)NULL);
        PdfColor(token, color, FALSE);
        Rbc_ExportFormat(token, "%.8f %.8f %.8f %.8f re f\nQ\n", x, y, width, height);
    }
}

/* Elliptical arcs use cubic Beziers of at most 90 degrees each. */
/**
 * @brief Append an elliptical arc using cubic Bezier pieces.
 * @ingroup pdf_backend
 *
 * Requires normalized finite geometry with nonzero extent from the renderer dispatcher. Each piece
 * spans at most 90 degrees. Negates angles for screen Y, closes full ellipses and chords, and adds the
 * center for partial pieslices. Does not paint or change the transformation matrix.
 *
 * @param[in,out] token Borrowed initialized export document; output and resource/error state may be updated.
 * @param[in] arc Borrowed normalized finite ellipse geometry with positive radii and nonzero extent.
 */
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

/**
 * @brief Emit cached stroke color, width, cap, join and dashes.
 * @ingroup pdf_backend
 *
 * The background pass selects the off-color and an empty dash pattern with zero offset. The foreground
 * pass uses the bounded dash array and its offset. The caller saves/restores graphics state.
 *
 * @param[in,out] ctx Live Pdf stroke context with configured color, width and dash state.
 * @param[in] background Nonzero emits the solid off-color pass; zero selects cached foreground dashes.
 */
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
/**
 * @brief Stroke a connected path, optionally over a solid dash background.
 * @ingroup pdf_backend
 *
 * No foreground color or fewer than two points is a no-op. Each pass reconstructs the whole path and
 * has its own q/Q scope; the input array remains borrowed.
 *
 * @param[in,out] ctx Live Pdf stroke context with configured color, width and dash state.
 * @param[in] points Borrowed mapped point array readable for count vertices.
 * @param[in] count Number of geometry records, not bytes.
 */
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

/**
 * @brief Stroke independent segments in their original painter order.
 * @ingroup pdf_backend
 *
 * Each segment delegates to PdfPolyline, including its background and foreground passes, before the
 * next segment. Does not combine the batch into one shared path.
 *
 * @param[in,out] ctx Live Pdf stroke context with configured color, width and dash state.
 * @param[in] segments Borrowed mapped endpoint pairs readable for count Segment2D records.
 * @param[in] count Number of geometry records, not bytes.
 */
static void PdfSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count) {
    Tcl_Size i;
    for (i = 0; i < count; i++) {
        Point2D points[2] = {segments[i].p, segments[i].q};
        PdfPolyline(ctx, points, 2);
    }
}

/**
 * @brief Stroke normalized arc geometry with the cached line style.
 * @ingroup pdf_backend
 *
 * No foreground color is a no-op. Rebuilds the arc for each dash-background/foreground pass and
 * balances graphics state for each pass.
 *
 * @param[in,out] ctx Live Pdf stroke context with configured color, width and dash state.
 * @param[in] arc Borrowed normalized finite ellipse geometry with positive radii and nonzero extent.
 */
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
/**
 * @brief Convert set bitmap pixels into filled horizontal rectangles.
 * @ingroup pdf_backend
 *
 * Borrows a valid XImage and optional same-sized mask. Emits runs where both images are set; NULL
 * color produces no output. Uses the current transform and graphics scope, paints once with f, and
 * does not release either image.
 *
 * @param[in,out] token Borrowed initialized export document; output and resource/error state may be updated.
 * @param[in] bits Borrowed readable monochrome XImage supplying set pixels.
 * @param[in] mask Optional borrowed same-sized XImage; only pixels set in both images are emitted.
 * @param[in] color Borrowed paint color; omission follows this helper's format-specific policy.
 */
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
/**
 * @brief Copy a Tk photo block into PDF RGB and optional alpha resources.
 * @ingroup pdf_backend
 *
 * Returns an image object ID, or zero for empty/invalid input. Invalid size/storage records an export
 * error. Requires valid Tk channel offsets and row layout. Copies RGB bytes row by row and creates an
 * 8-bit grayscale SMask only if any alpha is nonopaque. Frees temporary DStrings; does not retain the
 * borrowed block or pixel pointer.
 *
 * @param[in,out] token Borrowed initialized export document; output and resource/error state may be updated.
 * @param[in] block Borrowed readable Tk photo block with valid pitch, pixel size and RGB channel offsets.
 */
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

/**
 * @brief Place an image resource using top-left display coordinates.
 * @ingroup pdf_backend
 *
 * A zero resource ID is a no-op. Emits a locally saved matrix with negative image height to correct
 * raster orientation, then invokes the image XObject.
 *
 * @param[in,out] token Borrowed initialized export document; output and resource/error state may be updated.
 * @param[in] id Document-local image object ID; zero emits nothing.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Image display width in pixels.
 * @param[in] height Image display height in pixels; negated in the placement matrix to correct raster orientation.
 */
static void PdfImageUse(Rbc_ExportContext *token, int id, double x, double y, int width, int height) {
    if (id) {
        Rbc_ExportFormat(token, "q\n%d 0 0 %d %.8f %.8f cm /I%d Do\nQ\n", width, -height, x, y + height, id);
    }
}

/**
 * @brief Adapt a packed RBC color image to the photo resource writer.
 * @ingroup pdf_backend
 *
 * Borrows the image, checks that row pitch fits int, and describes Pix32 channel offsets without
 * copying the source first. Resource construction copies the pixels. Returns zero and records an
 * export error for excessive pitch; does not free the image.
 *
 * @param[in,out] token Borrowed initialized export document; output and resource/error state may be updated.
 * @param[in] image Borrowed RBC color image with readable packed pixels; not freed by this helper.
 */
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

/**
 * @brief Create or reuse this fill context's colored tiling pattern.
 * @ingroup pdf_backend
 *
 * Uses a borrowed tile for backgroundOnly fills, otherwise a stipple bitmap. Tile photos use their
 * pixel block; other Tk images use raster capture. Stipples become bitmap runs over an optional
 * background. Restores token->buffer after constructing temporary pattern content and releases
 * temporary images. The pattern matrix includes page scale/Y reversal; tile origin also accounts for
 * graph ancestry. Caches the resource ID until the tile setter invalidates it. Zero means no pattern;
 * the enclosing export error remains authoritative.
 *
 * @param[in,out] ctx Live Pdf fill context with initialized fillStyle and any required graph/tile resources.
 * @param[in] foreground Borrowed resolved pattern foreground color; not retained by the emitter.
 */
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
/**
 * @brief Prepare nonstroking opacity and solid or patterned fill state.
 * @ingroup pdf_backend
 *
 * Returns false for nonpositive opacity, missing solid color or unavailable pattern. Opacity below one
 * uses an ExtGState ca resource. Pattern creation precedes path construction so resource emission
 * cannot consume the drawing path. The caller provides a q/Q scope; this helper is not a Tcl status
 * API.
 *
 * @param[in,out] ctx Live Pdf fill context with initialized fillStyle and any required graph/tile resources.
 */
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

/**
 * @brief Fill a closed polygon with the even-odd rule.
 * @ingroup pdf_backend
 *
 * Establishes fill resources before appending borrowed geometry. Always balances its local graphics
 * save, including when the style produces no fill.
 *
 * @param[in,out] ctx Live Pdf fill context with initialized fillStyle and any required graph/tile resources.
 * @param[in] points Borrowed mapped point array readable for count vertices.
 * @param[in] count Number of geometry records, not bytes.
 */
static void PdfFillPolygon(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count) {
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "q\n", (char *)NULL);
    if (PdfFillStyle(ctx)) {
        PdfPath(RENDER_EXPORT(ctx)->exportPtr, points, count, TRUE);
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "f*\n", (char *)NULL);
    }
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "Q\n", (char *)NULL);
}

/**
 * @brief Fill each supplied rectangle in a separate graphics scope.
 * @ingroup pdf_backend
 *
 * Uses the cached fill style, with shared pattern resource reuse across the batch. Geometry is already
 * mapped; no outline or clipping calculation is performed here.
 *
 * @param[in,out] ctx Live Pdf fill context with initialized fillStyle and any required graph/tile resources.
 * @param[in] rectangles Borrowed mapped rectangle array readable for count entries.
 * @param[in] count Number of geometry records, not bytes.
 */
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

/**
 * @brief Fill normalized ellipse, chord or sector geometry.
 * @ingroup pdf_backend
 *
 * Prepares fill state before constructing the path and uses even-odd fill. The caller selects a
 * fillable arc style; the helper balances graphics state even if filling is disabled.
 *
 * @param[in,out] ctx Live Pdf fill context with initialized fillStyle and any required graph/tile resources.
 * @param[in] arc Borrowed normalized closed ellipse segment; common dispatch excludes open arcs.
 */
static void PdfFillArc(Rbc_RenderContext *ctx, const Rbc_RenderArcGeometry *arc) {
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "q\n", (char *)NULL);
    if (PdfFillStyle(ctx)) {
        PdfArcPath(RENDER_EXPORT(ctx)->exportPtr, arc);
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "f*\n", (char *)NULL);
    }
    Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "Q\n", (char *)NULL);
}

/**
 * @brief Emit anchored, rotated text using document-local font resources.
 * @ingroup pdf_backend
 *
 * Owns and frees a temporary TextLayout. Resolves glyphs before output to widen zero-width Tk
 * measurements when a fallback has positive advance, then recomputes justification. Emits shadow
 * before foreground, selecting active color when requested. Each glyph uses Tk prefix advance plus
 * fallback corrections; genuine zero-width glyphs retain their position. Font/width errors are
 * recorded on the export; this void emitter does not return a completed-document status.
 *
 * @param[in,out] ctx Live Pdf renderer context borrowing the active export document.
 * @param[in] string Borrowed NUL-terminated Tcl UTF text consumed during the call.
 * @param[in] style Borrowed text style with live font/color resources; layout and placement follow its fields.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 */
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

/**
 * @brief Read a borrowed Tk photo and place its copied PDF resource.
 * @ingroup pdf_backend
 *
 * A missing/unreadable photo records an export error. Does not destroy the photo or retain its pixel
 * block.
 *
 * @param[in,out] ctx Live Pdf renderer context borrowing the active export document.
 * @param[in] photo Borrowed Tk photo handle, read synchronously.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 */
static void PdfPhoto(Rbc_RenderContext *ctx, Tk_PhotoHandle photo, double x, double y) {
    Tk_PhotoImageBlock block;
    if (photo == NULL || !Tk_PhotoGetImage(photo, &block)) {
        Rbc_ExportError(RENDER_EXPORT(ctx)->exportPtr, "cannot read photo image for PDF");
        return;
    }
    PdfImageUse(RENDER_EXPORT(ctx)->exportPtr, PdfPhotoResource(RENDER_EXPORT(ctx)->exportPtr, &block), x, y,
                block.width, block.height);
}

/**
 * @brief Rasterize a non-photo Tk image and emit its PDF resource.
 * @ingroup pdf_backend
 *
 * Owns the capture returned by Rbc_RenderExportRasterImage and frees it after resource creation. A
 * NULL capture emits no image; consult the export error for failed captures.
 *
 * @param[in,out] ctx Live Pdf renderer context borrowing the active export document.
 * @param[in] image Borrowed live Tk image; no ownership transfer.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 */
static void PdfTkImage(Rbc_RenderContext *ctx, Tk_Image image, double x, double y) {
    Rbc_ColorImage captured = Rbc_RenderExportRasterImage(RENDER_EXPORT(ctx)->exportPtr, image);
    if (captured != NULL) {
        PdfImageUse(RENDER_EXPORT(ctx)->exportPtr, PdfColorImageResource(RENDER_EXPORT(ctx)->exportPtr, captured), x, y,
                    Rbc_ColorImageWidth(captured), Rbc_ColorImageHeight(captured));
        Rbc_FreeColorImage(captured);
    }
}

/**
 * @brief Capture a mapped window at its current screen dimensions.
 * @ingroup pdf_backend
 *
 * Unmapped or empty windows are omitted. Owns and frees the captured color image; capture failure
 * records an export error. The caller decides whether window export is enabled. This captures pixels,
 * not a reconstructed widget or new clipping container.
 *
 * @param[in,out] ctx Live Pdf renderer context borrowing the active export document.
 * @param[in] window Borrowed live Tk window; capture does not create or resize it.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 */
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

/**
 * @brief Paint a positioned one-bit mask as vector rectangles.
 * @ingroup pdf_backend
 *
 * Missing bitmap/color or nonpositive dimensions are ignored. Acquires and destroys an XImage; failure
 * records an export error. The background selector is unused because the explicit color determines
 * painting. Translation is confined to a q/Q scope.
 *
 * @param[in,out] ctx Live Pdf renderer context borrowing the active export document.
 * @param[in] display Borrowed display owning the source bitmap.
 * @param[in] bitmap Borrowed one-bit pixmap readable over width by height pixels; None suppresses output.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Source bitmap width and mapped extent in pixels; nonpositive values emit nothing.
 * @param[in] height Source bitmap height and mapped extent in pixels; nonpositive values emit nothing.
 * @param[in] color Borrowed paint color; omission follows this helper's format-specific policy.
 * @param[in] background Unused pass selector; the caller supplies the paint color explicitly.
 */
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

/**
 * @brief Paint a solid even-odd background polygon.
 * @ingroup pdf_backend
 *
 * NULL color is a no-op. Borrows mapped points and isolates the RGB fill in a saved graphics scope.
 *
 * @param[in,out] ctx Live Pdf renderer context borrowing the active export document.
 * @param[in] color Borrowed background color; NULL emits no paint.
 * @param[in] points Borrowed mapped point array readable for count vertices.
 * @param[in] count Number of geometry records, not bytes.
 */
static void PdfBackgroundPolygon(Rbc_RenderContext *ctx, const XColor *color, const Point2D *points, Tcl_Size count) {
    if (color != NULL) {
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "q\n", (char *)NULL);
        PdfColor(RENDER_EXPORT(ctx)->exportPtr, color, FALSE);
        PdfPath(RENDER_EXPORT(ctx)->exportPtr, points, count, TRUE);
        Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "f* Q\n", (char *)NULL);
    }
}

/**
 * @brief Adapt border polygon output to a temporary export state.
 * @ingroup pdf_backend
 *
 * Creates a stack-local state borrowing the token and delegates to PdfBackgroundPolygon. The opacity
 * parameter is currently ignored; border callers pass opaque color.
 *
 * @param[in,out] token Borrowed initialized export document; output and resource/error state may be updated.
 * @param[in] points Borrowed mapped point array readable for count vertices.
 * @param[in] count Number of geometry records, not bytes.
 * @param[in] color Borrowed paint color; omission follows this helper's format-specific policy.
 * @param[in] opacity Unused; this border adapter always delegates opaque painting.
 */
static void PdfPolygon(Rbc_ExportContext *token, const Point2D *points, Tcl_Size count, const XColor *color,
                       double opacity) {
    Rbc_RenderExportState ctx;
    (void)opacity;
    memset(&ctx, 0, sizeof(ctx));
    ctx.exportPtr = token;
    PdfBackgroundPolygon(&ctx.core, color, points, count);
}

/**
 * @brief Approximate Tk relief using solid rectangles and a polygon.
 * @ingroup pdf_backend
 *
 * Obtains border colors after forcing lazy light-GC allocation. Optionally fills the interior, then
 * emits raised/sunken/solid edges; groove and ridge recurse into two half-width borders. Flat,
 * nonpositive or undersized borders omit the edges. Borrows the Tk border; does not allocate
 * replacement colors.
 *
 * @param[in,out] ctx Live Pdf renderer context borrowing the active export document.
 * @param[in] border Borrowed Tk 3D border; lazy shadow-color initialization may occur.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Mapped rectangle width in display pixels.
 * @param[in] height Mapped rectangle height in display pixels.
 * @param[in] borderWidth Relief thickness in display pixels; caller keeps size arithmetic representable.
 * @param[in] relief Tk relief constant.
 * @param[in] fill Nonzero paints the interior before relief; zero requests edges only.
 */
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

/**
 * @brief Paint an opaque white rectangle.
 * @ingroup pdf_backend
 *
 * Clearing document output means white paint, not erasing to transparency. Nonpositive dimensions are
 * ignored by PdfRectangle.
 *
 * @param[in,out] ctx Live Pdf renderer context borrowing the active export document.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Mapped rectangle width in display pixels.
 * @param[in] height Mapped rectangle height in display pixels.
 */
static void PdfClearRectangle(Rbc_RenderContext *ctx, double x, double y, int width, int height) {
    XColor white;
    memset(&white, 0, sizeof(white));
    white.red = white.green = white.blue = 65535;
    PdfRectangle(RENDER_EXPORT(ctx)->exportPtr, x, y, width, height, &white);
}

/**
 * @brief Paint background rectangles in order.
 * @ingroup pdf_backend
 *
 * NULL color selects opaque white; otherwise uses the supplied RGB color. Borrows the batch and does
 * not retain geometry.
 *
 * @param[in,out] ctx Live Pdf renderer context borrowing the active export document.
 * @param[in] color Optional borrowed background color; NULL paints using the format clear-background policy.
 * @param[in] rectangles Borrowed mapped rectangle array readable for count entries.
 * @param[in] count Number of geometry records, not bytes.
 */
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

/**
 * @brief Paint the plot background and open its clipping scope.
 * @ingroup pdf_backend
 *
 * NULL background selects white. Ignores the font argument because text resolves its own resources.
 * Emits q/re/W/n after painting; PdfPlotEnd balances this separate plot scope.
 *
 * @param[in,out] ctx Live Pdf renderer context borrowing the active export document.
 * @param[in] font Unused by this backend; retained for the common plot-operation signature.
 * @param[in] x Mapped destination X coordinate in display pixels.
 * @param[in] y Mapped destination Y coordinate in display pixels.
 * @param[in] width Mapped rectangle width in display pixels.
 * @param[in] height Mapped rectangle height in display pixels.
 * @param[in] background Borrowed plot background color; NULL selects the format's clear-background policy.
 */
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

/**
 * @brief Close the saved plot clipping scope.
 * @ingroup pdf_backend
 *
 * Requires a matching PdfPlotBegin. This Q is separate from common renderer clip-depth unwinding.
 *
 * @param[in,out] ctx Live Pdf context with a matching plot scope; common depth is not changed here.
 */
static void PdfPlotEnd(Rbc_RenderContext *ctx) { Rbc_ExportAppend(RENDER_EXPORT(ctx)->exportPtr, "Q\n", (char *)NULL); }

/* Match the SVG/PostScript symbol size corrections and painter order. */
/**
 * @brief Paint symbol centers with legacy size corrections and painter order.
 * @ingroup pdf_backend
 *
 * Bar samples delegate to rectangle fills. Bitmap symbols acquire their source/mask once per batch,
 * require matching mask dimensions when used, and release both XImages on completion or failure. Each
 * center has a local transform; bitmap aspect ratio is preserved. Geometric symbols fill before
 * outlining, skinny symbols only stroke, and cross variants rotate the plus geometry. Borrows style
 * colors and handles throughout the call.
 *
 * @param[in,out] ctx Live Pdf symbol context with selected symbol style and document resources.
 * @param[in] centers Borrowed mapped symbol centers readable for count entries.
 * @param[in] count Number of geometry records, not bytes.
 */
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
