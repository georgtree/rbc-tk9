/* Native renderer implementation details; not installed. See license.terms. */
/** @file rbcRenderNative.h
 * Private native backend services; component callers use rbcRender.h.
 */
#ifndef RBC_RENDER_NATIVE_H
#define RBC_RENDER_NATIVE_H
#include "rbcRender.h"

Tcl_Size Rbc_NativePolygonPointLimit(Graph *graphPtr);

void Rbc_NativeText(Tk_Window, Drawable, const char *, TextStyle *, int, int);
void Rbc_NativeTextLayout(Tk_Window, Drawable, TextLayout *, TextStyle *, int, int);
void Rbc_NativeTextSize(Tk_Window, Drawable, const char *, TextStyle *, int, int, Dim2D *);

void Rbc_NativeBitmap(Graph *, Drawable, const Rbc_RenderRectangle *, const Rbc_RenderBitmapStyle *);
void Rbc_NativeImage(Drawable, Tk_Image, int, int);
void Rbc_NativeCopyArea(Tk_Window, Drawable, Drawable, GC, int, int, unsigned int, unsigned int, int, int);
void Rbc_NativePixmap(Graph *, Drawable, Pixmap, GC, const Rbc_RenderRectangle *);

void Rbc_NativeBorder(Tk_Window, Drawable, Tk_3DBorder, int, int, int, int, int, int, int);
void Rbc_NativeRectangle(Tk_Window, Drawable, GC, Rbc_Tile, int, int, int, int);
void Rbc_NativeScreenSegments(Tk_Window, Drawable, GC, const Segment2D *, Tcl_Size);
void Rbc_NativeFocus(Tk_Window, Drawable, GC, int);

typedef struct Rbc_NativeTrace Rbc_NativeTrace;
/**
 * @brief Allocate a reusable native polyline batch.
 * @return Owned context, or NULL when request capacity or allocation prevents it.
 * @details Borrows graph, drawable and configured GC until Rbc_NativeEndTrace.
 * Owns point workspace; Windows also acquires a DC and selects owned pen/brush
 * resources for the batch. Geometry supplied to subsequent calls is consumed
 * synchronously. This is a backend service, not a component constructor.
 */
Rbc_NativeTrace *Rbc_NativeBeginTrace(Graph *, Drawable, GC);
void Rbc_NativePolyline(Rbc_NativeTrace *, const Point2D *, Tcl_Size);
/**
 * @brief Release a native trace batch; NULL is accepted.
 * @details Windows restores the previously selected pen/brush before deleting
 * the batch-owned objects and releasing its DC state. Frees context/workspace
 * without freeing the borrowed GC, drawable or graph.
 */
void Rbc_NativeEndTrace(Rbc_NativeTrace *);
void Rbc_NativeStrips(Graph *, Drawable, GC, const Segment2D *, Tcl_Size);
void Rbc_NativeSegments(Graph *, Drawable, GC, const Segment2D *, Tcl_Size);

typedef struct Rbc_NativePolygonBatch Rbc_NativePolygonBatch;
void Rbc_NativeSymbolPoints(Graph *, Drawable, Rbc_RenderNativeSymbolStyle *, int, Tcl_Size, const Point2D *);
void Rbc_NativeBars(Graph *, Drawable, const Rbc_RenderBarStyle *, const Rbc_RenderRectangle *, Tcl_Size);
int Rbc_NativePolygonPointCount(Display *, Tcl_Size);
void Rbc_NativeFillPolygon(Graph *, Drawable, GC, Rbc_Tile, Pixmap, const Point2D *, Tcl_Size);
void Rbc_NativeArcStroke(Graph *, Drawable, GC, const Segment2D *, Tcl_Size);
Rbc_NativePolygonBatch *Rbc_NativeBeginPolygonBatch(Graph *, Drawable, GC, Tcl_Size);
void Rbc_NativeBatchPolygon(Rbc_NativePolygonBatch *, const Point2D *, Tcl_Size);
void Rbc_NativeEndPolygonBatch(Rbc_NativePolygonBatch *);
typedef struct Rbc_NativeClip Rbc_NativeClip;
/**
 * @brief Create a native rectangular clip stack without pushing a clip.
 * @return Owned stack, to be released with Rbc_NativeDestroyClip.
 * @details Borrows graph, base drawable and any supplied copy GC. A missing copy
 * GC is acquired lazily and owned by the stack. A nonzero final argument permits
 * platform DC clipping; otherwise use scratch targets. Keep graph dimensions
 * stable while the stack is live: scratch pixmaps are cached per nesting depth.
 */
