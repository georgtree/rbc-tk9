/* rbcRenderNative.c -- Native screen geometry and batching.
 * See license.terms for copyright and redistribution terms.
 *
 * Geometry is already mapped by the components. GCs, tiles and bitmap resources
 * are borrowed; the existing integer conversions and platform batching limits
 * are deliberately retained. */
#include "rbcRenderNative.h"
/** @file rbcRenderNative.c
 * Shared native drawing, geometry conversion and rectangular clip storage.
 */
/** @defgroup native_shared_drawing Shared native drawing helpers
 * Private backend services operating on already mapped geometry.
 */
#include <stdint.h>
#include <X11/Xutil.h>

/**
 * @brief Draw independent segments in bounded X requests.
 * @ingroup native_shared_drawing
 *
 * Borrows the display, drawable, GC and input array. Ignores absent display/
 * segments or nonpositive counts. Allocates one request-sized XSegment buffer;
 * clips endpoints to signed-short protocol bounds before truncating to short.
 * Returns without drawing if capacity or allocation fails; frees workspace.
 * This protocol clipping does not replace the owning component plot clip.
 */
static void DrawSegments(Display *display, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size nSegments) {
    XSegment *xSegments;
    Extents2D protocolExtents;
    int maxSegments;

    if ((display == NULL) || (segments == NULL) || (nSegments <= 0)) {
        return;
    }
    maxSegments = Rbc_MaxRequestSize(display, sizeof(XSegment));
    if (maxSegments < 1) {
        return;
    }
    xSegments = Tcl_AttemptAlloc((size_t)maxSegments * sizeof(*xSegments));
    if (xSegments == NULL) {
        return;
    }
    /*
     * X11 PolySegment coordinates are signed 16-bit values.
     * Clip geometry to that representable region before converting
     * Point2D coordinates to XSegment coordinates.
     */
    protocolExtents.left = (double)SHRT_MIN;
    protocolExtents.right = (double)SHRT_MAX;
    protocolExtents.top = (double)SHRT_MIN;
    protocolExtents.bottom = (double)SHRT_MAX;
    while (nSegments > 0) {
        Tcl_Size remaining;
        int chunk;
        int nDrawn;
        int i;

        remaining = nSegments;
        chunk = (remaining > (Tcl_Size)maxSegments) ? maxSegments : (int)remaining;
        nDrawn = 0;
        for (i = 0; i < chunk; i++) {
            const Segment2D *segPtr;

            segPtr = segments + i;
            /*
             * Most graph geometry is already clipped to the graph extents
             * before it reaches this routine.  Avoid running the general
             * line-rectangle clipper again when both endpoints are already
             * representable by the signed 16-bit XSegment protocol type.
             */
            if ((segPtr->p.x >= protocolExtents.left) && (segPtr->p.x <= protocolExtents.right) &&
                (segPtr->p.y >= protocolExtents.top) && (segPtr->p.y <= protocolExtents.bottom) &&
                (segPtr->q.x >= protocolExtents.left) && (segPtr->q.x <= protocolExtents.right) &&
                (segPtr->q.y >= protocolExtents.top) && (segPtr->q.y <= protocolExtents.bottom)) {
                xSegments[nDrawn].x1 = (short)segPtr->p.x;
                xSegments[nDrawn].y1 = (short)segPtr->p.y;
                xSegments[nDrawn].x2 = (short)segPtr->q.x;
                xSegments[nDrawn].y2 = (short)segPtr->q.y;
                nDrawn++;
            } else {
                Segment2D clipped;

                /*
                 * Retain the existing clipping behavior for geometry outside
                 * the X11 signed-16-bit protocol coordinate range.
                 */
                if (!Rbc_LineRectClip(&protocolExtents, &segPtr->p, &segPtr->q, &clipped)) {
                    continue;
                }
                xSegments[nDrawn].x1 = (short)clipped.p.x;
                xSegments[nDrawn].y1 = (short)clipped.p.y;
                xSegments[nDrawn].x2 = (short)clipped.q.x;
                xSegments[nDrawn].y2 = (short)clipped.q.y;
                nDrawn++;
            }
        }
        if (nDrawn > 0) {
            XDrawSegments(display, drawable, gc, xSegments, nDrawn);
        }
        segments += chunk;
        nSegments -= chunk;
    }
    ckfree(xSegments);
}

