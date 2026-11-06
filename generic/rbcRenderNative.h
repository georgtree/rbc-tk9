/**
 * @file rbcRenderNative.h
 * @brief Private native backend services; component callers use rbcRender.h.
 *
 * Private header; not installed.
 *
 * See license.terms.
 */

#ifndef RBC_RENDER_NATIVE_H
#define RBC_RENDER_NATIVE_H
#include "rbcRender.h"

Tcl_Size Rbc_NativePolygonPointLimit(Graph *graphPtr);

void Rbc_NativeText(Tk_Window tkwin, Drawable drawable, const char *string, TextStyle *style, int x, int y);
void Rbc_NativeTextLayout(Tk_Window tkwin, Drawable drawable, TextLayout *layout, TextStyle *style, int x, int y);
void Rbc_NativeTextSize(Tk_Window tkwin, Drawable drawable, const char *string, TextStyle *style, int x, int y,
                        Dim2D *size);

void Rbc_NativeBitmap(Graph *graphPtr, Drawable drawable, const Rbc_RenderRectangle *r,
                      const Rbc_RenderBitmapStyle *style);
void Rbc_NativeImage(Drawable drawable, Tk_Image image, int x, int y);
void Rbc_NativeCopyArea(Tk_Window tkwin, Drawable source, Drawable destination, GC gc, int srcX, int srcY,
                        unsigned int width, unsigned int height, int dstX, int dstY);
void Rbc_NativePixmap(Graph *graphPtr, Drawable drawable, Pixmap pixmap, GC gc, const Rbc_RenderRectangle *r);

void Rbc_NativeBorder(Tk_Window tkwin, Drawable drawable, Tk_3DBorder border, int x, int y, int width, int height,
                      int borderWidth, int relief, int fill);
void Rbc_NativeRectangle(Tk_Window tkwin, Drawable drawable, GC gc, Rbc_Tile tile, int x, int y, int width,
                         int height);
void Rbc_NativeScreenSegments(Tk_Window tkwin, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size count);
void Rbc_NativeFocus(Tk_Window tkwin, Drawable drawable, GC gc, int width);

typedef struct Rbc_NativeTrace Rbc_NativeTrace;
/**
 * @brief Allocate a reusable native polyline batch.
 * @return Owned context, or NULL when request capacity or allocation prevents it.
 * @details Borrows graph, drawable and configured GC until Rbc_NativeEndTrace.
 * Owns point workspace; Windows also acquires a DC and selects owned pen/brush
 * resources for the batch. Geometry supplied to subsequent calls is consumed
 * synchronously. This is a backend service, not a component constructor.
 */
Rbc_NativeTrace *Rbc_NativeBeginTrace(Graph *graphPtr, Drawable drawable, GC gc);
void Rbc_NativePolyline(Rbc_NativeTrace *ctx, const Point2D *screenPts, Tcl_Size nPoints);
/**
 * @brief Release a native trace batch; NULL is accepted.
 * @details Windows restores the previously selected pen/brush before deleting
 * the batch-owned objects and releasing its DC state. Frees context/workspace
 * without freeing the borrowed GC, drawable or graph.
 */
void Rbc_NativeEndTrace(Rbc_NativeTrace *ctx);
void Rbc_NativeStrips(Graph *graphPtr, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size nSegments);
void Rbc_NativeSegments(Graph *graphPtr, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size count);

typedef struct Rbc_NativePolygonBatch Rbc_NativePolygonBatch;
void Rbc_NativeSymbolPoints(Graph *graphPtr, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, int size,
                            Tcl_Size nSymbolPts, const Point2D *symbolPts);
void Rbc_NativeBars(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr,
                    const Rbc_RenderRectangle *rectangles, Tcl_Size nRects);
int Rbc_NativePolygonPointCount(Display *display, Tcl_Size nPoints);
void Rbc_NativeFillPolygon(Graph *graphPtr, Drawable drawable, GC gc, Rbc_Tile tile, Pixmap stipple,
                           const Point2D *points, Tcl_Size count);
void Rbc_NativeArcStroke(Graph *graphPtr, Drawable drawable, GC gc, const Segment2D *segments, Tcl_Size count);
Rbc_NativePolygonBatch *Rbc_NativeBeginPolygonBatch(Graph *graphPtr, Drawable drawable, GC gc, Tcl_Size maxPoints);
void Rbc_NativeBatchPolygon(Rbc_NativePolygonBatch *batch, const Point2D *points, Tcl_Size count);
void Rbc_NativeEndPolygonBatch(Rbc_NativePolygonBatch *batch);
typedef struct Rbc_NativeClip Rbc_NativeClip;
/**
 * @brief Create a native rectangular clip stack without pushing a clip.
 * @return Owned stack, to be released with Rbc_NativeDestroyClip.
 * @details Borrows graph, base drawable and any supplied copy GC. A missing copy
 * GC is acquired lazily and owned by the stack. A nonzero final argument permits
 * platform DC clipping; otherwise use scratch targets. Keep graph dimensions
 * stable while the stack is live: scratch pixmaps are cached per nesting depth.
 *
 * @param[in] graphPtr Borrowed owning graph, kept alive until the returned context is released.
 * @param[in] drawable Borrowed target, kept valid until the returned context is released.
 * @param[in] copyGC Borrowed copy GC, or NULL to acquire one lazily and let the stack own it.
 * @param[in] useDC Nonzero permits platform DC clipping; zero selects scratch targets.
 */
