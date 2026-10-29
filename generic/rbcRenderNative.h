/* Native renderer implementation details; not installed. See license.terms. */
#ifndef RBC_RENDER_NATIVE_H
#define RBC_RENDER_NATIVE_H
#include "rbcRender.h"

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
#endif
