/* rbcRenderNative.c -- Native screen geometry and batching.
 * See license.terms for copyright and redistribution terms.
 *
 * Geometry is already mapped by the components. GCs, tiles and bitmap resources
 * are borrowed; the existing integer conversions and platform batching limits
 * are deliberately retained. */
#include "rbcRenderNative.h"
#include <stdint.h>
#include <X11/Xutil.h>

/* Keep Tk font selection, multiline layout, rotation, padding, anchors and
 * disabled/active text styling in the existing text helpers. Cached marker
 * layouts must not be rebuilt, and axis limits need the original rotated
 * dimensions to position subsequent labels. No text resources are retained. */
void Rbc_NativeText(Tk_Window tkwin, Drawable drawable, const char *string, TextStyle *style, int x, int y) {
    Rbc_DrawText(tkwin, drawable, string, style, x, y);
}

void Rbc_NativeTextLayout(Tk_Window tkwin, Drawable drawable, TextLayout *layout, TextStyle *style, int x, int y) {
    Rbc_DrawTextLayout(tkwin, drawable, layout, style, x, y);
}

void Rbc_NativeTextSize(Tk_Window tkwin, Drawable drawable, const char *string, TextStyle *style,
                        int x, int y, Dim2D *size) {
    Rbc_DrawText2(tkwin, drawable, string, style, x, y, size);
}

/* Present already mapped bitmaps using the original private GC state. Keep
 * the polygon conversion, Convex fill rule, mask origin and XCopyPlane request
 * unchanged: Tk implements these operations through GDI on Windows. */
void Rbc_NativeBitmap(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *r,
                       const Rbc_RenderBitmapStyle *style) {
    if (style->backgroundCount > 0) {
        XPoint polygon[RBC_RENDER_BITMAP_OUTLINE_POINTS];
        Tcl_Size i;

        assert(style->backgroundCount <= RBC_RENDER_BITMAP_OUTLINE_POINTS);
        for (i = 0; i < style->backgroundCount; i++) {
            polygon[i].x = (short int)style->backgroundPoints[i].x;
            polygon[i].y = (short int)style->backgroundPoints[i].y;
        }
        XFillPolygon(graphPtr->display, drawable, style->fillGC, polygon, (int)style->backgroundCount,
                     Convex, CoordModeOrigin);
    }
    XSetClipMask(graphPtr->display, style->gc, style->mask);
    if (style->mask != None) {
        XSetClipOrigin(graphPtr->display, style->gc, r->x, r->y);
    } else {
        XSetClipOrigin(graphPtr->display, style->gc, 0, 0);
    }
    XCopyPlane(graphPtr->display, style->bitmap, drawable, style->gc, 0, 0, r->width, r->height, r->x, r->y, 1);
}

/* Tk retains responsibility for image types, alpha compositing and image
 * resource ownership. The supplied image may be a mapped/scaled temporary. */
void Rbc_NativeImage(Drawable drawable, Tk_Image image, int x, int y) {
    int width, height;

    Tk_SizeOfImage(image, &width, &height);
    Tk_RedrawImage(image, 0, 0, width, height, drawable, x, y);
}

void Rbc_NativeCopyArea(Tk_Window tkwin, Drawable source, Drawable destination, GC gc,
                        int srcX, int srcY, unsigned int width, unsigned int height, int dstX, int dstY) {
    XCopyArea(Tk_Display(tkwin), source, destination, gc, srcX, srcY, width, height, dstX, dstY);
}

void Rbc_NativePixmap(Graph *graphPtr, Drawable drawable, Pixmap pixmap, GC gc, const Rbc_RenderRectangle *r) {
    Rbc_NativeCopyArea(graphPtr->tkwin, pixmap, drawable, gc, 0, 0, r->width, r->height, r->x, r->y);
}

/* Widget decorations intentionally use Tk/native painting even when element
 * geometry uses Cairo. Tile origins and borrowed GCs are configured by the
 * component; preserve relief shading and the original fill/draw distinction. */