Rbc_NativeClip *Rbc_NativeCreateClip(Graph *, Drawable, GC, int);
/**
 * @brief Push an intersection with the parent clip and graph drawable bounds.
 * @details Arguments after the stack are x, y, width and height in absolute
 * widget pixels. Nonpositive sizes create an empty frame. Query
 * Rbc_NativeClipDrawable after every push; None means empty or failed output,
 * and drawing must be skipped. Every push still requires a pop. A scratch
 * target is seeded from its parent and preserves absolute coordinates.
 */
void Rbc_NativePushClip(Rbc_NativeClip *, int, int, int, int);
/**
 * @brief Finish the active clip and restore its parent target.
 * @details Copies the visible scratch rectangle back, or restores/releases
 * platform DC state. An empty stack is a no-op. Frame/workspace allocations
 * are retained for reuse until destruction; borrowed drawables are not freed.
 */
void Rbc_NativePopClip(Rbc_NativeClip *);
Drawable Rbc_NativeClipDrawable(Rbc_NativeClip *);
/**
 * @brief Unwind and free a live native clip stack.
 * @details Pops remaining frames (committing their output), frees all cached
 * scratch pixmaps and frame records, and releases only an internally acquired
 * copy GC. Requires a non-NULL stack and live borrowed graph/display resources.
 */
void Rbc_NativeDestroyClip(Rbc_NativeClip *);

/* Platform services called once per mapped batch, never once per point. */
void Rbc_NativeCircles(Display *, Drawable, Rbc_RenderNativeSymbolStyle *, Tcl_Size, const Point2D *, int);
void Rbc_NativeSquares(Display *, Drawable, Rbc_RenderNativeSymbolStyle *, Tcl_Size, const Point2D *, int);
void Rbc_NativePolygonSymbols(Display *, Drawable, Rbc_RenderNativeSymbolStyle *, const XPoint *, Tcl_Size, int, int);
void Rbc_NativeBarRectangles(Graph *, Drawable, const Rbc_RenderBarStyle *, XRectangle *, int);
int Rbc_NativeBarBorders(Graph *, Drawable, const Rbc_RenderBarStyle *, const Rbc_RenderRectangle *, Tcl_Size);
size_t Rbc_NativeClipStateSize(void);
int Rbc_NativePlatformClip(Graph *, Drawable, int, int, int, int, void *, Drawable *);
void Rbc_NativePlatformUnclip(void *, Drawable);
void Rbc_NativeTextBackground(Graph *, Drawable, GC, const Point2D *, const Point2D *);
void Rbc_NativeArrowHead(Graph *, Drawable, GC, const Point2D *);
void Rbc_NativeLegendLine(Graph *, Drawable, GC, int, int, int);
void Rbc_NativeLegendBar(Graph *, Drawable, GC, int, int, int);

/* Runs are synchronous: callback/data/positions are borrowed only until return.
 * Run owns a shared platform target when available, otherwise uses the original
 * drawable. BitmapRun requires an opaque, unmasked GXcopy-compatible bitmap. */
void Rbc_NativeRun(Graph *, Drawable, void *, Tcl_Size, Rbc_RenderRunProc *);
void Rbc_NativeBitmapRun(Graph *, Drawable, Pixmap, GC, int, int, const Rbc_RenderPosition *, Tcl_Size);
int Rbc_NativeOpaqueBitmapRun(Graph *, Drawable, Pixmap, GC, int, int, const Rbc_RenderPosition *, Tcl_Size);
#endif