/**
 * @brief Fill a rectangle with the historical native relief adjustment.
 * @ingroup native_shared_drawing
 *
 * Borrows the window and border. For raised/sunken borders wider than one
 * pixel and rectangles larger than two pixels, draws the outer four lines,
 * shrinks the rectangle and delegates the remainder to Tk. X11 uses the
 * default GC for one outer shade; Windows uses the corresponding border GC.
 * Does not allocate or release the borrowed border GCs.
 */
static void Fill3DRectangle(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y, int width, int height,
                         int borderWidth, int relief) {
    if ((borderWidth > 1) && (width > 2) && (height > 2) &&
        ((relief == TK_RELIEF_SUNKEN) || (relief == TK_RELIEF_RAISED))) {
        GC lightGC, darkGC;
        int x2, y2;

        x2 = x + width - 1;
        y2 = y + height - 1;
        if (relief == TK_RELIEF_RAISED) {
            lightGC = Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC);
#ifdef WIN32
            darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_DARK_GC);
#else
            darkGC = DefaultGC(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
#endif
        } else {
#ifdef WIN32
            lightGC = Tk_3DBorderGC(tkwin, border, TK_3D_LIGHT_GC);
#else
            lightGC = DefaultGC(Tk_Display(tkwin), Tk_ScreenNumber(tkwin));
#endif
            darkGC = Tk_3DBorderGC(tkwin, border, TK_3D_FLAT_GC);
        }
        XDrawLine(Tk_Display(tkwin), drawable, lightGC, x, y, x2, y);
        XDrawLine(Tk_Display(tkwin), drawable, darkGC, x2, y2, x2, y);
        XDrawLine(Tk_Display(tkwin), drawable, darkGC, x2, y2, x, y2);
        XDrawLine(Tk_Display(tkwin), drawable, lightGC, x, y, x, y2);
        x++;
        y++;
        width -= 2;
        height -= 2;
        borderWidth--;
    }
    Tk_Fill3DRectangle(tkwin, drawable, border, x, y, width, height, borderWidth, relief);
}

/**
 * @brief Draw text through the existing Tk-based text service.
 * @ingroup native_shared_drawing
 *
 * Borrows string and style for the call. Retains native font selection,
 * rotation, anchors, padding and active/disabled styling in Rbc_DrawText.
 * No text resource is retained by this wrapper.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] string Borrowed NUL-terminated text, consumed during this call.
 * @param[in,out] style Borrowed text style with live font/GC resources; the text service may refresh its GC state.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 */
void Rbc_NativeText(Tk_Window tkwin, Drawable drawable, const char *string, TextStyle *style, int x, int y) {
    Rbc_DrawText(tkwin, drawable, string, style, x, y);
}

/**
 * @brief Draw an already constructed text layout.
 * @ingroup native_shared_drawing
 *
 * Borrows layout and style; delegates to Rbc_DrawTextLayout without rebuilding
 * or freeing the cached layout.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] layout Borrowed cached text layout; no ownership transfer.
 * @param[in,out] style Borrowed text style with live font/GC resources; the text service may refresh its GC state.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 */
void Rbc_NativeTextLayout(Tk_Window tkwin, Drawable drawable, TextLayout *layout, TextStyle *style, int x, int y) {
    Rbc_DrawTextLayout(tkwin, drawable, layout, style, x, y);
}

/**
 * @brief Draw text and return its rotated dimensions.
 * @ingroup native_shared_drawing
 *
 * Delegates to Rbc_DrawText2 using borrowed text/style and caller-provided
 * size storage. Axis-limit placement uses these dimensions for later labels.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] string Borrowed NUL-terminated text, consumed during this call.
 * @param[in,out] style Borrowed text style with live font/GC resources; the text service may refresh its GC state.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 * @param[out] size Required caller-owned storage receiving the rotated text extent.
 */
void Rbc_NativeTextSize(Tk_Window tkwin, Drawable drawable, const char *string, TextStyle *style, int x, int y,
                        Dim2D *size) {
    Rbc_DrawText2(tkwin, drawable, string, style, x, y, size);
}