void Rbc_NativeBorder(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y,
                       int width, int height, int borderWidth, int relief, int fill) {
    if (fill) {
        Rbc_Fill3DRectangle(tkwin, drawable, border, x, y, width, height, borderWidth, relief);
    } else {
        Rbc_Draw3DRectangle(tkwin, drawable, border, x, y, width, height, borderWidth, relief);
    }
}

void Rbc_NativeRectangle(Tk_Window tkwin, Drawable drawable, GC gc, Rbc_Tile tile,
                          int x, int y, int width, int height) {
    if (tile != NULL) {
        Rbc_TileRectangle(tkwin, drawable, tile, x, y, width, height);
    } else {
        XFillRectangle(Tk_Display(tkwin), drawable, gc, x, y, (unsigned int)width, (unsigned int)height);
    }
}

void Rbc_NativeFocus(Tk_Window tkwin, Drawable drawable, GC gc, int width) {
    Tk_DrawFocusHighlight(tkwin, gc, width, drawable);
}


void Rbc_NativeSegments(Graph *graphPtr, Drawable drawable, GC gc,
    const Segment2D *segments, Tcl_Size count) {
    Rbc_Draw2DSegments(graphPtr->display, drawable, gc, segments, count);
}

INLINE static int Round(double x) { return (int)(x + ((x < 0.0) ? -0.5 : 0.5)); }

#define DRAW_SYMBOL(style) (((style)->counter % (style)->interval) == 0)

#define MAX_DRAWLINES(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWPOLYGON(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWSEGMENTS(d) Rbc_MaxRequestSize(d, sizeof(XSegment))
#define MAX_DRAWRECTANGLES(d) Rbc_MaxRequestSize(d, sizeof(XRectangle))
#define MAX_DRAWARCS(d) Rbc_MaxRequestSize(d, sizeof(XArc))

