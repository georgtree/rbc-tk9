/* Native renderer implementation details; not installed. See license.terms. */
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
Rbc_NativeTrace *Rbc_NativeBeginTrace(Graph *, Drawable, GC);
void Rbc_NativePolyline(Rbc_NativeTrace *, const Point2D *, Tcl_Size);
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
Rbc_NativeClip *Rbc_NativeCreateClip(Graph *, Drawable, GC, int);
void Rbc_NativePushClip(Rbc_NativeClip *, int, int, int, int);
void Rbc_NativePopClip(Rbc_NativeClip *);
Drawable Rbc_NativeClipDrawable(Rbc_NativeClip *);
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