/**
 * @brief Paint a mapped bitmap and optional background polygon.
 * @ingroup native_shared_drawing
 *
 * Borrows mapped rectangle and style resources. Converts the supplied bounded
 * background point array to short coordinates and fills it as Convex. Installs
 * the bitmap mask and destination-relative clip origin on the private bitmap
 * GC, then copies plane one. Leaves that clip state installed; does not retain
 * or free bitmap/mask resources. Caller must honor the fixed outline capacity.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] r Borrowed destination rectangle; its dimensions select the source extent starting at (0,0).
 * @param[in] style Borrowed bitmap style; backgroundCount must fit RBC_RENDER_BITMAP_OUTLINE_POINTS. Its private GC clip state is modified.
 */
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
        XFillPolygon(graphPtr->display, drawable, style->fillGC, polygon, (int)style->backgroundCount, Convex,
                     CoordModeOrigin);
    }
    XSetClipMask(graphPtr->display, style->gc, style->mask);
    if (style->mask != None) {
        XSetClipOrigin(graphPtr->display, style->gc, r->x, r->y);
    } else {
        XSetClipOrigin(graphPtr->display, style->gc, 0, 0);
    }
    XCopyPlane(graphPtr->display, style->bitmap, drawable, style->gc, 0, 0, r->width, r->height, r->x, r->y, 1);
}

/**
 * @brief Redraw a complete Tk image at a mapped position.
 * @ingroup native_shared_drawing
 *
 * Queries the borrowed image size and delegates image-type drawing and alpha
 * handling to Tk_RedrawImage. Does not scale, retain or free the image.
 *
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] image Borrowed live Tk image; its entire current extent is redrawn.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 */
void Rbc_NativeImage(Drawable drawable, Tk_Image image, int x, int y) {
    int width, height;

    Tk_SizeOfImage(image, &width, &height);
    Tk_RedrawImage(image, 0, 0, width, height, drawable, x, y);
}

/**
 * @brief Copy a native drawable rectangle using the supplied GC.
 * @ingroup native_shared_drawing
 *
 * Borrows both drawables, window and GC; passes source/destination coordinates
 * and unsigned dimensions to XCopyArea. Does not change the supplied GC or
 * introduce a separate plot clip.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] source Borrowed source drawable compatible with destination and gc.
 * @param[in] destination Borrowed destination drawable.
 * @param[in] gc Borrowed configured drawing GC, including raster operation, clip and style.
 * @param[in] srcX Source rectangle X coordinate in pixels.
 * @param[in] srcY Source rectangle Y coordinate in pixels.
 * @param[in] width Nonnegative rectangle width in pixels.
 * @param[in] height Nonnegative rectangle height in pixels.
 * @param[in] dstX Destination rectangle X coordinate in pixels.
 * @param[in] dstY Destination rectangle Y coordinate in pixels.
 */
void Rbc_NativeCopyArea(Tk_Window tkwin, Drawable source, Drawable destination, GC gc, int srcX, int srcY,
                        unsigned int width, unsigned int height, int dstX, int dstY) {
    XCopyArea(Tk_Display(tkwin), source, destination, gc, srcX, srcY, width, height, dstX, dstY);
}

/**
 * @brief Copy a pixmap from its origin into a mapped rectangle.
 * @ingroup native_shared_drawing
 *
 * Borrows graph, pixmap and GC. Uses rectangle dimensions and destination
 * position with Rbc_NativeCopyArea; the pixmap is neither scaled nor freed.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] pixmap Borrowed source pixmap, readable from (0,0) over the supplied rectangle dimensions.
 * @param[in] gc Borrowed configured drawing GC, including raster operation, clip and style.
 * @param[in] r Borrowed destination rectangle; its dimensions select the source extent starting at (0,0).
 */
void Rbc_NativePixmap(Graph *graphPtr, Drawable drawable, Pixmap pixmap, GC gc, const Rbc_RenderRectangle *r) {
    Rbc_NativeCopyArea(graphPtr->tkwin, pixmap, drawable, gc, 0, 0, r->width, r->height, r->x, r->y);
}

/**
 * @brief Draw or fill a native relief border.
 * @ingroup native_shared_drawing
 *
 * The fill flag chooses Fill3DRectangle; otherwise uses Rbc_Draw3DRectangle.
 * Borrows window/border and preserves the two paths' different fill semantics.
 * Decorations may use this service even when element geometry uses Cairo.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] border Borrowed Tk border supplying relief shading resources.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 * @param[in] width Nonnegative rectangle width in pixels.
 * @param[in] height Nonnegative rectangle height in pixels.
 * @param[in] borderWidth Relief border thickness in pixels.
 * @param[in] relief Tk relief constant.
 * @param[in] fill Nonzero fills the interior as well as drawing the relief; zero draws only the border.
 */