void Rbc_NativeSymbolPoints(Graph *graphPtr, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, int size,
                                 Tcl_Size nSymbolPts, const Point2D *symbolPts) {
    XPoint pattern[13]; /* Template for polygon symbols */
    int r1, r2;
    Tcl_Size i, n;
    Tcl_Size count;
    const Point2D *pointPtr, *endPtr;
#define SQRT_PI 1.77245385090552
#define S_RATIO 0.886226925452758

    if (size < 3) {
        if ((style->fillGC != NULL) && (nSymbolPts > 0)) {
            XPoint *points;
            Tcl_Size offset;
            int maxPoints;

            maxPoints = Rbc_MaxRequestSize(graphPtr->display, sizeof(XPoint));
            if (maxPoints < 1) {
                return;
            }
            if ((size_t)maxPoints > SIZE_MAX / sizeof(*points)) {
                return;
            }
            points = Tcl_AttemptAlloc((size_t)maxPoints * sizeof(*points));
            if (points == NULL) {
                return;
            }
            offset = 0;
            while (offset < nSymbolPts) {
                Tcl_Size remaining;
                int chunk;
                int j;

                remaining = nSymbolPts - offset;
                chunk = (remaining > (Tcl_Size)maxPoints) ? maxPoints : (int)remaining;
                for (j = 0; j < chunk; j++) {
                    points[j].x = (short int)symbolPts[offset + j].x;
                    points[j].y = (short int)symbolPts[offset + j].y;
                }
                XDrawPoints(graphPtr->display, drawable, style->fillGC, points, chunk, CoordModeOrigin);
                offset += chunk;
            }
            ckfree(points);
        }
        return;
    }
    r1 = (int)ceil(size * 0.5);
    r2 = (int)ceil(size * S_RATIO * 0.5);
    switch (style->type) {
    case RBC_RENDER_SYMBOL_NONE:
        break;
    case RBC_RENDER_SYMBOL_SQUARE:
        Rbc_NativeSquares(graphPtr->display, drawable, style, nSymbolPts, symbolPts, r2);
        break;
    case RBC_RENDER_SYMBOL_CIRCLE:
        Rbc_NativeCircles(graphPtr->display, drawable, style, nSymbolPts, symbolPts, r1);
        break;
    case RBC_RENDER_SYMBOL_SPLUS:
    case RBC_RENDER_SYMBOL_SCROSS: {
        XSegment *segArr; /* Array of line segments (splus, scross) */
        register XSegment *segPtr;
        Tcl_Size nSegs;
        int reqSize;
        int chunk;

        if (style->type == RBC_RENDER_SYMBOL_SCROSS) {
            r2 = Round(r2 * M_SQRT1_2);
            pattern[3].y = pattern[2].x = pattern[0].x = pattern[0].y = -r2;
            pattern[3].x = pattern[2].y = pattern[1].y = pattern[1].x = r2;
        } else {
            pattern[0].y = pattern[1].y = pattern[2].x = pattern[3].x = 0;
            pattern[0].x = pattern[2].y = -r2;
            pattern[1].x = pattern[3].y = r2;
        }
        if ((nSymbolPts > TCL_SIZE_MAX / 2) || ((size_t)nSymbolPts > SIZE_MAX / (2 * sizeof(*segArr)))) {
            return;
        }
        segArr = Tcl_AttemptAlloc((size_t)nSymbolPts * 2 * sizeof(*segArr));
        if (segArr == NULL) {
            return;
        }
        segPtr = segArr;
        if (style->interval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(style)) {
                    segPtr->x1 = pattern[0].x + (short int)pointPtr->x;
                    segPtr->y1 = pattern[0].y + (short int)pointPtr->y;
                    segPtr->x2 = pattern[1].x + (short int)pointPtr->x;
                    segPtr->y2 = pattern[1].y + (short int)pointPtr->y;
                    segPtr++;
                    segPtr->x1 = pattern[2].x + (short int)pointPtr->x;
                    segPtr->y1 = pattern[2].y + (short int)pointPtr->y;
                    segPtr->x2 = pattern[3].x + (short int)pointPtr->x;
                    segPtr->y2 = pattern[3].y + (short int)pointPtr->y;
                    segPtr++;
                    count++;
                }
                style->counter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                segPtr->x1 = pattern[0].x + (short int)pointPtr->x;
                segPtr->y1 = pattern[0].y + (short int)pointPtr->y;
                segPtr->x2 = pattern[1].x + (short int)pointPtr->x;
                segPtr->y2 = pattern[1].y + (short int)pointPtr->y;
                segPtr++;
                segPtr->x1 = pattern[2].x + (short int)pointPtr->x;
                segPtr->y1 = pattern[2].y + (short int)pointPtr->y;
                segPtr->x2 = pattern[3].x + (short int)pointPtr->x;
                segPtr->y2 = pattern[3].y + (short int)pointPtr->y;
                segPtr++;
            }
        }
        nSegs = count * 2;
        reqSize = MAX_DRAWSEGMENTS(graphPtr->display);
        if (reqSize < 1) {
            ckfree(segArr);
            return;
        }
        for (i = 0; i < nSegs; i += (Tcl_Size)reqSize) {
            Tcl_Size remaining;

            remaining = nSegs - i;
            chunk = (remaining > (Tcl_Size)reqSize) ? reqSize : (int)remaining;
            XDrawSegments(graphPtr->display, drawable, style->outlineGC, segArr + i, chunk);
        }
        ckfree((char *)segArr);
    } break;
    case RBC_RENDER_SYMBOL_PLUS:
    case RBC_RENDER_SYMBOL_CROSS: {
        XPoint *polygon;
        register XPoint *p;
        int d; /* Small delta for cross/plus thickness */

        d = (r2 / 3);
        /*
         *
         *          2   3       The plus/cross symbol is a closed polygon
         *                      of 12 points. The diagram to the left
         *    0,12  1   4    5  represents the positions of the points
         *           x,y        which are computed below. The extra
         *     11  10   7    6  (thirteenth) point connects the first and
         *                      last points.
         *          9   8
         */
        pattern[0].x = pattern[11].x = pattern[12].x = -r2;
        pattern[2].x = pattern[1].x = pattern[10].x = pattern[9].x = -d;
        pattern[3].x = pattern[4].x = pattern[7].x = pattern[8].x = d;
        pattern[5].x = pattern[6].x = r2;
        pattern[2].y = pattern[3].y = -r2;
        pattern[0].y = pattern[1].y = pattern[4].y = pattern[5].y = pattern[12].y = -d;
        pattern[11].y = pattern[10].y = pattern[7].y = pattern[6].y = d;
        pattern[9].y = pattern[8].y = r2;
        if (style->type == RBC_RENDER_SYMBOL_CROSS) {
            double dx, dy;

            /* For the cross symbol, rotate the points by 45 degrees. */
            for (n = 0; n < 12; n++) {
                dx = (double)pattern[n].x * M_SQRT1_2;
                dy = (double)pattern[n].y * M_SQRT1_2;
                pattern[n].x = Round(dx - dy);
                pattern[n].y = Round(dx + dy);
            }
            pattern[12] = pattern[0];
        }
        if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / (13u * sizeof(*polygon)))) {
            return;
        }
        polygon = Tcl_AttemptAlloc((size_t)nSymbolPts * 13u * sizeof(*polygon));
        if (polygon == NULL) {
            return;
        }
        p = polygon;
        if (style->interval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(style)) {
                    for (n = 0; n < 13; n++) {
                        p->x = pattern[n].x + (short int)pointPtr->x;
                        p->y = pattern[n].y + (short int)pointPtr->y;
                        p++;
                    }
                    count++;
                }
                style->counter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                for (n = 0; n < 13; n++) {
                    p->x = pattern[n].x + (short int)pointPtr->x;
                    p->y = pattern[n].y + (short int)pointPtr->y;
                    p++;
                }
            }
        }
        Rbc_NativePolygonSymbols(graphPtr->display, drawable, style, polygon, count, 13, 12);
        ckfree((char *)polygon);
    } break;
    case RBC_RENDER_SYMBOL_DIAMOND: {
        XPoint *polygon;
        register XPoint *p;
        /*
         *
         *                      The plus symbol is a closed polygon
         *            1         of 4 points. The diagram to the left
         *                      represents the positions of the points
         *       0,4 x,y  2     which are computed below. The extra
         *                      (fifth) point connects the first and
         *            3         last points.
         *
         */
        pattern[1].y = pattern[0].x = -r1;
        pattern[2].y = pattern[3].x = pattern[0].y = pattern[1].x = 0;
        pattern[3].y = pattern[2].x = r1;
        pattern[4] = pattern[0];
        if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / (5u * sizeof(*polygon)))) {
            return;
        }
        polygon = Tcl_AttemptAlloc((size_t)nSymbolPts * 5u * sizeof(*polygon));
        if (polygon == NULL) {
            return;
        }
        p = polygon;
        if (style->interval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(style)) {
                    for (n = 0; n < 5; n++, p++) {
                        p->x = pattern[n].x + (short int)pointPtr->x;
                        p->y = pattern[n].y + (short int)pointPtr->y;
                    }
                    count++;
                }
                style->counter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                for (n = 0; n < 5; n++, p++) {
                    p->x = pattern[n].x + (short int)pointPtr->x;
                    p->y = pattern[n].y + (short int)pointPtr->y;
                }
            }
        }
        Rbc_NativePolygonSymbols(graphPtr->display, drawable, style, polygon, count, 5, 4);
        ckfree((char *)polygon);
    } break;
    case RBC_RENDER_SYMBOL_TRIANGLE:
    case RBC_RENDER_SYMBOL_ARROW: {
        XPoint *polygon;
        register XPoint *p;
        double b;
        int b2, h1, h2;
#define H_RATIO 1.1663402261671607
#define B_RATIO 1.3467736870885982
#define TAN30 0.57735026918962573
#define COS30 0.86602540378443871

        b = Round(size * B_RATIO * 0.7);
        b2 = Round(b * 0.5);
        h2 = Round(TAN30 * b2);
        h1 = Round(b2 / COS30);
        /*
         *
         *                      The triangle symbol is a closed polygon
         *           0,3         of 3 points. The diagram to the left
         *                      represents the positions of the points
         *           x,y        which are computed below. The extra
         *                      (fourth) point connects the first and
         *      2           1   last points.
         *
         */
        if (style->type == RBC_RENDER_SYMBOL_ARROW) {
            pattern[3].x = pattern[0].x = 0;
            pattern[3].y = pattern[0].y = h1;
            pattern[1].x = b2;
            pattern[2].y = pattern[1].y = -h2;
            pattern[2].x = -b2;
        } else {
            pattern[3].x = pattern[0].x = 0;
            pattern[3].y = pattern[0].y = -h1;
            pattern[1].x = b2;
            pattern[2].y = pattern[1].y = h2;
            pattern[2].x = -b2;
        }
        if ((nSymbolPts <= 0) || ((size_t)nSymbolPts > SIZE_MAX / (4u * sizeof(*polygon)))) {
            return;
        }
        polygon = Tcl_AttemptAlloc((size_t)nSymbolPts * 4u * sizeof(*polygon));
        if (polygon == NULL) {
            return;
        }
        p = polygon;
        if (style->interval > 0) {
            count = 0;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(style)) {
                    for (n = 0; n < 4; n++) {
                        p->x = pattern[n].x + (short int)pointPtr->x;
                        p->y = pattern[n].y + (short int)pointPtr->y;
                        p++;
                    }
                    count++;
                }
                style->counter++;
            }
        } else {
            count = nSymbolPts;
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                for (n = 0; n < 4; n++) {
                    p->x = pattern[n].x + (short int)pointPtr->x;
                    p->y = pattern[n].y + (short int)pointPtr->y;
                    p++;
                }
            }
        }
        Rbc_NativePolygonSymbols(graphPtr->display, drawable, style, polygon, count, 4, 3);
        ckfree((char *)polygon);
    } break;
    case RBC_RENDER_SYMBOL_BITMAP: {
        Pixmap bitmap, mask;
        int width, height, bmWidth, bmHeight;
        double scale, sx, sy;
        int dx, dy;
        int x, y;

        Tk_SizeOfBitmap(graphPtr->display, style->bitmap, &width, &height);
        mask = None;
        /*
         * Compute the size of the scaled bitmap.  Stretch the
         * bitmap to fit a nxn bounding box.
         */
        sx = (double)size / (double)width;
        sy = (double)size / (double)height;
        scale = MIN(sx, sy);
        bmWidth = (int)(width * scale);
        bmHeight = (int)(height * scale);
        XSetClipMask(graphPtr->display, style->outlineGC, None);
        if (style->mask != None) {
            mask = Rbc_ScaleBitmap(graphPtr->tkwin, style->mask, width, height, bmWidth, bmHeight);
            XSetClipMask(graphPtr->display, style->outlineGC, mask);
        }
        bitmap = Rbc_ScaleBitmap(graphPtr->tkwin, style->bitmap, width, height, bmWidth, bmHeight);
        if (style->fillGC == NULL) {
            XSetClipMask(graphPtr->display, style->outlineGC, bitmap);
        }
        dx = bmWidth / 2;
        dy = bmHeight / 2;
        if (style->interval > 0) {
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                if (DRAW_SYMBOL(style)) {
                    x = (int)pointPtr->x - dx;
                    y = (int)pointPtr->y - dy;
                    if ((style->fillGC == NULL) || (mask != None)) {
                        XSetClipOrigin(graphPtr->display, style->outlineGC, x, y);
                    }
                    XCopyPlane(graphPtr->display, bitmap, drawable, style->outlineGC, 0, 0, bmWidth, bmHeight,
                               x, y, 1);
                }
                style->counter++;
            }
        } else {
            for (pointPtr = symbolPts, endPtr = symbolPts + nSymbolPts; pointPtr < endPtr; pointPtr++) {
                x = (int)pointPtr->x - dx;
                y = (int)pointPtr->y - dy;
                if ((style->fillGC == NULL) || (mask != None)) {
                    XSetClipOrigin(graphPtr->display, style->outlineGC, x, y);
                }
                XCopyPlane(graphPtr->display, bitmap, drawable, style->outlineGC, 0, 0, bmWidth, bmHeight, x, y,
                           1);
            }
        }
        Tk_FreePixmap(graphPtr->display, bitmap);
        if (mask != None) {
            Tk_FreePixmap(graphPtr->display, mask);
        }
    } break;
    }
}