Rbc_NativeClip *Rbc_NativeCreateClip(Graph *graphPtr, Drawable drawable, GC copyGC, int useDC);
/**
 * @brief Push an intersection with the parent clip and graph drawable bounds.
 * @details Arguments after the stack are x, y, width and height in absolute
 * widget pixels. Nonpositive sizes create an empty frame. Query
 * Rbc_NativeClipDrawable after every push; None means empty or failed output,
 * and drawing must be skipped. Every push still requires a pop. A scratch
 * target is seeded from its parent and preserves absolute coordinates.
 *
 * @param[in,out] clip Live non-NULL clip stack with its borrowed resources intact.
 * @param[in] x Absolute left edge in widget pixels.
 * @param[in] y Absolute top edge in widget pixels.
 * @param[in] width Clip width; nonpositive values create an empty frame.
 * @param[in] height Clip height; nonpositive values create an empty frame.
 */
void Rbc_NativePushClip(Rbc_NativeClip *clip, int x, int y, int width, int height);
/**
 * @brief Finish the active clip and restore its parent target.
 * @details Copies the visible scratch rectangle back, or restores/releases
 * platform DC state. An empty stack is a no-op. Frame/workspace allocations
 * are retained for reuse until destruction; borrowed drawables are not freed.
 *
 * @param[in,out] clip Live non-NULL clip stack with its borrowed resources intact.
 */
void Rbc_NativePopClip(Rbc_NativeClip *clip);
Drawable Rbc_NativeClipDrawable(Rbc_NativeClip *clip);
/**
 * @brief Unwind and free a live native clip stack.
 * @details Pops remaining frames (committing their output), frees all cached
 * scratch pixmaps and frame records, and releases only an internally acquired
 * copy GC. Requires a non-NULL stack and live borrowed graph/display resources.
 *
 * @param[in] clip Live non-NULL clip stack; destroyed by this call.
 */
void Rbc_NativeDestroyClip(Rbc_NativeClip *clip);

/* Platform services called once per mapped batch, never once per point. */
void Rbc_NativeCircles(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, Tcl_Size nSymbolPts,
                       const Point2D *symbolPts, int radius);
void Rbc_NativeSquares(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style, Tcl_Size nSymbolPts,
                       const Point2D *symbolPts, int r);
void Rbc_NativePolygonSymbols(Display *display, Drawable drawable, Rbc_RenderNativeSymbolStyle *style,
                              const XPoint *polygons, Tcl_Size nPolygons, int stride, int nVertices);
void Rbc_NativeBarRectangles(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr,
                             XRectangle *rectangles, int nRects);
int Rbc_NativeBarBorders(Graph *graphPtr, Drawable drawable, const Rbc_RenderBarStyle *penPtr,
                         const Rbc_RenderRectangle *rectangles, Tcl_Size nRects);
size_t Rbc_NativeClipStateSize(void);
int Rbc_NativePlatformClip(Graph *graphPtr, Drawable parent, int left, int top, int right, int bottom, void *clientData,
                           Drawable *drawablePtr);
void Rbc_NativePlatformUnclip(void *clientData, Drawable drawable);
void Rbc_NativeTextBackground(Graph *graphPtr, Drawable drawable, GC gc, const Point2D *outline,
                              const Point2D *anchor);
void Rbc_NativeArrowHead(Graph *graphPtr, Drawable drawable, GC gc, const Point2D *arrow);
void Rbc_NativeLegendLine(Graph *graphPtr, Drawable drawable, GC gc, int x, int y, int size);
void Rbc_NativeLegendBar(Graph *graphPtr, Drawable drawable, GC gc, int x, int y, int size);

/* Runs are synchronous: callback/data/positions are borrowed only until return.
 * Run owns a shared platform target when available, otherwise uses the original
 * drawable. BitmapRun requires an opaque, unmasked GXcopy-compatible bitmap. */
void Rbc_NativeRun(Graph *graphPtr, Drawable drawable, void *data, Tcl_Size count, Rbc_RenderRunProc *proc);
void Rbc_NativeBitmapRun(Graph *graphPtr, Drawable drawable, Pixmap bitmap, GC gc, int width, int height,
                         const Rbc_RenderPosition *positions, Tcl_Size count);
int Rbc_NativeOpaqueBitmapRun(Graph *graphPtr, Drawable drawable, Pixmap bitmap, GC gc, int width, int height,
                              const Rbc_RenderPosition *positions, Tcl_Size count);
#endif