void Rbc_NativeBorder(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y, int width, int height,
                      int borderWidth, int relief, int fill) {
    if (fill) {
        Fill3DRectangle(tkwin, drawable, border, x, y, width, height, borderWidth, relief);
    } else {
        Rbc_Draw3DRectangle(tkwin, drawable, border, x, y, width, height, borderWidth, relief);
    }
}

/**
 * @brief Fill a native rectangle with a tile or the supplied GC.
 * @ingroup native_shared_drawing
 *
 * A non-NULL borrowed tile takes precedence and uses Rbc_TileRectangle with
 * its existing origin. Otherwise calls XFillRectangle after converting sizes
 * to unsigned integers. Callers supply valid nonnegative dimensions.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] gc Borrowed fill GC, required only when tile is NULL.
 * @param[in] tile Optional borrowed tile taking precedence over gc; retains its existing pattern origin.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 * @param[in] width Nonnegative rectangle width in pixels.
 * @param[in] height Nonnegative rectangle height in pixels.
 */
void Rbc_NativeRectangle(Tk_Window tkwin, Drawable drawable, GC gc, Rbc_Tile tile, int x, int y, int width,
                         int height) {
    if (tile != NULL) {
        Rbc_TileRectangle(tkwin, drawable, tile, x, y, width, height);
    } else {
        XFillRectangle(Tk_Display(tkwin), drawable, gc, x, y, (unsigned int)width, (unsigned int)height);
    }
}

/**
 * @brief Draw the Tk focus highlight.
 * @ingroup native_shared_drawing
 *
 * Borrows the live window and configured GC; delegates width handling and
 * painting to Tk_DrawFocusHighlight.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] gc Borrowed configured drawing GC, including raster operation, clip and style.
 * @param[in] width Focus highlight thickness in pixels, passed to Tk.
 */
void Rbc_NativeFocus(Tk_Window tkwin, Drawable drawable, GC gc, int width) {
    Tk_DrawFocusHighlight(tkwin, gc, width, drawable);
}

/**
 * @brief Draw graph segments through the shared bounded segment helper.
 * @ingroup native_shared_drawing
 *
 * Borrows graph, drawable, GC and mapped segments. DrawSegments owns temporary
 * conversion storage and handles protocol clipping and request batching.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] gc Borrowed configured drawing GC, including raster operation, clip and style.
 * @param[in] segments Borrowed mapped endpoint pairs; readable for the supplied segment count.
 * @param[in] count Nonnegative number of Segment2D records, not endpoints.
 */
void Rbc_NativeSegments(Graph *graphPtr, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size count) {
    DrawSegments(graphPtr->display, drawable, gc, segments, count);
}

/**
 * @brief Round a representable symbol coordinate away from zero at ties.
 * @ingroup native_shared_drawing
 *
 * Adds or subtracts one half before conversion to int. Requires a finite
 * value whose adjusted result fits int; this is not a general range check.
 */
INLINE static int Round(double x) { return (int)(x + ((x < 0.0) ? -0.5 : 0.5)); }

#define DRAW_SYMBOL(style) (((style)->counter % (style)->interval) == 0)

#define MAX_DRAWLINES(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWPOLYGON(d) Rbc_MaxRequestSize(d, sizeof(XPoint))
#define MAX_DRAWSEGMENTS(d) Rbc_MaxRequestSize(d, sizeof(XSegment))
#define MAX_DRAWRECTANGLES(d) Rbc_MaxRequestSize(d, sizeof(XRectangle))
#define MAX_DRAWARCS(d) Rbc_MaxRequestSize(d, sizeof(XArc))