void Rbc_NativeFillPolygon(Graph *graphPtr, Drawable drawable, GC gc, Rbc_Tile tile,
    Pixmap stipple, const Point2D *points, Tcl_Size count) {
    XPoint *nativePoints;
    Tcl_Size i;
    int n;
    if (points == NULL || (tile != NULL && Rbc_PixmapOfTile(tile) == None)) return;
    n = Rbc_NativePolygonPointCount(graphPtr->display, count);
    if (n == 0) {
        return;
    }
    nativePoints = Tcl_AttemptAlloc((size_t)n * sizeof(*nativePoints));
    if (nativePoints == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        nativePoints[i].x = (short int)points[i].x;
        nativePoints[i].y = (short int)points[i].y;
    }
    if (tile != NULL) {
        Rbc_SetTileOrigin(graphPtr->tkwin, tile, 0, 0);
        Rbc_TilePolygon(graphPtr->tkwin, drawable, tile, nativePoints, n);
    } else if (stipple != None) {
        XFillPolygon(graphPtr->display, drawable, gc, nativePoints, n, Complex, CoordModeOrigin);
    }
    ckfree(nativePoints);
}


/*
 * -----------------------------------------------------------------
 *
 * DrawBarSegments --
 *
 *      Draws each of the rectangular segments for the element.
 *
 * Parameters:
 *      Graph *graphPtr
 *      Drawable drawable - Pixmap or window to draw into
 *      const Rbc_RenderBarStyle *penPtr
 *      const Rbc_RenderRectangle *rectangles
 *      int nRects
 *
 * Results:
 *      None.
 *
 * Side Effects:
 *      TODO: Side Effects
 *
 * -----------------------------------------------------------------
 */
