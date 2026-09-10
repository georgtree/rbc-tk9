/* Private graph rendering interface. See license.terms for details. */
#ifndef RBC_RENDER_H
#define RBC_RENDER_H

#include "rbcGraph.h"

typedef struct Rbc_RenderContext Rbc_RenderContext;

Rbc_RenderContext *Rbc_RenderBegin(Graph *graphPtr, Drawable drawable,
                                   const XColor *colorPtr, double width,
                                   const Rbc_Dashes *dashesPtr, const XColor *offColorPtr);
void Rbc_RenderPolyline(Rbc_RenderContext *ctx, const Point2D *points, Tcl_Size count);
void Rbc_RenderSegments(Rbc_RenderContext *ctx, const Segment2D *segments, Tcl_Size count);
void Rbc_RenderEnd(Rbc_RenderContext *ctx);

#endif