/**
 * @brief Draw mapped symbol centers using native shape batches.
 * @ingroup native_shared_drawing
 *
 * Borrows graph, points and style resources; shape-specific temporary buffers
 * and scaled bitmaps are released before return. For size below three, draws
 * all centers as points with fillGC and bypasses symbol type/interval handling.
 * Larger symbols use circle/square platform batches, segment crosses, polygon
 * templates or scaled bitmaps. Interval-aware branches advance style->counter
 * for examined centers, including skipped ones. Bitmap symbols mutate outline
 * GC clip state. Requires valid mapped coordinates and a nonnegative count.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in,out] style Borrowed symbol style; interval-aware branches advance its counter for examined centers.
 * @param[in] size Mapped symbol size in pixels; sizes below three select the point-only path.
 * @param[in] nSymbolPts Nonnegative number of mapped symbol centers.
 * @param[in] symbolPts Borrowed mapped center array with nSymbolPts entries.
 */
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
                    XCopyPlane(graphPtr->display, bitmap, drawable, style->outlineGC, 0, 0, bmWidth, bmHeight, x, y, 1);
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
                XCopyPlane(graphPtr->display, bitmap, drawable, style->outlineGC, 0, 0, bmWidth, bmHeight, x, y, 1);
            }
        }
        Tk_FreePixmap(graphPtr->display, bitmap);
        if (mask != None) {
            Tk_FreePixmap(graphPtr->display, mask);
        }
    } break;
    }
}

/**
 * @brief Fill a tiled or stippled native polygon.
 * @ingroup native_shared_drawing
 *
 * Rejects absent points, unavailable tile pixmaps and unsupported point counts.
 * Owns temporary short-coordinate points. A tile takes precedence and its origin
 * is reset through Rbc_SetTileOrigin before drawing. Otherwise a non-None
 * stipple selects Complex XFillPolygon using the already configured GC.
 * With neither tile nor stipple this helper intentionally draws nothing.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] gc Borrowed configured drawing GC, including raster operation, clip and style.
 * @param[in] tile Optional borrowed tile taking precedence; its origin is reset for the graph before drawing.
 * @param[in] stipple Non-None enables the configured GC polygon fill when no tile is supplied; None alone does not select solid filling.
 * @param[in] points Borrowed mapped polygon vertices with count entries.
 * @param[in] count Number of input records, not bytes.
 */