void Rbc_NativeBars(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr, const Rbc_RenderRectangle *rectangles,
                            Tcl_Size nRects) {
    const Rbc_RenderRectangle *rectPtr;
    const Rbc_RenderRectangle *endPtr;
    XRectangle *xRects;
    Tcl_Size i;
    int maxRects;
    int nBuffered;

    if ((nRects <= 0) || ((penPtr->border == NULL) && (penPtr->fgColor == NULL))) {
        return;
    }
    if (penPtr->fillOpacity > 0.0) {
        {
            maxRects = Rbc_MaxRequestSize(graphPtr->display, sizeof(XRectangle));
            if (maxRects < 1) {
                return;
            }
            xRects = Tcl_AttemptAlloc((size_t)maxRects * sizeof(*xRects));
            if (xRects == NULL) {
                return;
            }
            nBuffered = 0;
            for (i = 0; i < nRects; i++) {
                const Rbc_RenderRectangle *srcPtr;

                srcPtr = rectangles + i;
                /*
                 * XRectangle itself uses signed 16-bit coordinates and
                 * unsigned 16-bit dimensions.  Preserve batching for the
                 * normal case and fall back to the int-based Xlib call when
                 * a rectangle does not fit that representation.
                 */
                if ((srcPtr->x >= SHRT_MIN) && (srcPtr->x <= SHRT_MAX) && (srcPtr->y >= SHRT_MIN) &&
                    (srcPtr->y <= SHRT_MAX) && (srcPtr->width >= 0) && (srcPtr->width <= USHRT_MAX) &&
                    (srcPtr->height >= 0) && (srcPtr->height <= USHRT_MAX)) {
                    xRects[nBuffered].x = (short)srcPtr->x;
                    xRects[nBuffered].y = (short)srcPtr->y;
                    xRects[nBuffered].width = (unsigned short)srcPtr->width;
                    xRects[nBuffered].height = (unsigned short)srcPtr->height;
                    nBuffered++;
                    if (nBuffered == maxRects) {
                        Rbc_NativeBarRectangles(graphPtr, drawable, penPtr, xRects, nBuffered);
                        nBuffered = 0;
                    }
                } else {
                    /*
                     * Flush the batched XRectangle request before issuing the
                     * wider int-coordinate API call.
                     */
                    if (nBuffered > 0) {
                        Rbc_NativeBarRectangles(graphPtr, drawable, penPtr, xRects, nBuffered);
                        nBuffered = 0;
                    }
                    if ((srcPtr->width > 0) && (srcPtr->height > 0)) {
                        XFillRectangle(graphPtr->display, drawable, penPtr->gc, srcPtr->x, srcPtr->y,
                                       (unsigned int)srcPtr->width, (unsigned int)srcPtr->height);
                    }
                }
            }
            if (nBuffered > 0) {
                Rbc_NativeBarRectangles(graphPtr, drawable, penPtr, xRects, nBuffered);
            }
            ckfree(xRects);
        }
    }
    if ((penPtr->border != NULL) && (penPtr->borderWidth > 0) && (penPtr->relief != TK_RELIEF_FLAT)) {
        if (!Rbc_NativeBarBorders(graphPtr, drawable, penPtr, rectangles, nRects)) {
            for (rectPtr = rectangles, endPtr = rectangles + nRects; rectPtr < endPtr; rectPtr++) {
                Rbc_Draw3DRectangle(graphPtr->tkwin, drawable, penPtr->border, rectPtr->x, rectPtr->y, rectPtr->width,
                                    rectPtr->height, penPtr->borderWidth, penPtr->relief);
            }
        }
    }
}


void Rbc_NativeArcStroke(Graph *graphPtr, Drawable drawable, GC gc,
    const Segment2D *segments, Tcl_Size count) {
    Point2D *points;
    Tcl_Size i, n;
    if (count == 0) return;
    points = Tcl_AttemptAlloc((size_t)(count + 1) * sizeof(*points));
    if (points == NULL) return;
    for (i = 0; i < count;) {
        n = 0;
        points[n++] = segments[i].p;
        do {
            points[n++] = segments[i++].q;
        } while (i < count && points[n-1].x == segments[i].p.x &&
                 points[n-1].y == segments[i].p.y);
        {
            XPoint *xp = Tcl_AttemptAlloc((size_t)n * sizeof(*xp));
            Tcl_Size j;
            if (xp == NULL) continue;
            for (j = 0; j < n; j++) {
                xp[j].x = (short)points[j].x;
                xp[j].y = (short)points[j].y;
            }
            XDrawLines(graphPtr->display, drawable, gc, xp, (int)n, CoordModeOrigin);
            ckfree(xp);
        }
    }
    ckfree(points);
}


/* Rectangular screen clipping. Scratch storage is reused at each nesting
 * depth. Seeding and copying only the visible rectangle preserves painter
 * order, XOR, bitmap masks and Tk's shared GCs on both platforms. */