void Rbc_NativeFillPolygon(Graph *graphPtr, Drawable drawable, GC gc, Rbc_Tile tile, Pixmap stipple,
                           const Point2D *points, Tcl_Size count) {
    XPoint *nativePoints;
    Tcl_Size i;
    int n;
    if (points == NULL || (tile != NULL && Rbc_PixmapOfTile(tile) == None)) {
        return;
    }        
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

/**
 * @brief Fill mapped bar rectangles, then draw relief borders.
 * @ingroup native_shared_drawing
 *
 * Borrows graph, style and rectangles. Positive fillOpacity enables native
 * filling; this helper does not alpha-blend fractional opacity. Packs eligible
 * rectangles into request-sized XRectangle batches, flushing before wider
 * int-argument fallback calls. Attempts platform border batching before
 * per-rectangle relief fallback. Allocation/capacity failure in the fill path
 * returns before borders. Requires valid mapped geometry and configured GCs.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] penPtr Borrowed bar style and its live color, border and GC resources.
 * @param[in] rectangles Borrowed mapped rectangle array with the supplied rectangle count.
 * @param[in] nRects Number of rectangle records, not bytes.
 */
void Rbc_NativeBars(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr,
                    const Rbc_RenderRectangle *rectangles, Tcl_Size nRects) {
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

/**
 * @brief Join contiguous arc segments into native polylines.
 * @ingroup native_shared_drawing
 *
 * Requires nonnegative count, valid mapped segments and representable sizes.
 * Zero count is a no-op. Exact endpoint equality joins adjacent segments; gaps
 * start separate XDrawLines calls. Owns point and per-run XPoint buffers, with
 * short-coordinate conversion. Outer allocation failure skips all drawing;
 * per-run failure skips that run. Unlike DrawSegments, does not protocol-clip
 * or split a run to the X request-size limit.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] gc Borrowed configured drawing GC, including raster operation, clip and style.
 * @param[in] segments Borrowed mapped arc segments; consecutive equal endpoints join a run. Caller bounds each run for native conversion/request limits.
 * @param[in] count Nonnegative number of Segment2D records, not endpoints.
 */
void Rbc_NativeArcStroke(Graph *graphPtr, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size count) {
    Point2D *points;
    Tcl_Size i, n;
    if (count == 0) {
        return;
    }        
    points = Tcl_AttemptAlloc((size_t)(count + 1) * sizeof(*points));
    if (points == NULL) {
        return;
    }        
    for (i = 0; i < count;) {
        n = 0;
        points[n++] = segments[i].p;
        do {
            points[n++] = segments[i++].q;
        } while (i < count && points[n - 1].x == segments[i].p.x && points[n - 1].y == segments[i].p.y);
        {
            XPoint *xp = Tcl_AttemptAlloc((size_t)n * sizeof(*xp));
            Tcl_Size j;
            if (xp == NULL) {
                continue;
            }                
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
/** @brief Reusable clip depth with absolute bounds and optional scratch target.
 * Previous/next link owned frames. Parent/drawable are borrowed targets; scratch
 * is owned until stack destruction. Platform state follows the allocation.
 */
typedef struct NativeClipFrame {
    struct NativeClipFrame *previous, *next;
    Drawable parent, drawable;
    Pixmap scratch;
    int left, top, right, bottom;
    int platformActive;
} NativeClipFrame;

/** @brief Native clip stack borrowing a graph and base drawable.
 * Owns cached frames and only a lazily acquired copy GC. Active identifies the
 * current depth; dimensions and borrowed resources stay valid until destruction.
 */
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

/**
 * @brief Return the active clip target or the base drawable.
 * @ingroup native_shared_drawing
 *
 * Requires a live non-NULL stack. None means the active frame is empty or
 * failed; callers must skip drawing but still pop that frame. The target is
 * borrowed and must not outlive its frame/stack.
 *
 * @param[in] clip Live non-NULL clip stack with its borrowed resources intact.
 */
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
        if (parent != NULL) {
            parent->next = frame;
        } else {
            clip->first = frame;
        }
    }
    frame->parent = Rbc_NativeClipDrawable(clip);
    frame->drawable = None;
    clip->active = frame;
    frame->left = MAX(x, parent != NULL ? parent->left : 0);
    frame->top = MAX(y, parent != NULL ? parent->top : 0);
    /* Compute endpoints in a wide type before clamping to the drawable. */
    frame->right = (int)MIN((Tcl_WideInt)x + MAX(0, width), parent != NULL ? parent->right : graphPtr->width);
    frame->bottom = (int)MIN((Tcl_WideInt)y + MAX(0, height), parent != NULL ? parent->bottom : graphPtr->height);
    if (frame->parent == None || frame->right <= frame->left || frame->bottom <= frame->top) {
        return;
    }
    if (clip->useDC) {
        frame->platformActive = Rbc_NativePlatformClip(graphPtr, frame->parent, frame->left, frame->top, frame->right,
                                                       frame->bottom, frame + 1, &frame->drawable);
        if (frame->platformActive) {
            return;
        }
    }
    if (frame->scratch == None) {
        frame->scratch = Tk_GetPixmap(graphPtr->display, frame->parent, graphPtr->width, graphPtr->height,
                                      Tk_Depth(graphPtr->tkwin));
    }
    if (frame->scratch == None) {
        return;
    }
    if (clip->copyGC == NULL) {
        XGCValues values;
        values.graphics_exposures = False;
        clip->copyGC = Tk_GetGC(graphPtr->tkwin, GCGraphicsExposures, &values);
        clip->ownGC = 1;
    }
    XCopyArea(graphPtr->display, frame->parent, frame->scratch, clip->copyGC, frame->left, frame->top,
              frame->right - frame->left, frame->bottom - frame->top, frame->left, frame->top);
    frame->drawable = frame->scratch;
}

void Rbc_NativePopClip(Rbc_NativeClip *clip) {
    NativeClipFrame *frame = clip->active;
    Graph *graphPtr = clip->graphPtr;
    if (frame == NULL) {
        return;
    }        
    if (frame->platformActive) {
        Rbc_NativePlatformUnclip(frame + 1, frame->drawable);
        frame->platformActive = FALSE;
    } else if (frame->drawable != None) {
        XCopyArea(graphPtr->display, frame->scratch, frame->parent, clip->copyGC, frame->left, frame->top,
                  frame->right - frame->left, frame->bottom - frame->top, frame->left, frame->top);
    }
    frame->drawable = None;
    clip->active = frame->previous;
}

void Rbc_NativeDestroyClip(Rbc_NativeClip *clip) {
    NativeClipFrame *frame, *next;
    while (clip->active != NULL) {
        Rbc_NativePopClip(clip);
    }
    for (frame = clip->first; frame != NULL; frame = next) {
        next = frame->next;
        if (frame->scratch != None) {
            Tk_FreePixmap(clip->graphPtr->display, frame->scratch);
        }
        ckfree(frame);
    }
    if (clip->ownGC) {
        Tk_FreeGC(clip->graphPtr->display, clip->copyGC);
    }
    ckfree(clip);
}

/**
 * @brief Draw independent native segments without a Graph argument.
 * @ingroup native_shared_drawing
 *
 * Uses the borrowed window display and the same DrawSegments conversion,
 * protocol clipping, temporary storage and request batching as graph segments.
 *
 * @param[in] tkwin Borrowed live Tk window supplying display and drawing resources.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] gc Borrowed configured drawing GC, including raster operation, clip and style.
 * @param[in] segments Borrowed mapped endpoint pairs; readable for the supplied segment count.
 * @param[in] count Nonnegative number of Segment2D records, not endpoints.
 */
void Rbc_NativeScreenSegments(Tk_Window tkwin, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size count) {
    DrawSegments(Tk_Display(tkwin), drawable, gc, segments, count);
}

/**
 * @brief Scan-convert a five-vertex arrowhead at integer pixel centers.
 * @ingroup native_shared_drawing
 *
 * Borrows the mapped arrow and GC. Keeps doubles until selecting covered
 * pixels; rounding vertices first distorts narrow diagonal necks. Half-open
 * edges count shared vertices once. Clips spans to graph and signed-short
 * bounds, then sends batches of at most 128 one-pixel-high rectangles.
 * Requires finite geometry; the enclosing renderer retains track clipping.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] gc Borrowed configured drawing GC, including raster operation, clip and style.
 * @param[in] arrow Borrowed finite array of RBC_RENDER_ARROW_POINTS vertices, including the repeated closing vertex.
 */
void Rbc_NativeArrowHead(Graph *graphPtr, Drawable drawable, GC gc, const Point2D *arrow) {
    XRectangle spans[128];
    double top = arrow[0].y, bottom = arrow[0].y;
    int i, j, y, lastY, right, nSpans = 0;

    for (i = 1; i < RBC_RENDER_ARROW_POINTS - 1; i++) {
        top = MIN(top, arrow[i].y);
        bottom = MAX(bottom, arrow[i].y);
    }
    top = MAX(0.0, ceil(top));
    bottom = MIN((double)MIN(graphPtr->height - 1, SHRT_MAX), floor(bottom));
    right = MIN(graphPtr->width - 1, SHRT_MAX);
    if ((top > bottom) || (right < 0)) {
        return;
    }
    lastY = (int)bottom;
    for (y = (int)top; y <= lastY; y++) {
        double intersections[RBC_RENDER_ARROW_POINTS - 1];
        int count = 0;

        for (i = 0; i < RBC_RENDER_ARROW_POINTS - 1; i++) {
            const Point2D *p = arrow + i;
            const Point2D *q = arrow + ((i + 1) % (RBC_RENDER_ARROW_POINTS - 1));

            if (((p->y <= y) && (y < q->y)) || ((q->y <= y) && (y < p->y))) {
                double x = p->x + ((double)y - p->y) / (q->y - p->y) * (q->x - p->x);

                /* At most five crossings; insertion sort keeps pairs ordered. */
                for (j = count; (j > 0) && (intersections[j - 1] > x); j--) {
                    intersections[j] = intersections[j - 1];
                }
                intersections[j] = x;
                count++;
            }
        }
        for (i = 0; i + 1 < count; i += 2) {
            double left = MAX(0.0, ceil(intersections[i]));
            double end = MIN((double)right, ceil(intersections[i + 1]) - 1.0);

            if (left > end) {
                continue;
            }
            spans[nSpans].x = (short)left;
            spans[nSpans].y = (short)y;
            spans[nSpans].width = (unsigned short)(end - left + 1.0);
            spans[nSpans].height = 1;
            if (++nSpans == 128) {
                XFillRectangles(graphPtr->display, drawable, gc, spans, nSpans);
                nSpans = 0;
            }
        }
    }
    if (nSpans > 0) {
        XFillRectangles(graphPtr->display, drawable, gc, spans, nSpans);
    }
}

/**
 * @brief Fill a four-corner text background polygon.
 * @ingroup native_shared_drawing
 *
 * Borrows outline, anchor and GC. Adds the anchor before short conversion and
 * fills with Convex/CoordModeOrigin. Requires four mapped outline points;
 * does not impose a plot clip or retain point storage.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] gc Borrowed configured drawing GC, including raster operation, clip and style.
 * @param[in] outline Four borrowed text-background vertices relative to anchor.
 * @param[in] anchor Borrowed mapped text anchor added to each outline vertex before short conversion.
 */
void Rbc_NativeTextBackground(Graph *graphPtr, Drawable drawable, GC gc, const Point2D *outline,
                              const Point2D *anchor) {
    XPoint points[4];
    int i;
    for (i = 0; i < 4; i++) {
        points[i].x = (short int)(outline[i].x + anchor->x);
        points[i].y = (short int)(outline[i].y + anchor->y);
    }
    XFillPolygon(graphPtr->display, drawable, gc, points, 4, Convex, CoordModeOrigin);
}

/**
 * @brief Draw a two-row legend line sample.
 * @ingroup native_shared_drawing
 *
 * Uses two independent XDrawLine requests, preserving the original dash
 * restart on each row. Borrows graph, target and configured GC.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] gc Borrowed configured drawing GC, including raster operation, clip and style.
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 * @param[in] size Half-length of each horizontal sample line; rows are y and y+1.
 */
void Rbc_NativeLegendLine(Graph *graphPtr, Drawable drawable, GC gc, int x, int y, int size) {
    XDrawLine(graphPtr->display, drawable, gc, x - size, y, x + size, y);
    XDrawLine(graphPtr->display, drawable, gc, x - size, y + 1, x + size, y + 1);
}

/**
 * @brief Draw a square legend fill sample with a local pattern origin.
 * @ingroup native_shared_drawing
 *
 * Sets the borrowed GC tile/stipple origin to the sample position, fills the
 * square, then resets that origin to zero. Does not restore a nonzero previous
 * origin; callers must use the established legend GC contract.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in,out] gc Borrowed fill GC; stipple origin is set to (x,y) then reset to (0,0).
 * @param[in] x Destination X coordinate in pixels.
 * @param[in] y Destination Y coordinate in pixels.
 * @param[in] size Nonnegative side length of the filled sample square.
 */
void Rbc_NativeLegendBar(Graph *graphPtr, Drawable drawable, GC gc, int x, int y, int size) {
    XSetTSOrigin(graphPtr->display, gc, x, y);
    XFillRectangle(graphPtr->display, drawable, gc, x, y, size, size);
    XSetTSOrigin(graphPtr->display, gc, 0, 0);
}

/**
 * @brief Draw a run of opaque bitmap copies with platform fallback.
 * @ingroup native_shared_drawing
 *
 * Ignores absent positions or nonpositive counts. Tries the platform opaque
 * batch first, then clears GC clip mask/origin and replays all positions with
 * XCopyPlane if declined. Requires unmasked GXcopy-compatible input so replay
 * after partial optimized failure is safe. Borrows all resources and leaves
 * fallback GC clipping cleared; do not use for masked or XOR runs.
 *
 * @param[in] graphPtr Borrowed owning graph; its display and drawing resources must remain live during the call.
 * @param[in] drawable Borrowed destination drawable in mapped pixel coordinates.
 * @param[in] bitmap Borrowed one-bit source bitmap, readable over width by height pixels.
 * @param[in,out] gc Borrowed opaque GXcopy-compatible GC; shared fallback clears its mask and clip origin.
 * @param[in] width Positive source bitmap width in pixels.
 * @param[in] height Positive source bitmap height in pixels.
 * @param[in] positions Borrowed array of count integer destination origins.
 * @param[in] count Number of run items; nonpositive counts suppress drawing.
 */
void Rbc_NativeBitmapRun(Graph *graphPtr, Drawable drawable, Pixmap bitmap, GC gc, int width, int height,
                         const Rbc_RenderPosition *positions, Tcl_Size count) {
    Tcl_Size i;
    if (positions == NULL || count <= 0) {
        return;
    }
    if (Rbc_NativeOpaqueBitmapRun(graphPtr, drawable, bitmap, gc, width, height, positions, count)) {
        return;
    }
    /* The optimized path is GXcopy-equivalent. After a partial failure it is
     * safe to replay the complete run through the original XCopyPlane path. */
    XSetClipMask(graphPtr->display, gc, None);
    XSetClipOrigin(graphPtr->display, gc, 0, 0);
    for (i = 0; i < count; i++) {
        XCopyPlane(graphPtr->display, bitmap, drawable, gc, 0, 0, width, height, positions[i][0], positions[i][1], 1);
    }
}