typedef struct NativeClipFrame {
    struct NativeClipFrame *previous, *next;
    Drawable parent, drawable;
    Pixmap scratch;
    int left, top, right, bottom;
    int platformActive;
} NativeClipFrame;

struct Rbc_NativeClip {
    Graph *graphPtr;
    Drawable drawable;
    GC copyGC;
    int ownGC, useDC;
    NativeClipFrame *first, *active;
};

Rbc_NativeClip *Rbc_NativeCreateClip(Graph *graphPtr, Drawable drawable, GC copyGC, int useDC) {
    Rbc_NativeClip *clip = (Rbc_NativeClip *)ckalloc(sizeof(*clip));
    memset(clip, 0, sizeof(*clip));
    clip->graphPtr = graphPtr;
    clip->drawable = drawable;
    clip->copyGC = copyGC;
    clip->useDC = useDC;
    return clip;
}

Drawable Rbc_NativeClipDrawable(Rbc_NativeClip *clip) {
    return clip->active != NULL ? clip->active->drawable : clip->drawable;
}

void Rbc_NativePushClip(Rbc_NativeClip *clip, int x, int y, int width, int height) {
    Graph *graphPtr = clip->graphPtr;
    NativeClipFrame *parent = clip->active;
    NativeClipFrame *frame = parent != NULL ? parent->next : clip->first;
    if (frame == NULL) {
        frame = (NativeClipFrame *)ckalloc(sizeof(*frame) + Rbc_NativeClipStateSize());
        memset(frame, 0, sizeof(*frame));
        frame->previous = parent;
        if (parent != NULL) parent->next = frame;
        else clip->first = frame;
    }
    frame->parent = Rbc_NativeClipDrawable(clip);
    frame->drawable = None;
    clip->active = frame;
    frame->left = MAX(x, parent != NULL ? parent->left : 0);
    frame->top = MAX(y, parent != NULL ? parent->top : 0);
    /* Compute endpoints in a wide type before clamping to the drawable. */
    frame->right = (int)MIN((Tcl_WideInt)x + MAX(0, width),
                            parent != NULL ? parent->right : graphPtr->width);
    frame->bottom = (int)MIN((Tcl_WideInt)y + MAX(0, height),
                             parent != NULL ? parent->bottom : graphPtr->height);
    if (frame->parent == None || frame->right <= frame->left || frame->bottom <= frame->top) return;
    if (clip->useDC) {
        frame->platformActive = Rbc_NativePlatformClip(graphPtr, frame->parent,
            frame->left, frame->top, frame->right, frame->bottom,
            frame + 1, &frame->drawable);
        if (frame->platformActive) return;
    }
    if (frame->scratch == None) {
        frame->scratch = Tk_GetPixmap(graphPtr->display, frame->parent, graphPtr->width, graphPtr->height,
                                       Tk_Depth(graphPtr->tkwin));
    }
    if (frame->scratch == None) return;
    if (clip->copyGC == NULL) {
        XGCValues values;
        values.graphics_exposures = False;
        clip->copyGC = Tk_GetGC(graphPtr->tkwin, GCGraphicsExposures, &values);
        clip->ownGC = 1;
    }
    XCopyArea(graphPtr->display, frame->parent, frame->scratch, clip->copyGC,
              frame->left, frame->top, frame->right-frame->left, frame->bottom-frame->top,
              frame->left, frame->top);
    frame->drawable = frame->scratch;
}

void Rbc_NativePopClip(Rbc_NativeClip *clip) {
    NativeClipFrame *frame = clip->active;
    Graph *graphPtr = clip->graphPtr;
    if (frame == NULL) return;
    if (frame->platformActive) {
        Rbc_NativePlatformUnclip(frame + 1, frame->drawable);
        frame->platformActive = FALSE;
    } else
    if (frame->drawable != None) {
        XCopyArea(graphPtr->display, frame->scratch, frame->parent, clip->copyGC,
                  frame->left, frame->top, frame->right-frame->left, frame->bottom-frame->top,
                  frame->left, frame->top);
    }
    frame->drawable = None;
    clip->active = frame->previous;
}

void Rbc_NativeDestroyClip(Rbc_NativeClip *clip) {
    NativeClipFrame *frame, *next;
    while (clip->active != NULL) Rbc_NativePopClip(clip);
    for (frame = clip->first; frame != NULL; frame = next) {
        next = frame->next;
        if (frame->scratch != None) Tk_FreePixmap(clip->graphPtr->display, frame->scratch);
        ckfree(frame);
    }
    if (clip->ownGC) Tk_FreeGC(clip->graphPtr->display, clip->copyGC);
    ckfree(clip);
}

void Rbc_NativeScreenSegments(Tk_Window tkwin, Drawable drawable, GC gc,
    const Segment2D *segments, Tcl_Size count) {
    Rbc_Draw2DSegments(Tk_Display(tkwin), drawable, gc, segments, count);
}
